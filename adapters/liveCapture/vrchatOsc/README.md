# vrmAdapterVrchatOsc

The third live input adapter: VRChat OSC tracking data in, and — eventually —
canonical humanoid motion out. It is the first adapter here whose input is not a
pose.

```text
UDP datagram → OSC decode → tracker semantics → tracking-space normalisation
             → tracker frame → (a generic humanoid solve) → HumanoidPose
```

**Status: recorder, and a measured inventory of what a real session contains**
(2026-08-30 —
[report 02](../../../docs/reports/motion/02-2026-08-30-vrchat-osc-address-inventory.md)).
What exists
is the library's identity and its two edges, the
[frozen diagnostic set](include/vrmAdapterVrchatOsc/Diagnostics.h), the
[recorded-packet format](include/vrmAdapterVrchatOsc/PacketCapture.h), the
[receiver seam](include/vrmAdapterVrchatOsc/UdpReceiver.h) onto the shared
transport, the [address inventory](include/vrmAdapterVrchatOsc/AddressInventory.h),
and [**`vrchat_osc_record`**](tools/vrchatOscRecord/README.md) — the CLI that
turns a sender aimed at this machine into a capture file and reads one back.

**There is no *semantic* decoder, and that is the milestone rather than a gap.**
Nothing here knows that `/tracking/trackers/1/position` is a tracker or that `1`
is an index. The inventory reads OSC's grammar — where an address ends, what its
type tags are — and counts what it finds; giving a row a meaning is VRC-2's. See
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

The inventory (VRC-1) is the input to the decoder's design, and until a session
has been through it the decoder has no committed shape. **A session has now been
through it** — 2026-08-30, six captures,
[report 02](../../../docs/reports/motion/02-2026-08-30-vrchat-osc-address-inventory.md).

The prediction that justified this order was checked and it held. The risk was
that mocopi's `VRChat (OSC)` output is **not** the tracker subset anyone expects,
and it is not: **three numbered trackers out of a surface of eight, plus a named
`head`** occupying the same path position the numbers do. A decoder written from
the specification would have read that segment as an integer, dropped the head,
and reported nothing wrong. The inventory carrying no list of addresses it
expects is what turned that into a row rather than into four absences.

What else it measured, each of which a decoder here now inherits as a fact:
every address is `,fff`, so a rotation is three floats and not a quaternion;
there are no bundles at all, so one datagram is one message and every frame
boundary is inferred; the eight arrive as a fixed cycle inside a median 0.053 ms;
and about a third of the frames never arrive, with the residual single-address
loss falling 96 % on one address, which is what
`VRM_VRCHAT_OSC_TRACKER_PARTIAL` is for.

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

Two: `liveTransport` and `osc`.

WORKSPACE.md §2 permits an adapter four — `motionCore`, `motionRuntime`,
`liveTransport` and `osc` — and the two core ones are what an adapter takes when
it has canonical values to produce. This one has none yet: an address and a type
tag are facts about a capture, not motion. Declaring them would claim a
dependency the library does not have.

One consequence worth knowing before reading a build log: **this adapter's test
binaries load no OpenUSD at all**, because Gf arrives through `motionCore` and
`motionCore` is not linked. Both siblings need OpenUSD's DLL directory on `PATH`
for `ctest`; this one does not.

That survived the decoder arriving, which the VRC-0 version of this paragraph
predicted it would not — it said "until a decoder produces a pose". `osc` links
nothing at all, not even a socket, so the link line grew and the closure did not.
The prediction was about a decoder that produces a *pose*, and it still holds for
that one.

No adapter may depend on another. Through VRC-0 that rule guarded something real
here: `vrmAdapterVmc` held the only OSC decoder in this repository and this
adapter reads the same wire format, so reaching across would have *worked*.
OSC-3 removed the temptation rather than the rule — the decoder is
[`libs/osc`](../../../libs/osc/README.md) and this adapter is the second consumer
it was extracted for
([§3.1](../../../docs/roadmap/osc-and-vrchat-trackers.md#31-libsosc--extract-after-the-second-consumer)).
[`tests/check_boundaries.py`](tests/check_boundaries.py) still refuses a sibling
include, because a sibling edge is forbidden by the contract and not by whether
it would pay.

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

**Whose codes a shared OSC decoder raises was settled by OSC-3, and the answer
is neither adapter's.** `libs/osc` refuses a datagram with a subject and a detail
and *no code at all* — not even a neutral event enum, which is where it differs
from the transport ring. That library's receiver raises two events a caller must
tell apart; a decoder makes one distinction, decodable or not. So each adapter
maps one refusal onto one of its own codes, and here that is
`VRM_VRCHAT_OSC_PACKET_MALFORMED`, raised from
[`src/AddressInventory.cpp`](src/AddressInventory.cpp) and nowhere else so far.

## Layout

```text
include/vrmAdapterVrchatOsc/   Diagnostics, the capture magic, the receiver seam,
                               the address inventory
src/                           the code table, the event → code map, and the
                               inventory (the first file here that reads a byte)
tests/                         unit, format, inventory, socket, boundary
tests/corpus/                  empty by design until VRC-1 and VRC-2
tools/vrchatOscRecord/         the CLI
```

## Not in this adapter's boundary

Avatar Parameters, OSC eye tracking, OSCQuery discovery, an OSC **sender** or
router, two-way VRChat client integration, realtime display, and OpenExec. A
VRChat client is never a test dependency: every replay test completes with
nothing installed.
