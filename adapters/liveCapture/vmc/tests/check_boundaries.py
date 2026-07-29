#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Enforce vrmAdapterVmc's leaf boundary.

WORKSPACE.md §2 gives an adapter library exactly two edges — motionCore and
motionRuntime — and forbids the rest: vrmSchema, every USD file-format bundle,
`vrmRetarget` (the library), OpenExec, `ExecIr`, and every sibling adapter. It
also may not be a plugin bundle (§1), so a plugin manifest or a plugInfo.json
anywhere under the adapter is a failure by itself.

Two differences from the equivalent check on `libs/motionRuntime` are
deliberate, and both come straight from the contract:

* **Transport is allowed here.** A socket in `motionRuntime` is a violation; a
  socket in an adapter is the adapter's job (motion policy §8.2). This script
  therefore does not scan for one.
* **Only `include/` and `src/` are scanned.** The adapter's CLI under `tools/`
  is a workspace *tool*, and a tool may drive `vrmRetarget` and author a stage
  exactly as `motion_retarget` does. Scanning it would flag the one place the
  contract permits what the library may not do.
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
    libraries the code may not depend on. Scanning the comments too would make
    an accurate explanation indistinguishable from a violation.
    """
    return _LINE_COMMENT.sub("", _BLOCK_COMMENT.sub("", text))


def _binary_dependencies(library: pathlib.Path) -> str:
    if sys.platform == "win32":
        tool = _find_dumpbin()
        if not tool:
            raise RuntimeError("dumpbin was not found")
        command = [tool, "/nologo", "/dependents", str(library)]
    elif sys.platform == "darwin":
        tool = shutil.which("otool")
        if not tool:
            raise RuntimeError("otool was not found")
        command = [tool, "-L", str(library)]
    else:
        tool = shutil.which("readelf")
        if not tool:
            raise RuntimeError("readelf was not found")
        command = [tool, "-d", str(library)]
    return subprocess.run(
        command, check=True, text=True, encoding="utf-8", errors="replace",
        stdout=subprocess.PIPE).stdout


def main() -> int:
    source = pathlib.Path(sys.argv[1]).resolve()
    library = pathlib.Path(sys.argv[2]).resolve()
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
    # -- a runtime data path through one is not a build edge on it (§2.1).
    forbidden_neighbours = re.compile(
        r"\b(?:vrmSchema|vrmContainer|vrmRetarget|usdVrm\w*|execMotion|execVrm|"
        r"vrmAdapterMocopi|vrmAdapterArdy|cgltf|mocopi|ardy)\b",
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
    # misses a multi-line call outright; naming the four tokens that *are*
    # permitted cannot.
    cmake = re.sub(r"#[^\n]*", "",
                   (source / "CMakeLists.txt").read_text(encoding="utf-8"))
    allowed_link = {
        "vrmadaptervmc", "public", "private", "interface",
        "motioncore::motioncore", "motionruntime::motionruntime",
    }
    for arguments in re.findall(r"target_link_libraries\s*\((.*?)\)", cmake,
                                re.DOTALL):
        for token in arguments.split():
            if token.lower() not in allowed_link:
                errors.append(
                    "vrmAdapterVmc may link only motionCore and motionRuntime; "
                    f"CMakeLists.txt links `{token}`")

    try:
        dependencies = _binary_dependencies(library)
    except (OSError, RuntimeError, subprocess.CalledProcessError) as exc:
        errors.append(f"could not inspect vrmAdapterVmc dependencies: {exc}")
        dependencies = ""
    forbidden_binary = re.compile(
        r"(?:usd_ms|lib(?:usd|sdf|plug|ar)(?:[._-]|\.(?:dll|dylib|so))|"
        r"vrmSchema|vrmContainer|vrmRetarget|vrmAdapter(?:Mocopi|Ardy))",
        re.IGNORECASE)
    if forbidden_binary.search(dependencies):
        errors.append(
            "vrmAdapterVmc binary imports an OpenUSD stage/plugin library, a "
            "plugin bundle, or a sibling adapter")

    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    print("vrmAdapterVmc boundary check passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
