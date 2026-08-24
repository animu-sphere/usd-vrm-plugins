#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Enforce liveTransport's leaf boundary.

WORKSPACE.md §2 gives this library an edge set that is **empty** — not short,
none — and that is a measurement rather than an intention: the six files it was
extracted from include their own headers and the standard library and nothing
else. So this check is the unusual one in the workspace: it names no permitted
neighbour, because there is none to name.

Three rules, and each catches a different way a shared leaf fails. A shared leaf
does not fail by being misplaced; it fails by *growing*.

* **The first `motionCore` value makes it a motion library.** Every workspace
  library is refused by name, `motionCore` most of all — the neighbouring
  libraries are not "allowed through" here as they are in an adapter's check.
* **The first address literal makes it a protocol decoder.** `/VMC/`,
  `/tracking/`, `/avatar/`, and a producer's name in any form. This is the cheap
  check and it catches the realistic failure: a transport that "just knows" one
  address or one sender is special.
* **The first adapter code makes one adapter's frozen diagnostics into every
  adapter's.** `VRM_<something>_<SOMETHING>` is refused outright. The library
  owns the diagnostic *vehicle*; a code is an adapter's property, frozen before
  its decoder, and a `liveTransport` holding one is a violation.

The binary argument is the library's **test executable**, not its `.lib`/`.a`.
A static archive records no imports at all — `dumpbin /dependents` on one prints
a section summary and nothing else — so pointing this check at the library would
make it a gate that cannot fail, which is worse than no gate.

What the binary half proves here is stronger than what it proves for an adapter,
because the claim is stronger: this executable links `liveTransport` and the
platform, so **no** OpenUSD library may appear in its imports. An adapter's
check has to maintain an allowlist of the value-type layer its declared edges
drag in; this one has nothing to allow.
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
        matches = sorted(root.glob(
            "Microsoft Visual Studio/2022/*/VC/Tools/MSVC/*/bin/Hostx64/x64/dumpbin.exe"),
            reverse=True)
        if matches:
            return str(matches[0])
    return None


_BLOCK_COMMENT = re.compile(r"/\*.*?\*/", re.DOTALL)
_LINE_COMMENT = re.compile(r"//[^\n]*")


def _code_only(text: str) -> str:
    """Strip C++ comments before scanning.

    These headers document the boundary in situ, so the prose names the very
    libraries and the very codes the code may not name. Scanning the comments
    too would make an accurate explanation indistinguishable from a violation —
    and the explanations here are load-bearing: UdpReceiver.h's whole first half
    is why an adapter's code cannot live in it.
    """
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


# Every workspace library and bundle, plus OpenExec. None of these is allowed
# through: the edge set is empty, so this list has no companion allowlist.
_FORBIDDEN_WORKSPACE = re.compile(
    r"\b(?:motionCore|motionRuntime|motionSource|motionBvh|vrmRetarget|"
    r"vrmContainer|vrmSchema|usdVrm\w*|execMotion|execVrm|ExecIr\w*|"
    r"vrmAdapter\w*)\b",
    re.IGNORECASE)

# OpenUSD in any form. This library names no value type at all, not even Gf.
_FORBIDDEN_USD = re.compile(
    r"pxr/|PXR_NAMESPACE|TF_REGISTRY_FUNCTION|SDF_DEFINE_FILE_FORMAT|"
    r"EXEC_REGISTER_COMPUTATIONS|"
    r"\bGf(?:Vec|Quat|Matrix)|\bUsd[A-Z]|\bSdf[A-Z]|\bPlugRegistry\b")

