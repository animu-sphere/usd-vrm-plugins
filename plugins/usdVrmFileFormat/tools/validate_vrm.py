#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Standalone stage validator for usdVrm imports.

This is the validation contract of the design policy (§10): a checker that runs
*over an already-imported stage*, entirely separate from the importer. It never
reads a ``.vrm`` itself — it opens whatever OpenUSD gives it (a ``.vrm`` through
the file-format plugin, or an exported ``.usd``/``.usda``) and asserts the shape
the importer promises: a ``/Asset`` default prim, skinned-mesh -> skeleton
bindings, parent-before-child joint order, in-range ``JOINTS_0`` indices, and
resolvable material / texture / humanoid / expression / spring-bone targets.

Findings are reported as typed diagnostics from the shared taxonomy
(``vrm_diagnostics``); the exit status is non-zero when any ERROR/FATAL is found.

Run inside an environment where the usdVrm plugin is discoverable, e.g.:

    ost plugin run plugins/usdVrmFileFormat -- \
        python plugins/usdVrmFileFormat/tools/validate_vrm.py avatar.vrm
"""

from __future__ import annotations

import argparse
import json
import os
import pathlib
import sys
from typing import Any

from pxr import Ar, Plug, Sdf, Tf, Usd, UsdGeom, UsdShade, UsdSkel

import vrm_diagnostics as diag
from vrm_diagnostics import Diagnostic, Severity


HUMANOID_PATH = "/Asset/rig/Humanoid"
EXPRESSIONS_PATH = "/Asset/rig/Expressions"
LOOKAT_PATH = "/Asset/rig/LookAt"
SPRINGBONES_PATH = "/Asset/rig/SecondaryMotion/SpringBones"
COLLIDERS_PATH = "/Asset/rig/SecondaryMotion/Colliders"
CONSTRAINTS_PATH = "/Asset/rig/Constraints"
SCHEMA_CONTRACT_VERSION = 1

# The canonical material schemas (material policy §6; schema contract). Where
# they may apply and which texture roles exist are read from the schema
# registry, never copied here: adding a role to schema.usda is an additive v1
# change (§11 q4), and a frozen copy would turn it into a validation error.
MATERIAL_SCHEMAS = ("VrmMaterialAPI", "VrmMToonAPI", "VrmTextureInfoAPI")
# Token-valued canonical properties carry no allowedTokens list in the schema
# (a value outside the set reaches a consumer as data); this is where the set
# is enforced. Texture-role properties are checked per applied instance.
MATERIAL_TOKEN_SETS = {
    "inputs:vrm:material:alphaMode": ("OPAQUE", "MASK", "BLEND"),
    "inputs:vrm:mtoon:outlineWidthMode":
        ("none", "worldCoordinates", "screenCoordinates"),
    # The MToon model the canonical values follow -- 1.0 for a VRM 0.x source
    # too, which is normalized into it (§6.6); never the source's own version.
    "inputs:vrm:mtoon:specVersion": ("1.0",),
}
TEXTURE_WRAP_TOKENS = ("repeat", "clampToEdge", "mirroredRepeat")


def _skeletons(stage: Usd.Stage) -> list[Usd.Prim]:
    return [p for p in stage.Traverse() if p.IsA(UsdSkel.Skeleton)]


def _resolve_bound_skeleton(mesh: Usd.Prim) -> Usd.Prim | None:
    """The Skeleton bound to `mesh`, from its own or an inherited skel:skeleton."""

    prim: Usd.Prim | None = mesh
    while prim and prim.IsValid() and prim.GetPath() != Sdf.Path.absoluteRootPath:
        rel = UsdSkel.BindingAPI(prim).GetSkeletonRel()
        targets = rel.GetTargets() if rel else []
        if targets:
            target = prim.GetStage().GetPrimAtPath(targets[0])
            # A binding whose target exists but is not a Skeleton is *not* a
            # resolved binding — the caller surfaces that as VRM211.
            if target and target.IsValid() and target.IsA(UsdSkel.Skeleton):
                return target
            return None
        prim = prim.GetParent()
    return None


def _is_skinned(mesh: Usd.Prim) -> bool:
    binding = UsdSkel.BindingAPI(mesh)
    if binding.GetJointIndicesPrimvar().HasAuthoredValue():
        return True
    if binding.GetJointWeightsPrimvar().HasAuthoredValue():
        return True
    return _resolve_bound_skeleton(mesh) is not None


def _joint_paths(skel: Usd.Prim) -> list[str]:
    joints = UsdSkel.Skeleton(skel).GetJointsAttr().Get()
    return list(joints) if joints else []


def _has_api(prim: Usd.Prim, api_name: str) -> bool:
    return api_name in prim.GetAppliedSchemas()


def _array_len(attr: Usd.Attribute | None) -> int:
    value = attr.Get() if attr and attr.IsValid() else None
    return len(value) if value else 0


def _stage_relative_asset_exists(stage: Usd.Stage, authored: str) -> bool:
    if not authored or "://" in authored or "[" in authored:
        return False
    if pathlib.PureWindowsPath(authored).is_absolute() or \
            pathlib.PurePosixPath(authored.replace("\\", "/")).is_absolute():
        return pathlib.Path(authored).exists()

    layer = stage.GetRootLayer()
    layer_path = getattr(layer, "realPath", "") or layer.identifier
    if not layer_path or layer.anonymous:
        return False

    relative = pathlib.PurePosixPath(authored.replace("\\", "/"))
    return (pathlib.Path(layer_path).parent / pathlib.Path(*relative.parts)).exists()


def asset_path_resolves(
        stage: Usd.Stage, value: Sdf.AssetPath | None, resolver: Ar.Resolver
) -> bool:
    authored = getattr(value, "path", "") if value else ""
    if not authored:
        return False
    if getattr(value, "resolvedPath", ""):
        return True
    if resolver.Resolve(authored):
        return True
    return _stage_relative_asset_exists(stage, authored)


# --- Individual contract checks ---------------------------------------------
# Each appends zero or more Diagnostics to `out`.


def _check_stage_root(stage: Usd.Stage, out: list[Diagnostic]) -> Usd.Prim | None:
    dp = stage.GetDefaultPrim()
    if not dp or not dp.IsValid():
        out.append(diag.make("VRM200", "stage has no default prim"))
        return None
    if dp.GetPath().pathString != "/Asset":
        out.append(diag.make(
            "VRM201",
            f"default prim is {dp.GetPath().pathString!r}, expected '/Asset'",
            dp.GetPath().pathString))

    kind = Usd.ModelAPI(dp).GetKind()
    if kind != "component":
        out.append(diag.make(
            "VRM202", f"/Asset kind is {kind!r}, expected 'component'",
            dp.GetPath().pathString))

    if UsdGeom.GetStageUpAxis(stage) != UsdGeom.Tokens.y:
        out.append(diag.make(
            "VRM203", f"up-axis is {UsdGeom.GetStageUpAxis(stage)!r}, expected 'Y'"))
    if abs(UsdGeom.GetStageMetersPerUnit(stage) - 1.0) > 1e-9:
        out.append(diag.make(
            "VRM204",
            f"metersPerUnit is {UsdGeom.GetStageMetersPerUnit(stage)}, expected 1.0"))

    has_skel = bool(_skeletons(stage))
    is_skelroot = dp.IsA(UsdSkel.Root)
    if has_skel and not is_skelroot:
        out.append(diag.make(
            "VRM205",
            "stage has a Skeleton but /Asset is not a SkelRoot",
            dp.GetPath().pathString))
    elif not has_skel and is_skelroot:
        out.append(diag.make(
            "VRM205",
            "/Asset is a SkelRoot but the stage has no Skeleton",
            dp.GetPath().pathString))
    return dp


def _check_schema_contract_metadata(dp: Usd.Prim, out: list[Diagnostic]) -> None:
    vrm = dp.GetCustomData().get("vrm", {})
    version = vrm.get("schemaContractVersion")
    if version is None:
        out.append(diag.make(
            "VRM270",
            "no usdVrm schema contract version on /Asset",
            dp.GetPath().pathString))
        return
    if not isinstance(version, int) or version != SCHEMA_CONTRACT_VERSION:
        out.append(diag.make(
            "VRM271",
            f"usdVrm schema contract version is {version!r}, "
            f"expected {SCHEMA_CONTRACT_VERSION}",
            dp.GetPath().pathString))


def _check_skinning(stage: Usd.Stage, out: list[Diagnostic]) -> None:
    for prim in stage.Traverse():
        if not prim.IsA(UsdGeom.Mesh) or not _is_skinned(prim):
            continue
        path = prim.GetPath().pathString
        skel = _resolve_bound_skeleton(prim)
        if skel is None:
            rel = UsdSkel.BindingAPI(prim).GetSkeletonRel()
            if rel and rel.GetTargets():
                out.append(diag.make(
                    "VRM211",
                    f"skel:skeleton target {rel.GetTargets()[0]} is not a Skeleton",
                    path))
            else:
                out.append(diag.make(
                    "VRM210", "skinned mesh has no skel:skeleton binding", path))
            continue

        binding = UsdSkel.BindingAPI(prim)
        indices = binding.GetJointIndicesPrimvar().Get()
        weights = binding.GetJointWeightsPrimvar().Get()
        if not indices or not weights:
            out.append(diag.make(
                "VRM213", "skinned mesh is missing joint indices/weights", path))
            continue

        joint_count = len(_joint_paths(skel))
        bad = [int(i) for i in indices if int(i) < 0 or int(i) >= joint_count]
        if bad:
            out.append(diag.make(
                "VRM212",
                f"joint index {bad[0]} out of range [0,{joint_count}) "
                f"for {skel.GetPath().pathString}",
                path))


def _check_skeleton_topology(stage: Usd.Stage, out: list[Diagnostic]) -> None:
    for skel in _skeletons(stage):
        joints = _joint_paths(skel)
        if not joints:
            continue
        valid, reason = UsdSkel.Topology(joints).Validate()
        if not valid:
            out.append(diag.make(
                "VRM214", f"invalid skeleton topology: {reason}",
                skel.GetPath().pathString))


def _check_materials(stage: Usd.Stage, out: list[Diagnostic]) -> None:
    resolver = Ar.GetResolver()
    for prim in stage.Traverse():
        if prim.IsA(UsdGeom.Mesh):
            rel = UsdShade.MaterialBindingAPI(prim).GetDirectBindingRel()
            targets = rel.GetTargets() if rel else []
            if not targets:
                out.append(diag.make(
                    "VRM220", "mesh has no material binding",
                    prim.GetPath().pathString))
            else:
                target = stage.GetPrimAtPath(targets[0])
                if not target or not target.IsValid():
                    out.append(diag.make(
                        "VRM221", f"material binding target {targets[0]} does not exist",
                        prim.GetPath().pathString))

        # Texture assets: every UsdUVTexture inputs:file must resolve.
        if prim.GetAttribute("info:id") and \
                prim.GetAttribute("info:id").Get() == "UsdUVTexture":
            attr = prim.GetAttribute("inputs:file")
            value = attr.Get() if attr else None
            authored = getattr(value, "path", "") if value else ""
            if authored and not asset_path_resolves(stage, value, resolver):
                out.append(diag.make(
                    "VRM222", f"texture asset {authored!r} does not resolve",
                    prim.GetPath().pathString))


def _check_material_semantics(stage: Usd.Stage, out: list[Diagnostic]) -> None:
    """The canonical material schemas: where they apply, which texture roles
    exist, which token values are meaningful, and that a material claiming
    MToon semantics says so in `vrm:shaderModel` as well (§11 q5: the
    attribute stays, because contract v1 cannot remove it)."""
    resolver = Ar.GetResolver()
    registry = Usd.SchemaRegistry()
    # The rules below are the registered schema's own. A session without
    # vrmSchema cannot judge them, and the registry answers "not allowed" for
    # an unknown schema, so an unregistered family is left alone rather than
    # reported against every material.
    registered = {family for family in MATERIAL_SCHEMAS
                  if registry.FindAppliedAPIPrimDefinition(family)}
    for prim in stage.Traverse():
        applied = [Usd.SchemaRegistry.GetTypeNameAndInstance(s)
                   for s in prim.GetAppliedSchemas()]
        material_apis = [(family, instance) for family, instance in applied
                         if family in MATERIAL_SCHEMAS]
        if not material_apis:
            continue
        path = prim.GetPath().pathString

        misplaced = set()
        for family, instance in material_apis:
            if family not in registered:
                continue
            allowed = Usd.SchemaRegistry.GetAPISchemaCanOnlyApplyToTypeNames(
                family, instance)
            if allowed and not any(
                    prim.IsA(Usd.SchemaRegistry.GetTypeFromSchemaTypeName(name))
                    for name in allowed):
                misplaced.add(family)
        if misplaced:
            out.append(diag.make(
                "VRM223",
                f"{', '.join(sorted(misplaced))} applied to a "
                f"{prim.GetTypeName() or 'typeless'} prim, not a Material",
                path))

        token_attrs = dict(MATERIAL_TOKEN_SETS)
        for family, role in material_apis:
            if family != "VrmTextureInfoAPI":
                continue
            if family in registered and \
                    not Usd.SchemaRegistry.IsAllowedAPISchemaInstanceName(family, role):
                out.append(diag.make(
                    "VRM224", f"VrmTextureInfoAPI:{role} is not a texture role",
                    path))
                continue
            base = f"inputs:vrm:textureInfo:{role}:"
            for axis in ("wrapS", "wrapT"):
                token_attrs[base + axis] = TEXTURE_WRAP_TOKENS
            attr = prim.GetAttribute(base + "file")
            if not attr or not attr.HasAuthoredValue():
                continue
            # Every authored value, not only the default: a time-sampled asset
            # path has no default, and reading one would skip the check.
            values = [attr.Get(Usd.TimeCode(t)) for t in attr.GetTimeSamples()]
            values.append(attr.Get())
            for value in values:
                authored = getattr(value, "path", "") if value else ""
                if authored and not asset_path_resolves(stage, value, resolver):
                    out.append(diag.make(
                        "VRM222",
                        f"texture asset {authored!r} ({role}) does not resolve",
                        path))

        for name, allowed in token_attrs.items():
            attr = prim.GetAttribute(name)
            if not attr or not attr.HasAuthoredValue():
                continue
            value = attr.Get()
            if str(value) not in allowed:
                out.append(diag.make(
                    "VRM225",
                    f"{name} = {value!r} is not one of {', '.join(allowed)}", path))

        if any(family == "VrmMToonAPI" for family, _ in material_apis):
            model = prim.GetAttribute("vrm:shaderModel")
            value = model.Get() if model and model.HasAuthoredValue() else None
            if value != "MToon":
                out.append(diag.make(
                    "VRM226",
                    f"VrmMToonAPI is applied but vrm:shaderModel is {value!r}", path))


def _check_humanoid(stage: Usd.Stage, out: list[Diagnostic]) -> None:
    prim = stage.GetPrimAtPath(HUMANOID_PATH)
    if not prim or not prim.IsValid():
        return
    if not _has_api(prim, "VrmHumanoidAPI"):
        out.append(diag.make(
            "VRM230", "humanoid prim does not apply VrmHumanoidAPI", HUMANOID_PATH))

    skel_rel = prim.GetRelationship("vrm:skeleton")
    targets = skel_rel.GetTargets() if skel_rel else []
    skel = stage.GetPrimAtPath(targets[0]) if targets else None
    if not skel or not skel.IsValid() or not skel.IsA(UsdSkel.Skeleton):
        out.append(diag.make(
            "VRM231", "humanoid vrm:skeleton does not resolve to a Skeleton",
            HUMANOID_PATH))
        return

    joints = set(_joint_paths(skel))
    for attr in prim.GetAttributes():
        name = attr.GetName()
        if not name.startswith("vrm:humanBones:") or not attr.HasAuthoredValue():
            continue
        value = attr.Get()
        if value and str(value) not in joints:
            out.append(diag.make(
                "VRM232",
                f"{name} -> {value!r} is not a joint on {skel.GetPath().pathString}",
                HUMANOID_PATH))


def _check_expressions(stage: Usd.Stage, out: list[Diagnostic]) -> None:
    scope = stage.GetPrimAtPath(EXPRESSIONS_PATH)
    if not scope or not scope.IsValid():
        return
    for expr in scope.GetChildren():
        path = expr.GetPath().pathString
        if not _has_api(expr, "VrmExpressionAPI"):
            out.append(diag.make(
                "VRM242", "expression prim does not apply VrmExpressionAPI", path))

        morph_rel = expr.GetRelationship("vrm:morphTargets")
        morph_targets = morph_rel.GetTargets() if morph_rel else []
        for target in morph_targets:
            tp = stage.GetPrimAtPath(target)
            if not tp or not tp.IsValid() or not tp.IsA(UsdSkel.BlendShape):
                out.append(diag.make(
                    "VRM240", f"morph target {target} is not a BlendShape", path))
        if morph_targets and \
                _array_len(expr.GetAttribute("vrm:morphTargetWeights")) != \
                len(morph_targets):
            out.append(diag.make(
                "VRM243",
                "expression morph target weights are not parallel to vrm:morphTargets",
                path))

        color_rel = expr.GetRelationship("vrm:materialColorTargets")
        color_targets = color_rel.GetTargets() if color_rel else []
        for target in color_targets:
            tp = stage.GetPrimAtPath(target)
            if not tp or not tp.IsValid():
                out.append(diag.make(
                    "VRM241", f"material-color target {target} does not exist", path))
        if color_targets:
            type_count = _array_len(expr.GetAttribute("vrm:materialColorTypes"))
            value_count = _array_len(expr.GetAttribute("vrm:materialColorValues"))
            indices_attr = expr.GetAttribute("vrm:materialColorTargetIndices")
            indices = indices_attr.Get() if indices_attr and                 indices_attr.HasAuthoredValue() else None
            if indices is not None:
                # One entry per bind, each naming a target: a relationship
                # holds a material once however many of its slots are bound.
                if type_count != len(indices) or value_count != len(indices):
                    out.append(diag.make(
                        "VRM244",
                        "expression material-color arrays are not parallel to "
                        "vrm:materialColorTargetIndices",
                        path))
                if any(i < 0 or i >= len(color_targets) for i in indices):
                    out.append(diag.make(
                        "VRM244",
                        "vrm:materialColorTargetIndices names a target "
                        "vrm:materialColorTargets does not have",
                        path))
            elif type_count != len(color_targets) or value_count != len(color_targets):
                # A stage from before the index array pairs by position.
                out.append(diag.make(
                    "VRM244",
                    "expression material-color arrays are not parallel to "
                    "vrm:materialColorTargets",
                    path))


def _check_lookat(stage: Usd.Stage, out: list[Diagnostic]) -> None:
    prim = stage.GetPrimAtPath(LOOKAT_PATH)
    if not prim or not prim.IsValid():
        return
    if not _has_api(prim, "VrmLookAtAPI"):
        out.append(diag.make(
            "VRM245", "lookAt prim does not apply VrmLookAtAPI", LOOKAT_PATH))

    skel_rel = prim.GetRelationship("vrm:skeleton")
    targets = skel_rel.GetTargets() if skel_rel else []
    if not targets:
        return
    skel = stage.GetPrimAtPath(targets[0])
    if not skel or not skel.IsValid() or not skel.IsA(UsdSkel.Skeleton):
        out.append(diag.make(
            "VRM246", "lookAt vrm:skeleton does not resolve to a Skeleton",
            LOOKAT_PATH))
        return

    joints = set(_joint_paths(skel))
    for attr_name in ("vrm:leftEye", "vrm:rightEye"):
        attr = prim.GetAttribute(attr_name)
        value = attr.Get() if attr and attr.IsValid() and attr.HasAuthoredValue() else ""
        if value and str(value) not in joints:
            out.append(diag.make(
                "VRM247",
                f"{attr_name} -> {value!r} is not a joint on "
                f"{skel.GetPath().pathString}",
                LOOKAT_PATH))


def _check_springbones(stage: Usd.Stage, out: list[Diagnostic]) -> None:
    scope = stage.GetPrimAtPath(SPRINGBONES_PATH)
    if not scope or not scope.IsValid():
        return
    skels = _skeletons(stage)
    joints = set(_joint_paths(skels[0])) if skels else set()
    for spring in scope.GetChildren():
        path = spring.GetPath().pathString
        if not _has_api(spring, "VrmSpringBoneAPI"):
            out.append(diag.make(
                "VRM252", "spring-bone prim does not apply VrmSpringBoneAPI", path))

        joints_attr = spring.GetAttribute("vrm:joints")
        joint_values = joints_attr.Get() if joints_attr and joints_attr.IsValid() else []
        joint_values = joint_values or []
        for value in joint_values:
            v = str(value)
            if not joints or v in joints:
                continue
            # A spring chain's terminal "end" node is a real VRM construct that
            # is not a skinned joint; the importer authors it by bare source-node
            # name (jointTok fallback). Only a *hierarchical* path that fails to
            # resolve is genuine breakage — a bare leaf name is the end node.
            if "/" in v:
                out.append(diag.make(
                    "VRM250", f"spring joint path {v!r} is not on the skeleton", path))

        joint_count = len(joint_values)
        for attr_name in ("vrm:stiffness", "vrm:gravityPower", "vrm:dragForce",
                          "vrm:hitRadius", "vrm:gravityDir"):
            if joint_count and _array_len(spring.GetAttribute(attr_name)) != joint_count:
                out.append(diag.make(
                    "VRM253",
                    f"{attr_name} is not parallel to vrm:joints",
                    path))

        cg_rel = spring.GetRelationship("vrm:colliderGroups")
        for target in (cg_rel.GetTargets() if cg_rel else []):
            tp = stage.GetPrimAtPath(target)
            if not tp or not tp.IsValid():
                out.append(diag.make(
                    "VRM251", f"collider group {target} does not exist", path))


def _check_colliders(stage: Usd.Stage, out: list[Diagnostic]) -> None:
    collider_scope = stage.GetPrimAtPath(COLLIDERS_PATH)
    if not collider_scope or not collider_scope.IsValid():
        return
    for group in collider_scope.GetChildren():
        for collider in group.GetChildren():
            path = collider.GetPath().pathString
            if not _has_api(collider, "VrmColliderAPI"):
                out.append(diag.make(
                    "VRM254", "collider prim does not apply VrmColliderAPI", path))
            shape = collider.GetAttribute("vrm:shape")
            shape_value = shape.Get() if shape and shape.IsValid() else ""
            if shape_value and str(shape_value) not in ("sphere", "capsule"):
                out.append(diag.make(
                    "VRM255",
                    f"collider shape {shape_value!r} is not 'sphere' or 'capsule'",
                    path))


def _check_constraints(stage: Usd.Stage, out: list[Diagnostic]) -> None:
    scope = stage.GetPrimAtPath(CONSTRAINTS_PATH)
    if not scope or not scope.IsValid():
        return
    skels = _skeletons(stage)
    joints = set(_joint_paths(skels[0])) if skels else set()
    for constraint in scope.GetChildren():
        path = constraint.GetPath().pathString
        if not _has_api(constraint, "VrmConstraintAPI"):
            out.append(diag.make(
                "VRM262", "constraint prim does not apply VrmConstraintAPI", path))

        ctype_attr = constraint.GetAttribute("vrm:type")
        ctype = ctype_attr.Get() if ctype_attr and ctype_attr.IsValid() else ""
        if ctype and str(ctype) not in ("roll", "aim", "rotation"):
            out.append(diag.make(
                "VRM263",
                f"constraint type {ctype!r} is not 'roll', 'aim', or 'rotation'",
                path))

        for attr_name in ("vrm:constrained", "vrm:source"):
            attr = constraint.GetAttribute(attr_name)
            value = attr.Get() if attr and attr.IsValid() and attr.HasAuthoredValue() else ""
            if value and "/" in str(value) and joints and str(value) not in joints:
                out.append(diag.make(
                    "VRM264",
                    f"{attr_name} -> {value!r} is not a joint on the skeleton",
                    path))


def _check_raw_preservation(dp: Usd.Prim, out: list[Diagnostic]) -> None:
    vrm = dp.GetCustomData().get("vrm", {})
    if not vrm.get("rawExtension") and not vrm.get("meta"):
        out.append(diag.make(
            "VRM260", "no lossless raw VRM extension block on /Asset",
            dp.GetPath().pathString))


# --- Imaging (opt-in) ---------------------------------------------------------
# Whether the canonical material records reach a Hydra renderer at all
# (VRM_IMAGING_POLICY.md §18, §19). That is a property of the session, not of
# the stage, so it is checked only when asked: a headless deployment may leave
# vrmImaging out on purpose, and the stage is no less valid for it.

IMAGING_ADAPTER_BASE = "UsdImagingAPISchemaAdapter"


def _getenv_bool(name: str, default: bool) -> bool:
    """TfGetenvBool's reading of an environment variable."""
    value = os.environ.get(name, "")
    if not value:
        return default
    return value.lower() in ("true", "yes", "on", "1")


