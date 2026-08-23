#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""A mocopi session all the way to a rig, through tools this adapter cannot touch.

    capture.mocopipackets
        -> mocopi_record --inspect --export-trace   (this adapter)
        -> motion_capture                           (unchanged)
        -> motion_retarget                          (unchanged)
        -> a target rig, resolved through UsdSkelSkeletonQuery

The claim is not that each tool works -- each has its own tests -- but that the
chain closes with **no edge between the product and this adapter**
(WORKSPACE.md §2). `motion_capture` is a member of the aggregate product and
this adapter is deliberately excluded from it, so the only thing that can pass
between them is a file. If that file ever stops being one the product can read,
this test fails and every other test in the repository still passes: the two
halves are only ever connected here.

That is also why the test lives with the adapter rather than with the product,
and it is the sibling's arrangement for the sibling's reasons
(`test_vmc_end_to_end.py`). What differs is the *claim*, and the difference is a
property of this corpus rather than a weaker test.

## Two sessions compared, where the sibling compares two instants

The VMC corpus has a capture that moves -- an arm raise across five frames -- so
that test can ask which joints moved during a session. **No committed mocopi
capture moves.** Every one of them is a held pose, because they were generated
from a measured grammar to pin a decode path: `arms-lowered-60hz` carries one
non-identity rotation on each upper arm and repeats it, and
`neutral-standing-60hz` carries identity everywhere. A capture that moves needs
a device, and a device is exactly what the corpus policy keeps out of CI.

So the question asked here is the same question across files: bake both sessions
onto one rig and require the joints that differ between them to be the joints
the two sessions differ by. It is not the weaker half of the sibling's check --
a session where every joint differed would fail this as surely as it would fail
that.

**A set of joint names is not enough here, and that is this device's own
finding.** `arms-lowered-60hz` rotates *both* upper arms, so swapping the two
sides anywhere in the path leaves the differing set identical: the same two
joints differ, in the same two amounts. `SkeletonMap.h` measured exactly this
one layer down -- a bilaterally symmetric rig cannot be asked which arm moved,
and what catches a swap is asking which arm is *where*. So on the fixture rig,
whose rest pose is symmetric by construction and whose arms sit at +X and -X,
the two rotations are checked for being mirror images **and** for which side
carries which sign. On a released avatar they are not: its rest pose is its
own, so the mirror is not a property the retarget preserves and asserting one
would be pinning that model rather than this path.

