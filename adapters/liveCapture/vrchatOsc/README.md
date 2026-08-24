# vrmAdapterVrchatOsc

The third live input adapter: VRChat OSC tracking data in, and — eventually —
canonical humanoid motion out. It is the first adapter here whose input is not a
pose.

```text
UDP datagram → OSC decode → tracker semantics → tracking-space normalisation
             → tracker frame → (a generic humanoid solve) → HumanoidPose
```

**Status: scaffold and recorder.** What exists is the library's identity and its
one real edge, the [frozen diagnostic set](include/vrmAdapterVrchatOsc/Diagnostics.h),
the [recorded-packet format](include/vrmAdapterVrchatOsc/PacketCapture.h), the
[receiver seam](include/vrmAdapterVrchatOsc/UdpReceiver.h) onto the shared
transport, and [**`vrchat_osc_record`**](tools/vrchatOscRecord/README.md) — the
CLI that turns a sender aimed at this machine into a capture file.

**There is no decoder, and that is the milestone rather than a gap.** Nothing in
this library reads a byte of a datagram. See
[the plan](../../../docs/roadmap/osc-and-vrchat-trackers.md) §6 and VRC-0 for the
order, and the two sections below for why an adapter over a *published*
specification still records before it decodes, and why the humanoid solve is not
here.

## The specification is published, and the recorder still comes first

VRChat documents its OSC tracker surface, and Sony's help pages list
`VRChat (OSC)` as a mocopi transfer format and name VRChat's default port. That
is more than `vrmAdapterMocopi` had — its protocol is documented nowhere — and it
is still not enough to write a decoder from.

**A menu entry is not a packet shape.** What a product sends is a measurement,
and the fact that the *receiving* specification is public does not establish that
a sender implements all of it, or only it. A decoder written from the document
would be correct about VRChat and unfalsifiable about the sender, and the first
real session would arrive as a series of surprises attributed to whichever field
was read first.

So this adapter takes the order its native sibling was *forced* into, by choice:

```text
raw UDP capture → recorded corpus → address / type-tag / cadence inventory
                → decoder → canonical comparison
```

The inventory (VRC-1) is the input to the decoder's design, and until it exists
the decoder has no committed shape. What that buys is stated as a prediction to
be checked rather than a claim: the risk this adapter was written expecting is
that mocopi's `VRChat (OSC)` output is **not** the tracker subset anyone expects,
and the inventory is what will say so before a decoder has been built around the
assumption.

## A tracker source is not a pose source

`vrmAdapterVmc` and `vrmAdapterMocopi` both carry humanoid bone transforms: a
message names a bone, and mapping it onto `motion::HumanBone` is a lookup. This
wire carries **numbered tracker observations**, which are pre-IK, and a tracker
index is not a body role — the same index is a hip on one player and a knee on
another, decided by a calibration the receiving application performs.

Two consequences shape everything above:

- **This adapter stops at a tracker frame.** Turning trackers into humanoid
  semantics is a solve, and a solve written inside a vendor adapter is a second
  motion pipeline. It is a separate and generic boundary (VRC-5), and **no
  VRChat-shaped type enters `motionCore` under any outcome**: if the generic form
  is not clear, this adapter keeps its own and the contract stays unwritten.
- **The diagnostic set says things neither sibling's can.** A tracker can report
  half of itself, because position and rotation arrive on separate addresses; and
  a stream can be well-formed and unusable, because it has not been calibrated.
  `VRM_VRCHAT_OSC_TRACKER_PARTIAL` and `VRM_VRCHAT_OSC_CALIBRATION_REQUIRED` are
  states this wire has and those wires do not.

## What this adapter is *not* the second copy of

