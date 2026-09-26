#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Write the pin table for the release's ``lookdev`` packages.

The release lane's ``publish`` job pushes each ``lookdev`` package (today
``vrmImaging``, for a Formation on the canonical CY2026 ``lookdev`` runtime) to
one GHCR repository. A consumer needs two digests per package and target: the
archive digest, which a Formation names as a component's ``artifact``, and the
OCI manifest digest to pull it from, which changes on every republish. This
script writes both, from the rows the job actually pushed, as
``lookdev-package-pins.json`` and a Markdown section for the release notes.

Rows are tab-separated: ``name version target archive_digest [oci_digest]``.
A dry run passes rows without an OCI digest and no ``--published``. The table
then names no source and says so, instead of inventing a locator that does
not exist.

Usage (what .github/workflows/release.yml runs):

    python scripts/make_lookdev_pins.py --rows .ost-ci/pushed.tsv \
        --version 0.9.0 --repository ghcr.io/animu-sphere/usd-vrm-plugins \
        --out pins --published
"""
from __future__ import annotations

import argparse
import json
import pathlib
import sys


def read_rows(path: pathlib.Path, repository: str, published: bool) -> dict:
    packages: dict[str, dict[str, dict]] = {}
    for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if not line.strip():
            continue
        fields = line.split("\t")
        if len(fields) not in (4, 5):
            raise SystemExit(f"{path}:{number}: expected 4 or 5 fields, got {len(fields)}")
        name, version, target, artifact = fields[:4]
        oci = fields[4] if len(fields) == 5 else ""
        if not target.endswith("-lookdev"):
            raise SystemExit(f"{path}:{number}: {name} target {target} is not a lookdev target")
        if published and not oci:
            raise SystemExit(f"{path}:{number}: {name} {target} was published but has no OCI digest")
        entry = {"version": version, "artifact": artifact}
        if oci:
            entry["source"] = f"oci://{repository}@{oci}"
            entry["tag"] = f"oci://{repository}:{name}-{version}-{target}"
        if target in packages.setdefault(name, {}):
            raise SystemExit(f"{path}:{number}: {name} {target} appears twice")
        packages[name][target] = entry
    if not packages:
        raise SystemExit(f"{path}: no package rows")
    return packages


def render_markdown(packages: dict, repository: str, published: bool) -> str:
    md = ["## `lookdev` packages", ""]
    if not published:
        md += ["> Dry run: nothing was pushed, so no source is named. "
               "The digests are this build's.", ""]
    md += ["For a Formation on the canonical CY2026 `lookdev` runtime. Pull "
           "by the OCI digest, then name the archive digest as the "
           "component's `artifact`.", "",
           "| Package | Target | Archive digest (`artifact`) | Pull from |",
           "| --- | --- | --- | --- |"]
    for name in sorted(packages):
        for target, entry in sorted(packages[name].items()):
            source = f"`{entry['source']}`" if "source" in entry else "not published"
            md.append(f"| `{name}` {entry['version']} | `{target}` "
                      f"| `{entry['artifact']}` | {source} |")
    if published:
        md += ["", "```sh"]
        for name in sorted(packages):
            for entry in (packages[name][t] for t in sorted(packages[name])):
                md.append(f"ost artifact pull {entry['source']} "
                          f"--expect-artifact {entry['artifact']}")
        md.append("```")
    return "\n".join(md) + "\n"


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--rows", type=pathlib.Path, required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--repository", required=True)
    parser.add_argument("--out", type=pathlib.Path, required=True)
    parser.add_argument("--published", action="store_true")
    args = parser.parse_args(argv)

    packages = read_rows(args.rows, args.repository, args.published)
    args.out.mkdir(parents=True, exist_ok=True)
    document = {"version": args.version, "published": args.published,
                "repository": args.repository, "packages": packages}
    (args.out / "lookdev-package-pins.json").write_text(
        json.dumps(document, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    (args.out / "lookdev-package-pins.md").write_text(
        render_markdown(packages, args.repository, args.published), encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
