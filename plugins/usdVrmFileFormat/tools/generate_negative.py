#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Generate the committed, license-clean *negative* VRM fixtures.

These are deliberately malformed avatars, authored entirely here (no third-party
art), used to pin the importer's diagnostic contract: each fixture provokes one
specific ``VRMxxx`` code (or a clean container rejection) that
``tests/test_usdvrm_negative.py`` then asserts against
``tests/corpus/generated/negative-manifest.json``.

Two failure mechanisms are exercised:

* ``read-fatal`` — the container is unreadable, so ``Usd.Stage.Open`` fails and
  the importer surfaces ``[VRM003]`` in the raised error (malformed GLB JSON,
  a dangling glTF index cgltf rejects, a non-finite transform).
* ``import-warning`` — the stage opens but the importer records a coded warning
  on ``/Asset.customData.vrm:warnings`` (out-of-range JOINTS_0, an unmapped or
  duplicate humanoid bone, an out-of-range expression morph index, a dangling
  spring collider-group index, an unparseable VRM extension block).

Usage: python generate_negative.py [out_dir]
       (default: ../tests/corpus/generated/malformed)
"""
import os
import sys

from vrm_fixture_lib import (
    GlbBuilder, ARRAY_BUFFER, ELEMENT_ARRAY_BUFFER, FLOAT, U8, U16,
    TRI_POSITIONS, TRI_INDICES, IDENTITY16, vrm1_extension,
)

W4 = (1.0, 0.0, 0.0, 0.0)
J0 = (0, 0, 0, 0)


def _skin_attrs(b):
    pos = b.add(FLOAT, "VEC3", TRI_POSITIONS, ARRAY_BUFFER, minmax=True)
    return {"POSITION": pos,
            "JOINTS_0": b.add(U8, "VEC4", [J0, J0, J0]),
            "WEIGHTS_0": b.add(FLOAT, "VEC4", [W4, W4, W4])}


def _idx(b):
    return b.add(U16, "SCALAR", TRI_INDICES, ELEMENT_ARRAY_BUFFER)


def _mat(name="Body_Mat"):
    return {"name": name, "pbrMetallicRoughness": {
        "baseColorFactor": [0.8, 0.6, 0.5, 1.0]}}


def _body_nodes():
    return [
        {"name": "Body", "mesh": 0, "skin": 0},
        {"name": "hips", "children": [2], "translation": [0.0, 0.5, 0.0]},
        {"name": "spine", "translation": [0.0, 0.3, 0.0]},
    ]


# --- read-fatal fixtures (cgltf / container rejection -> VRM003) -------------

def build_malformed_glb_json():
    """A well-formed minimal VRM whose GLB JSON chunk is then corrupted.

    The 12-byte GLB header is followed by an 8-byte JSON chunk header, so the
    JSON payload starts at byte 20. Flipping its opening brace makes cgltf
    reject the container.
    """
    b = GlbBuilder()
    attrs = _skin_attrs(b)
    idx = _idx(b)
    ibm = b.add(FLOAT, "MAT4", [tuple(IDENTITY16), tuple(IDENTITY16)])
    gltf = {
        "asset": {"version": "2.0", "generator": "usdVrm negative fixtures"},
        "scene": 0, "scenes": [{"nodes": [0, 1]}], "nodes": _body_nodes(),
        "meshes": [{"name": "Body", "primitives": [
            {"attributes": attrs, "indices": idx, "material": 0}]}],
        "skins": [{"joints": [1, 2], "inverseBindMatrices": ibm, "skeleton": 1}],
        "materials": [_mat()], "extensionsUsed": ["VRMC_vrm"],
        "extensions": {"VRMC_vrm": vrm1_extension({"hips": 1, "spine": 2})},
    }
    data = bytearray(b.build(gltf))
    assert data[20] == ord("{"), "unexpected GLB JSON chunk layout"
    data[20] = ord("X")
    return bytes(data)


def build_dangling_texture_index():
    """A material baseColorTexture.index that points past the textures array.

    There are no textures at all, so cgltf's index validation rejects the file.
    """
    b = GlbBuilder()
    pos = b.add(FLOAT, "VEC3", TRI_POSITIONS, ARRAY_BUFFER, minmax=True)
    uv = b.add(FLOAT, "VEC2", [(0.0, 0.0), (1.0, 0.0), (0.5, 1.0)], ARRAY_BUFFER)
    idx = _idx(b)
    gltf = {
        "asset": {"version": "2.0", "generator": "usdVrm negative fixtures"},
        "scene": 0, "scenes": [{"nodes": [0]}],
        "nodes": [{"name": "Body", "mesh": 0}],
        "meshes": [{"name": "Body", "primitives": [
            {"attributes": {"POSITION": pos, "TEXCOORD_0": uv},
             "indices": idx, "material": 0}]}],
        "materials": [{"name": "Skin", "pbrMetallicRoughness": {
            "baseColorTexture": {"index": 9}}}],
        "extensionsUsed": ["VRMC_vrm"],
        "extensions": {"VRMC_vrm": vrm1_extension({})},
    }
    return b.build(gltf)


def build_nonfinite_transform():
    """A node translation with NaN / Infinity components.

    The GLB JSON serializes these as the non-standard ``NaN`` / ``Infinity``
    tokens, which cgltf's JSON parser rejects — the container never opens.
    """
    b = GlbBuilder()
    attrs = _skin_attrs(b)
    idx = _idx(b)
    ibm = b.add(FLOAT, "MAT4", [tuple(IDENTITY16), tuple(IDENTITY16)])
    nodes = _body_nodes()
    nodes[1]["translation"] = [float("nan"), 0.5, float("inf")]
    gltf = {
        "asset": {"version": "2.0", "generator": "usdVrm negative fixtures"},
        "scene": 0, "scenes": [{"nodes": [0, 1]}], "nodes": nodes,
        "meshes": [{"name": "Body", "primitives": [
            {"attributes": attrs, "indices": idx, "material": 0}]}],
        "skins": [{"joints": [1, 2], "inverseBindMatrices": ibm, "skeleton": 1}],
        "materials": [_mat()], "extensionsUsed": ["VRMC_vrm"],
        "extensions": {"VRMC_vrm": vrm1_extension({"hips": 1, "spine": 2})},
    }
    return b.build(gltf)


# --- import-warning fixtures (stage opens, coded warning) --------------------

def build_nonobject_vrm_extension():
    """A VRMC_vrm whose value is a JSON array, not an object.

    cgltf requires an extension value to be an object; a non-object VRM extension
    makes it reject the whole container, so the stage fails to open (VRM003).
    (This is why the ERROR-level VRM002 "extension JSON unparseable" is not
    reachable through a cgltf-validated file — cgltf gates it first.)
    """
    b = GlbBuilder()
    pos = b.add(FLOAT, "VEC3", TRI_POSITIONS, ARRAY_BUFFER, minmax=True)
    idx = _idx(b)
    gltf = {
        "asset": {"version": "2.0", "generator": "usdVrm negative fixtures"},
        "scene": 0, "scenes": [{"nodes": [0]}],
        "nodes": [{"name": "Body", "mesh": 0}],
        "meshes": [{"name": "Body", "primitives": [
            {"attributes": {"POSITION": pos}, "indices": idx, "material": 0}]}],
        "materials": [_mat()], "extensionsUsed": ["VRMC_vrm"],
        "extensions": {"VRMC_vrm": []},
    }
    return b.build(gltf)


def build_out_of_range_joints():
    """JOINTS_0 references local joint 5, but the skin has only 2 joints."""
    b = GlbBuilder()
    pos = b.add(FLOAT, "VEC3", TRI_POSITIONS, ARRAY_BUFFER, minmax=True)
    badj = b.add(U8, "VEC4", [(5, 0, 0, 0), (5, 0, 0, 0), (5, 0, 0, 0)])
    w4 = b.add(FLOAT, "VEC4", [W4, W4, W4])
    idx = _idx(b)
    ibm = b.add(FLOAT, "MAT4", [tuple(IDENTITY16), tuple(IDENTITY16)])
    gltf = {
        "asset": {"version": "2.0", "generator": "usdVrm negative fixtures"},
        "scene": 0, "scenes": [{"nodes": [0, 1]}], "nodes": _body_nodes(),
        "meshes": [{"name": "Body", "primitives": [
            {"attributes": {"POSITION": pos, "JOINTS_0": badj, "WEIGHTS_0": w4},
             "indices": idx, "material": 0}]}],
        "skins": [{"joints": [1, 2], "inverseBindMatrices": ibm, "skeleton": 1}],
        "materials": [_mat()], "extensionsUsed": ["VRMC_vrm"],
        "extensions": {"VRMC_vrm": vrm1_extension({"hips": 1, "spine": 2})},
    }
    return b.build(gltf)


def build_unmapped_humanoid_node():
    """Humanoid bone 'spine' references node 999, which does not exist."""
    b = GlbBuilder()
    attrs = _skin_attrs(b)
    idx = _idx(b)
    ibm = b.add(FLOAT, "MAT4", [tuple(IDENTITY16), tuple(IDENTITY16)])
    gltf = {
        "asset": {"version": "2.0", "generator": "usdVrm negative fixtures"},
        "scene": 0, "scenes": [{"nodes": [0, 1]}], "nodes": _body_nodes(),
        "meshes": [{"name": "Body", "primitives": [
            {"attributes": attrs, "indices": idx, "material": 0}]}],
        "skins": [{"joints": [1, 2], "inverseBindMatrices": ibm, "skeleton": 1}],
        "materials": [_mat()], "extensionsUsed": ["VRMC_vrm"],
        "extensions": {"VRMC_vrm": vrm1_extension({"hips": 1, "spine": 999})},
    }
    return b.build(gltf)


def build_duplicate_humanoid_bone():
    """A VRM 0.x humanBones array that maps 'hips' twice."""
    b = GlbBuilder()
    attrs = _skin_attrs(b)
    idx = _idx(b)
    ibm = b.add(FLOAT, "MAT4", [tuple(IDENTITY16), tuple(IDENTITY16)])
    vrm0 = {
        "specVersion": "0.0",
        "meta": {"title": "usdVrm dup-bone negative", "author": "usd-vrm-plugins",
                 "licenseName": "Redistribution_Prohibited"},
        "humanoid": {"humanBones": [
            {"bone": "hips", "node": 1},
            {"bone": "hips", "node": 2},   # duplicate
            {"bone": "spine", "node": 2}]},
    }
    gltf = {
        "asset": {"version": "2.0", "generator": "usdVrm negative fixtures"},
        "scene": 0, "scenes": [{"nodes": [0, 1]}], "nodes": _body_nodes(),
        "meshes": [{"name": "Body", "primitives": [
            {"attributes": attrs, "indices": idx, "material": 0}]}],
        "skins": [{"joints": [1, 2], "inverseBindMatrices": ibm, "skeleton": 1}],
        "materials": [_mat()], "extensionsUsed": ["VRM"],
        "extensions": {"VRM": vrm0},
    }
    return b.build(gltf)


def build_oob_expression_morph():
    """An expression morphTargetBind whose index exceeds the mesh's targets."""
    b = GlbBuilder()
    attrs = _skin_attrs(b)
    idx = _idx(b)
    ibm = b.add(FLOAT, "MAT4", [tuple(IDENTITY16), tuple(IDENTITY16)])
    morph = b.add(FLOAT, "VEC3", [(0, 0, 0), (0, 0, 0), (0, 0.5, 0)])
    ext = vrm1_extension({"hips": 1, "spine": 2})
    ext["expressions"] = {"preset": {"happy": {
        "isBinary": False,
        # The Face mesh has exactly one morph target (index 0); 7 is out of range.
        "morphTargetBinds": [{"node": 0, "index": 7, "weight": 1.0}]}}}
    gltf = {
        "asset": {"version": "2.0", "generator": "usdVrm negative fixtures"},
        "scene": 0, "scenes": [{"nodes": [0, 1]}], "nodes": [
            {"name": "Face", "mesh": 0, "skin": 0},
            {"name": "hips", "children": [2], "translation": [0.0, 0.5, 0.0]},
            {"name": "spine", "translation": [0.0, 0.3, 0.0]}],
        "meshes": [{"name": "Face", "primitives": [
            {"attributes": attrs, "indices": idx, "material": 0,
             "targets": [{"POSITION": morph}]}],
            "extras": {"targetNames": ["happy_shape"]}}],
        "skins": [{"joints": [1, 2], "inverseBindMatrices": ibm, "skeleton": 1}],
        "materials": [_mat("Face_Mat")], "extensionsUsed": ["VRMC_vrm"],
        "extensions": {"VRMC_vrm": ext},
    }
    return b.build(gltf)


