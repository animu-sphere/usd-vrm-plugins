# SPDX-License-Identifier: Apache-2.0
"""Tiny GLB/VRM builder shared by the fixture generators.

All fixtures authored with this are license-clean (no third-party art) and safe
to commit. Keep them small — they exist to guard specific import behaviors, not
to look like avatars.
"""
import json
import struct
import zlib

FLOAT, U16, U8 = 5126, 5123, 5121
ARRAY_BUFFER, ELEMENT_ARRAY_BUFFER = 34962, 34963

_TYPE_COUNT = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}
_FMT = {FLOAT: "f", U16: "H", U8: "B"}

# A unit triangle reused by most fixtures.
TRI_POSITIONS = [(-0.5, 0.0, 0.0), (0.5, 0.0, 0.0), (0.0, 1.0, 0.0)]
TRI_INDICES = [0, 1, 2]
IDENTITY16 = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]


def translate16(x, y, z):
    """Column-major 4x4 translation matrix (glTF order)."""
    return [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, x, y, z, 1]


class GlbBuilder:
    """Accumulates accessors/bufferViews into one bin chunk, emits a GLB."""

    def __init__(self):
        self._bin = bytearray()
        self._views = []
        self._accessors = []

    def _align(self):
        while len(self._bin) % 4:
            self._bin.append(0)

    def add_bytes(self, data, target=None):
        """Append raw bytes (e.g. an embedded image) and return a bufferView."""
        offset = len(self._bin)
        self._bin += data
        view = {"buffer": 0, "byteOffset": offset, "byteLength": len(data)}
        if target:
            view["target"] = target
        self._align()
        self._views.append(view)
        return len(self._views) - 1

    def add(self, component_type, type_, rows, target=None, minmax=False):
        n = _TYPE_COUNT[type_]
        fmt = "<" + _FMT[component_type] * n
        offset = len(self._bin)
        for r in rows:
            r = r if isinstance(r, (tuple, list)) else (r,)
            self._bin += struct.pack(fmt, *r)
        view = {"buffer": 0, "byteOffset": offset,
                "byteLength": len(self._bin) - offset}
        if target:
            view["target"] = target
        self._align()
        self._views.append(view)
        acc = {"bufferView": len(self._views) - 1, "componentType": component_type,
               "count": len(rows), "type": type_}
        if minmax and type_ == "VEC3":
            cols = list(zip(*rows))
            acc["min"] = [min(c) for c in cols]
            acc["max"] = [max(c) for c in cols]
        self._accessors.append(acc)
        return len(self._accessors) - 1

    def build(self, gltf):
        gltf["accessors"] = self._accessors
        gltf["bufferViews"] = self._views
        gltf["buffers"] = [{"byteLength": len(self._bin)}]
        json_blob = json.dumps(gltf, separators=(",", ":")).encode("utf-8")
        while len(json_blob) % 4:
            json_blob += b" "
        bin_blob = bytes(self._bin)
        while len(bin_blob) % 4:
            bin_blob += b"\x00"

        def chunk(tag, data):
            return struct.pack("<I", len(data)) + tag + data

        body = chunk(b"JSON", json_blob) + chunk(b"BIN\x00", bin_blob)
        return struct.pack("<4sII", b"glTF", 2, 12 + len(body)) + body


def solid_png(r, g, b, a=255):
    """A valid 1x1 RGBA PNG of a solid color (CRCs computed, no deps)."""
    def chunk(typ, data):
        body = typ + data
        return (struct.pack(">I", len(data)) + body +
                struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF))
    sig = b"\x89PNG\r\n\x1a\n"
    ihdr = struct.pack(">IIBBBBB", 1, 1, 8, 6, 0, 0, 0)  # 1x1, 8-bit RGBA
    scanline = b"\x00" + bytes([r, g, b, a])             # filter byte + pixel
    idat = zlib.compress(scanline)
    return sig + chunk(b"IHDR", ihdr) + chunk(b"IDAT", idat) + chunk(b"IEND", b"")


def vrm1_extension(human_bones, meta_name="usdVrm Fixture"):
    """A VRMC_vrm (VRM 1.0) block. human_bones: {bone: node_index}."""
    return {
        "specVersion": "1.0",
        "meta": {
            "name": meta_name, "version": "1.0",
            "authors": ["usd-vrm-plugins"],
            "licenseUrl": "https://www.apache.org/licenses/LICENSE-2.0",
            "avatarPermission": "everyone",
        },
        "humanoid": {
            "humanBones": {b: {"node": n} for b, n in human_bones.items()},
        },
    }


def vrm0_extension(human_bones, title="usdVrm 0.x Fixture"):
    """A VRM (0.x) block. human_bones: {bone: node_index}."""
    return {
        "specVersion": "0.0",
        "meta": {"title": title, "author": "usd-vrm-plugins",
                 "licenseName": "Redistribution_Prohibited"},
        "humanoid": {
            "humanBones": [{"bone": b, "node": n} for b, n in human_bones.items()],
        },
    }
