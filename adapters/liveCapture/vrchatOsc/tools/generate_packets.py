#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Author the VRChat OSC packet corpus: the datagrams the tracker decoder replays.

Why these are synthesised rather than recorded: a VRChat OSC stream is a real
person's tracker positions in a real room, which is why the session of VRC-1
left a manifest and no bytes
(tests/corpus/recorded/manifests/2026-08-30-mocopi-vrchat-osc.json). A fixture
nobody may redistribute is a fixture CI cannot run, and the whole point of
recording packets is that the adapter is verifiable with no hardware, no device
and no VRChat client. Every byte here is assembled by this file from the OSC
grammar, so the corpus is Apache-2.0 like the rest of the repository,
byte-stable across machines, and reviewable in a diff.

**What it is written from is a measurement, not a specification.** VRChat's OSC
tracking surface is published, and writing these fixtures from that document is
the failure the plan's capture-before-decode order exists to prevent: a
specification says what a *receiver* must accept, and a corpus is evidence about
what a *sender* sends. So the shapes below come from
docs/reports/motion/02-2026-08-30-vrchat-osc-address-inventory.md -- eight
addresses, every one `,fff`, one message per datagram, no bundles, rotation
before position, the head leading a fixed eight-datagram cycle, ~58 Hz emitted
with about a third of the frames lost whole, and a single-address loss that
falls almost entirely on one address.

**Exactly one capture is the session's own shape**, and the manifest says so per
capture rather than leaving it to be inferred. `observed` takes three values:

* **`session`** -- the recorded session carried this shape. Only
  `three-trackers-58hz` does.
* **`derived`** -- every address, type tag and ordering in it is one the session
  carried, recombined into an arrangement it did not: one tracker alone, no
  head, a single channel sustained for a session, a permanent dropout. Five are.
* **`unobserved`** -- it carries something the session never carried at all.
  Six are, each for a stated reason: **trackers 4-8**, because a decoder that
  accepted only what one six-sensor configuration emitted would refuse a legal
  address the first time anyone connects a fuller setup; **an OSC bundle**,
  because "no sender bundles" must not quietly become "this cannot read one";
  **VRChat's wider surface** and **a reordered or duplicated frame**, neither of
  which that sender produces; and **malformed datagrams**, because port 9000 is
  well known and anything on the network may send to it.

Keeping that in a field rather than in a comment is what stops a later reader
mistaking any of the eleven for a recording.

The output must match the C++ writer byte for byte; `vrmAdapterVrchatOsc_corpus`
enforces that, and `vrmAdapterVrchatOsc_packetGen` enforces that this file still
reproduces the committed bytes. Run:

    python adapters/liveCapture/vrchatOsc/tools/generate_packets.py

`manifest.json` is maintained from here too. Its measured fields (datagram
counts, payload sizes, durations, the addresses present, digests) are re-derived
from the captures on every run and compared under --check, so the manifest
cannot quietly stop describing the corpus; its prose fields (`pins`, `tags`, the
top-level notes) are hand-written and preserved untouched.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import re
import struct
import sys

MANIFEST_NAME = "manifest.json"
BYTES_PER_LINE = 16

# ---------------------------------------------------------------------------
# OSC 1.0 encoding
#
# Deliberately written out here rather than taken from `libs/osc`: the corpus is
# the decoder's *input*, and generating it with the implementation under test
# would make every fixture agree with the decoder by construction rather than by
# the protocol. The sibling corpora are generated the same way for the same
# reason, and so is the address inventory's test suite one layer down.
# ---------------------------------------------------------------------------


def osc_string(text: str) -> bytes:
    """A NUL-terminated string padded to a four-byte boundary.

    Always at least one NUL: a string whose length is already a multiple of four
    gets four padding bytes, not zero.
    """
    raw = text.encode("ascii") + b"\x00"
    padding = (-len(raw)) % 4
    return raw + b"\x00" * padding


class Float64(float):
    """An argument written with the `d` type tag instead of `f`.

    No VRChat sender emits one. It exists for a single fixture: OSC puts an `f`
    and a `d` in the same decoded field, so a decoder that read values without
    checking the type tag string would accept a double-valued tracker position
    and this corpus would pin nothing at all about the wire format.
    """


def osc_message(address: str, *arguments) -> bytes:
    """One OSC message. Types are inferred: int, float, str, Float64."""
    tags = ","
    body = b""
    for argument in arguments:
        if isinstance(argument, bool):
            raise TypeError("OSC 1.0 has no boolean argument type here")
        # Before the `float` branch, which would otherwise claim it: Float64 is
        # a float subclass so that call sites read as plain numbers.
        if isinstance(argument, Float64):
            tags += "d"
            body += struct.pack(">d", argument)
        elif isinstance(argument, int):
            tags += "i"
            body += struct.pack(">i", argument)
        elif isinstance(argument, float):
            tags += "f"
            body += struct.pack(">f", argument)
        elif isinstance(argument, str):
            tags += "s"
            body += osc_string(argument)
        else:
            raise TypeError(f"unsupported OSC argument {argument!r}")
    return osc_string(address) + osc_string(tags) + body


