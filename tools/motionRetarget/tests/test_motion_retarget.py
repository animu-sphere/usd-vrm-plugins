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
import math
import pathlib
import shutil
import subprocess
import sys
import tempfile

from pxr import Gf, Sdf, Usd, UsdGeom, UsdSkel, Vt

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


# The same clip on the rig that arbitrates instead of summing
# (`overriding_avatar.usda`, whose `happy` carries `overrideBlink = "blend"` and
# whose `blink` is not binary).
#
# Only the blink column moves, and it moves by the weight `happy` holds at that
# instant: 0.5 of the way to a full override at time code 15 leaves half the
# blink standing, and a `happy` clamped to 1 at 30 takes all of it. Both are
# values a resolve that ignored the override could not produce -- it would
# author the unsuppressed 1.0 in both rows.
EXPECTED_OVERRIDDEN_WEIGHTS = {
    0.0: [0.0, 0.0, 0.0],
    15.0: [0.5, 0.25, 0.5],
    30.0: [0.0, 0.5, 1.0],
}


def check_expression_overrides_arbitrate_the_face(
        tool: str, fixtures: pathlib.Path, humanoid_map: str,
        workspace: pathlib.Path, failures: Failures) -> None:
    """The avatar's own arbitration, from its attributes to the baked weights.

    Two co-active expressions sum on the vertices they share, whatever their
    weights say, and VRM 1.0's per-expression `overrideBlink` / `overrideLookAt`
    / `overrideMouth` are the only mechanism the specification gives for it. The
    resolve is the unit suite's; what this measures is that the rule reaches it
    -- the tokens are read off the avatar, the rate lands on the right
    expression, and the suppressed weight is what gets authored.
    """
    avatar = fixtures / "overriding_avatar.usda"
    clip = fixtures / "expressive_clip.usda"
    output = workspace / "overridden_bake.usda"
    result = run_tool(
        tool, "--avatar", str(avatar), "--animation", str(clip),
        "--output", str(output), "--humanoid-map", humanoid_map,
        "--animation-name", "OverriddenBake")
    if not failures.check(
            result.returncode == 0,
            f"overridden bake failed ({result.returncode}): "
            f"{result.stderr.strip()}"):
        return

    stage = Usd.Stage.Open(str(output))
    animation = find_animation(stage)
    blend_shapes = animation.GetBlendShapesAttr().Get()
    blend_shapes = list(blend_shapes) if blend_shapes else []
    # The same three shapes as the unarbitrated bake: an override changes what a
    # weight is, never which shapes the rig has.
    failures.check(
        blend_shapes == EXPECTED_BLEND_SHAPES,
        f"authored blendShapes {blend_shapes} != {EXPECTED_BLEND_SHAPES}")

    weights = animation.GetBlendShapeWeightsAttr()
    for time, want in EXPECTED_OVERRIDDEN_WEIGHTS.items():
        values = weights.Get(time)
        values = list(values) if values else []
        failures.check(
            vectors_match(values, want),
            f"overridden blendShapeWeights at {time}: {values} != {want}")

    # The suppression is named, with the expression that did it: a producer
    # whose blink track went flat has nothing to find in the weights, because
    # nothing in them is wrong.
    failures.check(
        "blink (by happy)" in result.stderr,
        f"the bake suppressed a blink without saying which expression took it: "
        f"{result.stderr.strip()}")
    # And the token outside the vocabulary is refused out loud rather than read
    # as "no arbitration".
    failures.check(
        "shush" in result.stderr,
        f"an override token the tool cannot read was accepted in silence: "
        f"{result.stderr.strip()}")


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


# What the gazing fixtures must resolve to, per time code, as (yaw, pitch) in
# degrees measured off the authored eye rotation itself.
#
# The numbers are the range maps and nothing else. The clip aims 45 degrees to
# the character's left at 0 and 45 degrees right and 35.264 degrees down at 30;
# the avatar maps 90 degrees of aim onto 10 degrees of outer eye, 5 of inner, 6
# of down and 12 of up. So the outer eye takes 5, the inner 2.5, and the shared
# vertical 35.264/90 * 6 = 2.3510 -- three different scales, which is what makes
# a resolve that read one map for both eyes, or swapped inner for outer, fail
# here rather than merely look plausible.
EXPECTED_GAZE = {
    0.0: {"LeftEye": (5.0, 0.0), "RightEye": (2.5, 0.0)},
    30.0: {"LeftEye": (-2.5, -2.350959), "RightEye": (-5.0, -2.350959)},
}

