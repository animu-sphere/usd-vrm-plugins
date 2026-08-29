#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Run every `find_package` consumer on this host, and record what it measured.

PKG-4's lane, as one command. It is a separate entry point from the driver
rather than a loop in the workflow YAML for the reason the track states about
lanes generally: a lane that cannot be reproduced by hand is a lane nobody can
debug. This is the thing CI runs, and it is the thing a maintainer runs when CI
disagrees with them.

    python scripts/run_package_consumer_lane.py --reports reports-linux

**Which packages it runs is read, not listed.** The set is every row in
PACKAGE_CONTRACT.md section 4 that carries a `find_package` contract, taken
through the driver's own parser -- so a thirteenth package arrives in this lane
by acquiring a contract row, and cannot arrive in the contract without arriving
here. A list in this file would be the one place the twelve could quietly become
eleven.

**The OpenUSD prefix is resolved, not remembered.** `ost env <platform>
--profile <profile> --json` names the runtime this host materialized, which is
where a consumer of these packages gets OpenUSD from; `--extra-prefix` is how it
reaches the driver, exactly as it does for anyone else. It is never the
workspace build tree, and there is not one here: this lane builds no workspace.

Exit 0 when every package met every criterion this host can check, 1 when one
did not, 2 when the run could not be set up. Criterion 6 is not answered here at
all -- one host cannot -- and `scripts/check_package_closures.py` is what reads
the reports this writes from all three.
"""
from __future__ import annotations

import argparse
import json
import pathlib
import platform
import shutil
import subprocess
import sys

REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
DRIVER = REPO_ROOT / "scripts/check_package_consumer.py"

sys.path.insert(0, str(REPO_ROOT / "scripts"))
import check_package_consumer as driver  # noqa: E402


def fail_setup(msg: str) -> int:
    print(f"SETUP: {msg}")
    return 2


def runtime_prefix(runtime_platform: str, profile: str) -> str:
    """Where OpenUSD is on this host, from `ost` rather than from a path this
    file remembers. The README tells a workstation to read the same directory
    out of `.strata/targets/<target>/toolchain.cmake`; that file is written by a
    workspace build, and this lane deliberately does not run one."""
    ost = shutil.which("ost")
    if not ost:
        return ""
    result = subprocess.run(
        [ost, "env", runtime_platform, "--profile", profile, "--json"],
        text=True, capture_output=True, errors="replace")
    if result.returncode:
        return ""
    try:
        data = json.loads(result.stdout)["data"]
    except (ValueError, KeyError):
        return ""
    return data["prefix"] if data.get("pulled") else ""


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--reports", default="package-consumer-reports",
                    metavar="DIR", help="where the JSON reports are written")
    ap.add_argument("--extra-prefix", action="append", default=[],
                    metavar="DIR",
                    help="a prefix holding non-workspace packages. When "
                         "omitted, `ost env` names the materialized runtime")
    ap.add_argument("--platform", default="cy2026",
                    help="the runtime platform to resolve OpenUSD from")
    ap.add_argument("--profile", default="usd")
    ap.add_argument("--generator", help="CMake generator, passed through")
    ap.add_argument("--package", action="append", default=[], metavar="NAME",
                    help="run only these packages (default: every row with a "
                         "find_package contract)")
    args = ap.parse_args()

    rows = driver.contract_rows()
    packages = [name for name, row in rows.items()
                if "reserved" not in (row["target"], row["product"])
                and row["target"] not in ("-", "—")]
    if args.package:
        unknown = [p for p in args.package if p not in packages]
        if unknown:
            return fail_setup(
                f"{', '.join(unknown)} carries no find_package contract in "
                f"PACKAGE_CONTRACT.md section 4")
        packages = [p for p in packages if p in args.package]

    extra = list(args.extra_prefix)
    if not extra:
        prefix = runtime_prefix(args.platform, args.profile)
        if prefix:
            extra = [prefix]
            print(f"OpenUSD from `ost env {args.platform} "
                  f"--profile {args.profile}`: {prefix}")
        else:
            # Four of the twelve need no OpenUSD, so this is not fatal -- but
            # it is the difference between a lane that measured twelve packages
            # and one that measured four while printing nothing about it.
            print("note: no materialized runtime and no --extra-prefix; only "
                  "the packages that need no OpenUSD can pass")

    reports = pathlib.Path(args.reports)
    reports.mkdir(parents=True, exist_ok=True)

    outcomes: dict = {}
    for package in packages:
        print(f"\n{'=' * 70}\n=== {package}\n{'=' * 70}", flush=True)
        command = [sys.executable, str(DRIVER), package,
                   "--json", str(reports / f"{package}.json")]
        for prefix in extra:
            command += ["--extra-prefix", prefix]
        if args.generator:
            command += ["--generator", args.generator]
        outcomes[package] = subprocess.run(command).returncode

    print(f"\n{'=' * 70}")
    print(f"PACKAGE_CONTRACT.md section 5, criteria 1-5 on "
          f"{platform.system()} -- {len(packages)} packages")
    for package in packages:
        code = outcomes[package]
        print(f"  {'pass' if code == 0 else 'FAIL'}  {package}"
              + ("" if code == 0 else f"  (exit {code})"))
    failed = [p for p, code in outcomes.items() if code != 0]
    if failed:
        print(f"\nFAIL: {', '.join(failed)}. Fix the config, never the fixture "
              f"(roadmap/packaging-hardening.md PKG-3).")
        return 1
    print(f"\nPASS: every package met every criterion {platform.system()} can "
          f"check. Criterion 6 needs three platforms and is answered by "
          f"scripts/check_package_closures.py.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
