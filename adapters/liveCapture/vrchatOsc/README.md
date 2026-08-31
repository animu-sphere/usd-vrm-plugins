# vrmAdapterVrchatOsc

The third live input adapter: VRChat OSC tracking data in, and — eventually —
canonical humanoid motion out. It is the first adapter here whose input is not a
pose.

```text
UDP datagram → OSC decode → tracker semantics → tracking-space normalisation
             → tracker frame → (a generic humanoid solve) → HumanoidPose
```

**Status: a recorder, a measured inventory, a tracker decoder and a measured
tracking space** (VRC-3, 2026-08-30). What exists is the library's identity and
its three edges, the
[frozen diagnostic set](include/vrmAdapterVrchatOsc/Diagnostics.h), the
[recorded-packet format](include/vrmAdapterVrchatOsc/PacketCapture.h), the
[receiver seam](include/vrmAdapterVrchatOsc/UdpReceiver.h) onto the shared
transport, the [address inventory](include/vrmAdapterVrchatOsc/AddressInventory.h),
the [tracker decoder](include/vrmAdapterVrchatOsc/TrackerMessage.h),
[the change of basis](include/vrmAdapterVrchatOsc/TrackingSpace.h) measured from
a labelled session,
[the generated corpus](tests/corpus/generated/README.md) it replays, and
[**`vrchat_osc_record`**](tools/vrchatOscRecord/README.md) — the CLI that turns a
sender aimed at this machine into a capture file and reads one back.

**What a decoded message is, and what it deliberately is not.** A known address
becomes a tracker identity, a channel and three floats *verbatim* — no basis
change, no unit, no normalisation, because a documented tracking space is a
hypothesis until a recorded rest pose agrees with it. It does now:
[`TrackingSpace.h`](include/vrmAdapterVrchatOsc/TrackingSpace.h) is where the
conversion lives, one layer up, and
[report 03](../../../docs/reports/motion/03-2026-08-30-vrchat-osc-tracking-space.md)
is the measurement. It is a
`TrackerMessage` rather than the plan's `TrackerSample` because position and
rotation arrive in **separate datagrams**: a message decoder that returned a
sample would default the other half, and a defaulted rotation of (0, 0, 0) is
bit-for-bit what a tracker at rest reports. Assembling the two, and raising
`VRM_VRCHAT_OSC_TRACKER_PARTIAL` when only one arrives, is VRC-4's — a single
message is always partial, so a layer that reported it would warn about once a
datagram forever.

**No humanoid is resolved anywhere here**, and none will be: a tracker index is
not a body role, assignment is a generic policy outside this adapter, and the
solve is the motion layer's. See
[the plan](../../../docs/roadmap/osc-and-vrchat-trackers.md) §5 and §5.1, and the
two sections below for why an adapter over a *published* specification still
records before it decodes.

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

- **This adapter stops at a tracker observation, and then at a tracker frame.**
  Turning trackers into humanoid semantics is a solve, and a solve written
  inside a vendor adapter is a second motion pipeline. It is a separate and generic boundary (VRC-5), and **no
  VRChat-shaped type enters `motionCore` under any outcome**: if the generic form
  is not clear, this adapter keeps its own and the contract stays unwritten.
- **The diagnostic set says things neither sibling's can.** A tracker can report
  half of itself, because position and rotation arrive on separate addresses; and
  a stream can be well-formed and unusable, because it has not been calibrated.
  `VRM_VRCHAT_OSC_TRACKER_PARTIAL` and `VRM_VRCHAT_OSC_CALIBRATION_REQUIRED` are
  states this wire has and those wires do not.

## Where a frame begins, on a wire with no clock

VMC marks a frame with a clock message. This wire sends three floats per
address and nothing else, so the boundary is a **measurement** rather than a
convention: a frame here is a burst of eight datagrams inside a median
0.053 ms, with ~17 ms between bursts
([report 02](../../../docs/reports/motion/02-2026-08-30-vrchat-osc-address-inventory.md) §2).

Two rules cut it, and having two is the point — they fail differently:

| Rule | What it needs | What it survives |
| --- | --- | --- |
| a repeated tracker and channel closes the frame | no clock at all | loss: a frame missing one address is still closed by the next cycle |
| a datagram past the 5 ms window closes the frame | a receive clock | a sender whose next frame repeats no address the last one carried |

On the recorded sender the window gets there first and the two produce the
same frames, which `vrmAdapterVrchatOsc_frameAssembler` measures by running
one stream twice — window on, window off — and comparing sample for sample.

**A silence is not a restart.** This wire marks a restart with a new ephemeral
source port and with nothing else, so a peer that changes is a session
boundary and a gap of any length is `SOURCE_TIMEOUT` and no more. A caller
that supplies no peer never sees a restart, deliberately — and that this is
testable from a *file* at all is new: the capture format grew a per-record
peer on 2026-08-30 because the only marker this wire has did not survive into
one.

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