def osc_bundle(*elements: bytes) -> bytes:
    """An OSC bundle with the immediate time tag.

    The time tag is `1` -- NTP "immediately" -- because that is what a real-time
    sender emits, and because a bundle scheduled for a future instant is a shape
    this adapter has no policy for and would be inventing one by recording.
    """
    packet = b"#bundle\x00" + struct.pack(">Q", 1)
    for element in elements:
        packet += struct.pack(">i", len(element)) + element
    return packet


# ---------------------------------------------------------------------------
# VRChat OSC tracker messages
# ---------------------------------------------------------------------------

TRACKER_PREFIX = "/tracking/trackers"


def tracker(identity: str, channel: str, values) -> bytes:
    """`/tracking/trackers/<identity>/<position|rotation>` with three floats.

    `identity` is a string on purpose, and it is the shape of this whole corpus
    in one parameter: `head` and `1` occupy the same path position, so a
    generator that took an integer here could not author the address that
    matters most (report 02 §6).
    """
    return osc_message(f"{TRACKER_PREFIX}/{identity}/{channel}",
                       *[float(value) for value in values])


# The send order measured off the wire: the head leads, rotation precedes
# position for the same tracker, and the eight datagrams of a frame arrive
# inside a median 53 microseconds. Neither the order nor the burst is something
# any specification promised, and a frame assembler that assumed the opposite
# order would have looked correct against a fixture written from the document.
CHANNELS = ("rotation", "position")


def cycle(identities) -> list[tuple[str, str]]:
    """The (identity, channel) send order for one frame."""
    return [(identity, channel)
            for identity in identities
            for channel in CHANNELS]


# Per-tracker base values. **These name no body region and no unit**, and that
# is the point rather than a shortcoming: which tracker is on which part of a
# body is an assignment policy outside this adapter (§5.1), and what space the
# numbers are in is VRC-3's to establish against a recorded rest pose. What the
# table is for is that the trackers are *distinguishable*: a decoder that
# crossed two identities, or that returned its defaults, fails on every capture
# here rather than on a special one.
#
# The magnitudes are person-shaped so that a reviewer reading a hex dump can
# tell a position from a rotation at a glance, and nothing downstream may read
# more into them than that.
TRACKER_BASE = {
    "head": ((0.020, 1.620, -0.050), (2.5, -1.0, 0.0)),
    "1": ((0.010, 0.980, -0.020), (5.0, 3.0, -1.0)),
    "2": ((-0.110, 0.460, 0.030), (-4.0, 0.0, 2.0)),
    "3": ((0.100, 0.450, 0.020), (-3.0, 1.0, -2.0)),
    "4": ((-0.180, 0.980, 0.040), (6.0, -2.0, 1.0)),
    "5": ((0.180, 0.980, 0.040), (-6.0, 2.0, -1.0)),
    "6": ((-0.150, 1.350, 0.010), (1.0, 4.0, 0.0)),
    "7": ((0.150, 1.350, 0.010), (-1.0, -4.0, 0.0)),
    "8": ((0.000, 1.100, -0.080), (0.0, 8.0, 0.0)),
}

# The three numbered trackers plus the named head: exactly what the recorded
# session carried.
MEASURED_TRACKERS = ("head", "1", "2", "3")
FULL_SURFACE = ("head", "1", "2", "3", "4", "5", "6", "7", "8")

# ~58 Hz emitted, which is what the sender aims at; the delivered rate is lower
# because frames go missing, and that is modelled by omitting them rather than
# by stretching this interval.
FRAME_SECONDS = 1.0 / 58.0
# The eight datagrams of one frame arrive inside a median 53 microseconds.
BURST_SECONDS = 0.000_053 / 7.0


def frame_values(identity: str, channel: str, frame: int):
    """One tracker's values in one frame: its base, drifting slowly.

    The drift exists so that no two frames of a capture are byte-identical. A
    corpus of repeated frames would let a decoder that returned the first frame
    forever pass every count this repository checks.
    """
    position, rotation = TRACKER_BASE[identity]
    step = frame * 0.001
    if channel == "position":
        return (position[0] + step, position[1] - step, position[2] + step)
    return (rotation[0] + frame * 0.25, rotation[1], rotation[2] - frame * 0.5)


