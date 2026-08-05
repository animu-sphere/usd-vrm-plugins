#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Check each shipped motion profile against the recorded file it describes.

`motionSource_shippedProfiles` proves every profile in `profiles/motion/` is one
the library can read. That is a check of the file's *form*: a profile can be
perfectly well-formed and describe no rig anybody has -- a joint name that does
not exist in the export, a hierarchy the profile assumes and the rig does not
have, a joint the export carries that the profile neither maps nor ignores.

Nothing in C++ can check that today. A profile is matched against a rig, and
turning a recorded file into one is the reader's extractor, which is not written
(WORKSPACE.md §2, roadmap §12 item 9). So this script reads both sides itself:
the hierarchy out of the recorded file, the mapping out of the profile, and the
claim that the second describes the first.

That is also why it lives here rather than in either library. `motionSource` is
forbidden to know a reader exists and `motionBvh` is forbidden to know a producer
does; a caller holding one of each is exactly what neither may be, and in this
workspace a caller is a tool or a script.

Three things it deliberately re-derives rather than calls into:

* **The file's hierarchy**, scanned from the format, the way
  `libs/motionBvh/tools/check_corpus.py` measures the corpus. A profile checked
  against the same parser the pipeline uses would be two implementations
  agreeing with each other. Where the bytes are not here at all -- a recording
  under a licence this repository may not carry leaves a manifest row and no
  file (roadmap §8) -- the row's own measured `hierarchy` stands in, and the
  check is the same one. That is the point of the field: what a profile is held
  against is a rig, and a rig is names and parents.
* **The profile's keys**, read by a small reader of the same stated subset
  `SourceProfileFile.h` defines. A profile file is data a human wrote, and the
  second reading is what catches a key that reads one way to a person and
  another to the parser.
* **The humanoid parent relation**, for the bones the profiles actually use.
  This is a duplicate of `motionCore`'s taxonomy and is bounded on purpose: a
  bone no table below names is an error rather than a skipped check, so this
  file cannot quietly become a second full vocabulary.

    check_motion_profiles.py [--profiles DIR] [--corpus DIR]