Three: `motionCore`, `liveTransport` and `osc`.

WORKSPACE.md §2 permits an adapter four, and the fourth — `motionRuntime` — is
what an adapter takes when it produces a **pose**. This one produces none: a
tracker observation is pre-IK, and the solve that makes it a pose is generic and
lives in `libs/motionTracking`. Declaring it would claim a dependency the library
does not have.

**Its CLI has two edges this library may not have**, and that split is §2's
rather than an accident of layout: `tools/vrchatOscRecord` links `motionTracking`
to solve and `motionRuntime` to write the trace. A *tool* may, because an
assignment is an operator's statement about a rig; a library may not, because a
decoder that resolved one would have invented a calibration and hidden it inside
itself. `adapters/* -> motionTracking` is a refused source token in
[`tests/check_boundaries.py`](tests/check_boundaries.py) for that reason, and it
is the one name on that list with no link line to fail on — this package is enums
and a policy over them, so an include and a `TrackerRegion` would compile.

`motionCore` arrived with VRC-3, which is the first code here that produces a
canonical value — the sender's axes into the canonical basis, which is a
`GfVec3f` and a `GfQuatf`. **The edge is taken for the value types**, and no
`motion::` type is named in the library yet: §2 gives an adapter `motionCore`
and does not give it `pxr`, so Gf arrives through the library whose contract
already carries it.

One consequence worth knowing before reading a build log: **this adapter's test
binaries now load OpenUSD**, so `ctest` needs Gf's DLL directory on `PATH` on
Windows exactly as both siblings do — `tests/CMakeLists.txt` puts it there, and
without it a test exits `0xC0000135` rather than failing.

That is the transition the two previous versions of this paragraph predicted and
neither produced. VRC-0's said the closure would grow when a decoder arrived;
VRC-2's decoder arrived and it did not, because `osc` links nothing at all, not
even a socket. Producing a canonical value is what does it.

**The boundary check needed one change to notice**, and it was found in review
rather than by the check. A static archive contributes only the objects a binary
references, so `vrmAdapterVrchatOsc_tests` — which names nothing in
`TrackingSpace.cpp` — still imports no OpenUSD, and the gate pointed at it would
have passed whatever that file reached for. It now inspects the conversion's own
suite, which is the binary linking the widest layer this adapter touches;
`tests/CMakeLists.txt` carries the rule for the next file that reaches a new
one. With that fixed the value-type allowlist — `arch`, `boost`, `gf`, `python`,
`tf`, `vt` — is the check it was written as, and the closure grew *inside* it.

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
[`src/AddressInventory.cpp`](src/AddressInventory.cpp) and
[`src/TrackerMessage.cpp`](src/TrackerMessage.cpp).

**Seven of the ten are raised today**: five from the decode path — malformed,
unsupported, argument mismatch, bad tracker id, bad coordinate — and two from
[`src/UdpReceiver.cpp`](src/UdpReceiver.cpp), which has raised `SOURCE_TIMEOUT`
and `SOCKET_BIND_FAILED` since VRC-0 because a receiver had them before a
decoder existed.

The three that are not are `TRACKER_PARTIAL`, `SOURCE_RESTARTED` and
`CALIBRATION_REQUIRED`, and what they have in common is the argument for
freezing a code set before writing a decoder. Two need a layer that remembers
the previous frame, which is VRC-4's. The third has **no recorded behaviour
behind it at all** — the application was calibrated before every take of the
2026-08-30 session, so nothing here has ever seen an uncalibrated stream — and it
stays frozen and unraised on the same terms `VRM_MOCOPI_TRACKING_LOST` did. A
set written after the decoder would contain none of the three.

## Layout

```text
include/vrmAdapterVrchatOsc/   Diagnostics, the capture magic, the receiver seam,
                               the address inventory, the tracker decoder,
                               the basis, the frame
src/                           the code table, the event → code map, the
                               inventory (the first file here that reads a byte),
                               the decoder (the first that reads a meaning), the
                               conversion (the first that produces a canonical
                               value) and the assembler (the first that decides)
tests/                         unit, format, inventory, tracker, basis, frame,
                               socket, boundary
tests/corpus/generated/        fifteen captures, written from the measured session
tests/corpus/recorded/         a manifest and no bytes, by policy
tools/generate_packets.py      what writes the generated half
tools/vrchatOscRecord/         the CLI
```

## Not in this adapter's boundary

Avatar Parameters, OSC eye tracking, OSCQuery discovery, an OSC **sender** or
router, two-way VRChat client integration, realtime display, and OpenExec. A
VRChat client is never a test dependency: every replay test completes with
nothing installed.
