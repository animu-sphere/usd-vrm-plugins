#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Enforce vrmAdapterVrchatOsc's leaf boundary.

WORKSPACE.md §2 gives an adapter library at most four edges — motionCore,
motionRuntime, liveTransport and osc — and forbids the rest: vrmSchema, every USD
file-format bundle, `vrmRetarget` (the library), OpenExec, `ExecIr`, and every
sibling adapter. It also may not be a plugin bundle (§1), so a plugin manifest or
a plugInfo.json anywhere under the adapter is a failure by itself.

**The sibling rule is what this check is for, and the thing it guarded has
been removed rather than the rule.** Through VRC-0 `vrmAdapterVmc` held the only
OSC decoder in this repository and this adapter read the same wire format, so
reaching across was not an implausible mistake by somebody who misread the
layout — it was the *correct-looking* thing to do the first time a datagram
started with a '/', and it would have worked. OSC-3 answered it the way the plan
said: `libs/osc` has two consumers, this adapter is the second, and an include of
`vrmAdapterVmc/OscPacket.h` from here now buys nothing at all. The line below
still refuses it, because a sibling edge is forbidden by the contract and not by
whether it would pay (osc-and-vrchat-trackers.md §3.1).

`mocopi` is refused on the same line for symmetry: all three adapters' checks now
name the other two, so no pair can grow an edge quietly.

Three differences from the equivalent check on `libs/motionRuntime` are
deliberate, and all three come straight from the contract:

* **Transport is allowed here.** A socket in `motionRuntime` is a violation; a
  socket in an adapter is the adapter's job (motion policy §8.2). This script
  therefore does not scan for one. Since OSC-2 an adapter reaches one through
  `liveTransport` rather than opening it directly — this adapter never opened one
  at all — which narrows what the library contains but not what it is permitted
  to contain, so the platform's own primitives stay on the link allowlist.
* **An address literal is allowed here, and refused in `libs/osc`.** That
  library may not carry one anywhere, tests included, because a decoder that
  knows one address is special has stopped being a wire-format decoder. An
  adapter is the layer whose job is knowing which addresses are special — and
  this one does not know yet either, because VRC-1 counts addresses rather than
  naming them. The permission is the contract's; the emptiness is the milestone.
* **Only `include/` and `src/` are scanned.** An adapter's CLI under `tools/` is
  a workspace *tool*, and a tool may drive `vrmRetarget` and author a stage
  exactly as `motion_retarget` does. Scanning it would flag the one place the
  contract permits what the library may not do.

The binary argument is the adapter's **test executable**, not its `.lib`/`.a`. A
static archive records no imports at all — `dumpbin /dependents` on one prints a
section summary and nothing else — so pointing this check at the library would
make it a gate that cannot fail. Since VRC-3 the executable's closure reaches
OpenUSD's value-type layer, because the adapter links motionCore for the types
its change of basis returns — so the allowlist below is doing the job it was
written for, which is keeping the closure *small*, rather than the stronger one
it did while the closure was empty. The day it named is the day it happened.

This is also the only enforcement there is. `ost` 0.22.2 discovers plain
libraries in the project root's immediate subdirectories and under `libs/`, so
`adapters/liveCapture/*/openstrata.library.yaml` is never loaded and the
workspace graph gate validates none of the edges declared in it — silently, since
the gate still reports "valid" (report 34).
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
    libraries the code may not depend on — `PacketCapture.h` explains why a VMC
    capture must be refused at line 1, and `Diagnostics.h` names both siblings'
    code namespaces to say they are not this adapter's to raise. Scanning the
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
# `libusd_<name>` decoration all three platforms use. An allowlist rather than a
# list of forbidden names, for the same reason the CMake check below uses one: a
# denylist has to anticipate every library nobody has linked yet.
#
# What is permitted is the value-type and foundation layer motionCore's own
# contract allows. None of it carries a stage, a composition engine, or a plugin
# registry. `boost` and `python` are on the list because the same source links
# differently per platform: Apple's ld64 records every dylib that satisfied a
# symbol, where Linux's --as-needed and Windows' import libraries drop them.
#
# This adapter imported **none** of them until VRC-3, which took the motionCore
# edge for the value types its change of basis returns. The list was the
# contract's permission before it was a description of the binary, and that is
# what made the transition legible rather than silent: the closure grew to
# exactly this set, and the check was not touched to let it.
_ALLOWED_USD_LIBRARIES = {"arch", "boost", "gf", "python", "tf", "vt"}
_USD_LIBRARY = re.compile(r"usd_([A-Za-z0-9]+)")


