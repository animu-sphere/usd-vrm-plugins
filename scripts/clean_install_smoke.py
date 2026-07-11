#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Drive the clean-install / plugin-discovery smoke against a *packaged* bundle.

This proves the near-term plan's §7.2 clean-install contract without any
build-tree reference: it packages the bundle with `ost`, extracts the artifact
into a fresh directory **outside** the repo, then runs
`plugins/usdVrm/tests/clean_install_smoke.py` inside that extracted bundle's
runtime session — so USD discovers the plugin only from the extracted package.

Pipeline (all via `ost`, so it is target-portable):

    ost plugin build   <bundle>        # unless --skip-build
    ost plugin package  <bundle> --json -> archive_digest
    ost artifact import <dist-output-dir>
    ost artifact extract <digest> <fresh-tmp>
    ost plugin run <fresh-tmp> -- python tests/clean_install_smoke.py

Usage:
    python scripts/clean_install_smoke.py [--bundle plugins/usdVrm]
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


def run(cmd: list[str], *, capture: bool = False, **kw) -> subprocess.CompletedProcess:
    print(f"$ {' '.join(cmd)}", flush=True)
    return subprocess.run(cmd, check=True, text=True,
                          capture_output=capture, **kw)


def ost_json(cmd: list[str]) -> dict:
    proc = run(cmd, capture=True)
    # `ost --json` prints a single JSON object on stdout.
    return json.loads(proc.stdout)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bundle", default="plugins/usdVrm",
                        help="bundle path relative to repo root")
    parser.add_argument("--skip-build", action="store_true",
                        help="reuse the existing build; only package + extract + run")
    parser.add_argument("--keep", action="store_true",
                        help="keep the extracted package dir for inspection")
    parser.add_argument("--ost", default="ost", help="ost executable")
    args = parser.parse_args()

    bundle = (REPO_ROOT / args.bundle).resolve()
    if not (bundle / "openstrata.plugin.yaml").exists():
        print(f"FAIL: not a bundle (no openstrata.plugin.yaml): {bundle}")
        return 2
    smoke = bundle / "tests" / "clean_install_smoke.py"
    repo_tools = bundle / "tools"

    ost = args.ost
    if not args.skip_build:
        run([ost, "plugin", "build", str(bundle)])

    pkg = ost_json([ost, "plugin", "package", str(bundle), "--json"])["data"]
    digest = pkg["archive_digest"]
    archive = pathlib.Path(pkg["archive"])
    dist_dir = archive.parent  # holds archive + manifest.json + SHA256SUMS
    print(f"packaged {pkg['target']} -> {digest}")

    # Import the freshly packaged artifact into the local registry (idempotent),
    # then extract it (digest re-verified) into a fresh dir OUTSIDE the repo.
    run([ost, "artifact", "import", str(dist_dir)])
    extract_root = pathlib.Path(tempfile.mkdtemp(prefix="vrm-clean-install-"))
    pkg_root = extract_root / "pkg"
    rc = 1
    try:
        run([ost, "artifact", "extract", digest, str(pkg_root)])
        # Sanity: the extract dir must not sit inside the repo/build tree.
        if REPO_ROOT in pkg_root.resolve().parents:
            print(f"FAIL: extract dir is inside the repo tree: {pkg_root}")
            return 2

        env = dict(os.environ)
        env["VRM_PKG_ROOT"] = str(pkg_root)
        env["VRM_REPO_TOOLS"] = str(repo_tools)
        # Run the assertions inside the EXTRACTED bundle's session: `ost plugin
        # run` puts only that bundle's plugin path on PXR_PLUGINPATH_NAME.
        proc = subprocess.run(
            [ost, "plugin", "run", str(pkg_root), "--", "python", str(smoke)],
            env=env, text=True)
        rc = proc.returncode
    finally:
        if args.keep:
            print(f"kept extracted package at: {pkg_root}")
        else:
            shutil.rmtree(extract_root, ignore_errors=True)

    if rc == 0:
        print("clean-install smoke: PASS")
    else:
        print(f"clean-install smoke: FAIL (exit {rc})")
    return rc


if __name__ == "__main__":
    sys.exit(main())
