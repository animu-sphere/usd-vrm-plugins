#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Generate the small, license-free VRMA fixtures used by the importer tests."""

from __future__ import annotations

import argparse
import json
import pathlib
import struct
import sys


FIXTURE_DIR = pathlib.Path(__file__).parents[1] / "tests" / "fixtures"


def _align4(data: bytes) -> bytes:
    return data + b"\0" * ((-len(data)) % 4)


class _Buffer:
    """Accumulates float arrays into one GLB binary chunk."""

    def __init__(self) -> None:
        self._chunks: list[bytes] = []

    def append_floats(self, values: list[float]) -> tuple[int, int]:
        offset = sum(len(chunk) for chunk in self._chunks)
        chunk = struct.pack("<" + "f" * len(values), *values)
        self._chunks.append(chunk)
        padding = (-len(chunk)) % 4
        if padding:
            self._chunks.append(b"\0" * padding)
        return offset, len(chunk)

    def bytes(self) -> bytes:
        return b"".join(self._chunks)


def _glb(document: dict, binary: bytes) -> bytes:
    json_bytes = _align4(
        json.dumps(document, separators=(",", ":"), ensure_ascii=True).encode("utf-8"))
    glb_length = 12 + 8 + len(json_bytes) + 8 + len(binary)
    return (struct.pack("<4sII", b"glTF", 2, glb_length) +
            struct.pack("<I4s", len(json_bytes), b"JSON") + json_bytes +
            struct.pack("<I4s", len(binary), b"BIN\0") + binary)


def _canonical_walk_bytes() -> bytes:
    """Body motion only: rotations plus the hips translation track."""
    buffer = _Buffer()
    append_floats = buffer.append_floats

    times_offset, times_length = append_floats([0.0, 1.0])
    hips_rotation_offset, hips_rotation_length = append_floats([
        0.0, 0.0, 0.0, 1.0,
        0.0, 0.70710677, 0.0, 0.70710677,
    ])
    chest_rotation_offset, chest_rotation_length = append_floats([
        0.0, 0.0, 0.0, 1.0,
        0.70710677, 0.0, 0.0, 0.70710677,
    ])
    hips_translation_offset, hips_translation_length = append_floats([
        0.0, 1.0, 0.0,
        0.0, 1.0, 0.5,
    ])
    binary = buffer.bytes()

    document = {
        "asset": {"version": "2.0", "generator": "usd-vrm-plugins fixture generator"},
        "extensionsUsed": ["VRMC_vrm_animation"],
        "extensions": {
            "VRMC_vrm_animation": {
                "specVersion": "1.0",
                "humanoid": {
                    "humanBones": {
                        "hips": {"node": 0},
                        "spine": {"node": 1},
                        "chest": {"node": 2},
                    }
                },
            }
        },
        "nodes": [
            {"name": "hips", "translation": [0.0, 1.0, 0.0], "children": [1]},
            {"name": "spine", "translation": [0.0, 0.5, 0.0], "children": [2]},
            {"name": "chest", "translation": [0.0, 0.5, 0.0]},
        ],
        "animations": [{
            "name": "canonical_walk",
            "samplers": [
                {"input": 0, "output": 1, "interpolation": "LINEAR"},
                {"input": 0, "output": 2, "interpolation": "LINEAR"},
                {"input": 0, "output": 3, "interpolation": "LINEAR"},
            ],
            "channels": [
                {"sampler": 0, "target": {"node": 0, "path": "rotation"}},
                {"sampler": 1, "target": {"node": 2, "path": "rotation"}},
                {"sampler": 2, "target": {"node": 0, "path": "translation"}},
            ],
        }],
        "buffers": [{"byteLength": len(binary)}],
        "bufferViews": [
            {"buffer": 0, "byteOffset": times_offset, "byteLength": times_length},
            {"buffer": 0, "byteOffset": hips_rotation_offset, "byteLength": hips_rotation_length},
            {"buffer": 0, "byteOffset": chest_rotation_offset, "byteLength": chest_rotation_length},
            {"buffer": 0, "byteOffset": hips_translation_offset, "byteLength": hips_translation_length},
        ],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": 2, "type": "SCALAR", "max": [1.0], "min": [0.0]},
            {"bufferView": 1, "componentType": 5126, "count": 2, "type": "VEC4"},
            {"bufferView": 2, "componentType": 5126, "count": 2, "type": "VEC4"},
            {"bufferView": 3, "componentType": 5126, "count": 2, "type": "VEC3"},
        ],
    }
    return _glb(document, binary)


