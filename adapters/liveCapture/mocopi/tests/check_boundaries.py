#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Enforce vrmAdapterMocopi's leaf boundary.

WORKSPACE.md §2 gives an adapter library exactly two edges — motionCore and
motionRuntime — and forbids the rest: vrmSchema, every USD file-format bundle,
`vrmRetarget` (the library), OpenExec, `ExecIr`, and every sibling adapter. It
also may not be a plugin bundle (§1), so a plugin manifest or a plugInfo.json
anywhere under the adapter is a failure by itself.

The sibling live adapter is the name this check exists for more than any other,
and the reason is specific to this adapter rather than general. The two decode
motion from the same product — one natively, one after a relay has re-expressed
it — so reaching across is not an implausible mistake made by somebody who
misread the layout. It is the *convenient* thing to do the first time a packet
looks like an OSC bundle. Adapter plan §2.1 forbids it because a native decoder
that borrowed a relay's decoder would inherit the relay's assumptions about
framing, clocks and bone names, and the whole point of the native path is to
measure what those assumptions cost. That check is a `git grep`'s worth of
enforcement, and it is worth having anyway: the sibling's own boundary script
already refuses this adapter's name, so the pair is symmetric and neither can
grow the edge quietly.

Two differences from the equivalent check on `libs/motionRuntime` are
deliberate, and both come straight from the contract:

* **Transport is allowed here.** A socket in `motionRuntime` is a violation; a
  socket in an adapter is the adapter's job (motion policy §8.2). This script
  therefore does not scan for one. It does not *link* one yet either — the
  allowlist below names only what this library links today, so the receiver's
  platform libraries have to be argued for in the change that adds them.
* **Only `include/` and `src/` are scanned.** An adapter's CLI under `tools/`
  is a workspace *tool*, and a tool may drive `vrmRetarget` and author a stage
  exactly as `motion_retarget` does. Scanning it would flag the one place the
  contract permits what the library may not do.

The binary argument is the adapter's **test executable**, not its `.lib`/`.a`.
A static archive records no imports at all — `dumpbin /dependents` on one prints
a section summary and nothing else — so pointing this check at the library would
make it a gate that cannot fail, which is worse than no gate. The linked test
executable is the first artifact in which the adapter's real transitive imports
exist, so it is the first one worth inspecting. It links the adapter plus
`motionCore` and `motionRuntime` and nothing else, which is exactly the closure
this boundary is about.

This is also the only enforcement there is. `ost` 0.21.0 discovers plain
libraries in the project root's immediate subdirectories and under `libs/`, so
`adapters/liveCapture/*/openstrata.library.yaml` is never loaded and the
workspace graph gate validates none of the edges declared in it — silently,
since the gate still reports "valid" (report 34).
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
    libraries the code may not depend on — this adapter's `PacketCapture.h`
    spends a section on why it does not share the sibling's format. Scanning the
    comments too would make an accurate explanation indistinguishable from a
    violation.
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


# The OpenUSD libraries the adapter's closure may import, by the `usd_<name>` /
# `libusd_<name>` decoration all three platforms use. Everything else in that
# family is refused by name, which is what catches the ones that matter --
# usd_usd, usd_sdf, usd_plug, usd_ar, usd_usdSkel, usd_usdGeom, usd_exec*,
# usd_ms.
#
# An allowlist rather than a list of forbidden names, for the same reason the
# CMake check below uses one: a denylist has to anticipate every library nobody
# has linked yet. What is permitted is the value-type and foundation layer
# motionCore's own contract already allows. None of it carries a stage, a
# composition engine, or a plugin registry.
#
# `boost` and `python` are on the list because **the same source links
# differently per platform**, which cost the sibling adapter a CI round trip to
# learn. Gf's imported target carries OpenUSD's vendored Boost and its Python
# layer transitively, and Apple's ld64 records every dylib that satisfied a
# symbol on the link line, where Linux's default --as-needed and Windows' import
# libraries both drop them. So a test executable here imports usd_gf alone on
# Windows and Linux, and usd_gf + usd_boost + usd_python on macOS arm64 -- a
# property of the published Python-enabled runtime, not of anything this
# repository wrote.
_ALLOWED_USD_LIBRARIES = {"arch", "boost", "gf", "python", "tf", "vt"}
_USD_LIBRARY = re.compile(r"usd_([A-Za-z0-9]+)")


def _report(errors: list[str]) -> int:
    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    print("vrmAdapterMocopi boundary check passed")
    return 0


