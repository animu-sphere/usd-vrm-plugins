# SPDX-License-Identifier: Apache-2.0
"""End-to-end smoke test for the usdVrm file-format plugin.

Drives the freshly built plugin through OpenUSD's Python bindings: opens the
committed minimal .vrm fixture as a stage and asserts the Phase 1 contract
(/Asset hierarchy, skinned mesh, skeleton, humanoid mapping, VRM provenance).

The build wires PXR_PLUGINPATH_NAME / PATH / PYTHONPATH (see CMakeLists.txt);
when run by hand, do it inside `ost plugin run plugins/usdVrm -- python ...`.
"""
import os
import pathlib
import sys

from pxr import Gf, Plug, Sdf, Usd, UsdGeom, UsdShade, UsdSkel

FIXTURE = pathlib.Path(__file__).parent / "fixtures" / "minimal.vrm"


def main() -> int:
    plugin_path = os.environ.get("PXR_PLUGINPATH_NAME")
    if plugin_path:
        Plug.Registry().RegisterPlugins(plugin_path.split(os.pathsep))

    assert Sdf.FileFormat.FindByExtension("vrm"), \
        "usdVrm SdfFileFormat is not registered"

    stage = Usd.Stage.Open(str(FIXTURE))
    assert stage, "failed to open minimal.vrm"

    # /Asset is the default prim, a SkelRoot (skeleton present), component kind.
    dp = stage.GetDefaultPrim()
    assert dp and dp.GetPath().pathString == "/Asset", dp
    assert dp.GetTypeName() == "SkelRoot", dp.GetTypeName()

    assert abs(UsdGeom.GetStageMetersPerUnit(stage) - 1.0) < 1e-9
    assert UsdGeom.GetStageUpAxis(stage) == UsdGeom.Tokens.y

    vrm = dp.GetCustomData().get("vrm", {})
    assert vrm.get("sourceFormat") == "VRM", vrm
    assert vrm.get("specVersion") == "1.0", vrm
    assert vrm.get("meta"), "VRM meta should be preserved on /Asset"
    # Lossless preservation: the full VRM extension block is kept verbatim.
    assert vrm.get("rawExtension"), "VRM rawExtension should be preserved"
    assert "humanoid" in vrm["rawExtension"], "rawExtension should be the VRM block"

    # Geometry: one skinned mesh bound to the skeleton, with a material.
    body = stage.GetPrimAtPath("/Asset/geo/Body")
    assert body and body.IsA(UsdGeom.Mesh), "expected /Asset/geo/Body mesh"
    binding = UsdSkel.BindingAPI(body)
    targets = binding.GetSkeletonRel().GetTargets()
    assert targets == [Sdf.Path("/Asset/skel/Skeleton")], targets
    assert binding.GetJointWeightsPrimvar().Get(), "missing joint weights"

    mat_rel = UsdShade.MaterialBindingAPI(body).GetDirectBindingRel()
    assert mat_rel.GetTargets() == [Sdf.Path("/Asset/mtl/Body_Mat")], \
        mat_rel.GetTargets()

    # P0.2: the non-skinned Accessory keeps its glTF node placement (x = 1.0).
    acc = stage.GetPrimAtPath("/Asset/geo/Accessory")
    assert acc and acc.IsA(UsdGeom.Mesh), "expected /Asset/geo/Accessory mesh"
    assert not UsdSkel.BindingAPI(acc).GetSkeletonRel().GetTargets(), \
        "Accessory must not be skinned"
    xf = acc.GetAttribute("xformOp:transform").Get()
    assert xf is not None, "Accessory should have an authored xformOp:transform"
    assert abs(xf.ExtractTranslation()[0] - 1.0) < 1e-6, \
        f"Accessory node transform lost: {xf.ExtractTranslation()}"

    # Skeleton: hips -> spine.
    skel = UsdSkel.Skeleton(stage.GetPrimAtPath("/Asset/skel/Skeleton"))
    assert skel, "expected /Asset/skel/Skeleton"
    joints = list(skel.GetJointsAttr().Get())
    assert joints == ["hips", "hips/spine"], joints
    # P0.1: bind transforms come from the (identity) inverse bind matrices, not
    # the joints' non-identity node world transforms. Proves IBMs are honored.
    binds = skel.GetBindTransformsAttr().Get()
    assert binds and all(b == Gf.Matrix4d(1.0) for b in binds), \
        f"bindTransforms should equal inverse(identity IBM): {list(binds)}"
    rests = skel.GetRestTransformsAttr().Get()
    assert abs(rests[0].ExtractTranslation()[1] - 0.5) < 1e-6, \
        f"hips rest transform should keep node-local TRS: {rests[0]}"

    # Humanoid: a resolvable rel to the Skeleton prim + per-bone joint tokens.
    humanoid = stage.GetPrimAtPath("/Asset/rig/Humanoid")
    assert humanoid and humanoid.IsValid(), "expected /Asset/rig/Humanoid"
    skel_rel = humanoid.GetRelationship("vrm:skeleton")
    assert skel_rel.GetTargets() == [Sdf.Path("/Asset/skel/Skeleton")], \
        skel_rel.GetTargets()
    hips_attr = humanoid.GetAttribute("vrm:humanBones:hips")
    assert hips_attr and hips_attr.Get() == "hips", \
        hips_attr.Get() if hips_attr else None
    assert humanoid.GetAttribute("vrm:humanBones:spine").Get() == "hips/spine"

    print("usdVrm smoke test: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
