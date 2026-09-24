#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""One downstream half for every source category: the reference pipeline.

    .vrma clip        -> usdVrmaFileFormat -+
    BVH export        -> motion_convert    -+-> MotionClip -> motion_retarget -> VRM
    recorded session  -> motion_record     -+

Only the first arrow knows where a clip came from. Two of the three first
arrows are `usd-motion-plugins`' tools, whose committed output this reads
(`tests/motion/fixtures/`, `tools/motionRetarget/tests/fixtures/`); the third is
this repository's importer. Everything after the clip is the shared packages
and `motion_retarget`, and the claim here is that it is **the same code** for
all three, not that three outputs are each plausible (boundary consolidation
BND-1, which the motion migration made its MIG-5 cross-repository test).

"The same code" is asserted three ways, none of which a per-source test can
make:

  * **One invocation.** Every source is baked by one argument list that differs
    in `--animation` alone. A source that needed a flag has found a boundary
    defect.
  * **One set of loaded code.** `--load-report` names every plugin and module
    the run loaded. Across sources they may differ by the first arrow's plugin
    and nothing else -- `usdVrmaFileFormat` for a `.vrma`, nothing for a clip
    already on a stage. A downstream branch that pulled in code of its own
    would show up here as a module one source loads and another does not.
  * **One output shape.** The authored layer -- every spec's path, kind and
    type, the animation's joint order -- is identical across sources. The
    values differ, because the motion does.

