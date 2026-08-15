# vrmAdapterMocopi

The native live input adapter for one capture product: that product's own UDP
packets from the device's application, in; canonical humanoid motion, out. No
third-party sender application anywhere in the path.

```text
UDP datagram → packet decode → joint mapping → coordinate conversion
             → frame assembly → HumanoidPose → LiveCaptureSource
```

**Status: through the runtime bridge — the library is code-complete.** What
exists is the library's identity
and its two edges, the frozen diagnostic set, the recorded-packet format, the
**UDP receiver**, [**`mocopi_record`**](tools/mocopiRecord/README.md) — the CLI
that turns a source aimed at this machine into a capture file — the **packet
decoder** ([container](include/vrmAdapterMocopi/PacketChunk.h) and
[two packet kinds](include/vrmAdapterMocopi/MotionPacket.h), with a
[corpus](tests/corpus/README.md) behind them) — and now the
[**joint map and the basis change**](include/vrmAdapterMocopi/SkeletonMap.h),
which is the first layer here that knows a humanoid exists, then
[**frame assembly**](include/vrmAdapterMocopi/FrameAssembler.h) — the layer that
decides whether a datagram is a frame, whether it is complete, and whether the
source restarted — and finally the
[**live-source bridge**](include/vrmAdapterMocopi/LiveSource.h), where a frame
becomes a pose a consumer samples through the unchanged `motionRuntime`.

**What does not exist is a session that met a device.** Every layer above is
exercised, and by committed bytes that never met a sensor. Tracking state and
confidence have nothing to decode into — the measured grammar carries neither —
and reconnection, the opt-in hardware run, and the cross-source comparison of
§9.6 all need an operator rather than a commit.
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

**Handedness was the fourth, and it is settled as of 2026-08-12: the basis is
right-handed and +X is the body's left.** It was the last thing open, because a
mirrored basis satisfies every measurement above — a mirrored skeleton is
geometrically identical, so the two four-bone limbs off the root and the two off
the top of the torso stay legs and arms either way.

What settled it was not a new recording. The plan expected one: an asymmetric
motion whose side an operator could name. Two things were then measured, and the
second made the first moot.

- **The recorded sessions cannot label a side, and that is measured rather than
  assumed.** All four are bilateral. The arm-raise session raises *both* arms —
  the two hands rise 0.670 m and 0.650 m, tracking each other frame by frame —
  and the head-turn session turns both ways. No amount of remembering helps.
- **The answer was already in the repository.**
  [`mocopi-mobile-bvh-default-v1`](../../../profiles/motion/mocopi-mobile-bvh-default-v1.yaml)
  was measured on 2026-08-04 from *the same application's* BVH export and states
  `handedness: right`, `+Y`, `+Z`, and a side for every joint. The only question
  left was whether the application mirrors between the file it writes and the
  datagrams it sends — and it does not: **all 27 rest offsets agree sign for
  sign, worst component difference 4.4e-7 m**, once the centimetre/metre factor
  is removed. The hip height is 95.9893 cm there and 0.95989 m here. Two
  recordings a week apart, two transports sharing no code, one skeleton.

So the joint identities transfer in `bnid` order: 11–14 left arm, 15–18 right,
19–22 left leg, 23–26 right.

**The decoder still names no bone**, and that has not changed: which canonical
bone a `bnid` is belongs to the joint map, and the basis change into a target's
convention to the coordinate conversion beside it, exactly where the sibling
adapter draws the line. What changed is that both now have a measured input
rather than an open question — and, as of 2026-08-12, that both exist.

One thing not to over-read: this says the two paths agree about the *rest pose*,
not about the motion. That is the
[cross-source comparison](../../../docs/roadmap/adapters-mocopi-vmc-ardy.md) of
§9.6, on a single session observed both ways, and it is still owed.

## The map is where an id becomes a bone

[`SkeletonMap.h`](include/vrmAdapterMocopi/SkeletonMap.h) carries the reasoning;
four things in it are decisions rather than details.

**A bone id is a position, not a name.** The sibling maps the string
`LeftUpperArm`, which means the same thing whoever sent it. Here `bnid` 12 means
"the twelfth joint of whatever rig this session is sending", so the map is built
from a **skeleton packet** and the rig it declares is checked against the
measured parent column before any id is trusted. A rig that disagrees gets no map
rather than a map that reads the wrong joint; a *longer* rig keeps its measured
joints and reports the rest once for the session. That is the same refusal the
recorded track's profile makes with `unmappedJoints: refuse`, reached from the
other direction.