class Capture:
    """A recorded session: provenance plus (receive time, payload) records."""

    def __init__(self, source_id: str, peer: str,
                 sender: str = "example.synthetic",
                 device: str = "example.synthetic",
                 listen: str = "0.0.0.0:9000") -> None:
        self.sender = sender
        self.device = device
        self.source_id = source_id
        self.listen = listen
        self.peer = peer
        self.datagrams: list[tuple[float, bytes]] = []
        # Who sent each record, which the header cannot say: it names one
        # peer for a whole file, and this wire marks a restart with a new
        # ephemeral source port and with nothing else (report 02 §4).
        # Empty is the ordinary case and renders no `p` line at all, so
        # every fixture written before the line existed is unchanged.
        self.record_peer = ""
        self.peers: list[str] = []

    def add(self, receive_time: float, payload: bytes) -> None:
        self.datagrams.append((round(receive_time, 6), payload))
        self.peers.append(self.record_peer)

    def frame(self, receive_time: float, identities, frame: int,
              channels=CHANNELS, skip=(), shift=(0.0, 0.0, 0.0),
              values_for=None) -> None:
        """One frame as this sender sends one: a burst of single-message datagrams.

        `skip` drops individual (identity, channel) pairs, which is the shape of
        the single-address loss measured on the real wire -- 96 % of it on one
        address -- and the concrete case behind TRACKER_PARTIAL.

        `values_for` replaces the drifting table for the one capture that needs
        a body doing something rather than trackers that are merely
        distinguishable. It is a parameter rather than a second `frame` method
        because everything else about how this sender packages a frame -- the
        cycle order, the burst spacing, one message per datagram -- is the same
        measurement in both cases, and a copy of it would be free to drift.
        """
        step = 0
        for identity, channel in cycle(identities):
            if channel not in channels or (identity, channel) in skip:
                continue
            values = (values_for or frame_values)(identity, channel, frame)
            if channel == "position" and shift != (0.0, 0.0, 0.0):
                values = tuple(value + offset
                               for value, offset in zip(values, shift))
            self.add(receive_time + step * BURST_SECONDS,
                     tracker(identity, channel, values))
            step += 1


# ---------------------------------------------------------------------------
# The captures
# ---------------------------------------------------------------------------


def capture_three_trackers() -> Capture:
    """The measured shape: three numbered trackers, a named head, and loss.

    Eight frames at the sender's nominal rate, of which one never arrives at all
    and one arrives missing a single address. Both losses are what the recorded
    session measured rather than adversarial input -- about a third of frames go
    missing whole, and the residual single-address loss falls almost entirely on
    `/tracking/trackers/1/rotation` (report 02 §3).

    Nothing in this capture is refused by any layer. It is the fixture that says
    what *ordinary* traffic on this wire looks like, and the reason every other
    capture here can be read as a departure from something.
    """
    capture = Capture("three-trackers-01", "127.0.0.1:51662")
    for index in range(8):
        # The dropped frame leaves a two-period gap, which is exactly what the
        # bimodal interval distribution of the real session looks like.
        if index == 3:
            continue
        skip = {("1", "rotation")} if index == 6 else set()
        capture.frame(index * FRAME_SECONDS, MEASURED_TRACKERS, index,
                      skip=skip)
    return capture


def capture_one_tracker() -> Capture:
    """One tracker, and the only capture whose *values* are load-bearing.

    Every number below is exactly representable in binary32, so the decoded
    floats can be compared for equality rather than within a tolerance -- which
    is what lets this fixture make the claim no count can: **nothing is
    converted on the way through**. A decoder that reflected an axis, reordered
    components, converted degrees to radians or scaled centimetres to metres
    would still produce the right counts everywhere else in this corpus and
    fails here.

    The signs are asymmetric in all three components for the same reason: a
    reflection through any single axis is visible, where a symmetric fixture
    would hide two of the three.
    """
    capture = Capture("one-tracker-01", "127.0.0.1:51663")
    values = [
        ((0.25, -0.5, 1.25), (-90.0, 0.5, 45.25)),
        ((0.5, -0.25, 1.5), (-89.5, 0.25, 44.75)),
        ((0.75, -0.125, 1.75), (-89.0, 0.125, 44.5)),
    ]
    for index, (position, rotation) in enumerate(values):
        receive = index * FRAME_SECONDS
        capture.add(receive, tracker("1", "rotation", rotation))
        capture.add(receive + BURST_SECONDS, tracker("1", "position", position))
    return capture


def capture_eight_trackers() -> Capture:
    """The whole numbered surface, which no sender here has been seen to emit.

    Invented traffic, declared as such in the manifest. It is in the corpus
    because the alternative -- accepting only the identities one six-sensor
    configuration happened to send -- would make this decoder refuse a legal
    address as a protocol violation the first time somebody connects a fuller
    setup (report 02 §6.5).
    """
    capture = Capture("eight-trackers-01", "127.0.0.1:51664")
    for index in range(2):
        capture.frame(index * FRAME_SECONDS, FULL_SURFACE, index)
    return capture


def capture_head_absent() -> Capture:
    """Numbered trackers with no head reference at all.

    The head is a *name* in this protocol and it is also optional, and those are
    two different facts. This capture pins the second: a session with no
    `/tracking/trackers/head/*` in it is well-formed, and a decoder that treated
    the head as a required frame member would fail here rather than in a live
    session.
    """
    capture = Capture("head-absent-01", "127.0.0.1:51665")
    for index in range(3):
        capture.frame(index * FRAME_SECONDS, ("1", "2", "3"), index)
    return capture


def capture_position_only() -> Capture:
    """Positions and no rotations, for a whole session.

    Half of a tracker, sustained. This is not a frame-assembly fixture -- what a
    window does with a tracker that reports one channel is VRC-4's policy -- it
    is the decode claim that a position message is complete on its own, because
    on this wire a tracker's two halves are two datagrams and either can be the
    only one there is.
    """
    capture = Capture("position-only-01", "127.0.0.1:51666")
    for index in range(3):
        capture.frame(index * FRAME_SECONDS, MEASURED_TRACKERS, index,
                      channels=("position",))
    return capture


