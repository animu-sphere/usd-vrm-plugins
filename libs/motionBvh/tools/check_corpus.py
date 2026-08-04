#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Measure the BVH corpus independently, and check the manifest against it.

`motionBvh_corpus` proves the fixtures say what the C++ parser reads out of
them. That is one implementation agreeing with itself: a parser bug that
misreads a channel count and a fixture written to match it would both be green.

So this scanner is a second, deliberately independent reading of the same
files -- written from the format, not from `BvhParser.cpp` -- and the manifest's
measured fields are checked against it. Where the two disagree, one of them is
wrong and the fixture is not evidence of anything until that is settled.

    check_corpus.py --check    verify manifest.json against the committed files
    check_corpus.py --update   rewrite the measured fields, keeping the prose
    check_corpus.py --recorded  ... over recorded/ instead of generated/

The two halves are measured by the same scanner and kept in separate manifests,
because they are separate kinds of claim. A generated fixture pins a *shape of
the format* and its counts are a property of what was written; a recorded file
pins *what one producer actually exports*, and its counts are a measurement of
someone else's software that nobody here may adjust. Only the second kind has
provenance, and only the second kind can stop being reproducible.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import pathlib
import sys

CHANNELS = {
    "xposition", "yposition", "zposition",
    "xrotation", "yrotation", "zrotation",
}

STRUCTURAL = {"joint", "end", "offset", "channels", "motion", "root", "{", "}"}

PARSE_FAILED = "VRM_BVH_PARSE_FAILED"
UNSUPPORTED_CHANNEL = "VRM_BVH_UNSUPPORTED_CHANNEL"
FRAME_WIDTH_MISMATCH = "VRM_BVH_FRAME_WIDTH_MISMATCH"
INVALID_FRAME_TIME = "VRM_BVH_INVALID_FRAME_TIME"
NON_FINITE_VALUE = "VRM_BVH_NON_FINITE_VALUE"


class Refused(Exception):
    def __init__(self, code: str, detail: str) -> None:
        super().__init__(f"{code}: {detail}")
        self.code = code
        self.detail = detail


class Tokens:
    """Whitespace-delimited tokens, each remembering the line it came from."""

    def __init__(self, text: str) -> None:
        self.items: list[tuple[str, int, int]] = []  # token, line, end column
        for number, line in enumerate(text.splitlines(), start=1):
            column = 0
            for piece in line.split():
                column = line.index(piece, column)
                column += len(piece)
                self.items.append((piece, number, column))
        self.index = 0

    def next(self) -> tuple[str, int, int]:
        if self.index >= len(self.items):
            raise Refused(PARSE_FAILED, "the file ends early")
        item = self.items[self.index]
        self.index += 1
        return item

    def peek(self) -> str | None:
        if self.index >= len(self.items):
            return None
        return self.items[self.index][0]

    def expect(self, word: str) -> None:
        token, _, _ = self.next()
        if token.lower() != word.lower():
            raise Refused(PARSE_FAILED, f"expected {word}, found {token!r}")

    def label(self, word: str) -> None:
        """`Frames:` / `Frame Time:`, one word at a time; the colon is optional."""
        token, _, _ = self.next()
        stripped = token[:-1] if token.endswith(":") else token
        if stripped.lower() != word.lower():
            raise Refused(PARSE_FAILED, f"expected {word}, found {token!r}")
        if not token.endswith(":") and self.peek() == ":":
            self.next()


def _number(token: str, code: str = PARSE_FAILED) -> float:
    try:
        value = float(token)
    except ValueError as exc:
        raise Refused(code, f"{token!r} is not a number") from exc
    if not math.isfinite(value):
        raise Refused(NON_FINITE_VALUE, f"{token!r} is not finite")
    return value