def imaging_adapter_schemas(external_plugins: bool | None = None) -> dict[str, str]:
    """API schema name -> adapter type, as UsdImaging's adapter registry would
    construct them in this session.

    The discovery the registry makes, from plugin metadata alone: every type a
    plugin declares as derived from `UsdImagingAPISchemaAdapter`, keyed by its
    `apiSchemaName`. With `USDIMAGING_ENABLE_PLUGINS` off the registry keeps
    only types marked `isInternal`, which drops every adapter this workspace
    ships without a word (policy §27). Nothing is loaded.
    """
    if external_plugins is None:
        external_plugins = _getenv_bool("USDIMAGING_ENABLE_PLUGINS", True)
    # Through the plugin registry, which declares the plugins' types first:
    # Tf.Type.FindByName answers "unknown" for a type only a plugInfo.json
    # names until something has asked the registry.
    base = Plug.Registry.FindTypeByName(IMAGING_ADAPTER_BASE)
    registry = Plug.Registry()
    if base.isUnknown:
        return {}
    found: dict[str, str] = {}
    for adapter in registry.GetAllDerivedTypes(base):
        plugin = registry.GetPluginForType(adapter)
        if not plugin:
            continue
        metadata = plugin.GetMetadataForType(adapter)
        if not external_plugins and metadata.get("isInternal") is not True:
            continue
        name = metadata.get("apiSchemaName")
        if isinstance(name, str) and name:
            found[name] = adapter.typeName
    return found