# A producer, a protocol, or an SDK, in any spelling this repository uses. The
# capture format's magic strings are the reason this has teeth: they are the one
# place a product name could plausibly have travelled with the move, and they
# stayed in the adapters.
_PRODUCER_NAMES = (
    "vmc", "mocopi", "vrchat", "ardy", "vrm", "vroid", "unity", "sony",
    "waidayo", "virtualmotioncapture",
)
#
# A leading word boundary and deliberately **no trailing one**: the realistic
# failure is an identifier that begins with a producer's name — `mocopiPort`,
# `vmcDefault` — not a bare token sitting on its own. Measured: with a trailing
# boundary an injected `mocopiThing` passed this check.
_FORBIDDEN_PRODUCER = re.compile(
    r"\b(?:" + "|".join(re.escape(n) for n in _PRODUCER_NAMES) + r")",
    re.IGNORECASE)

# An OSC or vendor address literal. `/VMC/...`, `/tracking/...`, `/avatar/...`
# are the three surfaces this repository's adapters decode; a slash-led literal
# of any of those shapes in a transport is a decoder growing where it may not.
_FORBIDDEN_ADDRESS = re.compile(
    r"\"/(?:VMC|tracking|avatar|com|input|chatbox)\b", re.IGNORECASE)

# Any adapter's frozen diagnostic code, by the shape all of them share.
_FORBIDDEN_CODE = re.compile(r"\bVRM_[A-Z0-9]+_[A-Z0-9_]+\b")

_USD_LIBRARY = re.compile(r"usd_([A-Za-z0-9]+)")


def _report(errors: list[str]) -> int:
    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    print("liveTransport boundary check passed")
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
        (_FORBIDDEN_WORKSPACE, "liveTransport's edge set is empty; this names a "
                               "workspace library"),
        (_FORBIDDEN_USD, "OpenUSD is forbidden in liveTransport"),
        (_FORBIDDEN_PRODUCER, "a producer, protocol or SDK name is forbidden in "
                              "liveTransport"),
        (_FORBIDDEN_ADDRESS, "an address literal is forbidden in liveTransport"),
        (_FORBIDDEN_CODE, "an adapter's diagnostic code is forbidden in "
                          "liveTransport; it owns the vehicle, not the codes"),
    )
    for area in (source / "include", source / "src"):
        for path in sorted(area.rglob("*")):
            if not path.is_file():
                continue
            code = _code_only(path.read_text(encoding="utf-8"))
            for pattern, message in checks:
                found = pattern.search(code)
                if found:
                    errors.append(f"{message}: {path} (`{found.group(0)}`)")

    # An allowlist, not a denylist. A pattern hunting for forbidden names has to
    # anticipate the spelling of every library nobody has linked yet, and it
    # misses a multi-line call outright; naming the tokens that *are* permitted
    # cannot. `ws2_32` and `Threads::Threads` are the platform's own primitives
    # and are not workspace edges — §2 constrains which *workspace* libraries
    # this leaf may reach, and its answer there is none.
    cmake = re.sub(r"#[^\n]*", "",
                   (source / "CMakeLists.txt").read_text(encoding="utf-8"))
    allowed_link = {
        "livetransport", "public", "private", "interface",
        "ws2_32", "threads::threads",
    }
    for arguments in re.findall(r"target_link_libraries\s*\((.*?)\)", cmake,
                                re.DOTALL):
        for token in arguments.split():
            if token.lower() not in allowed_link:
                errors.append(
                    "liveTransport may link no workspace library; "
                    f"CMakeLists.txt links `{token}`")

    # `find_package` is how an edge arrives without a link line, so it is
    # refused by name too. `Threads` is the platform's, above.
    for package in re.findall(r"find_package\s*\(\s*([A-Za-z0-9_]+)", cmake):
        if package not in {"Threads", "Python3"}:
            errors.append(
                "liveTransport's allowed edge set is empty; CMakeLists.txt "
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

    # No allowlist here, unlike an adapter's check: nothing this library links
    # can drag an OpenUSD library in, so any one at all is a finding rather than
    # a question about the linker.
    for match in sorted(set(_USD_LIBRARY.findall(dependencies))):
        errors.append(
            f"{binary.name} imports usd_{match}; liveTransport links no "
            "OpenUSD and neither may anything it links")

    return _report(errors)


if __name__ == "__main__":
    sys.exit(main())
