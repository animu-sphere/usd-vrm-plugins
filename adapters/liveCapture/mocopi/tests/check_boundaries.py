#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Enforce vrmAdapterMocopi's leaf boundary.

WORKSPACE.md §2 gives an adapter library exactly three edges — motionCore,
motionRuntime and liveTransport — and forbids the rest: vrmSchema, every USD file-format bundle,
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
  therefore does not scan for one. Since OSC-2 the adapter reaches one through
  `liveTransport` rather than opening it here, which narrows what this library
  contains but not what it is permitted to contain — the allowlist keeps the
  platform's own primitives on it, because the permission is the contract's and
  not this file's to withdraw.
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
`motionCore`, `motionRuntime` and `liveTransport` and nothing else, which is
exactly the closure this boundary is about.

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
        # The release year is a wildcard rather than "2022", and the reason is a
        # measurement: this machine's VS 2022 was replaced by VS 18 in place on
        # 2026-08-25, leaving an empty `2022/` beside a populated `18/`. Every
        # boundary check in the tree then reported "dumpbin was not found" and
        # failed -- nine red names for an editor upgrade, none of them about a
        # boundary. A locator that names one release of a tool it only needs
        # `/dependents` from is a version pin with no reason to exist.
        matches = sorted(root.glob(
            "Microsoft Visual Studio/*/*/VC/Tools/MSVC/*/bin/Hostx64/x64/dumpbin.exe"),
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
    quotes the condition under which it *would* share a format, and then the
    change that met it, and `UdpReceiver.h` does the same for the socket.
    Scanning the comments too would make an accurate explanation
    indistinguishable from a violation.
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
    #
    # `vrmAdapterVrchatOsc` and `vrchat` joined the list with the third
    # adapter, and the first of the two is not redundant with `osc`: that
    # token has word boundaries on both sides, so it refuses a bare `osc`
    # and matches nothing inside `vrmAdapterVrchatOsc`. The three checks
    # each name the other two on purpose, so no pair can grow an edge
    # quietly.
    #
    # `motionTracking` is here for a reason none of the others need. Every other
    # name on this list is also a link edge, so the CMake allowlist below would
    # catch it even if this pattern missed; that package is enums and a policy
    # over them, and an adapter can include its header and name `TrackerRegion`
    # with no link line to fail on. WORKSPACE.md section 2 puts assignment on the
    # adapter's TOOL and not on the adapter, and this is the only place that
    # prohibition is enforceable -- which is the same argument, read from the
    # other end, that `motionTracking`'s own check makes about the bone enum.
    forbidden_neighbours = re.compile(
        r"\b(?:vrmSchema|vrmContainer|vrmRetarget|usdVrm\w*|execMotion|execVrm|"
        r"vrmAdapterVmc|vrmAdapterVrchatOsc|vrmAdapterArdy|cgltf|vmc|osc|"
        r"vrchat|ardy|motionTracking)\b",
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
    # `ws2_32` joined the list with the receiver, which is the arrangement an
    # earlier revision of this comment asked for: a platform library is argued
    # for by the change that needs it rather than reserved in advance. **OSC-2
    # then removed the link line and `Threads::Threads` arrived in its place**,
    # and both stay here for a reason that is not the one that put `ws2_32` on
    # the list. Neither is a dependency direction — WORKSPACE.md §2 constrains
    # which *workspace* libraries an adapter may reach, and motion policy §8.2
    # puts the socket inside the adapter *layer* deliberately — so the
    # permission is the contract's, and not this file's to withdraw because the
    # adapter stopped exercising it directly.
    #
    # `Threads::Threads` used to be the half of this list worth reading, and its
    # *absence* was the argument: the sibling linked it for a `DatagramQueue`'s
    # mutex and this adapter had no queue. That is still true of the adapter —
    # nothing here constructs a queue and nothing here holds a mutex — but the
    # queue is `liveTransport`'s now, it is opt-in there, and its threading
    # primitive travels with that library's exported target. A shared library
    # cannot offer one class its mutex and not another. So what the name means
    # on this list changed, from "this adapter asked for it" to "this adapter's
    # transport brings it", and only the first of those was ever a reservation
    # worth catching.
    cmake = re.sub(r"#[^\n]*", "",
                   (source / "CMakeLists.txt").read_text(encoding="utf-8"))
    allowed_link = {
        "vrmadaptermocopi", "public", "private", "interface",
        "motioncore::motioncore", "motionruntime::motionruntime",
        "livetransport::livetransport",
        "ws2_32", "threads::threads",
    }
    for arguments in re.findall(r"target_link_libraries\s*\((.*?)\)", cmake,
                                re.DOTALL):
        for token in arguments.split():
            if token.lower() not in allowed_link:
                errors.append(
                    "vrmAdapterMocopi may link only motionCore, motionRuntime, "
                    "liveTransport and the platform's own primitives; "
                    f"CMakeLists.txt links `{token}`")

    # An exported edge the package **config** does not resolve.
    #
    # The two checks above are about what may be linked; this one is about
    # whether a consumer of the *installed* package can link it at all. A
    # `PUBLIC` dependency lands in the exported target's
    # `INTERFACE_LINK_LIBRARIES`, so `find_package(vrmAdapterMocopi)` re-creates a target
    # naming `X::Y` — and if the config never called `find_dependency(X)`,
    # CMake fails at generate time with "the target was not found". It does not
    # search for it, even when that package's config is sitting in the same
    # prefix.
    #
    # **No other check in this repository can see it**, which is why it is
    # worth a rule of its own rather than a review habit. A composed workspace
    # build resolves every target in-tree and never opens a config file; `ost
    # library build` does the same. The path that breaks is a standalone
    # configure of this adapter or of its CLI — which is a stated PR
    # requirement (roadmap §12) and a manual step. Measured 2026-08-29: both
    # `vrmAdapterVmc` and `vrmAdapterVrchatOsc` grew a `PUBLIC osc::osc` and
    # neither config gained a `find_dependency(osc)`; all 17 CI lanes were
    # green and a two-line consumer project could not configure.
    #
    # One direction only. Every linked package must be resolved; a resolved
    # package that is not linked is not an error — `pxr` is exactly that here,
    # guarded and present because a transitive Gf target needs it.
    config_path = source / "cmake" / "vrmAdapterMocopiConfig.cmake.in"
    config = config_path.read_text(encoding="utf-8")
    resolved = set(re.findall(r"find_dependency\s*\(\s*([A-Za-z0-9_]+)", config))
    for arguments in re.findall(r"target_link_libraries\s*\((.*?)\)", cmake,
                                re.DOTALL):
        for token in arguments.split():
            if "::" not in token:
                continue
            package = token.split("::")[0]
            if package == "vrmAdapterMocopi":
                continue
            if package not in resolved:
                errors.append(
                    f"{token} is linked but {config_path.name} never calls "
                    f"find_dependency({package}); the installed package "
                    "cannot be consumed")

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
        r"vrmAdapterVrchatOsc|vrmAdapterArdy|UsdVrm\w*)\b",
        re.IGNORECASE)
    imported = forbidden_binary.search(dependencies)
    if imported:
        errors.append(
            f"{binary.name} imports `{imported.group(0)}` — a plugin bundle, a "
            "sibling adapter, or vrmRetarget")

    return _report(errors)


if __name__ == "__main__":
    raise SystemExit(main())