def main() -> int:
    source = pathlib.Path(sys.argv[1]).resolve()
    binary = pathlib.Path(sys.argv[2]).resolve()
    errors: list[str] = []

    # WORKSPACE.md §1: an adapter is a library, never a plugin bundle.
    forbidden_files = {"openstrata.plugin.yaml", "pluginfo.json"}
    for path in source.rglob("*"):
        if path.is_file() and path.name.lower() in forbidden_files:
            errors.append(f"plugin registration file is forbidden: {path}")

    # Gf value types arrive through motionCore and are allowed; stage,
    # composition, registration, and exec APIs are not.
    forbidden_source = re.compile(
        r"pxr/(?:usd|base/(?:tf|plug)|imaging|exec)/|PXR_NAMESPACE|"
        r"TF_REGISTRY_FUNCTION|SDF_DEFINE_FILE_FORMAT|"
        r"EXEC_REGISTER_COMPUTATIONS|"
        r"\b(?:UsdStage|SdfLayer|PlugRegistry|EsfStage|VdfNode|ExecIr\w*)\b",
        re.IGNORECASE)
    # WORKSPACE.md §2: everything the adapter library may not reach. The two
    # sibling adapters are in here because adapters are siblings, never a stack
    # -- a runtime data path through one is not a build edge on it (§2.1). `osc`
    # is named alongside them because the sibling's protocol layer is the piece
    # this adapter would be tempted to borrow rather than its library name.
    forbidden_neighbours = re.compile(
        r"\b(?:vrmSchema|vrmContainer|vrmRetarget|usdVrm\w*|execMotion|execVrm|"
        r"vrmAdapterVmc|vrmAdapterArdy|cgltf|vmc|osc|ardy)\b",
        re.IGNORECASE)
    for area in (source / "include", source / "src"):
        for path in area.rglob("*"):
            if not path.is_file():
                continue
            code = _code_only(path.read_text(encoding="utf-8"))
            if forbidden_source.search(code):
                errors.append(f"stage/plugin/exec API is forbidden: {path}")
            if forbidden_neighbours.search(code):
                errors.append(f"forbidden dependency direction: {path}")

    # An allowlist, not a denylist. A pattern hunting for forbidden names has to
    # anticipate the spelling of every library nobody has linked yet, and it
    # misses a multi-line call outright; naming the tokens that *are* permitted
    # cannot.
    #
    # `ws2_32` joined the list with the receiver, which is the arrangement the
    # previous revision of this comment asked for: a platform library is argued
    # for by the change that needs it rather than reserved in advance. It is not
    # a dependency direction — WORKSPACE.md §2 constrains which *workspace*
    # libraries an adapter may reach, and motion policy §8.2 puts the socket
    # inside the adapter deliberately.
    #
    # `Threads::Threads` is still absent, and that is the half of this list
    # worth reading. The sibling links it for a `DatagramQueue`'s mutex; this
    # adapter has no queue, so adding the name "because a receiver usually needs
    # one" would be the reservation this allowlist exists to catch.
    cmake = re.sub(r"#[^\n]*", "",
                   (source / "CMakeLists.txt").read_text(encoding="utf-8"))
    allowed_link = {
        "vrmadaptermocopi", "public", "private", "interface",
        "motioncore::motioncore", "motionruntime::motionruntime",
        "ws2_32",
    }
    for arguments in re.findall(r"target_link_libraries\s*\((.*?)\)", cmake,
                                re.DOTALL):
        for token in arguments.split():
            if token.lower() not in allowed_link:
                errors.append(
                    "vrmAdapterMocopi may link only motionCore, motionRuntime "
                    f"and the platform's transport; CMakeLists.txt links "
                    f"`{token}`")

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
        dependencies = ""

    for name in sorted({m.lower() for m in _USD_LIBRARY.findall(dependencies)}):
        if name not in _ALLOWED_USD_LIBRARIES:
            errors.append(
                f"{binary.name} imports OpenUSD library `usd_{name}`; the "
                "adapter's closure may reach the value-type layer only "
                f"({', '.join(f'usd_{a}' for a in sorted(_ALLOWED_USD_LIBRARIES))})")

    forbidden_binary = re.compile(
        r"\b(?:vrmSchema|vrmContainer|vrmRetarget|vrmAdapterVmc|"
        r"vrmAdapterArdy|UsdVrm\w*)\b", re.IGNORECASE)
    imported = forbidden_binary.search(dependencies)
    if imported:
        errors.append(
            f"{binary.name} imports `{imported.group(0)}` — a plugin bundle, a "
            "sibling adapter, or vrmRetarget")

    return _report(errors)


if __name__ == "__main__":
    raise SystemExit(main())
