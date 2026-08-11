#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""A recorded file onto a target rig, through two tools and nothing else.

    .bvh  ->  motion_bvh_convert  ->  semantic clip  ->  motion_retarget  ->  avatar

This is BVH-3's evaluation point (roadmap/recorded-motion-sources.md §7), and
the load-bearing word in it is **unchanged**: `motion_retarget` shipped in
v0.4.0, it knows nothing about BVH, and it is invoked here with the same flags a
`.vrma` bake uses. If the recorded path needed a special case in the retargeter,
the avatar-independent clip would not be avatar-independent -- it would be a BVH
clip that a BVH-aware consumer happens to read, and the whole reason for
stopping at the semantic layer (§5) would be gone.

Which is why the strongest check here is not "it ran". It is the invariant the
rest-pose correction exists to hold:

    a bone's world rotation *away from its own rest* is the same on both rigs

The fixture avatar is deliberately shaped so that a broken correction cannot
pass this. Its arms rest 45 degrees down where the recorded rig's rest is
straight, so `RestPoseCorrection` is a real rotation for four joints and
identity for the rest -- and a bake that silently skipped the correction would
place those four arms wrong by exactly that 45 degrees while every other bone
looked perfect. The proportions differ too, and its joints are named as a DCC
names them rather than as human bones, so nothing here can bind by coincidence.

Four more claims, each named in §7:

* the binding resolves, through a real `UsdSkelSkeletonQuery` rather than by
  reading attributes back;
* the bones that move are the bones that moved -- as a set, not a count. This
  export never rotates its toes, so a retarget that invented motion there, or
  lost the arms, changes that set;
* root motion follows the policy: body translation arrives as a displacement
  from the source's own rest, applied over the *target's* hips height;
* the same input gives the same output, byte for byte.

Running it from packaged artifacts with no build tree on the path is the
remaining item of §7's list and is a packaging test rather than this one.

