#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Enforce motionSource's boundaries: no producer, no format, one crossing.

This layer is the one a second reader plugs into, and every claim below is what
makes that possible rather than merely intended:

* **No producer name.** The layer that decides what one application's export
  means is a declarative profile, never code. A producer name here is the same
  failure the reader below refuses, one level up where the profile machinery
  makes it look reasonable (roadmap/recorded-motion-sources.md §2).
* **No format name.** `motionSource` must not know that any particular reader
  exists. The arrow `motionBvh -> motionSource` never reverses, and the day this
  layer names a format is the day a second reader cannot be added without
  changing it (WORKSPACE.md §2). The `format` field carries a reader's label and
  is never compared to anything here, which is why the field is allowed and a
  literal is not.
* **The crossing into canonical motion is a list, and the list is short.** The
  model is expressed in the source's own basis, unit and angle convention, so it
  names no Gf type and no humanoid bone. Two files are permitted a canonical
  type -- `CanonicalMetadata`, which derives the canonical provenance, and
  `SourceProfile`, whose joint map has a `HumanBone` on its right-hand side --
  and the permitted set is a list in this script: a later file that needs the
  crossing adds itself here, in review, rather than acquiring it quietly.
* **No live-input and no target rig.** Live capture and recorded files meet at
  `motionCore` and nowhere earlier, and turning source motion onto a target rig
  is `vrmRetarget`'s job, once.

There is deliberately **no binary import check** here, unlike the reader's. This
library links `motionCore`, whose Gf value types make "imports no OpenUSD" false
by design — and in a monolithic OpenUSD build Gf and Sdf are the same library, so
no import listing could tell the allowed dependency from a forbidden one. The
CMake link line and the source rules above are what carry the claim.

Two limits, stated so the claims above are not read as wider than they are. Only
`include/`, `src/` and the library's own `CMakeLists.txt` are scanned, so a test
target that linked OpenUSD directly would not be caught here — the same shape the
reader's check has. And the CMake rule reads link *lines*: a dependency reached
through a variable rather than named in a `target_link_libraries` call would pass
it.
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys

# Capture products, mocap vendors, and DCC applications whose exports this
# pipeline reads through a *profile*. Any of them in include/ or src/ means a
# producer's answer has been written into the layer that must not hold one. Kept
# in step with libs/motionBvh/tests/check_boundaries.py: two lists that can
# disagree would let a name pass one layer and fail the next.
PRODUCER_NAMES = [
    "mocopi", "sony", "rokoko", "xsens", "noitom", "perception neuron",
    "optitrack", "vicon", "qualisys", "motionbuilder", "motion builder",
    "autodesk", "blender", "maya", "3ds max", "unity", "unreal engine",
    "mixamo", "ardy", "vrchat", "vseeface", "waidayo",
]

# Recorded-motion file formats and the readers that would own them. A comment
# naming one is as much a leak as an identifier: the sentence "the format is
# usually X" is the assumption itself.
FORMAT_NAMES = [
    "bvh", "motionbvh", "biovision", "fbx", "motionfbx", "gltf", "glb", "vrma",
    "c3d", "trc", "asf", "amc", "end site",
]

# The files permitted to name a canonical type. Add to this list in review when
# a new file genuinely needs the crossing. The converter will be the next one,
# because producing a `motion::HumanoidAnimation` is what it is for -- and it is
# the last one this library has a reason to grant.
CANONICAL_FILES = {
    "CanonicalMetadata.h", "CanonicalMetadata.cpp",
    "SourceProfile.h", "SourceProfile.cpp",
}


def strip_comments(text: str) -> str:
    """Blank out // and /* */ comments, keeping line structure.

    The API checks run over code rather than prose, because the clearest way to
    say this layer has no `GfVec3f` is a comment saying so -- and a check that
    read comments would forbid the sentence explaining the rule. The producer and
    format checks deliberately do *not* use this.
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
    arguments = parser.parse_args()

    source = arguments.source.resolve()
    label = f"{source.parent.name}/{source.name}"
    errors: list[str] = []

    forbidden_files = {"openstrata.plugin.yaml", "pluginfo.json"}
    for path in source.rglob("*"):
        if path.is_file() and path.name.lower() in forbidden_files:
            errors.append(f"plugin registration file is forbidden: {path}")

    # Word boundaries are load-bearing rather than decoration: without them
    # "unity" matches inside "community" and "glb" inside "glboffset". A check
    # that fails on an ordinary sentence gets read as noise, and a check read as
    # noise stops being enforcement.
    producers = re.compile(
        r"\b(?:" + "|".join(re.escape(n) for n in PRODUCER_NAMES) + r")\b",
        re.IGNORECASE)
    formats = re.compile(
        r"\b(?:" + "|".join(re.escape(n) for n in FORMAT_NAMES) + r")\b",
        re.IGNORECASE)
    # OpenUSD value and stage types, the schema and retarget layers, and the live
    # half of the input layer. None of these is permitted anywhere here, in any
    # file: a value in a basis this layer does not know is not a geometric
    # vector, a target rig is `vrmRetarget`'s, and live input meets recorded
    # input at `motionCore` and nowhere earlier.
    forbidden_api = re.compile(
        r"pxr/|PXR_NAMESPACE|\bGf(?:Vec|Quat|Matrix)|\bUsd[A-Z]|\bSdf[A-Z]|"
        r"TF_REGISTRY_FUNCTION|vrmRetarget|vrmSchema|vrmContainer|"
        r"motionRuntime|vrmAdapter|\bTargetSkeleton\b")
    # Canonical motion, permitted only where the crossing is declared.
    canonical_api = re.compile(r"\bmotion::|motionCore|\bHumanBone\b")

    for area in (source / "include", source / "src"):
        for path in sorted(area.rglob("*")):
            if not path.is_file():
                continue
            text = path.read_text(encoding="utf-8")
            found = producers.search(text)
            if found:
                errors.append(
                    f"producer name '{found.group(0)}' is forbidden in this "
                    f"layer: {path}")
            found = formats.search(text)
            if found:
                errors.append(
                    f"format name '{found.group(0)}' is forbidden in this "
                    f"layer: {path}")
            code = strip_comments(text)
            found = forbidden_api.search(code)
            if found:
                errors.append(
                    f"'{found.group(0)}' is forbidden in this layer: {path}")
            if path.name not in CANONICAL_FILES:
                found = canonical_api.search(code)
                if found:
                    errors.append(
                        f"'{found.group(0)}' crosses into canonical motion "
                        f"outside {'/'.join(sorted(CANONICAL_FILES))}: {path}")

    cmake = (source / "CMakeLists.txt").read_text(encoding="utf-8")
    calls = re.findall(r"target_link_libraries\([^)]*\)", cmake)
    if not calls:
        # ASCII: this lands on a Windows console in CI, where a non-cp932
        # character comes out as a backslash escape and the sentence stops
        # reading as a sentence.
        errors.append(
            f"{label}: no target_link_libraries call; the link check examined "
            f"nothing, so this library's one declared edge is unverified")
    for call in calls:
        if "motionCore::motionCore" not in call:
            errors.append(
                f"{label} links something other than motionCore: {call.strip()}")
        # Everything else OpenUSD offers is reached through motionCore or not at
        # all -- naming a stage, plugin or value library here would be this
        # library acquiring a dependency its descriptor does not declare.
        if re.search(r"\b(?:gf|tf|vt|usd\w*|sdf|plug|ar)\b|pxr::", call,
                     re.IGNORECASE):
            errors.append(f"{label} CMake must link no OpenUSD library directly")

    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    print(f"{label} boundary check passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
