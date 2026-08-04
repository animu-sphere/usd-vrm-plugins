#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Enforce motionBvh's boundaries: no producer, no semantics, no OpenUSD.

Three claims are checked, and each one is load-bearing rather than tidy:

* **No producer name in library code.** Bake one application's joint names,
  unit, axes or root convention into the parser and the second producer is not
  a new profile, it is a rewrite (roadmap/recorded-motion-sources.md §1).
* **The syntax layer raises no semantic diagnostic.** The five syntax codes are
  this library's; the six semantic ones belong to the layer where a document
  meets a profile, and a parser that raised one would be deciding something it
  has no information about. The extractor is granted exactly one of the six by
  name -- see `EXTRACTION_CODES` -- and the grant is narrow because the reason
  for it is: a joint declaring two rotation channels forms no Euler order
  whoever wrote the file, so that code needs no profile and the file that
  raises it holds none.
* **No OpenUSD, no humanoid vocabulary, no stage.** Three numbers on an OFFSET
  line are not a vector in any basis this layer knows, and which joint is a
  `HumanBone` is a profile's answer. The source rule covers the whole library;
  the *binary* rule below covers what the binary it is pointed at actually
  pulled in, and since the extractor landed those two are no longer the same
  claim -- see the note beside `binary` in tests/CMakeLists.txt.

