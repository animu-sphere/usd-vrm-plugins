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

from pxr import Plug, Sdf, Usd, UsdGeom, UsdShade, UsdSkel

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

    # Skeleton: hips -> spine.
    skel = UsdSkel.Skeleton(stage.GetPrimAtPath("/Asset/skel/Skeleton"))
    assert skel, "expected /Asset/skel/Skeleton"
    joints = list(skel.GetJointsAttr().Get())
    assert joints == ["hips", "hips/spine"], joints

    # Humanoid mapping as relationships to the skeleton joint paths.
    humanoid = stage.GetPrimAtPath("/Asset/rig/Humanoid")
    assert humanoid and humanoid.IsValid(), "expected /Asset/rig/Humanoid"
    hips = humanoid.GetRelationship("vrm:humanBones:hips")
    assert hips and hips.GetTargets() == [Sdf.Path("/Asset/skel/Skeleton/hips")], \
        hips.GetTargets() if hips else None

    print("usdVrm smoke test: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