def _joint(tokens: Tokens, name: str, parent: int, joints: list[dict]) -> None:
    index = len(joints)
    joints.append({"name": name, "parent": parent, "channels": []})
    tokens.expect("{")
    seen_offset = False
    seen_channels = False
    seen_end_site = False
    while True:
        token, _, _ = tokens.next()
        lowered = token.lower()
        if token == "}":
            break
        if lowered == "offset":
            if seen_offset:
                raise Refused(PARSE_FAILED, f"{name} carries two OFFSETs")
            for _ in range(3):
                _number(tokens.next()[0])
            seen_offset = True
        elif lowered == "channels":
            if seen_channels:
                raise Refused(PARSE_FAILED, f"{name} carries two CHANNELS")
            count_token, _, _ = tokens.next()
            if not count_token.isdigit():
                raise Refused(PARSE_FAILED, f"CHANNELS {count_token!r}")
            for _ in range(int(count_token)):
                channel, _, _ = tokens.next()
                if channel.lower() in STRUCTURAL:
                    raise Refused(PARSE_FAILED,
                                  f"CHANNELS ran into {channel!r}")
                if channel.lower() not in CHANNELS:
                    raise Refused(UNSUPPORTED_CHANNEL, channel)
                joints[index]["channels"].append(channel.lower())
            seen_channels = True
        elif lowered == "end":
            tokens.expect("Site")
            if seen_end_site:
                raise Refused(PARSE_FAILED, f"{name} carries two End Sites")
            tokens.expect("{")
            tokens.expect("OFFSET")
            for _ in range(3):
                _number(tokens.next()[0])
            tokens.expect("}")
            seen_end_site = True
        elif lowered == "joint":
            child, _, _ = tokens.next()
            if child in ("{", "}"):
                raise Refused(PARSE_FAILED, "JOINT needs a name")
            _joint(tokens, child, index, joints)
        else:
            raise Refused(PARSE_FAILED, f"unexpected {token!r} in {name}")
    if not seen_offset:
        raise Refused(PARSE_FAILED, f"{name} declares no OFFSET")


def scan(text: str) -> dict:
    """Measure a BVH document, or raise `Refused` with the code it earns."""
    if text.startswith("﻿"):
        text = text[1:]

    tokens = Tokens(text)
    tokens.expect("HIERARCHY")
    tokens.expect("ROOT")
    root, _, _ = tokens.next()
    if root in ("{", "}"):
        raise Refused(PARSE_FAILED, "ROOT needs a name")
    joints: list[dict] = []
    _joint(tokens, root, -1, joints)
    channels = sum(len(joint["channels"]) for joint in joints)

    tokens.expect("MOTION")
    tokens.label("Frames")
    frames_token, _, _ = tokens.next()
    if not frames_token.isdigit():
        raise Refused(PARSE_FAILED, f"Frames: {frames_token!r}")
    declared = int(frames_token)

    tokens.label("Frame")
    tokens.label("Time")
    time_token, time_line, time_column = tokens.next()
    frame_time = _number(time_token, INVALID_FRAME_TIME)
    if frame_time < 0.0 or (declared > 1 and frame_time <= 0.0):
        raise Refused(INVALID_FRAME_TIME,
                      f"{frame_time} across {declared} frame(s)")

    # Rows begin just past the frame-time token, so a writer that put the first
    # row on that line is read the same way the C++ parser reads it.
    lines = text.splitlines()
    rows = [lines[time_line - 1][time_column:]] + lines[time_line:]
    read = 0
    for offset, row in enumerate(rows):
        pieces = row.split()
        if not pieces:
            continue
        if read >= declared:
            raise Refused(PARSE_FAILED,
                          f"Frames: declared {declared}, the file carries more")
        if len(pieces) != channels:
            raise Refused(FRAME_WIDTH_MISMATCH,
                          f"frame {read} on line {time_line + offset}: "
                          f"expected {channels}, read {len(pieces)}")
        for piece in pieces:
            _number(piece)
        read += 1
    if read != declared:
        raise Refused(PARSE_FAILED,
                      f"Frames: declared {declared}, read {read}")

    return {
        "joints": len(joints),
        "channels": channels,
        "frames": declared,
        "frameTime": frame_time,
    }


def measure(path: pathlib.Path) -> dict:
    raw = path.read_bytes()
    entry = {
        "file": path.name,
        "bytes": len(raw),
        "sha256": hashlib.sha256(raw).hexdigest(),
    }
    # `surrogateescape`, not strict, and not a refusal either. A producer is
    # free to write joint names in whatever encoding its platform handed it,
    # and the recorded half is where such a file first appears -- but the C++
    # parser reads *bytes*, treats a joint name as an opaque token and would
    # accept the file without noticing. A scanner that refused it would put the
    # two implementations in disagreement over a file neither has a problem
    # with, which is exactly the disagreement this second reading exists to
    # detect. Everything structural in BVH is ASCII, so round-tripping the
    # undecodable bytes through surrogates costs the scan nothing.
    try:
        entry.update(scan(raw.decode("utf-8", errors="surrogateescape")))
        entry["status"] = "valid"
    except Refused as refusal:
        entry["status"] = "refused"
        entry["diagnostic"] = refusal.code
    return entry