def capture_rotation_only() -> Capture:
    """The mirror of the capture above, and not a duplicate of it.

    The two channels differ in more than their name: a rotation is three floats
    where a quaternion would be four, so this is the capture that pins the
    Euler-shaped arity of a rotation on this wire. `malformed-forms` carries the
    four-float rotation that must be refused; this one carries the three-float
    rotation that must not be.
    """
    capture = Capture("rotation-only-01", "127.0.0.1:51667")
    for index in range(3):
        capture.frame(index * FRAME_SECONDS, MEASURED_TRACKERS, index,
                      channels=("rotation",))
    return capture


def capture_tracker_dropout() -> Capture:
    """A tracker that stops mid-session and never comes back.

    Distinct from the single-frame loss in `three-trackers-58hz`: that one is a
    packet that did not arrive, this one is an identity that stopped existing.
    Nothing at the decode layer can tell them apart -- both are simply absent --
    and that is the finding this fixture hands to VRC-4, which is the layer that
    owns a timeout and is therefore the only one that can.
    """
    capture = Capture("tracker-dropout-01", "127.0.0.1:51668")
    for index in range(6):
        identities = MEASURED_TRACKERS if index < 2 else ("head", "1", "3")
        capture.frame(index * FRAME_SECONDS, identities, index)
    return capture


def capture_duplicate_and_reordered() -> Capture:
    """Three frames: in order, in reverse, and with one address sent twice.

    Neither shape is an error and neither is emergent behaviour to be discovered
    later. The reversed frame is the measured send order inverted, so an
    assembler that hard-codes "rotation, then position" against the 99.7 %
    finding has a fixture that breaks it; the duplicate is one address arriving
    twice in a frame with *different values*, which is the case where "keep the
    first" and "keep the last" produce different answers and a policy is
    therefore required rather than assumed.
    """
    capture = Capture("duplicate-and-reordered-01", "127.0.0.1:51669")
    capture.frame(0.0, MEASURED_TRACKERS, 0)

    receive = FRAME_SECONDS
    for step, (identity, channel) in enumerate(
            reversed(cycle(MEASURED_TRACKERS))):
        capture.add(receive + step * BURST_SECONDS,
                    tracker(identity, channel,
                            frame_values(identity, channel, 1)))

    receive = 2 * FRAME_SECONDS
    capture.frame(receive, MEASURED_TRACKERS, 2)
    # The same address again, later in the same burst and with the values of a
    # different frame, so "first" and "last" are distinguishable in the decoded
    # output rather than only in the count.
    capture.add(receive + 8 * BURST_SECONDS,
                tracker("1", "position", frame_values("1", "position", 9)))
    return capture


def capture_bundled_frame() -> Capture:
    """A whole frame in one OSC bundle: legal, unobserved, and required reading.

    44 918 recorded datagrams carried no bundle at all, which is a fact about
    one sender and not about the protocol. Without this fixture "no sender
    bundles" would quietly become "this decoder cannot read one", and the
    `bundled` flag the decoder forwards would be unverified against bytes.
    """
    capture = Capture("bundled-frame-01", "127.0.0.1:51670")
    for index in range(3):
        messages = [tracker(identity, channel,
                            frame_values(identity, channel, index))
                    for identity, channel in cycle(MEASURED_TRACKERS)]
        capture.add(index * FRAME_SECONDS, osc_bundle(*messages))
    return capture


def capture_mixed_traffic() -> Capture:
    """Tracker frames with everything else VRChat's OSC surface carries.

    Port 9000 is well known, the surface on it is far larger than the tracker
    subset this adapter reads, and a session carrying avatar parameters beside
    tracker data is the ordinary case rather than a fault. Holding
    UNSUPPORTED_ADDRESS apart from PACKET_MALFORMED is the whole reason this
    capture exists -- and the tracker-shaped rows below are why it is not just a
    list of foreign addresses:

    * a channel this adapter does not read (`velocity`) and an address with no
      channel at all are unsupported -- well-formed addresses that map to
      nothing;
    * `0`, `9`, `01` and `hip` are TRACKER_ID_INVALID -- the address is one this
      adapter knows, and the identity in it is not one it can read. Reporting
      those as unsupported would make a sender's bad index indistinguishable
      from a part of VRChat's surface this adapter has not implemented.
    """
    capture = Capture("mixed-traffic-01", "127.0.0.1:51671")
    capture.frame(0.0, MEASURED_TRACKERS, 0)
    capture.frame(FRAME_SECONDS, MEASURED_TRACKERS, 1)

    receive = 2 * FRAME_SECONDS

    def add(payload: bytes) -> None:
        nonlocal receive
        capture.add(receive, payload)
        receive += 0.001

    # Traffic from the rest of VRChat's OSC surface. Every one of these is
    # well-formed OSC that this adapter maps to nothing.
    add(osc_message("/avatar/parameters/VRCEmote", 3))
    add(osc_message("/avatar/parameters/GestureLeftWeight", 0.75))
    add(osc_message("/avatar/change", "avtr_00000000"))
    add(osc_message("/tracking/eye/CenterPitchYaw", 1.5, -2.5))
    add(osc_message("/input/Jump", 1))
    # Tracker-shaped and still unsupported: a channel this adapter does not
    # read, an address with no channel, and one with a segment past the channel.
    add(osc_message(f"{TRACKER_PREFIX}/1/velocity", 0.1, 0.2, 0.3))
    add(osc_message(f"{TRACKER_PREFIX}/2", 0.1, 0.2, 0.3))
    add(osc_message(f"{TRACKER_PREFIX}/3/position/x", 0.1, 0.2, 0.3))
    # Identities this adapter cannot read: below the range, above it, spelled
    # with a leading zero, and named something that is not `head`.
    add(tracker("0", "position", (0.1, 0.2, 0.3)))
    add(tracker("9", "position", (0.1, 0.2, 0.3)))
    add(tracker("01", "position", (0.1, 0.2, 0.3)))
    add(tracker("hip", "position", (0.1, 0.2, 0.3)))
    return capture


