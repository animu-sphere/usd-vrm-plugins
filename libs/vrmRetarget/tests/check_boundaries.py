#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Enforce vrmRetarget's dependency boundary.

WORKSPACE.md §2 allows vrmRetarget -> motionCore and vrmRetarget -> motionRuntime
and nothing else. The load-bearing one is `vrmRetarget -> OpenExec` being
forbidden: the retarget core must be complete and testable before any exec node
exists (motion policy §10.1, §18.12). A network protocol is forbidden for the
same reason live capture must reach this library through motionRuntime's pose
buffer rather than through a socket of its own.
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

    # Gf value types are allowed; stage/composition/registration/exec APIs are
    # not. Reading a stage is the caller's job -- vrmRetarget takes values.
    forbidden_source = re.compile(
        r"pxr/(?:usd|base/(?:tf|plug)|imaging|exec)/|PXR_NAMESPACE|"
        r"TF_REGISTRY_FUNCTION|SDF_DEFINE_FILE_FORMAT|"
        r"\b(?:UsdStage|UsdSkel\w*|SdfLayer|PlugRegistry|EsfStage|VdfNode|"
        r"ExecUsd\w*)\b",
        re.IGNORECASE)
    forbidden_neighbours = re.compile(
        r"\b(?:vrmSchema|vrmContainer|usdVrm\w*|execMotion|execVrm|cgltf|"
        r"mocopi|ardy|liveTransport)\b|"
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
    if re.search(r"target_link_libraries\([^)]*(?:\busd\b|\bsdf\b|\bplug\b|"
                 r"\bar\b|\busdSkel\b|exec)", cmake, re.IGNORECASE):
        errors.append(
            "vrmRetarget CMake must link only motionCore, motionRuntime, and "
            "the OpenUSD gf value library")

    try:
        dependencies = _binary_dependencies(library)
    except (OSError, RuntimeError, subprocess.CalledProcessError) as exc:
        errors.append(f"could not inspect vrmRetarget dependencies: {exc}")
        dependencies = ""
    forbidden_binary = re.compile(
        r"(?:usd_ms|lib(?:usd|sdf|plug|ar)(?:[._-]|\.(?:dll|dylib|so))|"
        r"vrmSchema|vrmContainer|execMotion|execVrm)",
        re.IGNORECASE)
    if forbidden_binary.search(dependencies):
        errors.append(
            "vrmRetarget binary imports an OpenUSD stage/plugin/exec library or "
            "a sibling bundle")

    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    print("vrmRetarget boundary check passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
