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
  has no information about.
* **No OpenUSD, no humanoid vocabulary, no stage.** Three numbers on an OFFSET
  line are not a vector in any basis this layer knows, and which joint is a
  `HumanBone` is a profile's answer.

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
    # `motionSource` is absent only because its extractor has not landed; every
    # other name here stays forbidden after it does.
    forbidden_api = re.compile(
        r"pxr/|PXR_NAMESPACE|\bGf(?:Vec|Quat|Matrix)|\bUsd[A-Z]|\bSdf[A-Z]|"
        r"TF_REGISTRY_FUNCTION|\bHumanBone\b|\bmotion::|motionCore|"
        r"vrmRetarget|vrmSchema")
    semantic = re.compile(r"DiagnosticCode::(?:" + "|".join(SEMANTIC_CODES) + r")\b")

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
            if path.name not in DIAGNOSTIC_SOURCES and semantic.search(code):
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
    # Only with a filter, though. Unfiltered, zero calls is this library's
    # actual and intended state -- `libs/motionBvh/CMakeLists.txt` has no
    # `target_link_libraries` line because the library links nothing at all --
    # and the binary import check below is what covers that claim.
    if arguments.cmake_target and not calls:
        # ASCII: this lands on a Windows console in CI, where a non-cp932
        # character comes out as a backslash escape and the sentence stops
        # reading as a sentence.
        errors.append(
            f"{label}: no target_link_libraries call for target "
            f"'{arguments.cmake_target}'; the OpenUSD link check examined "
            f"nothing, so --cmake-target names a target that is not there")
    for call in calls:
        if openusd_link.search(call):
            errors.append(f"{label} CMake must link no OpenUSD library")
            break

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
