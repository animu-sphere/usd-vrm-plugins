#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Author the mocopi packet corpus: the recorded datagrams the decoder replays.

Why these are synthesised, and why the reason is *not* the sibling adapter's.
The VMC corpus is synthetic because a recording off a commercial sender carries
that application's avatar; the shapes themselves come from a published protocol,
so writing them down costs nothing. This protocol is not published. Its shapes
come from measuring 8000 datagrams of five real device sessions -- the grammar is
written out on include/vrmAdapterMocopi/MotionPacket.h with the population each
claim was measured on -- and those recordings hold a real person's motion and a
real person's body proportions, so they are evidence that cannot be committed.

That makes the split between what is measured and what is fixture unusually
important here, and it is worth being blunt about:

  * The **grammar** is measured. Chunk framing, field widths and types, the two
    packet kinds, which four floats are the quaternion, metres, +Y up, +Z
    forward, both clocks, and what an `fnum` gap means: all of it closed against
    real bytes, and all of it is on MotionPacket.h with its population.
  * The **content** is invented. The rest pose here is clean round numbers, not
    the measured one -- a real skeleton packet is a body measurement of a real
    person, which is not a thing to commit to a public repository. The proportions
    below are plausible and nobody's.
  * So these fixtures are a **regression test on the decoder, not evidence about
    the protocol.** They cannot be surprised by the wire format, because the wire
    format is what wrote them. The thing that can still surprise it is the
    vendor's `BVH Sender`, whose *encoding* is the vendor's own and whose content
    is a `.bvh` this repository wrote (libs/motionBvh/tests/corpus/generated/).
    That capture belongs in this directory when an operator makes one, and until
    it exists the sentence above is the honest description of this corpus.

The container encoder here is written out in this file rather than called into
the C++ library, deliberately: a corpus generated with the same implementation
the decoder uses would agree with the decoder by construction rather than by the
protocol.

Handedness is deliberately unresolved and no fixture names a left or a right --
a mirrored basis satisfies every measurement taken so far, so the arm bones are
described by their side of the +X axis and by their bone id. MotionPacket.h
argues it; naming a side here would smuggle a guess into a fixture, which is the
one place it would be hardest to notice later.

The output must match the C++ writer byte for byte; `vrmAdapterMocopi_corpus`
enforces that. Run:

    python adapters/liveCapture/mocopi/tools/generate_packets.py

`manifest.json` is maintained from here too. Its measured fields (datagram
counts, payload sizes, durations, the packet kinds and chunk tags present, bone
counts, digests) are re-derived from the captures on every run and compared under
--check, so the manifest cannot quietly stop describing the corpus; its prose
fields (`pins`, `tags`, and the top-level notes) live in the JSON, are
hand-written, and are preserved untouched.