`test_real_avatar.py` runs the same chain onto a released VRM. It is not a
replacement for this file and this file is not a weaker version of it: the rig
here is shaped to make one specific defect impossible to miss, and a shipped
model is shaped by whoever shipped it -- extra joints, unbound joints in the
middle of a chain, and optional human bones it simply does not have.
"""

from __future__ import annotations

import argparse
import json
import pathlib
import sys
import tempfile

from pxr import Gf, Usd, UsdSkel

# The rig reader, the failure collector and the tolerances live beside this file
# because the real-avatar test needs the same ones, and two copies of a
# quaternion chain walk are how a test starts agreeing with a defect.
from rigcheck import (DISTANCE_TOLERANCE, ROTATION_TOLERANCE, Failures, Rig,
                      quat_distance, run_tool)

RECORDED = "mocopi-mobile-arm-raise-turn.bvh"
PROFILE_ID = "mocopi-mobile-bvh-default-v1"


def check_pipeline(failures: Failures, clip_path: pathlib.Path,
                   result_path: pathlib.Path,
                   humanoid_map: dict[str, str]) -> None:
    clip_stage = Usd.Stage.Open(str(clip_path))
    result_stage = Usd.Stage.Open(str(result_path))
    if not failures.check(clip_stage is not None and result_stage is not None,
                          "the pipeline produced no openable stage"):
        return

    source = Rig(clip_stage)
    target = Rig(result_stage)

    # --- the binding resolves ----------------------------------------------
    #
    # Through the query rather than by reading attributes back: a skeleton whose
    # animation source does not actually compose still has every attribute a
    # reader would check, and resolves to its rest pose in silence.
    cache = UsdSkel.Cache()
    query = cache.GetSkelQuery(target.skeleton)
    if not failures.check(bool(query),
                          "the retargeted skeleton has no UsdSkelSkeletonQuery, "
                          "so nothing composed"):
        return
    for time in (target.times[0], target.times[len(target.times) // 2],
                 target.times[-1]):
        transforms = query.ComputeJointLocalTransforms(Usd.TimeCode(time))
        failures.check(
            transforms is not None and len(transforms) == len(target.joints),
            f"the query resolved no joint transforms at time {time}")

    failures.check(
        len(target.times) == len(source.times),
        f"the bake carries {len(target.times)} samples and the clip carries "
        f"{len(source.times)}")
    failures.check(
        target.stage.GetTimeCodesPerSecond()
        == source.stage.GetTimeCodesPerSecond(),
        "the bake changed the clip's rate")

    # --- the invariant the correction exists to hold ------------------------
    sampled = [target.times[0], target.times[len(target.times) // 3],
               target.times[2 * len(target.times) // 3], target.times[-1]]
    for bone, token in humanoid_map.items():
        if not failures.check(token in target.slot,
                              f"the avatar has no joint '{token}' for {bone}"):
            continue
        source_token = next((name for name in source.joints
                             if name.rsplit("/", 1)[-1] == bone), None)
        if source_token is None:
            continue
        for time in sampled:
            expected = source.rest_relative(source_token, time)
            actual = target.rest_relative(token, time)
            failures.check(
                quat_distance(Gf.Quatf(actual.GetReal(),
                                       Gf.Vec3f(*actual.GetImaginary())),
                              Gf.Quatf(expected.GetReal(),
                                       Gf.Vec3f(*expected.GetImaginary())))
                <= ROTATION_TOLERANCE,
                f"time {time}: {bone} on the avatar turns "
                f"{actual} away from its rest, and on the recorded rig it "
                f"turns {expected}. The two rest poses differ, so this is the "
                f"rest-pose correction disagreeing rather than the motion.")

    # --- the bones that move are the bones that moved -----------------------
    source_moving = {
        bone for bone, token in humanoid_map.items()
        if (name := next((n for n in source.joints
                          if n.rsplit("/", 1)[-1] == bone), None))
        and source.moved(name)}
    target_moving = {bone for bone, token in humanoid_map.items()
                     if target.moved(token)}
    failures.check(
        source_moving == target_moving,
        f"the clip moves {sorted(source_moving)} and the bake moves "
        f"{sorted(target_moving)}; the difference is "
        f"{sorted(source_moving ^ target_moving)}")
    # A guard on the guard: if this export turned out to move nothing, the set
    # comparison above would be two empty sets agreeing.
    failures.check(
        len(source_moving) >= 15,
        f"only {len(source_moving)} bones move in the clip, which is too few "
        f"for the set comparison above to mean anything")

    # --- root motion follows the policy -------------------------------------
    #
    # The default policy puts body translation on the hips. It arrives as a
    # displacement from the source's own rest applied over the target's hips
    # height, so the *displacement* is what the two rigs share and the absolute
    # position is deliberately not.
    hips_token = humanoid_map["hips"]
    source_hips = next(name for name in source.joints
                       if name.rsplit("/", 1)[-1] == "hips")
    source_start = source.translation(source_hips, source.times[0])
    target_start = target.translation(hips_token, target.times[0])
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
        "the recorded hips never move, so the root-motion check above compares "
        "zero against zero")
    # The absolute heights differ, which is the half that says the target rig's
    # own geometry is what the bake stands on.
    failures.check(
        abs(target_start[1] - source_start[1]) > DISTANCE_TOLERANCE,
        f"the avatar's hips start at the recorded rig's height "
        f"({target_start[1]}), so this bake did not use the target's rest")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--convert", required=True,
                        help="motion_bvh_convert")
    parser.add_argument("--retarget", required=True, help="motion_retarget")
    parser.add_argument("--corpus", type=pathlib.Path, required=True)
    parser.add_argument("--profiles", type=pathlib.Path, required=True)
    parser.add_argument("--fixtures", type=pathlib.Path, required=True)
    arguments = parser.parse_args()

    bvh = arguments.corpus / "recorded" / "redistributable" / RECORDED
    avatar = arguments.fixtures / "humanoid_avatar.usda"
    map_path = arguments.fixtures / "humanoid_avatar_map.json"
    for path in (bvh, avatar, map_path):
        if not path.exists():
            print(f"missing input: {path}", file=sys.stderr)
            return 1

    humanoid_map = json.loads(map_path.read_text(encoding="utf-8"))

    failures = Failures()
    with tempfile.TemporaryDirectory() as directory:
        work = pathlib.Path(directory)
        clip = work / "canonical.usda"
        result = work / "baked.usda"

        converted = run_tool(arguments.convert, str(bvh), "--profile",
                             PROFILE_ID, "--profile-dir",
                             str(arguments.profiles), "--output", str(clip),
                             "--quiet")
        if not failures.check(converted.returncode == 0,
                              f"motion_bvh_convert failed: {converted.stderr}"):
            return failures.report()

        # The same flags a `.vrma` bake uses. Nothing here says "bvh".
        baked = run_tool(arguments.retarget, "--avatar", str(avatar),
                         "--animation", str(clip), "--humanoid-map",
                         str(map_path), "--output", str(result), "--quiet")
        if not failures.check(
                baked.returncode == 0,
                f"motion_retarget failed on a converted recording, having "
                f"needed no change for a `.vrma`: {baked.stderr}"):
            return failures.report()

        check_pipeline(failures, clip, result, humanoid_map)

        # The same input twice, through both tools.
        clip_again = work / "canonical-again.usda"
        result_again = work / "baked-again.usda"
        run_tool(arguments.convert, str(bvh), "--profile", PROFILE_ID,
                 "--profile-dir", str(arguments.profiles), "--output",
                 str(clip_again), "--quiet")
        run_tool(arguments.retarget, "--avatar", str(avatar), "--animation",
                 str(clip_again), "--humanoid-map", str(map_path), "--output",
                 str(result_again), "--quiet")
        failures.check(clip.read_bytes() == clip_again.read_bytes(),
                       "two conversions of one recording differ")
        failures.check(
            result.read_bytes() == result_again.read_bytes(),
            "two bakes of one recording onto one avatar differ")

    return failures.report()


if __name__ == "__main__":
    raise SystemExit(main())
