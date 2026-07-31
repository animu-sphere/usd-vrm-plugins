#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Author the VMC packet corpus: the recorded datagrams the decoder replays.

Why these are synthesised rather than captured off a real sender: a recording
made from a commercial sender application carries that application's avatar, and
the VRM corpus is already licence-gated for exactly that reason
(plugins/usdVrmFileFormat/tests/corpus/CORPUS.md). A fixture nobody outside the
project may redistribute is a fixture CI cannot run, and the whole point of
recording packets is that the adapter is verifiable with no hardware and no
socket. Every byte here is assembled by this file from the OSC and VMC message
shapes, so the corpus is Apache-2.0 like the rest of the repo, byte-stable
across machines, and reviewable in a diff.

What they are not: a substitute for validating against real senders. These
reproduce the *shapes* the protocol produces -- a bundled frame, an unbundled
one, blend-shape and device traffic the body path must ignore, malformed
datagrams, a duplicate, a backwards sender clock, a restart -- not any
particular application's quirks. Two sender applications and a capture device
relayed through one are Milestone B's evidence, recorded with the record tool
and added to this corpus as they are measured
(roadmap/adapters-mocopi-vmc-ardy.md §10).

The output must match the C++ writer byte for byte; `vrmAdapterVmc_corpus`
enforces that. Run:

    python adapters/liveCapture/vmc/tools/generate_packets.py

`manifest.json` is maintained from here too. Its measured fields (datagram
counts, payload sizes, durations, the address patterns present, digests) are
re-derived from the captures on every run and compared under --check, so the
manifest cannot quietly stop describing the corpus; its prose fields (`pins`,
`tags`, the top-level notes) are hand-written and preserved untouched.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import pathlib
import re
import struct
import sys

MANIFEST_NAME = "manifest.json"
BYTES_PER_LINE = 16

# ---------------------------------------------------------------------------
# OSC 1.0 encoding
#
# Deliberately written out here rather than pulled from a library: the corpus is
# the decoder's *input*, and generating it with the same third-party
# implementation the decoder might later use would make the fixtures agree with
# the decoder by construction rather than by the protocol.
# ---------------------------------------------------------------------------


def osc_string(text: str) -> bytes:
    """A NUL-terminated string padded to a four-byte boundary.

    Always at least one NUL: a string whose length is already a multiple of four
    gets four padding bytes, not zero.
    """
    raw = text.encode("ascii") + b"\x00"
    padding = (-len(raw)) % 4
    return raw + b"\x00" * padding


def osc_message(address: str, *arguments) -> bytes:
    """One OSC message. Argument types are inferred: int, float, str."""
    tags = ","
    body = b""
    for argument in arguments:
        if isinstance(argument, bool):
            raise TypeError("OSC 1.0 has no boolean argument type here")
        if isinstance(argument, int):
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

    The time tag is `1` -- NTP "immediately" -- because that is what real-time
    senders emit; a bundle scheduled for a future instant is a shape this
    adapter has no reason to author and no policy for.
    """
    packet = b"#bundle\x00" + struct.pack(">Q", 1)
    for element in elements:
        packet += struct.pack(">i", len(element)) + element
    return packet


# ---------------------------------------------------------------------------
# VMC messages
# ---------------------------------------------------------------------------


def vmc_ok(loaded: int = 1, calibration_state: int = 3,
           calibration_mode: int = 0) -> bytes:
    """`/VMC/Ext/OK` -- the sender's availability and calibration status."""
    return osc_message("/VMC/Ext/OK", loaded, calibration_state,
                       calibration_mode)


def vmc_time(seconds: float) -> bytes:
    """`/VMC/Ext/T` -- the *sender's* clock, which is not the receive clock."""
    return osc_message("/VMC/Ext/T", float(seconds))


def vmc_model(path: str, title: str) -> bytes:
    """`/VMC/Ext/VRM` -- the source model, kept as provenance, never resolved."""
    return osc_message("/VMC/Ext/VRM", path, title)


