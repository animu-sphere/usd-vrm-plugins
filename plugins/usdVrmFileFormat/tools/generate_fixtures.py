#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Generate the committed, license-clean .vrm test fixtures.

Each fixture is authored entirely here (no third-party art) and targets a
specific import behavior the smoke test then asserts:

  minimal.vrm        skinned mesh + non-skinned node-placed accessory; humanoid
  textures.vrm       embedded PNG base-color texture + an MToon material ext
  animation.vrm      a glTF rotation clip (spine 90 deg about Z over 1s)
  lookat.vrm         a bone-type lookAt with leftEye/rightEye humanoid bones
  springbone.vrm     a VRM 1.0 SpringBone hair chain + a sphere collider group
  shared_accessor.vrm two primitives sharing one vertex accessor (compaction)
  vrm0_minimal.vrm   the VRM 0.x extension shape (extensions.VRM, humanBones[])
  vrm0_expressions.vrm VRM 0.x blendShapeMaster preset group (weight 0..100)
  vrm0_frontbake.vrm VRM 0.x front-direction bake (geometry/skeleton/root clip)
  multiskin_ibm.vrm  two skins, overlapping joints, non-identity inverse binds
  unordered_skel.vrm skin joints listed child-before-parent (topology reorder)
  expressions.vrm    a morph target + a VRM 1.0 preset expression binding it
  names.vrm          duplicate / Japanese / empty mesh & material names
  materials.vrm      alpha BLEND + double-sided, alpha MASK with a cutoff, and
                     the unlit cases the MaterialX realization is built on:
                     unlit, unlit + texture + MASK, unlit + KHR_texture_transform
  constraints.vrm    a VRMC_node_constraint (roll) driving one node from another
  badext.vrm         semantically broken VRM humanoid (must warn, not crash)
  mtoon_vrm0.vrm     VRM 0.x MToon materialProperties covering every 0.x -> 1.0
                     conversion: render modes and queue ranking, shading ramp,
                     outline units, UV animation, texture tiling, defaults
  mtoon_vrm1.vrm     the same materials as UniVRM migrates them to VRM 1.0;
                     both must author the same canonical material values

