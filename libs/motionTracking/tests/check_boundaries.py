#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Enforce motionTracking's leaf boundary, which is now two boundaries.

WORKSPACE.md §2 gave this library an empty edge set and VRC-5 gave it exactly
one line: `motionCore`, taken by the **solve** and by nothing else. That is not
a link-line distinction — one static library links what it links — so the rule
lives here, per file:

* the **assignment half** (`TrackerRegion`, `TrackerAssignment`, and their
  suite) keeps every rule it had. No workspace name, `motionCore` included; no
  OpenUSD in any form, not even a `Gf` value type; no `HumanBone` and no
  `motion::` qualifier. Assignment maps one vocabulary this library owns onto
  another, and the day it names a bone it has become a lookup.
* the **solve half** (`TrackerObservation`, `TrackerSolve`, and their suite) may
  name `motionCore` and OpenUSD's `Gf` value types, and nothing else: no stage,
  no `Sdf`, no `Plug`, no registration macro, and no other workspace library.
  A solve produces a `HumanoidPose`, which is what the edge exists for.

**A file in neither half is an error**, and that is the rule that keeps this
check honest as the library grows. Adding a file to the solve half is a
deliberate act with an argument attached; a file that quietly picked its own
rules by existing is how the split would end.

Three rules apply to every file whatever its half:

* **the alias, in either direction.** `TrackerRegion` may never *be* a
  `HumanBone`. This is the one prohibition in §2 that forbids a `typedef`, and
  it is the failure with no link line to fail on — the enum copied by hand, or
  the two names tied together with `using`. The solve half may name both
  vocabularies, so it is the half where the alias is actually reachable.
* **the first address literal or product name makes it one source's policy.** A
  generic contract whose fixtures carry `/tracking/...` or a device's real
  numbering is generic in name only.
* **the first adapter code makes one adapter's frozen diagnostics into every
  adapter's.** `VRM_<something>_<SOMETHING>` is refused outright; a refusal here
  names the event and the caller supplies the code.

## `tests/` is scanned, on `osc`'s rule rather than `liveTransport`'s

A policy library's tests are where a body role plausibly arrives with a bone's
name on it, because the shortest way to write a fixture for "the left foot" is
to reach for the word the rig files use. The suites therefore live inside the
boundary, each on its own half's rules.

Comments are stripped before every scan. These files document the boundary in
situ — the headers argue at length about the bones a region is *not*, and a
check that fired on the sentence saying so would be answered by deleting it.