The census that preceded this directory
([§2](../../../docs/roadmap/osc-and-vrchat-trackers.md#2-the-duplication-census))
measured the two existing adapters with vendor identifiers erased: one
packet-capture implementation written twice, differing by six lines across 800,
and one UDP receiver written twice and drifted by 210. A third adapter's first
deliverable is a packet recorder, so those were extracted into
[`liveTransport`](../../../libs/liveTransport/README.md) **before** this
directory existed rather than after.

The result is visible in the sizes rather than in an argument:

| What | The siblings | Here |
| --- | --- | --- |
| The capture format | ~400 lines, twice | one magic string, four forwarding calls |
| The UDP receiver | ~550 lines, twice | a `switch` over two transport events |

What is left in this library is what a shared library may not hold: **a code
table**, and **the map from a transport event to one of its rows**. That split is
[WORKSPACE.md](../../../docs/architecture/WORKSPACE.md) §2's, and this adapter is
the first one written on the near side of it.

It also inherits, fixed, the four receiver defects a review found on 2026-08-11 —
a silently truncated oversize datagram, a large finite timeout mapped onto "wait
forever", an uninspected `revents`, and idle accounting charged to a call that
had received something. Being the third consumer of a library rather than the
third copy of a file is what that inheritance *is*.

## Edges

One, today: `liveTransport`.

WORKSPACE.md §2 permits an adapter three — `motionCore`, `motionRuntime` and
`liveTransport` — and the other two are what an adapter takes when it has
canonical values to produce. This one has none yet, so declaring them would claim
a dependency the library does not have, and
[`tests/test_vrm_adapter_vrchat_osc.cpp`](tests/test_vrm_adapter_vrchat_osc.cpp)
exercises the edge that is declared for exactly that reason.

One consequence worth knowing before reading a build log: **this adapter's test
binaries load no OpenUSD at all**, because Gf arrives through `motionCore` and
`motionCore` is not linked. Both siblings need OpenUSD's DLL directory on `PATH`
for `ctest`; this one does not, until a decoder produces a pose.

No adapter may depend on another, and here that rule guards something the other
two adapters' equivalent does not: `vrmAdapterVmc` holds the only OSC decoder in
this repository, and this adapter reads the same wire format. Reaching across
would *work*, which is what makes
[`tests/check_boundaries.py`](tests/check_boundaries.py) worth having rather than
a formality. The shared decoder with two consumers is
[§3.1](../../../docs/roadmap/osc-and-vrchat-trackers.md#31-libsosc--extract-after-the-second-consumer)'s,
and it is extracted after a real datagram has been decoded through the existing
one — measured first, reconciled second, moved third.

## Diagnostics

Ten codes, frozen in
[§8](../../../docs/roadmap/osc-and-vrchat-trackers.md#8-diagnostics) before this
directory existed and before anything here decodes a byte:

```text
VRM_VRCHAT_OSC_PACKET_MALFORMED     VRM_VRCHAT_OSC_UNSUPPORTED_ADDRESS
VRM_VRCHAT_OSC_ARGUMENT_MISMATCH    VRM_VRCHAT_OSC_TRACKER_ID_INVALID
VRM_VRCHAT_OSC_TRACKER_PARTIAL      VRM_VRCHAT_OSC_SOURCE_TIMEOUT
VRM_VRCHAT_OSC_SOURCE_RESTARTED     VRM_VRCHAT_OSC_COORDINATE_INVALID
VRM_VRCHAT_OSC_SOCKET_BIND_FAILED   VRM_VRCHAT_OSC_CALIBRATION_REQUIRED
```

Exactly one is fatal: a receiver that never bound has nothing to recover into.
`VRM_VRCHAT_OSC_UNSUPPORTED_ADDRESS` is *information* rather than a warning,
which is this set's one severity worth arguing about — VRChat's OSC surface is
far larger than the tracker subset read here, so traffic this adapter maps to
nothing is the ordinary case, and warning about it would train an operator to
ignore the warnings that mean something.

**Whose codes a *shared* OSC decoder will raise is still open.** Today
`DecodeOscPacket` lives in `vrmAdapterVmc` and raises `VRM_VMC_PACKET_MALFORMED`
for a failure that is about OSC and not about VMC. The transport ring answered
the same question for itself — a neutral event, mapped by each adapter onto its
frozen set — and that is precedent rather than decision: `VRM_VMC_*` has golden
tests over its formatted line, so whatever the decoder does has to leave those
strings standing. It is decided in the extraction (OSC-3), not here.

## Layout

```text
include/vrmAdapterVrchatOsc/   Diagnostics, the capture magic, the receiver seam
src/                           the code table, and the event → code map
tests/                         unit, format, socket, boundary
tests/corpus/                  empty by design until VRC-1 and VRC-2
tools/vrchatOscRecord/         the CLI
```

## Not in this adapter's boundary

Avatar Parameters, OSC eye tracking, OSCQuery discovery, an OSC **sender** or
router, two-way VRChat client integration, realtime display, and OpenExec. A
VRChat client is never a test dependency: every replay test completes with
nothing installed.
