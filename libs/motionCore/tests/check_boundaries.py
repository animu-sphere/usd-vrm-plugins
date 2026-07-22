#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Enforce motionCore's no-stage/no-plugin dependency boundary."""

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

    # Gf value types are intentionally allowed. Stage/composition/registration
    # APIs are not: a future retargeter owns those responsibilities.
    forbidden_source = re.compile(
        r"pxr/(?:usd|base/(?:tf|plug)|imaging)/|PXR_NAMESPACE|"
        r"TF_REGISTRY_FUNCTION|SDF_DEFINE_FILE_FORMAT|"
        r"\b(?:UsdStage|SdfLayer|PlugRegistry)\b",
        re.IGNORECASE)
    for area in (source / "include", source / "src"):
        for path in area.rglob("*"):
            if path.is_file():
                text = path.read_text(encoding="utf-8")
                if forbidden_source.search(text):
                    errors.append(f"stage/plugin API is forbidden: {path}")

    cmake = (source / "CMakeLists.txt").read_text(encoding="utf-8")
    if re.search(r"(?:target_link_libraries\([^\n]*(?:usd|sdf|plug|ar)|"
                 r"pxr::(?:usd|sdf|plug|ar))", cmake, re.IGNORECASE):
        errors.append("motionCore CMake must link only the OpenUSD gf value library")

    try:
        dependencies = _binary_dependencies(library)
    except (OSError, RuntimeError, subprocess.CalledProcessError) as exc:
        errors.append(f"could not inspect motionCore dependencies: {exc}")
        dependencies = ""
    forbidden_binary = re.compile(
        r"(?:usd_ms|lib(?:usd|sdf|plug|ar)(?:[._-]|\.(?:dll|dylib|so)))",
        re.IGNORECASE)
    if forbidden_binary.search(dependencies):
        errors.append("motionCore binary imports an OpenUSD stage/plugin library")

    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    print("motionCore boundary check passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