def vmc_transform(address: str, name: str, position, rotation) -> bytes:
    """A name plus a position and a quaternion, the shape VMC uses throughout.

    The quaternion is serialised x, y, z, w -- Unity's component order, which is
    not the (w, x, y, z) the canonical layer uses. That conversion belongs to the
    mapping step, not to a fixture.
    """
    return osc_message(address, name,
                       float(position[0]), float(position[1]),
                       float(position[2]),
                       float(rotation[0]), float(rotation[1]),
                       float(rotation[2]), float(rotation[3]))


def vmc_bone(name: str, position, rotation) -> bytes:
    return vmc_transform("/VMC/Ext/Bone/Pos", name, position, rotation)


def vmc_root(position, rotation) -> bytes:
    return vmc_transform("/VMC/Ext/Root/Pos", "root", position, rotation)


def vmc_blend(name: str, value: float) -> bytes:
    return osc_message("/VMC/Ext/Blend/Val", name, float(value))


def vmc_blend_apply() -> bytes:
    """`/VMC/Ext/Blend/Apply` -- no arguments, so the type tag string is bare."""
    return osc_message("/VMC/Ext/Blend/Apply")


IDENTITY = (0.0, 0.0, 0.0, 1.0)


def quaternion_z(degrees: float) -> tuple[float, float, float, float]:
    """Axis-angle about +Z to Unity's (x, y, z, w) order."""
    half = math.radians(degrees) * 0.5
    return (0.0, 0.0, math.sin(half), math.cos(half))


# A synthetic rest skeleton: each bone's local position in metres, relative to
# its parent, as a VMC sender reports it. Invented proportions -- no real
# avatar's measurements are committed here -- but plausible ones, so a decoded
# frame is recognisable as a humanoid rather than a pile of zeros.
REST_OFFSETS = {
    "Hips": (0.0, 0.9, 0.0),
    "LeftUpperLeg": (0.09, 0.0, 0.0),
    "RightUpperLeg": (-0.09, 0.0, 0.0),
    "LeftLowerLeg": (0.0, -0.42, 0.0),
    "RightLowerLeg": (0.0, -0.42, 0.0),
    "LeftFoot": (0.0, -0.4, 0.0),
    "RightFoot": (0.0, -0.4, 0.0),
    "Spine": (0.0, 0.1, 0.0),
    "Chest": (0.0, 0.12, 0.0),
    "Neck": (0.0, 0.2, 0.0),
    "Head": (0.0, 0.08, 0.0),
    "LeftShoulder": (0.03, 0.15, 0.0),
    "RightShoulder": (-0.03, 0.15, 0.0),
    "LeftUpperArm": (0.12, 0.0, 0.0),
    "RightUpperArm": (-0.12, 0.0, 0.0),
    "LeftLowerArm": (0.26, 0.0, 0.0),
    "RightLowerArm": (-0.26, 0.0, 0.0),
    "LeftHand": (0.24, 0.0, 0.0),
    "RightHand": (-0.24, 0.0, 0.0),
    "LeftToes": (0.0, -0.06, 0.12),
    "RightToes": (0.0, -0.06, 0.12),
    "UpperChest": (0.0, 0.08, 0.0),
}

# The order a Unity sender walks its bones: HumanBodyBones enum order, in which
# the legs precede the spine and `UpperChest` sorts *last* of all -- it was added
# to the enum after the fingers. Nothing downstream may infer hierarchy from
# arrival order, and a corpus that emitted a tidy parent-first order would let an
# adapter get away with assuming otherwise.
BODY_RIG = [
    "Hips",
    "LeftUpperLeg", "RightUpperLeg", "LeftLowerLeg", "RightLowerLeg",
    "LeftFoot", "RightFoot",
    "Spine", "Chest", "Neck", "Head",
    "LeftShoulder", "RightShoulder", "LeftUpperArm", "RightUpperArm",
    "LeftLowerArm", "RightLowerArm", "LeftHand", "RightHand",
    "LeftToes", "RightToes",
]
FULL_TORSO_RIG = BODY_RIG + ["UpperChest"]

