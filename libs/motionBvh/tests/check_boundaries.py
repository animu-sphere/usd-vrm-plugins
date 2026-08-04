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
* **No OpenUSD name, no humanoid vocabulary, no stage.** Three numbers on an
  OFFSET line are not a vector in any basis this layer knows, and which joint is
  a `HumanBone` is a profile's answer. This is a rule about *source*, and since
  the extractor landed it is the only form the rule can take -- see below.

**There is no binary import check here any more, and its removal is a
measurement rather than a concession.** Until the extractor, this script also
inspected a built executable and refused any OpenUSD library in its imports. The
declared `motionBvh -> motionSource` edge reaches `motionCore`, whose Gf value
types live in a shared library, so what that check now reports depends entirely
on the linker:

* MSVC pulls only the archive members that resolve a symbol, so an executable
  using the parser alone records no OpenUSD import;
* GNU ld with `--as-needed` -- the default on the distributions this builds on
  -- drops the resulting unused `DT_NEEDED` entries, and reports the same;
* Apple's ld64 records a load command for every dylib on the link line whether
  or not a symbol is referenced, and reports the opposite.

All three are correct about their own artifact. One source tree therefore
produces two different answers, and a check that passes on two platforms and
fails on the third is not measuring the property it names -- it is measuring the
linker. It cost a red macOS lane to find that out, on a claim that had been
verified on Windows and generalised. The source rule above is platform
independent and is what carries the claim now: no OpenUSD name appears in any
file of this library, extractor included, which is the thing a reviewer can act
on.

`motionSource`'s own check reached the same conclusion from the other end and
says so at length: in a monolithic OpenUSD build Gf and Sdf are the same
library, so no import listing could tell an allowed dependency from a forbidden
one even where the listing is stable.

It runs over `tools/motionBvh` as well as over the library, because a CLI that
named a producer would put the assumption one directory away from the layer that
forbids it and call the boundary kept. One script rather than two: a second copy
of the producer list is a second list to keep current.
"""

from __future__ import annotations

import argparse
import pathlib
import re
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
    parser.add_argument(
        "--cmake-target", default=None,
        help="Check only this target's link lines. The library's CMakeLists "
             "declares one target and needs no filter; the tools directory "
             "will also hold motion_bvh_convert, which links OpenUSD's stage "
             "libraries by design.")
    arguments = parser.parse_args()

    source = arguments.source.resolve()
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

    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    print(f"{label} boundary check passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
