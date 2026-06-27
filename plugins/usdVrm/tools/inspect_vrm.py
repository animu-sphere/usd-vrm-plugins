#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Open a .vrm through the usdVrm plugin and print a structural summary.

Run it inside the plugin's OpenStrata session so the plugin is discovered:

    ost plugin run plugins/usdVrm -- python plugins/usdVrm/tools/inspect_vrm.py <file.vrm>

(Or run with any environment where PXR_PLUGINPATH_NAME already includes the
bundle's plugInfo root and the built library is on PATH/LD_LIBRARY_PATH.)
"""
import sys
from pxr import Usd, UsdGeom, UsdShade, UsdSkel


def main() -> int:
    if len(sys.argv) < 2:
        print("usage: inspect_vrm.py <file.vrm>", file=sys.stderr)
        return 2
    path = sys.argv[1]

    stage = Usd.Stage.Open(path)
    if not stage:
        print(f"failed to open stage: {path}", file=sys.stderr)
        return 1

    dp = stage.GetDefaultPrim()
    vrm = dp.GetCustomData().get("vrm", {})
    print(f"file              : {path}")
    print(f"defaultPrim       : {dp.GetPath()} ({dp.GetTypeName()})")
    print(f"upAxis / metersPU : {UsdGeom.GetStageUpAxis(stage)} / "
          f"{UsdGeom.GetStageMetersPerUnit(stage)}")
    print(f"vrm.sourceVersion : {vrm.get('sourceVersion')} "
          f"(spec {vrm.get('specVersion')})")
    print(f"vrm.meta present  : {'meta' in vrm}")

    meshes = [p for p in Usd.PrimRange(stage.GetPseudoRoot())
              if p.IsA(UsdGeom.Mesh)]
    skinned = sum(1 for m in meshes
                  if UsdSkel.BindingAPI(m).GetSkeletonRel().GetTargets())
    mats = [p for p in Usd.PrimRange(stage.GetPseudoRoot())
            if p.IsA(UsdShade.Material)]
    print(f"meshes            : {len(meshes)} ({skinned} skinned)")
    print(f"materials         : {len(mats)}")

    skel = stage.GetPrimAtPath("/Asset/skel/Skeleton")
    if skel and skel.IsValid():
        joints = UsdSkel.Skeleton(skel).GetJointsAttr().Get() or []
        print(f"skeleton joints   : {len(joints)}")

    hum = stage.GetPrimAtPath("/Asset/rig/Humanoid")
    if hum and hum.IsValid():
        rels = [r for r in hum.GetRelationships()
                if r.GetName().startswith("vrm:humanBones:")]
        print(f"humanoid bones    : {len(rels)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