The comparison is over **parsed field values and not over the serialised text**,
which is the sibling adapter's arrangement and is load-bearing rather than
stylistic: `.mocopipackets` carries `eol=lf` in `.gitattributes` and
`manifest.json` does not, so a text comparison passes on a Linux checkout and
fails on a Windows one for a file whose content is identical. That is exactly
what it did, once.
"""

from __future__ import annotations

import argparse
import collections
import hashlib
import json
import math
import pathlib
import struct
import sys
from dataclasses import dataclass, field

MAGIC = "!mocopi-packet-capture"
FORMAT_VERSION = 1
BYTES_PER_LINE = 16
HEX_COLUMN_WIDTH = BYTES_PER_LINE * 3 - 1
PRECISION = 6
MANIFEST_NAME = "manifest.json"

FORMAT_TYPE = b"sony motion format"
FRAME_RATE = 60.0

# The product's joint hierarchy, measured: a single root, topologically sorted,
# one 4-bone limb per side off the root and one per side off the top of the torso
# chain. The *topology* is the device's rig and is a fact about the protocol; the
# offsets below are not the measured ones. See the module docstring.
PARENTS = [-1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 7, 11, 12, 13, 7, 15, 16, 17,
           0, 19, 20, 21, 0, 23, 24, 25]

# Invented proportions in metres, +Y up and +Z forward. Round numbers on purpose:
# a reviewer can tell at a glance that this is nobody's body.
REST_OFFSETS = [
    (0.0, 0.90, 0.0),      # 0  root
    (0.0, 0.06, 0.0),      # 1  torso
    (0.0, 0.06, 0.0),      # 2
    (0.0, 0.06, 0.0),      # 3
    (0.0, 0.06, 0.0),      # 4
    (0.0, 0.06, 0.0),      # 5
    (0.0, 0.06, 0.0),      # 6
    (0.0, 0.10, 0.0),      # 7  top of the torso chain: neck and both shoulders
    (0.0, 0.05, 0.0),      # 8  neck
    (0.0, 0.05, 0.0),      # 9
    (0.0, 0.05, 0.0),      # 10 head
    (0.02, -0.08, 0.08),   # 11 +X-side shoulder
    (0.14, 0.0, 0.0),      # 12 +X-side upper arm
    (0.30, 0.0, 0.0),      # 13
    (0.25, 0.0, 0.0),      # 14
    (-0.02, -0.08, 0.08),  # 15 -X-side shoulder
    (-0.14, 0.0, 0.0),     # 16 -X-side upper arm
    (-0.30, 0.0, 0.0),     # 17
    (-0.25, 0.0, 0.0),     # 18
    (0.09, -0.05, 0.0),    # 19 +X-side hip
    (0.0, -0.40, 0.0),     # 20
    (0.0, -0.42, 0.0),     # 21
    (0.0, -0.10, 0.13),    # 22 +X-side toe, forward on +Z
    (-0.09, -0.05, 0.0),   # 23 -X-side hip
    (0.0, -0.40, 0.0),     # 24
    (0.0, -0.42, 0.0),     # 25
    (0.0, -0.10, 0.13),    # 26
]

IDENTITY = (0.0, 0.0, 0.0, 1.0)
SENDER_ID = bytes([0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88])
RECEIVE_PORT = 12351
NO_TIMECODE = bytes(6)

NAN32 = struct.unpack("<f", struct.pack("<I", 0x7FC00000))[0]
INF32 = struct.unpack("<f", struct.pack("<I", 0x7F800000))[0]


# --------------------------------------------------------------------------
# The container, encoded here rather than borrowed from the decoder
# --------------------------------------------------------------------------

def chunk(tag: str, payload: bytes) -> bytes:
    """`<uint32 little-endian payload length><4-byte ASCII tag><payload>`."""
    assert len(tag) == 4, tag
    return struct.pack("<I", len(payload)) + tag.encode("ascii") + payload


def tran(rotation: tuple, translation: tuple) -> bytes:
    """Seven binary32: four of rotation, scalar last, then three of translation."""
    return chunk("tran", struct.pack("<7f", *rotation, *translation))


def head(version: int = FORMAT_VERSION, format_type: bytes = FORMAT_TYPE) -> bytes:
    return chunk("head", chunk("ftyp", format_type)
                 + chunk("vrsn", bytes([version])))


def sndf(port: int = RECEIVE_PORT, sender_id: bytes = SENDER_ID) -> bytes:
    return chunk("sndf", chunk("ipad", sender_id)
                 + chunk("rcvp", struct.pack("<H", port)))


def bndt(bone: int, parent: int, rotation: tuple, offset: tuple) -> bytes:
    return chunk("bndt", chunk("bnid", struct.pack("<H", bone))
                 + chunk("pbid", struct.pack("<h", parent))
                 + tran(rotation, offset))


def btdt(bone: int, rotation: tuple, translation: tuple) -> bytes:
    return chunk("btdt", chunk("bnid", struct.pack("<H", bone))
                 + tran(rotation, translation))


def skeleton_packet(count: int = len(PARENTS)) -> bytes:
    bones = b"".join(bndt(index, PARENTS[index], IDENTITY, REST_OFFSETS[index])
                     for index in range(count))
    return head() + sndf() + chunk("skdf", chunk("bons", bones))


def frame_packet(number: int, seconds: float, unix_seconds: float,
                 rotations: dict | None = None,
                 root: tuple | None = None,
                 count: int = len(PARENTS),
                 timecode: bytes = NO_TIMECODE) -> bytes:
    """A frame whose bone translations are the rest offsets, except the root's.

    That is the measured shape and not a simplification: across 207,064
    bone-frames of real sessions every non-root translation equalled its rest
    offset bit for bit, and only bone 0 moved.
    """
    rotations = rotations or {}
    records = []
    for index in range(count):
        translation = REST_OFFSETS[index]
        if index == 0:
            translation = root if root is not None else REST_OFFSETS[0]
        records.append(btdt(index, rotations.get(index, IDENTITY), translation))
    payload = (chunk("fnum", struct.pack("<I", number))
               + chunk("time", struct.pack("<f", seconds))
               + chunk("uttm", struct.pack("<d", unix_seconds))
               + chunk("tmcd", timecode)
               + chunk("btrs", b"".join(records)))
    return head() + sndf() + chunk("fram", payload)


# A fixed instant so the corpus is byte-stable: 2026-08-12T00:00:00Z.
EPOCH = 1786492800.0


def rotation_about_z(degrees: float) -> tuple:
    """Scalar-last, and about the third imaginary component.

    Which axis that *is* depends on a handedness this project has not measured,
    so the name says which component carries it and stops there.
    """
    half = math.radians(degrees) / 2.0
    return (0.0, 0.0, math.sin(half), math.cos(half))


# --------------------------------------------------------------------------
# Captures
# --------------------------------------------------------------------------

@dataclass
class Capture:
    sender: str = ""
    device: str = ""
    source_id: str = ""
    listen: str = ""
    peer: str = ""
    datagrams: list = field(default_factory=list)  # (receive_time, bytes)

    def add(self, payload: bytes, at: float) -> None:
        self.datagrams.append((at, payload))


def base_capture(source_id: str) -> Capture:
    # `sender` and `device` are provenance only and nothing in the decode path may
    # branch on either, so a synthetic capture names itself synthetic rather than
    # borrowing a real application's version string. `peer` is deliberately absent:
    # the measured sessions' peer address and `ipad` are both possibly
    # device-identifying and stay out of anything committed.
    return Capture(sender="example.synthetic", device="example.synthetic",
                   source_id=source_id, listen="0.0.0.0:12351")


def neutral_standing() -> Capture:
    capture = base_capture("neutral-standing-01")
    capture.add(skeleton_packet(), 0.0)
    for index in range(5):
        seconds = index / FRAME_RATE
        capture.add(frame_packet(1000 + index, seconds, EPOCH + seconds),
                    round(seconds + 1.0 / FRAME_RATE, PRECISION))
    return capture


def arms_lowered() -> Capture:
    capture = base_capture("arms-lowered-01")
    # The rest pose holds the arms along +/-X. A standing figure has them down,
    # which is what the measured neutral sessions showed: about 85 degrees on the
    # upper arms, opposite signs on the two sides.
    rotations = {12: rotation_about_z(-85.0), 16: rotation_about_z(85.0)}
    capture.add(skeleton_packet(), 0.0)
    for index in range(3):
        seconds = index / FRAME_RATE
        capture.add(
            frame_packet(2000 + index, seconds, EPOCH + seconds,
                         rotations=rotations,
                         root=(0.0, 0.90 - 0.01 * index, 0.02 * index)),
            round(seconds + 1.0 / FRAME_RATE, PRECISION))
    return capture


def frame_loss() -> Capture:
    capture = base_capture("frame-loss-01")
    capture.add(skeleton_packet(), 0.0)
    arrival = 1.0 / FRAME_RATE
    # Three frames, then the measured Wi-Fi loss shape: `fnum` jumps by 3 and
    # `time` jumps by exactly 3/60, because the sender's clock never skipped.
    for number, step in ((3000, 0), (3001, 1), (3002, 2), (3005, 5), (3006, 6)):
        seconds = step / FRAME_RATE
        capture.add(frame_packet(number, seconds, EPOCH + seconds), arrival)
        arrival = round(arrival + 1.0 / FRAME_RATE, PRECISION)
    # A duplicate delivery: the same frame twice, which a socket can do and a
    # decoder must not care about.
    seconds = 6 / FRAME_RATE
    capture.add(frame_packet(3006, seconds, EPOCH + seconds), arrival)
    arrival = round(arrival + 1.0 / FRAME_RATE, PRECISION)
    # A restart: the counter and the stream clock both begin again. Nothing here
    # refuses it -- one packet cannot see it -- and it is the assembler's
    # VRM_MOCOPI_SOURCE_RESTARTED when it arrives.
    capture.add(frame_packet(1, 0.0, EPOCH + 30.0), arrival)
    return capture


def malformed_container() -> Capture:
    capture = base_capture("malformed-container-01")
    # A zero-length datagram is receivable, and is the smallest thing a decoder
    # must refuse without crashing. The container walk accepts it as zero chunks;
    # the packet layer is what refuses it, for carrying neither kind.
    capture.add(b"", 0.0)
    # A header needs eight bytes and there are four.
    capture.add(struct.pack("<I", 0), 0.1)
    # A chunk claiming more payload than the datagram holds.
    capture.add(struct.pack("<I", 4096) + b"fram" + b"\x00" * 16, 0.2)
    # A walk that lands between chunks: a whole `head` followed by three bytes.
    capture.add(head() + b"\x01\x02\x03", 0.3)
    # Not this protocol at all. An OSC datagram's first four bytes read as a
    # little-endian length far past the end, so it dies in the container rather
    # than at a field -- which is the refusal the two magic lines exist to make
    # legible.
    capture.add(b"/VMC/Ext/T\x00\x00,f\x00\x00\x3f\x80\x00\x00", 0.4)
    return capture


def malformed_packets() -> Capture:
    capture = base_capture("malformed-packets-01")
    seconds, unix = 0.0, EPOCH
    body = chunk("fram", chunk("fnum", struct.pack("<I", 7))
                 + chunk("time", struct.pack("<f", seconds))
                 + chunk("uttm", struct.pack("<d", unix))
                 + chunk("tmcd", NO_TIMECODE)
                 + chunk("btrs", btdt(0, IDENTITY, REST_OFFSETS[0])))

    # The magic is some other format's.
    capture.add(head(format_type=b"not a motion format") + sndf() + body, 0.0)
    # A version this decoder has not measured. The one refusal that blames a
    # sender for being newer, and MotionPacket.h argues why that is correct here.
    capture.add(head(version=2) + sndf() + body, 0.1)
    # No `head` at all.
    capture.add(sndf() + body, 0.2)
    # Both payload kinds in one datagram: nothing says which clock the skeleton
    # belongs to.
    capture.add(head() + sndf() + body + chunk("skdf", chunk("bons", b"")), 0.3)
    # Neither payload kind.
    capture.add(head() + sndf(), 0.4)
    # Two `fnum` chunks: a field read exactly once, sent twice.
    capture.add(head() + sndf()
                + chunk("fram", chunk("fnum", struct.pack("<I", 7))
                        + chunk("fnum", struct.pack("<I", 8))
                        + chunk("time", struct.pack("<f", seconds))
                        + chunk("uttm", struct.pack("<d", unix))
                        + chunk("tmcd", NO_TIMECODE)
                        + chunk("btrs", btdt(0, IDENTITY, REST_OFFSETS[0]))),
                0.5)
    # `fnum` two bytes wide instead of four: a field whose declared length
    # disagrees with the type, refused rather than read as a prefix.
    capture.add(head() + sndf()
                + chunk("fram", chunk("fnum", struct.pack("<H", 7))
                        + chunk("time", struct.pack("<f", seconds))
                        + chunk("uttm", struct.pack("<d", unix))
                        + chunk("tmcd", NO_TIMECODE)
                        + chunk("btrs", btdt(0, IDENTITY, REST_OFFSETS[0]))),
                0.6)
    # A six-float `tran`.
    capture.add(head() + sndf()
                + chunk("fram", chunk("fnum", struct.pack("<I", 7))
                        + chunk("time", struct.pack("<f", seconds))
                        + chunk("uttm", struct.pack("<d", unix))
                        + chunk("tmcd", NO_TIMECODE)
                        + chunk("btrs", chunk("btdt",
                                              chunk("bnid", struct.pack("<H", 0))
                                              + chunk("tran", struct.pack("<6f", *IDENTITY, 0.0, 0.0))))),
                0.7)
    # An unusable stream clock, three ways. A frame nothing can be ordered by is
    # refused whole, unlike a single bad bone.
    for index, value in enumerate((NAN32, -1.0)):
        capture.add(head() + sndf()
                    + chunk("fram", chunk("fnum", struct.pack("<I", 7))
                            + chunk("time", struct.pack("<f", value))
                            + chunk("uttm", struct.pack("<d", unix))
                            + chunk("tmcd", NO_TIMECODE)
                            + chunk("btrs", btdt(0, IDENTITY, REST_OFFSETS[0]))),
                    round(0.8 + 0.1 * index, PRECISION))
    capture.add(head() + sndf()
                + chunk("fram", chunk("fnum", struct.pack("<I", 7))
                        + chunk("time", struct.pack("<f", seconds))
                        + chunk("uttm", struct.pack("<d", float("inf")))
                        + chunk("tmcd", NO_TIMECODE)
                        + chunk("btrs", btdt(0, IDENTITY, REST_OFFSETS[0]))),
                1.0)
    # A `tmcd` of the wrong width. Never read, still checked: within a version the
    # field widths are the format, and `vrsn` is what a producer bumps.
    capture.add(head() + sndf()
                + chunk("fram", chunk("fnum", struct.pack("<I", 7))
                        + chunk("time", struct.pack("<f", seconds))
                        + chunk("uttm", struct.pack("<d", unix))
                        + chunk("tmcd", bytes(4))
                        + chunk("btrs", btdt(0, IDENTITY, REST_OFFSETS[0]))),
                1.1)
    # A skeleton packet with one unusable rest transform, which is refused WHOLE
    # rather than short a bone -- the deliberate asymmetry with a frame. Every
    # other bone's `pbid` names an id in this table, so a table with a hole in it
    # describes a hierarchy whose parents do not all exist.
    bones = b"".join(
        bndt(index, PARENTS[index],
             (NAN32, 0.0, 0.0, 1.0) if index == 7 else IDENTITY,
             REST_OFFSETS[index])
        for index in range(len(PARENTS)))
    capture.add(head() + sndf() + chunk("skdf", chunk("bons", bones)), 1.2)
    return capture


def refused_bones() -> Capture:
    capture = base_capture("refused-bones-01")
    seconds = 0.0
    records = []
    for index in range(len(PARENTS)):
        translation = REST_OFFSETS[index]
        rotation = IDENTITY
        if index == 5:
            rotation = (NAN32, 0.0, 0.0, 1.0)      # not finite
        elif index == 9:
            rotation = (0.0, 0.0, 0.0, 0.0)        # names no orientation
        elif index == 20:
            translation = (0.0, INF32, 0.0)        # not finite
        records.append(btdt(index, rotation, translation))
    payload = (chunk("fnum", struct.pack("<I", 4000))
               + chunk("time", struct.pack("<f", seconds))
               + chunk("uttm", struct.pack("<d", EPOCH))
               + chunk("tmcd", NO_TIMECODE)
               + chunk("btrs", b"".join(records)))
    capture.add(head() + sndf() + chunk("fram", payload), 0.0)
    # The same frame with every bone usable, so a test can compare the two and
    # see that only the three refusals differ.
    capture.add(frame_packet(4001, 1.0 / FRAME_RATE,
                             EPOCH + 1.0 / FRAME_RATE),
                round(1.0 / FRAME_RATE, PRECISION))
    return capture


def extended_form() -> Capture:
    capture = base_capture("extended-form-01")
    seconds = 0.0
    # Unknown chunks at three levels: beside the payload, inside `fram`, and
    # inside every bone record. The last is the one that matters -- a decoder that
    # reported it once per bone would bury the fact under 27 copies of it.
    records = [chunk("btdt", chunk("bnid", struct.pack("<H", index))
                     + tran(IDENTITY, REST_OFFSETS[index])
                     + chunk("conf", struct.pack("<f", 0.5)))
               for index in range(len(PARENTS))]
    payload = (chunk("fnum", struct.pack("<I", 5000))
               + chunk("time", struct.pack("<f", seconds))
               + chunk("uttm", struct.pack("<d", EPOCH))
               + chunk("tmcd", NO_TIMECODE)
               + chunk("btrs", b"".join(records))
               + chunk("xxxx", b"\xde\xad\xbe\xef"))
    capture.add(head() + sndf() + chunk("fram", payload)
                + chunk("yyyy", b"\x00"), 0.0)
    # A rig this project has not seen: eleven bones rather than twenty-seven.
    # Not malformed -- a longer or shorter joint list is a different rig, and a
    # decoder that required the measured count would refuse rigs rather than
    # packets.
    capture.add(skeleton_packet(count=11), 0.1)
    capture.add(frame_packet(5001, 1.0 / FRAME_RATE, EPOCH + 1.0 / FRAME_RATE,
                             count=11),
                round(0.1 + 1.0 / FRAME_RATE, PRECISION))
    # A non-zero `tmcd`. Never observed on the wire, so the decoder carries the
    # six bytes and claims nothing about them; this fixture is what keeps that
    # true rather than accidentally true.
    capture.add(frame_packet(5002, 2.0 / FRAME_RATE, EPOCH + 2.0 / FRAME_RATE,
                             timecode=bytes([1, 2, 3, 4, 5, 6])),
                round(0.1 + 2.0 / FRAME_RATE, PRECISION))
    return capture


CAPTURES = {
    "neutral-standing-60hz.mocopipackets": neutral_standing,
    "arms-lowered-60hz.mocopipackets": arms_lowered,
    "frame-loss-60hz.mocopipackets": frame_loss,
    "malformed-container.mocopipackets": malformed_container,
    "malformed-packets.mocopipackets": malformed_packets,
    "refused-bones-60hz.mocopipackets": refused_bones,
    "extended-form.mocopipackets": extended_form,
}


# --------------------------------------------------------------------------
# Rendering: must equal the C++ writer byte for byte
# --------------------------------------------------------------------------

def gutter(payload: bytes) -> str:
    return "".join(chr(byte) if 0x20 <= byte <= 0x7e else "." for byte in payload)


def render(capture: Capture) -> str:
    lines = [f"{MAGIC} {FORMAT_VERSION}"]
    for key, value in (("sender", capture.sender), ("device", capture.device),
                       ("sourceId", capture.source_id),
                       ("listen", capture.listen), ("peer", capture.peer)):
        if value:
            lines.append(f"{key} {value}")
    for at, payload in capture.datagrams:
        lines.append("")
        lines.append(f"d {at:.{PRECISION}f} {len(payload)}")
        for offset in range(0, len(payload), BYTES_PER_LINE):
            window = payload[offset:offset + BYTES_PER_LINE]
            hexes = " ".join(f"{byte:02x}" for byte in window)
            lines.append(f"  {hexes.ljust(HEX_COLUMN_WIDTH)}  |{gutter(window)}|")
    return "\n".join(lines) + "\n"


# --------------------------------------------------------------------------
# The manifest's measured half
# --------------------------------------------------------------------------

def walk_chunks(payload: bytes):
    """One level of the container, or None when it does not close exactly.

    None and [] are different answers and the manifest reports them differently:
    a zero-length datagram closes on zero chunks, which is a datagram the decoder
    refuses one layer up rather than one the container could not read.
    """
    chunks, offset = [], 0
    while offset + 8 <= len(payload):
        (length,) = struct.unpack_from("<I", payload, offset)
        if length > len(payload) - offset - 8:
            return None
        chunks.append((payload[offset + 4:offset + 8].decode("latin-1"),
                       payload[offset + 8:offset + 8 + length]))
        offset += 8 + length
    return chunks if offset == len(payload) else None


# The chunks whose payload is itself a sequence of chunks. Descending into
# anything else is how a leaf's bytes become a fabricated tag, and it is not a
# hypothetical: the binary64 for +inf is 00 00 00 00 00 00 f0 7f, whose first four
# bytes read as length 0 and whose next four then look exactly like a tag that
# closes the payload precisely -- so an unguarded walk of `uttm` invented the tag
# "\x00\x00\xf0\x7f" and wrote it, plus a raw control byte, into the manifest this
# corpus calls its machine-readable source of truth. A leaf is only recognisable
# from the grammar, so the grammar is written down here rather than guessed at per
# payload.
CONTAINER_TAGS = frozenset({
    "head", "sndf", "fram", "skdf", "btrs", "bons", "btdt", "bndt",
})


def collect_tags(chunks, tags: set, bone_counts: set) -> None:
    """Record the tags of one level, descending only into known containers."""
    for tag, body in chunks:
        tags.add(tag)
        if tag not in CONTAINER_TAGS:
            continue
        inner = walk_chunks(body)
        if inner is None:
            continue
        if tag in ("btrs", "bons"):
            bone_counts.add(len(inner))
        collect_tags(inner, tags, bone_counts)


def describe(capture: Capture) -> dict:
    kinds: collections.Counter = collections.Counter()
    tags: set = set()
    bone_counts: set = set()
    for _, payload in capture.datagrams:
        top = walk_chunks(payload)
        if top is None:
            kinds["unwalkable"] += 1
            continue
        collect_tags(top, tags, bone_counts)
        # Reported by what the datagram carries rather than by the first tag
        # found: "both" is a refusal of its own and must not read as a frame.
        names = [tag for tag, _ in top]
        has = ("fram" in names, "skdf" in names)
        kinds[{(True, False): "fram", (False, True): "skdf",
               (True, True): "both", (False, False): "neither"}[has]] += 1

    times = [at for at, _ in capture.datagrams]
    return {
        "sender": capture.sender,
        "device": capture.device,
        "sourceId": capture.source_id,
        "datagrams": len(capture.datagrams),
        "payloadBytes": sum(len(p) for _, p in capture.datagrams),
        "durationSeconds": round(max(times) - min(times), PRECISION) if times else 0.0,
        "packetKinds": dict(sorted(kinds.items())),
        "chunkTags": sorted(tags),
        "boneCounts": sorted(bone_counts),
    }


def field_matches(claimed, measured) -> bool:
    """Compare one manifest field against its measured value.

    Floats go through a tolerance because a duration survives a JSON round trip
    as the nearest binary64 and not as the decimal that was written.
    """
    if isinstance(measured, float):
        try:
            return abs(float(claimed) - float(measured)) <= 1e-6
        except (TypeError, ValueError):
            return False
    return claimed == measured


def sync_manifest(directory: pathlib.Path, rendered: dict, check: bool) -> list:
    """Compare (or rewrite) the manifest's measured fields. Returns problems.

    The manifest is the hand-maintained artifact and this only owns the fields it
    can derive: the prose (`pins`, `tags`, and every top-level note) lives in the
    JSON and is never touched here. Comparison is over parsed values rather than
    over the serialised text, so a CRLF checkout of a file with no `eol=lf`
    attribute cannot fail a check about the corpus.
    """
    path = directory / MANIFEST_NAME
    if not path.exists():
        return [f"{MANIFEST_NAME} is missing"]

    manifest = json.loads(path.read_text(encoding="utf-8"))
    problems: list = []

    if manifest.get("format", {}).get("version") != FORMAT_VERSION:
        problems.append(
            f"{MANIFEST_NAME}: format.version is "
            f"{manifest.get('format', {}).get('version')!r}, expected "
            f"{FORMAT_VERSION}")

    listed = [entry.get("file") for entry in manifest.get("captures", [])]
    if sorted(listed) != sorted(rendered):
        problems.append(
            f"{MANIFEST_NAME} describes {sorted(listed)}, the corpus is "
            f"{sorted(rendered)}")
        return problems

    for entry in manifest["captures"]:
        name = entry["file"]
        derived = {
            "id": pathlib.Path(name).stem,
            **describe(CAPTURES[name]()),
            "sha256": hashlib.sha256(rendered[name].encode("utf-8")).hexdigest(),
        }
        for key, value in derived.items():
            if not field_matches(entry.get(key), value):
                problems.append(
                    f"{name}: {key} is {entry.get(key)!r}, measured {value!r}")
        if not check:
            entry.update(derived)

    if check:
        return problems

    # Writing mode fills the derived fields in, so a new entry needs only its
    # `file` and the prose no tool can infer. Report what changed either way:
    # silently rewriting a provenance record is the wrong kind of convenient.
    path.write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n",
                    encoding="utf-8", newline="\n")
    for problem in problems:
        print(f"updated {problem}")
    return []


def main() -> int:
    default_output = (pathlib.Path(__file__).resolve().parents[1] / "tests"
                      / "corpus")
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=pathlib.Path, default=default_output,
                        help="corpus directory to write into")
    parser.add_argument("--check", action="store_true",
                        help="fail if any committed capture differs from the "
                             "freshly generated one instead of rewriting it")
    args = parser.parse_args()

    if args.check:
        # Not `mkdir(exist_ok=True)`: a check against a path that does not exist
        # must say so, rather than materialise an empty directory and then report
        # all seven captures as drifted -- which diagnoses the typo as a corpus
        # problem and leaves a stray directory in the source tree.
        if not args.output.is_dir():
            print(f"{args.output} is not a directory", file=sys.stderr)
            return 1
    else:
        args.output.mkdir(parents=True, exist_ok=True)
    drifted: list = []
    rendered: dict = {}
    for name, builder in sorted(CAPTURES.items()):
        text = render(builder())
        rendered[name] = text
        path = args.output / name
        existing = (path.read_text(encoding="utf-8", newline="")
                    if path.exists() else None)
        if args.check:
            if existing != text:
                drifted.append(name)
            continue
        # Binary-mode newline control: a capture written on Windows must equal
        # one written on Linux, or the three OS cells cannot share the fixture.
        with open(path, "w", encoding="utf-8", newline="\n") as handle:
            handle.write(text)
        print(f"wrote {name} ({text.count(chr(10))} lines)")

    if drifted:
        print("regenerated captures differ from the committed corpus: "
              + ", ".join(drifted), file=sys.stderr)
        print("run: python adapters/liveCapture/mocopi/tools/"
              "generate_packets.py", file=sys.stderr)
        return 1

    problems = sync_manifest(args.output, rendered, args.check)
    if problems:
        for problem in problems:
            print(f"manifest drift: {problem}", file=sys.stderr)
        print("run: python adapters/liveCapture/mocopi/tools/"
              "generate_packets.py", file=sys.stderr)
        return 1
    if not args.check:
        print(f"{MANIFEST_NAME} is in sync with {len(rendered)} capture(s)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