# The same two samples on the rig that aims its eyes with expressions. One
# weight drives both eyes, so there is no inner and outer to tell apart and the
# gaze angle over a right angle is the weight: 45/90 horizontally, 35.264/90
# down. The two the sample does not drive are still authored, at zero.
EXPECTED_GAZE_BLEND_SHAPES = ["Eyes_Down", "Eyes_Left", "Eyes_Right", "Eyes_Up"]
EXPECTED_GAZE_WEIGHTS = {
    0.0: [0.0, 0.5, 0.0, 0.0],
    30.0: [0.391827, 0.0, 0.5, 0.0],
}


def gaze_angles(quaternion) -> tuple[float, float]:
    """The yaw and pitch an authored eye rotation actually aims at.

    Measured by turning the forward axis with it rather than by decomposing the
    quaternion the way the tool composed it: a check that rebuilt
    `yaw about +Y then pitch about +X` would agree with a tool that had both
    conventions inverted, which is exactly the mistake worth catching.
    """
    forward = Gf.Rotation(quaternion).TransformDir(Gf.Vec3d(0.0, 0.0, 1.0))
    yaw = math.degrees(math.atan2(forward[0], forward[2]))
    pitch = math.degrees(math.atan2(forward[1],
                                    math.hypot(forward[0], forward[2])))
    return yaw, pitch


def check_look_at_bake(tool: str, fixtures: pathlib.Path,
                       workspace: pathlib.Path, failures: Failures) -> None:
    """The gaze half: a place the clip looks at becomes this rig's eyes.

    A clip names a target *point*, because a direction is only meaningful next
    to a head and where the head sits belongs to the avatar. So the answer
    depends on the rig, and the same clip is baked onto two of them here -- one
    that aims its eyes with joints and one that aims them with expressions --
    because those two are not a spelling difference and each is a path to the
    stage of its own.
    """
    avatar = fixtures / "gazing_avatar.usda"
    clip = fixtures / "gazing_clip.usda"
    output = workspace / "gazing_bake.usda"
    result = run_tool(
        tool, "--avatar", str(avatar), "--animation", str(clip),
        "--output", str(output), "--animation-name", "GazeBake")
    if not failures.check(
            result.returncode == 0,
            f"look-at bake failed ({result.returncode}): "
            f"{result.stderr.strip()}"):
        return

    stage = Usd.Stage.Open(str(output))
    animation = find_animation(stage)
    joints = list(animation.GetJointsAttr().Get())
    rotations = animation.GetRotationsAttr()

    for time, expected in EXPECTED_GAZE.items():
        values = rotations.Get(time)
        if not failures.check(
                values is not None and len(values) == len(joints),
                f"the gaze bake authored no rotations at {time}"):
            continue
        for eye, (want_yaw, want_pitch) in expected.items():
            index = next((i for i, joint in enumerate(joints)
                          if joint.endswith("/" + eye)), None)
            if not failures.check(index is not None,
                                  f"the gaze bake authored no {eye} joint"):
                continue
            yaw, pitch = gaze_angles(values[index])
            failures.check(
                abs(yaw - want_yaw) <= 1e-3,
                f"{eye} at {time} aims {yaw:.4f} degrees horizontally, "
                f"expected {want_yaw}")
            failures.check(
                abs(pitch - want_pitch) <= 1e-3,
                f"{eye} at {time} aims {pitch:.4f} degrees vertically, "
                f"expected {want_pitch}")

    # The eyes are driven and they are not in the humanoid map: a VRM names them
    # through its look-at configuration, so a gaze that only reached mapped
    # joints would be a different feature. The summary says how many it aimed.
    failures.check("2 eye joints aimed" in result.stdout,
                   f"the gaze bake did not report the eyes it aimed: "
                   f"{result.stdout.strip()}")
    # A bone-driven gaze authors no blend shape at all -- it is joints, and
    # authoring an empty array would state that this animation drives no blend
    # shape rather than saying nothing about them.
    failures.check(
        not animation.GetBlendShapesAttr().HasAuthoredValue(),
        "a bone-driven gaze authored blendShapes")

    check_look_at_through_expressions(tool, fixtures, workspace, failures)
    check_no_look_at_leaves_the_eyes_alone(tool, avatar, clip, workspace,
                                           failures)
    check_a_gazeless_rig_says_so(tool, fixtures, clip, workspace, failures)
    check_an_unstated_gaze_holds(tool, avatar, clip, workspace, failures)
    check_a_clip_driven_eye_is_reported(tool, fixtures, workspace, failures)
    check_no_expressions_takes_an_expression_gaze_with_it(
        tool, fixtures, workspace, failures)
    check_a_gaze_expression_collision_is_reported_once(
        tool, fixtures, workspace, failures)


