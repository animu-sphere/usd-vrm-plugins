#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Drive the clean-install / plugin-discovery smoke against *packaged* bundles.

This proves the near-term plan's §7.2 clean-install contract without any
build-tree reference: it packages the importer with `ost` **and its runtime
bundle dependencies**, extracts every artifact into fresh directories
**outside** the repo, then runs `plugins/usdVrm/tests/clean_install_smoke.py`
in a runtime session whose plugin discovery comes only from those extracted
packages — so USD sees the plugins solely from the shipped layout.

Since the workspace split, opening a textured avatar and resolving its embedded
textures spans three bundles (WORKSPACE.md §1): the importer `usdVrm`, the typed
schemas `vrmSchema` it applies, and the `usdVrmPackageResolver` that serves
`avatar.vrm[images/...]` bytes. The aggregate clean-install therefore composes
all three shipped packages with `ost plugin run --no-inject --plugin-path`
(there is no single-package product closure yet — Phase 5). Pass
`--bundle`/`--with-bundle` to change the set.

Pipeline (all via `ost`, so it is target-portable):

    ost plugin build   <bundle>              # unless --skip-build, per bundle
    ost plugin package  <bundle> --json       -> archive_digest, per bundle
    ost artifact import <dist-output-dir>      -> local registry, per bundle
    ost artifact extract <digest> <fresh-tmp>  -> shipped tree, per bundle
    ost plugin run <primary-pkg> --no-inject --plugin-path <pkg>... \
        -- python tests/clean_install_smoke.py

Usage:
    python scripts/clean_install_smoke.py
        [--bundle plugins/usdVrm]
        [--with-bundle plugins/vrmSchema --with-bundle plugins/usdVrmPackageResolver]
        [--skip-build] [--keep]
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

REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]

# The importer's runtime bundle dependencies for a full textured clean-install:
# the schemas it applies and the resolver that serves embedded texture bytes.
# vrmContainer is a plain library, already staged inside each consumer package.
DEFAULT_RUNTIME_BUNDLES = ["plugins/vrmSchema", "plugins/usdVrmPackageResolver"]


def run(cmd: list[str], *, capture: bool = False, **kw) -> subprocess.CompletedProcess:
    print(f"$ {' '.join(cmd)}", flush=True)
    return subprocess.run(cmd, check=True, text=True,
                          capture_output=capture, **kw)


def ost_json(cmd: list[str]) -> dict:
    proc = run(cmd, capture=True)
    # `ost --json` prints a single JSON object on stdout.
    return json.loads(proc.stdout)


def package_and_extract(ost: str, bundle_rel: str, dest: pathlib.Path,
                        *, skip_build: bool) -> pathlib.Path:
    """Build (optional), package, import, and extract one bundle. Returns the
    extracted package root (holds openstrata.plugin.yaml, plugin/, lib/)."""
    bundle = (REPO_ROOT / bundle_rel).resolve()
    if not (bundle / "openstrata.plugin.yaml").exists():
        raise SystemExit(f"FAIL: not a bundle (no openstrata.plugin.yaml): {bundle}")
    if not skip_build:
        run([ost, "plugin", "build", str(bundle)])

    pkg = ost_json([ost, "plugin", "package", str(bundle), "--json"])["data"]
    digest = pkg["archive_digest"]
    dist_dir = pathlib.Path(pkg["archive"]).parent  # archive + manifest + SHA256SUMS
    print(f"packaged {bundle.name} {pkg['target']} -> {digest}")

    run([ost, "artifact", "import", str(dist_dir)])
    pkg_root = dest / bundle.name
    run([ost, "artifact", "extract", digest, str(pkg_root)])
    if REPO_ROOT in pkg_root.resolve().parents:
        raise SystemExit(f"FAIL: extract dir is inside the repo tree: {pkg_root}")
    return pkg_root


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bundle", default="plugins/usdVrm",
                        help="primary bundle path (runs the smoke assertions)")
    parser.add_argument("--with-bundle", action="append", default=None,
                        dest="with_bundles",
                        help="runtime dependency bundle to also package + compose "
                             "(repeatable; default: vrmSchema + usdVrmPackageResolver)")
    parser.add_argument("--skip-build", action="store_true",
                        help="reuse existing builds; only package + extract + run")
    parser.add_argument("--keep", action="store_true",
                        help="keep the extracted package dirs for inspection")
    parser.add_argument("--ost", default="ost", help="ost executable")
    args = parser.parse_args()

    primary_rel = args.bundle
    with_rels = (args.with_bundles if args.with_bundles is not None
                 else DEFAULT_RUNTIME_BUNDLES)
    primary_bundle = (REPO_ROOT / primary_rel).resolve()
    smoke = primary_bundle / "tests" / "clean_install_smoke.py"
    repo_tools = primary_bundle / "tools"

    ost = args.ost
    extract_root = pathlib.Path(tempfile.mkdtemp(prefix="vrm-clean-install-"))
    rc = 1
    try:
        primary_pkg = package_and_extract(ost, primary_rel, extract_root,
                                          skip_build=args.skip_build)
        dep_pkgs = [package_and_extract(ost, rel, extract_root,
                                        skip_build=args.skip_build)
                    for rel in with_rels]

        env = dict(os.environ)
        env["VRM_PKG_ROOT"] = str(primary_pkg)
        env["VRM_REPO_TOOLS"] = str(repo_tools)
        # --no-inject: discovery comes ONLY from the extracted packages, never
        # the source bundle's build tree. Every shipped package (primary + deps)
        # goes on --plugin-path so the composed aggregate is what USD sees.
        plugin_path_args = []
        for root in [primary_pkg, *dep_pkgs]:
            plugin_path_args += ["--plugin-path", str(root)]
        proc = subprocess.run(
            [ost, "plugin", "run", str(primary_pkg), "--no-inject",
             *plugin_path_args, "--", "python", str(smoke)],
            env=env, text=True)
        rc = proc.returncode
    finally:
        if args.keep:
            print(f"kept extracted packages under: {extract_root}")
        else:
            shutil.rmtree(extract_root, ignore_errors=True)

    if rc == 0:
        print("clean-install smoke: PASS")
    else:
        print(f"clean-install smoke: FAIL (exit {rc})")
    return rc


if __name__ == "__main__":
    sys.exit(main())