def capture_malformed_packets() -> Capture:
    """Datagrams that are not decodable OSC at all: the whole datagram is refused.

    The refusal is the shared decoder's (`libs/osc`) and the *code* is this
    adapter's, which is the two-layer split OSC-3 built and the one thing this
    capture is really about. Nine ways a datagram is not OSC, and one valid
    tracker message at the end -- without it, a decoder that refused everything
    would pass this fixture.
    """
    capture = Capture("malformed-packets-01", "127.0.0.1:51672")
    time = 0.0

    def add(payload: bytes) -> None:
        nonlocal time
        capture.add(time, payload)
        time += 0.001

    # A zero-length datagram. Receivable on any socket, and the smallest thing a
    # decoder must refuse without reading a byte of it.
    add(b"")
    # Not a multiple of four, which OSC requires of every packet.
    add(b"/tracking")
    # Neither '/' nor "#bundle": a sender that is not writing OSC at this port.
    add(b"tracking/trackers/1/position\x00\x00\x00\x00")
    # An address with no terminator: every byte of the datagram is address.
    add(b"/tracking/trackers/1/position/xx")
    # A type tag string that does not begin with a comma.
    add(osc_string("/tracking/trackers/1/position") + osc_string("fff")
        + struct.pack(">fff", 0.1, 0.2, 0.3))
    # No type tag string at all: the address, and nothing after it.
    add(osc_string("/tracking/trackers/1/position"))
    # Three floats declared, four sent. The extra bytes are not an extended
    # form; the type tags do not describe the payload, so the framing is wrong.
    add(osc_string("/tracking/trackers/1/position") + osc_string(",fff")
        + struct.pack(">ffff", 0.1, 0.2, 0.3, 0.4))
    # A type tag OSC does not define. Its size is unknown, so everything after
    # it would be read at the wrong offset.
    add(osc_string("/tracking/trackers/1/position") + osc_string(",fQf")
        + struct.pack(">fff", 0.1, 0.2, 0.3))
    # A bundle whose element declares more bytes than the bundle has left.
    element = tracker("1", "position", (0.1, 0.2, 0.3))
    add(b"#bundle\x00" + struct.pack(">Q", 1)
        + struct.pack(">i", len(element) + 16) + element)
    # And one that is fine, so the capture cannot be passed by refusing
    # everything.
    add(tracker("1", "position", (0.1, 0.2, 0.3)))
    return capture


def capture_malformed_forms() -> Capture:
    """Valid OSC at a known address, with arguments that are not this address's.

    The counterpart to the capture above: every datagram here decodes as OSC and
    the refusal is one layer up, per message rather than per datagram.

    The load-bearing datagram is the second: a bundle carrying a whole frame in
    which one message is a four-float rotation. The claim that a bad message
    costs *that message* and not the seven that arrived with it is made by a
    recorded session rather than only by a unit test -- and the message chosen to
    be bad is the one whose plausibility is the danger. A four-float rotation is
    a quaternion; reading its first three components as Euler angles is not a
    partial read of a longer form, it is a confident misreading of a different
    one, and it is the reason this adapter refuses arguments past the known form
    where `vrmAdapterVmc` counts them.
    """
    capture = Capture("malformed-forms-01", "127.0.0.1:51673")
    capture.frame(0.0, MEASURED_TRACKERS, 0)

    messages = [tracker(identity, channel, frame_values(identity, channel, 1))
                for identity, channel in cycle(MEASURED_TRACKERS)]
    # cycle() is head/rotation, head/position, 1/rotation, ... so index 2 is
    # `/tracking/trackers/1/rotation` -- which is also the address the real
    # session loses most often, and therefore the one most worth being sure
    # about.
    messages[2] = osc_message(f"{TRACKER_PREFIX}/1/rotation",
                              5.25, 3.0, -1.0, 1.0)
    capture.add(FRAME_SECONDS, osc_bundle(*messages))

    time = 2 * FRAME_SECONDS

    def add(payload: bytes) -> None:
        nonlocal time
        capture.add(time, payload)
        time += 0.001

    # Two floats where three are required.
    add(osc_message(f"{TRACKER_PREFIX}/2/position", 0.1, 0.2))
    # The right count and every type wrong: a position in doubles.
    add(osc_message(f"{TRACKER_PREFIX}/2/rotation",
                    *[Float64(value) for value in (0.1, 0.2, 0.3)]))
    # A named argument first, as if this were VMC's `,sfff`. It is not: the
    # identity is in the address on this wire, and a sender putting it in an
    # argument is speaking a different protocol at the same port.
    add(osc_message(f"{TRACKER_PREFIX}/3/position", "3", 0.1, 0.2, 0.3))
    # No arguments at all.
    add(osc_message(f"{TRACKER_PREFIX}/3/rotation"))
    # Integers, which are not floats and are not coerced into them.
    add(osc_message(f"{TRACKER_PREFIX}/head/position", 0, 1, 2))
    # Non-finite components: a NaN and an infinity. Both are well-formed IEEE
    # 754 and neither is a coordinate -- every comparison against a NaN is
    # false, so one that reached a solve would make a tracker disappear from
    # every test that had one.
    add(tracker("head", "rotation", (float("nan"), 0.0, 0.0)))
    add(tracker("2", "position", (0.0, float("inf"), 0.0)))
    return capture