def check_look_at_through_expressions(tool: str, fixtures: pathlib.Path,
                                      workspace: pathlib.Path,
                                      failures: Failures) -> None:
    """An expression-driven rig answers the same clip with weights.

    And it answers through the expression resolve the face already uses:
    `lookLeft` is an expression of this rig exactly as `happy` is, so the gaze
    reaches the avatar's binds through one accumulator rather than through a
    second path into the same blend shapes.
    """
    avatar = fixtures / "gazing_expression_avatar.usda"
    clip = fixtures / "gazing_clip.usda"
    output = workspace / "gazing_expression_bake.usda"
    result = run_tool(
        tool, "--avatar", str(avatar), "--animation", str(clip),
        "--output", str(output), "--animation-name", "GazeExpressionBake")
    if not failures.check(
            result.returncode == 0,
            f"expression-driven look-at bake failed: {result.stderr.strip()}"):
        return

    stage = Usd.Stage.Open(str(output))
    animation = find_animation(stage)
    blend_shapes = animation.GetBlendShapesAttr().Get()
    blend_shapes = list(blend_shapes) if blend_shapes else []
    failures.check(
        blend_shapes == EXPECTED_GAZE_BLEND_SHAPES,
        f"the expression-driven gaze authored {blend_shapes}, expected "
        f"{EXPECTED_GAZE_BLEND_SHAPES}")

    weights = animation.GetBlendShapeWeightsAttr()
    for time, want in EXPECTED_GAZE_WEIGHTS.items():
        values = weights.Get(time)
        failures.check(
            values is not None and vectors_match(list(values), want),
            f"the expression-driven gaze resolves {values} at {time}, "
            f"expected {want}")

    # No eye joint exists on this rig, and none is invented: the skeleton it
    # references has six joints and the animation drives exactly those.
    joints = list(animation.GetJointsAttr().Get())
    failures.check(
        not any("Eye" in joint for joint in joints),
        f"an expression-driven gaze authored an eye joint: {joints}")


def check_no_look_at_leaves_the_eyes_alone(tool: str, avatar: pathlib.Path,
                                           clip: pathlib.Path,
                                           workspace: pathlib.Path,
                                           failures: Failures) -> None:
    """--no-look-at bakes the body and leaves the eyes where the rig rests them.

    A pipeline that aims the eyes itself needs the bake not to write over them,
    and "not written" has to mean the rest rotation rather than a forward gaze:
    the eye joints are still in the animation, because every joint of the target
    rig is, so the check is on their value and not on their presence.
    """
    output = workspace / "gazing_no_look_at.usda"
    result = run_tool(
        tool, "--avatar", str(avatar), "--animation", str(clip),
        "--output", str(output), "--no-look-at", "--quiet")
    if not failures.check(result.returncode == 0,
                          f"--no-look-at bake failed: {result.stderr}"):
        return
    stage = Usd.Stage.Open(str(output))
    animation = find_animation(stage)
    joints = list(animation.GetJointsAttr().Get())
    rotations = animation.GetRotationsAttr()
    for time in rotations.GetTimeSamples():
        values = rotations.Get(time)
        for joint, quaternion in zip(joints, values):
            if "Eye" not in joint:
                continue
            yaw, pitch = gaze_angles(quaternion)
            failures.check(
                abs(yaw) <= 1e-4 and abs(pitch) <= 1e-4,
                f"--no-look-at still aimed {joint} at {time}: "
                f"({yaw:.4f}, {pitch:.4f})")


