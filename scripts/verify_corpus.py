#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Verify the vendored VRM corpus against tests/corpus/manifest.json.

For every `models[]` entry with `storage == "vendored"`:
  - the file exists at its manifest path,
  - its SHA-256 matches the manifest pin,
  - its `licenseFile` exists,
  - the required provenance fields are present.

`candidates[]` and any `storage != "vendored"` model are reported as
informational (fetch/opt-in — see scripts/fetch_corpus.py), never a failure.

Exit code is non-zero if any vendored asset is missing, mismatched, or
under-documented.

Usage: python scripts/verify_corpus.py
"""
from __future__ import annotations

import hashlib
import json
import pathlib
import sys

REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
CORPUS = REPO_ROOT / "plugins" / "usdVrm" / "tests" / "corpus"
MANIFEST = CORPUS / "manifest.json"

REQUIRED_FIELDS = [
    "id", "file", "format", "vrmVersion", "source", "sourceUrl",
    "downloadedAt", "sha256", "license", "licenseFile",
    "redistributionAllowed", "roles",
]


def sha256_of(path: pathlib.Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def main() -> int:
    with open(MANIFEST, encoding="utf-8") as fh:
        manifest = json.load(fh)

    errors: list[str] = []
    checked = 0
    for model in manifest.get("models", []):
        mid = model.get("id", "<no-id>")
        if model.get("storage") != "vendored":
            print(f"  - {mid}: storage={model.get('storage')!r} (skipped)")
            continue

        missing = [f for f in REQUIRED_FIELDS if f not in model]
        if missing:
            errors.append(f"{mid}: missing required fields {missing}")

        rel = model.get("file")
        if not rel:
            errors.append(f"{mid}: no 'file'")
            continue
        path = CORPUS / rel
        if not path.exists():
            errors.append(f"{mid}: missing file {rel}")
            continue

        actual = sha256_of(path)
        expected = (model.get("sha256") or "").lower()
        if actual != expected:
            errors.append(f"{mid}: sha256 mismatch\n"
                          f"      expected {expected}\n      actual   {actual}")
        else:
            checked += 1
            print(f"  OK {mid}: sha256 {actual[:16]}... ({path.relative_to(CORPUS)})")

        lic = model.get("licenseFile")
        if lic and not (CORPUS / lic).exists():
            errors.append(f"{mid}: licenseFile not found: {lic}")

    fetchable = [m.get("id") for m in manifest.get("candidates", [])]
    if fetchable:
        print(f"\n  candidates (fetch/opt-in, not vendored): {', '.join(fetchable)}")

    if errors:
        print("\nCORPUS VERIFY: FAIL")
        for e in errors:
            print(f"  ✗ {e}")
        return 1
    print(f"\nCORPUS VERIFY: OK ({checked} vendored asset(s) match their pins)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
