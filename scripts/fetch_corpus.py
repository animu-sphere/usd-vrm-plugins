#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Fetch opt-in / non-vendored corpus assets declared in a corpus manifest.

Two corpora declare them, and one fetcher serves both. They hold different
things - avatars for the VRM reader, recordings for the BVH one - and exactly
the same policy, so a second script would be a second license gate to keep in
step with this one:

  vrm     plugins/usdVrmFileFormat/tests/corpus/manifest.json
          every `storage == "fetch"` model plus every `candidates[]` entry:
          the VRoid samples (Vita, Victoria_Rubin, Sendagaya_Shino,
          AvatarSample_A/B) and Alicia
  motion  libs/motionBvh/tests/corpus/recorded/manifest.json
          every `storage == "fetch"` recording: a producer export under a
          licence this repository may not carry, whose manifest row stays
          behind with the measurements taken from it

It is deliberately conservative, per the corpus policy:

  - **License-gated.** An entry whose `redistributionAllowed` is not `true`
    (i.e. `false` or `null`) is NOT downloaded unless you pass
    `--accept-license <id>` (or `--accept-license all`), acknowledging you have
    checked that asset's terms yourself. An entry may state a `licenseId`
    shared with its siblings, so one acknowledgement covers one dataset rather
    than one file.
  - **Pinned.** An entry is auto-fetched only if it declares BOTH a direct
    `downloadUrl` and a `sha256`. After download the SHA-256 is verified; a
    mismatch deletes the file and fails. Entries missing either pin print
    manual acquisition instructions and are skipped (not a failure).
  - **Allowlisted.** Only the manifest's own `downloadUrl` is used - no
    redirects to other hosts.
  - **Idempotent.** If the target already exists and its SHA-256 matches, it is
    left untouched.

Nothing here is committed to the repo; downloaded files land under each
corpus's own git-ignored directory.

What to do afterwards is the same in both corpora and is not this script's job:
re-measure. `libs/motionBvh/tools/check_corpus.py --check --recorded` reads a
fetched recording with the scanner that wrote its manifest row and says so if
the two disagree, and `scripts/check_motion_profiles.py` then holds the profile
against the file rather than against the row.

Usage:
  python scripts/fetch_corpus.py                       # every corpus
  python scripts/fetch_corpus.py --corpus motion       # one of them
  python scripts/fetch_corpus.py --list
  python scripts/fetch_corpus.py --accept-license alicia-solid-vrm0
  python scripts/fetch_corpus.py --corpus motion \
      --accept-license bandai-namco-motiondataset