def build_missing_spring_collider_group():
    """A SpringBone spring referencing collider-group index 5 (only 1 exists)."""
    b = GlbBuilder()
    attrs = _skin_attrs(b)
    idx = _idx(b)
    ibm = b.add(FLOAT, "MAT4", [tuple(IDENTITY16)] * 4)
    spring = {
        "specVersion": "1.0",
        "colliders": [{"node": 2, "shape": {"sphere": {
            "offset": [0.0, 0.0, 0.0], "radius": 0.1}}}],
        "colliderGroups": [{"name": "Head", "colliders": [0]}],
        "springs": [{"name": "Hair", "center": 3, "colliderGroups": [5],
                     "joints": [{"node": 3, "hitRadius": 0.02},
                                {"node": 4, "hitRadius": 0.02}]}],
    }
    gltf = {
        "asset": {"version": "2.0", "generator": "usdVrm negative fixtures"},
        "scene": 0, "scenes": [{"nodes": [0, 1]}], "nodes": [
            {"name": "Body", "mesh": 0, "skin": 0},
            {"name": "hips", "children": [2], "translation": [0.0, 0.5, 0.0]},
            {"name": "spine", "children": [3], "translation": [0.0, 0.3, 0.0]},
            {"name": "hair1", "children": [4], "translation": [0.0, 0.2, 0.0]},
            {"name": "hair2", "translation": [0.0, 0.2, 0.0]}],
        "meshes": [{"name": "Body", "primitives": [
            {"attributes": attrs, "indices": idx, "material": 0}]}],
        "skins": [{"joints": [1, 2, 3, 4], "inverseBindMatrices": ibm,
                   "skeleton": 1}],
        "materials": [_mat()], "extensionsUsed": ["VRMC_vrm", "VRMC_springBone"],
        "extensions": {"VRMC_vrm": vrm1_extension({"hips": 1, "spine": 2}),
                       "VRMC_springBone": spring},
    }
    return b.build(gltf)


FIXTURES = {
    "malformed_glb_json.vrm": build_malformed_glb_json,
    "dangling_texture_index.vrm": build_dangling_texture_index,
    "nonfinite_transform.vrm": build_nonfinite_transform,
    "nonobject_vrm_extension.vrm": build_nonobject_vrm_extension,
    "out_of_range_joints.vrm": build_out_of_range_joints,
    "unmapped_humanoid_node.vrm": build_unmapped_humanoid_node,
    "duplicate_humanoid_bone.vrm": build_duplicate_humanoid_bone,
    "oob_expression_morph.vrm": build_oob_expression_morph,
    "missing_spring_collider_group.vrm": build_missing_spring_collider_group,
}


def main() -> int:
    here = os.path.dirname(os.path.abspath(__file__))
    out_dir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        here, "..", "tests", "corpus", "generated", "malformed")
    os.makedirs(out_dir, exist_ok=True)
    for name, fn in FIXTURES.items():
        data = fn()
        with open(os.path.join(out_dir, name), "wb") as f:
            f.write(data)
        print(f"wrote {name} ({len(data)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