**The basis change is the identity, and that is a measurement.** Canonical motion
is right-handed, +Y up, +Z forward, metres; so is this device, with +X the body's
left. Nothing is permuted, mirrored or scaled — the one thing that does change is
the quaternion's component order, scalar-last on the wire and scalar-first in
`GfQuatf`, and a reorder is not a rotation. It is a named function anyway,
because "nothing is converted" is a claim that can be wrong, and because the
sibling's answer to the same question is *not* the identity.

**Twenty-two of twenty-seven joints carry a bone, and the other five are not
dropped.** A joint between two mapped ones is on the path between them, so a
bound bone's rotation is the composition from just below its nearest bound
ancestor down to itself — the path rule, stated in
[MOTION_CONTRACT.md](../../../docs/design/MOTION_CONTRACT.md) and now implemented
twice. The two tracks may not share the table (§2.1), so their agreement is
enforced from outside instead: `scripts/check_docs.py` reads the adapter's table,
the recorded profile, and the committed BVH export the correspondence was
measured on, and fails when the three stop describing one rig.

**One joint translates, and it is the hips.** The sibling has two candidate root
translations and cannot compose them; natively there is no second channel, and in
207,064 measured bone-frames every other translation restated its rest offset bit
for bit. So the body's placement is the hips joint's own translation — reported
as that, and deliberately not as a `motion::RootMotion`, because whether it *is*
root motion is a policy question this release still owes a record of.

A native session also brings something a relay cannot: a **real rest pose**. A
skeleton packet is one, repeated about every 3.5 s, so nothing here has to
manufacture a source rest from the first frame it happens to see — which is
exactly what the sibling's header says an adapter must not do and cannot avoid.

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
aimed at this machine does not lose it to the test suite. Three names in this
adapter bind a socket and a lane that forbids binds must exclude all three:
`vrmAdapterMocopi_udpReceiver`, `vrmAdapterMocopi_udpReceiverTruncation`, and
`vrmAdapterMocopi_loopbackCorpus` below — which reads like a corpus pass and
behaves like a socket test, so it is the one such a list would miss.

And once every layer above it existed, the same binary took on the one claim the
inverted order left open: `vrmAdapterMocopi_loopbackCorpus` sends all nine
committed captures — 54 datagrams — to a bound port, reads them back off it, and
requires the frames, the sampled poses, the diagnostics and all three tallies to
be **identical** to what the same bytes produce with no socket in the path. Every
other name here reaches the decoder from a file; this is the only place the two
meet, and it is what says the receiver added nothing and lost nothing. The
comparison needs no clock exemption, unlike the sibling's, because a receive time
reaches nothing on this protocol — every frame carries the sender's own `time`.

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
restart mid-frame — which is to say, only the second can test a decoder. Nine of
them now do, in [`tests/corpus/`](tests/corpus/README.md); the two
`malformed-*` captures, `frame-loss-60hz` and `session-restart-60hz` are the four
that make the sentence above concrete.

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

**Six of the nine are now paid for by a caller**, which is how a freeze earns its
keep. The receiver raises `SOCKET_BIND_FAILED` and `DEVICE_UNAVAILABLE`; the
decoder raises `PACKET_MALFORMED`, `NON_FINITE_TRANSFORM` and `TIMESTAMP_INVALID`
and no others; the joint map raises `UNSUPPORTED_JOINT` — for a rig it cannot
read, never for the five segments it deliberately maps to no bone, and never per
frame. The remaining three are one layer up or not on the wire:
`FRAME_INCOMPLETE` and `SOURCE_RESTARTED` need something that holds more than one
packet at a time, and `TRACKING_LOST` needs a field this project has not yet
found. That last one is worth naming as an open question rather than a to-do: the
measured grammar has 27 bone records and no per-joint confidence or state
anywhere in them, so either it lives in one of the two unidentified fields
(`sndf/ipad`, `fram/tmcd`) or this application version does not send it. Nothing
here guesses which.

## Building and testing

Composed with the rest of the workspace:

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=<usd-install>
cmake --build build --config Release
# Both halves: the library's sixteen names and the CLI's three. `-R vrmAdapterMocopi`
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
