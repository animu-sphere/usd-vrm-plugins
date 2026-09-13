#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""The OpenExec plan's P0-6: one bake, two implementations, the same input.

    .bvh -> motion_bvh_convert -> clip --+--> motion_retarget ------> bake
                                         |                             |
                                         +--> execMotion + execVrm     |
                                              (exec_parity) <----------+

This file runs the tools and hands `exec_parity` the one argument list it
handed `motion_retarget`, so the two cannot be given different flags. Every
comparison is the harness's; what this file asserts is what a case is *for*,
over the harness's JSON report.

The representative input is recorded, not generated (the plan's P0-6): the
mocopi export this repository may redistribute, converted by the shipped
profile, onto two rigs -- the fixture shaped so a broken rest-pose correction
cannot pass, and `Seed-san.vrm`, a released avatar with twist joints between
bound bones and no `upperChest` for a bone the clip drives. Two generated clips
are the other half, kept for their shape rather than their motion: the design
triplet's walk as a `.vrma`, through the VRMA reader, and a 30 fps clip keyed
where the tool does not put a sample back at the frame it read.
"""

from __future__ import annotations

import argparse
import json
import pathlib
import subprocess
import sys
import tempfile

RECORDED = "mocopi-mobile-arm-raise-turn.bvh"
PROFILE_ID = "mocopi-mobile-bvh-default-v1"

# The three root-motion statements a humanoid can make besides the default,
# each as the tool's flags. A case per flag set would be three near-identical
# CTest names; the statements are one claim -- that they are the tool's flags
# word for word -- so they are one case.
ROOT_MOTION_VARIANTS = (
    ("ignore", ["--root-motion", "ignore"]),
    ("root joint", ["--root-motion", "root", "--root-joint", "Root"]),
    ("scaled, height kept",
     ["--translation-scale", "0.5", "--preserve-target-height"]),
)


class Failures:
    def __init__(self) -> None:
        self.messages: list[str] = []

    def check(self, condition: bool, message: str) -> bool:
        if not condition:
            self.messages.append(message)
        return condition

    def report(self) -> int:
        for message in self.messages:
            print(f"FAIL: {message}", file=sys.stderr)
        return 1 if self.messages else 0


def run(*command: str) -> subprocess.CompletedProcess:
    return subprocess.run(list(command), text=True, encoding="utf-8",
                          errors="replace", stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE)


def convert(arguments: argparse.Namespace, work: pathlib.Path,
            failures: Failures) -> pathlib.Path | None:
    bvh = arguments.corpus / "recorded" / "redistributable" / RECORDED
    clip = work / "recorded.usda"
    done = run(arguments.convert, str(bvh), "--profile", PROFILE_ID,
               "--profile-dir", str(arguments.profiles), "--output", str(clip),
               "--quiet")
    if not failures.check(done.returncode == 0,
                          f"motion_bvh_convert failed: {done.stderr}"):
        return None
    return clip


def compare(arguments: argparse.Namespace, work: pathlib.Path, name: str,
            common: list[str], bake: pathlib.Path) -> tuple[int, dict | None]:
    """Runs the harness over one bake and returns its exit code and report."""
    report = work / f"{name}.parity.json"
    compared = run(arguments.parity, *common, "--bake", str(bake),
                   "--report", str(report))
    print(compared.stdout, end="")
    if compared.stderr:
        print(compared.stderr, end="", file=sys.stderr)
    if not report.exists():
        return compared.returncode, None
    data = json.loads(report.read_text(encoding="utf-8"))
    for text, count in sorted(data.get("exec_warnings", {}).items()):
        # One line per distinct warning, which is what the report's
        # diagnostics row is written from.
        print(f"  exec warned x{count}: {text}")
    return compared.returncode, data


def parity(arguments: argparse.Namespace, work: pathlib.Path, name: str,
           avatar: pathlib.Path, clip: pathlib.Path, flags: list[str],
           failures: Failures) -> tuple[dict | None, pathlib.Path]:
    """Bakes, then evaluates the same inputs through exec, and returns the
    harness's report. The tool is not `--quiet`: what it says is half of the
    diagnostics row, and a parity run prints both halves."""
    bake = work / f"{name}.bake.usda"
    common = ["--avatar", str(avatar), "--animation", str(clip), *flags]

    baked = run(arguments.retarget, *common, "--output", str(bake))
    print(f"--- {name}: motion_retarget {' '.join(flags)}")
    for line in (baked.stdout + baked.stderr).splitlines():
        print(f"  tool: {line}")
    if not failures.check(baked.returncode == 0,
                          f"{name}: motion_retarget failed: {baked.stderr}"):
        return None, bake

    code, data = compare(arguments, work, name, common, bake)
    failures.check(code == 0,
                   f"{name}: exec and the bake do not agree (exit {code}); "
                   f"the harness output above names the first difference")
    return data, bake


def check_moves(failures: Failures, name: str, data: dict,
                floor: int) -> None:
    """A guard on the guard: two implementations that both answered the rest
    pose would agree at every sample, so a case has to be over motion."""
    failures.check(
        data["samples"] >= floor and data["moving_samples"] >= floor - 1,
        f"{name}: {data['samples']} samples of which {data['moving_samples']} "
        f"move away from the first; parity over a still clip proves nothing")


def recorded_fixture(arguments, work, failures) -> None:
    clip = convert(arguments, work, failures)
    if clip is None:
        return
    avatar = arguments.fixtures / "humanoid_avatar.usda"
    map_path = arguments.fixtures / "humanoid_avatar_map.json"
    data, _ = parity(arguments, work, "recorded_fixture", avatar, clip,
                     ["--humanoid-map", str(map_path)], failures)
    if data is None:
        return
    check_moves(failures, "recorded_fixture", data, 100)
    # The fixture has no humanoid, so the harness defines one from the map --
    # which is the tool's `--humanoid-map`, stated on the stage because exec
    # reads nothing else.
    failures.check(
        any("from --humanoid-map" in line for line in data["stated"]),
        "the harness did not state the map it was handed")


def recorded_root_motion(arguments, work, failures) -> None:
    clip = convert(arguments, work, failures)
    if clip is None:
        return
    avatar = arguments.fixtures / "humanoid_avatar.usda"
    map_flags = ["--humanoid-map",
                 str(arguments.fixtures / "humanoid_avatar_map.json")]
    _, default_bake = parity(arguments, work, "root_default", avatar, clip,
                             map_flags, failures)
    for label, flags in ROOT_MOTION_VARIANTS:
        name = "root_" + label.replace(" ", "_").replace(",", "")
        data, bake = parity(arguments, work, name, avatar, clip,
                            map_flags + flags, failures)
        if data is None:
            continue
        check_moves(failures, name, data, 100)
        # Agreement proves the statements are the flags only if the flags did
        # something: a statement exec silently ignored would still agree with
        # a tool that ignored the flag too, and both would equal the default.
        failures.check(
            bake.exists() and default_bake.exists()
            and bake.read_bytes() != default_bake.read_bytes(),
            f"{name}: the bake under {' '.join(flags)} equals the default "
            f"bake, so agreeing with it says nothing about the statements")

        # And the other half of the pair: exec under these statements held
        # against the DEFAULT bake has to disagree -- in translation, where
        # root motion lands, and nowhere else. Passing alone would be
        # satisfied by a harness that had quietly stopped comparing.
        common = ["--avatar", str(avatar), "--animation", str(clip),
                  *map_flags, *flags]
        code, mismatched = compare(arguments, work, name + "_vs_default",
                                   common, default_bake)
        if not failures.check(
                code != 0 and mismatched is not None,
                f"{name}: exec under {' '.join(flags)} agreed with the "
                f"default bake, so the harness cannot tell the statements "
                f"apart"):
            continue
        failures.check(
            mismatched["translations"]["divergence"] > 0
            and mismatched["rotations"]["divergence"] == 0,
            f"{name}: against the default bake the statements should move "
            f"translations only; rotations "
            f"{mismatched['rotations']['divergence']}, translations "
            f"{mismatched['translations']['divergence']} diverged")


def recorded_real_avatar(arguments, work, failures) -> None:
    clip = convert(arguments, work, failures)
    if clip is None:
        return
    data, _ = parity(arguments, work, "recorded_real_avatar",
                     arguments.real_avatar, clip, [], failures)
    if data is None:
        return
    check_moves(failures, "recorded_real_avatar", data, 100)
    # The importer's own humanoid carries the schema and the skeleton, so the
    # harness states only what no producer authors yet.
    failures.check(
        not any("humanBones" in line or "defined <" in line
                for line in data["stated"]),
        f"the harness stated a humanoid over a real avatar's own: "
        f"{data['stated']}")


def vrma_walk(arguments, work, failures) -> None:
    """The plan's own slice, "avatar + walk.vrma", with the design triplet's
    avatar: a `.vrma` read by usdVrmaFileFormat, which is how a semantic clip
    reaches both implementations in practice. The clip is the VRMA reader's
    fixture of the triplet's walk -- generated, kept for its shape."""
    data, _ = parity(arguments, work, "vrma_walk",
                     arguments.design_fixtures / "avatar.usda",
                     arguments.vrma_walk,
                     ["--humanoid-map", str(arguments.design_map)], failures)
    if data is None:
        return
    check_moves(failures, "vrma_walk", data, 2)


def generated_thirty_fps(arguments, work, failures) -> None:
    """The rule, exercised: the tool places frames 31 and 62 of a 30 fps clip
    at 31.000000000000004 and 62.00000000000001, and the harness compares at
    those samples. Characterisation, both halves -- a tool that authored at the
    time code it read would change this file before it changed the numbers."""
    data, _ = parity(arguments, work, "generated_thirty_fps",
                     arguments.design_fixtures / "avatar.usda",
                     arguments.parity_fixtures / "thirty_fps_clip.usda",
                     ["--humanoid-map", str(arguments.design_map)], failures)
    if data is None:
        return
    check_moves(failures, "generated_thirty_fps", data, 3)
    placement = data["placement"]
    failures.check(
        placement["exact"] == 1 and placement["rounding"] == 2,
        f"the bake should place frame 0 exactly and frames 31 and 62 a "
        f"rounding away; it placed {placement['exact']} exactly and "
        f"{placement['rounding']} a rounding away")
    # Read at the clip's frame instead, the bake is interpolated towards the
    # key before it -- which is why the harness does not read it there.
    at_keys = data["read_at_clip_keys"]["rotations"]
    failures.check(
        at_keys["exact"] < data["samples"] * data["joints"],
        "read at the clip's own frames the bake answered every rotation "
        "exactly, so the rule this case exists for costs nothing here")


CASES = {
    "recorded_fixture": recorded_fixture,
    "recorded_root_motion": recorded_root_motion,
    "recorded_real_avatar": recorded_real_avatar,
    "vrma_walk": vrma_walk,
    "generated_thirty_fps": generated_thirty_fps,
}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--case", required=True, choices=sorted(CASES))
    parser.add_argument("--parity", required=True, help="exec_parity")
    parser.add_argument("--convert", required=True, help="motion_bvh_convert")
    parser.add_argument("--retarget", required=True, help="motion_retarget")
    parser.add_argument("--corpus", type=pathlib.Path, required=True)
    parser.add_argument("--profiles", type=pathlib.Path, required=True)
    parser.add_argument("--fixtures", type=pathlib.Path, required=True)
    parser.add_argument("--design-fixtures", type=pathlib.Path, required=True)
    parser.add_argument("--design-map", type=pathlib.Path, required=True)
    parser.add_argument("--parity-fixtures", type=pathlib.Path, required=True)
    parser.add_argument("--real-avatar", type=pathlib.Path)
    parser.add_argument("--vrma-walk", type=pathlib.Path)
    arguments = parser.parse_args()

    failures = Failures()
    with tempfile.TemporaryDirectory() as directory:
        CASES[arguments.case](arguments, pathlib.Path(directory), failures)
    return failures.report()


if __name__ == "__main__":
    raise SystemExit(main())
