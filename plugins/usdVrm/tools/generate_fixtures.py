#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Generate the committed, license-clean .vrm test fixtures.

Each fixture is authored entirely here (no third-party art) and targets a
specific import behavior the smoke test then asserts:

  minimal.vrm        skinned mesh + non-skinned node-placed accessory; humanoid
  vrm0_minimal.vrm   the VRM 0.x extension shape (extensions.VRM, humanBones[])
  vrm0_expressions.vrm VRM 0.x blendShapeMaster preset group (weight 0..100)
  multiskin_ibm.vrm  two skins, overlapping joints, non-identity inverse binds
  names.vrm          duplicate / Japanese / empty mesh & material names
  materials.vrm      alpha BLEND + double-sided, and alpha MASK with a cutoff
  badext.vrm         semantically broken VRM humanoid (must warn, not crash)

Usage: python generate_fixtures.py [out_dir]   (default: ../tests/fixtures)
"""
import os
import sys

from vrm_fixture_lib import (
    GlbBuilder, ARRAY_BUFFER, ELEMENT_ARRAY_BUFFER, FLOAT, U8, U16,
    TRI_POSITIONS, TRI_INDICES, IDENTITY16, translate16,
    vrm0_extension, vrm1_extension,
)

W4 = (1.0, 0.0, 0.0, 0.0)   # one fully-weighted influence
J0 = (0, 0, 0, 0)           # influence -> the skin's local joint 0


def _tri_accessors(b, with_skin=False):
    """Add the shared triangle accessors; return an attributes dict."""
    pos = b.add(FLOAT, "VEC3", TRI_POSITIONS, ARRAY_BUFFER, minmax=True)
    attrs = {"POSITION": pos}
    if with_skin:
        attrs["JOINTS_0"] = b.add(U8, "VEC4", [J0, J0, J0])
        attrs["WEIGHTS_0"] = b.add(FLOAT, "VEC4", [W4, W4, W4])
    return attrs


def _idx(b):
    return b.add(U16, "SCALAR", TRI_INDICES, ELEMENT_ARRAY_BUFFER)


def _basic_material(name="Mat"):
    return {"name": name, "pbrMetallicRoughness": {
        "baseColorFactor": [0.8, 0.6, 0.5, 1.0],
        "metallicFactor": 0.0, "roughnessFactor": 0.7}}


def build_minimal():
    b = GlbBuilder()
    skin_attrs = _tri_accessors(b, with_skin=True)
    idx = _idx(b)
    ibm = b.add(FLOAT, "MAT4", [tuple(IDENTITY16), tuple(IDENTITY16)])
    gltf = {
        "asset": {"version": "2.0", "generator": "usdVrm fixtures"},
        "scene": 0, "scenes": [{"nodes": [0, 1, 3]}],
        "nodes": [
            {"name": "Body", "mesh": 0, "skin": 0},
            {"name": "hips", "children": [2], "translation": [0.0, 0.5, 0.0]},
            {"name": "spine", "translation": [0.0, 0.3, 0.0]},
            {"name": "Accessory", "mesh": 1, "translation": [1.0, 0.0, 0.0]},
        ],
        "meshes": [
            {"name": "Body", "primitives": [
                {"attributes": skin_attrs, "indices": idx, "material": 0}]},
            {"name": "Accessory", "primitives": [
                {"attributes": {"POSITION": skin_attrs["POSITION"]},
                 "indices": idx, "material": 0}]},
        ],
        "skins": [{"joints": [1, 2], "inverseBindMatrices": ibm, "skeleton": 1}],
        "materials": [_basic_material("Body_Mat")],
        "extensionsUsed": ["VRMC_vrm"],
        "extensions": {"VRMC_vrm": vrm1_extension({"hips": 1, "spine": 2},
                                                  "usdVrm Minimal Fixture")},
    }
    return b.build(gltf)


def build_vrm0():
    b = GlbBuilder()
    skin_attrs = _tri_accessors(b, with_skin=True)
    idx = _idx(b)
    ibm = b.add(FLOAT, "MAT4", [tuple(IDENTITY16), tuple(IDENTITY16)])
    gltf = {
        "asset": {"version": "2.0", "generator": "usdVrm fixtures"},
        "scene": 0, "scenes": [{"nodes": [0, 1]}],
        "nodes": [
            {"name": "Body", "mesh": 0, "skin": 0},
            {"name": "hips", "children": [2], "translation": [0.0, 0.5, 0.0]},
            {"name": "spine", "translation": [0.0, 0.3, 0.0]},
        ],
        "meshes": [{"name": "Body", "primitives": [
            {"attributes": skin_attrs, "indices": idx, "material": 0}]}],
        "skins": [{"joints": [1, 2], "inverseBindMatrices": ibm, "skeleton": 1}],
        "materials": [_basic_material("Body_Mat")],
        "extensionsUsed": ["VRM"],
        "extensions": {"VRM": vrm0_extension({"hips": 1, "spine": 2})},
    }
    return b.build(gltf)


def build_multiskin_ibm():
    b = GlbBuilder()
    a0 = _tri_accessors(b, with_skin=True)
    a1 = _tri_accessors(b, with_skin=True)
    idx = _idx(b)
    # Non-identity inverse binds: IBM = inverse(world) so bind == world != I.
    ibm0 = b.add(FLOAT, "MAT4",
                 [tuple(translate16(0, -0.5, 0)), tuple(translate16(0, -0.8, 0))])
    ibm1 = b.add(FLOAT, "MAT4",
                 [tuple(translate16(0, -0.8, 0)), tuple(translate16(0, -1.0, 0))])
    gltf = {
        "asset": {"version": "2.0", "generator": "usdVrm fixtures"},
        "scene": 0, "scenes": [{"nodes": [0, 1, 3]}],
        "nodes": [
            {"name": "BodyA", "mesh": 0, "skin": 0},
            {"name": "hips", "children": [2], "translation": [0.0, 0.5, 0.0]},
            {"name": "spine", "children": [4], "translation": [0.0, 0.3, 0.0]},
            {"name": "BodyB", "mesh": 1, "skin": 1},
            {"name": "extra", "translation": [0.0, 0.2, 0.0]},
        ],
        "meshes": [
            {"name": "BodyA", "primitives": [
                {"attributes": a0, "indices": idx, "material": 0}]},
            {"name": "BodyB", "primitives": [
                {"attributes": a1, "indices": idx, "material": 0}]},
        ],
        "skins": [
            {"joints": [1, 2], "inverseBindMatrices": ibm0, "skeleton": 1},
            {"joints": [2, 4], "inverseBindMatrices": ibm1, "skeleton": 1},
        ],
        "materials": [_basic_material("Body_Mat")],
        "extensionsUsed": ["VRMC_vrm"],
        "extensions": {"VRMC_vrm": vrm1_extension({"hips": 1, "spine": 2})},
    }
    return b.build(gltf)


def build_names():
    b = GlbBuilder()
    pos = b.add(FLOAT, "VEC3", TRI_POSITIONS, ARRAY_BUFFER, minmax=True)
    idx = _idx(b)
    attrs = {"POSITION": pos}
    # Duplicate ("Body" x2), Japanese, and empty mesh names; duplicate material
    # names — all must sanitize/uniquify to distinct valid USD identifiers.
    names = ["Body", "Body", "顔", ""]   # 顔 = face
    nodes, meshes = [], []
    for i, nm in enumerate(names):
        nodes.append({"name": nm, "mesh": i})
        meshes.append({"name": nm, "primitives": [
            {"attributes": attrs, "indices": idx, "material": i % 2}]})
    gltf = {
        "asset": {"version": "2.0", "generator": "usdVrm fixtures"},
        "scene": 0, "scenes": [{"nodes": list(range(len(names)))}],
        "nodes": nodes, "meshes": meshes,
        "materials": [_basic_material("Mat"), _basic_material("Mat")],
        "extensionsUsed": ["VRMC_vrm"],
        "extensions": {"VRMC_vrm": vrm1_extension({})},
    }
    return b.build(gltf)


def build_materials():
    b = GlbBuilder()
    pos = b.add(FLOAT, "VEC3", TRI_POSITIONS, ARRAY_BUFFER, minmax=True)
    idx = _idx(b)
    attrs = {"POSITION": pos}
    gltf = {
        "asset": {"version": "2.0", "generator": "usdVrm fixtures"},
        "scene": 0, "scenes": [{"nodes": [0, 1]}],
        "nodes": [{"name": "Blended", "mesh": 0}, {"name": "Masked", "mesh": 1}],
        "meshes": [
            {"name": "Blended", "primitives": [
                {"attributes": attrs, "indices": idx, "material": 0}]},
            {"name": "Masked", "primitives": [
                {"attributes": attrs, "indices": idx, "material": 1}]},
        ],
        "materials": [
            {"name": "Glass", "doubleSided": True, "alphaMode": "BLEND",
             "pbrMetallicRoughness": {"baseColorFactor": [0.2, 0.4, 0.9, 0.3]}},
            {"name": "Leaf", "alphaMode": "MASK", "alphaCutoff": 0.3,
             "pbrMetallicRoughness": {"baseColorFactor": [0.1, 0.8, 0.2, 1.0]}},
        ],
        "extensionsUsed": ["VRMC_vrm"],
        "extensions": {"VRMC_vrm": vrm1_extension({})},
    }
    return b.build(gltf)


def build_badext():
    b = GlbBuilder()
    skin_attrs = _tri_accessors(b, with_skin=True)
    idx = _idx(b)
    ibm = b.add(FLOAT, "MAT4", [tuple(IDENTITY16), tuple(IDENTITY16)])
    # Valid glTF, but the VRM humanoid block is broken: a good bone, a bone whose
    # node is out of range, and 'meta' of the wrong type. The importer must warn
    # and skip, never crash, and still map the good bone.
    bad_vrm = {
        "specVersion": "1.0",
        "meta": "this should be an object",
        "humanoid": {"humanBones": {
            "hips": {"node": 1},
            "spine": {"node": 999},
        }},
    }
    gltf = {
        "asset": {"version": "2.0", "generator": "usdVrm fixtures"},
        "scene": 0, "scenes": [{"nodes": [0, 1]}],
        "nodes": [
            {"name": "Body", "mesh": 0, "skin": 0},
            {"name": "hips", "children": [2], "translation": [0.0, 0.5, 0.0]},
            {"name": "spine", "translation": [0.0, 0.3, 0.0]},
        ],
        "meshes": [{"name": "Body", "primitives": [
            {"attributes": skin_attrs, "indices": idx, "material": 0}]}],
        "skins": [{"joints": [1, 2], "inverseBindMatrices": ibm, "skeleton": 1}],
        "materials": [_basic_material("Body_Mat")],
        "extensionsUsed": ["VRMC_vrm"],
        "extensions": {"VRMC_vrm": bad_vrm},
    }
    return b.build(gltf)


def build_unordered_skel():
    """A skin whose joint list puts a child *before* its parent.

    glTF imposes no ordering on skin.joints, but UsdSkel requires parents before
    children. This guards the topological reorder: skin.joints = [spine, hips]
    with hips the parent of spine.
    """
    b = GlbBuilder()
    skin_attrs = _tri_accessors(b, with_skin=True)
    idx = _idx(b)
    ibm = b.add(FLOAT, "MAT4", [tuple(IDENTITY16), tuple(IDENTITY16)])
    gltf = {
        "asset": {"version": "2.0", "generator": "usdVrm fixtures"},
        "scene": 0, "scenes": [{"nodes": [0, 1]}],
        "nodes": [
            {"name": "Body", "mesh": 0, "skin": 0},
            {"name": "hips", "children": [2], "translation": [0.0, 0.5, 0.0]},
            {"name": "spine", "translation": [0.0, 0.3, 0.0]},
        ],
        "meshes": [{"name": "Body", "primitives": [
            {"attributes": skin_attrs, "indices": idx, "material": 0}]}],
        # joints intentionally child-first: [spine(2), hips(1)].
        "skins": [{"joints": [2, 1], "inverseBindMatrices": ibm, "skeleton": 1}],
        "materials": [_basic_material("Body_Mat")],
        "extensionsUsed": ["VRMC_vrm"],
        "extensions": {"VRMC_vrm": vrm1_extension({"hips": 1, "spine": 2})},
    }
    return b.build(gltf)


def build_expressions():
    """A morph target + a VRM 1.0 preset expression binding it (weight 1.0)."""
    b = GlbBuilder()
    skin_attrs = _tri_accessors(b, with_skin=True)
    idx = _idx(b)
    ibm = b.add(FLOAT, "MAT4", [tuple(IDENTITY16), tuple(IDENTITY16)])
    # Morph target: lift the apex vertex by 0.5 in Y.
    morph_pos = b.add(FLOAT, "VEC3",
                      [(0.0, 0.0, 0.0), (0.0, 0.0, 0.0), (0.0, 0.5, 0.0)])
    prim = {
        "attributes": skin_attrs, "indices": idx, "material": 0,
        "targets": [{"POSITION": morph_pos}],
    }
    gltf = {
        "asset": {"version": "2.0", "generator": "usdVrm fixtures"},
        "scene": 0, "scenes": [{"nodes": [0, 1]}],
        "nodes": [
            {"name": "Face", "mesh": 0, "skin": 0},
            {"name": "hips", "children": [2], "translation": [0.0, 0.5, 0.0]},
            {"name": "spine", "translation": [0.0, 0.3, 0.0]},
        ],
        "meshes": [{
            "name": "Face", "primitives": [prim],
            "extras": {"targetNames": ["happy_shape"]},
        }],
        "skins": [{"joints": [1, 2], "inverseBindMatrices": ibm, "skeleton": 1}],
        "materials": [_basic_material("Face_Mat")],
        "extensionsUsed": ["VRMC_vrm"],
        "extensions": {"VRMC_vrm": {
            **vrm1_extension({"hips": 1, "spine": 2}),
            "expressions": {"preset": {"happy": {
                "isBinary": False,
                "morphTargetBinds": [{"node": 0, "index": 0, "weight": 1.0}],
            }}},
        }},
    }
    return b.build(gltf)


def build_vrm0_expressions():
    """VRM 0.x blendShapeMaster: a preset group binding a morph (weight 0..100)."""
    b = GlbBuilder()
    skin_attrs = _tri_accessors(b, with_skin=True)
    idx = _idx(b)
    ibm = b.add(FLOAT, "MAT4", [tuple(IDENTITY16), tuple(IDENTITY16)])
    # Same apex-lift morph as the VRM 1.0 fixture.
    morph_pos = b.add(FLOAT, "VEC3",
                      [(0.0, 0.0, 0.0), (0.0, 0.0, 0.0), (0.0, 0.5, 0.0)])
    prim = {
        "attributes": skin_attrs, "indices": idx, "material": 0,
        "targets": [{"POSITION": morph_pos}],
    }
    gltf = {
        "asset": {"version": "2.0", "generator": "usdVrm fixtures"},
        "scene": 0, "scenes": [{"nodes": [0, 1]}],
        "nodes": [
            {"name": "Face", "mesh": 0, "skin": 0},
            {"name": "hips", "children": [2], "translation": [0.0, 0.5, 0.0]},
            {"name": "spine", "translation": [0.0, 0.3, 0.0]},
        ],
        "meshes": [{
            "name": "Face", "primitives": [prim],
            "extras": {"targetNames": ["happy_shape"]},
        }],
        "skins": [{"joints": [1, 2], "inverseBindMatrices": ibm, "skeleton": 1}],
        "materials": [_basic_material("Face_Mat")],
        "extensionsUsed": ["VRM"],
        "extensions": {"VRM": {
            **vrm0_extension({"hips": 1, "spine": 2}),
            # binds.mesh = glTF mesh index, weight 0..100 (-> 0..1 on import),
            # presetName "joy" marks it a preset.
            "blendShapeMaster": {"blendShapeGroups": [{
                "name": "Joy", "presetName": "joy", "isBinary": False,
                "binds": [{"mesh": 0, "index": 0, "weight": 100.0}],
            }]},
        }},
    }
    return b.build(gltf)


FIXTURES = {
    "minimal.vrm": build_minimal,
    "vrm0_minimal.vrm": build_vrm0,
    "multiskin_ibm.vrm": build_multiskin_ibm,
    "unordered_skel.vrm": build_unordered_skel,
    "expressions.vrm": build_expressions,
    "vrm0_expressions.vrm": build_vrm0_expressions,
    "names.vrm": build_names,
    "materials.vrm": build_materials,
    "badext.vrm": build_badext,
}


def main() -> int:
    here = os.path.dirname(os.path.abspath(__file__))
    out_dir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        here, "..", "tests", "fixtures")
    os.makedirs(out_dir, exist_ok=True)
    for name, fn in FIXTURES.items():
        data = fn()
        with open(os.path.join(out_dir, name), "wb") as f:
            f.write(data)
        print(f"wrote {name} ({len(data)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
