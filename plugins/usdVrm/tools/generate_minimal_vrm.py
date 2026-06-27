#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Generate a tiny, license-clean VRM 1.0 (.vrm/GLB) test fixture.

The fixture is authored entirely by this script — it contains no third-party
art — so it is safe to commit. It is intentionally minimal but exercises the
Phase 1 import path end to end:

  * one triangle mesh, skinned to a 2-joint skeleton (hips -> spine)
  * one PBR metallic-roughness material
  * a VRMC_vrm extension with meta + humanoid (hips, spine)

Usage:
  python generate_minimal_vrm.py [out.vrm]   (default: ../tests/fixtures/minimal.vrm)
"""
import json
import os
import struct
import sys

FLOAT, U16, U8 = 5126, 5123, 5121
ARRAY_BUFFER, ELEMENT_ARRAY_BUFFER = 34962, 34963


def main() -> int:
    here = os.path.dirname(os.path.abspath(__file__))
    out_path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        here, "..", "tests", "fixtures", "minimal.vrm")

    # --- geometry: one triangle -------------------------------------------
    positions = [(-0.5, 0.0, 0.0), (0.5, 0.0, 0.0), (0.0, 1.0, 0.0)]
    indices = [0, 1, 2]
    # all three verts fully weighted to joint 0 (hips)
    joints = [(0, 0, 0, 0)] * 3
    weights = [(1.0, 0.0, 0.0, 0.0)] * 3
    # identity inverse-bind matrices for the 2 joints (column-major)
    identity = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]
    ibms = identity + identity

    def pack(fmt, rows):
        return b"".join(struct.pack(fmt, *r) if isinstance(r, (tuple, list))
                        else struct.pack(fmt, r) for r in rows)

    def align(b, n=4, fill=b"\x00"):
        pad = (-len(b)) % n
        return b + fill * pad

    blobs = []
    offset = 0
    views = []

    def add_view(data, target=None):
        nonlocal offset
        data = align(data)
        views.append((offset, len(data), target))
        blobs.append(data)
        idx = len(views) - 1
        offset += len(data)
        return idx

    v_pos = add_view(pack("<3f", positions), ARRAY_BUFFER)
    v_idx = add_view(pack("<3H", [tuple(indices)]), ELEMENT_ARRAY_BUFFER)
    v_jnt = add_view(pack("<4B", joints), ARRAY_BUFFER)
    v_wgt = add_view(pack("<4f", weights), ARRAY_BUFFER)
    v_ibm = add_view(pack("<16f", [tuple(ibms[i:i + 16]) for i in range(0, 32, 16)]))

    bin_blob = b"".join(blobs)

    accessors = [
        {"bufferView": v_pos, "componentType": FLOAT, "count": 3, "type": "VEC3",
         "min": [-0.5, 0.0, 0.0], "max": [0.5, 1.0, 0.0]},
        {"bufferView": v_idx, "componentType": U16, "count": 3, "type": "SCALAR"},
        {"bufferView": v_jnt, "componentType": U8, "count": 3, "type": "VEC4"},
        {"bufferView": v_wgt, "componentType": FLOAT, "count": 3, "type": "VEC4"},
        {"bufferView": v_ibm, "componentType": FLOAT, "count": 2, "type": "MAT4"},
    ]
    buffer_views = [
        {"buffer": 0, "byteOffset": o, "byteLength": l,
         **({"target": t} if t else {})}
        for (o, l, t) in views
    ]

    gltf = {
        "asset": {"version": "2.0", "generator": "usdVrm minimal fixture"},
        "scene": 0,
        "scenes": [{"nodes": [0, 1, 3]}],
        # node 0 = skinned mesh holder; 1 = hips; 2 = spine (child of hips);
        # node 3 = a NON-skinned accessory placed by a node transform (guards
        # that node TRS survives for non-skinned meshes).
        "nodes": [
            {"name": "Body", "mesh": 0, "skin": 0},
            {"name": "hips", "children": [2], "translation": [0.0, 0.5, 0.0]},
            {"name": "spine", "translation": [0.0, 0.3, 0.0]},
            {"name": "Accessory", "mesh": 1, "translation": [1.0, 0.0, 0.0]},
        ],
        "meshes": [
            {
                "name": "Body",
                "primitives": [{
                    "attributes": {"POSITION": 0, "JOINTS_0": 2, "WEIGHTS_0": 3},
                    "indices": 1,
                    "material": 0,
                }],
            },
            {
                # Non-skinned; reuses the triangle accessors, placed by node 3.
                "name": "Accessory",
                "primitives": [{
                    "attributes": {"POSITION": 0},
                    "indices": 1,
                    "material": 0,
                }],
            },
        ],
        "skins": [{"joints": [1, 2], "inverseBindMatrices": 4, "skeleton": 1}],
        "materials": [{
            "name": "Body_Mat",
            "pbrMetallicRoughness": {
                "baseColorFactor": [0.8, 0.6, 0.5, 1.0],
                "metallicFactor": 0.0,
                "roughnessFactor": 0.7,
            },
        }],
        "accessors": accessors,
        "bufferViews": buffer_views,
        "buffers": [{"byteLength": len(bin_blob)}],
        "extensionsUsed": ["VRMC_vrm"],
        "extensions": {
            "VRMC_vrm": {
                "specVersion": "1.0",
                "meta": {
                    "name": "usdVrm Minimal Fixture",
                    "version": "1.0",
                    "authors": ["usd-vrm-plugins"],
                    "licenseUrl": "https://www.apache.org/licenses/LICENSE-2.0",
                    "avatarPermission": "everyone",
                },
                "humanoid": {
                    "humanBones": {
                        "hips": {"node": 1},
                        "spine": {"node": 2},
                    },
                },
            },
        },
    }

    json_blob = align(json.dumps(gltf, separators=(",", ":")).encode("utf-8"),
                      fill=b" ")
    bin_blob = align(bin_blob)

    def chunk(tag, data):
        return struct.pack("<I", len(data)) + tag + data

    body = chunk(b"JSON", json_blob) + chunk(b"BIN\x00", bin_blob)
    glb = struct.pack("<4sII", b"glTF", 2, 12 + len(body)) + body

    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    with open(out_path, "wb") as f:
        f.write(glb)
    print(f"wrote {out_path} ({len(glb)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
