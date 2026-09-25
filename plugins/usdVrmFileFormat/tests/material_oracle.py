# SPDX-License-Identifier: Apache-2.0
"""What the importer must author as a material's canonical semantics.

Material policy §6 / P5 Step 4: every imported material carries
`VrmMaterialAPI`, an MToon one also `VrmMToonAPI`, and each texture it samples
one `VrmTextureInfoAPI:<role>` instance -- all as `inputs:vrm:*` on the
UsdShadeMaterial, every value authored.

For a glTF / VRM 1.0 source that mapping is a rename plus the specification
defaults, so it is restated here from the source JSON, independently of the
importer, and a stage is compared against it (`expected` vs `actual`). VRM 0.x
is not restated: its conversion is proved by the fixture pair, a 0.x file
whose canonical values must equal those of the 1.0 file UniVRM migrates it to.
"""
import json
import math
import struct

# VRMC_materials_mtoon 1.0 non-texture fields and their specification defaults.
MTOON_DEFAULTS = {
    "specVersion": "1.0",
    "transparentWithZWrite": False,
    "renderQueueOffsetNumber": 0,
    "shadeColorFactor": (1.0, 1.0, 1.0),
    "shadingShiftFactor": 0.0,
    "shadingToonyFactor": 0.9,
    "giEqualizationFactor": 0.9,
    "matcapFactor": (1.0, 1.0, 1.0),
    "parametricRimColorFactor": (0.0, 0.0, 0.0),
    "parametricRimFresnelPowerFactor": 5.0,
    "parametricRimLiftFactor": 0.0,
    "rimLightingMixFactor": 1.0,
    "outlineWidthMode": "none",
    "outlineWidthFactor": 0.0,
    "outlineColorFactor": (0.0, 0.0, 0.0),
    "outlineLightingMixFactor": 1.0,
    "uvAnimationScrollXSpeedFactor": 0.0,
    "uvAnimationScrollYSpeedFactor": 0.0,
    "uvAnimationRotationSpeedFactor": 0.0,
}

MTOON_TEXTURES = {
    "shadeMultiplyTexture": "shadeMultiply",
    "shadingShiftTexture": "shadingShift",
    "matcapTexture": "matcap",
    "rimMultiplyTexture": "rimMultiply",
    "outlineWidthMultiplyTexture": "outlineWidthMultiply",
    "uvAnimationMaskTexture": "uvAnimationMask",
}

_WRAP = {10497: "repeat", 33071: "clampToEdge", 33648: "mirroredRepeat"}

_CORE = "inputs:vrm:material:"
_MTOON = "inputs:vrm:mtoon:"
_TEX = "inputs:vrm:textureInfo:"


def read_glb(path):
    """(gltf JSON, BIN chunk bytes) of a .vrm / .glb; None if it is not one."""
    data = open(path, "rb").read()
    if data[:4] != b"glTF":
        return None
    json_len = struct.unpack_from("<I", data, 12)[0]
    gltf = json.loads(data[20:20 + json_len])
    rest = 20 + json_len
    binary = b""
    if rest + 8 <= len(data):
        bin_len = struct.unpack_from("<I", data, rest)[0]
        binary = data[rest + 8:rest + 8 + bin_len]
    return gltf, binary


def _image_bytes(gltf, binary, texture_index):
    image = gltf["images"][gltf["textures"][texture_index]["source"]]
    view = gltf["bufferViews"][image["bufferView"]]
    start = view.get("byteOffset", 0)
    return binary[start:start + view["byteLength"]]


def _texture(gltf, binary, info, contribution=None):
    """The expected VrmTextureInfoAPI instance for a glTF textureInfo."""
    texture = gltf["textures"][info["index"]]
    sampler = gltf["samplers"][texture["sampler"]] if "sampler" in texture else {}
    xf = info.get("extensions", {}).get("KHR_texture_transform", {})
    t = {
        "bytes": _image_bytes(gltf, binary, info["index"]),
        "texCoord": xf.get("texCoord", info.get("texCoord", 0)),
        "wrapS": _WRAP[sampler.get("wrapS", 10497)],
        "wrapT": _WRAP[sampler.get("wrapT", 10497)],
        "transform:offset": tuple(xf.get("offset", (0.0, 0.0))),
        "transform:rotation": xf.get("rotation", 0.0),
        "transform:scale": tuple(xf.get("scale", (1.0, 1.0))),
    }
    if contribution:
        name, value = contribution
        t[name] = value
    return t