def _authored_material_families(stage: Usd.Stage) -> dict[str, int]:
    """Canonical material schema -> the number of prims applying it.

    Read from the composed `apiSchemas` list as authored, not from
    GetAppliedSchemas(): the registry-backed list leaves out a schema it has no
    definition for, and a session without vrmSchema is one this check is for.
    """
    counts: dict[str, int] = {}
    for prim in stage.Traverse():
        list_op = prim.GetMetadata("apiSchemas")
        applied = list_op.ApplyOperations([]) if list_op else []
        families = {Usd.SchemaRegistry.GetTypeNameAndInstance(s)[0]
                    for s in applied}
        for family in families & set(MATERIAL_SCHEMAS):
            counts[family] = counts.get(family, 0) + 1
    return counts


def _check_imaging(stage: Usd.Stage, out: list[Diagnostic],
                   adapters: dict[str, str] | None = None,
                   external_plugins: bool | None = None) -> None:
    """VRM300: the stage carries canonical material semantics and this session
    cannot hand them to Hydra, so a renderer sees the `/preview` and `/mtlx`
    realizations only. Once per stage, naming each schema left out and why."""
    families = _authored_material_families(stage)
    if not families:
        return
    if external_plugins is None:
        external_plugins = _getenv_bool("USDIMAGING_ENABLE_PLUGINS", True)
    if adapters is None:
        adapters = imaging_adapter_schemas(external_plugins)
    registry = Usd.SchemaRegistry()
    unregistered = sorted(f for f in families
                          if not registry.FindAppliedAPIPrimDefinition(f))
    unadapted = sorted(f for f in families
                       if f not in unregistered and f not in adapters)
    if not unregistered and not unadapted:
        return
    reasons = []
    if unregistered:
        reasons.append(
            f"{', '.join(unregistered)} is not a registered schema (vrmSchema "
            f"is not in the session), so UsdImaging asks no adapter about it")
    if unadapted:
        why = ("USDIMAGING_ENABLE_PLUGINS is off, which drops every external "
               "adapter" if not external_plugins
               else "vrmImaging is not in the session")
        reasons.append(f"no UsdImaging adapter handles {', '.join(unadapted)} "
                       f"({why})")
    applied = ", ".join(f"{f} on {n}" for f, n in sorted(families.items()))
    dp = stage.GetDefaultPrim()
    out.append(diag.make(
        "VRM300",
        f"canonical material semantics ({applied}) will not reach Hydra: "
        f"{'; '.join(reasons)}. A Hydra renderer sees /preview and /mtlx only",
        dp.GetPath().pathString if dp else ""))


