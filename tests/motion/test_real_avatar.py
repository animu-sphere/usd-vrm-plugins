#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""A recorded file onto an avatar somebody actually made.

    mocopi-mobile-arm-raise-turn.bvh
        -> motion_bvh_convert  ->  semantic clip
        -> motion_retarget     ->  Seed-san.vrm

The rig is the thing under test here, not the tools. `workspace_bvh_end_to_end`
already drives this exact chain onto a fixture built so a broken rest-pose
correction cannot pass it. That fixture is sharp *and* it is ours: every joint in
it exists because a test wanted it, every bone the humanoid names is bound, and
nothing in it can be surprising. Those are the properties a released avatar does
not have, which is why the v0.7.0 release condition says **a target VRM** rather
than a rig (roadmap/current.md).

`Seed-san.vrm` is a VRM 1.0 specification sample, committed and redistributable
under its own permission flags (see the LICENSE.md beside it). *Seed-san model by
VirtualCast, Inc. — VRM Public License 1.0.* Three of its properties carry this
test, and none of them was chosen for it:

  * **128 joints, 51 of which the humanoid binds.** The other 77 are hair, a
    backpack, ropes, heels and twist joints. A bake that wrote by array position
    rather than by binding moves a ponytail, and no fixture in this repository
    has enough hair to notice.

  * **Two of those unbound joints sit *between* bound ones.**
    `forearm_twist_L/R` are on the path from the forearm to the hand, so the
    hand's rotation has to arrive *through* a joint the clip knows nothing
    about. That is the target-side of the question the converter answered on the
    source side (recorded-motion-sources.md §10, the path rule).

  * **Its humanoid is incomplete: no `upperChest`, no jaw, no eyes.** VRM 1.0
    makes `upperChest` optional and this model leaves it out, while the mocopi
    profile maps a source joint *to* it. So this clip carries a bone the rig
    cannot represent — the first time anything in this repository has.

What the missing bone costs is asserted rather than described, and it is exact:
every bone below it reproduces the source **computed as if the source's
`upperChest` had never moved**. The rotation is dropped whole rather than
redistributed onto a neighbour, and `motion_retarget` says which bone on stderr.
Whether dropping it is the right answer is a contract question with two
candidate answers — hoist it to the nearest bound ancestor, or push it onto each
nearest bound descendant — and one real avatar is not enough to choose
(recorded-motion-sources.md §10). So this is a characterisation test: a later
rule has to change this file before it changes the behaviour.

The live half of the same release condition is closed in the VMC adapter's own
end-to-end test rather than here. Its chain starts at `vmc_record`, and the root
tree adds every adapter last precisely so that nothing above one may name it.

