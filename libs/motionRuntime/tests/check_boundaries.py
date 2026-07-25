#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Enforce motionRuntime's no-stage/no-plugin/no-vendor dependency boundary.

motionRuntime sits one level above motionCore: it may depend on motionCore and
OpenUSD's Gf value types, and on nothing else. WORKSPACE.md §2 additionally
forbids it from reaching vrmSchema, any USD file-format bundle, OpenExec, a
network protocol, or an adapter.
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

    forbidden_files = {"openstrata.plugin.yaml", "pluginfo.json"}
    for path in source.rglob("*"):
        if path.is_file() and path.name.lower() in forbidden_files:
            errors.append(f"plugin registration file is forbidden: {path}")

    # Gf value types are allowed; stage/composition/registration APIs are not.
    forbidden_source = re.compile(
        r"pxr/(?:usd|base/(?:tf|plug)|imaging|exec)/|PXR_NAMESPACE|"
        r"TF_REGISTRY_FUNCTION|SDF_DEFINE_FILE_FORMAT|"
        r"\b(?:UsdStage|SdfLayer|PlugRegistry|EsfStage|VdfNode)\b",
        re.IGNORECASE)
    # WORKSPACE.md §2: motionRuntime -> vrmSchema, any USD file-format bundle,
    # OpenExec, a network protocol, or adapters/* is forbidden.
    forbidden_neighbours = re.compile(
        r"\b(?:vrmSchema|vrmContainer|vrmRetarget|usdVrm\w*|execMotion|execVrm|"
        r"cgltf|mocopi|ardy)\b|"
        r"\b(?:winsock|sys/socket\.h|asio|curl|websocket)\b",
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

    cmake = re.sub(r"#[^\n]*", "",
                   (source / "CMakeLists.txt").read_text(encoding="utf-8"))
    if re.search(r"(?:target_link_libraries\([^\n]*(?:\busd\b|\bsdf\b|\bplug\b|"
                 r"\bar\b|exec)|pxr::(?:usd|sdf|plug|ar))", cmake, re.IGNORECASE):
        errors.append(
            "motionRuntime CMake must link only motionCore and the OpenUSD gf "
            "value library")

    try:
        dependencies = _binary_dependencies(library)
    except (OSError, RuntimeError, subprocess.CalledProcessError) as exc:
        errors.append(f"could not inspect motionRuntime dependencies: {exc}")
        dependencies = ""
    forbidden_binary = re.compile(
        r"(?:usd_ms|lib(?:usd|sdf|plug|ar)(?:[._-]|\.(?:dll|dylib|so))|"
        r"vrmSchema|vrmContainer)",
        re.IGNORECASE)
    if forbidden_binary.search(dependencies):
        errors.append(
            "motionRuntime binary imports an OpenUSD stage/plugin library or a "
            "sibling bundle")

    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    print("motionRuntime boundary check passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
