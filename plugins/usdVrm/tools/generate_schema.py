#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Regenerate the typed VRM schema from schema/schema.usda.

This is the reproducible form of the Phase 4 "compiled, co-located schema" build
step that `ost` does not yet own (the schema-into-an-existing-bundle ask; see
docs/report/ost). It runs OpenUSD's `usdGenSchema`, then folds the result into the
usdVrm file-format plugin:

  * C++ (api.h, tokens.{h,cpp}, vrmHumanoidAPI.{h,cpp}) -> src/schema/  (committed,
    compiled into libUsdVrmFileFormat; the Python bindings wrap*.cpp and module.cpp
    are intentionally skipped — the plugin ships no Python module).
  * generatedSchema.usda -> plugin/resources/usdVrm/  (committed; USD's schema
    registry reads it next to plugInfo.json).
  * the generated schema `Types` block -> merged into the committed
    plugin/resources/usdVrm/plugInfo.json beside the SdfFileFormat entry.

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

# Generated files we keep (compiled into the lib). wrap*.cpp / module.cpp /
# generatedSchema.module.h / generatedSchema.classes.txt are Python-module build
# helpers we deliberately don't ship.
CPP_FILES = ["api.h", "tokens.h", "tokens.cpp", "vrmHumanoidAPI.h", "vrmHumanoidAPI.cpp"]


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
        for name in CPP_FILES:
            shutil.copy2(gen / name, CPP_DEST / name)
            print(f"  src/schema/{name}")
        shutil.copy2(gen / "generatedSchema.usda", RESOURCES / "generatedSchema.usda")
        print("  plugin/resources/usdVrm/generatedSchema.usda")

        _merge_plug_info(gen / "plugInfo.json")

    print("done. Review the diff and rebuild.")
    return 0


def _merge_plug_info(generated: Path) -> None:
    """Merge usdGenSchema's schema Types into the committed plugInfo.json,
    preserving the SdfFileFormat entry (the ost ask #2 stand-in)."""
    committed = json.loads(PLUGINFO.read_text(encoding="utf-8"))
    # usdGenSchema's plugInfo has comment lines (# ...) before the JSON.
    gen_text = "\n".join(l for l in generated.read_text(encoding="utf-8").splitlines()
                         if not l.lstrip().startswith("#"))
    gen_types = json.loads(gen_text)["Plugins"][0]["Info"]["Types"]

    types = committed["Plugins"][0]["Info"]["Types"]
    for name, spec in gen_types.items():
        types[name] = spec
    PLUGINFO.write_text(json.dumps(committed, indent=4) + "\n", encoding="utf-8")
    print(f"  merged {len(gen_types)} schema type(s) into plugInfo.json")


if __name__ == "__main__":
    raise SystemExit(main())
