#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Generate the small, license-free VRMA fixture used by the importer tests."""

from __future__ import annotations

import argparse
import json
import pathlib
import struct
import sys


FIXTURE = pathlib.Path(__file__).parents[1] / "tests" / "fixtures" / "canonical_walk.vrma"


def _align4(data: bytes) -> bytes:
    return data + b"\0" * ((-len(data)) % 4)


def _fixture_bytes() -> bytes:
    chunks: list[bytes] = []

    def append_floats(values: list[float]) -> tuple[int, int]:
        offset = sum(len(chunk) for chunk in chunks)
        chunk = struct.pack("<" + "f" * len(values), *values)
        chunks.append(chunk)
        padding = (-len(chunk)) % 4
        if padding:
            chunks.append(b"\0" * padding)
        return offset, len(chunk)

    times_offset, times_length = append_floats([0.0, 1.0])
    hips_rotation_offset, hips_rotation_length = append_floats([
        0.0, 0.0, 0.0, 1.0,
        0.0, 0.70710677, 0.0, 0.70710677,
    ])
    chest_rotation_offset, chest_rotation_length = append_floats([
        0.0, 0.0, 0.0, 1.0,
        0.70710677, 0.0, 0.0, 0.70710677,
    ])
    hips_translation_offset, hips_translation_length = append_floats([
        0.0, 1.0, 0.0,
        0.0, 1.0, 0.5,
    ])
    binary = b"".join(chunks)

    document = {
        "asset": {"version": "2.0", "generator": "usd-vrm-plugins fixture generator"},
        "extensionsUsed": ["VRMC_vrm_animation"],
        "extensions": {
            "VRMC_vrm_animation": {
                "specVersion": "1.0",
                "humanoid": {
                    "humanBones": {
                        "hips": {"node": 0},
                        "spine": {"node": 1},
                        "chest": {"node": 2},
                    }
                },
            }
        },
        "nodes": [
            {"name": "hips", "translation": [0.0, 1.0, 0.0], "children": [1]},
            {"name": "spine", "translation": [0.0, 0.5, 0.0], "children": [2]},
            {"name": "chest", "translation": [0.0, 0.5, 0.0]},
        ],
        "animations": [{
            "name": "canonical_walk",
            "samplers": [
                {"input": 0, "output": 1, "interpolation": "LINEAR"},
                {"input": 0, "output": 2, "interpolation": "LINEAR"},
                {"input": 0, "output": 3, "interpolation": "LINEAR"},
            ],
            "channels": [
                {"sampler": 0, "target": {"node": 0, "path": "rotation"}},
                {"sampler": 1, "target": {"node": 2, "path": "rotation"}},
                {"sampler": 2, "target": {"node": 0, "path": "translation"}},
            ],
        }],
        "buffers": [{"byteLength": len(binary)}],
        "bufferViews": [
            {"buffer": 0, "byteOffset": times_offset, "byteLength": times_length},
            {"buffer": 0, "byteOffset": hips_rotation_offset, "byteLength": hips_rotation_length},
            {"buffer": 0, "byteOffset": chest_rotation_offset, "byteLength": chest_rotation_length},
            {"buffer": 0, "byteOffset": hips_translation_offset, "byteLength": hips_translation_length},
        ],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": 2, "type": "SCALAR", "max": [1.0], "min": [0.0]},
            {"bufferView": 1, "componentType": 5126, "count": 2, "type": "VEC4"},
            {"bufferView": 2, "componentType": 5126, "count": 2, "type": "VEC4"},
            {"bufferView": 3, "componentType": 5126, "count": 2, "type": "VEC3"},
        ],
    }
    json_bytes = _align4(json.dumps(document, separators=(",", ":"), ensure_ascii=True).encode("utf-8"))
    glb_length = 12 + 8 + len(json_bytes) + 8 + len(binary)
    return (struct.pack("<4sII", b"glTF", 2, glb_length) +
            struct.pack("<I4s", len(json_bytes), b"JSON") + json_bytes +
            struct.pack("<I4s", len(binary), b"BIN\0") + binary)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="fail if fixture is not current")
    args = parser.parse_args()
    expected = _fixture_bytes()
    if args.check:
        if not FIXTURE.exists() or FIXTURE.read_bytes() != expected:
            print(f"fixture is stale: run {pathlib.Path(__file__).name}", file=sys.stderr)
            return 1
        print(f"fixture is current: {FIXTURE}")
        return 0
    FIXTURE.parent.mkdir(parents=True, exist_ok=True)
    FIXTURE.write_bytes(expected)
    print(f"wrote {FIXTURE} ({len(expected)} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