def check_a_gazeless_rig_says_so(tool: str, fixtures: pathlib.Path,
                                 clip: pathlib.Path, workspace: pathlib.Path,
                                 failures: Failures) -> None:
    """A clip that names a gaze no rig can follow must not go quiet.

    This is the look-at equivalent of the faceless-rig case, and it is the same
    kind of loss: the clip stated where the character is looking, the avatar
    declares nothing about how its eyes work, and the whole track goes missing.
    An operator who reads the warnings has to be told, or the recovery works on
    every rig except the one that lost everything.
    """
    # The gazing avatar with its look-at removed, so the *only* difference from
    # the passing case is the thing under test. A different rig would have
    # brought a different skeleton and a different humanoid map with it, and the
    # warning would then be as likely to be about a missing head.
    avatar = workspace / "gazeless_avatar.usda"
    shutil.copy(fixtures / "gazing_avatar.usda", avatar)
    # The stage stays in a local: the layer it saves is fetched back off it.
    gazeless_stage = Usd.Stage.Open(str(avatar))
    gazeless_stage.RemovePrim("/Avatar/rig/LookAt")
    gazeless_stage.GetRootLayer().Save()
    if not failures.check(
            not gazeless_stage.GetPrimAtPath("/Avatar/rig/LookAt"),
            "the gazeless fixture still declares a look-at, so it cannot tell "
            "a dropped gaze from an authored one"):
        return

    output = workspace / "gazeless_bake.usda"
    result = run_tool(
        tool, "--avatar", str(avatar), "--animation", str(clip),
        "--output", str(output))
    if not failures.check(
            result.returncode == 0,
            f"bake of a gazing clip onto a rig with no look-at failed: "
            f"{result.stderr}"):
        return
    failures.check(
        "look-at" in result.stderr,
        f"a rig declaring no look-at dropped the clip's gaze without saying "
        f"so: {result.stderr.strip()}")


def check_an_unstated_gaze_holds(tool: str, avatar: pathlib.Path,
                                 clip: pathlib.Path, workspace: pathlib.Path,
                                 failures: Failures) -> None:
    """A sample that says nothing about the gaze leaves it standing.

    A USD value block is how a clip spells "no statement at this time", and the
    rule this repository already applies to a blocked expression weight is that
    the previous value holds rather than dropping to neutral. The eyes have to
    be under the same rule, and not only for consistency: the two rig types
    reach the stage by different routes -- a fixed-width blend-shape array,
    which holds by construction, and a per-sample joint array, which does not --
    so the same clip would freeze one rig's eyes and snap the other's back to
    rest unless this is done deliberately.
    """
    blocked = workspace / "blocked_gaze_clip.usda"
    shutil.copy(clip, blocked)
    # The stage stays in a local; the layer it saves is fetched back off it.
    blocked_stage = Usd.Stage.Open(str(blocked))
    target = blocked_stage.GetPrimAtPath(
        "/Animation/LookAt").GetAttribute("vrm:lookAtTarget")
    target.Set(Sdf.ValueBlock(), 30.0)
    blocked_stage.GetRootLayer().Save()
    # Without this the case is vacuous: an unblocked target resolves at 30 by
    # simply being read, and a held eye would be indistinguishable from a
    # re-evaluated one.
    if not failures.check(
            target.Get(30.0) is None,
            "the blocked-gaze fixture still resolves a target at 30, so it "
            "cannot tell a held gaze from a re-evaluated one"):
        return

    output = workspace / "blocked_gaze_bake.usda"
    result = run_tool(
        tool, "--avatar", str(avatar), "--animation", str(blocked),
        "--output", str(output), "--quiet")
    if not failures.check(result.returncode == 0,
                          f"bake of a blocked gaze failed: {result.stderr}"):
        return
    stage = Usd.Stage.Open(str(output))
    animation = find_animation(stage)
    joints = list(animation.GetJointsAttr().Get())
    values = animation.GetRotationsAttr().Get(30.0)
    for eye, (want_yaw, want_pitch) in EXPECTED_GAZE[0.0].items():
        index = next((i for i, joint in enumerate(joints)
                      if joint.endswith("/" + eye)), None)
        if index is None:
            continue
        yaw, pitch = gaze_angles(values[index])
        failures.check(
            abs(yaw - want_yaw) <= 1e-3 and abs(pitch - want_pitch) <= 1e-3,
            f"a blocked gaze at 30 left {eye} at ({yaw:.4f}, {pitch:.4f}), "
            f"expected the held ({want_yaw}, {want_pitch})")


