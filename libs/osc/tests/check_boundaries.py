#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Enforce osc's leaf boundary.

WORKSPACE.md §2 gives this library an edge set that is **empty**, and unlike
`liveTransport`'s it is empty of `liveTransport` too: a decoder that reads no
socket needs nothing a transport owns, and the two are siblings rather than a
stack. So this check names no permitted neighbour, because there is none.

Four rules, and each catches a different way a shared decoder fails. It does not
fail by being misplaced; it fails by *learning something*.

* **The first `motionCore` value makes it a motion library.** Every workspace
  library is refused by name, `liveTransport` included.
* **The first address literal makes it one protocol's decoder.** `/VMC/`,
  `/tracking/`, `/avatar/`, and a producer's name in any form. This is the cheap
  check and it catches the realistic failure the plan names: a decoder that
  "just knows" one address is special ([the OSC track] §4).
* **The first adapter code makes one adapter's frozen diagnostics into every
  adapter's.** `VRM_<something>_<SOMETHING>` is refused outright. This library
  raises no code at all — its refusal is an `OscDecodeError` carrying a subject
  and a detail — so unlike `liveTransport` it does not even own a vehicle to
  put one in.
* **The first `osc::` in a neighbour makes the direction reversible.** That half
  is not here: it is in the four libraries' own checks, which refuse this
  library by name.

## `tests/` is scanned, and that is the difference from `liveTransport`'s check

That library's check reads `include/` and `src/`. This one reads `tests/` as
well, because a decoder's tests are the one place a vendor address plausibly
arrives: every payload needs *some* address, and the shortest path is to paste
one off a real session. The suite that moved here did exactly that — it was
written inside `vrmAdapterVmc` and every sample address was a `/VMC/...` one —
and the addresses were replaced on the way, at identical length so the byte
offsets it asserts stayed the same numbers. Without this rule that substitution
would be a convention, and a convention is what the next author has not read.

Comments are stripped before every scan. These files document the boundary in
situ, so the prose names the very addresses and the very codes the code may not
name — the header's whole point is that it does *not* know what `/VMC/...`
means, and a check that fired on the sentence saying so would be answered by
deleting it.

The binary argument is the library's **test executable**, not its `.lib`/`.a`.
A static archive records no imports at all, so pointing this check at the
library would make it a gate that cannot fail.

This executable links `osc` and the standard library and nothing else — not even
the platform's socket, which is what most visibly separates this leaf from the
transport one — so **no** OpenUSD library may appear in its imports, and there
is nothing to allowlist.
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


# Every workspace library and bundle, plus OpenExec and the sibling leaf. None
# of these is allowed through: the edge set is empty, so this list has no
# companion allowlist.
_FORBIDDEN_WORKSPACE = re.compile(
    r"\b(?:motionCore|motionRuntime|motionSource|motionBvh|vrmRetarget|"
    r"vrmContainer|vrmSchema|usdVrm\w*|execMotion|execVrm|ExecIr\w*|"
    r"vrmAdapter\w*|liveTransport)\b",
    re.IGNORECASE)

# OpenUSD in any form. This library names no value type at all, not even Gf.
_FORBIDDEN_USD = re.compile(
    r"pxr/|PXR_NAMESPACE|TF_REGISTRY_FUNCTION|SDF_DEFINE_FILE_FORMAT|"
    r"EXEC_REGISTER_COMPUTATIONS|"
    r"\bGf(?:Vec|Quat|Matrix)|\bUsd[A-Z]|\bSdf[A-Z]|\bPlugRegistry\b")

# A producer, a protocol, or an SDK, in any spelling this repository uses.
_PRODUCER_NAMES = (
    "vmc", "mocopi", "vrchat", "ardy", "vrm", "vroid", "unity", "sony",
    "waidayo", "virtualmotioncapture",
)
#
# A leading word boundary and deliberately **no trailing one**: the realistic
# failure is an identifier that begins with a producer's name — `vmcDefault`,
# `mocopiPort` — not a bare token sitting on its own.
_FORBIDDEN_PRODUCER = re.compile(
    r"\b(?:" + "|".join(re.escape(n) for n in _PRODUCER_NAMES) + r")",
    re.IGNORECASE)

# An OSC address literal belonging to a surface some adapter here decodes. The
# leading quote is what makes this about *payloads* rather than about a path in
# a string: a decoder that carries one of these is a decoder that has an opinion
# about it.
_FORBIDDEN_ADDRESS = re.compile(
    r"\"/(?:VMC|tracking|avatar|com|input|chatbox)\b", re.IGNORECASE)

# Any adapter's frozen diagnostic code, by the shape all of them share.
_FORBIDDEN_CODE = re.compile(r"\bVRM_[A-Z0-9]+_[A-Z0-9_]+\b")

_USD_LIBRARY = re.compile(r"usd_([A-Za-z0-9]+)")


def _report(errors: list[str]) -> int:
    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    print("osc boundary check passed")
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
         "osc's edge set is empty; this names a workspace library"),
        (_FORBIDDEN_USD, "OpenUSD is forbidden in osc"),
        (_FORBIDDEN_PRODUCER,
         "a producer, protocol or SDK name is forbidden in osc"),
        (_FORBIDDEN_ADDRESS,
         "an address literal is forbidden in osc, tests included"),
        (_FORBIDDEN_CODE,
         "an adapter's diagnostic code is forbidden in osc; a refusal here "
         "carries a subject and a detail and no code"),
    )
    # tests/ is in this list and not in liveTransport's. See the module
    # docstring: a decoder's payloads are where an address literal arrives.
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
    # cannot. There is no platform primitive on this list, unlike the transport
    # leaf's: this library opens nothing and waits for nothing.
    cmake = re.sub(r"#[^\n]*", "",
                   (source / "CMakeLists.txt").read_text(encoding="utf-8"))
    allowed_link = {"osc", "public", "private", "interface"}
    for arguments in re.findall(r"target_link_libraries\s*\((.*?)\)", cmake,
                                re.DOTALL):
        for token in arguments.split():
            if token.lower() not in allowed_link:
                errors.append(
                    "osc may link no workspace library and no transport; "
                    f"CMakeLists.txt links `{token}`")

    # `find_package` is how an edge arrives without a link line, so it is
    # refused by name too. `Python3` is the interpreter that runs this file.
    for package in re.findall(r"find_package\s*\(\s*([A-Za-z0-9_]+)", cmake):
        if package not in {"Python3"}:
            errors.append(
                "osc's allowed edge set is empty; CMakeLists.txt calls "
                f"find_package({package})")

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
            f"{binary.name} imports usd_{match}; osc links no OpenUSD and "
            "neither may anything it links")

    return _report(errors)


if __name__ == "__main__":
    sys.exit(main())