def validate_stage(stage: Usd.Stage, check_imaging: bool = False) -> list[Diagnostic]:
    """Run the full validation contract; return the diagnostics found.

    `check_imaging` adds VRM300, a check of the session rather than the stage:
    whether a Hydra consumer would see the canonical material records. Off by
    default.
    """

    out: list[Diagnostic] = []
    dp = _check_stage_root(stage, out)
    if dp is None:
        return out  # no default prim: nothing else is well-defined.
    _check_schema_contract_metadata(dp, out)
    _check_skinning(stage, out)
    _check_skeleton_topology(stage, out)
    _check_materials(stage, out)
    _check_material_semantics(stage, out)
    _check_humanoid(stage, out)
    _check_expressions(stage, out)
    _check_lookat(stage, out)
    _check_springbones(stage, out)
    _check_colliders(stage, out)
    _check_constraints(stage, out)
    _check_raw_preservation(dp, out)
    if check_imaging:
        _check_imaging(stage, out)
    return out


def validate_path(path: str, check_imaging: bool = False
                  ) -> tuple[list[Diagnostic], bool]:
    """Open `path` as a stage and validate it. Returns (diagnostics, opened)."""

    try:
        stage = Usd.Stage.Open(str(path))
    except Tf.ErrorException:
        stage = None
    if not stage:
        return ([diag.make("VRM200", f"failed to open stage: {path}")], False)
    return validate_stage(stage, check_imaging), True