# Hand-written per half, carried through `--update` untouched. A measured field
# this script rewrites and a prose field a person wrote must never be the same
# field: the whole point of re-measuring is that nobody edits the numbers.
PROSE = {
    "generated": ("pins",),
    # A recorded file's provenance is the part no scanner can recover. Which
    # application wrote it, which version, what it was called when it came off
    # the device, and what may be done with it are facts about the world, and
    # BVH carries a statement of none of them.
    #
    # `profileId` and `expectedMappedBones` are the same kind of fact one layer
    # up: which producer profile claims to describe this file, and which bones
    # that claim binds. They are listed here so `--update` carries them through
    # untouched -- a re-measurement that dropped a file's profile link would be
    # a scanner deciding a semantic question, which is the one thing it may not
    # do. `scripts/check_motion_profiles.py` is what checks the claim.
    "recorded": ("pins", "producer", "producerVersion", "originalFile",
                 "capturedAt", "redistribution", "profileId",
                 "expectedMappedBones", "observations"),
}
REQUIRED_PROSE = {
    "generated": ("pins",),
    "recorded": ("pins", "producer", "redistribution"),
}


def ordered(entry: dict, previous: dict, half: str) -> dict:
    """One manifest row, with a stable field order for a readable diff."""
    row = {"file": entry["file"], "status": entry["status"]}
    for field in PROSE[half]:
        if field in previous:
            row[field] = previous[field]
    row.setdefault("pins", "TODO: what this pins")
    if entry["status"] == "valid":
        for field in ("joints", "channels", "frames", "frameTime"):
            row[field] = entry[field]
    else:
        row["diagnostic"] = entry["diagnostic"]
    row["bytes"] = entry["bytes"]
    row["sha256"] = entry["sha256"]
    return row


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--corpus", type=pathlib.Path,
                        default=pathlib.Path(__file__).resolve().parents[1]
                        / "tests" / "corpus")
    parser.add_argument(
        "--recorded", action="store_true",
        help="Measure recorded/redistributable against recorded/manifest.json "
             "instead of the generated half.")
    action = parser.add_mutually_exclusive_group()
    action.add_argument("--check", action="store_true", default=True)
    action.add_argument("--update", action="store_true")
    args = parser.parse_args()

    half = "recorded" if args.recorded else "generated"
    if args.recorded:
        files = args.corpus / "recorded" / "redistributable"
        manifest_path = args.corpus / "recorded" / "manifest.json"
    else:
        files = args.corpus / "generated"
        manifest_path = args.corpus / "manifest.json"
    if not files.is_dir():
        print(f"corpus directory not found: {files}", file=sys.stderr)
        return 1

    manifest = json.loads(manifest_path.read_text(encoding="utf-8")) \
        if manifest_path.exists() else {"schemaVersion": 1, "fixtures": []}
    declared = {row["file"]: row for row in manifest.get("fixtures", [])}
    measured = {path.name: measure(path)
                for path in sorted(files.glob("*.bvh"))}

    if args.update:
        manifest["fixtures"] = [
            ordered(measured[name], declared.get(name, {}), half)
            for name in sorted(measured)
        ]
        manifest_path.write_text(
            json.dumps(manifest, indent=2, ensure_ascii=False) + "\n",
            encoding="utf-8")
        print(f"{half} manifest updated: {len(measured)} fixture(s)")
        return 0

    failures: list[str] = []
    for name in sorted(set(measured) | set(declared)):
        if name not in declared:
            failures.append(f"{name}: committed but not in the manifest")
            continue
        if name not in measured:
            failures.append(f"{name}: in the manifest but not committed")
            continue
        want, got = declared[name], measured[name]
        for field in ("status", "bytes", "sha256", "diagnostic", "joints",
                      "channels", "frames", "frameTime"):
            if field in want or field in got:
                if want.get(field) != got.get(field):
                    failures.append(
                        f"{name}: {field} is {got.get(field)!r}, the manifest "
                        f"says {want.get(field)!r}")
        for field in REQUIRED_PROSE[half]:
            if not want.get(field):
                failures.append(f"{name}: the manifest states no {field}")

    if failures:
        print("\n".join(failures), file=sys.stderr)
        return 1
    print(f"motionBvh {half} corpus manifest: {len(measured)} file(s) agree")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