Then each bake is held to the same checks, by the same function, with the same
tolerance: the bound bones reproduce the clip's motion away from rest, the ones
the avatar cannot represent are named, nothing outside the humanoid moves, and
the skeleton resolves the animation rather than its rest pose (#64).

Two runs are registered. `workspace_reference_pipeline` runs by default, over
the committed inputs: `Seed-san.vrm`, a generated `.vrma`, the converted mocopi
export and the recorded session. `workspace_reference_pipeline_local` runs the
same checks over data that cannot be committed, named by two environment
variables, and is skipped without them:

    USDVRM_LOCAL_AVATAR     a .vrm to bake onto
    USDVRM_LOCAL_VRMA_DIR   a directory whose *.vrma are the .vrma sources
    USDVRM_LOCAL_OUTPUT_DIR optional: where to keep each bake and its load
                            report, to open in usdview afterwards

The converted export and the recorded session join those, so the local run
still compares three categories.
"""

from __future__ import annotations

import argparse
import json
import os
import pathlib
import sys
import tempfile

from pxr import Sdf, Usd

from rigcheck import (DISTANCE_TOLERANCE, ROTATION_TOLERANCE, Failures, Rig,
                      as_quatf, quat_distance, run_tool)
from test_real_avatar import bones_bound_by, check_binding_resolves

# The plugin each first arrow may add to a run. Everything else a run loads has
# to be loaded by every other run too.
FIRST_ARROW = {
    "vrma": frozenset({"UsdVrmaFileFormat"}),
    "bvh": frozenset(),
    "trace": frozenset(),
}

# ctest's SKIP_RETURN_CODE for the local run without its data.
SKIPPED = 77


class Source:
    def __init__(self, kind: str, path: pathlib.Path) -> None:
        self.kind = kind
        self.path = path
        self.name = f"{kind}:{path.name}"


def downstream(tool: str, avatar: pathlib.Path, source: Source,
               output: pathlib.Path, report: pathlib.Path):
    """The one invocation. Nothing in it may depend on `source.kind`."""
    return run_tool(tool, "--avatar", str(avatar), "--animation", str(source.path),
                    "--output", str(output), "--load-report", str(report))


def layer_shape(layer: Sdf.Layer) -> set[tuple[str, str, str]]:
    """Every spec's path, kind and type -- what was authored, not its value."""
    shape: set[tuple[str, str, str]] = set()

    def visit(path: Sdf.Path) -> None:
        spec = layer.GetObjectAtPath(path)
        if spec is None:
            return
        kind = type(spec).__name__
        if isinstance(spec, Sdf.PrimSpec):
            detail = spec.typeName
        elif isinstance(spec, Sdf.AttributeSpec):
            detail = str(spec.typeName)
        else:
            detail = ""
        shape.add((str(path), kind, detail))

    layer.Traverse(Sdf.Path.absoluteRootPath, visit)
    return shape


def normalized(path: str) -> str:
    return os.path.normcase(os.path.normpath(path))


def check_bake(failures: Failures, source: Source, output: pathlib.Path,
               bound: dict[str, str], stderr: str) -> Rig | None:
    """The same checks for every source category, and the same tolerance."""
    stage = Usd.Stage.Open(str(output))
    if not failures.check(stage is not None, f"{source.name}: the bake does not open"):
        return None
    target = Rig(stage)
    clip = Rig(Usd.Stage.Open(str(source.path)))
    if not check_binding_resolves(failures, target):
        return None

    failures.check(
        target.animation.GetScalesAttr().HasAuthoredValue(),
        f"{source.name}: the bake authors no scales, so UsdSkel resolves every "
        f"joint to its rest pose (#64)")

    silenced = frozenset({clip.leaf(token) for token in clip.joints} - set(bound))
    for bone in sorted(silenced):
        failures.check(
            bone in stderr,
            f"{source.name}: the clip drives {bone}, the avatar binds no joint "
            f"for it, and motion_retarget did not say so")

    times = target.times
    sampled = sorted({times[0], times[len(times) // 3],
                      times[2 * len(times) // 3], times[-1]})
    compared = 0
    carried = 0
    for bone, token in sorted(bound.items()):
        clip_token = clip.find_leaf(bone)
        if clip_token is None:
            continue
        carried += 1
        if not failures.check(
                token in target.slot,
                f"{source.name}: the avatar binds {bone} to '{token}', which "
                f"its own skeleton does not list"):
            continue
        compared += 1
        for time in sampled:
            expected = clip.rest_relative(clip_token, time, silenced)
            actual = target.rest_relative(token, time)
            failures.check(
                quat_distance(as_quatf(actual), as_quatf(expected))
                <= ROTATION_TOLERANCE,
                f"{source.name}, time {time}: {bone} turns {actual} away from "
                f"its rest on the avatar and {expected} in the clip")
    failures.check(
        compared and compared == carried,
        f"{source.name}: {compared} of the {carried} bones the clip carries and "
        f"the avatar binds were compared")

    # Root motion is a delta from each rig's own start (MOTION_CONTRACT.md, root
    # and hips), so the hips travel what the clip's travel, whatever the two
    # rigs' heights.
    clip_hips = clip.find_leaf("hips")
    if failures.check(clip_hips is not None and "hips" in bound,
                      f"{source.name}: no hips on one side of the bake"):
        clip_start = clip.translation(clip_hips, clip.times[0])
        target_start = target.translation(bound["hips"], times[0])
        for time in sampled:
            clip_delta = clip.translation(clip_hips, time) - clip_start
            target_delta = target.translation(bound["hips"], time) - target_start
            failures.check(
                all(abs(a - b) <= DISTANCE_TOLERANCE
                    for a, b in zip(clip_delta, target_delta)),
                f"{source.name}, time {time}: the avatar's hips moved "
                f"{tuple(target_delta)} and the clip's {tuple(clip_delta)}")

    clip_moving = {bone for bone in bound
                   if (token := clip.find_leaf(bone)) and clip.moved(token)}
    target_moving = {bone for bone, token in bound.items() if target.moved(token)}
    failures.check(
        bool(clip_moving),
        f"{source.name}: no bound bone moves in the clip, so the bake is "
        f"compared against a still pose")
    failures.check(
        clip_moving == target_moving,
        f"{source.name}: the clip moves {sorted(clip_moving)} and the bake "
        f"moves {sorted(target_moving)}")

    unbound = [token for token in target.joints if token not in set(bound.values())]
    moved = sorted(token for token in unbound if target.moved(token))
    failures.check(
        not moved,
        f"{source.name}: the bake moves {len(moved)} joint(s) no human bone is "
        f"bound to: {moved[:8]}")
    return target


def check_same_code(failures: Failures, sources: list[Source],
                    reports: dict[str, dict]) -> None:
    plugins = {s.name: frozenset(reports[s.name]["loaded_plugins"]) for s in sources}
    modules = {s.name: frozenset(normalized(m)
                                 for m in reports[s.name]["loaded_modules"])
               for s in sources}
    common_plugins = frozenset.intersection(*plugins.values())
    common_modules = frozenset.intersection(*modules.values())
    for source in sources:
        extra = plugins[source.name] - common_plugins
        failures.check(
            extra <= FIRST_ARROW[source.kind],
            f"{source.name} loads {sorted(extra)} that another source does "
            f"not, beyond its own first arrow {sorted(FIRST_ARROW[source.kind])}")
        arrow_libraries = {normalized(reports[source.name]["loaded_plugins"][name])
                           for name in extra}
        stray = modules[source.name] - common_modules - arrow_libraries
        failures.check(
            not stray,
            f"{source.name} loads module(s) no other source loads and no "
            f"first-arrow plugin accounts for: {sorted(stray)}")
    kinds = {source.kind for source in sources}
    failures.check(
        kinds == set(FIRST_ARROW),
        f"only {sorted(kinds)} were compared; the claim is about "
        f"{sorted(FIRST_ARROW)}")


def check_same_shape(failures: Failures, sources: list[Source],
                     outputs: dict[str, pathlib.Path],
                     targets: dict[str, Rig]) -> None:
    reference = sources[0]
    shape = layer_shape(Sdf.Layer.FindOrOpen(str(outputs[reference.name])))
    for source in sources[1:]:
        other = layer_shape(Sdf.Layer.FindOrOpen(str(outputs[source.name])))
        failures.check(
            other == shape,
            f"{source.name} and {reference.name} bake layers of different "
            f"shape: {sorted(other ^ shape)[:6]}")
        if reference.name in targets and source.name in targets:
            failures.check(
                targets[source.name].anim_joints
                == targets[reference.name].anim_joints,
                f"{source.name} and {reference.name} animate different joints "
                f"or the same joints in a different order")


def local_inputs() -> tuple[pathlib.Path, list[pathlib.Path]] | None:
    avatar = os.environ.get("USDVRM_LOCAL_AVATAR")
    vrma_dir = os.environ.get("USDVRM_LOCAL_VRMA_DIR")
    if not avatar or not vrma_dir:
        return None
    return pathlib.Path(avatar), sorted(pathlib.Path(vrma_dir).glob("*.vrma"))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--retarget", required=True, help="motion_retarget")
    parser.add_argument("--avatar", type=pathlib.Path,
                        help="the target .vrm (not with --local)")
    parser.add_argument("--vrma", type=pathlib.Path, action="append", default=[],
                        help="a .vrma source (not with --local)")
    parser.add_argument("--bvh-clip", type=pathlib.Path, required=True,
                        help="motion_convert's output for a BVH export")
    parser.add_argument("--trace-clip", type=pathlib.Path, required=True,
                        help="motion_record's output for a recorded session")
    parser.add_argument("--local", action="store_true",
                        help="take the avatar and the .vrma sources from "
                             "USDVRM_LOCAL_AVATAR and USDVRM_LOCAL_VRMA_DIR")
    arguments = parser.parse_args()

    if arguments.local:
        local = local_inputs()
        if local is None:
            print("USDVRM_LOCAL_AVATAR / USDVRM_LOCAL_VRMA_DIR are not set; "
                  "skipping the local reference pipeline")
            return SKIPPED
        avatar, vrmas = local
    else:
        avatar, vrmas = arguments.avatar, arguments.vrma

    sources = ([Source("vrma", path) for path in vrmas]
               + [Source("bvh", arguments.bvh_clip),
                  Source("trace", arguments.trace_clip)])
    missing = [p for p in [avatar, *(s.path for s in sources)]
               if p is None or not p.exists()]
    if missing or not vrmas:
        print(f"missing input: {missing or 'no .vrma source'}", file=sys.stderr)
        return 1

    avatar_stage = Usd.Stage.Open(str(avatar))
    if avatar_stage is None:
        print(f"{avatar} did not open. A .vrm needs usdVrmFileFormat on "
              f"PXR_PLUGINPATH_NAME.", file=sys.stderr)
        return 1
    failures = Failures()
    bound = bones_bound_by(avatar_stage)
    if not failures.check(
            len(bound) >= 20,
            f"{avatar.name} carries {len(bound)} vrm:humanBones bindings"):
        return failures.report()

    keep = None
    if arguments.local and os.environ.get("USDVRM_LOCAL_OUTPUT_DIR"):
        keep = pathlib.Path(os.environ["USDVRM_LOCAL_OUTPUT_DIR"])
        keep.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory(prefix="reference-pipeline-") as directory:
        work = pathlib.Path(directory)
        outputs: dict[str, pathlib.Path] = {}
        reports: dict[str, dict] = {}
        targets: dict[str, Rig] = {}
        for index, source in enumerate(sources):
            output = work / f"{index}-bake.usda"
            report = work / f"{index}-load.json"
            if keep is not None:
                # Baked in place rather than copied there: the bake references
                # its avatar by a path relative to itself, which a copy breaks.
                stem = f"{avatar.stem}__{source.kind}__{source.path.stem}"
                output = keep / f"{stem}.usda"
                report = keep / f"{stem}.load.json"
            result = downstream(arguments.retarget, avatar, source, output, report)
            if not failures.check(
                    result.returncode == 0,
                    f"{source.name}: motion_retarget failed with the argument "
                    f"list every other source takes: {result.stderr[-2000:]}"):
                continue
            outputs[source.name] = output
            reports[source.name] = json.loads(report.read_text(encoding="utf-8"))
            target = check_bake(failures, source, output, bound, result.stderr)
            if target is not None:
                targets[source.name] = target
            print(f"{source.name}: baked {len(target.times) if target else 0} "
                  f"samples onto {avatar.name}")

        baked = [source for source in sources if source.name in outputs]
        if len(baked) == len(sources):
            check_same_code(failures, baked, reports)
            check_same_shape(failures, baked, outputs, targets)

    return failures.report()


if __name__ == "__main__":
    raise SystemExit(main())