The binary argument is a **test executable**, not the `.lib`/`.a`. A static
archive records no imports at all, so pointing this check at the library would
make it a gate that cannot fail. What that executable may import changed at
VRC-5 too: `usd_gf` and the value libraries it pulls are expected now, and the
stage, composition and registration libraries are what must never appear.
"""

from __future__ import annotations

import os
import pathlib
import re
import shutil
import subprocess
import sys


def _find_dumpbin() -> str | None:
    tool = shutil.which("dumpbin")
    if tool:
        return tool
    roots = [
        pathlib.Path(os.environ.get("ProgramFiles", r"C:\\Program Files")),
        pathlib.Path(os.environ.get("ProgramFiles(x86)", r"C:\\Program Files (x86)")),
    ]
    for root in roots:
        # The release year is a wildcard rather than "2022": an in-place Visual
        # Studio upgrade left an empty `2022/` beside a populated `18/` on
        # 2026-08-25 and turned every boundary check in the tree red at once.
        matches = sorted(root.glob(
            "Microsoft Visual Studio/*/*/VC/Tools/MSVC/*/bin/Hostx64/x64/dumpbin.exe"),
            reverse=True)
        if matches:
            return str(matches[0])
    return None


_BLOCK_COMMENT = re.compile(r"/\*.*?\*/", re.DOTALL)
_LINE_COMMENT = re.compile(r"//[^\n]*")


def _code_only(text: str) -> str:
    """Strip C++ comments before scanning. See the module docstring."""
    return _LINE_COMMENT.sub("", _BLOCK_COMMENT.sub("", text))


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


# The two halves, by file name. A file in neither is an error; see the
# docstring.
_ASSIGNMENT_HALF = frozenset({
    "TrackerRegion.h", "TrackerRegion.cpp",
    "TrackerAssignment.h", "TrackerAssignment.cpp",
    "test_tracker_assignment.cpp",
})
_SOLVE_HALF = frozenset({
    "TrackerObservation.h", "TrackerObservation.cpp",
    "TrackerSolve.h", "TrackerSolve.cpp",
    "test_tracker_solve.cpp",
})
# `api.h` is the export macro and belongs to neither half. It is listed rather
# than pattern-matched so that the "a new file must choose a half" rule keeps
# working.
_NEITHER_HALF = frozenset({"api.h"})

# Every workspace library and bundle. `motionCore` is absent because the solve
# half may name it; the assignment half's own pattern below adds it back.
_FORBIDDEN_WORKSPACE = re.compile(
    r"\b(?:motionRuntime|motionSource|motionBvh|vrmRetarget|"
    r"vrmContainer|vrmSchema|usdVrm\w*|execMotion|execVrm|ExecIr\w*|"
    r"vrmAdapter\w*|liveTransport)\b|\bosc::|\bosc/",
    re.IGNORECASE)

_FORBIDDEN_MOTIONCORE = re.compile(r"\bmotionCore\b", re.IGNORECASE)

# OpenUSD in any form, which is the assignment half's rule: an assignment is two
# names and an index, and a position is the solve's.
_FORBIDDEN_USD = re.compile(
    r"pxr/|PXR_NAMESPACE|TF_REGISTRY_FUNCTION|SDF_DEFINE_FILE_FORMAT|"
    r"EXEC_REGISTER_COMPUTATIONS|"
    r"\bGf(?:Vec|Quat|Matrix)|\bUsd[A-Z]|\bSdf[A-Z]|\bPlugRegistry\b")

# The solve half's rule: Gf value types are allowed, stage / composition /
# registration APIs are not. Same shape as motionRuntime's, which is the
# neighbour with the same permission.
_FORBIDDEN_USD_BEYOND_GF = re.compile(
    r"pxr/(?:usd|base/(?:tf|plug|js|work|trace)|imaging|exec)/|PXR_NAMESPACE|"
    r"TF_REGISTRY_FUNCTION|SDF_DEFINE_FILE_FORMAT|"
    r"EXEC_REGISTER_COMPUTATIONS|"
    r"\b(?:UsdStage|SdfLayer|PlugRegistry|EsfStage|VdfNode)\b")

# A producer, a protocol, or an SDK, in any spelling this repository uses.
_PRODUCER_NAMES = (
    "vmc", "mocopi", "vrchat", "ardy", "vrm", "vroid", "unity", "sony",
    "waidayo", "virtualmotioncapture", "steamvr", "openvr",
)
#
# A leading word boundary and deliberately **no trailing one**: the realistic
# failure is an identifier that begins with a producer's name — `vrchatIndex`,
# `steamvrSerial` — not a bare token sitting on its own.
_FORBIDDEN_PRODUCER = re.compile(
    r"\b(?:" + "|".join(re.escape(n) for n in _PRODUCER_NAMES) + r")",
    re.IGNORECASE)

# An OSC address literal belonging to a surface some adapter here decodes. The
# leading quote is what makes this about *payloads* rather than about a path in
# a string.
_FORBIDDEN_ADDRESS = re.compile(
    r"\"/(?:VMC|tracking|avatar|com|input|chatbox)\b", re.IGNORECASE)

# Any adapter's frozen diagnostic code, by the shape all of them share.
_FORBIDDEN_CODE = re.compile(r"\bVRM_[A-Z0-9]+_[A-Z0-9_]+\b")

# `motionCore`'s humanoid vocabulary, arriving as a copy rather than as an edge.
# Every name here is a `HumanBone` enumerator that is NOT a `TrackerRegion`, so
# the pattern cannot fire on this library's own eleven regions: `Head`, `Chest`
# and `Hips` are spelled in both vocabularies and are deliberately absent from
# this list — a region named `Chest` is the whole point, and refusing it would
# make this check unpassable. What it catches is the enum arriving *whole*,
# which is the only way the alias realistically gets here.
_BONE_ONLY_NAMES = (
    "Spine", "UpperChest", "Neck", "Jaw",
    "LeftEye", "RightEye",
    "LeftUpperLeg", "LeftLowerLeg", "LeftToes",
    "RightUpperLeg", "RightLowerLeg", "RightToes",
    "LeftShoulder", "LeftUpperArm", "LeftLowerArm",
    "RightShoulder", "RightUpperArm", "RightLowerArm",
    "ThumbMetacarpal", "IndexProximal", "MiddleProximal",
)
_FORBIDDEN_BONE = re.compile(
    r"\b(?:HumanBone|motion::|" +
    "|".join(re.escape(n) for n in _BONE_ONLY_NAMES) + r")\b")

# The alias, in either direction and in either spelling. A `using` names its new
# name on the left and a `typedef` on the right, so both forms are read as a
# pair and the pair fails when one side is a region and the other is a bone.
_ALIAS_USING = re.compile(r"\busing\s+(\w+)\s*=\s*([^;]{0,200});")
_ALIAS_TYPEDEF = re.compile(r"\btypedef\s+([^;]{0,200}?)\s+(\w+)\s*;")
_REGION_NAME = re.compile(r"\bTrackerRegion\b")
_BONE_NAME = re.compile(r"\b(?:HumanBone|motion::HumanBone)\b")


def _alias_between_vocabularies(code: str) -> str | None:
    """The first alias tying a region to a bone, or None."""
    for pattern, left, right in ((_ALIAS_USING, 0, 1), (_ALIAS_TYPEDEF, 1, 0)):
        for match in pattern.finditer(code):
            sides = (match.group(left + 1), match.group(right + 1))
            joined = " = ".join(sides)
            if _REGION_NAME.search(joined) and _BONE_NAME.search(joined):
                return match.group(0)
    return None


# OpenUSD libraries a solve may never pull in, by component. The source scan
# above is an allowlist and this is a denylist, deliberately: which *value*
# libraries `usd_gf` drags in differs by platform and by how the runtime was
# built, so a check that had to enumerate them would go red for a reason that is
# not a boundary. What must never appear is the stage, composition,
# registration and imaging half, and that set is stable.
_FORBIDDEN_USD_LIBRARY = re.compile(
    r"\b(?:lib)?usd_(?:ms|usd|usdGeom|usdSkel|usdImaging|sdf|pcp|plug|ar|ndr|"
    r"sdr|hd|hdSt|hdx|hio|glf|garch|exec|esf|ef)\b",
    re.IGNORECASE)
_FORBIDDEN_BINARY_NEIGHBOUR = re.compile(
    r"\b(?:lib)?(?:vrmSchema|vrmContainer|vrmAdapter\w*|liveTransport)\b",
    re.IGNORECASE)


def _report(errors: list[str]) -> int:
    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    print("motionTracking boundary check passed")
    return 0


def main() -> int:
    source = pathlib.Path(sys.argv[1]).resolve()
    binary = pathlib.Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else None
    errors: list[str] = []

    # WORKSPACE.md §1: a library, never a plugin bundle.
    forbidden_files = {"openstrata.plugin.yaml", "pluginfo.json"}
    for path in source.rglob("*"):
        if path.is_file() and path.name.lower() in forbidden_files:
            errors.append(f"plugin registration file is forbidden: {path}")

    everywhere = (
        (_FORBIDDEN_WORKSPACE,
         "motionTracking's edge set is `motionCore` alone; this names another "
         "workspace library"),
        (_FORBIDDEN_PRODUCER,
         "a producer, protocol or SDK name is forbidden in motionTracking"),
        (_FORBIDDEN_ADDRESS,
         "an address literal is forbidden in motionTracking, tests included"),
        # This rule overlaps the producer one completely -- every adapter code
        # here is spelled `VRM_...` -- and both fire, because the scan below
        # appends one error per matching pattern and has no first-match-wins.
        # So the order of this tuple carries no meaning and the overlap costs
        # nothing: a code injection reports two reasons rather than one, which
        # is more information than a reader needs and none that is wrong.
        (_FORBIDDEN_CODE,
         "an adapter's diagnostic code is forbidden in motionTracking; a "
         "refusal here names the event and the caller supplies the code"),
    )
    assignment_only = (
        (_FORBIDDEN_MOTIONCORE,
         "the assignment half takes no edge at all; `motionCore` is the "
         "solve's"),
        (_FORBIDDEN_USD, "OpenUSD is forbidden in motionTracking's assignment "
                         "half; an assignment is two names and an index"),
        (_FORBIDDEN_BONE,
         "a humanoid bone is forbidden in motionTracking's assignment half; a "
         "region is a mount point and a bone is a joint, and the alias is what "
         "would turn assignment into a lookup"),
    )
    solve_only = (
        (_FORBIDDEN_USD_BEYOND_GF,
         "the solve half may name OpenUSD's Gf value types and nothing else; "
         "this is a stage, composition or registration API"),
    )

    # tests/ is in this list, on osc's rule: a policy library's fixtures are
    # where a body role plausibly arrives.
    for area in (source / "include", source / "src", source / "tests"):
        for path in sorted(area.rglob("*")):
            if not path.is_file() or path.suffix not in {".h", ".cpp"}:
                continue
            if path.name in _NEITHER_HALF:
                checks = everywhere + assignment_only
            elif path.name in _ASSIGNMENT_HALF:
                checks = everywhere + assignment_only
            elif path.name in _SOLVE_HALF:
                checks = everywhere + solve_only
            else:
                errors.append(
                    f"{path} is in neither half of this library; add it to "
                    "_ASSIGNMENT_HALF or _SOLVE_HALF in this file, which is "
                    "the act of choosing which rules it lives under")
                continue

            code = _code_only(path.read_text(encoding="utf-8"))
            for pattern, message in checks:
                found = pattern.search(code)
                if found:
                    errors.append(f"{message}: {path} (`{found.group(0)}`)")

            alias = _alias_between_vocabularies(code)
            if alias:
                errors.append(
                    "a tracker region may never be an alias for a humanoid "
                    f"bone (WORKSPACE.md §2): {path} (`{alias}`)")

    # An allowlist, not a denylist. A pattern hunting for forbidden names has to
    # anticipate the spelling of every library nobody has linked yet, and it
    # misses a multi-line call outright; naming the tokens that *are* permitted
    # cannot. There is no platform primitive on this list: this library opens
    # nothing and waits for nothing.
    cmake = re.sub(r"#[^\n]*", "",
                   (source / "CMakeLists.txt").read_text(encoding="utf-8"))
    allowed_link = {"motiontracking", "motioncore::motioncore", "public",
                    "private", "interface"}
    for arguments in re.findall(r"target_link_libraries\s*\((.*?)\)", cmake,
                                re.DOTALL):
        for token in arguments.split():
            if token.lower() not in allowed_link:
                errors.append(
                    "motionTracking may link only motionCore; "
                    f"CMakeLists.txt links `{token}`")

    # `find_package` is how an edge arrives without a link line, so it is
    # refused by name too. `Python3` is the interpreter that runs this file, and
    # `pxr` is how `motionCore`'s Gf target is resolved in a standalone
    # configure -- the same two-step motionRuntime and motionSource take.
    for package in re.findall(r"find_package\s*\(\s*([A-Za-z0-9_]+)", cmake):
        if package not in {"Python3", "pxr", "motionCore"}:
            errors.append(
                "motionTracking's allowed edge set is `motionCore`; "
                f"CMakeLists.txt calls find_package({package})")

    if binary is None:
        return _report(errors)

    # Refuse a static archive outright rather than inspecting one and finding
    # nothing. An archive records no imports, so this check would pass on any
    # input whatsoever.
    if binary.suffix.lower() in {".lib", ".a"}:
        errors.append(
            f"{binary.name} is a static archive and records no imports; point "
            "this check at a linked binary (the test executable)")
        return _report(errors)

    try:
        dependencies = _binary_dependencies(binary)
    except (OSError, RuntimeError, subprocess.CalledProcessError) as exc:
        errors.append(f"could not inspect {binary.name}: {exc}")
        return _report(errors)

    for match in sorted(set(_FORBIDDEN_USD_LIBRARY.findall(dependencies))):
        errors.append(
            f"{binary.name} imports {match}; a solve links OpenUSD's value "
            "types and never its stage, composition or registration half")
    for match in sorted(set(_FORBIDDEN_BINARY_NEIGHBOUR.findall(dependencies))):
        errors.append(
            f"{binary.name} imports {match}; motionTracking links motionCore "
            "and nothing else in this workspace")

    return _report(errors)


if __name__ == "__main__":
    sys.exit(main())
