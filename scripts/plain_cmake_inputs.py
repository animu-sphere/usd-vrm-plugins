#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""The inputs of the plain-CMake lane, obtained without `ost`.

`.github/workflows/plain-cmake.yml` builds this repository the way someone
without OpenStrata would: an OpenUSD 26.08 install and a `cmake --install` of
usd-motion-plugins on CMAKE_PREFIX_PATH, then `cmake`, `cmake --build`, `ctest`
(docs/reference/SUPPORTED_CONFIGURATIONS.md, "Plain CMake"). What it must not do
is invent its own versions of those inputs -- a hand-authored workflow that
copies pins is how release.yml once built against a runtime every other lane
had moved off (ci_pins.py). So each input is read from the file that already
pins it, and fetched by that digest over plain HTTPS:

    openusd --cell <name> --out DIR
        The OpenUSD install of one `openstrata.ci.yaml` cell: its
        `runtime_artifact` archive, found in its `runtime_remote` manifest,
        checked against that digest and extracted. The archive is an ordinary
        OpenUSD install prefix (pxrConfig.cmake at its root). Prints
        `target=`, `apt=` (the cell's host packages) and `python=` (the
        interpreter the runtime's bindings are built for) for the workflow.

    motion-source --target <target>
        Which usd-motion-plugins commit to build. Every `requires.libraries`
        artifact pin for <target> is read from the descriptors, each artifact's
        SLSA provenance is fetched, its subject is checked against the pinned
        digest, and the source revisions must all be one commit. Prints
        `repository=` and `revision=`. So the lane builds from source exactly
        what the pinned packages were built from, and moves when they move.

    bundle --id <id> --target <target> --out DIR
        A consumed bundle (`requires.bundles` with an `artifact:` block --
        `execMotion`), fetched by its pinned digest and extracted, for
        USDVRM_EXEC_MOTION_ROOT.

Output lines are `key=value`, for "$GITHUB_OUTPUT". Registry access is
anonymous: every artifact here is public on GHCR.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import subprocess
import sys
import urllib.request
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
CI_MANIFEST = REPO_ROOT / "openstrata.ci.yaml"
DESCRIPTOR_GLOBS = ("libs/*/openstrata.library.yaml",
                    "plugins/*/openstrata.plugin.yaml",
                    "tools/*/openstrata.tool.yaml")
ARCHIVE_MEDIA_TYPE = "application/vnd.openstrata.artifact.archive.v1+tar+zstd"


def fail(msg: str) -> "NoReturn":
    print(f"error: {msg}", file=sys.stderr)
    raise SystemExit(1)


# ---------------------------------------------------------------------------
# Pins, read without a YAML parser -- like ci_pins.py and check_docs.py, so the
# lane needs nothing installed before it can read what to install.
# ---------------------------------------------------------------------------

def cell(name: str) -> dict:
    text = CI_MANIFEST.read_text(encoding="utf-8")
    match = re.search(rf"^  - name: {re.escape(name)}\n((?:    .*\n|\s*\n|    #.*\n)*)",
                      text, re.M)
    if not match:
        fail(f"{CI_MANIFEST.name} has no cell named {name!r}")
    body = match.group(1)

    def field(pattern: str) -> str:
        found = re.search(pattern, body, re.M)
        if not found:
            fail(f"cell {name!r} has no {pattern!r}")
        return found.group(1)

    apt = re.search(r"^\s+apt:\s*\[([^\]]*)\]", body, re.M)
    python = re.search(r"^\s+host_python:\s*\"([^\"]+)\"", body, re.M)
    return {
        "python": python.group(1) if python else "",
        "runtime_artifact": field(r"^\s+runtime_artifact:\s*(sha256:[0-9a-f]{64})"),
        "uri": field(r"^\s+uri:\s*(oci://\S+)"),
        "apt": [p.strip() for p in apt.group(1).split(",")] if apt else [],
    }


def artifact_pins(section: str, target: str) -> dict[str, dict]:
    """`requires.<section>[]` entries carrying an artifact pin for <target>."""
    pins: dict[str, dict] = {}
    for pattern in DESCRIPTOR_GLOBS:
        for path in sorted(REPO_ROOT.glob(pattern)):
            current_section = current_id = current_target = None
            for raw in path.read_text(encoding="utf-8").splitlines():
                line = raw.split("#", 1)[0].rstrip()
                if not line.strip():
                    continue
                indent = len(line) - len(line.lstrip())
                key = line.strip()
                if indent <= 2 and key.endswith(":") and not key.startswith("-"):
                    current_section = key[:-1]
                    current_id = current_target = None
                elif key.startswith("- id:"):
                    current_id = key.split(":", 1)[1].strip()
                    current_target = None
                elif key.endswith(":"):
                    # `artifact:`, `targets:`, or a target key -- only this
                    # one's `digest:`/`source:` lines are ours.
                    current_target = target if key[:-1] == target else None
                elif current_target and current_section == section and current_id:
                    field, _, value = key.partition(":")
                    if field in ("digest", "source"):
                        entry = pins.setdefault(current_id, {})
                        value = value.strip()
                        if entry.get(field) not in (None, value):
                            fail(f"{current_id} is pinned to two {field}s for "
                                 f"{target}: {entry[field]} and {value} ({path})")
                        entry[field] = value
    return pins


# ---------------------------------------------------------------------------
# Anonymous OCI pull
# ---------------------------------------------------------------------------

class Registry:
    def __init__(self, uri: str) -> None:
        match = re.fullmatch(r"oci://([^/]+)/([^@]+)@(sha256:[0-9a-f]{64})", uri)
        if not match:
            fail(f"not a digest-pinned OCI reference: {uri}")
        self.host, self.repository, self.manifest_digest = match.groups()
        token_url = (f"https://{self.host}/token?"
                     f"scope=repository:{self.repository}:pull")
        with urllib.request.urlopen(token_url, timeout=60) as response:
            self.token = json.load(response)["token"]

    def _get(self, path: str, accept: str | None = None):
        request = urllib.request.Request(
            f"https://{self.host}/v2/{self.repository}/{path}",
            headers={"Authorization": f"Bearer {self.token}",
                     **({"Accept": accept} if accept else {})})
        return urllib.request.urlopen(request, timeout=600)

    def manifest(self) -> dict:
        with self._get(f"manifests/{self.manifest_digest}",
                       "application/vnd.oci.image.manifest.v1+json") as response:
            body = response.read()
        actual = "sha256:" + hashlib.sha256(body).hexdigest()
        if actual != self.manifest_digest:
            fail(f"manifest {self.manifest_digest} hashes to {actual}")
        return json.loads(body)

    def blob_bytes(self, digest: str) -> bytes:
        with self._get(f"blobs/{digest}") as response:
            body = response.read()
        verify(digest, hashlib.sha256(body).hexdigest())
        return body

    def blob_to(self, digest: str, dest: Path) -> None:
        sha = hashlib.sha256()
        with self._get(f"blobs/{digest}") as response, dest.open("wb") as out:
            while chunk := response.read(1 << 20):
                sha.update(chunk)
                out.write(chunk)
        verify(digest, sha.hexdigest())


def verify(expected: str, actual_hex: str) -> None:
    if expected != f"sha256:{actual_hex}":
        fail(f"blob {expected} hashes to sha256:{actual_hex}")


def layer(manifest: dict, digest: str, uri: str) -> dict:
    for entry in manifest.get("layers", []):
        if entry.get("digest") == digest:
            return entry
    fail(f"{uri} carries no layer {digest}")


def layer_titled(manifest: dict, suffix: str, uri: str) -> dict:
    for entry in manifest.get("layers", []):
        title = entry.get("annotations", {}).get("org.opencontainers.image.title", "")
        if title.endswith(suffix):
            return entry
    fail(f"{uri} carries no *{suffix} layer")


def fetch_archive(uri: str, digest: str, out: Path) -> str:
    """Download the archive layer `digest` of `uri`, verify, extract into out."""
    registry = Registry(uri)
    entry = layer(registry.manifest(), digest, uri)
    if entry.get("mediaType") != ARCHIVE_MEDIA_TYPE:
        fail(f"{uri} layer {digest} is {entry.get('mediaType')}, not an archive")
    title = entry.get("annotations", {}).get("org.opencontainers.image.title", "")
    out.mkdir(parents=True, exist_ok=True)
    archive = out.parent / f".{out.name}.tar.zst"
    registry.blob_to(digest, archive)
    tar = shutil.which("tar")
    if not tar:
        fail("no tar on PATH")
    subprocess.run([tar, "--zstd", "-xf", str(archive), "-C", str(out)],
                   check=True)
    archive.unlink()
    return title


# ---------------------------------------------------------------------------
# Subcommands
# ---------------------------------------------------------------------------

def cmd_openusd(args) -> int:
    found = cell(args.cell)
    title = fetch_archive(found["uri"], found["runtime_artifact"], args.out)
    if not (args.out / "pxrConfig.cmake").exists():
        fail(f"{title} extracted into {args.out} with no pxrConfig.cmake at its root")
    target = re.fullmatch(r"openstrata-(.+)\.tar\.zst", title)
    if not target:
        fail(f"cannot read a target out of the archive name {title!r}")
    print(f"target={target.group(1)}")
    print(f"apt={' '.join(found['apt'])}")
    print(f"python={found['python']}")
    return 0


def cmd_motion_source(args) -> int:
    pins = artifact_pins("libraries", args.target)
    if not pins:
        fail(f"no requires.libraries artifact pin names {args.target}")
    sources: dict[tuple[str, str], list[str]] = {}
    for package, pin in sorted(pins.items()):
        if "digest" not in pin or "source" not in pin:
            fail(f"{package}'s {args.target} pin lacks a digest or a source")
        registry = Registry(pin["source"])
        manifest = registry.manifest()
        layer(manifest, pin["digest"], pin["source"])
        statement = json.loads(registry.blob_bytes(
            layer_titled(manifest, "provenance.intoto.jsonl", pin["source"])["digest"]))
        subjects = {"sha256:" + s["digest"]["sha256"] for s in statement["subject"]}
        if pin["digest"] not in subjects:
            fail(f"{package}'s provenance does not name the pinned {pin['digest']}")
        source = (statement["predicate"]["buildDefinition"]
                  ["externalParameters"]["source"])
        sources.setdefault((source["repository"], source["revision"]), []).append(package)
    if len(sources) != 1:
        described = "; ".join(f"{', '.join(p)} from {r}@{c}"
                              for (r, c), p in sorted(sources.items()))
        fail(f"the pinned packages were not built from one commit: {described}")
    (repository, revision), packages = next(iter(sources.items()))
    print(f"repository={repository}")
    print(f"revision={revision}")
    print(f"packages={' '.join(packages)}")
    return 0


def cmd_bundle(args) -> int:
    pins = artifact_pins("bundles", args.target)
    pin = pins.get(args.id)
    if not pin or "digest" not in pin or "source" not in pin:
        fail(f"no requires.bundles artifact pin for {args.id} on {args.target}")
    fetch_archive(pin["source"], pin["digest"], args.out)
    plug_info = args.out / "plugin" / "resources" / args.id / "plugInfo.json"
    if not plug_info.exists():
        fail(f"{args.id}'s archive has no {plug_info.relative_to(args.out)}")
    print(f"root={args.out.resolve().as_posix()}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = parser.add_subparsers(dest="command", required=True)
    p = sub.add_parser("openusd")
    p.add_argument("--cell", required=True)
    p.add_argument("--out", type=Path, required=True)
    p.set_defaults(func=cmd_openusd)
    p = sub.add_parser("motion-source")
    p.add_argument("--target", required=True)
    p.set_defaults(func=cmd_motion_source)
    p = sub.add_parser("bundle")
    p.add_argument("--id", required=True)
    p.add_argument("--target", required=True)
    p.add_argument("--out", type=Path, required=True)
    p.set_defaults(func=cmd_bundle)
    args = parser.parse_args()
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
