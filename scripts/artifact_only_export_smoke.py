#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Run the product's `vrm_export` from the *installed product* and nothing else.

Export track Step 2: `vrm_export_end_to_end` runs the build tree's executable
against the build tree's bundles. This is the product's, installed outside the
repository:

    ost plugin package --workspace --product   -> the product dist
    ost plugin product verify  <dist>          -> archive + every member checksum
    ost plugin product install <dist> --prefix -> a fresh prefix outside the repo
    <prefix>/tools/vrm_export/bin/vrm_export <input.vrm> -o <out.usdz> --check

The environment is the one `artifact_only_exec_smoke.py` runs in -- the
runtime `ost env` prints plus the product's activation, with every inherited
plugin path dropped and every loader path inside this repository removed --
and:

* the product's `vrm_export` exports the textured VRM 1.0 `Seed-san.vrm` and
  the VRM 0.x MToon fixture to `.usdz` with `--check`, so the importer, the
  schemas and the package resolver it opens them with are the product's;
* each package is read back in a process with **no VRM plugin registered** --
  the product's plugin paths taken away again -- where every asset path has
  to resolve inside the package, and no VRM plugin may be loaded
  (export policy §1, §8);
* then every copy of `usdVrmFileFormat`'s `plugInfo.json` in the prefix is
  moved aside and an export is run again. It must refuse with exit 3, naming
  the bundle -- the proof that no location outside the product was answering.

Exit codes follow `clean_install_smoke.py`: 0 pass, 1 the smoke ran and an
assertion failed, 2 the harness itself is misconfigured.

Usage:
    python scripts/artifact_only_export_smoke.py            # package, then run
    python scripts/artifact_only_export_smoke.py --product dist/products/...
"""
from __future__ import annotations

import argparse
import json
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from artifact_smoke import (  # noqa: E402
    REPO_ROOT, Failures, fail_setup, inside, package_product,
    product_environment, run, suffix, workspace_target)

IMPORTER_TESTS = REPO_ROOT / "plugins" / "usdVrmFileFormat" / "tests"
INPUTS = (
    IMPORTER_TESTS / "corpus" / "spec-samples" / "vrm1" / "seed-san" / "Seed-san.vrm",
    IMPORTER_TESTS / "fixtures" / "mtoon_vrm0.vrm",
)

# Read in a process with no VRM plugin registered. Printed as JSON.
READ_BACK = r'''
import json, sys
from pxr import Ar, Plug, Sdf, Usd

stage = Usd.Stage.Open(sys.argv[1])
assets, unresolved = 0, []
for prim in stage.Traverse():
    for attr in prim.GetAuthoredAttributes():
        if attr.GetTypeName() != Sdf.ValueTypeNames.Asset:
            continue
        value = attr.Get()
        if not value or not value.path:
            continue
        assets += 1
        resolved = value.resolvedPath
        asset = Ar.GetResolver().OpenAsset(Ar.ResolvedPath(resolved)) if resolved else None
        if not asset or asset.GetSize() == 0 or "[" not in resolved:
            unresolved.append(f"{attr.GetPath()}: {value.path} -> {resolved!r}")
registered = [p.name for p in Plug.Registry().GetAllPlugins()
              if p.name.lower().startswith(("usdvrm", "vrm", "execvrm"))]
print(json.dumps({
    "defaultPrim": stage.GetRootLayer().defaultPrim,
    "prims": sum(1 for _ in stage.Traverse()),
    "assets": assets,
    "unresolved": unresolved,
    "vrmPlugins": registered,
}))
'''


def plug_infos(prefix: pathlib.Path, plugin: str) -> list[pathlib.Path]:
    """Every copy of a plugin's plugInfo.json in the product."""
    return sorted(path for path in prefix.rglob("plugInfo.json")
                  if f'"{plugin}"' in path.read_text(encoding="utf-8"))


def without_product_plugins(env: dict, prefix: pathlib.Path) -> dict:
    """`env` with every plugin path the product added taken away again."""
    bare = dict(env)
    kept = [entry for entry in env.get("PXR_PLUGINPATH_NAME", "").split(os.pathsep)
            if entry and not inside(entry, prefix)]
    if kept:
        bare["PXR_PLUGINPATH_NAME"] = os.pathsep.join(kept)
    else:
        bare.pop("PXR_PLUGINPATH_NAME", None)
    return bare