ROOT_POSITION = (0.0, 0.0, 0.0)
MODEL_PATH = "avatar.vrm"
MODEL_TITLE = "Example Avatar"


class Capture:
    """A recorded session: provenance plus (receive time, payload) records."""

    def __init__(self, sender: str, source_id: str, listen: str,
                 peer: str) -> None:
        self.sender = sender
        self.source_id = source_id
        self.listen = listen
        self.peer = peer
        self.datagrams: list[tuple[float, bytes]] = []

    def add(self, receive_time: float, payload: bytes) -> None:
        self.datagrams.append((round(receive_time, 6), payload))


def body_messages(rig: list[str], rotations: dict[str, tuple] | None = None
                  ) -> list[bytes]:
    """One message per bone, plus the root, in sender order."""
    rotations = rotations or {}
    messages = [vmc_root(ROOT_POSITION, IDENTITY)]
    for bone in rig:
        messages.append(
            vmc_bone(bone, REST_OFFSETS[bone], rotations.get(bone, IDENTITY)))
    return messages


# ---------------------------------------------------------------------------
# The captures
# ---------------------------------------------------------------------------


def capture_neutral_standing() -> Capture:
    """The happy path: one OSC bundle per frame, a full torso rig, no motion.

    A neutral pose is every rotation identity, which is the one expected result
    a decoder test can state without a second implementation to compare against.
    """
    capture = Capture("example.synthetic", "neutral-standing-01",
                      "0.0.0.0:39539", "127.0.0.1:52001")
    capture.add(0.0, osc_bundle(vmc_ok(), vmc_model(MODEL_PATH, MODEL_TITLE)))

    for index in range(5):
        receive = 0.01 + index / 30.0
        # The sender's clock is its own uptime and shares no origin with the
        # receive clock. An adapter that treats one as the other looks correct
        # on a fixture where both start at zero.
        sender_time = 12.5 + index / 30.0
        capture.add(receive,
                    osc_bundle(vmc_time(sender_time),
                               *body_messages(FULL_TORSO_RIG)))
    return capture


def capture_arm_raise() -> Capture:
    """The other sender shape: one message per datagram, and no `UpperChest`.

    The frame boundary is not a bundle here -- it is the `/VMC/Ext/T` that
    *closes* each frame, the opposite convention from the bundled capture. Both
    are in the corpus so that neither can be assumed.
    """
    capture = Capture("example.synthetic", "arm-raise-01", "0.0.0.0:39539",
                      "127.0.0.1:52002")
    capture.add(0.0, vmc_ok())
    capture.add(0.000200, vmc_model(MODEL_PATH, MODEL_TITLE))

    receive = 0.01
    for index in range(5):
        rotations = {
            "LeftShoulder": quaternion_z(3.0 * index),
            "LeftUpperArm": quaternion_z(15.0 * index),
            "LeftLowerArm": quaternion_z(8.0 * index),
        }
        for message in body_messages(BODY_RIG, rotations):
            capture.add(receive, message)
            # Datagrams of one frame arrive spread across the frame interval,
            # which is what makes "when did this frame end" a question at all.
            receive += 0.000400
        capture.add(receive, vmc_time(20.0 + index / 30.0))
        receive = 0.01 + (index + 1) / 30.0
    return capture