def capture_session_restart() -> Capture:
    """A session that stops and starts again, and says so in the only way
    this wire can.

    The recorded `session-restart` take stopped the application's streaming
    for about ten seconds and started it again. Three things were measured
    and all three are here: the source port changes, the dark window is
    4.8452 s, and there is **no other marker at all** -- no session
    identifier, no rest table, no handshake, and no in-band signal of any
    kind (report 02 §4). The two ports are that session's.

    This is the fixture VRC-4 needs to tell a source that paused from a
    second source that began, and it could not have existed before
    2026-08-30: a capture had one peer in its header and none per record, so
    a replayed restart was a gap and nothing more. Its pair is
    `silent-gap`, which is the same silence with the same peer either side.
    """
    capture = Capture("session-restart-01", "192.168.1.8:51662")
    capture.record_peer = "192.168.1.8:51662"
    for index in range(3):
        capture.frame(index * FRAME_SECONDS, MEASURED_TRACKERS, index)
    # The measured dark window, to four decimals, from the last datagram of
    # the old session to the first of the new.
    resume = 2 * FRAME_SECONDS + 4.8452
    capture.record_peer = "192.168.1.8:50035"
    for index in range(3):
        capture.frame(resume + index * FRAME_SECONDS, MEASURED_TRACKERS,
                      index)
    return capture


def capture_silent_gap() -> Capture:
    """The same silence, and the same sender on the other side of it.

    A pause is not a restart, and this is the capture that says so: the gap
    is `session-restart`'s to the microsecond and the source port never
    changes, so an assembler that read a long silence as a new session
    would split this stream in two and forget a tracker set that never went
    away.

    Invented as a *shape*: no recorded take pauses without restarting,
    because the operator stopped the application to produce the pause. What
    it pins is the policy rather than the sender -- `SOURCE_TIMEOUT` and
    nothing more, however long the silence lasts.
    """
    capture = Capture("silent-gap-01", "192.168.1.8:51662")
    capture.record_peer = "192.168.1.8:51662"
    for index in range(3):
        capture.frame(index * FRAME_SECONDS, MEASURED_TRACKERS, index)
    resume = 2 * FRAME_SECONDS + 4.8452
    for index in range(3):
        capture.frame(resume + index * FRAME_SECONDS, MEASURED_TRACKERS,
                      index)
    return capture


def capture_calibration_jump() -> Capture:
    """Every tracker moves 1.2 m at one frame boundary, at once.

    VRChat's tracking space is the *player's*, established by a calibration
    the receiving application performs; redoing it moves everything into a
    space whose relationship to the old one nothing in this adapter can
    compute. What separates that from motion is **simultaneity** -- one
    tracker jumping is a tracking glitch, and a body cannot travel 1.2 m in
    17 ms in any case (that is 70 m/s).

    Unobserved: no recorded session contains a recalibration, because none
    was performed while recording. So this fixture pins a stated policy and
    not a measured shape, and the threshold it exercises is a decision
    rather than a reading.
    """
    capture = Capture("calibration-jump-01", "127.0.0.1:51662")
    for index in range(6):
        shift = (1.2, 0.0, -0.9) if index >= 3 else (0.0, 0.0, 0.0)
        capture.frame(index * FRAME_SECONDS, MEASURED_TRACKERS, index,
                      shift=shift)
    return capture