Usage: python generate_fixtures.py [out_dir]   (default: ../tests/fixtures)
"""
import os
import sys

from vrm_fixture_lib import (
    GlbBuilder, ARRAY_BUFFER, ELEMENT_ARRAY_BUFFER, FLOAT, U8, U16,
    TRI_POSITIONS, TRI_INDICES, IDENTITY16, translate16,
    solid_png, vrm0_extension, vrm1_extension,
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
    # "Body_2" is the regression shape: it is a source name that spells the
    # suffix the second "Body" is about to be given, so a uniquifier that counts
    # bases instead of checking claimed names hands the same identifier to two
    # meshes, and `Define` silently returns the first prim for both.
    names = ["Body", "Body", "Body_2", "顔", ""]   # 顔 = face
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


def build_springbone():
    """A VRM 1.0 SpringBone: a 2-joint hair chain + a sphere collider group."""
    b = GlbBuilder()
    skin_attrs = _tri_accessors(b, with_skin=True)
    idx = _idx(b)
    ibm = b.add(FLOAT, "MAT4", [tuple(IDENTITY16)] * 4)
    spring = {
        "specVersion": "1.0",
        "colliders": [{"node": 2,
                       "shape": {"sphere": {"offset": [0.0, 0.0, 0.0],
                                            "radius": 0.1}}}],
        "colliderGroups": [{"name": "Head", "colliders": [0]}],
        "springs": [{
            "name": "Hair", "center": 5, "colliderGroups": [0],
            "joints": [
                {"node": 3, "hitRadius": 0.02, "stiffness": 1.0,
                 "gravityPower": 0.5, "gravityDir": [0.0, -1.0, 0.0],
                 "dragForce": 0.4},
                {"node": 4, "hitRadius": 0.02, "stiffness": 0.8,
                 "gravityPower": 0.5, "gravityDir": [0.0, -1.0, 0.0],
                 "dragForce": 0.4},
            ]}],
    }
    gltf = {
        "asset": {"version": "2.0", "generator": "usdVrm fixtures"},
        "scene": 0, "scenes": [{"nodes": [0, 1, 5]}],
        "nodes": [
            {"name": "Body", "mesh": 0, "skin": 0},
            {"name": "hips", "children": [2], "translation": [0.0, 0.5, 0.0]},
            {"name": "spine", "children": [3], "translation": [0.0, 0.3, 0.0]},
            {"name": "hair1", "children": [4], "translation": [0.0, 0.2, 0.0]},
            {"name": "hair2", "translation": [0.0, 0.2, 0.0]},
            {"name": "springCenter", "translation": [0.0, 0.5, 0.0]},
        ],
        "meshes": [{"name": "Body", "primitives": [
            {"attributes": skin_attrs, "indices": idx, "material": 0}]}],
        "skins": [{"joints": [1, 2, 3, 4],
                   "inverseBindMatrices": ibm, "skeleton": 1}],
        "materials": [_basic_material("Body_Mat")],
        "extensionsUsed": ["VRMC_vrm", "VRMC_springBone"],
        "extensions": {
            "VRMC_vrm": vrm1_extension({"hips": 1, "spine": 2}),
            "VRMC_springBone": spring,
        },
    }
    return b.build(gltf)


def build_shared_accessor():
    """One mesh, two primitives sharing a 4-vertex POSITION accessor.

    Guards per-primitive vertex compaction: each primitive references only a
    sub-range, so without compaction the full 4-vertex buffer would exceed the
    topology Hydra accepts. After import each primitive must have exactly its
    3 used vertices.
    """
    b = GlbBuilder()
    quad = [(-0.5, 0.0, 0.0), (0.5, 0.0, 0.0), (0.5, 1.0, 0.0), (-0.5, 1.0, 0.0)]
    pos = b.add(FLOAT, "VEC3", quad, ARRAY_BUFFER, minmax=True)
    i0 = b.add(U16, "SCALAR", [0, 1, 2], ELEMENT_ARRAY_BUFFER)
    i1 = b.add(U16, "SCALAR", [0, 2, 3], ELEMENT_ARRAY_BUFFER)
    gltf = {
        "asset": {"version": "2.0", "generator": "usdVrm fixtures"},
        "scene": 0, "scenes": [{"nodes": [0]}],
        "nodes": [{"name": "Quad", "mesh": 0}],
        "meshes": [{"name": "Quad", "primitives": [
            {"attributes": {"POSITION": pos}, "indices": i0, "material": 0},
            {"attributes": {"POSITION": pos}, "indices": i1, "material": 0}]}],
        "materials": [_basic_material("Quad_Mat")],
        "extensionsUsed": ["VRMC_vrm"],
        "extensions": {"VRMC_vrm": vrm1_extension({})},
    }
    return b.build(gltf)


def build_materials():
    b = GlbBuilder()
    pos = b.add(FLOAT, "VEC3", TRI_POSITIONS, ARRAY_BUFFER, minmax=True)
    idx = _idx(b)
    attrs = {"POSITION": pos}
    # UnlitCutout's own accessors: an unlit material that is *also* textured and
    # alpha-masked is the combination a VRM avatar is made of, and the one no
    # other fixture covers -- the MaterialX realization reaches its sampled
    # alpha, its factor multiply and its wrap modes only along this path.
    uv = b.add(FLOAT, "VEC2", [(0.0, 0.0), (1.0, 0.0), (0.5, 1.0)], ARRAY_BUFFER)
    # Half-transparent on purpose: with a=255 the sampled alpha is 1.0 and no
    # cutoff, blend or factor-alpha regression can move the result. 128/255 =
    # 0.502, times the 0.8 factor alpha, sits just above UnlitCutout's 0.4
    # cutoff -- close enough that dropping either term crosses it.
    img_bv = b.add_bytes(solid_png(40, 200, 40, 128))
    tex_attrs = {"POSITION": pos, "TEXCOORD_0": uv}
    gltf = {
        "asset": {"version": "2.0", "generator": "usdVrm fixtures"},
        "scene": 0, "scenes": [{"nodes": [0, 1, 2, 3, 4]}],
        "nodes": [{"name": "Blended", "mesh": 0}, {"name": "Masked", "mesh": 1},
                  {"name": "Flat", "mesh": 2}, {"name": "FlatCutout", "mesh": 3},
                  {"name": "FlatPlaced", "mesh": 4}],
        "meshes": [
            {"name": "Blended", "primitives": [
                {"attributes": attrs, "indices": idx, "material": 0}]},
            {"name": "Masked", "primitives": [
                {"attributes": attrs, "indices": idx, "material": 1}]},
            {"name": "Flat", "primitives": [
                {"attributes": attrs, "indices": idx, "material": 2}]},
            {"name": "FlatCutout", "primitives": [
                {"attributes": tex_attrs, "indices": idx, "material": 3}]},
            {"name": "FlatPlaced", "primitives": [
                {"attributes": tex_attrs, "indices": idx, "material": 4}]},
        ],
        "images": [{"name": "green", "bufferView": img_bv, "mimeType": "image/png"}],
        "samplers": [{"wrapS": 10497, "wrapT": 33071}],  # repeat / clamp
        "textures": [{"source": 0, "sampler": 0}],
        "materials": [
            {"name": "Glass", "doubleSided": True, "alphaMode": "BLEND",
             "pbrMetallicRoughness": {"baseColorFactor": [0.2, 0.4, 0.9, 0.3]}},
            {"name": "Leaf", "alphaMode": "MASK", "alphaCutoff": 0.3,
             "pbrMetallicRoughness": {"baseColorFactor": [0.1, 0.8, 0.2, 1.0]}},
            {"name": "Unlit", "extensions": {"KHR_materials_unlit": {}},
             "pbrMetallicRoughness": {"baseColorFactor": [0.9, 0.1, 0.1, 1.0]}},
            {"name": "UnlitCutout", "alphaMode": "MASK", "alphaCutoff": 0.4,
             "extensions": {"KHR_materials_unlit": {}},
             "pbrMetallicRoughness": {
                 "baseColorFactor": [0.3, 0.6, 0.9, 0.8],
                 "baseColorTexture": {"index": 0},
             }},
            # KHR_texture_transform with every term non-identity, which is the
            # only way the UV-flip conjugation and the radians/degrees
            # conversion are visible: on an identity transform (all the corpus
            # has) every wrong answer coincides with the right one.
            {"name": "UnlitPlaced", "extensions": {"KHR_materials_unlit": {}},
             "pbrMetallicRoughness": {
                 "baseColorFactor": [1.0, 1.0, 1.0, 1.0],
                 "baseColorTexture": {
                     "index": 0,
                     "extensions": {"KHR_texture_transform": {
                         "offset": [0.25, 0.125],
                         "scale": [2.0, 4.0],
                         "rotation": 1.5707963,  # pi/2 radians = 90 degrees
                     }},
                 },
             }},
        ],
        "extensionsUsed": ["VRMC_vrm", "KHR_materials_unlit",
                           "KHR_texture_transform"],
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
            "expressions": {
                "preset": {"happy": {
                    "isBinary": False,
                    # Two of the three override fields, and deliberately not the
                    # third: an avatar that says nothing about the gaze must
                    # author no `vrm:overrideLookAt` at all, because an absent
                    # attribute and an authored "none" are different claims.
                    "overrideBlink": "blend",
                    "overrideMouth": "block",
                    "morphTargetBinds": [{"node": 0, "index": 0, "weight": 1.0}],
                    # Also drive the material's emission to red
                    # (materialColorBinds).
                    "materialColorBinds": [
                        {"material": 0, "type": "emissionColor",
                         "targetValue": [1.0, 0.0, 0.0, 1.0]}],
                }},
                # A custom expression whose name is not an identifier at all: it
                # sanitizes to a hashed fallback prim name, so the only thing a
                # clip can join to it by is `vrm:expressionName`.
                "custom": {"笑顔": {          # 笑顔 = smile
                    "isBinary": True,
                    "morphTargetBinds": [{"node": 0, "index": 0, "weight": 0.5}],
                }},
            },
        }},
    }
    return b.build(gltf)


def build_vrm0_frontbake():
    """VRM 0.x front-direction bake: a skinned mesh + a root (hips) animation.

    The importer bakes a 180-deg Y rotation into geometry/skeleton/clip (rather
    than a root xformOp), so this exercises every baked path: skinned points,
    bind/root-rest transforms, and an embedded clip whose animated joint is the
    skeleton root. Points use z != 0 so the rotation is visible on both x and z.

    A spring bone with a non-vertical (model-space) gravityDir is included so the
    bake of that direction is exercised too: (1,0,0) -> (-1,0,0).
    """
    b = GlbBuilder()
    pts = [(-0.5, 0.0, 1.0), (0.5, 0.0, 1.0), (0.0, 1.0, 1.0)]
    pos = b.add(FLOAT, "VEC3", pts, ARRAY_BUFFER, minmax=True)
    j0 = b.add(U8, "VEC4", [J0, J0, J0])        # all verts -> local joint 0 (hips)
    w4 = b.add(FLOAT, "VEC4", [W4, W4, W4])
    idx = _idx(b)
    # bind == world (IBM = inverse(world)): hips world (0,0.5,0), spine (0,0.8,0).
    ibm = b.add(FLOAT, "MAT4",
                [tuple(translate16(0, -0.5, 0)), tuple(translate16(0, -0.8, 0))])
    # Animation on hips (node 1, the root): identity -> 90 deg about Y, and
    # translation (0,0.5,0) -> (1,0.5,0), both LINEAR over one second.
    t_in = b.add(FLOAT, "SCALAR", [0.0, 1.0])
    s = 0.70710678  # sin/cos(45 deg)
    r_out = b.add(FLOAT, "VEC4", [(0, 0, 0, 1), (0, s, 0, s)])  # (x,y,z,w), 90 Y
    tr_out = b.add(FLOAT, "VEC3", [(0, 0.5, 0), (1, 0.5, 0)])
    # A spring on spine (node 2) with a horizontal "wind" gravity. Model-space,
    # so the front bake must rotate it: (1,0,0) -> (-1,0,0).
    vrm0 = vrm0_extension({"hips": 1, "spine": 2})
    vrm0["secondaryAnimation"] = {
        "boneGroups": [{"comment": "Wind", "bones": [2],
                        "gravityDir": [1.0, 0.0, 0.0], "gravityPower": 0.5,
                        "stiffiness": 1.0, "dragForce": 0.4, "hitRadius": 0.0}],
        "colliderGroups": [],
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
            {"attributes": {"POSITION": pos, "JOINTS_0": j0, "WEIGHTS_0": w4},
             "indices": idx, "material": 0}]}],
        "skins": [{"joints": [1, 2], "inverseBindMatrices": ibm, "skeleton": 1}],
        "animations": [{
            "name": "move",
            "samplers": [
                {"input": t_in, "output": r_out, "interpolation": "LINEAR"},
                {"input": t_in, "output": tr_out, "interpolation": "LINEAR"}],
            "channels": [
                {"sampler": 0, "target": {"node": 1, "path": "rotation"}},
                {"sampler": 1, "target": {"node": 1, "path": "translation"}}],
        }],
        "materials": [_basic_material("Body_Mat")],
        "extensionsUsed": ["VRM"],
        "extensions": {"VRM": vrm0},
    }
    return b.build(gltf)


def build_constraints():
    """A VRMC_node_constraint (roll) driving the head node from the spine."""
    b = GlbBuilder()
    skin_attrs = _tri_accessors(b, with_skin=True)
    idx = _idx(b)
    ibm = b.add(FLOAT, "MAT4", [tuple(IDENTITY16)] * 3)
    gltf = {
        "asset": {"version": "2.0", "generator": "usdVrm fixtures"},
        "scene": 0, "scenes": [{"nodes": [0, 1]}],
        "nodes": [
            {"name": "Body", "mesh": 0, "skin": 0},
            {"name": "hips", "children": [2], "translation": [0.0, 0.5, 0.0]},
            {"name": "spine", "children": [3], "translation": [0.0, 0.3, 0.0]},
            {"name": "head", "translation": [0.0, 0.3, 0.0],
             "extensions": {"VRMC_node_constraint": {
                 "specVersion": "1.0",
                 "constraint": {"roll": {
                     "source": 2, "rollAxis": "X", "weight": 0.8}}}}},
        ],
        "meshes": [{"name": "Body", "primitives": [
            {"attributes": skin_attrs, "indices": idx, "material": 0}]}],
        "skins": [{"joints": [1, 2, 3],
                   "inverseBindMatrices": ibm, "skeleton": 1}],
        "materials": [_basic_material("Body_Mat")],
        "extensionsUsed": ["VRMC_vrm", "VRMC_node_constraint"],
        "extensions": {"VRMC_vrm": vrm1_extension(
            {"hips": 1, "spine": 2, "head": 3})},
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


def build_textures():
    """A base-color texture (embedded PNG) + an MToon material extension."""
    b = GlbBuilder()
    pos = b.add(FLOAT, "VEC3", TRI_POSITIONS, ARRAY_BUFFER, minmax=True)
    uv = b.add(FLOAT, "VEC2", [(0.0, 0.0), (1.0, 0.0), (0.5, 1.0)], ARRAY_BUFFER)
    idx = _idx(b)
    img_bv = b.add_bytes(solid_png(200, 40, 40))
    gltf = {
        "asset": {"version": "2.0", "generator": "usdVrm fixtures"},
        "scene": 0, "scenes": [{"nodes": [0]}],
        "nodes": [{"name": "Body", "mesh": 0}],
        "meshes": [{"name": "Body", "primitives": [{
            "attributes": {"POSITION": pos, "TEXCOORD_0": uv},
            "indices": idx, "material": 0}]}],
        "images": [{"name": "red", "bufferView": img_bv, "mimeType": "image/png"}],
        "samplers": [{"wrapS": 10497, "wrapT": 33071}],  # repeat / clamp
        "textures": [{"source": 0, "sampler": 0}],
        "materials": [{
            "name": "Skin",
            "alphaMode": "OPAQUE",
            "pbrMetallicRoughness": {
                "baseColorFactor": [0.5, 0.25, 0.75, 0.6],
                "baseColorTexture": {"index": 0},
            },
            "extensions": {"VRMC_materials_mtoon": {
                "specVersion": "1.0",
                "shadeColorFactor": [0.5, 0.2, 0.2],
            }},
        }],
        "extensionsUsed": ["VRMC_vrm", "VRMC_materials_mtoon"],
        "extensions": {"VRMC_vrm": vrm1_extension({})},
    }
    return b.build(gltf)


# The MToon pair: one set of materials written twice, as VRM 0.x
# `materialProperties` (mtoon_vrm0.vrm) and as the VRM 1.0 file UniVRM's
# migration turns that into (mtoon_vrm1.vrm). The importer must author the same
# canonical values for both (material policy §6.6), so every 1.0 value below is
# a literal worked out by hand from the 0.x one beside it -- never computed by
# the code under test -- with the arithmetic in the comment.
#
# Eight solid images, one per MToon texture slot, so a texture landing in the
# wrong role is a different file rather than the same one.
_MTOON_IMAGES = [(200, 40, 40), (40, 200, 40), (128, 128, 255), (250, 250, 10),
                 (10, 250, 250), (250, 10, 250), (90, 60, 30), (30, 60, 90)]
_LIT, _SHADE, _BUMP, _EMIT, _SPHERE, _RIM, _OUTLINE, _UVMASK = range(8)


def _mtoon_scene(b, materials, ext_name, ext, extra_used=()):
    pos = b.add(FLOAT, "VEC3", TRI_POSITIONS, ARRAY_BUFFER, minmax=True)
    uv = b.add(FLOAT, "VEC2", [(0.0, 0.0), (1.0, 0.0), (0.5, 1.0)], ARRAY_BUFFER)
    idx = _idx(b)
    images = [{"name": f"img{i}", "bufferView": b.add_bytes(solid_png(*rgb)),
               "mimeType": "image/png"} for i, rgb in enumerate(_MTOON_IMAGES)]
    return {
        "asset": {"version": "2.0", "generator": "usdVrm fixtures"},
        "scene": 0, "scenes": [{"nodes": [0]}],
        "nodes": [{"name": "Body", "mesh": 0}],
        "meshes": [{"name": "Body", "primitives": [{
            "attributes": {"POSITION": pos, "TEXCOORD_0": uv},
            "indices": idx, "material": 0}]}],
        "images": images,
        "samplers": [{"wrapS": 10497, "wrapT": 33071}],  # repeat / clamp
        "textures": [{"source": i, "sampler": 0} for i in range(len(images))],
        "materials": materials,
        "extensionsUsed": [ext_name, *extra_used],
        "extensions": {ext_name: ext},
    }


def build_mtoon_vrm0():
    """VRM 0.x MToon, every conversion the 0.x -> 1.0 table names."""
    b = GlbBuilder()

    def mtoon(name, render_queue, floats, vectors=None, textures=None):
        return {"name": name, "shader": "VRM/MToon", "renderQueue": render_queue,
                "floatProperties": floats, "vectorProperties": vectors or {},
                "textureProperties": textures or {},
                "keywordMap": {}, "tagMap": {}}

    material_properties = [
        # Every property stated, every texture slot filled.
        mtoon("Hair", 2510, {
            "_BlendMode": 3, "_CullMode": 0, "_Cutoff": 0.5, "_BumpScale": 0.8,
            "_ShadeShift": -0.2, "_ShadeToony": 0.5,
            "_IndirectLightIntensity": 0.25,
            "_RimFresnelPower": 3.0, "_RimLift": 0.1, "_RimLightingMix": 0.4,
            "_OutlineWidthMode": 1, "_OutlineWidth": 0.5,
            "_OutlineColorMode": 1, "_OutlineLightingMix": 0.6,
            "_UvAnimScrollX": 0.5, "_UvAnimScrollY": 0.25,
            "_UvAnimRotation": 0.25,
        }, {
            "_Color": [0.5, 1.0, 0.25, 0.75],
            "_ShadeColor": [0.5, 0.5, 0.5, 1.0],
            "_RimColor": [1.0, 0.5, 0.0, 1.0],
            "_OutlineColor": [0.25, 0.5, 1.0, 1.0],
            "_EmissionColor": [0.1, 0.2, 0.3, 1.0],
            "_MainTex": [0.25, 0.5, 2.0, 0.5],  # Unity offset.xy, scale.xy
        }, {
            "_MainTex": _LIT, "_ShadeTexture": _SHADE, "_BumpMap": _BUMP,
            "_EmissionMap": _EMIT, "_SphereAdd": _SPHERE, "_RimTexture": _RIM,
            "_OutlineWidthTexture": _OUTLINE, "_UvAnimMaskTexture": _UVMASK,
        }),
        # Cutout, screen-space outline with a fixed colour, no shade texture
        # (the lit texture stands in), no sphere; shading, GI and shade colour
        # left to the MToon 0.x shader defaults.
        mtoon("Face", 2450, {
            "_BlendMode": 1, "_CullMode": 2, "_Cutoff": 0.3,
            "_OutlineWidthMode": 2, "_OutlineWidth": 1.0,
            "_OutlineColorMode": 0, "_OutlineLightingMix": 0.8,
        }, {"_Color": [1.0, 1.0, 1.0, 1.0], "_MainTex": [0.0, 0.0, 1.0, 1.0]},
            {"_MainTex": _LIT}),
        # Transparent queues 3000 and 2990: ranked 0 and -1.
        mtoon("Veil", 3000, {"_BlendMode": 2}),
        mtoon("Tear", 2990, {"_BlendMode": 2, "_CullMode": 1}),
        # Transparent-with-z-write 2501 (and Hair's 2510): ranked 0 (and 1).
        mtoon("Lashes", 2501, {"_BlendMode": 3}),
        # Not MToon: the glTF core is the whole story.
        {"name": "Plain", "shader": "VRM_USE_GLTFSHADER", "renderQueue": -1,
         "floatProperties": {}, "vectorProperties": {}, "textureProperties": {},
         "keywordMap": {}, "tagMap": {}},
    ]
    # The glTF core an old exporter wrote beside them: gamma colours, OPAQUE,
    # single-sided, no unlit. The importer must read materialProperties for an
    # MToon material, so none of this may reach its canonical values -- except
    # metallic / roughness, which UniVRM keeps from here.
    core = {"pbrMetallicRoughness": {"baseColorFactor": [0.5, 1.0, 0.25, 0.75],
                                     "metallicFactor": 0.0, "roughnessFactor": 0.9}}
    materials = [dict(name=mp["name"], **core) for mp in material_properties[:-1]]
    materials.append({"name": "Plain", "alphaMode": "MASK", "alphaCutoff": 0.25,
                      "pbrMetallicRoughness": {
                          "baseColorFactor": [0.2, 0.3, 0.4, 1.0],
                          "baseColorTexture": {"index": _LIT}}})
    ext = vrm0_extension({})
    ext["materialProperties"] = material_properties
    return b.build(_mtoon_scene(b, materials, "VRM", ext))


def build_mtoon_vrm1():
    """mtoon_vrm0.vrm as UniVRM's migration writes it in VRM 1.0."""
    b = GlbBuilder()
    # _MainTex's Unity tiling (offset (0.25, 0.5), scale (2, 0.5), bottom-left
    # origin) as KHR_texture_transform: offset.y = 1 - 0.5 - 0.5 = 0.
    hair_xf = {"KHR_texture_transform": {"offset": [0.25, 0.0], "scale": [2.0, 0.5]}}
    ident_xf = {"KHR_texture_transform": {"offset": [0.0, 0.0], "scale": [1.0, 1.0]}}
    unlit = {"KHR_materials_unlit": {}}

    def tex(index, xf=None, **extra):
        t = {"index": index, **extra}
        if xf:
            t["extensions"] = xf
        return t

    def material(name, alpha_mode, double_sided, mtoon, base=(1.0, 1.0, 1.0, 1.0),
                 cutoff=0.5, base_tex=None, **core):
        pbr = {"baseColorFactor": list(base), "metallicFactor": 0.0,
               "roughnessFactor": 0.9}
        if base_tex:
            pbr["baseColorTexture"] = base_tex
        m = {"name": name, "alphaMode": alpha_mode, "alphaCutoff": cutoff,
             "doubleSided": double_sided, "pbrMetallicRoughness": pbr,
             "extensions": {**unlit, "VRMC_materials_mtoon": {
                 "specVersion": "1.0", **mtoon}}}
        m.update(core)
        return m

    # sRGB -> linear: 0.5 -> 0.21404114, 0.25 -> 0.05087609; the shader's
    # default shade colour (0.97, 0.81, 0.86) -> (0.93310684, 0.62091586,
    # 0.71056649).
    default_shade = [0.93310684, 0.62091586, 0.71056649]
    # Shading with the shader defaults toony 0.9, shift 0:
    #   max = lerp(1, 0, 0.9) = 0.1, min = 0
    #   toony = (2 - (0.1 - 0)) / 2 = 0.95, shift = -(0.1 + 0) / 2 = -0.05
    default_shading = {"shadingShiftFactor": -0.05, "shadingToonyFactor": 0.95,
                       "giEqualizationFactor": 0.9}  # 1 - 0.1
    # Every MToon material: no sphere -> matcap black; rim lighting mix 1.
    defaults = {"shadeColorFactor": default_shade, **default_shading,
                "matcapFactor": [0.0, 0.0, 0.0], "rimLightingMixFactor": 1.0,
                "parametricRimFresnelPowerFactor": 1.0,
                "outlineWidthMode": "none", "outlineLightingMixFactor": 0.0}
    materials = [
        material("Hair", "BLEND", True, {
            "transparentWithZWrite": True, "renderQueueOffsetNumber": 1,
            "shadeColorFactor": [0.21404114] * 3,
            "shadeMultiplyTexture": tex(_SHADE, hair_xf),
            # max = lerp(1, -0.2, 0.5) = 0.4, min = -0.2
            # toony = (2 - 0.6) / 2 = 0.7, shift = -(0.4 - 0.2) / 2 = -0.1
            "shadingShiftFactor": -0.1, "shadingToonyFactor": 0.7,
            "giEqualizationFactor": 0.75,                       # 1 - 0.25
            "matcapFactor": [1.0, 1.0, 1.0], "matcapTexture": tex(_SPHERE),
            "parametricRimColorFactor": [1.0, 0.21404114, 0.0],
            "parametricRimFresnelPowerFactor": 3.0,
            "parametricRimLiftFactor": 0.1,
            "rimMultiplyTexture": tex(_RIM, hair_xf),
            "rimLightingMixFactor": 1.0,                        # not 0.4
            "outlineWidthMode": "worldCoordinates",
            "outlineWidthFactor": 0.005,                        # 0.5 cm
            "outlineWidthMultiplyTexture": tex(_OUTLINE, hair_xf),
            "outlineColorFactor": [0.05087609, 0.21404114, 1.0],
            "outlineLightingMixFactor": 0.6,
            "uvAnimationMaskTexture": tex(_UVMASK, hair_xf),
            "uvAnimationScrollXSpeedFactor": 0.5,
            "uvAnimationScrollYSpeedFactor": -0.25,             # V flipped
            "uvAnimationRotationSpeedFactor": 1.5707963,        # 0.25 turn/s
        }, base=(0.21404114, 1.0, 0.05087609, 0.75),
            base_tex=tex(_LIT, hair_xf),
            normalTexture=tex(_BUMP, hair_xf, scale=0.8),
            emissiveFactor=[0.1, 0.2, 0.3],
            emissiveTexture=tex(_EMIT, hair_xf)),
        material("Face", "MASK", False, {
            **defaults,
            "shadeMultiplyTexture": tex(_LIT, ident_xf),        # lit stands in
            "outlineWidthMode": "screenCoordinates",
            "outlineWidthFactor": 0.005,                        # 1.0 / 200
            "outlineLightingMixFactor": 0.0,                    # fixed colour
        }, cutoff=0.3, base_tex=tex(_LIT, ident_xf)),
        material("Veil", "BLEND", False, {**defaults, "renderQueueOffsetNumber": 0}),
        material("Tear", "BLEND", True, {**defaults, "renderQueueOffsetNumber": -1}),
        material("Lashes", "BLEND", False, {
            **defaults, "transparentWithZWrite": True,
            "renderQueueOffsetNumber": 0}),
        {"name": "Plain", "alphaMode": "MASK", "alphaCutoff": 0.25,
         "pbrMetallicRoughness": {"baseColorFactor": [0.2, 0.3, 0.4, 1.0],
                                  "baseColorTexture": {"index": _LIT}}},
    ]
    return b.build(_mtoon_scene(
        b, materials, "VRMC_vrm", vrm1_extension({}),
        extra_used=("VRMC_materials_mtoon", "KHR_materials_unlit",
                    "KHR_texture_transform")))


