#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""The pins a hand-authored workflow needs, read from the CI contract.

`openstrata.ci.yaml` is the CI contract and `.github/workflows/ost-*.yml` is
renderer output from it. A lane `ost ci generate` cannot express has to be
hand-authored anyway -- `kind: consumer` is refused by the schema:

    error[MANIFEST_INVALID]: cells[N].kind: unknown variant `consumer`,
    expected `bundle` or `workspace`

-- and the failure mode of a hand-authored workflow in this repository is
documented rather than hypothetical: `release.yml` carries its own copy of the
X11 step, the `ost` pin and the runtime digests, it was missed when CI was
re-pinned to 26.08, and the v0.5.0 tag build failed on it while every PR lane
stayed green.

`ost ci matrix` exists for exactly this: *emit the resolved cells so a workflow
`ost ci generate` cannot express can consume the same pins instead of copying
them*. This script is the consumer of that, and it emits the selected cells as
the `{"include": [...]}` object a workflow's `strategy.matrix` takes whole.

    python scripts/ci_pins.py bootstrap-version
    python scripts/ci_pins.py lane-matrix

**The one value that cannot come from `ost` is which `ost` to install**, so
`bootstrap-version` reads it out of the contract file directly, with a regex
rather than a YAML parser (the same rule `check_docs.py` follows -- no
dependency a lane has to install before it can check anything). That reading is
not trusted on its own: `lane-matrix` re-reads the same value through
`ost ci matrix` and refuses to emit anything if the two disagree, so the
bootstrap step's version is checked by the tool it bootstrapped.
"""
from __future__ import annotations

import argparse
import json
import pathlib
import re
import shutil
import subprocess
import sys

REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
MATRIX = REPO_ROOT / "openstrata.ci.yaml"

# `bootstrap:` at column 0, then the first quoted `version:` under it. Anchored
# on the block header so a `version:` belonging to any other top-level key
# cannot answer for it.
BOOTSTRAP_VERSION = re.compile(
    r"^bootstrap:\s*$.*?^\s+version:\s*\"([^\"]+)\"", re.M | re.S)


def fail(msg: str) -> "NoReturn":
    print(f"error: {msg}", file=sys.stderr)
    raise SystemExit(2)


def declared_bootstrap_version() -> str:
    if not MATRIX.exists():
        fail(f"{MATRIX} is missing")
    m = BOOTSTRAP_VERSION.search(MATRIX.read_text(encoding="utf-8"))
    if not m:
        fail(f"{MATRIX} declares no bootstrap.ost.version")
    return m.group(1)


def resolved_matrix(lane: str) -> dict:
    ost = shutil.which("ost")
    if not ost:
        fail("`ost` is not on PATH; bootstrap it first "
             "(`ci_pins.py bootstrap-version` is the value to bootstrap)")
    result = subprocess.run([ost, "ci", "matrix", "--json", "--lane", lane],
                            text=True, capture_output=True, errors="replace")
    if result.returncode:
        fail(f"`ost ci matrix --lane {lane}` failed:\n{result.stderr.strip()}")
    try:
        return json.loads(result.stdout)["data"]
    except (ValueError, KeyError) as exc:
        fail(f"`ost ci matrix --json` did not emit a data object: {exc}")


# `ost ci generate` renders these onto every `ost artifact verify` it emits, out
# of the cell's `require_evidence`. A hand-authored lane that verified an
# artifact with a weaker gate than the generated ones would be pulling the same
# digest under a different promise.
EVIDENCE_FLAGS = {
    "all": "--require-sbom --require-provenance",
    "sbom": "--require-sbom",
    "provenance": "--require-provenance",
    "none": "",
}


def pins(found: dict) -> dict:
    """One cell's pins, in the spellings a workflow step consumes."""
    host_packages = found.get("host_packages") or {}
    evidence = found.get("require_evidence") or "none"
    if evidence not in EVIDENCE_FLAGS:
        fail(f"cell `{found['name']}` requires evidence `{evidence}`, which "
             f"this script cannot render as flags; the ones it knows are "
             f"{', '.join(sorted(EVIDENCE_FLAGS))}")
    return {
        "name": found["name"],
        "os": found["os"],
        "runs_on": found["runs_on"],
        "platform": found["platform"],
        "profile": found["profile"],
        "runtime_artifact": found["runtime_artifact"],
        "runtime_remote": found.get("runtime_remote") or "",
        "minimum_trust": found.get("minimum_trust") or "local",
        "evidence_flags": EVIDENCE_FLAGS[evidence],
        "host_python": found.get("host_python") or "",
        "host_packages_apt": " ".join(host_packages.get("apt") or []),
        "host_packages_brew": " ".join(host_packages.get("brew") or []),
    }


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="command", required=True)

    sub.add_parser("bootstrap-version",
                   help="the `ost` version the contract pins, read without "
                        "`ost` (the one value that cannot come from it)")

    def selection(parser):
        parser.add_argument("--kind", default="workspace",
                            help="cell kind (default: workspace)")
        parser.add_argument(
            "--verify", default="test",
            help="which verification the cell runs (default: test). The "
                 "default is load-bearing rather than tidiness: "
                 "`workspace-graph-pr` is a Linux workspace cell too, and a "
                 "`verify: graph` cell materializes no runtime -- its rendered "
                 "job stops after the checkout -- so its pins are on record and "
                 "never pulled. A consumer lane that followed it would ask for "
                 "a runtime nothing had verified")
        parser.add_argument("--lane", default="pull_request")
        return parser

    matrix = selection(sub.add_parser(
        "lane-matrix",
        help="every selected cell as the `{\"include\": [...]}` object a "
             "workflow's `strategy.matrix` takes whole"))
    matrix.add_argument(
        "--expect", type=int, default=3, metavar="N",
        help="how many cells must be selected (default: 3, the three OS "
             "criterion 6 names). Fewer is a lane silently narrowing")

    args = ap.parse_args()

    if args.command == "bootstrap-version":
        print(declared_bootstrap_version())
        return 0

    data = resolved_matrix(args.lane)
    resolved = data.get("bootstrap", {}).get("version", "")
    declared = declared_bootstrap_version()
    if resolved != declared:
        fail(f"`ost ci matrix` resolves bootstrap.ost.version to {resolved!r} "
             f"and this script reads {declared!r} out of {MATRIX.name}. One of "
             f"the two readings is wrong, and the bootstrap step already used "
             f"the second")

    cells = [c for c in data.get("cells", [])
             if c.get("kind") == args.kind and c.get("lane") == args.lane
             and (c.get("verify") or "") == args.verify]

    if args.command == "lane-matrix":
        if len(cells) != args.expect:
            fail(f"the {args.lane} lane has {len(cells)} `{args.kind}` cell(s) "
                 f"with `verify: {args.verify}` "
                 f"({', '.join(c['name'] for c in cells) or 'none'}) and this "
                 f"lane needs {args.expect}. Criterion 6 is a question about "
                 f"three platforms, and a run that asked it of two would "
                 f"answer a different one while printing a pass")
        systems = sorted({c["os"] for c in cells})
        if len(systems) != len(cells):
            fail(f"the selected cells cover {len(systems)} operating "
                 f"system(s) ({', '.join(systems)}) across {len(cells)} cells, "
                 f"so at least one platform would be measured twice and "
                 f"another not at all")
        print(json.dumps({"include": [pins(c) for c in cells]},
                         sort_keys=True))
        return 0

    return fail(f"unknown command {args.command}")


if __name__ == "__main__":
    sys.exit(main())
