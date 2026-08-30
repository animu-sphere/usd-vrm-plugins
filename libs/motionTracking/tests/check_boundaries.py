#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Enforce motionTracking's leaf boundary.

WORKSPACE.md §2 gives this library an empty edge set, and it is the third such
leaf here with a different reason for it. `liveTransport`'s is empty because it
must never learn a protocol; `osc`'s because a decoder needs nothing a transport
owns. This one's is empty because assignment maps one vocabulary it owns onto
another — which is also the only interesting way this library can fail.

Four rules, and the third is the one this file exists for.

* **The first workspace name makes it something else.** Every library and bundle
  is refused by name, `motionCore` included.
* **The first address literal or product name makes it one source's policy.**
  A generic assignment contract that carried `/tracking/...` would be a claim
  about where a file sits rather than about what it knows
  ([the OSC track] §5.1).
* **The first `HumanBone` makes assignment a lookup.** `motionCore` is already
  refused by the first rule when it arrives as an *edge*; this catches it
  arriving as a *copy* — the enum pasted in, a `Hips`/`LeftUpperLeg`/`Spine`
  spelled here, a `motion::` qualifier. That is the failure with no link line to
  fail on, and it is the one that would quietly end the three-way split: a
  region is a mount point, a bone is a joint, and a knee tracker sits between
  two of the second.
* **The first adapter code makes one adapter's frozen diagnostics into every
  adapter's.** `VRM_<something>_<SOMETHING>` is refused outright; a refusal here
  names the event and the caller supplies the code.

## `tests/` is scanned, on `osc`'s rule rather than `liveTransport`'s

A policy library's tests are where a body role plausibly arrives with a bone's
name on it, because the shortest way to write a fixture for "the left foot" is
to reach for the word the rig files use. The suite therefore lives inside the
boundary, exactly as the decoder's does.

Comments are stripped before every scan. These files document the boundary in
situ — the headers argue at length about the bones a region is *not*, and a
check that fired on the sentence saying so would be answered by deleting it.

The binary argument is the library's **test executable**, not its `.lib`/`.a`.
A static archive records no imports at all, so pointing this check at the
library would make it a gate that cannot fail. That executable links this
library and the standard library and nothing else, so no OpenUSD library may
appear in its imports and there is nothing to allowlist.
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


# Every workspace library and bundle, the two shared leaves included. None of
# these is allowed through: the edge set is empty, so this list has no companion
# allowlist.
_FORBIDDEN_WORKSPACE = re.compile(
    r"\b(?:motionCore|motionRuntime|motionSource|motionBvh|vrmRetarget|"
    r"vrmContainer|vrmSchema|usdVrm\w*|execMotion|execVrm|ExecIr\w*|"
    r"vrmAdapter\w*|liveTransport|osc)\b",
    re.IGNORECASE)

# OpenUSD in any form. This library names no value type at all, not even Gf: an
# assignment is two names and an index, and a position is the solve's.
_FORBIDDEN_USD = re.compile(
    r"pxr/|PXR_NAMESPACE|TF_REGISTRY_FUNCTION|SDF_DEFINE_FILE_FORMAT|"
    r"EXEC_REGISTER_COMPUTATIONS|"
    r"\bGf(?:Vec|Quat|Matrix)|\bUsd[A-Z]|\bSdf[A-Z]|\bPlugRegistry\b")

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

_USD_LIBRARY = re.compile(r"usd_([A-Za-z0-9]+)")


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

    checks = (
        (_FORBIDDEN_WORKSPACE,
         "motionTracking's edge set is empty; this names a workspace library"),
        (_FORBIDDEN_USD, "OpenUSD is forbidden in motionTracking"),
        # The code rule runs BEFORE the producer rule, and the order is the
        # difference between a rule and a comment. Every adapter code here is
        # spelled `VRM_...`, so the producer pattern matches all of them --
        # order it first and the code rule can never fire, which makes it a
        # check that cannot be verified by injecting what it is for. Proved by
        # injection in this order: `VRM_TRACKER_UNPLACED` is refused as a code,
        # and a producer name that is not a code is still refused as one.
        (_FORBIDDEN_CODE,
         "an adapter's diagnostic code is forbidden in motionTracking; a "
         "refusal here names the event and the caller supplies the code"),
        (_FORBIDDEN_PRODUCER,
         "a producer, protocol or SDK name is forbidden in motionTracking"),
        (_FORBIDDEN_ADDRESS,
         "an address literal is forbidden in motionTracking, tests included"),
        (_FORBIDDEN_BONE,
         "a humanoid bone is forbidden in motionTracking; a region is a mount "
         "point and a bone is a joint, and the alias is what would turn "
         "assignment into a lookup"),
    )
    # tests/ is in this list, on osc's rule: a policy library's fixtures are
    # where a bone's name plausibly arrives.
    for area in (source / "include", source / "src", source / "tests"):
        for path in sorted(area.rglob("*")):
            if not path.is_file() or path.suffix not in {".h", ".cpp"}:
                continue
            code = _code_only(path.read_text(encoding="utf-8"))
            for pattern, message in checks:
                found = pattern.search(code)
                if found:
                    errors.append(f"{message}: {path} (`{found.group(0)}`)")

    # An allowlist, not a denylist. A pattern hunting for forbidden names has to
    # anticipate the spelling of every library nobody has linked yet, and it
    # misses a multi-line call outright; naming the tokens that *are* permitted
    # cannot. There is no platform primitive on this list: this library opens
    # nothing and waits for nothing.
    cmake = re.sub(r"#[^\n]*", "",
                   (source / "CMakeLists.txt").read_text(encoding="utf-8"))
    allowed_link = {"motiontracking", "public", "private", "interface"}
    for arguments in re.findall(r"target_link_libraries\s*\((.*?)\)", cmake,
                                re.DOTALL):
        for token in arguments.split():
            if token.lower() not in allowed_link:
                errors.append(
                    "motionTracking may link no workspace library; "
                    f"CMakeLists.txt links `{token}`")

    # `find_package` is how an edge arrives without a link line, so it is
    # refused by name too. `Python3` is the interpreter that runs this file.
    for package in re.findall(r"find_package\s*\(\s*([A-Za-z0-9_]+)", cmake):
        if package not in {"Python3"}:
            errors.append(
                "motionTracking's allowed edge set is empty; CMakeLists.txt "
                f"calls find_package({package})")

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

    for match in sorted(set(_USD_LIBRARY.findall(dependencies))):
        errors.append(
            f"{binary.name} imports usd_{match}; motionTracking links no "
            "OpenUSD and neither may anything it links")

    return _report(errors)


if __name__ == "__main__":
    sys.exit(main())