"""
from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import sys
import urllib.request

REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]


def vrm_entries(manifest: dict) -> list[dict]:
    out = [model for model in manifest.get("models", [])
           if model.get("storage") == "fetch"]
    out.extend(manifest.get("candidates", []))
    return out


def motion_entries(manifest: dict) -> list[dict]:
    return [row for row in manifest.get("fixtures", [])
            if row.get("storage") == "fetch"]


# `root` is what `targetPath` is relative to, and it is the corpus directory
# rather than the manifest's own, because the VRM manifest sits at its corpus
# root and the recorded one sits a level down inside it.
CORPORA = {
    "vrm": {
        "corpus": REPO_ROOT / "plugins" / "usdVrmFileFormat" / "tests" / "corpus",
        "manifest": ("plugins/usdVrmFileFormat/tests/corpus/manifest.json"),
        "entries": vrm_entries,
        "identity": lambda entry: entry["id"],
        "label": lambda entry: entry.get("vrmVersion", "?"),
    },
    "motion": {
        "corpus": REPO_ROOT / "libs" / "motionBvh" / "tests" / "corpus",
        "manifest": "libs/motionBvh/tests/corpus/recorded/manifest.json",
        "entries": motion_entries,
        "identity": lambda entry: entry["file"],
        "label": lambda entry: entry.get("producer", "?"),
    },
}


def sha256_of(path: pathlib.Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def target_path(corpus: dict, entry: dict) -> pathlib.Path | None:
    rel = entry.get("targetPath") or entry.get("file")
    return corpus["corpus"] / rel if rel else None


def accepted_by(entry: dict, accepted: set[str], identity: str) -> bool:
    """Whether the operator has acknowledged this entry's terms.

    A `licenseId` is accepted as well as the entry's own identity, so a dataset
    that arrives as several files takes one acknowledgement rather than one per
    file - the terms are a property of the dataset, and asking per file would
    train whoever runs this to type `all`.
    """
    return bool(accepted & {"all", identity, entry.get("licenseId") or ""})


def status_of(corpus: dict, entry: dict, accepted: set[str]) -> tuple[str, str]:
    """Return (state, detail) for reporting."""
    tgt = target_path(corpus, entry)
    sha = (entry.get("sha256") or "").lower()
    if tgt and tgt.exists() and sha and sha256_of(tgt) == sha:
        return ("present", str(tgt.relative_to(corpus["corpus"])))
    gated = entry.get("redistributionAllowed") is not True
    if gated and not accepted_by(entry, accepted,
                                 corpus["identity"](entry)):
        return ("license-gated", "needs --accept-license")
    if not entry.get("downloadUrl") or not sha:
        return ("manual", "no pinned downloadUrl + sha256 yet")
    return ("ready", entry["downloadUrl"])


def download(corpus: dict, url: str, dest: pathlib.Path,
             expected_sha: str) -> None:
    dest.parent.mkdir(parents=True, exist_ok=True)
    tmp = dest.with_suffix(dest.suffix + ".part")
    print(f"    downloading {url}")
    with urllib.request.urlopen(url) as resp, open(tmp, "wb") as out:  # noqa: S310
        while True:
            chunk = resp.read(1 << 20)
            if not chunk:
                break
            out.write(chunk)
    actual = sha256_of(tmp)
    if actual != expected_sha.lower():
        tmp.unlink(missing_ok=True)
        raise SystemExit(f"    SHA-256 mismatch for {dest.name}: "
                         f"expected {expected_sha}, got {actual}")
    tmp.replace(dest)
    print(f"    OK {dest.relative_to(corpus['corpus'])} ({actual[:16]}...)")


def run(name: str, corpus: dict, accepted: set[str],
        listing: bool) -> tuple[int, int, int]:
    manifest_path = REPO_ROOT / corpus["manifest"]
    if not manifest_path.exists():
        print(f"== {name}: no manifest at {corpus['manifest']}")
        return (0, 0, 0)
    with open(manifest_path, encoding="utf-8") as fh:
        entries = corpus["entries"](json.load(fh))

    print(f"== {name} ({len(entries)} declared)")
    fetched = failed = 0
    for entry in entries:
        identity = corpus["identity"](entry)
        state, detail = status_of(corpus, entry, accepted)
        print(f"- {identity} [{corpus['label'](entry)}]: {state} - {detail}")
        if listing or state != "ready":
            if state == "manual":
                print(f"    source: {entry.get('sourceUrl', '?')}  "
                      f"-> place at {entry.get('targetPath', '?')}")
            if state == "license-gated":
                print(f"    {entry.get('license') or 'licence unstated'}: "
                      f"{entry.get('licenseUrl', 'terms not linked')}")
            continue
        try:
            download(corpus, entry["downloadUrl"],
                     target_path(corpus, entry), entry["sha256"])
            fetched += 1
        except SystemExit as exc:
            print(exc)
            failed += 1
    return (fetched, failed, len(entries))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--corpus", choices=[*CORPORA, "all"], default="all",
                        help="which corpus to fetch for (default: all)")
    parser.add_argument("--list", action="store_true",
                        help="list fetchable entries and their status, fetch nothing")
    parser.add_argument("--accept-license", action="append", default=[],
                        metavar="ID",
                        help="acknowledge you have verified this asset's license "
                             "(an entry id or a licenseId; repeatable; 'all' "
                             "accepts every gated entry)")
    args = parser.parse_args()
    accepted = set(args.accept_license)

    names = list(CORPORA) if args.corpus == "all" else [args.corpus]
    fetched = failed = declared = 0
    for name in names:
        one, two, three = run(name, CORPORA[name], accepted, args.list)
        fetched += one
        failed += two
        declared += three

    print(f"\ncorpus fetch: {fetched} fetched, {failed} failed, "
          f"{declared} declared")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