def expected(gltf, binary, material_index):
    """Canonical semantics for a glTF / VRM 1.0 material, from its JSON."""
    m = gltf["materials"][material_index]
    pbr = m.get("pbrMetallicRoughness", {})
    ext = m.get("extensions", {})
    base = pbr.get("baseColorFactor", [1.0, 1.0, 1.0, 1.0])
    attrs = {
        _CORE + "baseColorFactor": tuple(base[:3]),
        _CORE + "baseColorAlphaFactor": base[3],
        _CORE + "metallicFactor": pbr.get("metallicFactor", 1.0),
        _CORE + "roughnessFactor": pbr.get("roughnessFactor", 1.0),
        _CORE + "emissiveFactor": tuple(m.get("emissiveFactor", (0.0, 0.0, 0.0))),
        _CORE + "emissiveStrength": ext.get(
            "KHR_materials_emissive_strength", {}).get("emissiveStrength", 1.0),
        _CORE + "alphaMode": m.get("alphaMode", "OPAQUE"),
        _CORE + "alphaCutoff": m.get("alphaCutoff", 0.5),
        _CORE + "doubleSided": m.get("doubleSided", False),
        _CORE + "unlit": "KHR_materials_unlit" in ext,
    }
    schemas = {"VrmMaterialAPI"}
    textures = {}
    for role, info, contribution in (
            ("baseColor", pbr.get("baseColorTexture"), None),
            ("metallicRoughness", pbr.get("metallicRoughnessTexture"), None),
            ("normal", m.get("normalTexture"), "scale"),
            ("occlusion", m.get("occlusionTexture"), "strength"),
            ("emissive", m.get("emissiveTexture"), None)):
        if info:
            value = info.get(contribution, 1.0) if contribution else None
            textures[role] = _texture(gltf, binary, info,
                                      (contribution, value) if contribution else None)

    mtoon = ext.get("VRMC_materials_mtoon")
    if mtoon is not None:
        schemas.add("VrmMToonAPI")
        for name, default in MTOON_DEFAULTS.items():
            value = mtoon.get(name, default)
            attrs[_MTOON + name] = tuple(value) if isinstance(value, list) else value
        for key, role in MTOON_TEXTURES.items():
            if key in mtoon:
                contribution = (("scale", mtoon[key].get("scale", 1.0))
                                if role == "shadingShift" else None)
                textures[role] = _texture(gltf, binary, mtoon[key], contribution)
    schemas |= {f"VrmTextureInfoAPI:{role}" for role in textures}
    return {"schemas": schemas, "attrs": attrs, "textures": textures}


def actual(prim):
    """The canonical semantics a stage carries on one Material prim."""
    from pxr import Ar

    schemas = {s for s in prim.GetAppliedSchemas() if s.startswith("Vrm")}
    attrs, textures = {}, {}
    for attr in prim.GetAuthoredAttributes():
        name = attr.GetName()
        if name.startswith(_TEX):
            role, field = name[len(_TEX):].split(":", 1)
            value = attr.Get()
            if field == "file":
                resolver = Ar.GetResolver()
                asset = resolver.OpenAsset(resolver.Resolve(value.path))
                assert asset, f"{name}: {value.path} does not open"
                value = bytes(asset.GetBuffer())
                field = "bytes"
            textures.setdefault(role, {})[field] = _plain(value)
        elif name.startswith(_CORE) or name.startswith(_MTOON):
            attrs[name] = _plain(attr.Get())
    return {"schemas": schemas, "attrs": attrs, "textures": textures}


def _plain(value):
    if isinstance(value, (bytes, str, bool, int, float)):
        return value
    try:
        return tuple(value)
    except TypeError:
        return str(value)  # TfToken


def _close(a, b, eps=1e-6):
    if isinstance(a, tuple) or isinstance(b, tuple):
        return (isinstance(a, tuple) and isinstance(b, tuple) and len(a) == len(b)
                and all(_close(x, y, eps) for x, y in zip(a, b)))
    if isinstance(a, bool) or isinstance(b, bool):
        return a is b
    if isinstance(a, (int, float)) and isinstance(b, (int, float)):
        return math.isclose(a, b, rel_tol=eps, abs_tol=eps)
    return a == b


def assert_same(want, got, where):
    """Every schema, attribute and texture field equal; nothing extra either side."""
    assert want["schemas"] == got["schemas"], (
        f"{where}: schemas {sorted(got['schemas'])} != {sorted(want['schemas'])}")
    assert want["attrs"].keys() == got["attrs"].keys(), (
        f"{where}: attributes differ: "
        f"missing {sorted(want['attrs'].keys() - got['attrs'].keys())}, "
        f"extra {sorted(got['attrs'].keys() - want['attrs'].keys())}")
    for name, value in want["attrs"].items():
        assert _close(value, got["attrs"][name]), (
            f"{where}: {name} = {got['attrs'][name]!r}, want {value!r}")
    assert want["textures"].keys() == got["textures"].keys(), (
        f"{where}: texture roles {sorted(got['textures'])} != {sorted(want['textures'])}")
    for role, fields in want["textures"].items():
        have = got["textures"][role]
        assert fields.keys() == have.keys(), (
            f"{where}: {role} fields {sorted(have)} != {sorted(fields)}")
        for field, value in fields.items():
            assert _close(value, have[field]), (
                f"{where}: {role}:{field} = {have[field]!r}, want {value!r}")