def check_a_clip_driven_eye_is_reported(tool: str, fixtures: pathlib.Path,
                                        workspace: pathlib.Path,
                                        failures: Failures) -> None:
    """An eye the clip itself animates is overwritten, and it is said out loud.

    An eye is a human bone like any other, so a rig that binds `leftEye` in its
    humanoid map and a clip that animates it produce a rotation the gaze is
    about to overwrite. One of the two has to win and the gaze does -- but
    losing a channel the clip explicitly authored is exactly the silence the
    expression collision one branch over is already reported for.
    """
    eye_token = "Root/Hips/Spine/Chest/Neck/Head/LeftEye"
    avatar = workspace / "eye_mapped_avatar.usda"
    shutil.copy(fixtures / "gazing_avatar.usda", avatar)
    # The stage stays in a local: the layer it saves is fetched back off it.
    avatar_stage = Usd.Stage.Open(str(avatar))
    avatar_stage.GetPrimAtPath("/Avatar/rig/humanoid").CreateAttribute(
        "vrm:humanBones:leftEye", Sdf.ValueTypeNames.Token, True).Set(eye_token)
    avatar_stage.GetRootLayer().Save()

    clip = workspace / "eye_driven_clip.usda"
    shutil.copy(fixtures / "gazing_clip.usda", clip)
    clip_stage = Usd.Stage.Open(str(clip))
    added = "hips/spine/chest/neck/head/leftEye"
    skeleton = clip_stage.GetPrimAtPath("/Animation/HumanoidSkeleton")
    joints_attr = skeleton.GetAttribute("joints")
    joints_attr.Set(Vt.TokenArray(list(joints_attr.Get()) + [added]))
    rest_attr = skeleton.GetAttribute("restTransforms")
    rest = list(rest_attr.Get())
    rest.append(Gf.Matrix4d().SetTranslate(Gf.Vec3d(0.03, 0.05, 0.05)))
    rest_attr.Set(Vt.Matrix4dArray(rest))

    animation = clip_stage.GetPrimAtPath("/Animation/BodyAnimation")
    animation_joints = animation.GetAttribute("joints")
    animation_joints.Set(Vt.TokenArray(list(animation_joints.Get()) + [added]))
    rotations = animation.GetAttribute("rotations")
    for time in rotations.GetTimeSamples():
        # A quarter turn, so the channel is unmistakably not the rest pose and
        # not the gaze either.
        rotations.Set(
            Vt.QuatfArray(list(rotations.Get(time))
                          + [Gf.Quatf(Gf.Rotation(Gf.Vec3d(0, 1, 0), 90.0)
                                      .GetQuat())]),
            time)
    translations = animation.GetAttribute("translations")
    for time in translations.GetTimeSamples():
        translations.Set(
            Vt.Vec3fArray(list(translations.Get(time))
                          + [Gf.Vec3f(0.03, 0.05, 0.05)]),
            time)
    scales = animation.GetAttribute("scales")
    scales.Set(Vt.Vec3hArray(list(scales.Get()) + [Gf.Vec3h(1.0, 1.0, 1.0)]))
    clip_stage.GetRootLayer().Save()

    output = workspace / "eye_driven_bake.usda"
    result = run_tool(
        tool, "--avatar", str(avatar), "--animation", str(clip),
        "--output", str(output))
    if not failures.check(
            result.returncode == 0,
            f"bake of a clip that drives an eye failed: {result.stderr}"):
        return
    failures.check(
        "leftEye" in result.stderr and "gaze" in result.stderr,
        f"the gaze overwrote a channel the clip authored without saying so: "
        f"{result.stderr.strip()}")
    # Once, not once per sample.
    failures.check(
        result.stderr.count("the clip animates 'leftEye'") == 1,
        f"the eye collision was reported more than once: "
        f"{result.stderr.strip()}")

    # And the gaze is what survives, not the clip's channel.
    stage = Usd.Stage.Open(str(output))
    baked = find_animation(stage)
    joints = list(baked.GetJointsAttr().Get())
    index = joints.index(eye_token)
    yaw, _ = gaze_angles(baked.GetRotationsAttr().Get(0.0)[index])
    failures.check(
        abs(yaw - EXPECTED_GAZE[0.0]["LeftEye"][0]) <= 1e-3,
        f"the clip's own eye channel survived the gaze: {yaw:.4f} degrees")