"""

from __future__ import annotations

import argparse
import json
import pathlib
import sys

# The canonical humanoid's parent relation, `motionCore/Humanoid.h`, restricted
# to the bones the shipped profiles map. Fingers, eyes and the jaw are absent
# because no profile maps one; the day one does, this table grows in the same
# review rather than the check silently passing.
HUMANOID_PARENT = {
    "hips": None,
    "spine": "hips",
    "chest": "spine",
    "upperChest": "chest",
    "neck": "upperChest",
    "head": "neck",
    "leftUpperLeg": "hips",
    "leftLowerLeg": "leftUpperLeg",
    "leftFoot": "leftLowerLeg",
    "leftToes": "leftFoot",
    "rightUpperLeg": "hips",
    "rightLowerLeg": "rightUpperLeg",
    "rightFoot": "rightLowerLeg",
    "rightToes": "rightFoot",
    "leftShoulder": "upperChest",
    "leftUpperArm": "leftShoulder",
    "leftLowerArm": "leftUpperArm",
    "leftHand": "leftLowerArm",
    "rightShoulder": "upperChest",
    "rightUpperArm": "rightShoulder",
    "rightLowerArm": "rightUpperArm",
    "rightHand": "rightLowerArm",
}


class Refused(Exception):
    pass


# --- the profile file, read again ----------------------------------------


def strip_comment(line: str) -> str:
    quoted = False
    escaped = False
    for index, char in enumerate(line):
        if escaped:
            escaped = False
        elif quoted and char == "\\":
            escaped = True
        elif char == '"':
            quoted = not quoted
        elif not quoted and char == "#" and (index == 0 or line[index - 1] == " "):
            return line[:index]
    return line


def scalar(text: str, where: str) -> str:
    text = text.strip()
    if not text:
        raise Refused(f"{where}: expected a value")
    if text.startswith('"'):
        out: list[str] = []
        index = 1
        while index < len(text):
            char = text[index]
            if char == "\\" and index + 1 < len(text):
                out.append(text[index + 1])
                index += 2
                continue
            if char == '"':
                if text[index + 1:].strip():
                    raise Refused(f"{where}: text after a quoted value")
                return "".join(out)
            out.append(char)
            index += 1
        raise Refused(f"{where}: no closing quote")
    return text


def split_outside_quotes(text: str, separator: str) -> list[str]:
    parts: list[str] = []
    quoted = False
    escaped = False
    start = 0
    for index, char in enumerate(text):
        if escaped:
            escaped = False
        elif quoted and char == "\\":
            escaped = True
        elif char == '"':
            quoted = not quoted
        elif not quoted and char == separator:
            parts.append(text[start:index])
            start = index + 1
    parts.append(text[start:])
    return parts


def split_key(text: str, where: str) -> tuple[str, str]:
    if text.startswith('"'):
        index = 1
        while index < len(text):
            if text[index] == "\\":
                index += 2
                continue
            if text[index] == '"':
                break
            index += 1
        if index >= len(text):
            raise Refused(f"{where}: no closing quote")
        key = scalar(text[: index + 1], where)
        rest = text[index + 1:].lstrip()
        if not rest.startswith(":"):
            raise Refused(f"{where}: expected ':' after the key")
        return key, rest[1:].strip()
    for index, char in enumerate(text):
        if char == ":" and (index + 1 == len(text) or text[index + 1] == " "):
            return text[:index].strip(), text[index + 1:].strip()
    raise Refused(f"{where}: expected 'key: value'")


def read_flow(text: str, close: str, where: str) -> list[str]:
    if not text.endswith(close):
        raise Refused(f"{where}: expected a closing '{close}'")
    inner = text[1:-1].strip()
    if not inner:
        return []
    return [part.strip() for part in split_outside_quotes(inner, ",")]


def read_value(text: str, lines: list[tuple[int, int, str]], index: int,
               indent: int, where: str):
    """A value on the line, or the more-indented block under it."""
    if text.startswith("{"):
        entries = {}
        for part in read_flow(text, "}", where):
            key, rest = split_key(part, where)
            entries[key] = scalar(rest, where)
        return entries, index
    if text.startswith("["):
        return [scalar(part, where) for part in read_flow(text, "]", where)], index
    if text:
        return scalar(text, where), index
    if index >= len(lines):
        raise Refused(f"{where}: states no value")
    # A sequence may sit at its key's own indentation as well as under it, which
    # is how the format is ordinarily written. A mapping may not: a key at the
    # parent's indentation is the parent's.
    own_indent_sequence = (lines[index][1] == indent
                           and lines[index][2].startswith("- "))
    if lines[index][1] <= indent and not own_indent_sequence:
        raise Refused(f"{where}: states no value")
    if lines[index][2].startswith("- "):
        items = []
        child = lines[index][1]
        while (index < len(lines) and lines[index][1] == child
               and lines[index][2].startswith("- ")):
            items.append(scalar(lines[index][2][1:].strip(), where))
            index += 1
        return items, index
    return read_mapping(lines, index, lines[index][1], where)


def read_mapping(lines: list[tuple[int, int, str]], index: int, indent: int,
                 where: str):
    entries: dict = {}
    while index < len(lines) and lines[index][1] >= indent:
        number, own, text = lines[index]
        if own > indent:
            raise Refused(f"{where}: unexpected indentation on line {number}")
        key, rest = split_key(text, f"{where} line {number}")
        if key in entries:
            raise Refused(f"{where}: key '{key}' is stated twice")
        index += 1
        entries[key], index = read_value(rest, lines, index, indent,
                                         f"{where} line {number}")
    return entries, index


def read_profile(path: pathlib.Path) -> dict:
    lines: list[tuple[int, int, str]] = []
    for number, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if "\t" in raw[: len(raw) - len(raw.lstrip(" "))]:
            raise Refused(f"{path.name}: line {number} indents with a tab")
        text = strip_comment(raw).rstrip()
        if text.strip():
            lines.append((number, len(text) - len(text.lstrip(" ")), text.strip()))
    document, index = read_mapping(lines, 0, 0, path.name)
    if index != len(lines):
        raise Refused(f"{path.name}: trailing content")
    return document


# --- the same file, through a real YAML implementation --------------------
#
# The reader in `motionSource` is a stated subset, written rather than borrowed:
# a profile needs an unknown key to be an error, and YAML's implicit typing would
# read a joint named `on`, `y` or `null` as something other than its name. Both
# are reasons to keep the dependency out of the library.
#
# Neither is a reason to skip the check a real implementation can make for free.
# What is claimed is not "everything YAML accepts, this accepts" -- deliberately
# false, and the header says which constructs are refused -- but "a file this
# accepts means to it what it means to YAML". A refusal is a bad file's worst
# outcome; a *silent difference of interpretation* is the failure a hand-written
# reader can produce and a refusal cannot, and it is the one this catches.
#
# Skipped, and said to be skipped, where PyYAML is not installed: it is a check
# on files this repository ships, not a build dependency of anything.


def yaml_reading(path: pathlib.Path):
    try:
        import yaml
    except ImportError:
        return None
    return yaml.safe_load(path.read_text(encoding="utf-8"))


def as_text(value) -> str:
    """One YAML scalar in the form this reader would have produced."""
    if isinstance(value, bool):
        return "true" if value else "false"
    if value is None:
        return "null"
    return str(value)


def as_ours(value):
    if isinstance(value, dict):
        return {as_text(key): as_ours(item) for key, item in value.items()}
    if isinstance(value, list):
        return [as_ours(item) for item in value]
    return as_text(value)


def disagreements(ours, theirs, where: str) -> list[str]:
    """Where the two readings of one file differ.

    Scalars are compared without case, because every vocabulary lookup in the
    library is ASCII case-insensitive -- so a case difference cannot change what
    a profile means, and only YAML's own `True` for a written `TRUE` produces
    one. A difference in characters is a difference in meaning and is reported.
    """
    if isinstance(ours, dict) or isinstance(theirs, dict):
        if not (isinstance(ours, dict) and isinstance(theirs, dict)):
            return [f"{where}: one reading is a mapping and the other is not"]
        errors = []
        for key in sorted(set(ours) | set(theirs)):
            if key not in ours:
                errors.append(f"{where}: YAML reads a key '{key}' that this "
                              f"reader does not")
            elif key not in theirs:
                errors.append(f"{where}: YAML does not read the key '{key}'")
            else:
                errors += disagreements(ours[key], theirs[key],
                                        f"{where}.{key}")
        return errors
    if isinstance(ours, list) or isinstance(theirs, list):
        if not (isinstance(ours, list) and isinstance(theirs, list)):
            return [f"{where}: one reading is a sequence and the other is not"]
        if len(ours) != len(theirs):
            return [f"{where}: {len(ours)} entries here, {len(theirs)} in YAML"]
        errors = []
        for index, (mine, other) in enumerate(zip(ours, theirs)):
            errors += disagreements(mine, other, f"{where}[{index}]")
        return errors
    if str(ours).lower() != str(theirs).lower():
        return [f"{where}: '{ours}' here, '{theirs}' in YAML"]
    return []


# --- the recorded file's hierarchy, scanned from the format ---------------


def read_hierarchy(path: pathlib.Path) -> list[tuple[str, int]]:
    """Joint names with their parent index, parent before child."""
    tokens = path.read_text(encoding="utf-8", errors="strict").split()
    joints: list[tuple[str, int]] = []
    stack: list[int] = []
    index = 0
    pending: int | None = None
    end_site = False
    while index < len(tokens):
        token = tokens[index]
        lowered = token.lower()
        if lowered == "motion":
            break
        if lowered in ("root", "joint"):
            index += 1
            if index >= len(tokens):
                raise Refused(f"{path.name}: a joint has no name")
            pending = len(joints)
            joints.append((tokens[index], stack[-1] if stack else -1))
        elif lowered == "end":
            end_site = True
        elif token == "{":
            if end_site:
                end_site = False
                # An End Site's braces are skipped whole: it is not a joint, and
                # counting it as one would put a name no profile can map into
                # every check below.
                depth = 1
                while index + 1 < len(tokens) and depth:
                    index += 1
                    depth += 1 if tokens[index] == "{" else 0
                    depth -= 1 if tokens[index] == "}" else 0
            elif pending is None:
                raise Refused(f"{path.name}: a block opens outside a joint")
            else:
                stack.append(pending)
                pending = None
        elif token == "}":
            if not stack:
                raise Refused(f"{path.name}: unbalanced '}}'")
            stack.pop()
        index += 1
    if not joints:
        raise Refused(f"{path.name}: no joint")
    return joints


# --- the claim -------------------------------------------------------------


def is_ancestor(joints: list[tuple[str, int]], ancestor: int, joint: int) -> bool:
    current = joints[joint][1]
    while current >= 0:
        if current == ancestor:
            return True
        current = joints[current][1]
    return False


def check(profile: dict, profile_name: str, joints: list[tuple[str, int]],
          fixture: str, expected_bones: list[str] | None) -> list[str]:
    errors: list[str] = []
    names = [name for name, _ in joints]
    where = f"{profile_name} vs {fixture}"

    # Refused rather than indexed into. The C++ side validates a profile's shape
    # and would fail first, but the two checks are independent tests: this one
    # meeting a profile without a joint map has to say so, not raise a
    # traceback at whoever is reading the other failure.
    try:
        mapped: dict[str, str] = {
            name: entry["bone"] for name, entry in profile["joints"].items()
        }
        root_joint = profile["root"]["joint"]
    except (KeyError, TypeError, AttributeError) as broken:
        raise Refused(f"{where}: the profile states no {broken}") from broken
    ignored = set(profile.get("ignoredJoints", []))

    if names[0] != root_joint:
        errors.append(f"{where}: the profile roots at '{root_joint}'"
                      f"; the file roots at '{names[0]}'")

    bound: dict[str, int] = {}
    for name, bone in mapped.items():
        found = [index for index, joint in enumerate(names) if joint == name]
        if not found:
            errors.append(f"{where}: the file carries no joint '{name}'")
            continue
        if len(found) > 1:
            errors.append(f"{where}: the file carries '{name}' {len(found)} times")
            continue
        if bone not in HUMANOID_PARENT:
            errors.append(f"{where}: '{bone}' is not in this check's humanoid "
                          f"table; add it here in review")
            continue
        bound[bone] = found[0]

    for name in sorted(ignored):
        if name not in names:
            errors.append(f"{where}: the file carries no ignored joint '{name}'")

    for index, name in enumerate(names):
        if name not in mapped and name not in ignored:
            errors.append(f"{where}: joint {index} '{name}' is neither mapped nor "
                          f"ignored, and the profile refuses that")

    # The near-miss profile: every name matched and the body is assembled wrong.
    for bone, joint in sorted(bound.items()):
        ancestor = HUMANOID_PARENT[bone]
        while ancestor is not None and ancestor not in bound:
            ancestor = HUMANOID_PARENT[ancestor]
        if ancestor is None:
            continue
        if not is_ancestor(joints, bound[ancestor], joint):
            errors.append(
                f"{where}: '{names[joint]}' carries {bone} but does not sit under "
                f"'{names[bound[ancestor]]}', which carries {ancestor}")

    if expected_bones is not None and sorted(expected_bones) != sorted(mapped.values()):
        errors.append(f"{where}: the manifest's expectedMappedBones and the "
                      f"profile's joint map disagree")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    root = pathlib.Path(__file__).resolve().parent.parent
    parser.add_argument("--profiles", type=pathlib.Path,
                        default=root / "profiles" / "motion")
    parser.add_argument("--corpus", type=pathlib.Path,
                        default=root / "libs" / "motionBvh" / "tests" / "corpus")
    arguments = parser.parse_args()

    profiles: dict[str, dict] = {}
    errors: list[str] = []
    conformed = 0
    for path in sorted(arguments.profiles.glob("*.yaml")):
        try:
            profile = read_profile(path)
        except Refused as refusal:
            errors.append(str(refusal))
            continue
        if profile.get("id") != path.stem:
            errors.append(f"{path.name}: states id '{profile.get('id')}'")
            continue
        theirs = yaml_reading(path)
        if theirs is not None:
            errors += disagreements(profile, as_ours(theirs), path.name)
            conformed += 1
        profiles[path.stem] = profile
    if not profiles and not errors:
        errors.append(f"no profile in {arguments.profiles}")

    manifest_path = arguments.corpus / "recorded" / "manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    recorded = arguments.corpus / "recorded"
    from_file = from_manifest = 0
    for fixture in manifest["fixtures"]:
        profile_id = fixture.get("profileId")
        if profile_id is None:
            # A recorded file no profile describes yet. Not an error: the corpus
            # is the evidence a profile is written *from*, so a file arrives
            # before one exists (roadmap §8).
            continue
        if profile_id not in profiles:
            errors.append(f"{fixture['file']}: no profile '{profile_id}'")
            continue
        # The bytes if they are here, and the row's own reading of them if they
        # are not. A recording this repository may not redistribute leaves a
        # manifest row and nothing else (roadmap §8), and the claim this script
        # makes -- that a profile describes this rig -- needs a hierarchy and
        # not a file. `hierarchy` is a measured field, written by the scanner in
        # `libs/motionBvh/tools/check_corpus.py` from the bytes and re-derived
        # whenever anyone fetches them, so reading it here is not this check
        # marking its own homework: what a person wrote is the profile, and the
        # two sides still come from different hands.
        try:
            path = next((candidate for candidate in
                         (recorded / "redistributable" / fixture["file"],
                          recorded / "fetched" / fixture["file"])
                         if candidate.exists()), None)
            if path is not None:
                joints = read_hierarchy(path)
                # Only against the file: from the manifest this would be the row
                # agreeing with itself, and the scanner is what pins it anyway.
                if len(joints) != fixture["joints"]:
                    errors.append(f"{fixture['file']}: {len(joints)} joints, "
                                  f"manifest says {fixture['joints']}")
                from_file += 1
            elif fixture.get("hierarchy"):
                joints = [(joint["name"], joint["parent"])
                          for joint in fixture["hierarchy"]]
                from_manifest += 1
            else:
                errors.append(f"{fixture['file']}: names profile "
                              f"'{profile_id}' but has neither bytes here nor a "
                              f"hierarchy, so nothing checks the claim")
                continue
            errors.extend(check(profiles[profile_id], profile_id, joints,
                                fixture["file"],
                                fixture.get("expectedMappedBones")))
        except Refused as refusal:
            errors.append(str(refusal))
            continue

    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    conformance = (f"{conformed} agreed with a YAML implementation"
                   if conformed else "no YAML implementation to agree with")
    against = f"{from_file} checked against a recorded file"
    if from_manifest:
        against += f", {from_manifest} against a manifest hierarchy"
    print(f"motion profiles: {len(profiles)} read, {against}, {conformance}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