def capture_mixed_traffic() -> Capture:
    """What a real sender also emits, and the body path must ignore quietly.

    Blend shapes, a head-mounted display, a controller, a camera, an option
    string. None of it is malformed; all of it is unimplemented, which is a
    different diagnostic (`VRM_VMC_UNSUPPORTED_MESSAGE`, info, recoverable) and
    the distinction is the point of this fixture.
    """
    capture = Capture("example.synthetic", "mixed-traffic-01",
                      "0.0.0.0:39539", "127.0.0.1:52003")
    capture.add(0.0, osc_bundle(vmc_ok(), vmc_model(MODEL_PATH, MODEL_TITLE),
                                osc_message("/VMC/Ext/Opt", "example=1")))

    for index in range(3):
        receive = 0.01 + index / 30.0
        sender_time = 4.0 + index / 30.0
        blend = 0.25 * index
        capture.add(receive,
                    osc_bundle(vmc_time(sender_time),
                               *body_messages(BODY_RIG),
                               vmc_blend("Joy", blend),
                               vmc_blend("Blink", 1.0 - blend),
                               vmc_blend("A", 0.0),
                               vmc_blend_apply()))
        capture.add(receive + 0.001,
                    vmc_transform("/VMC/Ext/Hmd/Pos", "Head", (0.0, 1.6, 0.0),
                                  IDENTITY))
        capture.add(receive + 0.0012,
                    vmc_transform("/VMC/Ext/Con/Pos", "LeftHand",
                                  (0.3, 1.1, 0.1), IDENTITY))
        capture.add(receive + 0.0014,
                    osc_message("/VMC/Ext/Cam", "Camera", 0.0, 1.4, -1.5,
                                0.0, 0.0, 0.0, 1.0, 60.0))
    return capture


def capture_malformed() -> Capture:
    """Every packet-level refusal, one datagram each.

    Two of these are *not* refusals and belong here for the contrast: a
    well-formed message outside the VMC namespace, and a well-formed VMC message
    this adapter does not implement. Both are `VRM_VMC_UNSUPPORTED_MESSAGE`, and
    a decoder that reports them as malformed is blaming the sender for
    something the sender did correctly.
    """
    capture = Capture("example.synthetic", "malformed-01", "0.0.0.0:39539",
                      "127.0.0.1:52004")
    time = 0.0

    def add(payload: bytes) -> None:
        nonlocal time
        capture.add(time, payload)
        time += 0.001

    # A zero-length datagram: legal to receive, decodable by nothing.
    add(b"")
    # An address pattern with no NUL terminator and nothing after it.
    add(b"/VMC/Ext/T")
    # A type-tag string that does not begin with a comma.
    add(osc_string("/VMC/Ext/T") + osc_string("f") + struct.pack(">f", 0.5))
    # `,f` promising a float, with two bytes of it delivered.
    add(osc_string("/VMC/Ext/T") + osc_string(",f") + b"\x3d\xcc")
    # `,sfffffff` promising a bone, with an unterminated string argument.
    add(osc_string("/VMC/Ext/Bone/Pos") + osc_string(",sfffffff")
        + b"Hips")
    # A bundle whose element length runs past the end of the datagram.
    add(b"#bundle\x00" + struct.pack(">Q", 1) + struct.pack(">i", 512)
        + vmc_time(0.5))
    # A well-formed message with three bytes of tail, so the packet is not a
    # whole number of four-byte words.
    add(vmc_time(0.5) + b"\x01\x02\x03")
    # Not OSC at all: a stray packet on a shared port.
    add(b"\xde\xad\xbe\xef\xde\xad\xbe\xef")
    # Well-formed OSC, outside the VMC namespace.
    add(osc_message("/foo/bar", 1))
    # Well-formed VMC, a message this adapter does not implement.
    add(osc_message("/VMC/Ext/Midi/Note", 1, 60, 100))
    return capture


