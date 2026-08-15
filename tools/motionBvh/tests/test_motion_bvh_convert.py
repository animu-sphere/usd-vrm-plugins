#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""What `motion_bvh_convert` writes, checked against the `.bvh` text.

The tool composes three things this repository tests separately -- a parser, an
extractor, and the conversion into canonical motion -- so what is left for a
test of the *tool* is the part nothing else covers: that the clip on disk says
what the file and the profile say, in the shape `motion_retarget` consumes.

Every expected number below is read out of the `.bvh` text and the profile by
this script, never out of the tool. A test that asked the converter what a file
means and then checked the tool agreed would be one implementation agreeing with
itself, and the whole reason a real export is committed here is that it can be
surprising.

Two premises are asserted rather than assumed, because both are properties of
the shipped profile that a later edit could change while leaving every
comparison below silently checking the wrong thing:

* the profile's basis is the canonical one (right-handed, +Y up, +Z forward), so
  the change of basis is the identity and a rest offset reaches the clip scaled
  and not permuted;
* the profile's translation unit is centimetres, which is the scale.

If either moves, this script fails on the premise instead of on the numbers.
"""

from __future__ import annotations

import argparse
import pathlib
import subprocess
import sys
import tempfile

from pxr import Gf, Usd, UsdGeom, UsdSkel

# The committed export and the profile written from it, named rather than
# discovered: a test that scanned a directory would pass on the day the file it
# is about stopped being there.
RECORDED = "mocopi-mobile-arm-raise-turn.bvh"
PROFILE_ID = "mocopi-mobile-bvh-default-v1"

TOLERANCE = 1e-6
# Rotations go through a degrees->radians->quaternion path in both this script
# and the tool, in float rather than double on the tool's side.
ANGLE_TOLERANCE = 1e-5


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


# --- reading the .bvh, independently of the parser under test ---------------


class Joint:
    def __init__(self, name: str, parent: int) -> None:
        self.name = name
        self.parent = parent
        self.offset: tuple[float, float, float] = (0.0, 0.0, 0.0)
        self.channels: list[str] = []
        self.column = 0


class Document:
    def __init__(self) -> None:
        self.joints: list[Joint] = []
        self.frame_time = 0.0
        self.rows: list[list[float]] = []

    def index_of(self, name: str) -> int:
        for index, joint in enumerate(self.joints):
            if joint.name == name:
                return index
        raise AssertionError(f"{name} is not a joint of this file")


def read_bvh(path: pathlib.Path) -> Document:
    """A deliberately small, separate BVH reader.

    It handles exactly what the committed export uses. Anything it does not
    understand raises rather than being skipped -- a reader that shrugged at a
    line would make this test agree with the tool by not looking.
    """
    document = Document()
    tokens = path.read_text(encoding="utf-8").split("\n")
    stack: list[int] = []
    index = 0
    column = 0

    while index < len(tokens):
        parts = tokens[index].split()
        index += 1
        if not parts:
            continue
        head = parts[0].upper()
        if head == "HIERARCHY":
            continue
        if head in ("ROOT", "JOINT"):
            parent = stack[-1] if stack else -1
            joint = Joint(parts[1], parent)
            document.joints.append(joint)
            stack.append(len(document.joints) - 1)
        elif head == "END":  # End Site
            # Consumed whole: a terminator carries an OFFSET and no channels,
            # and it is not a joint of the motion section.
            depth = 0
            while index < len(tokens):
                line = tokens[index].strip()
                index += 1
                if line == "{":
                    depth += 1
                elif line == "}":
                    depth -= 1
                    if depth == 0:
                        break
        elif head == "{":
            continue
        elif head == "}":
            stack.pop()
        elif head == "OFFSET":
            document.joints[stack[-1]].offset = tuple(
                float(value) for value in parts[1:4])
        elif head == "CHANNELS":
            joint = document.joints[stack[-1]]
            joint.channels = parts[2:]
            joint.column = column
            column += len(joint.channels)
        elif head == "MOTION":
            frames = int(tokens[index].split(":")[1])
            index += 1
            document.frame_time = float(tokens[index].split(":")[1])
            index += 1
            for _ in range(frames):
                document.rows.append(
                    [float(value) for value in tokens[index].split()])
                index += 1
            break
        else:
            raise AssertionError(f"unexpected line: {tokens[index - 1]!r}")
    return document


def euler_order(joint: Joint) -> str:
    """The relative order of the rotation channels, whatever sits between them.

    This is BvhExtract.h's rule, re-stated here rather than imported: the
    channel list is the only statement a BVH file makes about its rotation
    order.
    """
    return "".join(name[0].upper() for name in joint.channels
                   if name.lower().endswith("rotation"))


def joint_rotation(document: Document, index: int, frame: int) -> Gf.Quatd:
    """Three angles composed intrinsically, by the right-hand rule.

    `a * b` applies `b` first, so ZXY composes as qZ * qX * qY -- each
    successive angle turning about axes the previous ones have already moved.
    The source's handedness is deliberately absent: it lives in the basis
    change's determinant, and applying it here as well is the double-handedness
    failure CanonicalConversion.h exists to prevent.
    """
    joint = document.joints[index]
    row = document.rows[frame]
    axes = {"X": Gf.Vec3d(1, 0, 0), "Y": Gf.Vec3d(0, 1, 0),
            "Z": Gf.Vec3d(0, 0, 1)}
    rotation = Gf.Quatd(1.0)
    for offset, name in enumerate(joint.channels):
        if not name.lower().endswith("rotation"):
            continue
        angle = row[joint.column + offset]
        rotation = rotation * Gf.Rotation(axes[name[0].upper()],
                                          angle).GetQuat()
    return rotation


def path_to(document: Document, bound: dict[int, str], index: int) -> list[int]:
    """The rig joints from just below the nearest bound ancestor down to `index`.

    A joint between two mapped ones is on the path between them, so its rotation
    is composed in rather than dropped (CanonicalConversion.h). This is the rule
    the clip's rotations are checked against.
    """
    chain = [index]
    parent = document.joints[index].parent
    while parent >= 0 and parent not in bound:
        chain.append(parent)
        parent = document.joints[parent].parent
    chain.reverse()
    return chain


# --- reading the profile, independently of its loader -----------------------


def read_profile(path: pathlib.Path) -> dict:
    """The handful of keys this test needs, off the shipped file.

    Not a YAML parser and not the tool's reader: the file's shape is flat enough
    that the lines below are unambiguous, and every one of them is checked
    against the file's own text in `scripts/check_motion_profiles.py` already.
    """
    profile: dict = {"joints": {}, "ignored": [], "coordinates": {}}
    section = None
    for line in path.read_text(encoding="utf-8").splitlines():
        stripped = line.split("#")[0].rstrip()
        if not stripped.strip():
            continue
        indented = stripped.startswith(" ")
        key, _, value = stripped.strip().partition(":")
        value = value.strip()
        if not indented:
            section = key
            if key == "id":
                profile["id"] = value
            elif key == "producer":
                profile["producer"] = value
            elif key == "ignoredJoints":
                profile["ignored"] = [
                    name.strip() for name in value.strip("[]").split(",")
                    if name.strip()]
        elif section == "coordinates":
            profile["coordinates"][key] = value
        elif section == "root" and key == "joint":
            profile["root"] = value
        elif section == "joints":
            bone = value.strip("{} ").split(",")[0]
            profile["joints"][key] = bone.split(":")[1].strip()
    return profile


# --- the run ----------------------------------------------------------------


def run_tool(tool: str, *arguments: str) -> subprocess.CompletedProcess:
    return subprocess.run(
        [tool, *arguments], text=True, encoding="utf-8", errors="replace",
        stdout=subprocess.PIPE, stderr=subprocess.PIPE)


def near(actual, expected, tolerance=TOLERANCE) -> bool:
    return abs(float(actual) - float(expected)) <= tolerance


def near_vector(actual, expected, tolerance=TOLERANCE) -> bool:
    return all(near(a, b, tolerance) for a, b in zip(actual, expected))


def near_quat(actual: Gf.Quatf, expected: Gf.Quatd,
              tolerance=ANGLE_TOLERANCE) -> bool:
    a = Gf.Quatd(float(actual.GetReal()), Gf.Vec3d(*actual.GetImaginary()))
    b = expected.GetNormalized()
    # q and -q are the same rotation; a writer is free to pick either.
    direct = abs(a.GetReal() - b.GetReal()) + sum(
        abs(x - y) for x, y in zip(a.GetImaginary(), b.GetImaginary()))
    flipped = abs(a.GetReal() + b.GetReal()) + sum(
        abs(x + y) for x, y in zip(a.GetImaginary(), b.GetImaginary()))
    return min(direct, flipped) <= tolerance


def check_conversion(failures: Failures, tool: str, bvh: pathlib.Path,
                     profile_dir: pathlib.Path, work: pathlib.Path) -> None:
    document = read_bvh(bvh)
    profile = read_profile(profile_dir / f"{PROFILE_ID}.yaml")

    # --- the premises ------------------------------------------------------
    coordinates = profile["coordinates"]
    canonical_basis = (coordinates.get("handedness") == "right"
                       and coordinates.get("upAxis") in ("+Y", "Y")
                       and coordinates.get("forwardAxis") in ("+Z", "Z"))
    if not failures.check(
            canonical_basis,
            f"this test assumes {PROFILE_ID} states the canonical basis; it "
            f"now states {coordinates}. The rest and translation comparisons "
            f"below would silently check the wrong numbers."):
        return
    if not failures.check(
            coordinates.get("translationUnit") == "centimeters",
            f"this test assumes {PROFILE_ID} states centimetres; it now states "
            f"{coordinates.get('translationUnit')!r}"):
        return
    scale = 0.01

    output = work / "canonical.usda"
    result = run_tool(tool, str(bvh), "--profile", PROFILE_ID,
                      "--profile-dir", str(profile_dir), "--output",
                      str(output))
    if not failures.check(
            result.returncode == 0,
            f"conversion of the recorded export failed: {result.stderr}"):
        return

    stage = Usd.Stage.Open(str(output))
    failures.check(stage is not None, "the tool wrote no openable stage")
    if stage is None:
        return

    # --- what the stage says it is -----------------------------------------
    rate = 1.0 / document.frame_time
    failures.check(near(stage.GetTimeCodesPerSecond(), rate),
                   f"timeCodesPerSecond is {stage.GetTimeCodesPerSecond()}, "
                   f"and the file's frame time says {rate}")
    failures.check(near(stage.GetStartTimeCode(), 0.0),
                   "the clip does not start at time code 0")
    failures.check(
        near(stage.GetEndTimeCode(), len(document.rows) - 1),
        f"endTimeCode is {stage.GetEndTimeCode()}; the file carries "
        f"{len(document.rows)} rows, so the last is "
        f"{len(document.rows) - 1}")
    failures.check(UsdGeom.GetStageUpAxis(stage) == "Y",
                   "the clip is not +Y up, which canonical motion is")
    failures.check(near(UsdGeom.GetStageMetersPerUnit(stage), 1.0),
                   "the clip is not in metres, which canonical motion is")

    skeleton = next((UsdSkel.Skeleton(prim) for prim in stage.Traverse()
                     if prim.IsA(UsdSkel.Skeleton)), None)
    animation = next((UsdSkel.Animation(prim) for prim in stage.Traverse()
                      if prim.IsA(UsdSkel.Animation)), None)
    if not failures.check(skeleton is not None and animation is not None,
                          "the clip has no Skeleton or no SkelAnimation"):
        return

    joints = list(skeleton.GetJointsAttr().Get())
    failures.check(list(animation.GetJointsAttr().Get()) == joints,
                   "the animation's joint set is not the skeleton's")

    # --- the joint set, against the profile's map --------------------------
    expected_bones = set(profile["joints"].values())
    clip_bones = [token.split("/")[-1] for token in joints]
    failures.check(
        set(clip_bones) == expected_bones,
        f"the clip's bones are {sorted(set(clip_bones))}, and the profile maps "
        f"{sorted(expected_bones)}")
    failures.check(
        len(clip_bones) == len(set(clip_bones)),
        "the clip names a bone twice")
    # Every ignored joint is absent by construction -- it maps no bone -- and
    # the count is what says the two lists together cover the rig.
    failures.check(
        len(profile["joints"]) + len(profile["ignored"])
        == len(document.joints),
        f"the profile maps {len(profile['joints'])} and ignores "
        f"{len(profile['ignored'])} of the file's {len(document.joints)} "
        f"joints, so it does not describe this rig")

    # --- the rest pose, against the OFFSET lines ---------------------------
    #
    # A bound bone's rest translation is the sum of the OFFSETs from just below
    # its nearest bound ancestor down to itself: the unmapped segments between
    # two mapped joints are on the path, so their offsets are part of where the
    # lower one sits.
    bound_index = {document.index_of(name): bone
                   for name, bone in profile["joints"].items()}
    rest_transforms = list(skeleton.GetRestTransformsAttr().Get())
    failures.check(len(rest_transforms) == len(joints),
                   "restTransforms and joints are different lengths")
    rest_by_bone = {}
    for token, transform in zip(joints, rest_transforms):
        rest_by_bone[token.split("/")[-1]] = transform

    for index, bone in bound_index.items():
        chain = path_to(document, bound_index, index)
        expected = [sum(document.joints[i].offset[axis] for i in chain) * scale
                    for axis in range(3)]
        actual = rest_by_bone[bone].ExtractTranslation()
        failures.check(
            near_vector(actual, expected),
            f"the rest translation of {bone} is {tuple(actual)}; the file's "
            f"OFFSET chain {[document.joints[i].name for i in chain]} says "
            f"{tuple(expected)}")
        # BVH states no rest rotation, and the profile reads the rest as the
        # offsets, so every rest orientation is identity. A non-identity one
        # here would be a rotation invented by the writer.
        rotation = rest_by_bone[bone].ExtractRotationQuat()
        failures.check(
            near(rotation.GetReal(), 1.0, ANGLE_TOLERANCE),
            f"the rest rotation of {bone} is not identity, and the file states "
            f"no rest rotation")

    # --- the samples, against the motion rows ------------------------------
    translations = animation.GetTranslationsAttr()
    rotations = animation.GetRotationsAttr()
    scales = animation.GetScalesAttr().Get()
    # UsdSkel fetches translations, rotations and scales as a unit and `scales`
    # has no schema fallback: a clip missing it resolves to the rest pose with
    # no error anywhere. This assertion is the scar.
    failures.check(scales is not None and len(scales) == len(joints),
                   "the clip authors no scales, so UsdSkel resolves it to the "
                   "rest pose and the motion silently disappears")

    times = translations.GetTimeSamples()
    failures.check(
        len(times) == len(document.rows),
        f"the clip carries {len(times)} time samples and the file carries "
        f"{len(document.rows)} rows")

    hips_column = document.joints[document.index_of(profile["root"])].column
    hips_slot = clip_bones.index("hips")

    # Frames spread across the session rather than the first few: the first row
    # of this export is a rest-like pose, and a writer that dropped every row
    # after it would pass a check that only looked at the start.
    sampled = [0, 1, len(document.rows) // 2, len(document.rows) - 1]
    for frame in sampled:
        time_code = frame
        values_t = translations.Get(time_code)
        values_r = rotations.Get(time_code)
        if not failures.check(
                values_t is not None and values_r is not None,
                f"frame {frame} has no sample at time code {time_code}"):
            continue

        row = document.rows[frame]
        expected_hips = [row[hips_column + axis] * scale for axis in range(3)]
        failures.check(
            near_vector(values_t[hips_slot], expected_hips),
            f"frame {frame}: hips translation is {tuple(values_t[hips_slot])}; "
            f"the file's root position columns say {tuple(expected_hips)}")

        for index, bone in bound_index.items():
            chain = path_to(document, bound_index, index)
            expected = Gf.Quatd(1.0)
            for joint_index in chain:
                expected = expected * joint_rotation(document, joint_index,
                                                     frame)
            slot = clip_bones.index(bone)
            failures.check(
                near_quat(values_r[slot], expected),
                f"frame {frame}: {bone} is {values_r[slot]}; composing "
                f"{[document.joints[i].name for i in chain]} in "
                f"{euler_order(document.joints[chain[-1]])} order gives "
                f"{expected.GetNormalized()}")

    # --- provenance --------------------------------------------------------
    root = stage.GetDefaultPrim()
    failures.check(root and root.IsValid(), "the clip has no default prim")
    custom = root.GetCustomData().get("source", {}) if root else {}
    failures.check(custom.get("profileId") == PROFILE_ID,
                   f"the clip records profileId {custom.get('profileId')!r}")
    failures.check(custom.get("producer") == profile["producer"],
                   f"the clip records producer {custom.get('producer')!r}, and "
                   f"the profile says {profile['producer']!r}")
    failures.check(custom.get("format") == "bvh",
                   f"the clip records format {custom.get('format')!r}")
    failures.check(
        custom.get("sourceId") == bvh.name,
        f"the clip records sourceId {custom.get('sourceId')!r}, which should "
        f"be the file's name and not the path it was read from")
    failures.check(custom.get("frames") == str(len(document.rows)),
                   f"the clip records {custom.get('frames')!r} frames")

    # --- the binding -------------------------------------------------------
    binding = UsdSkel.BindingAPI(skeleton.GetPrim())
    targets = binding.GetAnimationSourceRel().GetTargets()
    failures.check(
        targets == [animation.GetPrim().GetPath()],
        f"the skeleton's animation source is {targets}, not the clip beside it")


def check_determinism(failures: Failures, tool: str, bvh: pathlib.Path,
                      profile_dir: pathlib.Path, work: pathlib.Path) -> None:
    """The same input twice gives the same bytes.

    Named in the plan's converter tests as `deterministic output`, and it is not
    free: a joint set built from a hash, or a report ordered by iteration order,
    would produce two clips that differ in the diff and agree in the motion.
    """
    first = work / "determinism-a.usda"
    second = work / "determinism-b.usda"
    for output in (first, second):
        result = run_tool(tool, str(bvh), "--profile", PROFILE_ID,
                          "--profile-dir", str(profile_dir), "--output",
                          str(output), "--quiet")
        if not failures.check(result.returncode == 0,
                              f"determinism run failed: {result.stderr}"):
            return
    failures.check(
        first.read_bytes() == second.read_bytes(),
        "two conversions of one file produced two different clips")


def check_refusals(failures: Failures, tool: str, bvh: pathlib.Path,
                   corpus: pathlib.Path, profile_dir: pathlib.Path,
                   work: pathlib.Path) -> None:
    """The refusals, and their exit codes.

    Each one is a way this tool must *not* produce a result, and the exit code
    is the claim about whose input was wrong: 1 the recorded file, 2 the command
    or something it named.
    """
    output = work / "refused.usda"

    # No profile named. There is no default and no automatic fallback, and the
    # frozen set names this event, so the code is what the tool prints.
    result = run_tool(tool, str(bvh), "--output", str(output))
    failures.check(result.returncode == 2,
                   f"a run with no --profile exited {result.returncode}")
    failures.check("VRM_BVH_PROFILE_REQUIRED" in result.stderr,
                   f"a run with no --profile did not raise "
                   f"VRM_BVH_PROFILE_REQUIRED: {result.stderr}")

    # A profile id nothing provides. The refusal lists where it looked, because
    # "profile not found" with no list is the least actionable thing this tool
    # could say.
    result = run_tool(tool, str(bvh), "--profile", "no-such-profile-v1",
                      "--profile-dir", str(profile_dir), "--output",
                      str(output))
    failures.check(result.returncode == 2,
                   f"an unknown profile id exited {result.returncode}")
    failures.check(str(profile_dir) in result.stderr,
                   "the refusal does not say where it looked")

    # A profile that does not describe this rig. The generated corpus is format
    # shapes rather than a producer's export, so no shipped profile matches one.
    #
    # Required rather than skipped-if-absent. It used to be optional here and
    # mandatory in `check_a_profile_this_repository_did_not_ship`, so the same
    # missing file made one check report success having verified nothing and the
    # other report a failure -- and "is the generated corpus required?" had two
    # answers in one suite. It is committed, so the answer is yes.
    mismatched = corpus / "generated" / "valid-nested-joints.bvh"
    if failures.check(mismatched.exists(),
                      f"the generated fixture is not at {mismatched}"):
        result = run_tool(tool, str(mismatched), "--profile", PROFILE_ID,
                          "--profile-dir", str(profile_dir), "--output",
                          str(output))
        failures.check(
            result.returncode == 1,
            f"a profile mismatch exited {result.returncode}; the file is the "
            f"input that was wrong")
        failures.check("VRM_BVH_PROFILE_MISMATCH" in result.stderr,
                       f"a profile mismatch did not raise "
                       f"VRM_BVH_PROFILE_MISMATCH: {result.stderr}")

    # A file that is not a BVH document at all: a syntax refusal, and the
    # profile is never reached.
    broken = work / "broken.bvh"
    broken.write_text("HIERARCHY\nROOT\n", encoding="utf-8")
    result = run_tool(tool, str(broken), "--profile", PROFILE_ID,
                      "--profile-dir", str(profile_dir), "--output",
                      str(output))
    failures.check(result.returncode == 1,
                   f"an unparseable file exited {result.returncode}")
    failures.check("VRM_BVH_PARSE_FAILED" in result.stderr,
                   f"an unparseable file did not raise VRM_BVH_PARSE_FAILED: "
                   f"{result.stderr}")

    # A file named as a profile whose id is not the one asked for. Refused
    # rather than accepted, because a conversion records the id it was given and
    # "which profile was used" has to stay a fact about the run.
    renamed = work / "renamed-profile.yaml"
    renamed.write_text(
        (profile_dir / f"{PROFILE_ID}.yaml").read_text(encoding="utf-8"),
        encoding="utf-8")
    result = run_tool(tool, str(bvh), "--profile", "renamed-profile",
                      "--profile-dir", str(work), "--output", str(output))
    failures.check(
        result.returncode == 2,
        f"a profile file whose id is not the one asked for exited "
        f"{result.returncode}")
    failures.check(
        PROFILE_ID in result.stderr,
        f"the id-mismatch refusal does not name the id the file states: "
        f"{result.stderr}")

    # The same file named as a path rather than as an id is accepted: naming a
    # file is naming it, and a profile under review has every reason not to be
    # called after its id yet.
    result = run_tool(tool, str(bvh), "--profile", str(renamed), "--output",
                      str(output), "--quiet")
    failures.check(
        result.returncode == 0,
        f"a profile named by path was refused for its file name: "
        f"{result.stderr}")

    # A missing parent directory is **created**, not refused, and that is
    # `SdfLayer::CreateNew`'s behaviour rather than this tool's -- `motion_capture`
    # does the same. Pinned rather than left implicit, because it is the kind of
    # thing a reader assumes goes the other way.
    nested = work / "made-on-demand" / "clip.usda"
    result = run_tool(tool, str(bvh), "--profile", PROFILE_ID, "--profile-dir",
                      str(profile_dir), "--output", str(nested), "--quiet")
    failures.check(
        result.returncode == 0 and nested.exists(),
        f"an output under a directory that did not exist was refused: "
        f"{result.stderr}")

    # An output that genuinely cannot be written: the path is a directory. Not a
    # diagnostic about the recording -- nothing is wrong with it -- so this
    # prints a plain message and no VRM_BVH code.
    result = run_tool(tool, str(bvh), "--profile", PROFILE_ID, "--profile-dir",
                      str(profile_dir), "--output", str(work))
    failures.check(result.returncode != 0,
                   "a clip that could not be written reported success")
    failures.check("VRM_BVH_" not in result.stderr,
                   f"an output failure was reported as a diagnostic about the "
                   f"recorded file: {result.stderr}")


# A profile nobody here ships, for a rig no shipped profile describes. Written
# out in full rather than derived from one of the shipped files: a fixture built
# by editing `mocopi-mobile-bvh-default-v1.yaml` would inherit whatever that file
# happens to do, and the claim is about somebody starting from the documented
# keys with a rig of their own.
#
# The rig is `valid-nested-joints.bvh` from the *generated* corpus — four joints,
# a torso and one leg — which `check_refusals` above already uses as the file no
# shipped profile matches. That it is a format shape rather than an export is the
# point here: this profile describes a skeleton that exists nowhere but in this
# repository's fixtures, so nothing in the conversion can be reaching for a
# producer it recognises.
USER_PROFILE_ID = "studio-custom-bvh-v1"
USER_PROFILE_PRODUCER = "A studio that is not in this repository"
USER_PROFILE = f"""\
schemaVersion: 1
id: {USER_PROFILE_ID}
producer: {USER_PROFILE_PRODUCER}

