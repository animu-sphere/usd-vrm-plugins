#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""End-to-end check for the Motion Phase C bake tool.

This is the milestone's evaluation point: the hand-authored design triplet in
docs/design/fixtures/motion/ says what retargeting `canonical_walk.usda` onto
`avatar.usda` must produce, and `expected_retargeted.usda` is that answer. The
comparison is value-level and goes through USD composition on both sides, so it
checks what a consumer actually resolves rather than how the layer is spelled.
"""

from __future__ import annotations

import argparse
import pathlib
import shutil
import subprocess
import sys
import tempfile

from pxr import Gf, Sdf, Usd, UsdGeom, UsdSkel

TOLERANCE = 1e-5


class Failures:
    def __init__(self) -> None:
        self.messages: list[str] = []

    def check(self, condition: bool, message: str) -> bool:
        if not condition:
            self.messages.append(message)
        return condition

    def report(self) -> int:
        if not self.messages:
            return 0
        for message in self.messages:
            print(f"FAIL: {message}", file=sys.stderr)
        return 1


def run_tool(tool: str, *arguments: str) -> subprocess.CompletedProcess:
    return subprocess.run(
        [tool, *arguments], text=True, encoding="utf-8", errors="replace",
        stdout=subprocess.PIPE, stderr=subprocess.PIPE)


def find_animation(stage: Usd.Stage) -> UsdSkel.Animation:
    for prim in stage.Traverse():
        if prim.IsA(UsdSkel.Animation):
            return UsdSkel.Animation(prim)
    raise AssertionError(f"{stage.GetRootLayer().identifier} has no SkelAnimation")


def find_skeleton(stage: Usd.Stage) -> UsdSkel.Skeleton:
    for prim in stage.Traverse():
        if prim.IsA(UsdSkel.Skeleton):
            return UsdSkel.Skeleton(prim)
    raise AssertionError(f"{stage.GetRootLayer().identifier} has no Skeleton")


def quaternions_match(a: Gf.Quatf, b: Gf.Quatf) -> bool:
    """q and -q are the same rotation, so compare orientations, not values."""
    dot = (a.GetReal() * b.GetReal()
           + Gf.Dot(a.GetImaginary(), b.GetImaginary()))
    if dot < 0.0:
        b = Gf.Quatf(-b.GetReal(), -b.GetImaginary())
    return (abs(a.GetReal() - b.GetReal()) <= TOLERANCE
            and all(abs(x - y) <= TOLERANCE
                    for x, y in zip(a.GetImaginary(), b.GetImaginary())))


def vectors_match(a, b) -> bool:
    return all(abs(x - y) <= TOLERANCE for x, y in zip(a, b))


def compare_with_expected(output: pathlib.Path, expected: pathlib.Path,
                          failures: Failures) -> None:
    produced_stage = Usd.Stage.Open(str(output))
    expected_stage = Usd.Stage.Open(str(expected))
    if not failures.check(produced_stage is not None,
                          f"could not open produced stage {output}"):
        return

    produced = find_animation(produced_stage)
    reference = find_animation(expected_stage)

    produced_joints = list(produced.GetJointsAttr().Get())
    expected_joints = list(reference.GetJointsAttr().Get())
    if not failures.check(
            produced_joints == expected_joints,
            f"joint order differs: {produced_joints} != {expected_joints}"):
        return

    # The skeleton must actually be driven by what we authored; an animation
    # nobody is bound to would pass every value check and animate nothing.
    skeleton = find_skeleton(produced_stage)
    targets = UsdSkel.BindingAPI(skeleton.GetPrim()).GetAnimationSourceRel() \
        .GetTargets()
    failures.check(
        targets == [produced.GetPrim().GetPath()],
        f"skel:animationSource is {targets}, expected "
        f"[{produced.GetPrim().GetPath()}]")

    # The referenced avatar must still compose: its rig, not a copy of it.
    failures.check(
        list(skeleton.GetJointsAttr().Get()) == expected_joints,
        "the referenced avatar's skeleton did not compose into the output")

    for attribute_name in ("Rotations", "Translations"):
        produced_attribute = getattr(produced, f"Get{attribute_name}Attr")()
        expected_attribute = getattr(reference, f"Get{attribute_name}Attr")()
        expected_times = expected_attribute.GetTimeSamples()
        produced_times = produced_attribute.GetTimeSamples()
        if not failures.check(
                produced_times == expected_times,
                f"{attribute_name} time samples {produced_times} != "
                f"{expected_times}"):
            continue

        for time in expected_times:
            produced_values = produced_attribute.Get(time)
            expected_values = expected_attribute.Get(time)
            if not failures.check(
                    len(produced_values) == len(expected_values),
                    f"{attribute_name} at {time} has "
                    f"{len(produced_values)} entries, expected "
                    f"{len(expected_values)}"):
                continue
            for joint, (got, want) in enumerate(
                    zip(produced_values, expected_values)):
                same = (quaternions_match(got, want)
                        if attribute_name == "Rotations"
                        else vectors_match(got, want))
                failures.check(
                    same,
                    f"{attribute_name}[{expected_joints[joint]}] at {time}: "
                    f"{got} != {want}")


def check_usdskel_resolves_the_animation(output: pathlib.Path,
                                         failures: Failures) -> None:
    """Drive the consumer's own query, not the attributes we just wrote.

    UsdSkel fetches translations, rotations and scales as a unit and fails as a
    unit, and `scales` has no schema fallback. An animation missing one binds
    cleanly and satisfies every value comparison above, then resolves no joint
    transforms at all -- the avatar sits at rest. Only the skeleton query
    distinguishes the two.
    """
    # Both the stage and the cache stay in locals: the query holds no strong
    # reference back, so a temporary would be released out from under it.
    stage = Usd.Stage.Open(str(output))
    skeleton = find_skeleton(stage)
    cache = UsdSkel.Cache()
    query = cache.GetSkelQuery(skeleton)
    if not failures.check(bool(query),
                          f"{output} yields no UsdSkel skeleton query"):
        return

    transforms = query.ComputeJointLocalTransforms(Usd.TimeCode(30))
    if not failures.check(
            transforms is not None and len(transforms) == 4,
            f"UsdSkel resolved no animated joint transforms from {output}: "
            f"the SkelAnimation is bound but does not drive the rig"):
        return

    pelvis = transforms[1]
    failures.check(
        vectors_match(pelvis.ExtractTranslation(), Gf.Vec3d(0.0, 1.0, 0.5)),
        f"UsdSkel resolved Pelvis at {pelvis.ExtractTranslation()}, "
        f"expected (0, 1, 0.5)")
    rotation = pelvis.ExtractRotationQuat()
    failures.check(
        quaternions_match(
            Gf.Quatf(rotation.GetReal(), Gf.Vec3f(*rotation.GetImaginary())),
            Gf.Quatf(0.70710677, 0.0, 0.70710677, 0.0)),
        "UsdSkel did not resolve the retargeted Pelvis rotation")


def check_stage_metrics(output: pathlib.Path, avatar: pathlib.Path,
                        failures: Failures) -> None:
    """The bake must re-declare the avatar's metrics, not inherit them.

    Stage metrics are read from the root layer alone, so the reference to the
    avatar does not carry them: an output layer that declares nothing resolves
    to USD's defaults and describes a 1 m avatar as 1.6 cm. Nothing above
    notices -- the joint values are identical either way, and usdview renders
    such a stage correctly because there is nothing else on it to disagree.
    """
    # Both stages stay in locals; the metrics accessors take a stage, not a
    # prim, but a temporary would still be released before the comparison.
    avatar_stage = Usd.Stage.Open(str(avatar))
    baked_stage = Usd.Stage.Open(str(output))
    for name, read in (("metersPerUnit", UsdGeom.GetStageMetersPerUnit),
                       ("upAxis", UsdGeom.GetStageUpAxis)):
        want = read(avatar_stage)
        got = read(baked_stage)
        failures.check(
            got == want,
            f"{output.name} resolves {name} = {got}, expected the avatar's "
            f"{want}")


EXPECTED_BLEND_SHAPES = ["Face_Blink", "Face_Brow", "Face_Smile"]

# Blend-shape weights the expressive fixtures must resolve to, per time code.
#
# `happy` drives Face_Smile at 1 and Face_Brow at 0.5, so its own weight halves
# on the brow; `blink` is binary, so 1 is the only non-zero value its target can
# take; and the clip's last `happy` key is 1.5, which the resolve clamps -- the
# reader carries it verbatim on purpose, and this is the layer the VRMA
# specification's clamp belongs to.
EXPECTED_BLEND_SHAPE_WEIGHTS = {
    0.0: [0.0, 0.0, 0.0],
    15.0: [1.0, 0.25, 0.5],
    30.0: [1.0, 0.5, 1.0],
}


def check_expression_bake(tool: str, fixtures: pathlib.Path,
                          faceless_avatar: pathlib.Path, humanoid_map: str,
                          workspace: pathlib.Path,
                          failures: Failures) -> None:
    """The face half: a named weight becomes this rig's blend-shape weights.

    Everything checked here is a decision rather than plumbing, so each is
    checked by its own consequence: what the tool authors, what it refuses to
    author, and what UsdSkel resolves back out of the result.
    """
    avatar = fixtures / "expressive_avatar.usda"
    clip = fixtures / "expressive_clip.usda"
    output = workspace / "expressive_bake.usda"
    result = run_tool(
        tool, "--avatar", str(avatar), "--animation", str(clip),
        "--output", str(output), "--humanoid-map", humanoid_map,
        "--animation-name", "ExpressiveBake")
    if not failures.check(
            result.returncode == 0,
            f"expression bake failed ({result.returncode}): "
            f"{result.stderr.strip()}"):
        return

    stage = Usd.Stage.Open(str(output))
    animation = find_animation(stage)

    blend_shapes = animation.GetBlendShapesAttr().Get()
    blend_shapes = list(blend_shapes) if blend_shapes else []
    failures.check(
        blend_shapes == EXPECTED_BLEND_SHAPES,
        f"authored blendShapes {blend_shapes} != {EXPECTED_BLEND_SHAPES}")
    # Two absences, for two different reasons, and both are the point.
    # `Face_Frown` is bound by an expression the clip declares and never gives a
    # weight -- an unreported name is not a zero, and authoring one would hold
    # this rig's face in a frown for the whole clip. `Face_Unbound` is driven by
    # an expression the clip does report, and no mesh binds it, so no token
    # names it and the weight has nowhere to land.
    failures.check("Face_Frown" not in blend_shapes,
                   "an expression the clip never weighted was authored anyway")
    failures.check("Face_Unbound" not in blend_shapes,
                   "a blend shape no mesh binds was authored into blendShapes")

    weights = animation.GetBlendShapeWeightsAttr()
    times = weights.GetTimeSamples()
    expected_times = sorted(EXPECTED_BLEND_SHAPE_WEIGHTS)
    if failures.check(
            [round(time, 6) for time in times] == expected_times,
            f"blendShapeWeights time samples {times} != {expected_times}"):
        for time in expected_times:
            values = list(weights.Get(time))
            want = EXPECTED_BLEND_SHAPE_WEIGHTS[time]
            failures.check(
                vectors_match(values, want),
                f"blendShapeWeights at {time}: {values} != {want}")

    # The body keys at 0 and 30 and the clip blinks at 15, which is a key no
    # joint has. Expressions live on the pose, so the bake carries one timeline
    # and not two -- a blink between two body keys must have somewhere to land.
    rotation_times = animation.GetRotationsAttr().GetTimeSamples()
    failures.check(
        [round(time, 6) for time in rotation_times] == expected_times,
        f"the body was baked at {rotation_times}, expected the union with the "
        f"expression keys {expected_times}")

    for wanted, message in (
            ("myWink",
             "an expression the avatar does not declare was not reported"),
            ("happy",
             "the weight clamped from 1.5 was not reported by name"),
            ("Face_Unbound",
             "the blend shape no mesh binds was not named on stderr"),
            ("material colour",
             "the material colour this rig resolves was not reported as "
             "unwritten")):
        failures.check(wanted in result.stderr,
                       f"{message}: {result.stderr.strip()}")

    check_usdskel_resolves_the_expressions(output, failures)
    check_an_unreported_weight_holds(tool, avatar, clip, humanoid_map,
                                     workspace, failures)
    check_a_faceless_rig_reports_the_whole_loss(tool, faceless_avatar, clip,
                                                humanoid_map, workspace,
                                                failures)

    # --no-expressions bakes the body alone. It must author no blendShapes at
    # all rather than an empty array: an authored empty array is a statement
    # that this animation drives no blend shape, which is not the same as an
    # animation that says nothing about them.
    body_only = workspace / "expressive_body_only.usda"
    result = run_tool(
        tool, "--avatar", str(avatar), "--animation", str(clip),
        "--output", str(body_only), "--humanoid-map", humanoid_map,
        "--no-expressions", "--quiet")
    if failures.check(result.returncode == 0,
                      f"--no-expressions bake failed: {result.stderr}"):
        body_only_stage = Usd.Stage.Open(str(body_only))
        body_only_animation = find_animation(body_only_stage)
        failures.check(
            not body_only_animation.GetBlendShapesAttr().HasAuthoredValue(),
            "--no-expressions still authored blendShapes")
        failures.check(
            not body_only_animation.GetBlendShapeWeightsAttr()
            .HasAuthoredValue(),
            "--no-expressions still authored blendShapeWeights")


def check_a_faceless_rig_reports_the_whole_loss(tool: str,
                                                avatar: pathlib.Path,
                                                clip: pathlib.Path,
                                                humanoid_map: str,
                                                workspace: pathlib.Path,
                                                failures: Failures) -> None:
    """A rig with no expressions at all must say so, not pass over it.

    This is the total loss -- every name the clip animates going missing at
    once -- and it is the easiest one to lose, because "the avatar declares no
    expression" reads like a rig that simply has no face rather than like a
    bake that dropped a whole track. A rig missing *one* name reports it; a rig
    missing all of them has to report the same way, or the recovery the guide
    prescribes (run it without --quiet and read the warnings) works on every
    avatar except the one that lost the most.
    """
    output = workspace / "faceless_bake.usda"
    result = run_tool(
        tool, "--avatar", str(avatar), "--animation", str(clip),
        "--output", str(output), "--humanoid-map", humanoid_map)
    if not failures.check(
            result.returncode == 0,
            f"bake of an expressive clip onto a faceless rig failed: "
            f"{result.stderr}"):
        return
    for name in ("happy", "blink", "angry", "ghost", "myWink"):
        failures.check(
            f"'{name}'" in result.stderr,
            f"a rig declaring no expression dropped '{name}' without saying "
            f"so: {result.stderr.strip()}")
    stage = Usd.Stage.Open(str(output))
    failures.check(
        not find_animation(stage).GetBlendShapesAttr().HasAuthoredValue(),
        "a rig that binds no blend shape still had blendShapes authored")


def check_an_unreported_weight_holds(tool: str, avatar: pathlib.Path,
                                     clip: pathlib.Path, humanoid_map: str,
                                     workspace: pathlib.Path,
                                     failures: Failures) -> None:
    """A sample that says nothing leaves the weight where it was.

    A value block is how USD spells "no statement at this time", and it is the
    one way a clip reaches this tool with a name reported at one sample and
    unreported at the next -- every other track holds its last value, so every
    name is reported at every sample. The rule one layer down is that an
    unreported name is not a zero weight, and a fixed-width array has no
    absent, so the authored weight must hold rather than fall to zero: zeroing
    it would end a blink the clip never asked to end.
    """
    blocked = workspace / "blocked_clip.usda"
    shutil.copy(clip, blocked)
    # The stage stays in a local; the layer it saves is fetched back off it.
    blocked_stage = Usd.Stage.Open(str(blocked))
    weight = blocked_stage.GetPrimAtPath(
        "/Animation/Expressions/blink").GetAttribute("vrm:expressionWeight")
    weight.Set(Sdf.ValueBlock(), 30.0)
    blocked_stage.GetRootLayer().Save()
    # Without this the case is vacuous: an unblocked track would resolve to the
    # same 1 by simply being read, and the check would pass either way.
    if not failures.check(
            weight.Get(30.0) is None,
            "the blocked-weight fixture still resolves a weight at 30, so it "
            "cannot tell a held weight from a re-read one"):
        return

    output = workspace / "blocked_bake.usda"
    result = run_tool(
        tool, "--avatar", str(avatar), "--animation", str(blocked),
        "--output", str(output), "--humanoid-map", humanoid_map, "--quiet")
    if not failures.check(result.returncode == 0,
                          f"bake of a blocked weight failed: {result.stderr}"):
        return
    stage = Usd.Stage.Open(str(output))
    animation = find_animation(stage)
    values = list(animation.GetBlendShapeWeightsAttr().Get(30.0))
    want = EXPECTED_BLEND_SHAPE_WEIGHTS[30.0]
    failures.check(
        vectors_match(values, want),
        f"a blocked expression sample resolved to {values} at 30, expected "
        f"the held {want}")


def check_usdskel_resolves_the_expressions(output: pathlib.Path,
                                           failures: Failures) -> None:
    """Ask UsdSkel, not the attributes we just wrote.

    A SkelAnimation names blend shapes by token and UsdSkel maps those tokens
    onto each skinned prim's own `skel:blendShapes` order. So an animation whose
    tokens are right and whose *order* is anything at all still resolves
    correctly on the mesh, and one naming a token no mesh binds resolves to
    nothing -- neither of which a comparison against the authored array can
    tell. The mapper is what a consumer actually runs, so it is what decides.
    """
    stage = Usd.Stage.Open(str(output))
    skeleton = find_skeleton(stage)
    cache = UsdSkel.Cache()
    skeleton_query = cache.GetSkelQuery(skeleton)
    if not failures.check(bool(skeleton_query),
                          f"{output} yields no UsdSkel skeleton query"):
        return
    animation_query = skeleton_query.GetAnimQuery()
    if not failures.check(bool(animation_query),
                          f"{output} binds no animation to its skeleton"):
        return
    order = list(animation_query.GetBlendShapeOrder())
    failures.check(
        order == EXPECTED_BLEND_SHAPES,
        f"UsdSkel reads the blend-shape order as {order}, expected "
        f"{EXPECTED_BLEND_SHAPES}")
    for time, want in EXPECTED_BLEND_SHAPE_WEIGHTS.items():
        resolved = animation_query.ComputeBlendShapeWeights(Usd.TimeCode(time))
        failures.check(
            resolved is not None and vectors_match(list(resolved), want),
            f"UsdSkel resolved blend-shape weights {resolved} at {time}, "
            f"expected {want}")

    root = UsdSkel.Root(stage.GetPrimAtPath("/Avatar"))
    if not failures.check(bool(root),
                          "the baked stage composes no SkelRoot at /Avatar"):
        return
    cache.Populate(root, Usd.TraverseInstanceProxies())
    bindings = cache.ComputeSkelBindings(root, Usd.TraverseInstanceProxies())
    skinning = [target for binding in bindings
                for target in binding.GetSkinningTargets()
                if target.HasBlendShapes()]
    if not failures.check(
            len(skinning) == 1,
            f"expected one skinned prim with blend shapes, found "
            f"{len(skinning)}"):
        return
    mapper = skinning[0].GetBlendShapeMapper()
    if not failures.check(bool(mapper),
                          "the face mesh maps none of the animation's blend "
                          "shapes: the tokens the bake authored are not the "
                          "ones it binds"):
        return
    # The mesh's own order is Smile, Blink, Brow, Frown -- deliberately not the
    # animation's -- and Frown is a shape the animation never names, so it must
    # come back at 0 rather than at whatever sat in that slot.
    weights = animation_query.ComputeBlendShapeWeights(Usd.TimeCode(15))
    remapped = mapper.Remap(weights)
    expected = [0.5, 1.0, 0.25, 0.0]
    failures.check(
        remapped is not None and vectors_match(list(remapped), expected),
        f"the face mesh resolves {remapped} at time 15, expected {expected}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--tool", required=True)
    parser.add_argument("--fixtures", required=True,
                        help="docs/design/fixtures/motion")
    parser.add_argument("--tool-fixtures", required=True,
                        help="tools/motionRetarget/tests/fixtures")
    parser.add_argument("--humanoid-map", required=True)
    options = parser.parse_args()

    fixtures = pathlib.Path(options.fixtures).resolve()
    tool_fixtures = pathlib.Path(options.tool_fixtures).resolve()
    avatar = fixtures / "avatar.usda"
    clip = fixtures / "canonical_walk.usda"
    expected = fixtures / "expected_retargeted.usda"
    for path in (avatar, clip, expected,
                 tool_fixtures / "expressive_avatar.usda",
                 tool_fixtures / "expressive_clip.usda"):
        if not path.is_file():
            print(f"FAIL: missing fixture {path}", file=sys.stderr)
            return 1

    failures = Failures()
    with tempfile.TemporaryDirectory() as directory:
        workspace = pathlib.Path(directory)

        output = workspace / "character_walk.usda"
        result = run_tool(
            options.tool,
            "--avatar", str(avatar),
            "--animation", str(clip),
            "--output", str(output),
            "--humanoid-map", options.humanoid_map,
            "--animation-name", "RetargetedWalk")
        if not failures.check(
                result.returncode == 0,
                f"bake failed ({result.returncode}): {result.stderr.strip()}"):
            return failures.report()
        failures.check(output.is_file(), f"{output} was not written")
        compare_with_expected(output, expected, failures)
        check_usdskel_resolves_the_animation(output, failures)
        check_stage_metrics(output, avatar, failures)

        # The metrics are read off the avatar, not assumed to be VRM's. The
        # tool takes any rig OpenUSD can open, and every check above passes
        # against `avatar.usda` on a bake that hardcodes `metersPerUnit = 1`
        # and `upAxis = "Y"` -- which is what that fixture declares.
        rescaled = workspace / "centimetre_avatar.usda"
        shutil.copy(avatar, rescaled)
        # The stage stays in a local: the layer it saves is fetched back off it.
        rescaled_stage = Usd.Stage.Open(str(rescaled))
        UsdGeom.SetStageMetersPerUnit(rescaled_stage, 0.01)
        UsdGeom.SetStageUpAxis(rescaled_stage, UsdGeom.Tokens.z)
        rescaled_stage.GetRootLayer().Save()
        rescaled_output = workspace / "centimetre_bake.usda"
        result = run_tool(
            options.tool,
            "--avatar", str(rescaled), "--animation", str(clip),
            "--output", str(rescaled_output),
            "--humanoid-map", options.humanoid_map, "--quiet")
        if failures.check(
                result.returncode == 0,
                f"bake onto a centimetre, Z-up rig failed: {result.stderr}"):
            check_stage_metrics(rescaled_output, rescaled, failures)

        # Re-baking over an existing output overwrites rather than failing.
        rerun = run_tool(
            options.tool,
            "--avatar", str(avatar), "--animation", str(clip),
            "--output", str(output), "--humanoid-map", options.humanoid_map,
            "--animation-name", "RetargetedWalk", "--quiet")
        failures.check(rerun.returncode == 0,
                       f"re-bake over an existing output failed: {rerun.stderr}")

        # The tool must apply SkelBindingAPI, not assume the rig already has
        # it. `avatar.usda` does, but an imported `.vrm` skeleton does not, and
        # UsdSkel honours skel:animationSource only on a prim carrying the
        # schema -- so a bare relationship binds nothing, the avatar sits at
        # rest, and no diagnostic fires anywhere. Every check above passes on
        # this fixture either way; only a rig without the schema separates them.
        # Strip the schema through USD rather than by filtering the layer text:
        # a line filter drops `apiSchemas` from every prim that happens to carry
        # it, so adding a skinned mesh to the fixture would quietly widen what
        # this case changes while it kept on passing.
        bare = workspace / "bare_avatar.usda"
        shutil.copy(avatar, bare)
        # The stage stays in a local for the same reason as above: the prim
        # holds only a weak reference back to it.
        bare_stage = Usd.Stage.Open(str(bare))
        bare_skeleton = find_skeleton(bare_stage).GetPrim()
        bare_skeleton.RemoveAPI(UsdSkel.BindingAPI)
        bare_stage.GetRootLayer().Save()
        failures.check(not bare_skeleton.HasAPI(UsdSkel.BindingAPI),
                       "the bare-rig fixture still applies SkelBindingAPI on "
                       "its skeleton, so it cannot detect a missing Apply()")
        bare_output = workspace / "bare_rig_bake.usda"
        result = run_tool(
            options.tool,
            "--avatar", str(bare), "--animation", str(clip),
            "--output", str(bare_output), "--humanoid-map", options.humanoid_map,
            "--animation-name", "RetargetedWalk", "--quiet")
        if failures.check(
                result.returncode == 0,
                f"bake onto a rig without SkelBindingAPI failed: "
                f"{result.stderr}"):
            check_usdskel_resolves_the_animation(bare_output, failures)

        # --root-motion ignore leaves every joint at its rest translation.
        in_place = workspace / "in_place.usda"
        result = run_tool(
            options.tool,
            "--avatar", str(avatar), "--animation", str(clip),
            "--output", str(in_place), "--humanoid-map", options.humanoid_map,
            "--root-motion", "ignore", "--quiet")
        if failures.check(result.returncode == 0,
                          f"in-place bake failed: {result.stderr}"):
            # Keep the stage in a local: a UsdPrim only weakly references its
            # stage, so an inline Usd.Stage.Open() would be released here and
            # every schema access on the prim would raise.
            in_place_stage = Usd.Stage.Open(str(in_place))
            animation = find_animation(in_place_stage)
            attribute = animation.GetTranslationsAttr()
            rest = attribute.Get(attribute.GetTimeSamples()[0])
            for time in attribute.GetTimeSamples():
                failures.check(
                    all(vectors_match(got, want)
                        for got, want in zip(attribute.Get(time), rest)),
                    f"--root-motion ignore still moved a joint at {time}")

        # --resample lands a uniform timeline on the output.
        resampled = workspace / "resampled.usda"
        result = run_tool(
            options.tool,
            "--avatar", str(avatar), "--animation", str(clip),
            "--output", str(resampled), "--humanoid-map", options.humanoid_map,
            "--resample", "10", "--quiet")
        if failures.check(result.returncode == 0,
                          f"resampled bake failed: {result.stderr}"):
            resampled_stage = Usd.Stage.Open(str(resampled))
            animation = find_animation(resampled_stage)
            times = animation.GetRotationsAttr().GetTimeSamples()
            # 1 s at 10 Hz -> 11 samples, authored in time codes at 30 fps.
            failures.check(len(times) == 11,
                           f"--resample 10 produced {len(times)} samples, "
                           f"expected 11")
            failures.check(
                abs(times[-1] - 30.0) <= TOLERANCE,
                f"resampled clip ends at time code {times[-1]}, expected 30")

        check_expression_bake(options.tool, tool_fixtures, avatar,
                              options.humanoid_map, workspace, failures)

        # Writing the output over an input is refused, and the input survives.
        # The output layer is cleared before it is authored, so accepting this
        # would destroy the avatar rather than merely producing a bad result.
        guarded = workspace / "guarded_avatar.usda"
        shutil.copy(avatar, guarded)
        before = guarded.read_bytes()
        result = run_tool(
            options.tool,
            "--avatar", str(guarded), "--animation", str(clip),
            "--output", str(guarded), "--humanoid-map", options.humanoid_map)
        failures.check(result.returncode != 0,
                       "a bake whose --output names the avatar was accepted")
        failures.check(guarded.read_bytes() == before,
                       "the avatar was modified by a refused in-place bake")

        # A rig with no VrmHumanoidAPI and no map is refused, by name.
        result = run_tool(
            options.tool,
            "--avatar", str(avatar), "--animation", str(clip),
            "--output", str(workspace / "unmapped.usda"))
        failures.check(result.returncode != 0,
                       "an unmapped avatar was accepted without a humanoid map")
        failures.check("--humanoid-map" in result.stderr,
                       "the unmapped-avatar error does not name --humanoid-map")

        # A bad joint token is reported rather than silently dropped.
        bad_map = workspace / "bad_map.json"
        bad_map.write_text('{"hips": "NoSuchJoint"}', encoding="utf-8")
        result = run_tool(
            options.tool,
            "--avatar", str(avatar), "--animation", str(clip),
            "--output", str(workspace / "bad.usda"),
            "--humanoid-map", str(bad_map))
        failures.check(result.returncode != 0,
                       "a humanoid map naming an absent joint was accepted")
        failures.check("NoSuchJoint" in result.stderr,
                       "the bad-joint error does not name the joint")

    result = run_tool(options.tool, "--help")
    failures.check(result.returncode == 0, "--help did not exit 0")
    failures.check("motion_retarget" in result.stdout,
                   "--help did not print usage")

    if failures.report() != 0:
        return 1
    print("motion_retarget end-to-end checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