def _expressive_face_bytes() -> bytes:
    """Expression weights, and the cases the reader has to tell apart.

    VRMA animates an expression weight as the X component of its node's
    translation, so this fixture is mostly about what surrounds that rule:

    * `happy` (preset) keys on the body's beats, 0 -> 1.
    * `myWink` (custom) keys on 0.5 and 1.0 only, so its key times widen the
      union the whole clip is sampled at, and its last value is 1.5 -- out of
      the specification's [0, 1] and carried verbatim rather than clamped.
    * `relaxed` (preset) has no channel and a node that states
      `translation = [0.3, 0, 0]`. glTF leaves an un-animated node at its own
      TRS, so that is a constant weight the clip really did give.
    * `surprised` (preset) has no channel and a node that states no transform at
      all: a weight that was never reported, which is not a weight of zero.
    * `angry` points at a node that does not exist and is dropped with a
      warning.

    It also carries the look-at case that belongs beside those: a block that
    declares a node which states no transform and is driven by no channel. That
    is a gaze the file never gave, and it must not be authored as a gaze at the
    origin -- the same distinction `surprised` makes for a weight.
    """
    buffer = _Buffer()
    append_floats = buffer.append_floats

    body_times_offset, body_times_length = append_floats([0.0, 1.0])
    wink_times_offset, wink_times_length = append_floats([0.5, 1.0])
    hips_rotation_offset, hips_rotation_length = append_floats([
        0.0, 0.0, 0.0, 1.0,
        0.0, 0.70710677, 0.0, 0.70710677,
    ])
    happy_offset, happy_length = append_floats([
        0.0, 0.0, 0.0,
        1.0, 0.0, 0.0,
    ])
    wink_offset, wink_length = append_floats([
        0.25, 0.0, 0.0,
        1.5, 0.0, 0.0,
    ])
    binary = buffer.bytes()

    document = {
        "asset": {"version": "2.0", "generator": "usd-vrm-plugins fixture generator"},
        "extensionsUsed": ["VRMC_vrm_animation"],
        "extensions": {
            "VRMC_vrm_animation": {
                "specVersion": "1.0",
                "humanoid": {
                    "humanBones": {
                        "hips": {"node": 0},
                        "spine": {"node": 1},
                        "chest": {"node": 2},
                    }
                },
                "lookAt": {
                    "node": 7,
                    "offsetFromHeadBone": [0.0, 0.06, 0.0],
                },
                "expressions": {
                    "preset": {
                        "happy": {"node": 3},
                        "surprised": {"node": 5},
                        "relaxed": {"node": 6},
                        "angry": {"node": 99},
                    },
                    "custom": {
                        "myWink": {"node": 4},
                    },
                },
            }
        },
        "nodes": [
            {"name": "hips", "translation": [0.0, 1.0, 0.0], "children": [1]},
            {"name": "spine", "translation": [0.0, 0.5, 0.0], "children": [2]},
            {"name": "chest", "translation": [0.0, 0.5, 0.0]},
            {"name": "expression_happy"},
            {"name": "expression_myWink"},
            {"name": "expression_surprised"},
            {"name": "expression_relaxed", "translation": [0.3, 0.0, 0.0]},
            {"name": "lookAt_target"},
        ],
        "animations": [{
            "name": "expressive_face",
            "samplers": [
                {"input": 0, "output": 2, "interpolation": "LINEAR"},
                {"input": 0, "output": 3, "interpolation": "LINEAR"},
                {"input": 1, "output": 4, "interpolation": "LINEAR"},
            ],
            "channels": [
                {"sampler": 0, "target": {"node": 0, "path": "rotation"}},
                {"sampler": 1, "target": {"node": 3, "path": "translation"}},
                {"sampler": 2, "target": {"node": 4, "path": "translation"}},
            ],
        }],
        "buffers": [{"byteLength": len(binary)}],
        "bufferViews": [
            {"buffer": 0, "byteOffset": body_times_offset, "byteLength": body_times_length},
            {"buffer": 0, "byteOffset": wink_times_offset, "byteLength": wink_times_length},
            {"buffer": 0, "byteOffset": hips_rotation_offset, "byteLength": hips_rotation_length},
            {"buffer": 0, "byteOffset": happy_offset, "byteLength": happy_length},
            {"buffer": 0, "byteOffset": wink_offset, "byteLength": wink_length},
        ],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": 2, "type": "SCALAR", "max": [1.0], "min": [0.0]},
            {"bufferView": 1, "componentType": 5126, "count": 2, "type": "SCALAR", "max": [1.0], "min": [0.5]},
            {"bufferView": 2, "componentType": 5126, "count": 2, "type": "VEC4"},
            {"bufferView": 3, "componentType": 5126, "count": 2, "type": "VEC3"},
            {"bufferView": 4, "componentType": 5126, "count": 2, "type": "VEC3"},
        ],
    }
    return _glb(document, binary)


