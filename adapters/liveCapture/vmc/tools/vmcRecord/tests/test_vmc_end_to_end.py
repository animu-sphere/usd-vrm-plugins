#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""A VMC session all the way to a rig, through tools this adapter cannot touch.

    capture.vmcpackets
        -> vmc_record --export-trace     (this adapter)
        -> motion_capture                (unchanged)
        -> motion_retarget               (unchanged)
        -> a target rig, resolved through UsdSkelSkeletonQuery

The claim is not that each tool works -- each has its own tests -- but that the
chain closes with **no edge between the product and this adapter**
(WORKSPACE.md §2). `motion_capture` is a member of the aggregate product and
this adapter is deliberately excluded from it, so the only thing that can pass
between them is a file. If that file ever stops being one the product can read,
this test fails and every other test in the repository still passes: the two
halves are only ever connected here.

That is also why the test lives with the adapter rather than with the product.
The product's tools must keep working without knowing this adapter exists, and a
test of theirs that spawned `vmc_record` would be the dependency this
arrangement exists to avoid -- in a test directory instead of a link line, which
is worse for being harder to see.

The last leg is a value check rather than a load check. A clip that binds and
then holds the rest pose opens without error and animates nothing (#64), so what
is asserted is that the joints the session actually drove are the joints
UsdSkel resolves as moving -- named, not counted.
"""

from __future__ import annotations

import argparse
import pathlib
import subprocess
import sys
import tempfile

from pxr import Gf, Usd, UsdSkel

# The committed capture that moves. `arm-raise-30hz` drives exactly three bones
# across its five frames and holds the other eighteen still, which is what makes
# it a usable end-to-end fixture: a session where everything moved could not
# tell "the chain works" apart from "something wrote noise into every joint".
CAPTURE = "arm-raise-30hz"

# The rig joints those three bones map to in the fixture avatar, and the only
# ones that may move. Spelled as joint paths because that is what UsdSkel calls
# them, and named rather than counted because three joints moving is not the
# claim -- *these* three moving is.
EXPECTED_MOVING = {
    "Root/Pelvis/SpineA/ChestA/ClavicleL",
    "Root/Pelvis/SpineA/ChestA/ClavicleL/ArmUpperL",
    "Root/Pelvis/SpineA/ChestA/ClavicleL/ArmUpperL/ArmLowerL",
}

# Two rotations count as the same when their quaternions agree this closely. The
# retarget composes a rest-pose correction, so an untouched joint comes back as
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


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--record-tool", required=True, type=pathlib.Path)
    parser.add_argument("--capture-tool", required=True, type=pathlib.Path)
    parser.add_argument("--retarget-tool", required=True, type=pathlib.Path)
    parser.add_argument("--corpus", required=True, type=pathlib.Path)
    parser.add_argument("--avatar", required=True, type=pathlib.Path)
    options = parser.parse_args()

    capture = options.corpus / f"{CAPTURE}.vmcpackets"
    if not capture.is_file():
        fail(f"{capture} is missing")

    with tempfile.TemporaryDirectory(prefix="vmc-e2e-") as scratch:
        directory = pathlib.Path(scratch)
        trace = directory / "session.trace"
        clip = directory / "session.usda"
        baked = directory / "baked.usda"

        # 1. The adapter's half ends here, at a file with no VMC in it.
        run(options.record_tool, "--inspect", str(capture),
            "--export-trace", str(trace), "--quiet")

        # 2. The product's replay tool, given no argument that names an adapter,
        #    a protocol, or a socket. It is reading a recording like any other.
        run(options.capture_tool, "--trace", str(trace), "--output", str(clip),
            "--quiet")

        # 3. The offline retarget, unchanged since v0.4.0, onto a rig whose
        #    joint names disagree with the humanoid vocabulary -- the binding
        #    comes from the avatar's `vrm:humanBones:*` attributes.
        run(options.retarget_tool, "--avatar", str(options.avatar),
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
        if len(times) != 5:
            fail(f"expected the session's five frames, found {len(times)} "
                 f"time sample(s)")

        def rotations(time) -> list:
            transforms = query.ComputeJointLocalTransforms(Usd.TimeCode(time))
            if not transforms:
                fail(f"UsdSkel resolved no joint transforms at {time}: the "
                     f"animation is bound but does not drive the rig")
            return [transform.ExtractRotationQuat()
                    for transform in transforms]

        joints = [str(joint) for joint in query.GetJointOrder()]
        reference = rotations(times[0])
        moved = {
            joints[index]
            for time in times[1:]
            for index, (before, after) in enumerate(
                zip(reference, rotations(time)))
            if not same_rotation(before, after)
        }

        if moved != EXPECTED_MOVING:
            fail("the wrong joints moved on the rig.\n"
                 f"  expected: {sorted(EXPECTED_MOVING)}\n"
                 f"  moved:    {sorted(moved)}")

    print(f"vmc_record -> motion_capture -> motion_retarget: {CAPTURE} reaches "
          f"a rig, {len(moved)} joint(s) moving as the session drove them")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
