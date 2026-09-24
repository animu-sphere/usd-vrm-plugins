#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Schema-only verification for the vrmSchema bundle.

Runs with no .vrm importer in the session (WORKSPACE.md §1: schema tests must
not require the file-format bundle). Asserts:

  1. discovery — the `vrmSchema` plugin is registered and declares exactly the
     nine Vrm*API types (WORKSPACE.md §6 invariant 5: registration moves are
     proven by discovery tests in the same PR).
  2. schema registry — every API has an applied-API prim definition whose
     builtin properties match the committed generatedSchema.usda.
  3. apply — ApplyAPI() by schema identifier works on an in-memory stage and
     the builtins expose their authored/fallback values.
  4. fixture — tests/fixtures/basic.usda opens and every prim that applies a
     Vrm*API validates against its prim definition.
  5. material contract — the three material schemas apply to a Material and
     nothing else, `VrmTextureInfoAPI` takes exactly its eleven role names,
     `VrmMToonAPI` carries the VRMC_materials_mtoon 1.0 fields under their
     specification names and defaults, and every canonical property is a
     UsdShade input a realization graph can connect to (material policy §6,
     §11 q1/q4/q9).

Env: the vrmSchema plugInfo must be discoverable (PXR_PLUGINPATH_NAME), either
via `ost plugin run plugins/vrmSchema -- python tests/test_vrmschema_plugin.py`
or the CTest wiring in tests/CMakeLists.txt.
"""
from __future__ import annotations

import pathlib
import sys

from pxr import Gf, Plug, Sdf, Usd, UsdShade

BUNDLE = pathlib.Path(__file__).resolve().parents[1]

# The control-prim schemas under /Asset/rig; single-apply, any prim type.
SCHEMA_APIS = [
    "VrmColliderAPI",
    "VrmConstraintAPI",
    "VrmExpressionAPI",
    "VrmHumanoidAPI",
    "VrmLookAtAPI",
    "VrmSpringBoneAPI",
]

# The canonical material schemas (material policy §6), Material-only.
MATERIAL_APIS = ["VrmMaterialAPI", "VrmMToonAPI"]
TEXTURE_INFO_API = "VrmTextureInfoAPI"
TEXTURE_ROLES = [
    "baseColor", "metallicRoughness", "normal", "occlusion", "emissive",
    "shadeMultiply", "shadingShift", "matcap", "rimMultiply",
    "outlineWidthMultiply", "uvAnimationMask",
]

ALL_APIS = SCHEMA_APIS + MATERIAL_APIS + [TEXTURE_INFO_API]

# Contract attributes that belong to no API schema. `vrm:shaderModel` predates
# VrmMToonAPI and stays beside it (material policy §11 q5): a material with
# VrmMToonAPI applied must also say "MToon".
CONTRACT_ATTRIBUTES = {"vrm:shaderModel"}

# Every non-texture field of VRMC_materials_mtoon 1.0 with the default its
# JSON schema declares (vrm-c/vrm-specification,
# specification/VRMC_materials_mtoon-1.0/schema/VRMC_materials_mtoon.schema.json).
# specVersion is required there and has no default; the schema's fallback is
# the version of the model the canonical values follow.
MTOON_FIELDS = {
    "specVersion": "1.0",
    "transparentWithZWrite": False,
    "renderQueueOffsetNumber": 0,
    "shadeColorFactor": (1.0, 1.0, 1.0),
    "shadingShiftFactor": 0.0,
    "shadingToonyFactor": 0.9,
    "giEqualizationFactor": 0.9,
    "matcapFactor": (1.0, 1.0, 1.0),
    "parametricRimColorFactor": (0.0, 0.0, 0.0),
    "rimLightingMixFactor": 1.0,
    "parametricRimFresnelPowerFactor": 5.0,
    "parametricRimLiftFactor": 0.0,
    "outlineWidthMode": "none",
    "outlineWidthFactor": 0.0,
    "outlineColorFactor": (0.0, 0.0, 0.0),
    "outlineLightingMixFactor": 1.0,
    "uvAnimationScrollXSpeedFactor": 0.0,
    "uvAnimationScrollYSpeedFactor": 0.0,
    "uvAnimationRotationSpeedFactor": 0.0,
}

_failures: list[str] = []


def check(cond: bool, msg: str) -> None:
    tag = "ok" if cond else "FAIL"
    print(f"[{tag}] {msg}")
    if not cond:
        _failures.append(msg)


def test_discovery() -> None:
    plugin = Plug.Registry().GetPluginWithName("vrmSchema")
    check(plugin is not None, "plugin 'vrmSchema' is registered")
    if not plugin:
        return
    types = dict(plugin.metadata).get("Types", {})
    declared = sorted(types.keys())
    expected = sorted(f"UsdVrm{name[3:]}" for name in ALL_APIS)
    check(declared == expected,
          f"vrmSchema declares exactly the nine schema types (got {declared})")
    for type_name, meta in types.items():
        kind = ("multipleApplyAPI" if type_name == f"UsdVrm{TEXTURE_INFO_API[3:]}"
                else "singleApplyAPI")
        check(dict(meta).get("schemaKind") == kind,
              f"{type_name} is a {kind}")


def test_registry_matches_generated_schema() -> None:
    gen = BUNDLE / "plugin" / "resources" / "vrmSchema" / "generatedSchema.usda"
    layer = Sdf.Layer.OpenAsAnonymous(str(gen))
    check(layer is not None, "generatedSchema.usda parses")
    registry = Usd.SchemaRegistry()
    for prim in layer.rootPrims:
        prim_def = registry.FindAppliedAPIPrimDefinition(prim.name)
        check(prim_def is not None,
              f"{prim.name} has an applied-API prim definition")
        if not prim_def:
            continue
        authored = {p.name for p in prim.attributes}
        authored |= {r.name for r in prim.relationships}
        builtin = set(prim_def.GetPropertyNames())
        missing = sorted(authored - builtin)
        check(not missing,
              f"{prim.name}: all generatedSchema properties are builtins"
              + (f" (missing {missing})" if missing else ""))


def test_apply() -> None:
    stage = Usd.Stage.CreateInMemory()
    prim = stage.DefinePrim("/Rig", "Scope")
    for name in SCHEMA_APIS:
        check(bool(prim.ApplyAPI(name)), f"ApplyAPI('{name}') succeeds")
    applied = prim.GetAppliedSchemas()
    check(sorted(applied) == sorted(SCHEMA_APIS),
          f"all six APIs applied (got {sorted(applied)})")
    attr = prim.GetAttribute("vrm:humanBones:hips")
    check(attr.IsValid() and not attr.HasAuthoredValue(),
          "VrmHumanoidAPI builtin is valid without an authored value")
    attr.Set("J_Bip_C_Hips")
    check(attr.Get() == "J_Bip_C_Hips", "builtin token round-trips a value")


def test_fixture() -> None:
    fixture = BUNDLE / "tests" / "fixtures" / "basic.usda"
    stage = Usd.Stage.Open(str(fixture))
    check(stage is not None, "tests/fixtures/basic.usda opens")
    if not stage:
        return
    seen: set[str] = set()
    for prim in stage.Traverse():
        vrm_schemas = [s for s in prim.GetAppliedSchemas() if s.startswith("Vrm")]
        for schema in vrm_schemas:
            family, _ = Usd.SchemaRegistry.GetTypeNameAndInstance(schema)
            seen.add(family)
            prim_def = Usd.SchemaRegistry().FindAppliedAPIPrimDefinition(family)
            check(prim_def is not None, f"{prim.GetPath()}: {schema} resolves")
        if not vrm_schemas:
            continue
        # The prim's composed definition, so a multiple-apply instance's
        # properties are checked under their instance names.
        builtin = set(prim.GetPrimDefinition().GetPropertyNames())
        for prop in prim.GetAuthoredProperties():
            name = prop.GetName()
            if name in CONTRACT_ATTRIBUTES:
                continue
            if name.startswith("vrm:") or name.startswith("inputs:vrm:"):
                check(name in builtin,
                      f"{prim.GetPath()}.{name} is a builtin of {vrm_schemas}")
    check(seen == set(ALL_APIS),
          f"fixture exercises all nine APIs (got {sorted(seen)})")

    hair = stage.GetPrimAtPath("/Asset/mtl/Hair")
    toony = hair.GetAttribute("inputs:vrm:mtoon:shadingToonyFactor")
    check(abs(toony.Get() - 0.95) < 1e-6,
          "fixture MToon value reads back through the typed property")
    check(hair.GetAttribute("vrm:shaderModel").Get() == "MToon",
          "fixture MToon material also says vrm:shaderModel = 'MToon'")


def _material(stage: Usd.Stage, path: str = "/Mat") -> Usd.Prim:
    return UsdShade.Material.Define(stage, path).GetPrim()


def test_material_apply_targets() -> None:
    stage = Usd.Stage.CreateInMemory()
    mat = _material(stage)
    scope = stage.DefinePrim("/Scope", "Scope")
    for name in MATERIAL_APIS:
        check(bool(mat.CanApplyAPI(name)), f"{name} can apply to a Material")
        check(not scope.CanApplyAPI(name), f"{name} cannot apply to a Scope")
    check(not scope.CanApplyAPI(TEXTURE_INFO_API, "baseColor"),
          f"{TEXTURE_INFO_API} cannot apply to a Scope")

    # §11 q4: exactly the eleven roles. `outlineWidth` is the name the policy
    # sketched before the specification was checked; the texture is
    # `outlineWidthMultiplyTexture`.
    for role in TEXTURE_ROLES:
        check(bool(mat.CanApplyAPI(TEXTURE_INFO_API, role)),
              f"{TEXTURE_INFO_API}:{role} is an allowed instance")
    for role in ("outlineWidth", "bogus", "baseColorTexture"):
        check(not mat.CanApplyAPI(TEXTURE_INFO_API, role),
              f"{TEXTURE_INFO_API}:{role} is not an allowed instance")


def test_mtoon_fields_match_specification() -> None:
    stage = Usd.Stage.CreateInMemory()
    mat = _material(stage)
    check(bool(mat.ApplyAPI("VrmMToonAPI")), "ApplyAPI('VrmMToonAPI') succeeds")
    prefix = "inputs:vrm:mtoon:"
    names = {n[len(prefix):] for n in mat.GetPrimDefinition().GetPropertyNames()
             if n.startswith(prefix)}
    detail = ""
    if names != set(MTOON_FIELDS):
        detail = (f" (extra {sorted(names - set(MTOON_FIELDS))}, "
                  f"missing {sorted(set(MTOON_FIELDS) - names)})")
    check(names == set(MTOON_FIELDS),
          "VrmMToonAPI carries exactly the VRMC_materials_mtoon 1.0 fields"
          + detail)
    for field, default in MTOON_FIELDS.items():
        value = mat.GetAttribute(prefix + field).Get()
        if isinstance(default, tuple):
            ok = value is not None and Gf.IsClose(Gf.Vec3f(*default), value, 1e-6)
        elif isinstance(default, float):
            ok = value is not None and abs(value - default) < 1e-6
        else:
            ok = value == default
        check(ok, f"{field} falls back to the specification default {default!r}"
                  f" (got {value!r})")


def test_canonical_properties_are_connectable_inputs() -> None:
    """§11 q9: canonical values are Material interface inputs, so a realization
    graph reads them through a UsdShade connection, animated values included."""
    stage = Usd.Stage.CreateInMemory()
    mat_prim = _material(stage)
    for name in MATERIAL_APIS:
        mat_prim.ApplyAPI(name)
    mat_prim.ApplyAPI(TEXTURE_INFO_API, "shadeMultiply")
    mat = UsdShade.Material(mat_prim)

    families = ("inputs:vrm:material:", "inputs:vrm:mtoon:",
                "inputs:vrm:textureInfo:shadeMultiply:")
    builtin = [n for n in mat_prim.GetPrimDefinition().GetPropertyNames()
               if n.startswith("inputs:vrm:")]
    check(len(builtin) == 10 + len(MTOON_FIELDS) + 9,
          f"every material builtin is under inputs:vrm: (got {len(builtin)})")
    for name in builtin:
        check(name.startswith(families), f"{name} is in a canonical family")
        check(UsdShade.Input.IsInput(mat_prim.GetAttribute(name)),
              f"{name} is a UsdShade input")

    graph = UsdShade.NodeGraph.Define(stage, "/Mat/mtlx")
    shade = graph.CreateInput("shadeColor", Sdf.ValueTypeNames.Color3f)
    canonical = mat.GetInput("vrm:mtoon:shadeColorFactor")
    check(UsdShade.ConnectableAPI.CanConnect(shade, canonical),
          "a realization graph input can connect to a canonical input")
    shade.ConnectToSource(canonical)
    # UsdShade resolves an interface connection to an *authored* value only:
    # the schema fallback is not what a connected realization sees, so a
    # writer authors every value a graph connects to.
    check(shade.GetValueProducingAttributes() == [],
          "an unauthored canonical value does not reach a connected graph")
    canonical.GetAttr().Set(Gf.Vec3f(0.2, 0.3, 0.4), Usd.TimeCode(0))
    canonical.GetAttr().Set(Gf.Vec3f(0.4, 0.3, 0.2), Usd.TimeCode(10))
    producing = shade.GetValueProducingAttributes()
    check([a.GetPath() for a in producing] == [canonical.GetAttr().GetPath()],
          "an authored canonical value is the connected graph's value source")
    check(bool(producing) and Gf.IsClose(producing[0].Get(Usd.TimeCode(10)),
                                         Gf.Vec3f(0.4, 0.3, 0.2), 1e-6),
          "an animated canonical value reaches the graph at every time")

    # The alternative q9 rejected: a plain `vrm:` attribute is not a
    # connection source, and the graph silently loses the value.
    plain = mat_prim.CreateAttribute("vrm:mtoon:shadeColorFactor",
                                     Sdf.ValueTypeNames.Color3f)
    plain.Set(Gf.Vec3f(1, 0, 0))
    rim = graph.CreateInput("rimColor", Sdf.ValueTypeNames.Color3f)
    rim.GetAttr().AddConnection(plain.GetPath())
    check(rim.GetValueProducingAttributes() == [],
          "a plain vrm: attribute is not a value source for a graph")


def main() -> int:
    for test in (test_discovery, test_registry_matches_generated_schema,
                 test_apply, test_fixture, test_material_apply_targets,
                 test_mtoon_fields_match_specification,
                 test_canonical_properties_are_connectable_inputs):
        print(f"--- {test.__name__}")
        test()
    if _failures:
        print(f"\n{len(_failures)} check(s) FAILED")
        return 1
    print("\nall checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