def rig_motion_values(identity: str, channel: str, frame: int):
    """One tracker's values in a session that is *doing* something.

    Every other capture here drifts by a millimetre and a quarter of a degree
    per frame, on purpose: what those fixtures pin is that a decoder does not
    return its defaults and does not cross two identities, and a tiny drift
    proves both while keeping the numbers readable in a hex dump.

    That is not enough for one question, and it is the question this fixture
    exists for: **does a session reach a rig**. The end-to-end check compares a
    joint's rotation across time, and a quarter of a degree per frame composed
    through a retarget's rest-pose correction lands within the tolerance that
    check needs to leave for float error -- so a session that arrived perfectly
    and a session that arrived not at all would both read as "nothing moved".

    So this one walks: half a metre forward, a head that turns most of a right
    angle, and two feet pitching in opposite directions. Nothing here is a
    measurement of a real body, and the manifest says so -- what it pins is a
    *magnitude* large enough that the arrival is unambiguous, which is a
    property of the test and not of the wire.
    """
    step = frame * 0.05
    table = {
        # Forward, and turning left. The angles are the wire's own: degrees, in
        # the sender's left-handed space, composed Ry * Rx * Rz (TrackingSpace.h).
        "head": ((0.020, 1.620, -0.050 + step), (0.0, -6.0 * frame, 0.0)),
        "1": ((0.010, 0.980, -0.020 + step), (0.0, -3.0 * frame, 0.0)),
        # The feet pitch in opposite directions, which is what makes the two
        # distinguishable in a joint comparison: a solve that bound both to one
        # region would show them moving together.
        "2": ((-0.110, 0.460, 0.030 + step), (4.0 * frame, 0.0, 0.0)),
        "3": ((0.100, 0.450, 0.020 + step), (-4.0 * frame, 0.0, 0.0)),
    }
    position, rotation = table[identity]
    return position if channel == "position" else rotation


def capture_rig_motion() -> Capture:
    """Twelve unbroken frames of a body walking, turning and rolling its feet.

    The one capture here written for a *consumer* rather than for a decoder.
    Everything else in this corpus pins a shape the wire produces; this pins
    that the shape reaches the far end -- `vrchat_osc_record --export-trace`,
    then `motion_capture`, then `motion_retarget`, onto a rig whose joints are
    checked by name (VRC-6).

    Nothing is lost and nothing is refused: a frame missing here would make a
    failed end-to-end run ambiguous between the loss and the chain, and loss
    already has three fixtures of its own.

    Unobserved, and the manifest says so. The 2026-08-30 session was recorded
    standing and turning a head, so the magnitudes below are invented -- and
    they are invented in the one dimension a test needs and no reading depends
    on.
    """
    capture = Capture("rig-motion-01", "127.0.0.1:51662")
    for index in range(12):
        capture.frame(index * FRAME_SECONDS, MEASURED_TRACKERS, index,
                      values_for=rig_motion_values)
    return capture


CAPTURES = {
    "three-trackers-58hz.vrchatoscpackets": capture_three_trackers,
    "rig-motion.vrchatoscpackets": capture_rig_motion,
    "one-tracker.vrchatoscpackets": capture_one_tracker,
    "eight-trackers.vrchatoscpackets": capture_eight_trackers,
    "head-absent.vrchatoscpackets": capture_head_absent,
    "position-only.vrchatoscpackets": capture_position_only,
    "rotation-only.vrchatoscpackets": capture_rotation_only,
    "tracker-dropout.vrchatoscpackets": capture_tracker_dropout,
    "session-restart.vrchatoscpackets": capture_session_restart,
    "silent-gap.vrchatoscpackets": capture_silent_gap,
    "calibration-jump.vrchatoscpackets": capture_calibration_jump,
    "duplicate-and-reordered.vrchatoscpackets": capture_duplicate_and_reordered,
    "bundled-frame.vrchatoscpackets": capture_bundled_frame,
    "mixed-traffic.vrchatoscpackets": capture_mixed_traffic,
    "malformed-packets.vrchatoscpackets": capture_malformed_packets,
    "malformed-forms.vrchatoscpackets": capture_malformed_forms,
}


# ---------------------------------------------------------------------------
# Rendering -- must equal the C++ writer, character for character
# ---------------------------------------------------------------------------

HEX_COLUMN_WIDTH = BYTES_PER_LINE * 3 - 1


def gutter(chunk: bytes) -> str:
    return "".join(chr(byte) if 0x20 <= byte <= 0x7e else "." for byte in chunk)


def render(capture: Capture) -> str:
    """Emit the capture exactly as `WritePacketCapture` would."""
    out = ["!vrchat-osc-packet-capture 1"]
    if capture.sender:
        out.append(f"sender {capture.sender}")
    if capture.device:
        out.append(f"device {capture.device}")
    if capture.source_id:
        out.append(f"sourceId {capture.source_id}")
    if capture.listen:
        out.append(f"listen {capture.listen}")
    if capture.peer:
        out.append(f"peer {capture.peer}")

    # A `p` line only where the peer changes, which is what the C++ writer
    # does: a capture whose records name nobody renders exactly as it did
    # before the line existed, and a restart is two lines in a fixture
    # rather than one per datagram.
    emitted = ""
    emitted_any = False
    for index, (receive_time, payload) in enumerate(capture.datagrams):
        peer = capture.peers[index] if capture.peers else ""
        out.append("")
        if peer != emitted and (emitted_any or peer):
            out.append(f"p {peer if peer else '-'}")
            emitted, emitted_any = peer, True
        out.append(f"d {receive_time:.6f} {len(payload)}")
        for offset in range(0, len(payload), BYTES_PER_LINE):
            chunk = payload[offset:offset + BYTES_PER_LINE]
            hex_column = " ".join(f"{byte:02x}" for byte in chunk)
            out.append(f"  {hex_column.ljust(HEX_COLUMN_WIDTH)}"
                       f"  |{gutter(chunk)}|")

    return "\n".join(out) + "\n"


