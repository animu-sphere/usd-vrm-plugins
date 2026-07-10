#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Regenerate the committed typed VRM schema fallback from schema/schema.usda.

`ost plugin build` 0.6+ generates the co-located schema C++ in `.strata/` for the
resolved runtime. This script refreshes the committed fallback used by plain
CMake and the checked-in USD registration resources. It runs OpenUSD's
`usdGenSchema`, then folds the result into the usdVrm file-format plugin:

  * C++ (api.h, tokens.{h,cpp}, vrmHumanoidAPI.{h,cpp}) -> src/schema/  (committed,
    compiled into libUsdVrmFileFormat; the Python bindings wrap*.cpp and module.cpp
    are intentionally skipped — the plugin ships no Python module).
  * generatedSchema.usda -> plugin/resources/usdVrm/  (committed; USD's schema
    registry reads it next to plugInfo.json).
  * the generated schema `Types` block -> merged into the committed
    plugin/resources/usdVrm/plugInfo.json and plugInfo.json.in beside the
    SdfFileFormat entry.

Dependencies (usdGenSchema is a Python tool):
  * A Python interpreter with the OpenUSD `pxr` modules importable, OR — as here —
    any Python plus an OpenUSD install whose `bin/usdGenSchema` and
    `lib/usd/usd/resources/codegenTemplates` are used directly.
  * Point --usd-root at that install (or set USD_INSTALL_ROOT). The script sets
    PXR_AR_DEFAULT_SEARCH_PATH so the `@usd/schema.usda@` sublayer (which defines
    APISchemaBase) resolves, and passes -t so the install's own codegen templates
    are used regardless of which `pxr` the interpreter imports.

Usage:
  python tools/generate_schema.py --usd-root C:/path/to/openusd-install
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

PLUGIN_DIR = Path(__file__).resolve().parent.parent
SCHEMA_SRC = PLUGIN_DIR / "schema" / "schema.usda"
CPP_DEST = PLUGIN_DIR / "src" / "schema"
RESOURCES = PLUGIN_DIR / "plugin" / "resources" / "usdVrm"
PLUGINFO = RESOURCES / "plugInfo.json"
PLUGINFO_TEMPLATE = RESOURCES / "plugInfo.json.in"
# The ctest harness loads the plugin through its own configured plugInfo (a
# configure_file template), so it carries a second copy of the schema Types. Keep
# it in lockstep here rather than relying on a hand-edit that silently drifts.
TEST_PLUGINFO = PLUGIN_DIR / "tests" / "plugInfo.in.json"

# Generated files we always keep (compiled into the lib). Per-class
# vrm*API.{h,cpp} are discovered dynamically (one set per schema class), so adding
# a class to schema.usda needs no edit here. wrap*.cpp / module.cpp /
# generatedSchema.module.h / generatedSchema.classes.txt are Python-module build
# helpers we deliberately don't ship.
SHARED_CPP_FILES = ["api.h", "tokens.h", "tokens.cpp"]


def _cpp_files(gen: Path) -> list[str]:
    names = list(SHARED_CPP_FILES)
    for p in sorted(gen.glob("vrm*API.*")):
        if p.suffix in (".h", ".cpp") and not p.name.startswith("wrap"):
            names.append(p.name)
    return names


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--usd-root", default=os.environ.get("USD_INSTALL_ROOT"),
                    help="OpenUSD install root (or set USD_INSTALL_ROOT).")
    ap.add_argument("--python", default=sys.executable,
                    help="Python interpreter to run usdGenSchema with.")
    args = ap.parse_args()

    if not args.usd_root:
        ap.error("pass --usd-root or set USD_INSTALL_ROOT")
    usd_root = Path(args.usd_root)
    gen_schema = usd_root / "bin" / "usdGenSchema"
    templates = usd_root / "lib" / "usd" / "usd" / "resources" / "codegenTemplates"
    core_resources = usd_root / "lib" / "usd" / "usd" / "resources"
    for p in (gen_schema, templates, core_resources):
        if not p.exists():
            ap.error(f"not found under --usd-root: {p}")

    env = dict(os.environ)
    # Resolve the @usd/schema.usda@ sublayer (defines APISchemaBase).
    env["PXR_AR_DEFAULT_SEARCH_PATH"] = str(core_resources)

    with tempfile.TemporaryDirectory() as tmp:
        cmd = [args.python, str(gen_schema), str(SCHEMA_SRC), tmp,
               "-t", str(templates)]
        print("==>", " ".join(cmd))
        subprocess.run(cmd, env=env, check=True)
        gen = Path(tmp)

        CPP_DEST.mkdir(parents=True, exist_ok=True)
        for name in _cpp_files(gen):
            shutil.copy2(gen / name, CPP_DEST / name)
            print(f"  src/schema/{name}")
        shutil.copy2(gen / "generatedSchema.usda", RESOURCES / "generatedSchema.usda")
        print("  plugin/resources/usdVrm/generatedSchema.usda")

        gen_types = _read_schema_types(gen / "plugInfo.json")
        for target in (PLUGINFO, PLUGINFO_TEMPLATE, TEST_PLUGINFO):
            _merge_plug_info(target, gen_types)

    print("done. Review the diff and rebuild.")
    return 0


def _read_schema_types(generated: Path) -> dict:
    """The schema Types block usdGenSchema emitted (its plugInfo has comment
    lines before the JSON)."""
    gen_text = "\n".join(l for l in generated.read_text(encoding="utf-8").splitlines()
                         if not l.lstrip().startswith("#"))
    return json.loads(gen_text)["Plugins"][0]["Info"]["Types"]


def _merge_plug_info(target: Path, gen_types: dict) -> None:
    """Merge usdGenSchema's schema Types into a committed plugInfo.json,
    preserving the hand-written SdfFileFormat entry (the ost ask #2 stand-in) and
    pruning any autoGenerated Types from a previous run that the current schema no
    longer defines, so a renamed/removed class doesn't linger."""
    committed = json.loads(target.read_text(encoding="utf-8"))
    types = committed["Plugins"][0]["Info"]["Types"]
    for stale in [n for n, spec in types.items() if spec.get("autoGenerated")]:
        del types[stale]
    for name, spec in gen_types.items():
        types[name] = spec
    target.write_text(json.dumps(committed, indent=4) + "\n", encoding="utf-8")
    print(f"  merged {len(gen_types)} schema type(s) into {target.name}")


if __name__ == "__main__":
    raise SystemExit(main())
