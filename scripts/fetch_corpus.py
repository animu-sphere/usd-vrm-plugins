#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Fetch opt-in / non-vendored corpus models declared in tests/corpus/manifest.json.

Covers every `storage == "fetch"` entry (under `models[]` or `candidates[]`) -
the VRoid samples (Vita, Victoria_Rubin, Sendagaya_Shino, AvatarSample_A/B) and
Alicia. It is deliberately conservative, per the corpus policy:

  - **License-gated.** An entry whose `redistributionAllowed` is not `true`
    (i.e. `false` or `null`) is NOT downloaded unless you pass
    `--accept-license <id>` (or `--accept-license all`), acknowledging you have
    checked that model's terms yourself.
  - **Pinned.** An entry is auto-fetched only if it declares BOTH a direct
    `downloadUrl` and a `sha256`. After download the SHA-256 is verified; a
    mismatch deletes the file and fails. Entries missing either pin print manual
    acquisition instructions and are skipped (not a failure).
  - **Allowlisted.** Only the manifest's own `downloadUrl` is used - no
    redirects to other hosts.
  - **Idempotent.** If the target already exists and its SHA-256 matches, it is
    left untouched.

Nothing here is committed to the repo; downloaded files land under
tests/corpus/<targetPath> (git-ignored - see tests/corpus/.gitignore).

Usage:
  python scripts/fetch_corpus.py                 # fetch pinned, license-clear entries
  python scripts/fetch_corpus.py --list          # list fetchable entries + status
  python scripts/fetch_corpus.py --accept-license alicia-solid-vrm0
  python scripts/fetch_corpus.py --accept-license all
"""
from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import sys
import urllib.request

REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
CORPUS = REPO_ROOT / "plugins" / "usdVrm" / "tests" / "corpus"
MANIFEST = CORPUS / "manifest.json"


def sha256_of(path: pathlib.Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def fetchable_entries(manifest: dict) -> list[dict]:
    out = []
    for m in manifest.get("models", []):
        if m.get("storage") == "fetch":
            out.append(m)
    out.extend(manifest.get("candidates", []))
    return out


def target_path(entry: dict) -> pathlib.Path | None:
    rel = entry.get("targetPath") or entry.get("file")
    return CORPUS / rel if rel else None


def status_of(entry: dict, accepted: set[str]) -> tuple[str, str]:
    """Return (state, detail) for reporting."""
    tgt = target_path(entry)
    sha = (entry.get("sha256") or "").lower()
    if tgt and tgt.exists() and sha and sha256_of(tgt) == sha:
        return ("present", str(tgt.relative_to(CORPUS)))
    gated = entry.get("redistributionAllowed") is not True
    if gated and entry["id"] not in accepted and "all" not in accepted:
        return ("license-gated", "needs --accept-license")
    if not entry.get("downloadUrl") or not sha:
        return ("manual", "no pinned downloadUrl + sha256 yet")
    return ("ready", entry["downloadUrl"])


def download(url: str, dest: pathlib.Path, expected_sha: str) -> None:
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
    print(f"    OK {dest.relative_to(CORPUS)} ({actual[:16]}...)")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--list", action="store_true",
                        help="list fetchable entries and their status, fetch nothing")
    parser.add_argument("--accept-license", action="append", default=[],
                        metavar="ID",
                        help="acknowledge you have verified this model's license "
                             "(repeatable; 'all' accepts every gated entry)")
    args = parser.parse_args()
    accepted = set(args.accept_license)

    with open(MANIFEST, encoding="utf-8") as fh:
        manifest = json.load(fh)
    entries = fetchable_entries(manifest)

    fetched = failed = 0
    for entry in entries:
        state, detail = status_of(entry, accepted)
        print(f"- {entry['id']} [{entry.get('vrmVersion', '?')}]: {state} - {detail}")
        if args.list or state != "ready":
            if state == "manual":
                print(f"    source: {entry.get('sourceUrl', '?')}  "
                      f"-> place at tests/corpus/{entry.get('targetPath', '?')}")
            continue
        try:
            download(entry["downloadUrl"], target_path(entry), entry["sha256"])
            fetched += 1
        except SystemExit as exc:
            print(exc)
            failed += 1

    print(f"\ncorpus fetch: {fetched} fetched, {failed} failed, "
          f"{len(entries)} declared")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