# ---------------------------------------------------------------------------
# The manifest
# ---------------------------------------------------------------------------

# A *lexical* scan, not a decode: every ASCII run that looks like an OSC address
# pattern. Deriving this by decoding would make the manifest agree with the
# decoder rather than describe the bytes, which is the same rule the corpus
# itself is generated under.
_ADDRESS = re.compile(rb"/[A-Za-z0-9/_]+")


def measure(text: str) -> dict:
    """Read back out of a capture everything the manifest claims about it."""
    sender = ""
    device = ""
    source_id = ""
    times: list[float] = []
    peers: list[str] = []
    peer = ""
    payload_bytes = 0
    # Per datagram, never concatenated: two records joined end to end produce
    # address patterns neither of them contains.
    payloads: list[bytearray] = []

    for line in text.splitlines():
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            continue
        tokens = stripped.split()
        if tokens[0] == "p":
            peer = "" if tokens[1] == "-" else tokens[1]
        elif tokens[0] == "d":
            times.append(float(tokens[1]))
            payload_bytes += int(tokens[2])
            payloads.append(bytearray())
            if peer and peer not in peers:
                peers.append(peer)
        elif not payloads:
            if tokens[0] == "sender":
                sender = tokens[1]
            elif tokens[0] == "device":
                device = tokens[1]
            elif tokens[0] == "sourceId":
                source_id = tokens[1]
        else:
            payloads[-1] += bytes.fromhex(
                stripped.split("|")[0].replace(" ", ""))

    addresses: set[str] = set()
    for payload in payloads:
        addresses.update(match.decode("ascii")
                         for match in _ADDRESS.findall(bytes(payload)))

    data = text.encode("utf-8")
    return {
        "sender": sender,
        "device": device,
        "sourceId": source_id,
        "datagrams": len(times),
        "payloadBytes": payload_bytes,
        "durationSeconds": round(times[-1] - times[0], 6) if times else 0.0,
        "addressPatterns": sorted(addresses),
        # In first-seen order rather than sorted: on this wire the order two
        # peers appear in *is* the order the sessions ran in, and a restart
        # is the only thing a second peer means here.
        "peers": peers,
        "sha256": hashlib.sha256(data).hexdigest(),
        "bytes": len(data),
    }


def field_matches(claimed, measured) -> bool:
    if isinstance(measured, float) or isinstance(claimed, float):
        try:
            return abs(float(claimed) - float(measured)) <= 1e-6
        except (TypeError, ValueError):
            return False
    return claimed == measured


def sync_manifest(directory: pathlib.Path, rendered: dict[str, str],
                  check: bool) -> list[str]:
    """Compare (or rewrite) the manifest's measured fields. Returns problems."""
    path = directory / MANIFEST_NAME
    if not path.exists():
        return [f"{MANIFEST_NAME} is missing"]

    manifest = json.loads(path.read_text(encoding="utf-8"))
    problems: list[str] = []

    if manifest.get("format", {}).get("version") != 1:
        problems.append(
            f"{MANIFEST_NAME}: format.version is "
            f"{manifest.get('format', {}).get('version')!r}, expected 1")

    listed = [entry.get("file") for entry in manifest.get("captures", [])]
    if sorted(listed) != sorted(rendered):
        problems.append(
            f"{MANIFEST_NAME} describes {sorted(listed)}, the corpus is "
            f"{sorted(rendered)}")
        return problems

    for entry in manifest["captures"]:
        derived = {"id": pathlib.Path(entry["file"]).stem,
                   **measure(rendered[entry["file"]])}
        for key, value in derived.items():
            if not field_matches(entry.get(key), value):
                problems.append(
                    f"{entry['file']}: {key} is {entry.get(key)!r}, measured "
                    f"{value!r}")
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
                      / "corpus" / "generated")
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=pathlib.Path, default=default_output,
                        help="corpus directory to write into")
    parser.add_argument("--check", action="store_true",
                        help="fail if any committed capture differs from the "
                             "freshly generated one instead of rewriting it")
    args = parser.parse_args()

    args.output.mkdir(parents=True, exist_ok=True)
    drifted: list[str] = []
    rendered: dict[str, str] = {}
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
        print("run: python adapters/liveCapture/vrchatOsc/tools/"
              "generate_packets.py", file=sys.stderr)
        return 1

    problems = sync_manifest(args.output, rendered, args.check)
    if problems:
        for problem in problems:
            print(f"manifest drift: {problem}", file=sys.stderr)
        print("run: python adapters/liveCapture/vrchatOsc/tools/"
              "generate_packets.py", file=sys.stderr)
        return 1
    if not args.check:
        print(f"{MANIFEST_NAME} is in sync with {len(rendered)} capture(s)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