def _gazing_head_bytes() -> bytes:
    """A look-at target that moves, under a parent that does not.

    VRMA points look-at at a node and the character watches where that node
    *is*, so the two things the reader has to get right are both about place
    rather than about the channel:

    * the target keys on the body's beats, moving from straight ahead to the
      character's side;
    * its node is parented under `gaze_space`, which states a translation of its
      own and is animated by nothing. A position read in the node's own space
      would be a gaze 1.5 m below the one the file describes, so the composed
      ancestor transform is the assertion.

    `offsetFromHeadBone` is stated here, because it is a measurement of the rig
    the clip was authored on and travels beside the clip rather than on its
    samples.
    """
    buffer = _Buffer()
    append_floats = buffer.append_floats

    times_offset, times_length = append_floats([0.0, 1.0])
    hips_rotation_offset, hips_rotation_length = append_floats([
        0.0, 0.0, 0.0, 1.0,
        0.0, 0.70710677, 0.0, 0.70710677,
    ])
    target_offset, target_length = append_floats([
        0.0, 0.0, -2.0,
        1.0, 0.0, -2.0,
    ])
    binary = buffer.bytes()

    document = {
        "asset": {"version": "2.0", "generator": "usd-vrm-plugins fixture generator"},
        "extensionsUsed": ["VRMC_vrm_animation"],
        "extensions": {
            "VRMC_vrm_animation": {
                "specVersion": "1.0",
                "humanoid": {
                    "humanBones": {
                        "hips": {"node": 0},
                        "spine": {"node": 1},
                        "chest": {"node": 2},
                    }
                },
                "lookAt": {
                    "node": 4,
                    "offsetFromHeadBone": [0.0, 0.06, 0.0],
                },
            }
        },
        "nodes": [
            {"name": "hips", "translation": [0.0, 1.0, 0.0], "children": [1]},
            {"name": "spine", "translation": [0.0, 0.5, 0.0], "children": [2]},
            {"name": "chest", "translation": [0.0, 0.5, 0.0]},
            {"name": "gaze_space", "translation": [0.0, 1.5, 0.0], "children": [4]},
            {"name": "lookAt_target", "translation": [0.0, 0.0, -2.0]},
        ],
        "animations": [{
            "name": "gazing_head",
            "samplers": [
                {"input": 0, "output": 1, "interpolation": "LINEAR"},
                {"input": 0, "output": 2, "interpolation": "LINEAR"},
            ],
            "channels": [
                {"sampler": 0, "target": {"node": 0, "path": "rotation"}},
                {"sampler": 1, "target": {"node": 4, "path": "translation"}},
            ],
        }],
        "buffers": [{"byteLength": len(binary)}],
        "bufferViews": [
            {"buffer": 0, "byteOffset": times_offset, "byteLength": times_length},
            {"buffer": 0, "byteOffset": hips_rotation_offset, "byteLength": hips_rotation_length},
            {"buffer": 0, "byteOffset": target_offset, "byteLength": target_length},
        ],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": 2, "type": "SCALAR", "max": [1.0], "min": [0.0]},
            {"bufferView": 1, "componentType": 5126, "count": 2, "type": "VEC4"},
            {"bufferView": 2, "componentType": 5126, "count": 2, "type": "VEC3"},
        ],
    }
    return _glb(document, binary)


def _gazing_still_bytes() -> bytes:
    """A look-at target the clip states once, entirely through its parent.

    glTF leaves an un-animated node where the file put it, so a look-at node
    with no channel names one target for the whole clip -- a value the file
    really did give, authored as a default rather than as a run of identical
    time samples.

    The target node here states **no transform of its own**: it sits under
    `gaze_space`, and its position is the parent's. That is the case a reader
    that only looked at the node's own TRS would report as "the file gave no
    gaze", for a clip that placed one unambiguously -- so the assertion is that
    a placement stated anywhere in the chain is a placement.

    The block also omits `offsetFromHeadBone`. Taking that as zero is a claim
    about the source rig -- that the gaze starts at the head bone itself -- so
    the reader warns rather than assuming quietly.
    """
    buffer = _Buffer()
    append_floats = buffer.append_floats

    times_offset, times_length = append_floats([0.0, 1.0])
    hips_rotation_offset, hips_rotation_length = append_floats([
        0.0, 0.0, 0.0, 1.0,
        0.0, 0.70710677, 0.0, 0.70710677,
    ])
    binary = buffer.bytes()

    document = {
        "asset": {"version": "2.0", "generator": "usd-vrm-plugins fixture generator"},
        "extensionsUsed": ["VRMC_vrm_animation"],
        "extensions": {
            "VRMC_vrm_animation": {
                "specVersion": "1.0",
                "humanoid": {
                    "humanBones": {
                        "hips": {"node": 0},
                        "spine": {"node": 1},
                        "chest": {"node": 2},
                    }
                },
                "lookAt": {"node": 4},
            }
        },
        "nodes": [
            {"name": "hips", "translation": [0.0, 1.0, 0.0], "children": [1]},
            {"name": "spine", "translation": [0.0, 0.5, 0.0], "children": [2]},
            {"name": "chest", "translation": [0.0, 0.5, 0.0]},
            {"name": "gaze_space", "translation": [0.2, 1.4, -2.0], "children": [4]},
            {"name": "lookAt_target"},
        ],
        "animations": [{
            "name": "gazing_still",
            "samplers": [
                {"input": 0, "output": 1, "interpolation": "LINEAR"},
            ],
            "channels": [
                {"sampler": 0, "target": {"node": 0, "path": "rotation"}},
            ],
        }],
        "buffers": [{"byteLength": len(binary)}],
        "bufferViews": [
            {"buffer": 0, "byteOffset": times_offset, "byteLength": times_length},
            {"buffer": 0, "byteOffset": hips_rotation_offset, "byteLength": hips_rotation_length},
        ],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": 2, "type": "SCALAR", "max": [1.0], "min": [0.0]},
            {"bufferView": 1, "componentType": 5126, "count": 2, "type": "VEC4"},
        ],
    }
    return _glb(document, binary)