def build_result(path: str, diagnostics: list[Diagnostic]) -> dict[str, Any]:
    counts = diag.severity_counts(diagnostics)
    ok = counts[Severity.ERROR.label] == 0 and counts[Severity.FATAL.label] == 0
    return {
        "schemaVersion": 1,
        "source": str(path),
        "valid": ok,
        "counts": counts,
        "diagnostics": [d.to_dict() for d in diagnostics],
    }


def render_text(result: dict[str, Any]) -> str:
    lines = [f"usdVrm validate: {result['source']}"]
    for d in result["diagnostics"]:
        loc = f" @ {d['primPath']}" if d.get("primPath") else ""
        code = d.get("code") or "----"
        lines.append(f"  [{d['severity']:<7}] {code} {d['message']}{loc}")
    c = result["counts"]
    lines.append(
        f"  -> {'VALID' if result['valid'] else 'INVALID'}: "
        f"{c['FATAL']} fatal, {c['ERROR']} error, "
        f"{c['WARNING']} warning, {c['INFO']} info")
    return "\n".join(lines)


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Validate an imported VRM stage against the usdVrm contract.")
    parser.add_argument("input", help="Stage to validate (.vrm/.usd/.usda)")
    parser.add_argument(
        "--json", action="store_true", help="Emit the machine-readable JSON report")
    parser.add_argument(
        "--check-imaging", action="store_true",
        help="Also report VRM300 when this session would not hand the "
             "canonical material schemas to Hydra (no vrmImaging, no "
             "vrmSchema, or USDIMAGING_ENABLE_PLUGINS off)")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = _build_parser().parse_args(argv)
    diagnostics, opened = validate_path(args.input, args.check_imaging)
    result = build_result(args.input, diagnostics)
    if args.json:
        print(json.dumps(result, indent=2, sort_keys=True))
    else:
        print(render_text(result))
    if not opened:
        return 2
    return 0 if result["valid"] else 1


if __name__ == "__main__":
    sys.exit(main())
