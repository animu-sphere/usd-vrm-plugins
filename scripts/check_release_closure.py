#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Record the release job's closure checklist; skipped checks never pass.

The manifest supplies the member set, ost's package JSON supplies the shipped
set, and GitHub's steps context supplies execution outcomes. This records
evidence from one product cell, not a claim about the other platforms or the
lookdev publication. No third-party Python modules are required.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import re
import sys
import tomllib

ROOT = Path(__file__).resolve().parents[1]
GATES = {
    "closure_graph": "Workspace dependency graph",
    "closure_standalone": "Standalone bundle builds against installed dependencies",
    "closure_workspace": "Workspace verification pyramid",
    "closure_package": "Aggregate product packaging",
    "closure_reproducibility": "Repeated package digests",
    "closure_libraries": "Packaged shared-library closure",
    "closure_clean_install": "Composed clean-install smoke and texture resolution",
    "closure_exec": "Installed-product OpenExec parity",
    "closure_imaging": "Installed-product imaging suites",
    "closure_export": "Installed-product native and animated export",
    "closure_vrma": "Packaged VRMA verification pyramid",
    "closure_consumers": "Installed CMake consumer configure, build and run",
}


def inventory(root: Path) -> dict:
    workspace = tomllib.loads((root / "openstrata.toml").read_text(encoding="utf-8"))["workspace"]
    members = {}
    bundles = {}
    for directory, filename, key, kind in (
        ("plugins", "openstrata.plugin.yaml", "name", "bundle"),
        ("tools", "openstrata.tool.yaml", "id", "tool"),
    ):
        for descriptor in sorted((root / directory).glob(f"*/{filename}")):
            body = descriptor.read_text(encoding="utf-8")
            match = re.search(rf"^  {key}:\s*([A-Za-z0-9_-]+)\s*$", body, re.M)
            if not match:
                raise ValueError(f"cannot read identity from {descriptor.relative_to(root)}")
            name = match[1]
            path = descriptor.parent.relative_to(root).as_posix()
            if name in members:
                raise ValueError(f"duplicate descriptor identity: {name}")
            if path not in workspace["members"]:
                raise ValueError(f"{path} is absent from workspace.members")
            members[name] = kind
            if kind == "bundle":
                bundles[name] = path
    declared = workspace["release_members"]
    if len(declared) != len(set(declared)) or set(declared) != set(members):
        raise ValueError("release_members must name every plugin/tool descriptor exactly once")
    if workspace.get("release_exclude"):
        raise ValueError("product plugin/tool exclusions need a release-closure policy change")
    return {"members": members, "bundles": bundles,
            "installData": workspace.get("install_data", [])}


def package_findings(data: dict, expected: dict) -> list[str]:
    findings = []
    declared = data["release_members"]
    if len(declared) != len(set(declared)) or set(declared) != set(expected):
        findings.append("packager release_members disagree with the descriptors")
    # Non-product packages may exist; only the declared release set ships.
    packages = [p for p in data["packages"] if p["name"] in expected]
    names = [p["name"] for p in packages]
    if len(names) != len(set(names)) or set(names) != set(expected):
        findings.append("packaged member identities are missing or duplicated")
    for package in packages:
        if package["member"] != expected[package["name"]]:
            findings.append(f"wrong packaged member kind: {package['name']}")
    if data["product"]["members"] != len(expected):
        findings.append("product member count disagrees with the descriptors")
    return findings


def checklist(inv: dict, steps: dict, package: dict) -> dict:
    checks = []
    for ident, label in GATES.items():
        step = steps.get(ident, {})
        passed = step.get("outcome") == "success" and step.get("conclusion") == "success"
        checks.append({"id": ident, "description": label,
                       "status": "passed" if passed else "failed",
                       "outcome": step.get("outcome", "missing"),
                       "conclusion": step.get("conclusion", "missing")})
    try:
        findings = package_findings(package, inv["members"])
    except (KeyError, TypeError) as exc:
        findings = [f"missing or invalid package evidence: {exc}"]
    checks.append({"id": "membership", "description": "Manifest and packaged member identities",
                   "status": "failed" if findings else "passed", "findings": findings})
    # No project-owned data is installed today. A new mapping must acquire a
    # real installed-data probe before the release can claim closure for it.
    checks.append({"id": "installed_data", "description": "Installed project data resolves",
                   "status": "failed" if inv["installData"] else "notApplicable",
                   "reason": "install_data requires an installed-data probe" if inv["installData"]
                   else "workspace declares no install_data; consumed motion profiles are owned elsewhere"})
    return {"reportVersion": 1, "passed": all(c["status"] != "failed" for c in checks),
            "members": dict(sorted(inv["members"].items())), "checks": checks}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bundle-paths", action="store_true",
                        help="print the standalone build list from the descriptors")
    parser.add_argument("--package-json", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if not args.bundle_paths and (not args.package_json or not args.output):
        parser.error("--package-json and --output are required")
    try:
        inv = inventory(ROOT)
        if args.bundle_paths:
            # ost composes each bundle's installed dependency closure. Keep
            # vrmSchema first and usdVrmaFileFormat last: the latter leaves
            # vrmContainer in the shared prefix used by packaging (report 40).
            for name in sorted(inv["bundles"], key=lambda n: (n != "vrmSchema", n == "usdVrmaFileFormat", n)):
                print(inv["bundles"][name])
            return 0
        steps = json.loads(os.environ.get("RELEASE_CLOSURE_STEPS", "{}"))
        package_error = None
        try:
            package = json.loads(args.package_json.read_text(encoding="utf-8"))["data"]
        except (OSError, ValueError, KeyError) as exc:
            package = {}
            package_error = str(exc)
        report = checklist(inv, steps, package)
        if package_error:
            report["packageError"] = package_error
    except (OSError, ValueError, KeyError, TypeError) as exc:
        if args.bundle_paths:
            print(f"release closure: {exc}", file=sys.stderr)
            return 1
        report = {"reportVersion": 1, "passed": False, "error": str(exc)}
    report.update({"commit": os.environ.get("GITHUB_SHA", ""),
                   "runId": os.environ.get("GITHUB_RUN_ID", ""),
                   "cell": os.environ.get("RELEASE_CLOSURE_CELL", "")})
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    for check in report.get("checks", []):
        print(f"{check['status']}: {check['description']}")
    if "error" in report:
        print(report["error"], file=sys.stderr)
    print("release closure: " + ("PASS" if report["passed"] else "FAIL"))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