def _gazing_refused_bytes() -> bytes:
    """A look-at block whose node the reader cannot use.

    `lookAt.node` here is `chest`, which the humanoid mapping already drives.
    One node cannot be both a bone's transform and a target position in the
    space that bone moves in, so no target is read.

    What the file still said is the point of the fixture: it raised the subject
    and it measured its rig's `offsetFromHeadBone`. Dropping the whole block
    would leave a stage indistinguishable from a clip that never mentioned
    look-at -- a different statement -- so the declaration survives and only the
    target is missing, which is the state a clip declaring an unplaced node is
    already in.
    """
    buffer = _Buffer()
    append_floats = buffer.append_floats

    times_offset, times_length = append_floats([0.0, 1.0])
    hips_rotation_offset, hips_rotation_length = append_floats([
        0.0, 0.0, 0.0, 1.0,
        0.0, 0.70710677, 0.0, 0.70710677,
    ])
    binary = buffer.bytes()

    document = {
        "asset": {"version": "2.0", "generator": "usd-vrm-plugins fixture generator"},
        "extensionsUsed": ["VRMC_vrm_animation"],
        "extensions": {
            "VRMC_vrm_animation": {
                "specVersion": "1.0",
                "humanoid": {
                    "humanBones": {
                        "hips": {"node": 0},
                        "spine": {"node": 1},
                        "chest": {"node": 2},
                    }
                },
                "lookAt": {
                    "node": 2,
                    "offsetFromHeadBone": [0.0, 0.06, 0.0],
                },
            }
        },
        "nodes": [
            {"name": "hips", "translation": [0.0, 1.0, 0.0], "children": [1]},
            {"name": "spine", "translation": [0.0, 0.5, 0.0], "children": [2]},
            {"name": "chest", "translation": [0.0, 0.5, 0.0]},
        ],
        "animations": [{
            "name": "gazing_refused",
            "samplers": [
                {"input": 0, "output": 1, "interpolation": "LINEAR"},
            ],
            "channels": [
                {"sampler": 0, "target": {"node": 0, "path": "rotation"}},
            ],
        }],
        "buffers": [{"byteLength": len(binary)}],
        "bufferViews": [
            {"buffer": 0, "byteOffset": times_offset, "byteLength": times_length},
            {"buffer": 0, "byteOffset": hips_rotation_offset, "byteLength": hips_rotation_length},
        ],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": 2, "type": "SCALAR", "max": [1.0], "min": [0.0]},
            {"bufferView": 1, "componentType": 5126, "count": 2, "type": "VEC4"},
        ],
    }
    return _glb(document, binary)


FIXTURES = {
    "canonical_walk.vrma": _canonical_walk_bytes,
    "expressive_face.vrma": _expressive_face_bytes,
    "gazing_head.vrma": _gazing_head_bytes,
    "gazing_refused.vrma": _gazing_refused_bytes,
    "gazing_still.vrma": _gazing_still_bytes,
}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="fail if a fixture is not current")
    args = parser.parse_args()

    stale = False
    for name, build in FIXTURES.items():
        path = FIXTURE_DIR / name
        expected = build()
        if args.check:
            if not path.exists() or path.read_bytes() != expected:
                print(f"fixture is stale: {path}", file=sys.stderr)
                stale = True
                continue
            print(f"fixture is current: {path}")
            continue
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(expected)
        print(f"wrote {path} ({len(expected)} bytes)")
    if stale:
        print(f"run {pathlib.Path(__file__).name} to regenerate", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
