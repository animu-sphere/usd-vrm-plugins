#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""End-to-end contract checks for the minimal VRMA importer."""

import os
import pathlib
import sys

from pxr import Gf, Plug, Sdf, Usd, UsdGeom, UsdSkel


FIXTURES = pathlib.Path(__file__).parent / "fixtures"


def _check_expression_clip() -> None:
    """The expression half: named weights over time, and what is *not* said.

    VRMA drives an expression weight with the X component of its node's
    translation. What the reader has to get right around that rule is the
    interesting part, so each assertion below stands for one case the fixture
    was built to separate.
    """
    stage = Usd.Stage.Open(str(FIXTURES / "expressive_face.vrma"))
    assert stage, "could not open the expression VRMA fixture"

    expressions = stage.GetPrimAtPath("/Animation/Expressions")
    assert expressions, "no /Animation/Expressions scope"
    names = sorted(child.GetName() for child in expressions.GetChildren())
    # `angry` points at a node index the file does not have, so it is dropped.
    assert names == ["happy", "myWink", "relaxed", "surprised"], names

    happy = stage.GetPrimAtPath("/Animation/Expressions/happy")
    assert happy.GetAttribute("vrm:expressionName").Get() == "happy"
    assert happy.GetAttribute("vrm:expressionType").Get() == "preset"
    weight = happy.GetAttribute("vrm:expressionWeight")
    # The custom expression keys on 0.5s and 1.0s and the body keys on 0s and
    # 1.0s: every channel is evaluated at the union, so 15 is a real sample and
    # not an interpolation a consumer has to perform.
    assert weight.GetTimeSamples() == [0.0, 15.0, 30.0], weight.GetTimeSamples()
    assert abs(weight.Get(0.0) - 0.0) < 1e-6
    assert abs(weight.Get(15.0) - 0.5) < 1e-6
    assert abs(weight.Get(30.0) - 1.0) < 1e-6

    wink = stage.GetPrimAtPath("/Animation/Expressions/myWink")
    assert wink.GetAttribute("vrm:expressionType").Get() == "custom"
    wink_weight = wink.GetAttribute("vrm:expressionWeight")
    assert wink_weight.GetTimeSamples() == [0.0, 15.0, 30.0]
    # Before its first key the channel holds, exactly as a body channel does.
    assert abs(wink_weight.Get(0.0) - 0.25) < 1e-6
    assert abs(wink_weight.Get(15.0) - 0.25) < 1e-6
    # The specification clamps a weight to [0, 1]; the importer carries what the
    # file said and leaves the clamp to whoever applies it to a rig.
    assert abs(wink_weight.Get(30.0) - 1.5) < 1e-6, wink_weight.Get(30.0)

    # Two expressions the clip never animates, saying two different things.
    relaxed = stage.GetPrimAtPath("/Animation/Expressions/relaxed")
    relaxed_weight = relaxed.GetAttribute("vrm:expressionWeight")
    # Its node states translation = [0.3, 0, 0] and nothing animates it. glTF
    # leaves an un-animated node at its own TRS, so the clip did give a weight:
    # one value for the whole clip, authored as a default rather than as a run
    # of identical time samples.
    assert relaxed_weight.IsValid(), (
        "a weight the node states is a weight the file gave, not an absence")
    assert relaxed_weight.GetTimeSamples() == [], relaxed_weight.GetTimeSamples()
    assert abs(relaxed_weight.Get() - 0.3) < 1e-6, relaxed_weight.Get()

    surprised = stage.GetPrimAtPath("/Animation/Expressions/surprised")
    assert surprised.GetAttribute("vrm:expressionType").Get() == "preset"
    # Declared, and the file gave no weight anywhere: no channel, and no
    # transform on the node to read one out of. An unreported weight is not a
    # weight of zero, so there is no value here to mistake for one.
    assert not surprised.GetAttribute("vrm:expressionWeight").IsValid(), (
        "an expression the clip never states must not be authored as a weight")

    # Expressions must not have been expanded into a blend-shape binding: which
    # morph targets an expression drives is the avatar's property, and this clip
    # binds to no avatar (motion policy 4.3).
    body = UsdSkel.Animation(stage.GetPrimAtPath("/Animation/BodyAnimation"))
    assert body, "missing body animation"
    assert not body.GetBlendShapesAttr().HasAuthoredValue()
    assert not body.GetBlendShapeWeightsAttr().HasAuthoredValue()

    # The body is sampled at the union too: 15 exists because an expression
    # keyed there, and the rotation on it is the interpolated body pose.
    rotations = body.GetRotationsAttr()
    assert rotations.GetTimeSamples() == [0.0, 15.0, 30.0], rotations.GetTimeSamples()
    midway = rotations.Get(15.0)[0]
    assert abs(midway.GetReal() - 0.9238795) < 1e-5, midway
    assert abs(midway.GetImaginary()[1] - 0.3826834) < 1e-5, midway


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

    scales = body.GetScalesAttr()
    assert scales.HasAuthoredValue(), "BodyAnimation authors no scales"
    assert list(scales.Get()) == [Gf.Vec3h(1.0)] * len(expected_joints)

    source = UsdSkel.BindingAPI(skeleton.GetPrim()).GetAnimationSourceRel()
    assert source.GetTargets() == [Sdf.Path("/Animation/BodyAnimation")]

    # Drive the consumer's own query rather than the attributes checked above.
    # UsdSkel fetches translations, rotations and scales as a unit and `scales`
    # has no schema fallback, so a clip missing it binds cleanly, satisfies
    # every value comparison above, and then resolves to the skeleton's rest
    # pose. Only comparing resolved transforms against the animation
    # distinguishes the two -- an existence check would pass either way.
    cache = UsdSkel.Cache()
    query = cache.GetSkelQuery(skeleton)
    assert query, "stage yields no UsdSkel skeleton query"
    transforms = query.ComputeJointLocalTransforms(Usd.TimeCode(30.0))
    assert transforms and len(transforms) == len(expected_joints), (
        "UsdSkel resolved no joint transforms: the SkelAnimation is bound "
        "but does not drive the skeleton")
    hips = transforms[0]
    assert Gf.IsClose(hips.ExtractTranslation(), Gf.Vec3d(0.0, 1.0, 0.5), 1e-5), (
        f"UsdSkel resolved hips at {hips.ExtractTranslation()}, expected "
        "(0, 1, 0.5) -- the skeleton is stuck at its rest pose")
    hips_rotation = hips.ExtractRotationQuat()
    assert abs(hips_rotation.GetReal() - 0.70710677) < 1e-5, hips_rotation
    assert abs(hips_rotation.GetImaginary()[1] - 0.70710677) < 1e-5, hips_rotation

    _check_expression_clip()

    print("usdVrmaFileFormat smoke tests: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