def _report(errors: list[str]) -> int:
    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    print("vrmAdapterVrchatOsc boundary check passed")
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
    # WORKSPACE.md §2: everything the adapter library may not reach. Both sibling
    # adapters are in here because adapters are siblings, never a stack -- and
    # `vmc` is named as a bare token as well as in `vrmAdapterVmc`, because the
    # thing this adapter would borrow is that adapter's OSC layer rather than its
    # library name.
    #
    # `osc` is deliberately NOT a forbidden token here, where it is one in the
    # mocopi adapter's check: this adapter's own identifiers contain it, and
    # since OSC-3 it is a permitted edge as well. That is the cost of naming a
    # leaf after a protocol, and it is paid by naming the sibling's spellings
    # precisely instead.
    forbidden_neighbours = re.compile(
        r"\b(?:vrmSchema|vrmContainer|vrmRetarget|usdVrm\w*|execMotion|execVrm|"
        r"vrmAdapterVmc|vrmAdapterMocopi|vrmAdapterArdy|cgltf|vmc|mocopi|ardy)\b",
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
    # `motioncore` and `motionruntime` are permitted and not linked. They are the
    # two edges WORKSPACE.md §2 allows an adapter that produces canonical values,
    # and this milestone produces none -- so the list states the contract and the
    # CMakeLists states the milestone, which is the right way round: a link line
    # growing to them is a change a reviewer sees, and this file is not where the
    # permission is granted or withheld.
    #
    # `ws2_32` and `threads::threads` are on the list for the reason the mocopi
    # adapter's check gives: a platform library is the contract's permission, not
    # this file's to withdraw because the adapter reaches it through
    # `liveTransport` instead of naming it.
    cmake = re.sub(r"#[^\n]*", "",
                   (source / "CMakeLists.txt").read_text(encoding="utf-8"))
    allowed_link = {
        "vrmadaptervrchatosc", "public", "private", "interface",
        "motioncore::motioncore", "motionruntime::motionruntime",
        "livetransport::livetransport", "osc::osc",
        "ws2_32", "threads::threads",
    }
    for arguments in re.findall(r"target_link_libraries\s*\((.*?)\)", cmake,
                                re.DOTALL):
        for token in arguments.split():
            if token.lower() not in allowed_link:
                errors.append(
                    "vrmAdapterVrchatOsc may link only motionCore, "
                    "motionRuntime, liveTransport, osc and the platform's own "
                    f"primitives; CMakeLists.txt links `{token}`")

    # An exported edge the package **config** does not resolve.
    #
    # The two checks above are about what may be linked; this one is about
    # whether a consumer of the *installed* package can link it at all. A
    # `PUBLIC` dependency lands in the exported target's
    # `INTERFACE_LINK_LIBRARIES`, so `find_package(vrmAdapterVrchatOsc)` re-creates a target
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
    config_path = source / "cmake" / "vrmAdapterVrchatOscConfig.cmake.in"
    config = config_path.read_text(encoding="utf-8")
    resolved = set(re.findall(r"find_dependency\s*\(\s*([A-Za-z0-9_]+)", config))
    for arguments in re.findall(r"target_link_libraries\s*\((.*?)\)", cmake,
                                re.DOTALL):
        for token in arguments.split():
            if "::" not in token:
                continue
            package = token.split("::")[0]
            if package == "vrmAdapterVrchatOsc":
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
        r"vrmAdapterMocopi|vrmAdapterArdy|UsdVrm\w*)\b", re.IGNORECASE)
    imported = forbidden_binary.search(dependencies)
    if imported:
        errors.append(
            f"{binary.name} imports `{imported.group(0)}` — a plugin bundle, a "
            "sibling adapter, or vrmRetarget")

    return _report(errors)


if __name__ == "__main__":
    raise SystemExit(main())