def build_animation():
    """A clip that rotates the spine 90 deg about Z over one second (LINEAR)."""
    b = GlbBuilder()
    skin_attrs = _tri_accessors(b, with_skin=True)
    idx = _idx(b)
    ibm = b.add(FLOAT, "MAT4", [tuple(IDENTITY16), tuple(IDENTITY16)])
    t_in = b.add(FLOAT, "SCALAR", [0.0, 1.0])
    s = 0.70710678  # sin/cos(45 deg) -> 90 deg quaternion about Z (x,y,z,w)
    r_out = b.add(FLOAT, "VEC4", [(0.0, 0.0, 0.0, 1.0), (0.0, 0.0, s, s)])
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
        "animations": [{
            "name": "wave",
            "samplers": [{"input": t_in, "output": r_out, "interpolation": "LINEAR"}],
            "channels": [{"sampler": 0,
                          "target": {"node": 2, "path": "rotation"}}],
        }],
        "materials": [_basic_material("Body_Mat")],
        "extensionsUsed": ["VRMC_vrm"],
        "extensions": {"VRMC_vrm": vrm1_extension({"hips": 1, "spine": 2})},
    }
    return b.build(gltf)


def build_lookat():
    """A bone-type lookAt with leftEye/rightEye humanoid bones."""
    b = GlbBuilder()
    skin_attrs = _tri_accessors(b, with_skin=True)
    idx = _idx(b)
    ibm = b.add(FLOAT, "MAT4", [tuple(IDENTITY16)] * 5)
    ext = vrm1_extension({"hips": 1, "spine": 2, "head": 3,
                          "leftEye": 4, "rightEye": 5})
    ext["lookAt"] = {
        "type": "bone",
        "offsetFromHeadBone": [0.0, 0.06, 0.0],
        "rangeMapHorizontalInner": {"inputMaxValue": 90.0, "outputScale": 10.0},
        "rangeMapHorizontalOuter": {"inputMaxValue": 90.0, "outputScale": 10.0},
    }
    gltf = {
        "asset": {"version": "2.0", "generator": "usdVrm fixtures"},
        "scene": 0, "scenes": [{"nodes": [0, 1]}],
        "nodes": [
            {"name": "Body", "mesh": 0, "skin": 0},
            {"name": "hips", "children": [2], "translation": [0.0, 0.5, 0.0]},
            {"name": "spine", "children": [3], "translation": [0.0, 0.3, 0.0]},
            {"name": "head", "children": [4, 5], "translation": [0.0, 0.3, 0.0]},
            {"name": "leftEye", "translation": [0.03, 0.05, 0.05]},
            {"name": "rightEye", "translation": [-0.03, 0.05, 0.05]},
        ],
        "meshes": [{"name": "Body", "primitives": [
            {"attributes": skin_attrs, "indices": idx, "material": 0}]}],
        "skins": [{"joints": [1, 2, 3, 4, 5],
                   "inverseBindMatrices": ibm, "skeleton": 1}],
        "materials": [_basic_material("Body_Mat")],
        "extensionsUsed": ["VRMC_vrm"],
        "extensions": {"VRMC_vrm": ext},
    }
    return b.build(gltf)


FIXTURES = {
    "minimal.vrm": build_minimal,
    "textures.vrm": build_textures,
    "animation.vrm": build_animation,
    "lookat.vrm": build_lookat,
    "springbone.vrm": build_springbone,
    "shared_accessor.vrm": build_shared_accessor,
    "vrm0_minimal.vrm": build_vrm0,
    "multiskin_ibm.vrm": build_multiskin_ibm,
    "unordered_skel.vrm": build_unordered_skel,
    "expressions.vrm": build_expressions,
    "vrm0_expressions.vrm": build_vrm0_expressions,
    "vrm0_frontbake.vrm": build_vrm0_frontbake,
    "names.vrm": build_names,
    "materials.vrm": build_materials,
    "constraints.vrm": build_constraints,
    "badext.vrm": build_badext,
    "mtoon_vrm0.vrm": build_mtoon_vrm0,
    "mtoon_vrm1.vrm": build_mtoon_vrm1,
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
