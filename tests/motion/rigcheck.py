#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Reading a baked rig back the way a consumer would, for the tests beside this.

Every end-to-end test in this directory ends the same way: two `UsdSkelAnimation`
layers -- the avatar-independent clip a tool wrote and the bake a second tool
made of it -- have to be compared as *motion* rather than as authored values. A
joint's local rotation says nothing on its own; the claim is always about where
the bone ends up, which means walking the token hierarchy and composing.

That walk is here because there is exactly one of it. Two copies of a
quaternion chain that compose in different orders agree on every axis-aligned
test pose and disagree on the first real one -- the same failure the motion
contract spells out for handedness -- and a test that is wrong in the same way
as the code it checks reports success. So the rule for this file is narrow: it
reads, it composes, and it decides nothing. Every claim about what a *correct*
bake looks like belongs to the test making it.

`Rig` deliberately does not read the rigs the way the tools wrote them. The
hierarchy comes from the joint token paths, which is the only thing a
`UsdSkelSkeleton` actually states about parenting, so a bake that authored a
sensible-looking array in the wrong order has nowhere to hide.
"""

from __future__ import annotations

import subprocess
import sys

from pxr import Gf, Usd, UsdSkel

# The correction is composed in float and read back through matrix decomposition
# on both sides, over chains up to seven joints deep.
ROTATION_TOLERANCE = 2e-4
DISTANCE_TOLERANCE = 1e-5
# Below this, a joint's rotation over the session is the same rotation. It is
# far above the numerical noise and far below any motion a person would call
# movement, so a "which bones moved" set does not depend on where it sits.
MOVEMENT_EPSILON = 1e-3


class Failures:
    """Every failure, not the first one.

    A bake that is wrong is usually wrong about a whole class of joints at
    once, and a run that stops at the first mismatch reports one arm when both
    are misplaced -- which reads as a local defect rather than as the systematic
    one it is.
    """

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


def find(stage: Usd.Stage, schema):
    return [prim for prim in stage.Traverse() if prim.IsA(schema)]


def quat_distance(a: Gf.Quatf, b: Gf.Quatf) -> float:
    """How far apart two rotations are, treating q and -q as the same one."""
    a = Gf.Quatd(float(a.GetReal()), Gf.Vec3d(*a.GetImaginary())).GetNormalized()
    b = Gf.Quatd(float(b.GetReal()), Gf.Vec3d(*b.GetImaginary())).GetNormalized()
    direct = abs(a.GetReal() - b.GetReal()) + sum(
        abs(x - y) for x, y in zip(a.GetImaginary(), b.GetImaginary()))
    flipped = abs(a.GetReal() + b.GetReal()) + sum(
        abs(x + y) for x, y in zip(a.GetImaginary(), b.GetImaginary()))
    return min(direct, flipped)


def as_quatf(q: Gf.Quatd) -> Gf.Quatf:
    return Gf.Quatf(q.GetReal(), Gf.Vec3f(*q.GetImaginary()))


class Rig:
    """One side of the comparison: a skeleton, its rest, and its animation."""

    def __init__(self, stage: Usd.Stage) -> None:
        skeletons = find(stage, UsdSkel.Skeleton)
        animations = find(stage, UsdSkel.Animation)
        if not skeletons or not animations:
            raise AssertionError(
                f"{stage.GetRootLayer().identifier} has no Skeleton or no "
                f"SkelAnimation")
        self.stage = stage
        self.skeleton = UsdSkel.Skeleton(skeletons[0])
        self.animation = UsdSkel.Animation(animations[0])
        self.joints = [str(token) for token in self.skeleton.GetJointsAttr().Get()]
        self.slot = {token: index for index, token in enumerate(self.joints)}

        rest = self.skeleton.GetRestTransformsAttr().Get()
        self.rest_local = [matrix.ExtractRotationQuat() for matrix in rest]

        self.anim_joints = [str(token)
                            for token in self.animation.GetJointsAttr().Get()]
        self.anim_slot = {token: index
                          for index, token in enumerate(self.anim_joints)}
        self.rotations = self.animation.GetRotationsAttr()
        self.translations = self.animation.GetTranslationsAttr()
        self.times = self.rotations.GetTimeSamples()

    def leaf(self, token: str) -> str:
        return token.rsplit("/", 1)[-1]

    def find_leaf(self, name: str) -> str | None:
        """The joint token whose last component is `name`, or None."""
        return next((token for token in self.joints if self.leaf(token) == name),
                    None)

    def parent(self, token: str) -> str | None:
        while "/" in token:
            token = token.rsplit("/", 1)[0]
            if token in self.slot:
                return token
        return None

    def chain(self, token: str) -> list[str]:
        """`token` and every ancestor of it, root first."""
        walk: list[str] = []
        current: str | None = token
        while current is not None:
            walk.append(current)
            current = self.parent(current)
        walk.reverse()
        return walk

    def _rest_quat(self, token: str) -> Gf.Quatd:
        local = self.rest_local[self.slot[token]]
        return Gf.Quatd(local.GetReal(), Gf.Vec3d(*local.GetImaginary()))

    def world_rest(self, token: str) -> Gf.Quatd:
        rotation = Gf.Quatd(1.0)
        for name in self.chain(token):
            rotation = rotation * self._rest_quat(name)
        return rotation.GetNormalized()

    def world_sample(self, token: str, time: float,
                     silenced: frozenset[str] = frozenset()) -> Gf.Quatd:
        """The bone's world rotation at `time`.

        `silenced` names bones (by their last token component) to read at their
        rest instead of at their sample. That is not a convenience: it is how a
        test states "this rotation did not arrive" as an expected value rather
        than as a tolerance wide enough to hide it.
        """
        values = self.rotations.Get(time)
        rotation = Gf.Quatd(1.0)
        for name in self.chain(token):
            index = self.anim_slot.get(name)
            if index is None or self.leaf(name) in silenced:
                # A joint the animation does not carry contributes its rest.
                sample = self._rest_quat(name)
            else:
                value = values[index]
                sample = Gf.Quatd(float(value.GetReal()),
                                  Gf.Vec3d(*value.GetImaginary()))
            rotation = rotation * sample
        return rotation.GetNormalized()

    def rest_relative(self, token: str, time: float,
                      silenced: frozenset[str] = frozenset()) -> Gf.Quatd:
        """The bone's world rotation away from its own rest."""
        return (self.world_sample(token, time, silenced)
                * self.world_rest(token).GetInverse()).GetNormalized()

    def moved(self, token: str) -> bool:
        index = self.anim_slot.get(token)
        if index is None:
            return False
        first = self.rotations.Get(self.times[0])[index]
        return any(
            quat_distance(self.rotations.Get(time)[index], first)
            > MOVEMENT_EPSILON
            for time in self.times)

    def translation(self, token: str, time: float) -> Gf.Vec3f:
        return self.translations.Get(time)[self.anim_slot[token]]