Not checked here: anything about the model's meshes, materials or textures. This
is a rig test that happens to open a real asset, and the importer has its own
corpus for the rest.
"""

from __future__ import annotations

import argparse
import math
import pathlib
import sys
import tempfile

from pxr import Gf, Usd, UsdSkel

from rigcheck import (DISTANCE_TOLERANCE, ROTATION_TOLERANCE, Failures, Rig,
                      as_quatf, quat_distance, run_tool)

RECORDED = "mocopi-mobile-arm-raise-turn.bvh"
PROFILE_ID = "mocopi-mobile-bvh-default-v1"

# The bone this clip drives and this avatar has no joint for. Spelled out rather
# than derived so that a model change or a profile change is a failure with a
# name in it, instead of a silently emptier test.
UNBINDABLE = frozenset({"upperChest"})

# The rig has 77 joints outside its humanoid. The floor is well under that and
# far over any fixture's, so "nothing outside the humanoid moved" cannot be
# satisfied by an avatar that has nothing outside it.
MINIMUM_UNBOUND_JOINTS = 50

# Unbound joints that sit on the path between two bound ones. Named because the
# interesting failure is not that they move -- it is that a bake which ignored
# them would place the hand correctly and never touch them.
INTERMEDIATE_UNBOUND = (
    "hips/spine/chest/shoulder_L/upper_arm_L/forearm_L/forearm_twist_L",
    "hips/spine/chest/shoulder_R/upper_arm_R/forearm_R/forearm_twist_R",
)

# A dropped bone's rotation has to be big enough in this clip that its absence is
# motion rather than noise. The recorded session turns its upper chest by 11.2
# degrees at most, so this floor says "the loss is real" without pinning the
# export's exact number into a test.
SILENCED_LOSS_FLOOR_DEGREES = 5.0


def angle_between(a: Gf.Quatd, b: Gf.Quatd) -> float:
    difference = (a * b.GetInverse()).GetNormalized()
    real = min(1.0, max(-1.0, abs(difference.GetReal())))
    return math.degrees(2.0 * math.acos(real))


def bones_bound_by(stage: Usd.Stage) -> dict[str, str]:
    """The avatar's own `vrm:humanBones:*` bindings, read independently.

    `motion_retarget` reads these as plain attributes rather than through
    `VrmHumanoidAPI` (a v0.4.0 decision, so the motion layer needs no link
    against the schema bundle), and this reads them the same way for the
    opposite reason: a test that asked the tool which joints it had bound could
    not catch it binding the wrong ones.

    The carrier prim is searched for rather than named, because where the
    importer puts it is the importer's business and not this test's.
    """
    for prim in stage.Traverse():
        bound = {}
        for attribute in prim.GetAttributes():
            name = attribute.GetName()
            if not name.startswith("vrm:humanBones:"):
                continue
            joint = attribute.Get()
            if joint:
                bound[name.rsplit(":", 1)[-1]] = str(joint)
        if bound:
            return bound
    return {}


def check_binding_resolves(failures: Failures, target: Rig) -> bool:
    """Through the query, not by reading attributes back.

    A skeleton whose animation source does not compose still has every attribute
    a reader would check, and resolves to its rest pose in silence (#64).
    """
    query = UsdSkel.Cache().GetSkelQuery(target.skeleton)
    if not failures.check(
            bool(query),
            "the retargeted skeleton has no UsdSkelSkeletonQuery, so nothing "
            "composed"):
        return False
    for time in (target.times[0], target.times[len(target.times) // 2],
                 target.times[-1]):
        transforms = query.ComputeJointLocalTransforms(Usd.TimeCode(time))
        failures.check(
            transforms is not None and len(transforms) == len(target.joints),
            f"the query resolved no joint transforms at time {time}")
    return True


def check_motion_the_rig_can_carry(failures: Failures, source: Rig, target: Rig,
                                  bound: dict[str, str],
                                  sampled: list[float]) -> None:
    """One expectation covers every bone.

    The source's motion with the bones this rig has no joint for held at their
    rest. For a fully bound chain that is just the source's motion, so this is
    the same invariant `workspace_bvh_end_to_end` asserts -- and here it reaches
    the hands *through* two unbound twist joints, which is the half a fixture
    cannot check.
    """
    checked = 0
    for bone, token in sorted(bound.items()):
        source_token = source.find_leaf(bone)
        if source_token is None:
            continue
        if not failures.check(
                token in target.slot,
                f"the avatar binds {bone} to '{token}', which its own skeleton "
                f"does not list"):
            continue
        checked += 1
        for time in sampled:
            expected = source.rest_relative(source_token, time, UNBINDABLE)
            actual = target.rest_relative(token, time)
            failures.check(
                quat_distance(as_quatf(actual), as_quatf(expected))
                <= ROTATION_TOLERANCE,
                f"time {time}: {bone} turns {actual} away from its rest on the "
                f"avatar and {expected} on the recorded rig. The two rest poses "
                f"differ, so this is the rest-pose correction disagreeing "
                f"rather than the motion.")
    failures.check(
        checked >= 15,
        f"only {checked} bones were compared, which is too few for the "
        f"invariant above to mean anything")


def check_the_loss_is_the_whole_rotation(failures: Failures, source: Rig,
                                         target: Rig, bound: dict[str, str],
                                         sampled: list[float]) -> None:
    """Silencing the bone is the right expectation only if not silencing is wrong.

    Otherwise the check above would pass just as happily against a bake that had
    quietly redistributed the rotation onto a neighbour, which is one of the two
    answers the contract question is between.
    """
    worst = 0.0
    for bone, token in sorted(bound.items()):
        source_token = source.find_leaf(bone)
        if source_token is None:
            continue
        if not any(source.leaf(name) in UNBINDABLE
                   for name in source.chain(source_token)):
            continue
        for time in sampled:
            worst = max(worst, angle_between(
                target.rest_relative(token, time),
                source.rest_relative(source_token, time)))
    failures.check(
        worst >= SILENCED_LOSS_FLOOR_DEGREES,
        f"dropping {sorted(UNBINDABLE)} costs at most {worst:.3f} degrees "
        f"anywhere below it, which is too little for the silenced expectation "
        f"to be distinguishable from the plain one. Either this clip stopped "
        f"moving that bone or the bake started redistributing it — and the "
        f"second is a contract change, not a tolerance.")


def check_nothing_else_moves(failures: Failures, target: Rig,
                             bound: dict[str, str]) -> None:
    unbound = [token for token in target.joints
               if token not in set(bound.values())]
    failures.check(
        len(unbound) >= MINIMUM_UNBOUND_JOINTS,
        f"this avatar has only {len(unbound)} joints outside its humanoid, so "
        f"the check below proves little")
    moved = sorted(token for token in unbound if target.moved(token))
    failures.check(
        not moved,
        f"the bake moves {len(moved)} joint(s) no human bone is bound to: "
        f"{moved[:8]}. Hair and luggage do not follow a humanoid clip; a bake "
        f"that writes by array position does.")
    for token in INTERMEDIATE_UNBOUND:
        if token in target.slot:
            failures.check(
                not target.moved(token),
                f"'{token}' moved. It sits between two bound joints and no "
                f"human bone names it, so the hand's rotation has to pass "
                f"through it rather than into it.")


def check_the_avatar_stands_on_its_own_geometry(
        failures: Failures, source: Rig, target: Rig, bound: dict[str, str],
        sampled: list[float]) -> None:
    """v0.4.0 decided root motion carries a delta rather than a height.

    This rig is the first target where that is worth asserting: its hips rest at
    0.796 m and the recorded rig's at 0.960 m, so a bake that passed the source's
    absolute position through would sink the model by 16 cm.
    """
    hips_token = bound["hips"]
    source_hips = source.find_leaf("hips")
    source_start = source.translation(source_hips, source.times[0])
    target_start = target.translation(hips_token, target.times[0])
    rest_hips = target.skeleton.GetRestTransformsAttr().Get()[
        target.slot[hips_token]].ExtractTranslation()
    failures.check(
        all(abs(a - b) <= DISTANCE_TOLERANCE
            for a, b in zip(target_start, rest_hips)),
        f"the bake starts the avatar's hips at {tuple(target_start)} and the "
        f"model rests them at {tuple(rest_hips)}. This session starts at the "
        f"recorded rig's own rest, so the target's first frame is the target's "
        f"own rest.")
    failures.check(
        abs(target_start[1] - source_start[1]) > DISTANCE_TOLERANCE,
        f"the avatar's hips start at the recorded rig's height "
        f"({target_start[1]}), so this bake did not stand on the target's rest")
    moved_any = False
    for time in sampled:
        source_delta = source.translation(source_hips, time) - source_start
        target_delta = target.translation(hips_token, time) - target_start
        failures.check(
            all(abs(a - b) <= DISTANCE_TOLERANCE
                for a, b in zip(source_delta, target_delta)),
            f"time {time}: the avatar's hips moved {tuple(target_delta)} from "
            f"where they started and the clip's moved {tuple(source_delta)}")
        moved_any = moved_any or any(abs(v) > DISTANCE_TOLERANCE
                                     for v in source_delta)
    failures.check(
        moved_any,
        "the recorded hips never move, so the displacement check above compares "
        "zero against zero")


def check_bake(failures: Failures, clip_path: pathlib.Path,
               result_path: pathlib.Path, bound: dict[str, str],
               report: str) -> None:
    clip_stage = Usd.Stage.Open(str(clip_path))
    result_stage = Usd.Stage.Open(str(result_path))
    if not failures.check(
            clip_stage is not None and result_stage is not None,
            "the pipeline produced no openable stage"):
        return

    source = Rig(clip_stage)
    target = Rig(result_stage)
    if not check_binding_resolves(failures, target):
        return

    # The bone the rig cannot represent is the one this file is written around,
    # so the clip is held to carrying exactly it -- no more, and not none.
    silenced = {source.leaf(token) for token in source.joints} - set(bound)
    if failures.check(
            silenced == set(UNBINDABLE),
            f"this clip carries {sorted(silenced)} that the avatar does not "
            f"bind, and this test is written around {sorted(UNBINDABLE)}. The "
            f"characterisation below has moved and has to be re-derived rather "
            f"than re-tuned."):
        for bone in sorted(silenced):
            failures.check(
                bone in report,
                f"{bone} is in the clip and not on the avatar, and "
                f"motion_retarget did not name it. A rotation that goes "
                f"nowhere has to be reported or it is lost in silence.\n"
                f"{report}")

    sampled = [target.times[0], target.times[len(target.times) // 3],
               target.times[2 * len(target.times) // 3], target.times[-1]]
    check_motion_the_rig_can_carry(failures, source, target, bound, sampled)
    check_the_loss_is_the_whole_rotation(failures, source, target, bound,
                                        sampled)
    check_nothing_else_moves(failures, target, bound)
    source_moving = {bone for bone in bound
                     if (token := source.find_leaf(bone))
                     and source.moved(token)}
    target_moving = {bone for bone, token in bound.items()
                     if target.moved(token)}
    failures.check(
        source_moving == target_moving,
        f"the clip moves {sorted(source_moving)} and the bake moves "
        f"{sorted(target_moving)}; the difference is "
        f"{sorted(source_moving ^ target_moving)}")
    failures.check(
        len(source_moving) >= 15,
        f"only {len(source_moving)} bones move in the clip, which is too few "
        f"for the set comparison above to mean anything")
    check_the_avatar_stands_on_its_own_geometry(failures, source, target, bound,
                                               sampled)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--convert", required=True, help="motion_bvh_convert")
    parser.add_argument("--retarget", required=True, help="motion_retarget")
    parser.add_argument("--avatar", type=pathlib.Path, required=True,
                        help="a real .vrm; needs usdVrmFileFormat registered")
    parser.add_argument("--corpus", type=pathlib.Path, required=True)
    parser.add_argument("--profiles", type=pathlib.Path, required=True)
    arguments = parser.parse_args()

    bvh = arguments.corpus / "recorded" / "redistributable" / RECORDED
    for path in (arguments.avatar, bvh, arguments.profiles):
        if not path.exists():
            print(f"missing input: {path}", file=sys.stderr)
            return 1

    avatar_stage = Usd.Stage.Open(str(arguments.avatar))
    if avatar_stage is None:
        print(f"{arguments.avatar} did not open. A .vrm needs "
              f"usdVrmFileFormat on PXR_PLUGINPATH_NAME.", file=sys.stderr)
        return 1

    failures = Failures()
    bound = bones_bound_by(avatar_stage)
    if not failures.check(
            len(bound) >= 20,
            f"{arguments.avatar.name} carries {len(bound)} vrm:humanBones "
            f"bindings. Either it is not a humanoid or the importer did not "
            f"author them, and every claim here reads the rig through them."):
        return failures.report()

    with tempfile.TemporaryDirectory(prefix="real-avatar-") as directory:
        work = pathlib.Path(directory)
        clip = work / "recorded.usda"
        baked = work / "baked.usda"

        converted = run_tool(
            arguments.convert, str(bvh), "--profile", PROFILE_ID,
            "--profile-dir", str(arguments.profiles), "--output", str(clip),
            "--quiet")
        if not failures.check(converted.returncode == 0,
                              f"motion_bvh_convert failed: {converted.stderr}"):
            return failures.report()

        # No --quiet: the report is what names the bone that went nowhere. The
        # flags are otherwise the ones a `.vrma` bake onto a fixture uses --
        # nothing here says "bvh", and nothing says "vrm" either.
        result = run_tool(
            arguments.retarget, "--avatar", str(arguments.avatar),
            "--animation", str(clip), "--output", str(baked))
        if not failures.check(
                result.returncode == 0,
                f"motion_retarget failed on a real avatar, having needed no "
                f"change for a fixture rig: {result.stderr}"):
            return failures.report()

        check_bake(failures, clip, baked, bound, result.stderr)

        # The same input twice, onto a model with 128 joints and a reference to
        # resolve.
        baked_again = work / "baked-again.usda"
        run_tool(arguments.retarget, "--avatar", str(arguments.avatar),
                 "--animation", str(clip), "--output", str(baked_again),
                 "--quiet")
        failures.check(
            baked.read_bytes() == baked_again.read_bytes(),
            "two bakes of one recording onto one real avatar differ")

    return failures.report()


if __name__ == "__main__":
    raise SystemExit(main())
