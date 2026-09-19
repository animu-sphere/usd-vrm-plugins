#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Check the moving motion headers for VRM vocabulary, against a ledger.

The motion migration's MIG-0 asks for this "as a boundary check rather than a
review" (docs/roadmap/motion-foundation-split.md §2): every public header of
`motionCore`, `motionRuntime` and the generic half of `vrmRetarget`
(docs/architecture/WORKSPACE.md §9.5) is leaving for `usd-motion-plugins`,
whose contract says the core carries no VRM meaning. A VRM name in one of them
is either renamed on arrival (WORKSPACE.md §9.3) or a boundary defect recorded
as a §9.5 finding, and the ledger says which.

What is scanned, and why only that:

* **Headers, with comments removed.** The headers explain themselves with
  VRM as context ("the VRM 1.0 humanoid vocabulary"), and prose that names
  where a rule came from is not a dependency on it. Identifiers and string
  literals are what a consumer of the package would spell.
* **String literals in the sources.** A diagnostic code, a trace keyword or a
  message a user reads is part of the contract even though no header
  declares it.

It fails on three things:

* **An unclassified name** -- one the scan finds and the ledger does not
  dispose of. This is the case the check exists for: a new VRM name in a
  header that is about to leave.
* **A stale ledger row** -- one the scan no longer finds. A ledger that only
  grows would keep answering for names that are gone, and would stop being
  a list of what is left to do.
* **A missing anchor.** Some findings are concepts no vocabulary pattern can
  see (`GetRequiredBones` is VRM 1.0's required set under a neutral name).
  The ledger names the identifier each such finding is about, and the check
  fails when that identifier disappears, so a finding is closed by an edit to
  the ledger rather than by a rename that quietly makes it invisible.

Usage: check_motion_vocabulary.py [--root DIR] [--ledger FILE]
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

_BLOCK_COMMENT = re.compile(r"/\*.*?\*/", re.DOTALL)
_LINE_COMMENT = re.compile(r"//[^\n]*")
_STRING = re.compile(r'"(?:\\.|[^"\\\n])*"')
_IDENTIFIER = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")

DISPOSITIONS = {
    # Renamed on arrival, to the name in `to` (WORKSPACE.md §9.3).
    "rename",
    # Named after the identity it lives in, and renamed with it: a namespace,
    # an export macro, an include path.
    "identity",
    # A diagnostic code string, restyled per the destination's DIAG-O1.
    "diagnostic",
    # A concept, not a name: a WORKSPACE.md §9.5 finding, fixed on arrival.
    "finding",
}


def _strip_comments(text: str) -> str:
    # Strings first would be more exact, but no header here puts `//` inside a
    # string literal, and a scan that mis-stripped one would report a name it
    # should not -- a loud failure, not a silent pass.
    return _LINE_COMMENT.sub("", _BLOCK_COMMENT.sub("", text))


def _expand(root: Path, patterns: list[str]) -> list[Path]:
    files: list[Path] = []
    for pattern in patterns:
        matches = sorted(p for p in root.glob(pattern) if p.is_file())
        if not matches:
            # A scope entry that matches nothing is a scope that moved; a check
            # that silently scanned less would pass for the wrong reason.
            raise SystemExit(f"scope pattern matches no file: {pattern}")
        files.extend(matches)
    return files


def scan(root: Path, ledger: dict) -> tuple[dict[tuple[str, str], set[str]], str]:
    """Return {(kind, text): {files}} for every vocabulary hit, and all code."""
    vocabulary = re.compile(ledger["vocabulary"], re.IGNORECASE)
    hits: dict[tuple[str, str], set[str]] = {}
    all_code: list[str] = []

    def record(kind: str, text: str, path: Path) -> None:
        hits.setdefault((kind, text), set()).add(path.relative_to(root).as_posix())

    for path in _expand(root, ledger["scope"]["headers"]):
        code = _strip_comments(path.read_text(encoding="utf-8"))
        all_code.append(code)
        for literal in _STRING.findall(code):
            if vocabulary.search(literal):
                record("string", literal[1:-1], path)
        for identifier in _IDENTIFIER.findall(_STRING.sub('""', code)):
            if vocabulary.search(identifier):
                record("identifier", identifier, path)
    for path in _expand(root, ledger["scope"]["sources"]):
        code = _strip_comments(path.read_text(encoding="utf-8"))
        all_code.append(code)
        for literal in _STRING.findall(code):
            if vocabulary.search(literal):
                record("string", literal[1:-1], path)
    return hits, "\n".join(all_code)


def check(root: Path, ledger: dict) -> list[str]:
    errors: list[str] = []
    findings = {int(k) for k in ledger.get("findings", {})}
    rows: dict[tuple[str, str], dict] = {}
    for row in ledger["names"]:
        key = (row["kind"], row["text"])
        if key in rows:
            errors.append(f"ledger lists {row['kind']} `{row['text']}` twice")
        rows[key] = row
        disposition = row.get("disposition")
        if disposition not in DISPOSITIONS:
            errors.append(f"ledger: {row['kind']} `{row['text']}` has disposition "
                          f"{disposition!r}, not one of {sorted(DISPOSITIONS)}")
        if disposition == "rename" and not row.get("to"):
            errors.append(f"ledger: rename of `{row['text']}` names no `to`")
        if disposition == "finding" and row.get("finding") not in findings:
            errors.append(f"ledger: `{row['text']}` cites finding "
                          f"{row.get('finding')!r}, which the ledger does not list")

    hits, code = scan(root, ledger)
    for key in sorted(hits):
        if key not in rows:
            kind, text = key
            errors.append(f"unclassified: {kind} `{text}` in "
                          f"{', '.join(sorted(hits[key]))}")
    for key in sorted(rows):
        if key not in hits:
            kind, text = key
            errors.append(f"stale: ledger lists {kind} `{text}`, which the scan "
                          f"no longer finds")

    identifiers = set(_IDENTIFIER.findall(_STRING.sub('""', code)))
    for anchor in ledger.get("anchors", []):
        if anchor["finding"] not in findings:
            errors.append(f"ledger: anchor `{anchor['identifier']}` cites finding "
                          f"{anchor['finding']!r}, which the ledger does not list")
        if anchor["identifier"] not in identifiers:
            errors.append(f"missing anchor: finding {anchor['finding']} is about "
                          f"`{anchor['identifier']}`, which the scanned code no "
                          f"longer declares or uses")
    return errors


def main() -> int:
    here = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root", type=Path, default=here)
    parser.add_argument("--ledger", type=Path,
                        default=here / "tests" / "boundary" / "motion-vocabulary.json")
    args = parser.parse_args()

    ledger = json.loads(args.ledger.read_text(encoding="utf-8"))
    errors = check(args.root.resolve(), ledger)
    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    print(f"motion vocabulary: {len(ledger['names'])} names and "
          f"{len(ledger.get('anchors', []))} anchors, every one accounted for")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