def export(tool: pathlib.Path, source: pathlib.Path, output: pathlib.Path,
           env: dict) -> subprocess.CompletedProcess:
    print(f"$ vrm_export {source.name} -o {output.name} --check", flush=True)
    return subprocess.run([str(tool), str(source), "-o", str(output), "--check"],
                          env=env, text=True, encoding="utf-8", errors="replace",
                          capture_output=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--product", default=None,
                        help="an existing product dist directory, manifest.json "
                             "or .tar.zst; default packages the workspace")
    parser.add_argument("--keep", action="store_true",
                        help="keep the installed prefix and the exports")
    parser.add_argument("--ost", default="ost", help="ost executable")
    args = parser.parse_args()

    platform, profile = workspace_target()
    for required in INPUTS:
        if not required.is_file():
            fail_setup(f"no input at {required}")

    if args.product is None:
        product = package_product(args.ost, platform, profile)
    else:
        product = pathlib.Path(args.product).resolve()
        if not product.exists():
            fail_setup(f"no product at {product}")
    run([args.ost, "plugin", "product", "verify", str(product)])

    scratch = pathlib.Path(tempfile.mkdtemp(prefix="vrm-export-only-"))
    prefix = scratch / "prefix"
    if inside(prefix, REPO_ROOT):
        fail_setup(f"the install prefix is inside the repository: {prefix}")

    failures = Failures()
    try:
        run([args.ost, "plugin", "product", "install", "--prefix",
             str(prefix), str(product)])
        env, _ = product_environment(args.ost, platform, profile, prefix)
        bare = without_product_plugins(env, prefix)
        tool = prefix / "tools" / "vrm_export" / "bin" / f"vrm_export{suffix()}"
        if not tool.is_file():
            fail_setup(f"the product holds no vrm_export at {tool}")
        importer = plug_infos(prefix, "UsdVrmFileFormat")
        if not importer:
            fail_setup("the product holds no usdVrmFileFormat plugInfo.json")

        # The product's tool, with the product's bundles, read back with none.
        for source in INPUTS:
            output = scratch / "out" / f"{source.stem}.usdz"
            result = export(tool, source, output, env)
            if not failures.check(result.returncode == 0,
                                  f"vrm_export {source.name}: exit {result.returncode}:\n"
                                  f"{result.stdout}{result.stderr}"):
                continue
            read = subprocess.run([sys.executable, "-c", READ_BACK, str(output)],
                                  env=bare, text=True, encoding="utf-8",
                                  errors="replace", capture_output=True)
            if not failures.check(read.returncode == 0,
                                  f"{output.name} does not open without the VRM "
                                  f"plugins:\n{read.stdout}{read.stderr}"):
                continue
            summary = json.loads(read.stdout)
            failures.check(summary["defaultPrim"] == "Asset",
                           f"{output.name}: defaultPrim {summary['defaultPrim']!r}")
            failures.check(summary["assets"] > 0,
                           f"{output.name}: no texture to resolve")
            failures.check(not summary["unresolved"],
                           f"{output.name}: {len(summary['unresolved'])} asset path(s) "
                           f"do not resolve inside the package, e.g. "
                           f"{summary['unresolved'][:2]}")
            failures.check(not summary["vrmPlugins"],
                           f"{output.name}: read back with VRM plugins registered: "
                           f"{summary['vrmPlugins']}")
            print(f"  {summary['prims']} prims, {summary['assets']} textures "
                  f"resolved inside the package, no VRM plugin")

        # And the negative: with the product's importer registration gone,
        # nothing may answer in its place.
        hidden = []
        try:
            for info in importer:
                moved = info.with_name("plugInfo.json.hidden")
                info.rename(moved)
                hidden.append((moved, info))
            output = scratch / "out" / "refused.usdz"
            result = export(tool, INPUTS[0], output, env)
            if failures.check(
                    result.returncode == 3 and "usdVrmFileFormat" in result.stderr,
                    f"with every usdVrmFileFormat plugInfo.json in the product moved "
                    f"aside ({len(importer)}), vrm_export exited {result.returncode} "
                    f"rather than 3 naming the bundle -- something outside the "
                    f"product answered:\n{result.stdout}{result.stderr}"):
                print(f"  negative: refused with {len(importer)} usdVrmFileFormat "
                      f"registration(s) moved aside")
        finally:
            for moved, info in hidden:
                moved.rename(info)
    finally:
        if args.keep:
            print(f"kept the installed prefix and the exports at: {scratch}")
        else:
            shutil.rmtree(scratch, ignore_errors=True)

    rc = failures.report()
    print("artifact-only export smoke: " + ("PASS" if rc == 0 else "FAIL"))
    return rc


if __name__ == "__main__":
    sys.exit(main())