coordinates:
  handedness: right
  upAxis: +Y
  forwardAxis: +Z
  translationUnit: centimeters

root:
  joint: Hips
  translation: absolute-position
  rotation: body-orientation

restPose: rest-offsets
unmappedJoints: refuse

joints:
  Hips:       {{ bone: hips,         required: true }}
  Spine:      {{ bone: spine,        required: true }}
  Head:       {{ bone: head,         required: true }}
  LeftUpLeg:  {{ bone: leftUpperLeg, required: true }}
"""


def check_a_profile_this_repository_did_not_ship(
        failures: Failures, tool: str, corpus: pathlib.Path,
        recorded: pathlib.Path, work: pathlib.Path) -> None:
    """A user-defined profile, by path, for a rig neither producer wrote.

    This is the release condition that says the profile contract is *usable from
    outside* rather than merely documented. Everything else in this suite drives
    a file this repository measured through a profile this repository wrote, and
    two artifacts by the same hand agreeing proves less than it looks like.

    Three things have to hold at once for the claim to be worth anything, so all
    three are checked here rather than assumed from the exit code:

    * the profile is read **from a path outside any search directory** — no
      `--profile-dir` is passed, and the file is in a temporary directory that
      does not exist when the tool is built;
    * it describes a rig neither shipped profile matches, so a conversion that
      succeeded by recognising a producer would fail here; and
    * what the clip records as its provenance is the *user's* id and producer,
      because a profile that converted a file and then labelled the result with
      something else would make "which profile was used" unanswerable.
    """
    bvh = corpus / "generated" / "valid-nested-joints.bvh"
    if not failures.check(bvh.exists(),
                          f"the generated fixture is not at {bvh}"):
        return

    profile = work / "not-shipped-anywhere.yaml"
    profile.write_text(USER_PROFILE, encoding="utf-8")

    output = work / "user-defined.usda"
    # No `--profile-dir`: the file is named by path, which is the whole of what
    # a user outside this repository has.
    result = run_tool(tool, str(bvh), "--profile", str(profile), "--output",
                      str(output))
    if not failures.check(
            result.returncode == 0,
            f"a user-defined profile was refused: {result.stderr}"):
        return

    stage = Usd.Stage.Open(str(output))
    if not failures.check(stage is not None,
                          "the user-defined conversion wrote no openable "
                          "stage"):
        return

    animation = next((UsdSkel.Animation(prim) for prim in stage.Traverse()
                      if prim.IsA(UsdSkel.Animation)), None)
    if not failures.check(animation is not None,
                          "the user-defined clip has no SkelAnimation"):
        return

    document = read_bvh(bvh)
    joints = list(animation.GetJointsAttr().Get())
    bones = sorted(token.split("/")[-1] for token in joints)
    expected = sorted(["hips", "spine", "head", "leftUpperLeg"])
    failures.check(bones == expected,
                   f"the user-defined clip's bones are {bones}, and the "
                   f"profile maps {expected}")

    # The **paths**, not the leaves. Sorted leaf tokens are identical for every
    # possible parenting, so a converter that mis-parented a rig it had no
    # shipped profile for -- head and spine swapped, say -- passed the check
    # above unchanged. This is the only test that drives a user-authored
    # profile, so it is the only place that mistake could be caught.
    paths = {token.split("/")[-1]: token for token in joints}
    for bone, parent in (("spine", "hips"), ("head", "spine"),
                         ("leftUpperLeg", "hips")):
        expected_path = f"{paths.get(parent, '?')}/{bone}"
        failures.check(
            paths.get(bone) == expected_path,
            f"the user-defined clip puts {bone} at {paths.get(bone)!r}, and "
            f"the file's hierarchy puts it at {expected_path!r}")

    rotations = animation.GetRotationsAttr()
    failures.check(
        len(rotations.GetTimeSamples()) == len(document.rows),
        f"the clip carries {len(rotations.GetTimeSamples())} sample(s) for the "
        f"file's {len(document.rows)} row(s)")

    # A converted **value**, so that reading the file for its identity and its
    # joint list is not all this proves. The profile declares
    # `translationUnit: centimeters` and `root.translation: absolute-position`;
    # the file's first row puts the root at Y=90. A converter that honoured the
    # `joints:` map and then fell back to a built-in default for the unit would
    # satisfy every other check here and author 90 metres.
    translations = animation.GetTranslationsAttr()
    hips_index = joints.index(paths["hips"])
    first = translations.Get(translations.GetTimeSamples()[0])
    root_y = first[hips_index][1] if first else None
    failures.check(
        root_y is not None and abs(root_y - 0.90) < 1e-5,
        f"the user-defined clip puts the root at Y={root_y}, and the profile's "
        f"centimetres put the file's 90.0 at 0.90 m")

    root = stage.GetDefaultPrim()
    custom = root.GetCustomData().get("source", {}) if root else {}
    failures.check(custom.get("profileId") == USER_PROFILE_ID,
                   f"the clip records profileId {custom.get('profileId')!r}, "
                   f"and the user's profile says {USER_PROFILE_ID!r}")
    failures.check(custom.get("producer") == USER_PROFILE_PRODUCER,
                   f"the clip records producer {custom.get('producer')!r}, and "
                   f"the user's profile says {USER_PROFILE_PRODUCER!r}")

    # And the same profile against a rig it does not describe is still refused,
    # so what passed above is this profile matching this rig rather than a file
    # named by path being trusted. Unguarded, and `recorded` is the path `main`
    # already resolved and already exits on: the guard that used to be here read
    # as "this half is optional" while being unable to take its false branch,
    # which is the opposite of what this half is for.
    result = run_tool(tool, str(recorded), "--profile", str(profile),
                      "--output", str(work / "user-defined-mismatch.usda"))
    failures.check(
        result.returncode == 1
        and "VRM_BVH_PROFILE_MISMATCH" in result.stderr,
        f"a user-defined profile was accepted for a rig it does not "
        f"describe: exit {result.returncode}, {result.stderr}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tool", required=True)
    parser.add_argument("--corpus", type=pathlib.Path, required=True,
                        help="libs/motionBvh/tests/corpus")
    parser.add_argument("--profiles", type=pathlib.Path, required=True,
                        help="profiles/motion")
    arguments = parser.parse_args()

    bvh = arguments.corpus / "recorded" / "redistributable" / RECORDED
    if not bvh.exists():
        print(f"the recorded export is not at {bvh}", file=sys.stderr)
        return 1
    profile = arguments.profiles / f"{PROFILE_ID}.yaml"
    if not profile.exists():
        print(f"the shipped profile is not at {profile}", file=sys.stderr)
        return 1

    failures = Failures()
    with tempfile.TemporaryDirectory() as directory:
        work = pathlib.Path(directory)
        check_conversion(failures, arguments.tool, bvh, arguments.profiles,
                         work)
        check_determinism(failures, arguments.tool, bvh, arguments.profiles,
                          work)
        check_refusals(failures, arguments.tool, bvh, arguments.corpus,
                       arguments.profiles, work)
        check_a_profile_this_repository_did_not_ship(
            failures, arguments.tool, arguments.corpus, bvh, work)
    return failures.report()


if __name__ == "__main__":
    raise SystemExit(main())
