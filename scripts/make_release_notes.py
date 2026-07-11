#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Render GitHub release notes for one version from the changelog + template.

Fills ``docs/RELEASE_NOTES_TEMPLATE.md`` with:

* ``{version}`` / ``{tag}``      — the release version (default: repo ``VERSION``).
* ``{changelog}``                — that version's ``## [X.Y.Z]`` section of
                                   ``CHANGELOG.md`` (heading dropped, body kept).
* ``{schema_contract}``          — the "Current schema contract version" the
                                   changelog preamble declares.
* ``{checksums}``                — contents of a ``SHA256SUMS`` file, when the
                                   release workflow passes one.

The changelog section heading must be *finalized* (a date, not "unreleased")
unless ``--allow-unreleased`` is given — tagging with an unfinished changelog
is the mistake this guard exists to catch.

Usage (what .github/workflows/release.yml runs):

    python scripts/make_release_notes.py --version 0.1.0 \
        --checksums dist-release/SHA256SUMS --out release-notes.md
"""
from __future__ import annotations

import argparse
import pathlib
import re
import sys

REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]


def changelog_section(changelog: str, version: str) -> tuple[str, str]:
    """Return (heading, body) of the ``## [version]`` section."""

    lines = changelog.splitlines()
    start = None
    for i, line in enumerate(lines):
        if re.match(rf"^## \[{re.escape(version)}\]", line):
            start = i
            break
    if start is None:
        raise SystemExit(f"ERROR: CHANGELOG.md has no '## [{version}]' section")
    end = len(lines)
    for j in range(start + 1, len(lines)):
        if lines[j].startswith("## ") or lines[j].startswith("[Unreleased]:"):
            end = j
            break
    body = "\n".join(lines[start + 1:end]).strip()
    return lines[start], body


def schema_contract_version(changelog: str) -> str:
    m = re.search(r"Current schema contract version:\s*\*\*(\d+)\*\*", changelog)
    return m.group(1) if m else "?"


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version",
                        default=(REPO_ROOT / "VERSION").read_text().strip(),
                        help="release version X.Y.Z (default: repo VERSION file)")
    parser.add_argument("--changelog", default=str(REPO_ROOT / "CHANGELOG.md"))
    parser.add_argument("--template",
                        default=str(REPO_ROOT / "docs" / "RELEASE_NOTES_TEMPLATE.md"))
    parser.add_argument("--checksums",
                        help="SHA256SUMS file to inline into the notes")
    parser.add_argument("--allow-unreleased", action="store_true",
                        help="do not fail when the section heading still says 'unreleased'")
    parser.add_argument("--out", help="write the notes here (default: stdout)")
    args = parser.parse_args(argv)

    changelog = pathlib.Path(args.changelog).read_text(encoding="utf-8")
    heading, body = changelog_section(changelog, args.version)
    if "unreleased" in heading.lower() and not args.allow_unreleased:
        raise SystemExit(
            f"ERROR: changelog heading is not finalized: {heading!r}\n"
            f"Replace 'unreleased' with the release date before tagging "
            f"(or pass --allow-unreleased for a dry run).")

    checksums = "(appended by the release workflow)"
    if args.checksums:
        checksums = pathlib.Path(args.checksums).read_text(encoding="utf-8").strip()

    notes = pathlib.Path(args.template).read_text(encoding="utf-8")
    for key, value in {
        "{version}": args.version,
        "{tag}": f"v{args.version}",
        "{changelog}": body,
        "{schema_contract}": schema_contract_version(changelog),
        "{checksums}": checksums,
    }.items():
        notes = notes.replace(key, value)

    if args.out:
        pathlib.Path(args.out).write_text(notes, encoding="utf-8", newline="\n")
        print(f"wrote release notes for v{args.version} -> {args.out}")
    else:
        sys.stdout.write(notes)
    return 0


if __name__ == "__main__":
    sys.exit(main())
