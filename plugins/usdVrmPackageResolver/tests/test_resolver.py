#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Bundle-local verification for usdVrmPackageResolver.

Runs with ONLY this bundle (plus OpenUSD) in the session — deliberately no
usdVrmFileFormat, no vrmSchema — proving the resolver is independently
discoverable and serves embedded bytes through the plain Ar surface
(WORKSPACE.md §2, plan §7.1):

  discovery      the ArPackageResolver registration moved here intact
  byte access    Resolve + OpenAsset return the exact embedded image bytes
  path safety    malformed / escaping / unknown packaged paths are rejected
  container      truncated GLBs and out-of-range buffer views are rejected

The expected content-addressed entry name (`images/<fnv1a>.png`) is recomputed
here from the GLB bytes, so the test cannot drift from the committed fixture.

Run standalone:
    ost plugin run plugins/usdVrmPackageResolver -- python tests/test_resolver.py
"""
from __future__ import annotations

import json
import pathlib
import struct
import sys
import tempfile

from pxr import Ar, Plug

FIXTURES = pathlib.Path(__file__).resolve().parent / "fixtures"
PACKAGE = FIXTURES / "textures.vrm"

_FAILED = []


def _fail(msg: str) -> None:
    _FAILED.append(msg)
    print(f"  FAIL: {msg}")


# ---------------------------------------------------------------------------
# Minimal GLB reading/writing (pure Python, mirrors the GLB 2.0 layout)
# ---------------------------------------------------------------------------

def _read_glb_chunks(data: bytes) -> tuple[dict, bytes]:
    magic, version, _length = struct.unpack_from("<4sII", data, 0)
    assert magic == b"glTF" and version == 2, "fixture is not a GLB 2 container"
    offset = 12
    doc = None
    binary = b""
    while offset + 8 <= len(data):
        chunk_len, chunk_type = struct.unpack_from("<II", data, offset)
        payload = data[offset + 8 : offset + 8 + chunk_len]
        if chunk_type == 0x4E4F534A:  # 'JSON'
            doc = json.loads(payload.decode("utf-8"))
        elif chunk_type == 0x004E4942:  # 'BIN\0'
            binary = payload
        offset += 8 + chunk_len
    assert doc is not None, "GLB has no JSON chunk"
    return doc, binary


def _write_glb(doc: dict, binary: bytes) -> bytes:
    payload = json.dumps(doc, separators=(",", ":")).encode("utf-8")
    payload += b" " * (-len(payload) % 4)
    binary = binary + b"\x00" * (-len(binary) % 4)
    total = 12 + 8 + len(payload) + 8 + len(binary)
    return b"".join([
        struct.pack("<4sII", b"glTF", 2, total),
        struct.pack("<II", len(payload), 0x4E4F534A), payload,
        struct.pack("<II", len(binary), 0x004E4942), binary,
    ])


def _fnv1a64(data: bytes) -> int:
    """vrmContainer::HashBytes — FNV-1a-shaped, but with the historical seed
    1469598103934665603 (NOT the standard 14695981039346656037 offset basis).
    The quirk predates the vrmContainer extraction and is deliberately
    preserved: entry names are part of the frozen package-path contract."""
    h = 1469598103934665603
    for b in data:
        h ^= b
        h = (h * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def _embedded_image_entries(package: pathlib.Path) -> list[tuple[str, bytes]]:
    """(entry-name, bytes) for every embedded image, recomputed from the GLB."""
    doc, binary = _read_glb_chunks(package.read_bytes())
    entries = []
    exts = {"image/png": "png", "image/jpeg": "jpg"}
    for image in doc.get("images", []):
        view = doc["bufferViews"][image["bufferView"]]
        start = view.get("byteOffset", 0)
        data = binary[start : start + view["byteLength"]]
        ext = exts[image["mimeType"]]
        entries.append((f"images/{_fnv1a64(data):016x}.{ext}", data))
    return entries


# ---------------------------------------------------------------------------
# Checks
# ---------------------------------------------------------------------------

def check_discovery():
    """The registration moved out of usdVrm intact — and only the resolver."""
    plugin = Plug.Registry().GetPluginWithName("UsdVrmPackageResolver")
    assert plugin, "plugin 'UsdVrmPackageResolver' is not discoverable"
    types = dict(plugin.metadata).get("Types", {})
    assert set(types) == {"UsdVrmPackageResolver"}, sorted(types)
    meta = dict(types["UsdVrmPackageResolver"])
    assert meta.get("bases") == ["ArPackageResolver"], meta
    assert meta.get("extensions") == ["vrm"], meta
    # The bundle must not smuggle a file format in: nothing in this session
    # may claim the .vrm *file format* (Sdf), only the package resolver (Ar).
    from pxr import Sdf
    assert Sdf.FileFormat.FindByExtension("vrm") is None, \
        "a .vrm SdfFileFormat leaked into the resolver-only session"


def check_embedded_image_open():
    """Resolve + OpenAsset return exactly the embedded bytes."""
    entries = _embedded_image_entries(PACKAGE)
    assert entries, "textures.vrm has no embedded images"
    resolver = Ar.GetResolver()
    for name, data in entries:
        path = f"{PACKAGE.as_posix()}[{name}]"
        resolved = resolver.Resolve(path)
        assert resolved, f"did not resolve: {path}"
        asset = resolver.OpenAsset(resolvedPath=resolved)
        assert asset, f"OpenAsset failed: {path}"
        got = bytes(asset.GetBuffer())
        assert got == data, (
            f"served bytes differ for {name}: {len(got)} vs {len(data)}")
        magic = b"\x89PNG" if name.endswith(".png") else b"\xff\xd8\xff"
        assert got.startswith(magic), f"wrong image magic for {name}"


def check_invalid_package_path():
    """Escaping, non-image, and unknown entry paths must not resolve."""
    resolver = Ar.GetResolver()
    bad_entries = [
        "images/../../../etc/passwd",
        "../escape.png",
        "/absolute.png",
        "C:/absolute.png",
        "images\\backslash.png",
        "notimages/0198c0f73fbeb889.png",
        "images/ffffffffffffffff.png",     # well-formed but unknown hash
        "images/0198c0f73fbeb889.png.exe", # suffix abuse
    ]
    for entry in bad_entries:
        path = f"{PACKAGE.as_posix()}[{entry}]"
        resolved = resolver.Resolve(path)
        if resolved:
            _fail(f"malformed packaged path resolved: {entry!r} -> {resolved}")
        # OpenAsset on the unresolved spelling must fail closed too.
        asset = resolver.OpenAsset(resolvedPath=Ar.ResolvedPath(path))
        if asset:
            _fail(f"malformed packaged path opened: {entry!r}")


def check_truncated_glb():
    """A truncated container is rejected (vrmContainer validation), no crash."""
    entries = _embedded_image_entries(PACKAGE)
    whole = PACKAGE.read_bytes()
    resolver = Ar.GetResolver()
    with tempfile.TemporaryDirectory() as tmp:
        for cut in (10, 100, len(whole) - 8):
            trunc = pathlib.Path(tmp) / f"trunc_{cut}.vrm"
            trunc.write_bytes(whole[:cut])
            for name, _ in entries:
                path = f"{trunc.as_posix()}[{name}]"
                if resolver.Resolve(path):
                    _fail(f"truncated GLB ({cut} bytes) resolved {name}")


def check_out_of_range_buffer_view():
    """An image whose buffer view exceeds the BIN chunk must be rejected."""
    doc, binary = _read_glb_chunks(PACKAGE.read_bytes())
    resolver = Ar.GetResolver()
    with tempfile.TemporaryDirectory() as tmp:
        # Point every image's view beyond the real binary payload, keeping the
        # container well-formed at the GLB layer.
        evil = json.loads(json.dumps(doc))
        for image in evil.get("images", []):
            view = evil["bufferViews"][image["bufferView"]]
            view["byteOffset"] = len(binary)
            view["byteLength"] = 0x10000
        for buf in evil.get("buffers", []):
            buf["byteLength"] = len(binary)
        pkg = pathlib.Path(tmp) / "oob.vrm"
        pkg.write_bytes(_write_glb(evil, binary))
        for name, data in _embedded_image_entries(PACKAGE):
            path = f"{pkg.as_posix()}[{name}]"
            if resolver.Resolve(path):
                _fail(f"out-of-range buffer view resolved {name}")


def main() -> int:
    checks = [check_discovery, check_embedded_image_open,
              check_invalid_package_path, check_truncated_glb,
              check_out_of_range_buffer_view]
    for check in checks:
        name = check.__name__
        try:
            check()
        except AssertionError as exc:
            _fail(f"{name}: {exc}")
        except Exception as exc:  # noqa: BLE001 — a crash is a finding here
            _fail(f"{name}: unexpected {type(exc).__name__}: {exc}")
        else:
            print(f"  ok: {name}")
    if _FAILED:
        print(f"FAILED: {len(_FAILED)} resolver check(s)")
        return 1
    print(f"usdVrmPackageResolver: {len(checks)} check group(s) passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