def capture_sender_restart() -> Capture:
    """The arrival-order phenomena, in one session.

    A duplicated datagram, a sender clock that goes backwards, a frame that
    stops halfway, and a restart that resets the clock to zero. Receive times
    still increase throughout -- a receive clock does not run backwards, and
    pretending it does would test the recorder rather than the assembler.
    """
    capture = Capture("example.synthetic", "sender-restart-01",
                      "0.0.0.0:39539", "127.0.0.1:52005")
    capture.add(0.0, osc_bundle(vmc_ok(), vmc_model(MODEL_PATH, MODEL_TITLE)))

    def frame(sender_time: float) -> bytes:
        return osc_bundle(vmc_time(sender_time), *body_messages(BODY_RIG))

    capture.add(0.010000, frame(30.000000))
    capture.add(0.043333, frame(30.033333))

    # The same datagram delivered twice: identical bytes, a later arrival.
    duplicated = frame(30.066667)
    capture.add(0.076667, duplicated)
    capture.add(0.078000, duplicated)

    # The sender's clock goes backwards while arrival order does not.
    capture.add(0.110000, frame(30.050000))

    # A frame that stops halfway: the bundle carries the first six bones and
    # then the sender goes away.
    capture.add(0.143333,
                osc_bundle(vmc_time(30.133333),
                           vmc_root(ROOT_POSITION, IDENTITY),
                           *[vmc_bone(bone, REST_OFFSETS[bone], IDENTITY)
                             for bone in BODY_RIG[:6]]))

    # The restart: a fresh handshake and a clock that begins again.
    capture.add(0.900000, osc_bundle(vmc_ok(),
                                     vmc_model(MODEL_PATH, MODEL_TITLE)))
    capture.add(0.910000, frame(0.016667))
    capture.add(0.943333, frame(0.050000))
    return capture


CAPTURES = {
    "neutral-standing-30hz.vmcpackets": capture_neutral_standing,
    "arm-raise-30hz.vmcpackets": capture_arm_raise,
    "mixed-traffic-30hz.vmcpackets": capture_mixed_traffic,
    "malformed-packets.vmcpackets": capture_malformed,
    "sender-restart-30hz.vmcpackets": capture_sender_restart,
}


# ---------------------------------------------------------------------------
# Rendering -- must equal the C++ writer, character for character
# ---------------------------------------------------------------------------

HEX_COLUMN_WIDTH = BYTES_PER_LINE * 3 - 1


def gutter(chunk: bytes) -> str:
    return "".join(chr(byte) if 0x20 <= byte <= 0x7e else "." for byte in chunk)


def render(capture: Capture) -> str:
    """Emit the capture exactly as `WritePacketCapture` would."""
    out = ["!vmc-packet-capture 1"]
    if capture.sender:
        out.append(f"sender {capture.sender}")
    if capture.source_id:
        out.append(f"sourceId {capture.source_id}")
    if capture.listen:
        out.append(f"listen {capture.listen}")
    if capture.peer:
        out.append(f"peer {capture.peer}")

    for receive_time, payload in capture.datagrams:
        out.append("")
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
# decoder rather than describe the bytes, and the decoder does not exist yet.
_ADDRESS = re.compile(rb"/[A-Za-z0-9/_]+")


def measure(text: str) -> dict:
    """Read back out of a capture everything the manifest claims about it."""
    sender = ""
    source_id = ""
    times: list[float] = []
    payload_bytes = 0
    # Per datagram, never concatenated: two records joined end to end produce
    # address patterns neither of them contains -- `//VMC/Ext/Bone/Pos` out of a
    # float that happens to end in 0x2f, and `/VMC/Ext/T/VMC/Ext/T` out of the
    # unterminated address in the malformed corpus. Both were measured that way
    # once.
    payloads: list[bytearray] = []

    for line in text.splitlines():
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            continue
        tokens = stripped.split()
        if tokens[0] == "d":
            times.append(float(tokens[1]))
            payload_bytes += int(tokens[2])
            payloads.append(bytearray())
        elif not payloads:
            if tokens[0] == "sender":
                sender = tokens[1]
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
        "sourceId": source_id,
        "datagrams": len(times),
        "payloadBytes": payload_bytes,
        "durationSeconds": round(times[-1] - times[0], 6) if times else 0.0,
        "addressPatterns": sorted(addresses),
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
                      / "corpus")
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
        print("run: python adapters/liveCapture/vmc/tools/generate_packets.py",
              file=sys.stderr)
        return 1

    problems = sync_manifest(args.output, rendered, args.check)
    if problems:
        for problem in problems:
            print(f"manifest drift: {problem}", file=sys.stderr)
        print("run: python adapters/liveCapture/vmc/tools/generate_packets.py",
              file=sys.stderr)
        return 1
    if not args.check:
        print(f"{MANIFEST_NAME} is in sync with {len(rendered)} capture(s)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
