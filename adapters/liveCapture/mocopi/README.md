# vrmAdapterMocopi

The native live input adapter for one capture product: that product's own UDP
packets from the device's application, in; canonical humanoid motion, out. No
third-party sender application anywhere in the path.

```text
UDP datagram → packet decode → joint mapping → coordinate conversion
             → frame assembly → HumanoidPose → LiveCaptureSource
```

**Status: transport, and the packet decoder above it.** What exists is the
library's identity and its two edges, the frozen diagnostic set, the
recorded-packet format, the **UDP receiver**,
[**`mocopi_record`**](tools/mocopiRecord/README.md) — the CLI that turns a source
aimed at this machine into a capture file — and now the **packet decoder**: the
[container](include/vrmAdapterMocopi/PacketChunk.h) and the
[two packet kinds](include/vrmAdapterMocopi/MotionPacket.h), with a
[corpus](tests/corpus/README.md) behind them. What does not exist is anything
that knows what the numbers *mean*: no joint map, no basis change, no frame
assembly, no live-source bridge. One of those is blocked on a measurement rather
than on a commit, and
[the section below](#the-decoder-stops-where-the-measurement-does) says which.
See [the plan](../../../docs/roadmap/adapters-mocopi-vmc-ardy.md) §6 and
Milestone D for the implementation order, and
[below](#transport-arrives-first-here-and-that-is-the-finding) for why this
adapter's order is the reverse of its sibling's.

## The format is not documented, so it was measured instead

The transport is stated publicly and the payload is not. What the vendor
documents is that the application sends motion data over **UDP** to a PC on
**port 12351** by default, that the address must be **IPv4** (`localhost` and
IPv6 are unsupported), and that the data is **not encrypted** because the
product assumes a local network.

There is no prose specification of the packet structure anywhere in that
documentation. The authoritative artifacts are the vendor's own **Apache-2.0**
sources — a serialization library and a set of receiver plugins — and the
`BVH Sender` application, which transmits a BVH file over the same UDP format.

Two consequences, and they are the whole reason the decoder is this adapter's
fourth commit rather than its first:

- **A decoder written from a remembered format is a guess wearing the shape of
  progress.** That is the failure mode the recorded-motion plan was built around
  ([BVH-0](../../../docs/roadmap/recorded-motion-sources.md#9-milestones)), and
  it applies harder here: a BVH file that is misread produces a visibly wrong
  figure, where a packet field read at the wrong offset produces plausible
  numbers.
- **So the grammar was measured, not recalled.** Five real device sessions, 8000
  datagrams, four distinct motions, walked until the arithmetic *closed* — every
  chunk consuming `length + 8` bytes and landing exactly on the end of its
  container, at every level, on every datagram. A container guess that closes
  8000 times over four different motions is a measurement; one that does not is
  a wrong guess, and the check was written to be able to say so. The grammar and
  the population behind each claim are on
  [`MotionPacket.h`](include/vrmAdapterMocopi/MotionPacket.h).

The recorded sessions themselves are **not committable** — they hold a real
person's motion, and a skeleton packet is a body measurement — so what is
committed is a corpus written *from* the measurement, and
[its README](tests/corpus/README.md) is blunt about what that can and cannot
prove. `BVH Sender` remains the path to bytes whose **encoding** is the vendor's
own while their content is ours, and it is still the thing that can refute the
grammar rather than agree with it.

So the corpus arrives before the decoder, the capture format arrives before the
corpus — and the **receiver** arrives before either, because a corpus that can
only be obtained from a socket needs a socket.

## The decoder stops where the measurement does

The decoder reads the container, the two packet kinds, both clocks, the frame
counter, the parent column, and seven floats per joint. It does not name an axis,
and that is the one deliberate hole in it.

Almost everything about the basis is measured. The translations are **metres**,
the up axis is **+Y** and forward is **+Z**, all three from the rest pose rather
than from a document: the root sits 0.96 up, which is a hip height in metres and
nothing else; the upper-arm and forearm offsets are 0.30 and 0.25; the toe sits
forward of the ankle. The quaternion is **scalar-last**, and its imaginary parts
are in the same component order as the translation — which is also measured, not
assumed, because the rest pose holds the arms along ±X, a standing frame has them
hanging down, and the component carrying that ~85° rotation is the one a rotation
from +X to −Y must turn about.

**Handedness is not.** A mirrored basis satisfies every measurement above,
because a mirrored skeleton is geometrically identical — the two four-bone limbs
off the root and the two off the top of the torso stay legs and arms either way.
Settling it takes an asymmetric motion whose side is *labelled*, which is an
operator's session and not a commit. Until then nothing here says left or right:
the corpus describes arm bones by their side of the +X axis and by their bone id,
and the naming stops short of x/y/z in the decoder's own structs. A layer that
published a guess about which arm is which would be putting it in the one place
hardest to notice later — which is why resolving it belongs to
`MocopiCoordinateConverter`, one layer up, where the sibling adapter draws the
same line for the same reason.

## What this is, structurally

A plain static CMake library with an `openstrata.library.yaml`, exactly like
`motionRuntime` and `vrmRetarget` — **not** a plugin bundle. It registers
nothing with OpenUSD and ships no `plugInfo.json`, because
[WORKSPACE.md §2](../../../docs/architecture/WORKSPACE.md) keeps it away from
`vrmSchema`, from every file-format bundle, and from OpenExec. It has exactly
two dependencies, and they are the two its manifest declares:

```text
vrmAdapterMocopi -> motionCore, motionRuntime
```

`tests/check_boundaries.py` is what makes that a fact rather than an intention.
It fails on a plugin manifest anywhere in the tree, on a stage/registration/exec
API in `include/` or `src/`, on a mention of the sibling adapter or a plugin
bundle, on a `target_link_libraries` naming anything outside its allowlist, and
on a binary whose imports leave the OpenUSD value-type layer.

That allowlist is the two libraries above plus `ws2_32`, which is not a
dependency direction — WORKSPACE.md §2 constrains which *workspace* libraries an
adapter may reach, and motion policy §8.2 puts the socket inside the adapter
deliberately. It arrived with the receiver rather than being reserved for it,
which is the arrangement the scaffold commit asked for. `Threads::Threads` is
still absent and is the half worth reading: the sibling links it for a datagram
queue's mutex, this adapter has no queue, and adding the name "because a
receiver usually needs one" is exactly the reservation the allowlist exists to
catch.

It is also the *only* enforcement: `ost` 0.21.0 does not discover a library
under `adapters/`, so the workspace graph gate validates none of these edges and
still reports "valid"
([report 34](../../../docs/reports/ost/34-2026-07-29-v0.21.0-adapter-library-discovery-gap.md)).

## The sibling adapter is not a dependency, and never becomes one

The other live adapter decodes a generic protocol that this same product can be
relayed through, which makes reaching across it the *convenient* mistake rather
than an implausible one. Adapter plan §2.1 forbids it, and the reason is not
tidiness: a native decoder that borrowed a relay's decoder would inherit the
relay's assumptions about framing, clocks and bone names, and the entire point
of building this path is to measure what those assumptions cost
([§6](../../../docs/roadmap/adapters-mocopi-vmc-ardy.md)). The two boundary
scripts refuse each other's names, so the pair is symmetric.

The two paths do meet once, deliberately: the **same physical session** observed
live over UDP and exported to a file can be compared at the canonical layer,
which is the release's distinguishing check and belongs to nothing lower than a
test that holds both.

## What it is not allowed to do

Target-skeleton discovery, retargeting, rest-pose correction, its own
interpolation, its own smoothing, `UsdSkelAnimation` authoring, stage authoring,
or a dependency on a sibling adapter. Every one of those already exists once in
this repository; a second copy inside an adapter is a forked pipeline that stays
invisible until two inputs disagree about the same avatar.

Two more are specific to a *native* adapter, because this is exactly where the
temptation appears. It does not grow buffering, interpolation or filtering — the
bridge into the runtime is a bridge, and one that acquires those has become a
second motion runtime. And it does not widen the canonical contract:
product-specific metadata is isolated as provenance, never as a new value type,
so its output meets the **same** contract as the sibling's rather than a
superset of it.

## Transport arrives *first* here, and that is the finding

The sibling adapter built its receiver last, and this one was planned the same
way: recorded-packet decoder → semantic mapping → frame assembly → live-source
bridge → thin UDP receiver. That order exists so every test below the transport
runs from committed bytes, and it was right there because the bytes existed —
the VMC Protocol is published, so a corpus could be *written* before a socket
was ever opened.

This protocol is not published, so there is nothing to write a corpus from and
exactly one way to obtain one without guessing: receive it from something that
already speaks the format. The transport is therefore not the last layer that
needs writing but the only one that *can* be, and the rule it appears to break
is the rule that sent it here — "the fixture-driven tests stay deterministic" is
a statement about the layers that decode, and this one decodes nothing.

It costs nothing the original order was protecting, either.
`vrmAdapterMocopi_udpReceiver` needs no device, no sender application and no
fixture: it binds loopback on an OS-assigned port and sends itself a dozen bytes.
It is its own CTest name so a runner that forbids sockets excludes a name rather
than a claim, and it never touches port 12351 — a developer with a real device
aimed at this machine does not lose it to the test suite.

What the receiver may do is bounded by what it knows. It hands back every
datagram exactly as it arrived, including the ones a decoder would refuse,
because a receiver that filtered its own input would make a corpus a description
of what the receiver let through rather than of what a source sent. It raises
two of the nine frozen codes and no others: `VRM_MOCOPI_SOCKET_BIND_FAILED`,
and `VRM_MOCOPI_DEVICE_UNAVAILABLE` against a silence threshold **the caller
states** — how long a device may reasonably take to start is a property of the
session, not of the socket, so there is no default and no threshold means no
code.

## The recorder

[`tools/mocopiRecord`](tools/mocopiRecord/README.md) is the CLI, and it is the
consumer the receiver above was waiting for: `mocopi_record --output` turns a
source aimed at this port into a capture file, and `--inspect` reads one back
with no socket at all. It ships in the same artifact as the library
([WORKSPACE.md §5](../../../docs/architecture/WORKSPACE.md)) and links the
adapter and nothing else, which is less than §2 permits a tool — an adapter's
CLI may drive `vrmRetarget` and author a stage, and this one cannot usefully do
either, because there is no decoder for a retarget to act on.

It decodes nothing, so its report is about the datagram *envelope*: the counts,
the peers, the arrival rate on the receive clock, a census of distinct payload
lengths, and the leading bytes every datagram shares. Those last two are the
first sentences about this protocol anything here has been able to say, and the
line they stay on the right side of is argued in
[`SessionReport.h`](tools/mocopiRecord/src/SessionReport.h). It is also where
`--silence-timeout` states the threshold `VRM_MOCOPI_DEVICE_UNAVAILABLE` has no
default for, and where the vendor's IPv4-only and no-`localhost` statements
become warnings that fire *before* the first datagram — the receiver refuses
neither, on the grounds that a socket should not invent a restriction on itself
out of a product's documentation.

## Recorded input

`mocopi-packet-capture` v1 — spec on
[`PacketCapture.h`](include/vrmAdapterMocopi/PacketCapture.h) — is the other
half of the arrangement above: the receiver turns a source into datagrams, and
this is what keeps them. The datagrams a session delivered, verbatim, with the
instant each arrived. Line-oriented text, so a fixture diffs; hex with an ASCII
gutter, so a binary protocol's field tags are legible without a decoder ring:

```text
!mocopi-packet-capture 1
sender example.synthetic
device example.synthetic
sourceId neutral-standing-01
listen 0.0.0.0:12351

d 0.000000 16
  6e 6f 74 2d 61 2d 70 61 63 6b 65 74 00 01 80 ff  |not-a-packet....|
```

It is not a `motion-capture-trace`, and the difference is the adapter's two
ends: a trace records what an adapter *produced*, a capture what it was *given*.
Only the second can represent a truncated datagram, a duplicate delivery, or a
restart mid-frame — which is to say, only the second can test a decoder. Seven of
them now do, in [`tests/corpus/`](tests/corpus/README.md); the two
`malformed-*` captures and `frame-loss-60hz` are the three that make the sentence
above concrete.

**It is a second format rather than the sibling's, and that is a decision.** The
header file argues it in full; the short version is that reaching the sibling's
header is a forbidden edge, `motionRuntime` is the wrong home for a transport
artifact, and two magic lines mean a capture of one protocol handed to the other
protocol's decoder fails at line 1 with a clear message instead of at the first
field with a malformed-packet diagnostic blamed on a source that never sent it.
This repeats a shape rather than sharing code, which is the same answer the
semantic clip writer already gave here; what would change it is a *third*
recorder, at which point the shape is a library and its home is a boundary
question argued in its own change.

`device` is the one header key the sibling format does not carry. The native
path's claim is that it keeps device and sensor state a relay drops, and a
capture that cannot say which device produced it cannot support that claim
later. Like `sender`, it is provenance only — nothing in the decode path may
branch on either.

## Diagnostics

Nine codes, frozen in `include/vrmAdapterMocopi/Diagnostics.h` — and frozen on
2026-08-03, two days before this directory existed, so that the set describes
the protocol's failure modes rather than whichever bug was chased first:

```text
VRM_MOCOPI_SOCKET_BIND_FAILED   VRM_MOCOPI_TRACKING_LOST
VRM_MOCOPI_DEVICE_UNAVAILABLE   VRM_MOCOPI_TIMESTAMP_INVALID
VRM_MOCOPI_UNSUPPORTED_JOINT    VRM_MOCOPI_SOURCE_RESTARTED
VRM_MOCOPI_PACKET_MALFORMED     VRM_MOCOPI_FRAME_INCOMPLETE
VRM_MOCOPI_NON_FINITE_TRANSFORM
```

`VRM_MOTION_*` is the canonical layer's namespace and `VRM_VMC_*` is the other
adapter's; neither is this one's. Exactly one code is non-recoverable — a
receiver that never bound has nothing to recover into. Two that look fatal and
are not: a device that is **not there yet** is the ordinary state of a receiver
bound before the operator started the application, and **tracking loss** is the
device reporting on itself accurately, which is a phenomenon a session recovers
from rather than a defect.

**Five of the nine are now paid for by a caller**, which is how a freeze earns
its keep. The receiver raises `SOCKET_BIND_FAILED` and `DEVICE_UNAVAILABLE`; the
decoder raises `PACKET_MALFORMED`, `NON_FINITE_TRANSFORM` and
`TIMESTAMP_INVALID` and no others. The remaining four are all one layer up —
`UNSUPPORTED_JOINT` needs a joint map, `FRAME_INCOMPLETE` and `SOURCE_RESTARTED`
need something that holds more than one packet at a time, and `TRACKING_LOST`
needs a field this project has not yet found on the wire. That last one is worth
naming as an open question rather than a to-do: the measured grammar has 27 bone
records and no per-joint confidence or state anywhere in them, so either it lives
in one of the two unidentified fields (`sndf/ipad`, `fram/tmcd`) or this
application version does not send it. Nothing here guesses which.

## Building and testing

Composed with the rest of the workspace:

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=<usd-install>
cmake --build build --config Release
# Both halves: the library's nine names and the CLI's three. `-R vrmAdapterMocopi`
# alone silently misses the tool, whose names begin with `mocopi_record`.
ctest --test-dir build -R "vrmAdapterMocopi|mocopi_record"
```

Or through the runtime `ost` resolves for the workspace:

```sh
ost build && ost test
```

Standalone — this directory is its own CMake project, resolving `motionCore` and
`motionRuntime` as installed packages rather than in-tree targets, and building
the CLI along with the library because that is the configuration the adapter's
artifact would be built from:

```sh
cmake -S adapters/liveCapture/mocopi -B build/mocopi \
      -DCMAKE_PREFIX_PATH="<usd-install>;<workspace-prefix>"
cmake --build build/mocopi
```

`ost plugin build` is not the standalone route here: it takes a *bundle*
directory and refuses anything without an `openstrata.plugin.yaml`, which an
adapter does not have and must not grow.
