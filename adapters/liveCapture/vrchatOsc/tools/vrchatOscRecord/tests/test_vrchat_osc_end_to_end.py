#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""A VRChat OSC session all the way to a rig, through tools this adapter cannot touch.

    capture.vrchatoscpackets
        -> vrchat_osc_record --inspect --export-trace   (this adapter, + a solve)
        -> motion_capture                               (unchanged)
        -> motion_retarget                              (unchanged)
        -> a target rig, resolved through UsdSkelSkeletonQuery

Both siblings have had this test since v0.7.0 and this adapter deliberately did
not: the chain was missing a link, because a tracker source produces no pose and
nothing had decided where the humanoid solve lived. VRC-5 decided (it is
`libs/motionTracking`, on the tool's side of WORKSPACE.md §2) and VRC-6 is the
tool that calls it. This file is the link closing.

The claim is not that each tool works -- each has its own tests -- but that the
chain closes with **no edge between the product and this adapter** (§2).
`motion_capture` is a member of the aggregate product and this adapter is
deliberately excluded from it, so the only thing that can pass between them is a
file. If that file ever stops being one the product can read, this test fails and
every other test in the repository still passes: the two halves are only ever
connected here.

That is also why the test lives with the adapter rather than with the product,
and why it is a third file rather than a shared driver: §2 forbids an adapter
from knowing its siblings exist, and a shared end-to-end driver would be that
knowledge in a test directory instead of a link line.

## What this leg checks that neither sibling's can

A pose source drives whatever bones its sender carries. A **tracker** source
drives the bones an operator's assignment reached and *no others* -- that is
VRC-5's stopping point stated as a value: an observed orientation becomes a
bone's rotation, and a joint nobody observed stays at rest rather than being
estimated, because estimating it is IK and IK needs limb lengths this layer does
not have.

So the assertion is a partition of the rig. Four joints move, fourteen do not,
and the fourteen do not move *exactly* -- an unauthored bone is the same
computation at every sample, so its rotation is bit-for-bit constant rather than
constant within a tolerance. A rig whose every joint was driven could not tell
"the session arrived" from "something wrote noise into the skeleton".
"""

from __future__ import annotations

import argparse
import pathlib
import subprocess
import sys
import tempfile

from pxr import Gf, Usd, UsdSkel

# The committed capture written for this leg. Every other fixture in the corpus
# drifts a millimetre and a quarter of a degree per frame, which is enough to
# prove a decoder is not returning its defaults and too little to survive a
# retarget's rest-pose correction as a visible rotation -- so a session that
# arrived perfectly and one that did not arrive at all would read the same here.
# `rig-motion` walks half a metre, turns its head most of a right angle and
# rolls its two feet in opposite directions.
CAPTURE = "rig-motion"

# The operator's statement, spelled in full. Nothing in this repository may
# derive it -- a tracker index is not a body role -- and a test that derived one
# would be the automatic assignment VRC-4a deliberately does not build.
ASSIGNMENT = "1=hips 2=leftFoot 3=rightFoot head=head"

# The rig joints those four regions reach, and the only ones that may move.
# Named rather than counted, because four joints moving is not the claim --
# *these* four moving is. The feet are at the end of leg chains whose thighs and
# shins nothing observes, which is what makes the partition below interesting:
# a solve that guessed at the joints between a hip and a foot would move them.
EXPECTED_MOVING = {
    "Root/Pelvis",
    "Root/Pelvis/SpineA/ChestA/NeckA/HeadA",
    "Root/Pelvis/ThighL/ShinL/FootL",
    "Root/Pelvis/ThighR/ShinR/FootR",
}

# Two rotations count as the same when their quaternions agree this closely. The
# retarget composes a rest-pose correction, so a driven joint comes back as a
# product of rotations rather than as the value the trace carried.
IDENTICAL = 1e-5


def fail(message: str) -> None:
    print(f"FAIL: {message}", file=sys.stderr)
    raise SystemExit(1)


def run(tool: pathlib.Path, *arguments: str) -> str:
    result = subprocess.run([str(tool), *arguments], text=True,
                            encoding="utf-8", errors="replace",
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode != 0:
        fail(f"{pathlib.Path(tool).name} {' '.join(arguments)} exited "
             f"{result.returncode}\n{result.stderr}")
    return result.stdout


def same_rotation(a: Gf.Quatd, b: Gf.Quatd) -> float:
    """How far apart two rotations are, as `q` and `-q` are one rotation."""
    dot = (a.GetReal() * b.GetReal()
           + Gf.Dot(a.GetImaginary(), b.GetImaginary()))
    return abs(abs(dot) - 1.0)


def drive(options, avatar: pathlib.Path, expected: set[str],
          minimum_joints: int) -> None:
    """The whole chain onto one rig, from the capture to the joints that moved."""
    capture = options.corpus / "generated" / f"{CAPTURE}.vrchatoscpackets"
    if not capture.is_file():
        fail(f"{capture} is missing")

    with tempfile.TemporaryDirectory(prefix="vrchat-osc-e2e-") as scratch:
        directory = pathlib.Path(scratch)
        trace = directory / "session.trace"
        clip = directory / "session.usda"
        baked = directory / "baked.usda"

        # 1. The adapter's half ends here, at a file with no VRChat in it -- and
        #    with no tracker in it either, which is the part VRC-5 added: a
        #    trace carries poses, and a consumer that cannot tell this one from
        #    a clip-driven one is reading it correctly.
        run(options.record_tool, "--inspect", str(capture),
            "--export-trace", str(trace), "--assign", ASSIGNMENT, "--quiet")

        # 2. The product's replay tool, given no argument that names an adapter,
        #    a protocol, a tracker or a socket. It is reading a recording like
        #    any other.
        run(options.capture_tool, "--trace", str(trace), "--output", str(clip),
            "--quiet")

        # 3. The offline retarget, unchanged since v0.4.0, onto a rig whose
        #    joint names disagree with the humanoid vocabulary -- the binding
        #    comes from the avatar's `vrm:humanBones:*` attributes.
        run(options.retarget_tool, "--avatar", str(avatar),
            "--animation", str(clip), "--output", str(baked),
            "--root-motion", "hips")

        # Both stay in locals: the query holds no strong reference back, so a
        # temporary would be released out from under it.
        stage = Usd.Stage.Open(str(baked))
        skeletons = [UsdSkel.Skeleton(prim) for prim in stage.Traverse()
                     if UsdSkel.Skeleton(prim)]
        if len(skeletons) != 1:
            fail(f"expected one skeleton in {baked.name}, found "
                 f"{len(skeletons)}")
        query = UsdSkel.Cache().GetSkelQuery(skeletons[0])
        if not query:
            fail(f"{baked.name} yields no UsdSkel skeleton query: the "
                 f"animation did not bind")

        animations = [UsdSkel.Animation(prim) for prim in stage.Traverse()
                      if UsdSkel.Animation(prim)]
        if len(animations) != 1:
            fail(f"expected one animation in {baked.name}, found "
                 f"{len(animations)}")
        times = animations[0].GetRotationsAttr().GetTimeSamples()
        if len(times) != 12:
            fail(f"expected the capture's twelve frames, found {len(times)} "
                 f"time sample(s)")

        def rotations(time) -> list:
            transforms = query.ComputeJointLocalTransforms(Usd.TimeCode(time))
            if not transforms:
                fail(f"UsdSkel resolved no joint transforms at {time}: the "
                     f"animation is bound but does not drive the rig")
            return [transform.ExtractRotationQuat()
                    for transform in transforms]

        joints = [str(joint) for joint in query.GetJointOrder()]
        if len(joints) < minimum_joints:
            fail(f"{avatar.name} resolves {len(joints)} joints and this "
                 f"expectation was measured against at least {minimum_joints}; "
                 f"the rig changed, so the set below has to be re-measured "
                 f"rather than re-tuned")

        reference = rotations(times[0])
        travel = {joint: 0.0 for joint in joints}
        for time in times[1:]:
            for index, (before, after) in enumerate(
                    zip(reference, rotations(time))):
                travel[joints[index]] = max(travel[joints[index]],
                                            same_rotation(before, after))

        moved = {joint for joint, distance in travel.items()
                 if distance > IDENTICAL}
        if moved != expected:
            fail(f"the wrong joints moved on {avatar.name}.\n"
                 f"  expected: {sorted(expected)}\n"
                 f"  moved:    {sorted(moved)}\n"
                 f"  travel:   {travel}")

        # The stopping point, asserted rather than implied. An unobserved joint
        # is not merely *within tolerance* of its rest pose -- nothing authored
        # it, so the retarget computes the same product at every sample and the
        # result is identical. A tolerance here would hide a solve that had
        # begun estimating the joints between a hip and a foot, which is exactly
        # the IK this layer refuses to do.
        estimated = {joint for joint in joints
                     if joint not in expected and travel[joint] != 0.0}
        if estimated:
            fail(f"joints nothing observed did not hold their rest pose "
                 f"exactly: {sorted(estimated)}")

    print(f"vrchat_osc_record -> motion_capture -> motion_retarget: {CAPTURE} "
          f"reaches {avatar.name}, {len(moved)} of {len(joints)} joint(s) "
          f"moving as the assignment placed them and "
          f"{len(joints) - len(moved)} holding rest exactly")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--record-tool", required=True, type=pathlib.Path)
    parser.add_argument("--capture-tool", required=True, type=pathlib.Path)
    parser.add_argument("--retarget-tool", required=True, type=pathlib.Path)
    parser.add_argument("--corpus", required=True, type=pathlib.Path)
    parser.add_argument("--avatar", required=True, type=pathlib.Path)
    options = parser.parse_args()

    # One rig, where both siblings drive two. The second there is `Seed-san.vrm`
    # and it buys them the other 125 joints -- "three moved and the hair did
    # not". This leg already makes that claim on its own fixture and makes it
    # more strongly: fourteen unobserved joints hold rest *exactly*, including
    # four that sit between a driven hip and a driven foot, which is a case a
    # released avatar's hair strands do not test. A released VRM is still worth
    # adding here, and what it would add is the release condition's own wording
    # rather than a sharper check -- see the roadmap's cross-source entry.
    drive(options, options.avatar, EXPECTED_MOVING, minimum_joints=0)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