def check_no_expressions_takes_an_expression_gaze_with_it(
        tool: str, fixtures: pathlib.Path, workspace: pathlib.Path,
        failures: Failures) -> None:
    """--no-expressions suppresses an expression-driven gaze, and reports it.

    Those four gaze weights reach the stage as blend-shape weights and by no
    other route, so the flag that refuses to author blend-shape weights refuses
    them too. The summary has to agree: a run that says it gazed and wrote
    nothing is worse than one that never claimed to.
    """
    avatar = fixtures / "gazing_expression_avatar.usda"
    clip = fixtures / "gazing_clip.usda"
    output = workspace / "gazing_expression_body_only.usda"
    result = run_tool(
        tool, "--avatar", str(avatar), "--animation", str(clip),
        "--output", str(output), "--no-expressions")
    if not failures.check(
            result.returncode == 0,
            f"--no-expressions bake of a gazing clip failed: {result.stderr}"):
        return
    stage = Usd.Stage.Open(str(output))
    animation = find_animation(stage)
    failures.check(
        not animation.GetBlendShapesAttr().HasAuthoredValue(),
        "--no-expressions still authored an expression-driven gaze")
    failures.check(
        "samples gazing" not in result.stdout,
        f"--no-expressions reported a gaze it did not author: "
        f"{result.stdout.strip()}")
    failures.check(
        "--no-expressions" in result.stderr,
        f"--no-expressions dropped the clip's gaze without saying so: "
        f"{result.stderr.strip()}")


def check_a_gaze_expression_collision_is_reported_once(
        tool: str, fixtures: pathlib.Path, workspace: pathlib.Path,
        failures: Failures) -> None:
    """A clip may drive `lookLeft` by name *and* name a look-at target.

    Both are legitimate authoring and one has to win; the gaze does, because it
    is the value this rig's own curves produced. What matters here is that the
    operator is told **once**: a collision is a fact about the clip, not about a
    sample, and a thousand-sample take reporting it per sample per name buries
    every other diagnostic under four thousand identical lines.
    """
    clip = workspace / "colliding_gaze_clip.usda"
    shutil.copy(fixtures / "gazing_clip.usda", clip)
    # The stage stays in a local: the layer it saves is fetched back off it.
    clip_stage = Usd.Stage.Open(str(clip))
    expressions = clip_stage.DefinePrim("/Animation/Expressions", "Scope")
    named = clip_stage.DefinePrim("/Animation/Expressions/lookLeft", "Scope")
    named.CreateAttribute("vrm:expressionName", Sdf.ValueTypeNames.Token,
                          True).Set("lookLeft")
    weight = named.CreateAttribute("vrm:expressionWeight",
                                   Sdf.ValueTypeNames.Float)
    weight.Set(0.25, 0.0)
    weight.Set(0.75, 30.0)
    clip_stage.GetRootLayer().Save()
    if not failures.check(
            bool(expressions) and weight.Get(0.0) is not None,
            "the colliding-gaze fixture authored no expression track, so it "
            "cannot collide with anything"):
        return

    output = workspace / "colliding_gaze_bake.usda"
    result = run_tool(
        tool, "--avatar", str(fixtures / "gazing_expression_avatar.usda"),
        "--animation", str(clip), "--output", str(output))
    if not failures.check(
            result.returncode == 0,
            f"bake of a colliding gaze failed: {result.stderr}"):
        return
    occurrences = result.stderr.count("the clip animates expression 'lookLeft'")
    failures.check(
        occurrences == 1,
        f"the gaze/expression collision was reported {occurrences} times, "
        f"expected exactly one line for a two-sample clip")

    # And the gaze wins: 45 degrees over a right angle, not the clip's 0.25.
    stage = Usd.Stage.Open(str(output))
    animation = find_animation(stage)
    blend_shapes = list(animation.GetBlendShapesAttr().Get())
    values = list(animation.GetBlendShapeWeightsAttr().Get(0.0))
    left = values[blend_shapes.index("Eyes_Left")]
    failures.check(
        abs(left - 0.5) <= TOLERANCE,
        f"the clip's own lookLeft weight survived the gaze: {left}")

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
                 tool_fixtures / "expressive_clip.usda",
                 tool_fixtures / "gazing_avatar.usda",
                 tool_fixtures / "gazing_expression_avatar.usda",
                 tool_fixtures / "gazing_clip.usda"):
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

        check_expression_overrides_arbitrate_the_face(
            options.tool, tool_fixtures, options.humanoid_map, workspace,
            failures)

        check_look_at_bake(options.tool, tool_fixtures, workspace, failures)

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
