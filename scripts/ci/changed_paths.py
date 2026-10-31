#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Conservative PR routing, using git objects rather than a capped files API.

Only Markdown under docs/ and the root README/changelog are prose by default. Markdown
read by packaging/release tools remains a full-CI input. Unknown or empty
diffs select full CI. An unreadable diff fails the changes job and required gate.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path, PurePosixPath
import re
import subprocess


FULL_DOCS = {
    "docs/architecture/PACKAGE_CONTRACT.md",  # check_package_consumer.py input
    "docs/contributing/RELEASE_NOTES_TEMPLATE.md",  # make_release_notes.py input
}


def prose(path: str) -> bool:
    parts = PurePosixPath(path).parts
    if not path or "\\" in path or any(p in {"", ".", ".."} for p in path.split("/")):
        return False
    return path in {"README.md", "CHANGELOG.md"} or (
        len(parts) > 1 and parts[0] == "docs" and path.endswith(".md")
        and path not in FULL_DOCS
    )


def requires_full(paths: list[str]) -> bool:
    return not paths or any(not prose(path) for path in paths)


def changed_paths(base: str, head: str, cwd: Path | None = None) -> list[str]:
    for sha in (base, head):
        if not re.fullmatch(r"[0-9a-fA-F]{40}", sha):
            raise ValueError("PR base/head must be full git SHA-1 object names")
    ancestor = subprocess.check_output(
        ["git", "merge-base", base, head], cwd=cwd, text=True).strip()
    # --no-renames exposes BOTH sides of a rename, including code -> docs.
    # NUL delimiters preserve spaces, Unicode, tabs and newlines in filenames.
    diff = subprocess.check_output(
        ["git", "diff", "--no-ext-diff", "--no-renames", "--name-only", "-z",
         ancestor, head, "--"], cwd=cwd)
    return [p.decode("utf-8", errors="surrogateescape") for p in diff.split(b"\0") if p]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--event", type=Path, default=os.environ.get("GITHUB_EVENT_PATH"))
    args = parser.parse_args()
    event_name = os.environ.get("GITHUB_EVENT_NAME", "")
    if event_name == "workflow_dispatch":
        full, count = True, 0
    elif event_name == "pull_request":
        event = json.loads(Path(args.event).read_text(encoding="utf-8"))
        pr = event["pull_request"]
        paths = changed_paths(pr["base"]["sha"], pr["head"]["sha"])
        full, count = requires_full(paths), len(paths)
    else:
        raise ValueError(f"unsupported routing event: {event_name!r}")
    output = f"full={str(full).lower()}\n"
    print(output, end="")
    if os.environ.get("GITHUB_OUTPUT"):
        with open(os.environ["GITHUB_OUTPUT"], "a", encoding="utf-8") as stream:
            stream.write(output)
    if os.environ.get("GITHUB_STEP_SUMMARY"):
        with open(os.environ["GITHUB_STEP_SUMMARY"], "a", encoding="utf-8") as stream:
            stream.write(f"CI route: **{'full' if full else 'docs-only'}** ({count} changed paths).\n")


if __name__ == "__main__":
    main()
