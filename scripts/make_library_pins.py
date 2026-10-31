#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Write the consumer pin table from what a release actually packaged or pushed.

Each row of ``--rows`` is tab-separated: kind, name, version, target, archive
digest and, once pushed, the OCI manifest digest. A missing member or target
fails instead of producing a table that is right about everything it mentions
and silently missing a platform.
"""
from __future__ import annotations

import argparse
import json
import pathlib

SECTIONS = {"library": "libraries", "bundle": "bundles", "tool": "tools"}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rows", required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--repository", required=True)
    parser.add_argument("--targets", type=int, required=True,
                        help="target count every member must have")
    parser.add_argument("--want", action="append", default=[], metavar="KIND=COUNT",
                        help="member count declared by the tree, per kind")
    parser.add_argument("--published", action="store_true")
    parser.add_argument("--out", required=True)
    args = parser.parse_args()

    want = {kind: 0 for kind in SECTIONS}
    for item in args.want:
        kind, _, count = item.partition("=")
        if kind not in SECTIONS:
            raise SystemExit(f"unknown kind {kind!r}")
        want[kind] = int(count)

    pins: dict[str, dict[str, dict]] = {kind: {} for kind in SECTIONS}
    for line in pathlib.Path(args.rows).read_text(encoding="utf-8").splitlines():
        if not line.strip():
            continue
        parts = line.split("\t")
        kind, name, version, target, digest = parts[:5]
        oci = parts[5] if len(parts) > 5 else ""
        if kind not in pins:
            raise SystemExit(f"unknown kind {kind!r} for {name}")
        if version != args.version:
            raise SystemExit(f"{name} is {version}, the release is {args.version}")
        if args.published and not oci:
            raise SystemExit(f"{name} on {target} has no OCI digest")
        entry = {"digest": digest}
        if oci:
            entry["source"] = f"oci://{args.repository}@{oci}"
        pins[kind].setdefault(name, {})[target] = entry

    for kind, members in pins.items():
        if len(members) != want[kind]:
            raise SystemExit(f"the pin table has {len(members)} {kind}(s), "
                             f"the tree declares {want[kind]}: {sorted(members)}")
        for name, targets in members.items():
            if len(targets) != args.targets:
                raise SystemExit(f"{name} has {len(targets)} target(s), "
                                 f"expected {args.targets}: {sorted(targets)}")

    out = pathlib.Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    document = {"version": args.version, "published": args.published,
                "repository": args.repository}
    document.update({SECTIONS[kind]: pins[kind] for kind in SECTIONS})
    (out / "external-library-pins.json").write_text(
        json.dumps(document, indent=2, sort_keys=True) + "\n", encoding="utf-8", newline="\n")

    major, minor = args.version.split(".")[:2]
    lines = []
    if not args.published:
        lines.append("> Dry run: nothing was pushed, so no `source` is named. "
                     "The digests are this build's.\n")
    for kind, section in SECTIONS.items():
        for name in sorted(pins[kind]):
            lines += [f"<details><summary><code>{name}</code> ({kind})</summary>\n",
                      "```yaml", "requires:", f"  {section}:", f"    - id: {name}",
                      f'      version: ">={major}.{minor},<{major}.{int(minor) + 1}"',
                      "      artifact:", "        targets:"]
            for target in sorted(pins[kind][name]):
                entry = pins[kind][name][target]
                lines += [f"          {target}:", f"            digest: {entry['digest']}"]
                if "source" in entry:
                    lines.append(f"            source: {entry['source']}")
            lines += ["```", "\n</details>\n"]
    (out / "external-library-pins.md").write_text(
        "\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    print(", ".join(f"{len(pins[k])} {k}(s)" for k in SECTIONS) + " in the pin table")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
