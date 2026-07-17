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
import pathlib
import sys
from typing import Any

from pxr import Ar, Sdf, Tf, Usd, UsdGeom, UsdShade, UsdSkel

import vrm_diagnostics as diag
from vrm_diagnostics import Diagnostic, Severity


HUMANOID_PATH = "/Asset/rig/Humanoid"
EXPRESSIONS_PATH = "/Asset/rig/Expressions"
LOOKAT_PATH = "/Asset/rig/LookAt"
SPRINGBONES_PATH = "/Asset/rig/SecondaryMotion/SpringBones"
COLLIDERS_PATH = "/Asset/rig/SecondaryMotion/Colliders"
CONSTRAINTS_PATH = "/Asset/rig/Constraints"
SCHEMA_CONTRACT_VERSION = 1


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
            if type_count != len(color_targets) or value_count != len(color_targets):
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


def validate_stage(stage: Usd.Stage) -> list[Diagnostic]:
    """Run the full validation contract; return the diagnostics found."""

    out: list[Diagnostic] = []
    dp = _check_stage_root(stage, out)
    if dp is None:
        return out  # no default prim: nothing else is well-defined.
    _check_schema_contract_metadata(dp, out)
    _check_skinning(stage, out)
    _check_skeleton_topology(stage, out)
    _check_materials(stage, out)
    _check_humanoid(stage, out)
    _check_expressions(stage, out)
    _check_lookat(stage, out)
    _check_springbones(stage, out)
    _check_colliders(stage, out)
    _check_constraints(stage, out)
    _check_raw_preservation(dp, out)
    return out


def validate_path(path: str) -> tuple[list[Diagnostic], bool]:
    """Open `path` as a stage and validate it. Returns (diagnostics, opened)."""

    try:
        stage = Usd.Stage.Open(str(path))
    except Tf.ErrorException:
        stage = None
    if not stage:
        return ([diag.make("VRM200", f"failed to open stage: {path}")], False)
    return validate_stage(stage), True


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
    return parser


def main(argv: list[str] | None = None) -> int:
    args = _build_parser().parse_args(argv)
    diagnostics, opened = validate_path(args.input)
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
