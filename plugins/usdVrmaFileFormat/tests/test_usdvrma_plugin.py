#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""End-to-end contract checks for the minimal VRMA importer."""

import os
import pathlib
import sys

from pxr import Gf, Plug, Sdf, Usd, UsdGeom, UsdSkel


FIXTURES = pathlib.Path(__file__).parent / "fixtures"


def main() -> int:
    plugin_path = os.environ.get("PXR_PLUGINPATH_NAME")
    if plugin_path:
        Plug.Registry().RegisterPlugins(plugin_path.split(os.pathsep))
    assert Sdf.FileFormat.FindByExtension("vrma"), "VRMA file format is not registered"

    stage = Usd.Stage.Open(str(FIXTURES / "canonical_walk.vrma"))
    assert stage, "could not open canonical VRMA fixture"
    assert stage.GetDefaultPrim().GetPath() == Sdf.Path("/Animation")
    assert UsdGeom.GetStageUpAxis(stage) == UsdGeom.Tokens.y
    assert abs(UsdGeom.GetStageMetersPerUnit(stage) - 1.0) < 1e-9
    assert abs(stage.GetTimeCodesPerSecond() - 30.0) < 1e-9
    assert abs(stage.GetStartTimeCode() - 0.0) < 1e-9
    assert abs(stage.GetEndTimeCode() - 30.0) < 1e-9

    root = stage.GetPrimAtPath("/Animation")
    custom = root.GetCustomData()
    vrma_metadata = custom.get("vrma", {})
    assert vrma_metadata.get("sourceFormat") == "VRMA", custom
    assert vrma_metadata.get("specVersion") == "1.0", custom
    assert vrma_metadata.get("rootMotionSource") == "hipsTranslation", custom
    # The stored value is the extension payload, not its outer glTF key.
    assert '"humanoid"' in vrma_metadata.get("rawExtension", "")

    skeleton = UsdSkel.Skeleton(stage.GetPrimAtPath("/Animation/HumanoidSkeleton"))
    assert skeleton, "missing semantic skeleton"
    expected_joints = ["hips", "hips/spine", "hips/spine/chest"]
    assert list(skeleton.GetJointsAttr().Get()) == expected_joints
    valid, reason = UsdSkel.Topology(expected_joints).Validate()
    assert valid, reason
    rest = skeleton.GetRestTransformsAttr().Get()
    assert abs(rest[0].ExtractTranslation()[1] - 1.0) < 1e-6
    assert abs(rest[1].ExtractTranslation()[1] - 0.5) < 1e-6

    body = UsdSkel.Animation(stage.GetPrimAtPath("/Animation/BodyAnimation"))
    assert body, "missing body animation"
    assert list(body.GetJointsAttr().Get()) == expected_joints
    rotations = body.GetRotationsAttr()
    assert rotations.GetTimeSamples() == [0.0, 30.0]
    final_hips = rotations.Get(30.0)[0]
    assert abs(final_hips.GetReal() - 0.70710677) < 1e-5
    assert abs(final_hips.GetImaginary()[1] - 0.70710677) < 1e-5
    final_chest = rotations.Get(30.0)[2]
    assert abs(final_chest.GetReal() - 0.70710677) < 1e-5
    assert abs(final_chest.GetImaginary()[0] - 0.70710677) < 1e-5

    translations = body.GetTranslationsAttr()
    assert translations.GetTimeSamples() == [0.0, 30.0]
    end_translation = translations.Get(30.0)
    assert end_translation[0] == Gf.Vec3f(0.0, 1.0, 0.5)
    assert end_translation[1] == Gf.Vec3f(0.0, 0.5, 0.0)

    source = UsdSkel.BindingAPI(skeleton.GetPrim()).GetAnimationSourceRel()
    assert source.GetTargets() == [Sdf.Path("/Animation/BodyAnimation")]
    print("usdVrmaFileFormat smoke tests: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
