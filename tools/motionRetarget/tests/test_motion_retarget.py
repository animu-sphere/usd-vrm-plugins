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

from pxr import Gf, Usd, UsdSkel

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


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--tool", required=True)
    parser.add_argument("--fixtures", required=True,
                        help="docs/design/fixtures/motion")
    parser.add_argument("--humanoid-map", required=True)
    options = parser.parse_args()

    fixtures = pathlib.Path(options.fixtures).resolve()
    avatar = fixtures / "avatar.usda"
    clip = fixtures / "canonical_walk.usda"
    expected = fixtures / "expected_retargeted.usda"
    for path in (avatar, clip, expected):
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

        # Re-baking over an existing output overwrites rather than failing.
        rerun = run_tool(
            options.tool,
            "--avatar", str(avatar), "--animation", str(clip),
            "--output", str(output), "--humanoid-map", options.humanoid_map,
            "--animation-name", "RetargetedWalk", "--quiet")
        failures.check(rerun.returncode == 0,
                       f"re-bake over an existing output failed: {rerun.stderr}")

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