It runs over `tools/motionBvh` as well as over the library, because a CLI that
named a producer would put the assumption one directory away from the layer that
forbids it and call the boundary kept. One script rather than two: a second copy
of the producer list is a second list to keep current.
"""

from __future__ import annotations

import argparse
import os
import pathlib
import re
import shutil
import subprocess
import sys

# Capture products, mocap vendors, and DCC applications whose BVH exports this
# pipeline is meant to read through a *profile*. Any of them appearing in
# include/ or src/ means a producer's answer has been written into the format.
PRODUCER_NAMES = [
    "mocopi", "sony", "rokoko", "xsens", "noitom", "perception neuron",
    "optitrack", "vicon", "qualisys", "motionbuilder", "motion builder",
    "autodesk", "blender", "maya", "3ds max", "unity", "unreal engine",
    "mixamo", "ardy", "vrchat", "vseeface", "waidayo",
]
# "unreal engine" rather than "unreal", because that one is an ordinary English
# adjective and the others are not. A bare "unreal" in a sentence is not a
# producer assumption; a bare "mocopi" always is.

# The semantic half of the frozen set. Definitions live in Diagnostics.cpp; no
# other source may name one.
SEMANTIC_CODES = [
    "ProfileRequired", "ProfileMismatch", "UnmappedJoint",
    "RequiredJointMissing", "InvalidRotationOrder", "InvalidRootPolicy",
]

DIAGNOSTIC_SOURCES = {"Diagnostics.h", "Diagnostics.cpp"}

# One file, one code, granted in review the way `motionSource` grants a file its
# crossing into canonical motion. `InvalidRotationOrder` is in the semantic half
# because most of what it can mean is -- but the extraction layer meets the half
# that a profile has nothing to say about: three rotation channels or none, and
# three distinct axes among them, are what a `SourceJointTrack` can hold. A
# second entry here, or a second code against this one, is a boundary moving and
# belongs in a review rather than in a diff nobody reads.
EXTRACTION_SOURCES = {"BvhExtract.h", "BvhExtract.cpp"}
EXTRACTION_CODES = {"InvalidRotationOrder"}


def _find_dumpbin() -> str | None:
    tool = shutil.which("dumpbin")
    if tool:
        return tool
    roots = [
        pathlib.Path(os.environ.get("ProgramFiles", r"C:\\Program Files")),
        pathlib.Path(os.environ.get("ProgramFiles(x86)", r"C:\\Program Files (x86)")),
    ]
    for root in roots:
        matches = sorted(root.glob(
            "Microsoft Visual Studio/2022/*/VC/Tools/MSVC/*/bin/Hostx64/x64/dumpbin.exe"),
            reverse=True)
        if matches:
            return str(matches[0])
    return None


def _binary_dependencies(binary: pathlib.Path) -> str:
    if sys.platform == "win32":
        tool = _find_dumpbin()
        if not tool:
            raise RuntimeError("dumpbin was not found")
        command = [tool, "/nologo", "/dependents", str(binary)]
    elif sys.platform == "darwin":
        tool = shutil.which("otool")
        if not tool:
            raise RuntimeError("otool was not found")
        command = [tool, "-L", str(binary)]
    else:
        tool = shutil.which("readelf")
        if not tool:
            raise RuntimeError("readelf was not found")
        command = [tool, "-d", str(binary)]
    return subprocess.run(
        command, check=True, text=True, encoding="utf-8", errors="replace",
        stdout=subprocess.PIPE).stdout


def strip_comments(text: str) -> str:
    """Blank out // and /* */ comments, keeping line structure.

    The API and diagnostic checks run over code rather than prose, because the
    clearest way to state that this layer has no `GfVec3f` is a comment saying
    so -- and a check that read comments would forbid the sentence explaining
    the rule. The producer-name check deliberately does *not* use this: a
    comment asserting what one application's export means is the assumption
    itself, wherever it is written.
    """
    out: list[str] = []
    index = 0
    while index < len(text):
        if text.startswith("//", index):
            end = text.find("\n", index)
            index = len(text) if end < 0 else end
        elif text.startswith("/*", index):
            end = text.find("*/", index + 2)
            body = text[index:len(text) if end < 0 else end + 2]
            out.append("\n" * body.count("\n"))
            index = len(text) if end < 0 else end + 2
        else:
            out.append(text[index])
            index += 1
    return "".join(out)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=pathlib.Path,
                        help="Directory whose include/ and src/ are checked.")
    parser.add_argument("binary", type=pathlib.Path,
                        help="An executable whose transitive imports must "
                             "contain no OpenUSD library.")
    parser.add_argument(
        "--cmake-target", default=None,
        help="Check only this target's link lines. The library's CMakeLists "
             "declares one target and needs no filter; the tools directory "
             "will also hold motion_bvh_convert, which links OpenUSD's stage "
             "libraries by design.")
    arguments = parser.parse_args()

    source = arguments.source.resolve()
    binary = arguments.binary.resolve()
    # "libs/motionBvh" or "tools/motionBvh" -- both directories are named
    # motionBvh, so the parent is what tells a failure apart.
    label = f"{source.parent.name}/{source.name}"
    errors: list[str] = []

    forbidden_files = {"openstrata.plugin.yaml", "pluginfo.json"}
    for path in source.rglob("*"):
        if path.is_file() and path.name.lower() in forbidden_files:
            errors.append(f"plugin registration file is forbidden: {path}")

    # Word boundaries are load-bearing, not decoration: without them "unity"
    # matches inside "community", "maya" inside "Mayan", and "unreal" inside
    # "unreal expectations". A check that fails on an ordinary English sentence
    # gets read as noise, and a check read as noise stops being enforcement.
    producers = re.compile(
        r"\b(?:" + "|".join(re.escape(n) for n in PRODUCER_NAMES) + r")\b",
        re.IGNORECASE)
    # OpenUSD in any form, the humanoid vocabulary, and the retarget layer.
    # `motionSource` is absent because the extractor is the declared edge; every
    # other name here stays forbidden with it in the tree, and `motionCore` most
    # of all: the edge is to one library, not through it to another.
    forbidden_api = re.compile(
        r"pxr/|PXR_NAMESPACE|\bGf(?:Vec|Quat|Matrix)|\bUsd[A-Z]|\bSdf[A-Z]|"
        r"TF_REGISTRY_FUNCTION|\bHumanBone\b|\bmotion::|motionCore|"
        r"vrmRetarget|vrmSchema")
    semantic = re.compile(r"DiagnosticCode::(?:" + "|".join(SEMANTIC_CODES) + r")\b")
    extraction_only = re.compile(
        r"DiagnosticCode::(?:"
        + "|".join(c for c in SEMANTIC_CODES if c not in EXTRACTION_CODES)
        + r")\b")

    for area in (source / "include", source / "src"):
        for path in sorted(area.rglob("*")):
            if not path.is_file():
                continue
            text = path.read_text(encoding="utf-8")
            found = producers.search(text)
            if found:
                errors.append(
                    f"producer name '{found.group(0)}' is forbidden in library "
                    f"code: {path}")
            code = strip_comments(text)
            found = forbidden_api.search(code)
            if found:
                errors.append(
                    f"'{found.group(0)}' is forbidden in this layer: {path}")
            if path.name in DIAGNOSTIC_SOURCES:
                pass
            elif path.name in EXTRACTION_SOURCES:
                found = extraction_only.search(code)
                if found:
                    errors.append(
                        f"the extraction layer raises '{found.group(0)}', which "
                        f"is not the one code it is granted: {path}")
            elif semantic.search(code):
                errors.append(
                    f"the syntax layer raises a semantic diagnostic: {path}")

    cmake = (source / "CMakeLists.txt").read_text(encoding="utf-8")
    target = re.escape(arguments.cmake_target) + r"\b" \
        if arguments.cmake_target else ""
    openusd_link = re.compile(r"\bgf\b|\busd|\bsdf|\bplug\b|pxr::",
                              re.IGNORECASE)
    calls = re.findall(r"target_link_libraries\(\s*" + target + r"[^)]*\)",
                       cmake)
    # A filter that matches nothing is the failure mode this check is least able
    # to survive: `re.findall` returns an empty list, the loop below never runs,
    # and a misspelled --cmake-target reports "boundary check passed" having
    # examined no link line at all. The whole check would then be one typo away
    # from silently not existing.
    #
    # Unfiltered, zero calls used to be this library's intended state and is no
    # longer: the extractor's edge to motionSource is declared in the descriptor
    # and has to be in the link line too, so an absent call now means the one
    # declared dependency is not actually linked.
    if not calls:
        # ASCII: this lands on a Windows console in CI, where a non-cp932
        # character comes out as a backslash escape and the sentence stops
        # reading as a sentence.
        errors.append(
            f"{label}: no target_link_libraries call"
            + (f" for target '{arguments.cmake_target}'; --cmake-target names "
               f"a target that is not there"
               if arguments.cmake_target else
               "; the link check examined nothing, so this library's one "
               "declared edge is unverified"))
    for call in calls:
        if openusd_link.search(call):
            errors.append(f"{label} CMake must link no OpenUSD library")
            break
    # The library's own edge, and only it. The tools directory is exempt because
    # a tool links what a tool needs -- `motion_bvh_convert` authors a stage --
    # and the --cmake-target filter is what keeps those apart.
    if not arguments.cmake_target:
        for call in calls:
            if "motionSource::motionSource" not in call:
                errors.append(
                    f"{label} links something other than motionSource: "
                    f"{call.strip()}")

    try:
        dependencies = _binary_dependencies(binary)
    except (OSError, RuntimeError, subprocess.CalledProcessError) as exc:
        errors.append(f"could not inspect {label} dependencies: {exc}")
        dependencies = ""
    forbidden_binary = re.compile(
        r"(?:usd_ms|usd_gf|lib(?:usd|sdf|plug|ar|gf)(?:[._-]|\.(?:dll|dylib|so)))",
        re.IGNORECASE)
    if forbidden_binary.search(dependencies):
        errors.append(f"{label} binary imports an OpenUSD library")

    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    print(f"{label} boundary check passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
