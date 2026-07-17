#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Check the documentation against the manifests — contract values only.

The docs are hand-written on purpose; this does not generate them. It asserts
the handful of facts that silently rot when the workspace changes shape:

1. every bundle/library identity in the manifests appears in the root README's
   component table and in WORKSPACE.md's identity table;
2. no current-state document describes `usdVrm` as a bundle id or points at the
   pre-rename `plugins/usdVrm/` path (history is exempt — see HISTORY);
3. the schema contract version in the docs matches vrmSchema's manifest;
4. every local markdown link resolves.

Run: python scripts/check_docs.py   (exit 1 on any failure)
"""
from __future__ import annotations

import pathlib
import re
import sys

REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]

# Historical records. They describe the world as it was and must not be
# rewritten to match the world as it is (see docs/reports/README.md).
HISTORY = (
    "docs/reports/",     # dated dogfooding evidence + delivery log
    "docs/releases/",    # immutable per-version release records
    "CHANGELOG.md",      # released sections are history
)


def is_history(rel: str) -> bool:
    rel = rel.replace("\\", "/")
    return any(rel.startswith(h) or rel == h for h in HISTORY)


def read(rel: str) -> str:
    return (REPO_ROOT / rel).read_text(encoding="utf-8")


def scalar(text: str, key: str) -> str | None:
    """Pull `  key: value` out of a manifest without a YAML dependency."""
    m = re.search(rf"^\s*{re.escape(key)}:\s*(.+?)\s*$", text, re.M)
    return m.group(1).strip().strip("'\"") if m else None


def discover() -> tuple[dict[str, str], dict[str, str]]:
    """Return ({bundle id: manifest path}, {library id: descriptor path})."""
    bundles, libraries = {}, {}
    for m in sorted(REPO_ROOT.glob("plugins/*/openstrata.plugin.yaml")):
        name = scalar(m.read_text(encoding="utf-8"), "name")
        if name:
            bundles[name] = str(m.relative_to(REPO_ROOT)).replace("\\", "/")
    for m in sorted(REPO_ROOT.glob("libs/*/openstrata.library.yaml")):
        lid = scalar(m.read_text(encoding="utf-8"), "id")
        if lid:
            libraries[lid] = str(m.relative_to(REPO_ROOT)).replace("\\", "/")
    return bundles, libraries


def check_inventory(failures: list[str]) -> None:
    bundles, libraries = discover()
    if not bundles:
        failures.append("no plugin manifests found under plugins/*/ — "
                        "has the layout moved?")
        return

    readme = read("README.md")
    workspace = read("docs/architecture/WORKSPACE.md")
    for ident, manifest in {**bundles, **libraries}.items():
        if not re.search(rf"`{re.escape(ident)}`", readme):
            failures.append(
                f"README.md does not mention `{ident}` (declared in {manifest})")
        if not re.search(rf"`{re.escape(ident)}`", workspace):
            failures.append(
                f"docs/architecture/WORKSPACE.md does not mention `{ident}` "
                f"(declared in {manifest})")

    # usdVrm is the aggregate product name, never a bundle id.
    if "usdVrm" in bundles:
        failures.append(
            "plugins/*/openstrata.plugin.yaml declares a bundle named `usdVrm`; "
            "the aggregate product name must not be a bundle id "
            "(docs/architecture/WORKSPACE.md §1)")


def check_no_stale_paths(failures: list[str]) -> None:
    """The pre-rename bundle path must not survive in current-state docs."""
    stale = re.compile(r"plugins/usdVrm(?![A-Za-z])")
    for p in sorted(REPO_ROOT.glob("**/*.md")):
        rel = str(p.relative_to(REPO_ROOT)).replace("\\", "/")
        if rel.startswith(("build/", "scratch/")) or is_history(rel):
            continue
        for i, line in enumerate(p.read_text(encoding="utf-8",
                                             errors="replace").splitlines(), 1):
            if stale.search(line):
                failures.append(
                    f"{rel}:{i} references the pre-rename path `plugins/usdVrm` "
                    f"(now plugins/usdVrmFileFormat)")


def check_schema_contract(failures: list[str]) -> None:
    manifest = read("plugins/vrmSchema/openstrata.plugin.yaml")
    contract = scalar(manifest, "contract")
    if not contract:
        failures.append("plugins/vrmSchema/openstrata.plugin.yaml declares no "
                        "schema.contract")
        return
    doc = read("plugins/vrmSchema/docs/SCHEMA_CONTRACT.md")
    if not re.search(rf"\bv?{re.escape(contract)}\b", doc.split("\n")[0]):
        failures.append(
            f"SCHEMA_CONTRACT.md's title does not name contract version "
            f"{contract} (from the vrmSchema manifest): {doc.splitlines()[0]!r}")
    ffmt = read("plugins/usdVrmFileFormat/openstrata.plugin.yaml")
    pinned = scalar(ffmt, "contract")
    if pinned != contract:
        failures.append(
            f"usdVrmFileFormat pins schema contract {pinned!r} but vrmSchema "
            f"provides {contract!r}")


LINK = re.compile(r"\[[^\]]*\]\(([^)]+)\)")


def check_links(failures: list[str]) -> None:
    for p in sorted(REPO_ROOT.glob("**/*.md")):
        rel = str(p.relative_to(REPO_ROOT)).replace("\\", "/")
        if rel.startswith(("build/", "scratch/")):
            continue
        for target in LINK.findall(p.read_text(encoding="utf-8",
                                               errors="replace")):
            t = target.strip()
            if t.startswith(("http://", "https://", "mailto:", "#")):
                continue
            path = t.partition("#")[0]
            if path and not (p.parent / path).resolve().exists():
                failures.append(f"{rel}: broken link -> {t}")


def main() -> int:
    # The findings quote doc prose, which is not ASCII. Don't let a legacy
    # console encoding (e.g. cp932) turn a real failure into a UnicodeEncodeError.
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")

    failures: list[str] = []
    for check in (check_inventory, check_no_stale_paths,
                  check_schema_contract, check_links):
        check(failures)

    if failures:
        print(f"FAIL: {len(failures)} documentation inconsistency(ies)\n")
        for f in failures:
            print(f"  - {f}")
        print("\nThe docs are hand-written; fix the prose, not this check — "
              "unless the manifests really did change shape.")
        return 1

    bundles, libraries = discover()
    print(f"OK: docs consistent with {len(bundles)} bundle(s) "
          f"({', '.join(sorted(bundles))}) and {len(libraries)} library(ies) "
          f"({', '.join(sorted(libraries))})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
