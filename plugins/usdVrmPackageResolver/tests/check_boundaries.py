#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Enforce the resolver side of the workspace dependency contract.

WORKSPACE.md §2: usdVrmPackageResolver may depend on vrmContainer and OpenUSD
Ar only. This check proves, on the built binary and the source tree, that the
resolver split left no hidden edge back into the file format or the schema:

  binary   the DLL/SO must not import usdVrmFileFormat or vrmSchema, and must
           import vrmContainer (the declared library edge is real)
  source   no UsdStage/UsdPrim authoring, no Sdf file-format definition, no
           vrmSchema / importer headers

Usage: check_boundaries.py <bundle-source-dir> <built-library>
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
        pathlib.Path(os.environ.get("ProgramFiles", r"C:\Program Files")),
        pathlib.Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")),
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


_SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".inl"}


def _strip_comments(text: str) -> str:
    """Drop // and /* */ comments so the boundary regex reads code, not prose.

    The source check matches bare identifiers like UsdStage, so without this a
    comment explaining that the resolver must never author a UsdStage would
    fail the very check it describes. String literals are stripped too when
    they contain a comment opener; that can only cost a false negative on a
    literal, never a false positive, and no boundary violation lives in one.
    """
    return re.sub(r"//[^\n]*|/\*.*?\*/", " ", text, flags=re.DOTALL)


def main() -> int:
    source = pathlib.Path(sys.argv[1]).resolve()
    library = pathlib.Path(sys.argv[2]).resolve()
    errors: list[str] = []

    # --- source boundary -----------------------------------------------------
    # Stage authoring and sibling-plugin surfaces are forbidden in the resolver
    # (plan §4.3: "resolver 内で UsdStage や UsdPrim を生成しない").
    forbidden_source = re.compile(
        r"(?:#\s*include\s*[<\"]pxr/usd/usd(?:Geom|Skel|Shade)?/|"
        r"#\s*include\s*[<\"]pxr/usd/sdf/fileFormat|"
        r"#\s*include\s*[<\"]vrmSchema/|"
        r"\bUsdStage\b|\bUsdPrim\b|SDF_DEFINE_FILE_FORMAT)")
    for path in sorted((source / "src").rglob("*")):
        if not path.is_file() or path.suffix.lower() not in _SOURCE_SUFFIXES:
            continue
        try:
            text = path.read_text(encoding="utf-8")
        except (OSError, UnicodeDecodeError) as exc:
            errors.append(f"could not read {path}: {exc}")
            continue
        if forbidden_source.search(_strip_comments(text)):
            errors.append(f"stage-authoring/file-format API is forbidden: {path}")

    # --- binary boundary -----------------------------------------------------
    try:
        dependencies = _binary_dependencies(library)
    except (OSError, RuntimeError, subprocess.CalledProcessError) as exc:
        errors.append(f"could not inspect resolver dependencies: {exc}")
        dependencies = ""
    forbidden_import = re.compile(
        r"(?:UsdVrmFileFormat|vrmSchema)", re.IGNORECASE)
    if forbidden_import.search(dependencies):
        errors.append(
            "resolver binary imports a forbidden workspace library "
            "(usdVrmFileFormat / vrmSchema)")
    if dependencies and not re.search(r"vrmContainer", dependencies, re.IGNORECASE):
        errors.append(
            "resolver binary does not import vrmContainer: the declared "
            "requires.libraries edge is not real")

    if errors:
        # Degrade unencodable characters rather than raising UnicodeEncodeError
        # on a non-UTF-8 console (cp932 Windows) and masking the finding.
        if hasattr(sys.stderr, "reconfigure"):
            sys.stderr.reconfigure(errors="replace")
        print("\n".join(errors), file=sys.stderr)
        return 1
    print("usdVrmPackageResolver boundary check passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