The last leg is a value check rather than a load check. A clip that binds and
then holds the rest pose opens without error and animates nothing (#64), so what
is asserted is which joints UsdSkel resolves as differing -- named, not counted.

It runs over two rigs where the build tree has both. The fixture avatar beside
this file is hand-authored and bilateral on purpose; the second is
`Seed-san.vrm`, a released VRM 1.0 model, which is what the v0.7.0 release
condition asks for by name. The first is the sharper test and the second is the
honest one -- a real model has 126 joints no human bone names, and it is missing
a bone this device sends.
"""

from __future__ import annotations

import argparse
import pathlib
import subprocess
import sys
import tempfile

from pxr import Gf, Usd, UsdSkel

# The two committed captures that differ, and the frames each one holds. Both
# numbers are asserted: a chain that dropped a frame somewhere would still put
# the right rotation on the right joint.
SESSIONS = {"arms-lowered-60hz": 3, "neutral-standing-60hz": 5}

# The joints those two sessions differ by on the fixture rig -- the two upper
# arms, which is where `arms-lowered-60hz` carries its rotation. Spelled as joint
# paths because that is what UsdSkel calls them, and named rather than counted
# because two joints differing is not the claim: *these* two differing is.
LEFT_ARM = "Root/Hip/Spine1/Chest1/ChestUp/ClavL/ArmUpL"
RIGHT_ARM = "Root/Hip/Spine1/Chest1/ChestUp/ClavR/ArmUpR"
EXPECTED_DIFFERING = {LEFT_ARM, RIGHT_ARM}

# Which side carries which sign, measured on the fixture rig: the arm at +X --
# the body's left on this device and in canonical motion alike -- comes back with
# a negative third imaginary component, and the arm at -X with a positive one.
#
# This is the assertion a side swap fails and the set above does not, because
# both arms rotate. It is stated as a sign rather than a value so that it pins
# the basis and not the fixture's proportions.
EXPECTED_ARM_SIGNS = {LEFT_ARM: -1.0, RIGHT_ARM: 1.0}

# The same two bones on `Seed-san.vrm`, when this build tree has the importer to
# open it with. What it buys here is the other 126 joints: this model carries
# hair, a backpack, ropes and twist joints that no human bone names, so the set
# comparison stops being "two joints differ" and becomes "two differ and a
# hundred and twenty-six do not".
#
# *Seed-san model by VirtualCast, Inc. — VRM Public License 1.0* (its
# `creditNotation` is `required`; see the LICENSE.md beside the model).
EXPECTED_DIFFERING_VRM = {
    "hips/spine/chest/shoulder_L/upper_arm_L",
    "hips/spine/chest/shoulder_R/upper_arm_R",
}
# Below this the model is not the one this expectation was measured against.
MINIMUM_VRM_JOINTS = 100

# The bone this device sends that a released avatar does not have. Measured
# rather than predicted: `motion_retarget` names it on stderr and drops the
# rotation whole rather than redistributing it, which is the contract question
# recorded in roadmap/recorded-motion-sources.md §10. Asserted here so that the
# day it stops being dropped is a failing test rather than a silent improvement.
VRM_UNMAPPED_BONE = "upperChest"

# Two rotations count as the same when their quaternions agree this closely. The
# retarget composes a rest-pose correction, so an unaffected joint comes back as
# a product of rotations rather than as a literal identity.
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
    return result.stderr


def same_rotation(a: Gf.Quatd, b: Gf.Quatd) -> bool:
    """q and -q are one rotation, so compare the axis-angle pair, not the four."""
    dot = (a.GetReal() * b.GetReal()
           + Gf.Dot(a.GetImaginary(), b.GetImaginary()))
    return abs(abs(dot) - 1.0) <= IDENTICAL


def bake(options, avatar: pathlib.Path, capture: pathlib.Path,
         directory: pathlib.Path, name: str) -> tuple[pathlib.Path, str]:
    """One session, from committed datagrams to a baked stage."""
    trace = directory / f"{name}.trace"
    clip = directory / f"{name}.usda"
    baked = directory / f"{name}-baked.usda"

    # 1. The adapter's half ends here, at a file with no mocopi in it. It runs
    #    against the capture rather than a socket because a recording on this
    #    tool decodes nothing at all -- see TraceExport.h.
    run(options.record_tool, "--inspect", str(capture),
        "--export-trace", str(trace), "--quiet")

    # 2. The product's replay tool, given no argument that names an adapter, a
    #    protocol, or a socket. It is reading a recording like any other.
    run(options.capture_tool, "--trace", str(trace), "--output", str(clip),
        "--quiet")

    # 3. The offline retarget, unchanged since v0.4.0, onto a rig whose joint
    #    names disagree with the humanoid vocabulary -- the binding comes from
    #    the avatar's `vrm:humanBones:*` attributes. A `.vrm` avatar takes the
    #    identical command; only usdVrmFileFormat being registered makes it
    #    openable, and that is the environment's job.
    warnings = run(options.retarget_tool, "--avatar", str(avatar),
                   "--animation", str(clip), "--output", str(baked),
                   "--root-motion", "hips")
    return baked, warnings


def resolve(baked: pathlib.Path, frames: int,
            minimum_joints: int) -> tuple[list[str], list]:
    """The joint order and each joint's local rotation at the first sample."""
    # Both stay in locals: the query holds no strong reference back, so a
    # temporary would be released out from under it.
    stage = Usd.Stage.Open(str(baked))
    skeletons = [UsdSkel.Skeleton(prim) for prim in stage.Traverse()
                 if UsdSkel.Skeleton(prim)]
    if len(skeletons) != 1:
        fail(f"expected one skeleton in {baked.name}, found {len(skeletons)}")
    query = UsdSkel.Cache().GetSkelQuery(skeletons[0])
    if not query:
        fail(f"{baked.name} yields no UsdSkel skeleton query: the animation "
             f"did not bind")

    animations = [UsdSkel.Animation(prim) for prim in stage.Traverse()
                  if UsdSkel.Animation(prim)]
    if len(animations) != 1:
        fail(f"expected one animation in {baked.name}, found "
             f"{len(animations)}")
    times = animations[0].GetRotationsAttr().GetTimeSamples()
    if len(times) != frames:
        fail(f"{baked.name} carries {len(times)} time sample(s) and the "
             f"session delivered {frames} frame(s)")

    joints = [str(joint) for joint in query.GetJointOrder()]
    if len(joints) < minimum_joints:
        fail(f"{baked.name} resolves {len(joints)} joints and this expectation "
             f"was measured against at least {minimum_joints}; the rig "
             f"changed, so the sets below have to be re-measured rather than "
             f"re-tuned")

    def transforms_at(time) -> list:
        transforms = query.ComputeJointLocalTransforms(Usd.TimeCode(time))
        if not transforms:
            fail(f"UsdSkel resolved no joint transforms at {time}: the "
                 f"animation is bound but does not drive the rig")
        return transforms

    def rotations(time) -> list:
        return [transform.ExtractRotationQuat()
                for transform in transforms_at(time)]

    first = rotations(times[0])

    # Where the body went, resolved off the same query as the rotations. The
    # retargeter was given `--root-motion hips`, so the clip's root motion lands
    # on the joint bound to `HumanBone::Hips` and this is the only joint whose
    # translation a session can change. It is the end of the path the root/hips
    # record opened: a hips translation the device sent, composed into a
    # `RootMotion` by the assembler, written to a trace, replayed by a product
    # tool that knows no protocol, and baked onto a rig by one that knows no
    # adapter (MOTION_CONTRACT.md, "Root and hips").
    # The joint is found the way the retargeter found it -- through the
    # avatar's own `vrm:humanBones:hips` -- and not as "the rig's root". This
    # rig has a `Root` above its `Hip`, which is the arrangement that makes the
    # distinction matter: a test that read index 0 would resolve an identity
    # transform for every session and report that nothing ever travels.
    bindings = [prim.GetAttribute("vrm:humanBones:hips")
                for prim in stage.Traverse()
                if prim.HasAttribute("vrm:humanBones:hips")]
    if len(bindings) != 1:
        fail(f"{baked.name} carries {len(bindings)} hips binding(s); the bake "
             f"references the avatar, so exactly one should compose through")
    hips_path = str(bindings[0].Get())
    if hips_path not in joints:
        fail(f"{baked.name} binds hips to {hips_path!r}, which the resolved "
             f"joint order does not contain")
    hips_index = joints.index(hips_path)
    travel = [transforms_at(time)[hips_index].ExtractTranslation()
              for time in times]

    # Every committed mocopi capture is a *held* pose, so nothing may move
    # within a session. This is asserted rather than assumed because the whole
    # shape of this test depends on it: the day a capture that moves is
    # committed -- a `BVH Sender` file, or a redistributable device session --
    # this fails, and the right response is to ask the sibling's question of
    # that capture rather than to widen this one.
    for time in times[1:]:
        later = rotations(time)
        moved = [joints[index] for index, (before, after)
                 in enumerate(zip(first, later)) if not same_rotation(before,
                                                                     after)]
        if moved:
            fail(f"{baked.name} moves within the session, at {time}: {moved}. "
                 f"No committed mocopi capture moves, so this test compares two "
                 f"sessions; a capture that moves wants the sibling's check.")
    return joints, first, travel


def check_sides(joints: list[str], lowered: list, signs: dict[str, float],
                avatar: pathlib.Path) -> None:
    """Which arm is where, which a set of joint names cannot say.

    A quaternion and its negation are one rotation, so the sign of a component
    is only meaningful once the scalar part is fixed: both arms come back with a
    positive scalar here, and the check flips the pair when they do not rather
    than reading a sign off a value that was free to be written either way.
    """
    for joint, expected in signs.items():
        rotation = lowered[joints.index(joint)]
        component = rotation.GetImaginary()[2]
        if rotation.GetReal() < 0.0:
            component = -component
        if abs(component) < 0.5:
            fail(f"{avatar.name}: {joint} carries {rotation}, which is not the "
                 f"lowered arm this session drives")
        if component * expected < 0.0:
            fail(f"{avatar.name}: {joint} rotated the way the other arm should. "
                 f"Both arms move in this session, so the set of moving joints "
                 f"cannot tell a side swap from a correct map -- this can, and "
                 f"it says the sides are crossed somewhere between the joint "
                 f"map and the rig.\n  {joint}: {rotation}")

    left, right = (lowered[joints.index(joint)] for joint in signs)
    if (abs(left.GetReal() - right.GetReal()) > IDENTICAL
            or abs(left.GetImaginary()[2] + right.GetImaginary()[2])
            > IDENTICAL):
        fail(f"{avatar.name}: the two arms are not mirror images of each other "
             f"on a rig whose rest pose is symmetric: {left} and {right}")


def drive(options, avatar: pathlib.Path, expected: set[str],
          minimum_joints: int, expect_unmapped: bool,
          signs: dict[str, float] | None = None) -> tuple[int, str]:
    """Both sessions onto one rig, and the joints they differ by."""
    baked = {}
    warnings = ""
    with tempfile.TemporaryDirectory(prefix="mocopi-e2e-") as scratch:
        directory = pathlib.Path(scratch)
        for name, frames in SESSIONS.items():
            capture = options.corpus / f"{name}.mocopipackets"
            if not capture.is_file():
                fail(f"{capture} is missing")
            stage, said = bake(options, avatar, capture, directory, name)
            warnings += said
            baked[name] = resolve(stage, frames, minimum_joints)

    (joints, lowered, travelled), (other, neutral, held) = (
        baked["arms-lowered-60hz"], baked["neutral-standing-60hz"])
    if joints != other:
        fail("the two bakes resolved different joint orders, so they cannot be "
             "compared joint by joint")

    # The body's placement, reaching the avatar. The two sessions must disagree
    # about this and they are built to: one moves its root every frame and the
    # other restates rest, so a check that passed on both would be measuring
    # neither. `arms-lowered-60hz` steps 0.01 down and 0.02 along +Z per frame,
    # which is the generator's number rather than one measured off this bake.
    #
    # It is asserted as *movement* rather than against those figures: the
    # retargeter maps a source delta onto this rig, so the metres depend on the
    # avatar and pinning them here would make one rig's proportions a property
    # of the pipeline.
    if travelled[0] == travelled[-1]:
        fail(f"{avatar.name}: the hips did not move across a session that "
             f"translates its root every frame, so the body's placement stopped "
             f"somewhere between the trace and the bake: {travelled[0]}")
    if held[0] != held[-1]:
        fail(f"{avatar.name}: the hips moved across a session that restates its "
             f"root every frame, so something downstream is manufacturing "
             f"travel: {held[0]} -> {held[-1]}")

    differing = {joints[index]
                 for index, (a, b) in enumerate(zip(lowered, neutral))
                 if not same_rotation(a, b)}
    if differing != expected:
        fail(f"the wrong joints differ on {avatar.name}.\n"
             f"  expected: {sorted(expected)}\n"
             f"  differ:   {sorted(differing)}")

    if signs is not None:
        check_sides(joints, lowered, signs, avatar)

    # A bone this device sends and a released humanoid does not have. The
    # fixture rig binds all 22, so the same run must *not* warn there -- which
    # is what makes the warning a fact about the avatar rather than about the
    # clip.
    said_unmapped = VRM_UNMAPPED_BONE in warnings
    if said_unmapped != expect_unmapped:
        # Built before the f-string rather than inside it: a replacement field
        # that spans a line needs PEP 701 and so Python 3.12, and this file is
        # run by whatever interpreter a developer's build tree found. CI pins
        # 3.13 and would never have caught it.
        said = "warned about" if said_unmapped else "did not warn about"
        rig = ("has no such bone" if expect_unmapped
               else "binds every bone this device sends")
        fail(f"{avatar.name}: motion_retarget {said} an unmapped "
             f"{VRM_UNMAPPED_BONE}, and this rig {rig}\n{warnings}")

    step = travelled[-1] - travelled[0]
    print(f"mocopi_record -> motion_capture -> motion_retarget: the two "
          f"committed sessions reach {avatar.name} and differ at "
          f"{len(differing)} of {len(joints)} joint(s), on the bones they were "
          f"recorded to differ by; the travelling session moves its hips by "
          f"{step} and the held one does not move at all")
    return len(joints), warnings


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--record-tool", required=True, type=pathlib.Path)
    parser.add_argument("--capture-tool", required=True, type=pathlib.Path)
    parser.add_argument("--retarget-tool", required=True, type=pathlib.Path)
    parser.add_argument("--corpus", required=True, type=pathlib.Path)
    parser.add_argument("--avatar", required=True, type=pathlib.Path)
    # Optional, because a standalone build of this adapter has no importer
    # bundle to open a `.vrm` with. The fixture rig is not a stand-in for it: it
    # is the sharper of the two, and this one is the *released* one.
    parser.add_argument("--vrm-avatar", type=pathlib.Path)
    options = parser.parse_args()

    drive(options, options.avatar, EXPECTED_DIFFERING, minimum_joints=0,
          expect_unmapped=False, signs=EXPECTED_ARM_SIGNS)
    if options.vrm_avatar is not None:
        if not options.vrm_avatar.is_file():
            fail(f"{options.vrm_avatar} is missing")
        drive(options, options.vrm_avatar, EXPECTED_DIFFERING_VRM,
              MINIMUM_VRM_JOINTS, expect_unmapped=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
