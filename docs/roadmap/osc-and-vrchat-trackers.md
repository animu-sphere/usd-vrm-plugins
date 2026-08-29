# Shared OSC foundation and VRChat OSC Trackers input

The plan for the **third** live input: a VRChat OSC Trackers adapter, and the
extraction that stops it being the third copy of code this repository already
maintains twice.

This document holds **boundaries, order, and completion conditions** only. Where
it touches structure it defers: adapter identities, dependency directions, and
artifact naming are settled in
[architecture/WORKSPACE.md](../architecture/WORKSPACE.md) §1, §2, §5, and motion
semantics in
[design/MOTION_ARCHITECTURE_POLICY.md](../design/MOTION_ARCHITECTURE_POLICY.md)
§8, §14, §15. Items this plan needs from those contracts are in
[§10](#10-contract-changes-this-plan-requires) rather than asserted here. It
names no release version; that is the
[roadmap status table](README.md#status-at-a-glance)'s.

It is the sibling of [the live adapter plan](adapters-mocopi-vmc-ardy.md), not a
section of it, for the reason that plan and
[the recorded-file plan](recorded-motion-sources.md) are separate: this one is
about a *shared decoder* and a *tracker* source, and neither claim belongs
inside a document whose subject is one vendor's pose stream. Where the two meet
is [§11](#11-the-fourth-observation-of-one-session), on a single session observed
four ways.

Legend: 🚧 in progress · ⬜ not started · ⛔ blocked

## 1. What this adds, and the two halves it comes in

```text
VMC sender ──────── OSC/UDP ──> vrmAdapterVmc ───────┐
mocopi device ───── native UDP > vrmAdapterMocopi ───┼─> motionCore
mocopi / other ──── VRChat OSC > vrmAdapterVrchatOsc ┘        ↓
                                                        motionRuntime
                                                             ↓
                                                        vrmRetarget
```

The value is not that a capture product gains a second way to connect. It is
four things the current shape cannot buy:

1. The OSC decoder acquires a **second consumer**, which is the only way to find
   out whether it is protocol-neutral or merely believed to be.
2. **A tracker source and a pose source stop being the same word.** VMC and
   mocopi both carry humanoid bone transforms. VRChat OSC carries tracker
   observations, which are pre-IK, and the difference has to be visible in the
   architecture rather than absorbed by an adapter ([§5](#5-a-tracker-source-is-not-a-pose-source)).
3. One physical session becomes observable on a **fourth** independent surface,
   through a protocol that is a published specification rather than a measured
   grammar ([§11](#11-the-fourth-observation-of-one-session)).
4. It creates the boundary that later Avatar Parameters, eye tracking, and
   OSCQuery would otherwise have to be retrofitted through — while shipping none
   of them ([§12](#12-what-this-boundary-deliberately-excludes)).

The two halves are separately checkable and land in that order:

- **The OSC foundation** — the wire-format decoder becomes a library with two
  consumers, and the transport code the two existing adapters already duplicate
  stops being duplicated before a third adapter triples it ([§2](#2-the-duplication-census)–[§4](#4-what-libsosc-owns)).
- **The VRChat OSC adapter** — capture first, decoder second, tracking-space and
  frame policy third, the humanoid solve last ([§6](#6-the-adapter-capture-precedes-decoder)–[§9](#9-milestones)).

## 2. The duplication census

[The adapter plan §4](adapters-mocopi-vmc-ardy.md#4-layout) forbids an
`adapters/common/` until two adapters *demonstrably* duplicate code carrying no
vendor semantics. Two cases are already recorded — the receiver pair on
2026-08-11, in the mocopi header itself, and the two live-source bridges on
2026-08-15 in that plan's §4. This section is those findings widened to the
whole adapter pair, because a third adapter is exactly the event the rule was
written to be ready for — and because the answer changed the extraction order.

**Method, stated so the numbers can be argued with.** Each file pair was
normalised by erasing the vendor identifier (`vmc`/`Vmc`/`VMC` and
`mocopi`/`Mocopi`/`MOCOPI` → the same token), stripping `//` comments and blank
lines, and collapsing runs of whitespace; then diffed. The count is *changed
lines in that diff*, both sides together. Comments are excluded on purpose:
prose that explains the same code differently is not duplication, and prose that
explains *different* code identically is a worse problem than either. Measured
2026-08-23, against the tree at `f2f5511`.

| Pair | `vmc` | `mocopi` | changed | Reading |
| --- | ---: | ---: | ---: | --- |
| `PacketCapture.h` | 43 | 44 | **1** | one file, written twice |
| `PacketCapture.cpp` | 361 | 366 | **5** | one file, written twice |
| `LiveSource.h` | 91 | 93 | 12 | one class ([adapter plan §11](adapters-mocopi-vmc-ardy.md#11-contract-changes-this-plan-requires)) |
| `Diagnostics.h` | 53 | 54 | 15 | one vehicle, two code enums |
| `Diagnostics.cpp` | 125 | 126 | 15 | one vehicle, two code enums |
| `LiveSource.cpp` | 131 | 110 | 33 | one class, three stated differences |
| `TraceExport.cpp` | 43 | 55 | 26 | one hand-off, two payloads |
| `UdpReceiver.h` | 110 | 91 | 49 | one class, **drifted** |
| `UdpReceiver.cpp` | 551 | 548 | 161 | one class, **drifted** |
| `main.cpp` | 293 | 346 | 257 | two CLIs |
| `Options.cpp` | 419 | 460 | 287 | two CLIs |
| `SessionReport.cpp` | 349 | 317 | 360 | two protocol reports |
| `FrameAssembler.cpp` | 296 | 227 | 369 | two protocols — correctly duplicated |

The table separates cleanly, and the separation is the finding. `FrameAssembler`
differs by more lines than either copy contains, because assembling a frame *is*
the protocol; `PacketCapture` differs by six lines across 800, because recording
datagrams is not. Nothing in the middle needs a judgement call.

### 2.1 The divergences, which the tree already documents

`UdpReceiver` is the pair the census flags loudest, and none of this is a new
discovery — it is a prediction coming true on schedule.
[`mocopi/include/vrmAdapterMocopi/UdpReceiver.h`](../../adapters/liveCapture/mocopi/include/vrmAdapterMocopi/UdpReceiver.h)
says so in its own preamble: a review of that file on 2026-08-11 found **four
defects the sibling has identically, because they were copied along with
everything else**, corrected all four in the younger copy, and wrote down that
they remain in the older one. It then names the trigger for turning the
repetition into a library, and names it exactly:

> a **third** recorder — a third live adapter, or a tool that must drive both —
> is what turns the repetition into a library, and the boundary that library
> needs is argued in its own change rather than smuggled into this one.

This plan is that third adapter. The four defects, re-verified against the tree
on 2026-08-23 and all four still present in `vrmAdapterVmc` then. **All four are
closed as of 2026-08-24** ([OSC-1](#osc-1--merge-the-transport-divergences));
the table is kept as the census that justified the extraction, not as a
description of the tree:

| Defect | `vmc` | `mocopi` |
| --- | --- | --- |
| Oversize datagram | silently truncated on POSIX and handed back as whole — the source says *no truncation check on this path, and none is possible* | reads into `MaxDatagramBytes + 1`, so the overlong length is detectable and counted |
| A finite timeout ≥ 2147483647 ms | mapped onto `-1`, the sentinel meaning **wait forever** | clamped to the maximum finite wait |
| `poll` wake-up | `revents` never inspected | `(revents & POLLIN) == 0` checked, against a spin |
| Idle accounting | applied to a call that had just proved traffic was arriving | corrected |

Two further differences are not defects but widen the gap the same way: the
mocopi receiver has a silence timeout (`_ReportSilence`) that the VMC one has
no equivalent of, and its `Open`/`Close` reset session statistics, buffer and
configuration where the VMC pair leave all three standing.

**What has changed since 2026-08-11 is the count of instances, not the
argument.** The live-source bridges were the second case
([adapter plan §4](adapters-mocopi-vmc-ardy.md#4-layout), 2026-08-15) and the
first one found by a reviewer holding both files rather than by an author
writing the second. Two independent findings, two different discovery methods,
one cause. The rule "extract when duplication is demonstrated" was satisfied at
the first; what the second demonstrated is that the cost is now being paid in
defects rather than in line count.

### 2.2 What the census changes about the plan

The OSC extraction and the transport extraction are different problems and the
census is what tells them apart:

- **OSC has one consumer.** `mocopi` contains no OSC at all — it decodes a
  binary grammar of its own. So the OSC duplication does not exist yet, and
  extracting before the second consumer would settle the API on the only caller
  there is. The existing rule applies unchanged: **characterise, add the second
  consumer, then extract** ([§3.1](#31-libsosc--extract-after-the-second-consumer)).
- **Transport has two consumers and measured drift.** `PacketCapture` and
  `UdpReceiver` are already duplicated, already diverged, and a third adapter's
  *first* deliverable — record raw datagrams before writing any decoder — is
  precisely the code that would be copied a third time. Here the rule points the
  other way: **extract before the third consumer**, because the third consumer
  is the copy. That is not a new reading of the rule either — both files say so
  themselves, and name a third recorder as their own trigger
  ([§2.1](#21-the-divergences-which-the-tree-already-documents),
  [§3.2](#32-the-transport-ring--extract-before-the-third-consumer)).

An extraction order that did OSC first and transport never would have left the
tree with one shared decoder and three copies of the socket.

## 3. Where each shared ring can live

Two of the four rings have a home the contract already permits. Two do not, and
saying which is which is this section's whole job.

### 3.1 `libs/osc` — extract after the second consumer

The OSC decoder is already free of VMC semantics, and the measurement is small
enough to state exactly. Everything in
[`OscPacket.h`](../../adapters/liveCapture/vmc/include/vrmAdapterVmc/OscPacket.h)
— `OscBlob`, `OscArgument`, `OscMessage`, `OscPacket`, `DecodeOscPacket` —
decodes OSC 1.0 and nothing else; the header says so in its first line and the
code agrees. It is coupled to the adapter in exactly three places: the
namespace, the `VRMADAPTERVMC_API` export macro, and the diagnostic code it
raises on a refusal ([§8](#8-diagnostics)).

So the extraction is a move, a rename, and **one decision** — not a redesign.
That is also why it must still wait for the second consumer: a surface that
costs three edits to move is a surface nobody will fight to keep neutral, and
the only evidence that it *is* neutral is a caller that never says `VMC`.

**That caller was written and measured on 2026-08-29, and the three couplings
above are exactly the three it paid.** An address inventory of a VRChat OSC
session — VRC-1's tool, decoding through this decoder without moving it — needed
five VMC tokens in its source, and every one of them is the *name*: one include
path and four namespace qualifications. It needed the export macro on its
compile line, which is the second coupling. And its report on a VRChat session
printed `VRM_VMC_PACKET_MALFORMED`, which is the third, arriving as an
observation rather than as the prediction §8 wrote it down as. Nothing else
crossed: no VMC address literal, no bone name, no `VmcMessage`, no
`SkeletonMap`. The count that matters is the one that stayed at zero.

### 3.2 The transport ring — extract before the third consumer

`UdpReceiver`, the `<adapter>-packet-capture` file format, and the `Diagnostic`
struct that both raise. Both homes a reader reaches for first are already ruled
out, and neither ruling is new here.

**It cannot be `motionRuntime`**, and this is enforced rather than agreed:
[`libs/motionRuntime/tests/check_boundaries.py:94`](../../libs/motionRuntime/tests/check_boundaries.py)
refuses `winsock`, `sys/socket.h`, `asio`, `curl` and `websocket` in that
library's sources. The contract behind the check is
[WORKSPACE.md §2](../architecture/WORKSPACE.md): `motion_capture` is a member of
the aggregate product and links `motionRuntime`, and *no tool in the product
opens a transport or reads a wall clock, which is what makes every clip in this
repository reproducible by construction*. A socket in `motionRuntime` puts one
in the product's link closure — the property being protected, reintroduced
through the library rather than through the `--source vmc` flag that was already
refused.

**It cannot be `adapters/common/`.** [WORKSPACE.md §2](../architecture/WORKSPACE.md)
forbids an adapter → adapter edge, and a shared leaf between two leaves is that
edge wearing a hat.

**The candidate is therefore a new leaf library outside the product's closure**
— `libs/liveTransport`, holding the socket, the capture format, and the
diagnostic vehicle, depended on by adapter libraries and by nothing in the
aggregate. It was named here as a candidate and not as a decision: adding a
library changes WORKSPACE.md §1's identity table, §2's dependency directions,
and §5's artifact and aggregate-exclusion rules, and those land in their own
change before this plan depends on them ([§10](#10-contract-changes-this-plan-requires)).

**That change landed on 2026-08-24 and the candidate is now the contract's**
([§10](#10-contract-changes-this-plan-requires)). Two things it settled are
worth reading back here, because they narrow what OSC-2 may do. `liveTransport`
is contracted with an **empty edge set** — not "few edges", none — so the first
`motionCore` value the move drags along is a contract violation rather than a
design discussion. And its exclusion from the product is written on the second
of two clauses rather than the first: it is producer-neutral, as `motionSource`
and `motionBvh` are, and it is out because the product would acquire I/O.

Three questions the extraction had to answer rather than assume. **All three
are answered as of 2026-08-24** ([OSC-2](#osc-2--extract-the-transport-ring));
they are kept in their original form here because the reasoning that framed them
is what the answers were checked against:

- **Is the capture format one format or three?** The two on disk differ by one
  optional header key (`device`) and their magic string. A single
  `!live-packet-capture` with an open header vocabulary would make every
  committed fixture a rewrite; keeping per-adapter magic over shared reader and
  writer code would not. The committed corpora are the constraint, not the
  aesthetics.
- **What happens to the four defects?** Extraction merges them, and merging is a
  behaviour change to the VMC path — a receiver that clamps where it used to
  block, and that detects an oversize datagram where it used to hand one back as
  whole. That belongs in its own change with its own tests, ahead of the move,
  so that a file move never carries a fix inside it ([OSC-1](#osc-1--merge-the-transport-divergences)).
- **What does *not* come along?** `DatagramQueue` is `vrmAdapterVmc`'s and
  deliberately absent from `vrmAdapterMocopi`, whose header argues the case: a
  queue exists for a consumer that cannot poll often enough to keep a kernel
  receive buffer from overflowing, and *that case is real and this adapter has
  not met it*. A shared library must keep it opt-in for exactly that reason —
  the failure mode of an extraction is that everything one caller needed becomes
  everything every caller gets. A tracker recorder is a polling loop like the
  mocopi one, so the third consumer does not change this answer either.

### 3.3 The live-source bridge — already scheduled, and the third instance settles it

[Adapter plan §11](adapters-mocopi-vmc-ardy.md#11-contract-changes-this-plan-requires)
carries this one already: the shared bridge belongs beside `LiveCaptureSource`
in `motionRuntime` — it holds poses, not sockets, so §3.2's objection does not
apply to it — and moving it moves the restart-policy vocabulary into the motion
contract.

What this plan changes is the evidence. That item closes by observing that the
third instance which would settle the shape "may never arrive", because
`vrmAdapterArdy` is a generator and may have no datagram, no restart, and no
session clock. **A VRChat OSC adapter has all three.** The third instance
arrives here, ahead of the generator, and it arrives with a useful stress: a
tracker stream's restart is not a sender's restart, so the parameterisation is
tested by something that disagrees with both existing copies rather than by a
third that agrees.

### 3.4 What is not shared, and must not become shared

`FrameAssembler`, `SkeletonMap`, and each adapter's diagnostic **code enum**.
The census puts the first two further apart than their own length, and the third
is the one part of the diagnostic ring that is per-protocol by design
([adapter plan §8](adapters-mocopi-vmc-ardy.md#8-diagnostics)). A shared frame
assembler is how three protocols acquire one protocol's frame policy.

The record CLIs are the borderline case and the answer is no for now:
`Options.cpp` and `main.cpp` differ by more than half their lines, and what they
share is argument-parsing idiom rather than behaviour. A fourth tool is when to
re-measure.

## 4. What `libs/osc` owns

Owns: OSC packet decoding · bundle flattening and traversal · address
extraction · type-tag validation · argument access · malformed-packet
diagnostics as protocol-neutral events · a datagram-oriented decode API.

Does not own: `/VMC/...`, `/tracking/...`, or `/avatar/...` · VRM bone names ·
tracker roles · humanoid semantics · coordinate conversion · frame policy ·
socket configuration · anything a sender application is called.

```text
OSC wire format       = library
OSC address semantics = adapter
```

A CI boundary check enforces it, in the shape the repository already uses for
`vrmContainer` and `motionSource`: no `libs/osc` → any adapter, → `motionCore`,
or → `motionRuntime`; and no VMC or VRChat address literal anywhere in its
sources. The literal check is the cheap one and catches the realistic failure —
a decoder that "just knows" one address is special.

## 5. A tracker source is not a pose source

The distinction this adapter exists to make visible:

```text
VMC / mocopi            VRChat OSC Trackers
sender or device pose   tracker observations
        ↓                       ↓
bone transforms         calibration / assignment
        ↓                       ↓
HumanoidPose                   IK
                                ↓
                         HumanoidPose
```

VRChat's tracker addresses carry a numbered tracker's position and rotation.
**A tracker index is not a body role** — it is an index into whatever the user
calibrated, and the mapping from one to the other is a solve, not a lookup. An
adapter that maps `/tracking/trackers/1/*` onto `HumanBone::hips` has invented a
calibration and hidden it in a decoder.

So the adapter's intermediate is an observation, not a pose:

```text
TrackerSample { trackerId, position, rotation, receiveTimestamp, sourceTimestamp? }
TrackerFrame  { samples[], headReference?, receiveTimestamp, frame metadata }
```

and the pipeline is

```text
VRChat OSC tracker messages -> TrackerFrame -> tracking-space normalisation
    -> tracker-to-humanoid solve -> HumanoidPose -> LiveCaptureSource
```

Two consequences bind the rest of this plan. The adapter must **not** grow a
copy of the VMC bone decoder, because it has no bones to decode. And the solve
is where a *generic contract* is missing rather than where an adapter should
improvise: [§10](#10-contract-changes-this-plan-requires) carries it, and
nothing VRChat-specific enters `motionCore` under any outcome.

VRChat's documented tracking space is Unity's — left-handed, +Y up, metres,
Euler rotations. It is written down, which makes it a claim to verify against a
capture rather than a grammar to measure from scratch; that is the one respect
in which this adapter starts ahead of `vrmAdapterMocopi`.

## 6. The adapter: capture precedes decoder

Sony's help pages list `VRChat (OSC)` as a mocopi transfer format and name
VRChat's default port. **This repository does not infer a packet shape from a
menu entry.** What a product sends is a measurement, and the fact that the
receiving specification is public does not establish that a sender implements
all of it, or only it.

```text
mocopi VRChat (OSC) -> raw UDP capture -> recorded corpus
    -> address / type-tag / cadence inventory -> decoder -> canonical comparison
```

This is the order `vrmAdapterMocopi` was forced into by an undocumented grammar
([adapter plan §6](adapters-mocopi-vmc-ardy.md#6-phase-2--the-mocopi-native-adapter)),
adopted here by choice. The inventory PR is the input to the decoder's design,
and until it exists the decoder has no committed shape.

**The first inventory records** — as manifest fields, per
[§7](#7-corpus-policy): sender application and version · device and sensor
configuration · target address and port · unique OSC addresses · type tags ·
packet size distribution · packet rate · per-address update rate · whether
bundles are used · tracker count · whether head data is present · position
units · rest-pose values · rotation representation · presence of a source
timestamp · traffic during tracking loss · traffic on reconnect · what changes
across a recentre.

The device session recipe is
[the mocopi one](adapters-mocopi-vmc-ardy.md#96-cross-source-comparison)'s, with
one difference worth writing down before the session rather than after: the
capture has to be taken on the same physical take as the native UDP recording,
or [§11](#11-the-fourth-observation-of-one-session) compares two performances
instead of two transports.

## 7. Corpus policy

Unchanged from [adapter plan §9.2](adapters-mocopi-vmc-ardy.md#92-corpus) —
generated and recorded never mix, and a session that cannot be redistributed
leaves a manifest and no bytes.

```text
adapters/liveCapture/vrchatOsc/tests/corpus/
├─ generated/     protocol shapes, committed, CI-runnable, no hardware
└─ recorded/
   ├─ redistributable/   real sessions cleared for publication
   └─ manifests/         everything else, as measured facts
```

**Generated** fixes the protocol's shapes with no hardware: one tracker · three ·
eight · head reference present and absent · position only · rotation only ·
mixed messages · a malformed packet · an unsupported address · a duplicate
update · a missing tracker · reordered packets · an OSC bundle · non-tracker
VRChat OSC traffic interleaved.

**Recorded** is the evidence, and its minimum is one real mocopi
`VRChat (OSC)` session covering the same takes the native corpus covers —
neutral standing, head turn, arm raise, walk with root motion, restart — so that
[§11](#11-the-fourth-observation-of-one-session) has a per-take comparison
rather than one aggregate number.

**A VRChat client is never a test dependency.** The wire-level generated corpus
and the recorded mocopi corpus are the primary evidence, and every replay test
completes with nothing installed
([adapter plan §9.5](adapters-mocopi-vmc-ardy.md#95-lanes): the hardware lane is
never a required PR gate).

## 8. Diagnostics

The adapter owns a namespace, frozen before its decoder, as both siblings' sets
were:

```text
VRM_VRCHAT_OSC_PACKET_MALFORMED     VRM_VRCHAT_OSC_UNSUPPORTED_ADDRESS
VRM_VRCHAT_OSC_ARGUMENT_MISMATCH    VRM_VRCHAT_OSC_TRACKER_ID_INVALID
VRM_VRCHAT_OSC_TRACKER_PARTIAL      VRM_VRCHAT_OSC_SOURCE_TIMEOUT
VRM_VRCHAT_OSC_SOURCE_RESTARTED     VRM_VRCHAT_OSC_COORDINATE_INVALID
VRM_VRCHAT_OSC_SOCKET_BIND_FAILED   VRM_VRCHAT_OSC_CALIBRATION_REQUIRED
```

`VRM_VRCHAT_OSC_SOCKET_BIND_FAILED` is here because both siblings needed one and
a set that omits it describes a decoder rather than a live adapter — the same
correction the `VRM_MOCOPI_*` set took on 2026-08-03.

**The one open question is the shared decoder's own refusals**, and it is
[§3.1](#31-libsosc--extract-after-the-second-consumer)'s third coupling. Today
`DecodeOscPacket` raises `VRM_VMC_PACKET_MALFORMED` — a code owned by an adapter
— for a failure that is about OSC and not about VMC. Three ways out, decided in
the extraction change and not here:

1. `libs/osc` raises protocol-neutral codes (`OSC_PACKET_MALFORMED`,
   `OSC_TYPE_TAG_INVALID`, `OSC_BUNDLE_INVALID`) and each adapter maps them onto
   its own surface. This is the shape
   [`MatchSourceProfile`](../architecture/WORKSPACE.md) already uses: the lower
   layer returns a typed refusal naming the event, and the caller that knows
   which reader it holds maps it onto that reader's frozen codes.
2. `libs/osc` returns a typed refusal carrying no code string at all, and every
   code stays in an adapter.
3. The decoder takes the code as a parameter — rejected on sight, because a
   library that can be handed any code has no diagnostic contract.

(1) is the precedent and (2) is the smaller move. Either keeps the existing
`VRM_VMC_*` strings stable, which is a requirement rather than a preference:
they are a frozen surface with golden tests over their formatted form.

## 9. Milestones

| Milestone | Half | State |
| --- | --- | --- |
| OSC-0 — characterise the existing decoder | foundation | ✅ |
| OSC-1 — merge the transport divergences | foundation | ✅ |
| OSC-2 — extract the transport ring | foundation | ✅ |
| VRC-0 — adapter scaffold and raw capture | adapter | ✅ |
| OSC-3 — second OSC consumer, then extract `libs/osc` | foundation | ✅ |
| VRC-1 — real mocopi capture and address inventory | adapter | 🚧 |
| VRC-2 — tracker semantic decode | adapter | ⬜ |
| VRC-3 — tracking-space normalisation | adapter | ⬜ |
| VRC-4 — tracker frame assembly | adapter | ⬜ |
| VRC-5 — the humanoid solve boundary | adapter | ⬜ |
| VRC-6 — CLI and record | adapter | ⬜ |
| VRC-7 — cross-source evidence | both | ⬜ |

The order interleaves on purpose, and the two places it does are the two
findings of [§2](#2-the-duplication-census). **OSC-1 and OSC-2 precede VRC-0**,
because VRC-0's deliverable is a packet recorder and recording one is what would
copy `PacketCapture` a third time. **OSC-3 follows VRC-1**, because until a
non-VMC caller has decoded a real VRChat datagram there is no evidence the OSC
API is neutral, and extracting on belief is what the second-consumer rule
exists to prevent.

**The second of those was split rather than followed or broken, and the table
above shows the result.** VRC-1 has two halves that need different things: the
inventory *tool* needs a decoder, and the inventory *itself* needs an operator
and a device. The rule OSC-3 waits on is about the first — a caller that never
says `VMC`, decoding real bytes — and a session is not what makes a caller
neutral. So the tool was written and measured first, OSC-3 moved the decoder on
that evidence, and VRC-1 is open on the session alone. Written down because the
alternative was to mark VRC-1 done on a tool, which would have hidden the one
thing it exists to produce.

### OSC-0 — characterise the existing decoder

Freeze `OscPacket`'s public behaviour in tests before anything moves: valid
messages · bundles · every supported type tag · malformed rejection · offset
diagnostics · atomic packet decode. No source moves in this step. Done when a
change to `OscPacket.cpp` that alters observable behaviour fails a test that
names the behaviour rather than the implementation.

**Done 2026-08-24.** Seven characterisation tests in
[`test_osc_packet.cpp`](../../adapters/liveCapture/vmc/tests/test_osc_packet.cpp),
`src/` untouched. They name what the suite written beside the decoder left
implicit: a bundle refused *after* two good elements yields nothing; a decode
overwrites all three `OscPacket` fields and a refusal overwrites none; a
diagnostic's byte offset is an exact number — including for a message two
bundles deep, where a lost `base` reports 40 instead of 60 — and its subject is
the offending address; every decoded view points into the caller's datagram;
`i`/`h` are signed where `c`/`r`/`m` are raw bits; a string whose length is
already a multiple of four is padded by four; and the bundle depth cap accepts
exactly `MaxOscBundleDepth`. The acceptance criterion was checked rather than
assumed: six mutations of `OscPacket.cpp` — decode in place, a nested offset
without its base, a refusal that drops the address, `c`/`r`/`m` sign-extended,
padding rounded up, and the depth cap off by one — each fail a test named for
the behaviour they break.

One finding, recorded and not fixed: a `t` *argument* shares `h`'s signed
64-bit path, so a real NTP time tag — whose high bit has been set since 1968 —
reads as a negative `integer`. Nothing in VMC sends one and `OscArgument` has no
unsigned field to widen into, so it is a question `libs/osc`'s API owes an
answer to ([§10](#10-contract-changes-this-plan-requires)) rather than a defect
OSC-0 may repair: this step changes no behaviour by construction. A *bundle's*
time tag is unaffected — it lands in `OscPacket::timeTag`, which is unsigned.

### OSC-1 — merge the transport divergences

Bring the older copy up to the four fixes the younger one already carries
([§2.1](#21-the-divergences-which-the-tree-already-documents)), each with a
test, **before** either copy moves. A fix inside a file move is a fix nobody
reviews.

This step has value even if the rest of this plan is abandoned: `vrmAdapterVmc`
is shipped, and the four are live-session defects in shipped code.

Done when: the VMC receiver clamps a large finite timeout instead of blocking
indefinitely; sizes its buffer so an oversize datagram is detectable and
counted; inspects `revents` before treating a wake-up as traffic; and stops
charging idle accounting to a call that received something. The two non-defect
differences are decided in the same change rather than inherited: the silence
timeout exists in both or in neither, and `Open`/`Close` leave the same state in
both.

The mocopi header's own framing is the acceptance criterion — *the two copies
are held together by their tests rather than by their source*. After this step
they are held together by their behaviour, which is what makes the next step a
move rather than a merge.

**Done 2026-08-24.** Four fixes, one behaviour per commit, no file moved. The
buffer is one byte above the bound so an over-long datagram is detectable on
POSIX; a timeout past what a poll can express is clamped rather than mapped onto
"wait forever"; `revents` is inspected before a wake-up is treated as traffic;
and the retry tail no longer charges `idleReceives` for a call that met
something.

**Two of the four ship without a test, and the reason is the same for both: no
test could tell the fix from the defect.** A poll timeout of `-1` and one of
`INT_MAX` differ only after 24.8 days, and a wake-up reporting `POLLERR` instead
of a datagram is not producible on three platforms from a suite that owns only
its own sockets — an unconnected UDP socket collects no ICMP error, and
`POLLNVAL` needs a descriptor closed underneath a poll already running, which is
the race this class documents as unsupported. A test that passed against the
defect would be worse than none. The honest seam is a unit test of the mapping
and of the wake-up predicate, and putting one in `vrmAdapterVmc` alone means
giving it a public function or an internal header the sibling does not have —
divergence, in the step whose purpose is convergence. **OSC-2 carries that ask**:
the extracted library can hold an internal header and its own unit tests without
either adapter growing an API.

The other two are tested. `vrmAdapterVmc_udpReceiverTruncation` mirrors the
mocopi test it derives from, on its own CTest name with `SKIP_RETURN_CODE 77`
because it needs an IPv6 loopback.

**Exactly one lane of three proves the buffer fix, and it is worth knowing
which.** Windows passes it with or without the fix, since `WSAEMSGSIZE` catches
the case there either way. **macOS arm64 skips it** — the hosted runner will not
carry the datagram, and it skips `vrmAdapterMocopi_udpReceiverTruncation` for
the same reason and has since v0.7.0, so this is the runner rather than the
change. **Linux runs it**, and on Linux the defect is what the assertion sees:
a buffer of exactly `MaxDatagramBytes` makes `recvfrom` return that length, the
drop branch is never entered, and the first assertion fails on `Received`
instead of `Idle`. That is the whole of the POSIX evidence, and calling it "the
POSIX lanes" would overstate it by one.

The idle-accounting assertion rides on the same case with a zero timeout, which
is the only way to narrow the window to the one call whose accounting is in
question; putting the increment back fails it. That path is reachable on
Windows through `WSAEMSGSIZE`, so unlike the buffer fix it is verified on all
three.

**The two non-defect differences, decided rather than inherited:**

- **`Open`/`Close` now leave the same state in both.** `Open` resets the stats,
  because it restarts `_epoch` either way — carrying the counters over produced
  a `datagramsReceived` spanning two sessions beside a `firstReceiveTime`
  describing one. `Close` releases the receive buffer instead of holding 64 KB
  on a closed receiver.
- **The silence timeout stays mocopi-only, and the difference now carries its
  reason in the VMC header.** Not because silence matters less to a VMC session:
  because this adapter's frozen diagnostic set has no code for it and its own
  documentation argues it did not need a ninth, so adding one is a contract
  change — which [§13](#13-pr-splitting) forbids inside a fix. Inventing a
  second spelling of `VRM_MOCOPI_DEVICE_UNAVAILABLE` would also make the shared
  library choose between two names for one event, which is exactly the question
  [§8](#8-diagnostics) exists to answer once. It arrives with the extraction.

### OSC-2 — extract the transport ring

The move, with no behaviour change: receiver, queue, capture format, diagnostic
vehicle. **Unblocked 2026-08-24** — the contract change in
[§10](#10-contract-changes-this-plan-requires) landed, so `liveTransport` has a
destination to be reviewed against. Its empty edge set and its no-code rule are
the two lines a reviewer can check the move against without reading the diff
twice.

It also inherits two asks from OSC-1, both of which exist because a shared
library can hold what an adapter cannot: an internal header, and unit tests
against it. The timeout mapping and the poll-wake-up predicate go there and get
the tests OSC-1 could not write without making the two adapters diverge. The
silence timeout arrives with them, once [§8](#8-diagnostics) has answered whose
code a shared receiver raises.

Done when: both adapters build against the shared library, every committed
capture in both corpora still reads without being rewritten, both adapters'
tests pass unchanged, and the binary link check shows neither imports the other.

**Done 2026-08-24.** `libs/liveTransport`, in three changes: the library, then
each adapter pointed at it. 1485 lines left `vrmAdapterVmc` and 337 arrived;
1781 left `vrmAdapterMocopi` and 359 arrived. What arrives in each is the part
a shared library may not hold — a code table, and a map from a transport event
to one of its rows. 97/97 green, both corpora round-tripping, and
`ost library build` / `ost library test` measured working on the new leaf first
try.

**The three questions [§3.2](#32-the-transport-ring--extract-before-the-third-consumer)
said the extraction had to answer rather than assume, answered:**

- **The capture format is one format with a per-adapter magic**, and the
  committed corpora were the constraint exactly as predicted. The magic is a
  parameter; the header *vocabulary* converged, so `device` is now everyone's.
  That widens a VMC capture by one accepted key and changes no fixture byte,
  because the writer emits only the fields a capture carries. The alternative —
  a per-adapter key list in the shared reader — is a knob for one optional field,
  which is the per-caller difference the library exists to stop carrying.
- **The four defects arrived as merged behaviour**, because OSC-1 merged them
  first. No fix rode inside the move.
- **`DatagramQueue` came along and stayed opt-in.** A tracker recorder is a
  polling loop like the mocopi one, so the third consumer did not change the
  answer, and `vrmAdapterMocopi` still names it nowhere.

**The mechanical problem [§10](#10-contract-changes-this-plan-requires) left
open is solved by a template parameter, and the reason is worth a line.**
`Diagnostic::code` was a per-adapter enum *by value*, so the vehicle is now
`Diagnostic<Code, DefaultCode>` over the adapter's own enum. `DefaultCode` is a
parameter rather than `Code{}` because the two adapters disagree and **both are
right**: each defaults to its own `PacketMalformed`, which is enumerator 0 in
one set and 6 in the other. A shared struct that defaulted to zero would have
silently changed what a default-constructed mocopi diagnostic meant — the one
behaviour change this step could have shipped without noticing.

**The silence timeout arrived, and §8's question is untouched.** The shared
receiver raises no code: it reports a `TransportEvent` — `BindFailed`,
`Silence` — and each adapter maps it onto its own frozen set. That is §8's
option (1) applied to the transport ring, and it does not pre-empt the same
question for the decoder, where the answer has to survive `VRM_VMC_*` being a
golden surface. So the capability is unconditional in the library and the *code*
is still the adapter's problem: `vrmAdapterMocopi` exposes the threshold,
`vrmAdapterVmc` leaves it at 0 because `VRM_VMC_*` has no code for silence. The
difference used to be thirty lines of receiver in one copy and none in the
other; it is now one configuration field and one `switch` arm.

**OSC-1's two asks are paid.** `src/PollTimeout.h` is the internal header an
adapter could not hold without diverging from its sibling, and
`tests/test_poll_timeout.cpp` exercises the timeout mapping and the wake-up
predicate with synthetic bits rather than a platform's `POLLIN` — which is what
makes the `POLLERR` / `POLLHUP` / `POLLNVAL` combinations no socket produces on
demand testable at all.

**"No behaviour change" is checkable, and it was checked in the two places it
could have failed silently.** The corpus round-trip tests compare bytes rather
than parse trees, so a writer that changed one character would be red; and the
boundary check was verified by injection rather than by its green result — an
added workspace include, adapter code, address literal, producer-prefixed
identifier and `pxr/` include each fail it. The producer pattern has no trailing
word boundary because with one, an injected `mocopiThing` passed.

**One test line changed, and it is the whole of the source-compatibility cost.**
`vrmAdapterVmc`'s `test_udp_receiver.cpp` called `ReadPacketCaptureFile`
unqualified, reaching the adapter by argument-dependent lookup because
`PacketCapture` was declared in its namespace; the type is the library's now and
ADL follows it there. It is qualified, as the other 27 packet-capture call sites
in the tree already were. Every other name both adapters exported is unchanged,
because the shared types arrive through a `using` rather than a rename.

### VRC-0 — adapter scaffold and raw capture

`adapters/liveCapture/vrchatOsc/`: manifest, build scaffold, frozen diagnostic
set, and a recorder over the shared transport. **No semantic decoder.**

Done when: bytes off the socket and bytes in the capture file are identical;
a capture replays deterministically; the manifest records sender and version.

**Done 2026-08-25**, in three changes: the contract row, the library, the CLI.
105/105 green, of which eight names are new.

**The extraction paid, and the receipt is the file sizes.** The census
([§2](#2-the-duplication-census)) measured the capture format written twice at
~400 lines and the receiver written twice at ~550. In this adapter the format is
one magic string and four forwarding calls, and the receiver is a `switch` over
two transport events. What is left is exactly what a shared library may not hold
— a code table, and the map from an event to one of its rows — which is
[WORKSPACE.md](../architecture/WORKSPACE.md) §2's diagnostic split seen from the
first adapter written on the near side of it. The four receiver defects arrive
fixed rather than copied a third time.

**One edge where the contract permits three, and it is measurable.**
`motionCore` and `motionRuntime` are what an adapter takes when it produces
canonical values, and this milestone produces none — so declaring them would
claim a dependency the library does not have. The consequence is visible rather
than asserted: this adapter's test binaries import **no OpenUSD at all** (checked
with `dumpbin`), so unlike both siblings they need no Gf DLL directory on `PATH`
to run. The day a decoder produces a pose, that changes as a link line growing.

**The published specification changed nothing about the order, which was the
open question this milestone actually answered.** `vrmAdapterMocopi` records
before it decodes because its protocol is documented nowhere; this one had the
option of writing a decoder from VRChat's own documentation first. It did not,
and the reason is [§6](#6-the-adapter-capture-precedes-decoder) applied rather
than restated: a specification says what a *receiver* must accept, and what a
sender sends is a measurement. Every payload in every test here is a counting
pattern, and the one place a plausible OSC message would have been most welcome
— the recorder's report — deliberately has none: it reports the datagram
envelope and refuses to group by address, because that grouping would be the
first thing anybody read off a real session with every number conditional on an
untested assumption. The address inventory is VRC-1's, measured from bytes.

**The done-condition is asserted end to end and against an independent reader.**
`vrchat_osc_record_loopback` sends deliberately awkward payloads — empty,
unaligned, spanning the printable range and out of it — through a real socket
into a real capture file, and a capture parser written in Python reads it back
and compares byte for byte. A writer and a reader that agreed with each other
and with nothing else would fail it. A second name,
`vrmAdapterVrchatOsc_loopbackCorpus`, does the same for committed fixtures and
is registered by globbing for a capture rather than for the corpus directory — so it appears on
the commit that adds the first one instead of failing red from today.

**Two things were measured rather than assumed, and both are worth carrying
forward.** A replayed report's duration differs from the live one's in the last
digit, because a capture stores receive times to six decimal places — so the
counts are compared and the duration is not. And the boundary check was verified
by injection in every direction rather than by its green result: an added
`vrmAdapterVmc/OscPacket.h` include fails this adapter's check, an added
`vrmAdapterVrchatOsc` include fails each sibling's, and each file passes without
it. That trio is the point at which the sibling rule stops being a formality —
`vrmAdapterVmc` holds the only OSC decoder here and this adapter reads the same
wire format, so reaching across would *work*.

**One unrelated defect surfaced and was fixed in its own change.** Every
`check_boundaries.py` in the tree located `dumpbin` under a glob naming
`2022` literally; this machine's Visual Studio was upgraded in place, and all
nine boundary checks went red at once with "dumpbin was not found". The release
year is a wildcard now.

### VRC-1 — real mocopi capture and address inventory

A real `VRChat (OSC)` session recorded and inventoried
([§6](#6-the-adapter-capture-precedes-decoder)). This milestone's output is a
measurement report, and it is the input to VRC-2's design. Done when the
inventory answers, from bytes, which subset of the VRChat OSC Trackers surface
this sender actually uses.

**The tool exists as of 2026-08-29 and the session does not.**
`vrmAdapterVrchatOsc::InventoryAddresses` and `vrchat_osc_record --inspect`
produce one row per address *and type tag string* a capture carried, with
message and datagram counts and the span each row covers. The pair is the key
rather than the address, because a sender that spells one address two ways is
the finding a table keyed on the address alone would average away.

It carries **no list of addresses it expects**, which is the property this
milestone actually needs: the risk being tested is that mocopi's `VRChat (OSC)`
output is not the tracker subset anyone assumes, and an inventory that reported
absences of expected rows would answer a different question. An address nobody
predicted appears as a row.

Every datagram in both suites is built byte by byte, and the tool's test encodes
OSC in Python rather than calling anything the tool links — so a decoder that
agreed with this repository's own encoder and with nothing else fails it.

What remains is an operator and a device, on the same terms as VRC-0's
done-condition: this milestone closes on a session, not on a tool.

### OSC-3 — second consumer, then extract `libs/osc`

VRC-1's inventory tool decodes through the existing decoder first, from
`vrmAdapterVrchatOsc`, without moving it. If that requires no VMC vocabulary,
the surface is neutral and the move follows in its own change, with the
diagnostic decision from [§8](#8-diagnostics) made explicitly.

**Done 2026-08-29**, in five changes: the contract, the move, the `t` decision,
`vrmAdapterVmc`, and the second consumer. 108/108 green, three names more than
VRC-0 left and none lost.

**The measurement came first and it is the whole of the argument.** An address
inventory of a VRChat OSC session, decoding through `vrmAdapterVmc`'s decoder
without moving it, needed **five VMC tokens** and every one was the *name*: one
include path and four namespace qualifications. It needed `VRMADAPTERVMC_STATIC`
on its compile line. And its report on a VRChat session printed
`VRM_VMC_PACKET_MALFORMED`. [§3.1](#31-libsosc--extract-after-the-second-consumer)
predicted exactly three couplings — the namespace, the export macro, the
diagnostic code — and the measurement found exactly those three and nothing
else: no VMC address literal, no bone name, no `VmcMessage`, no `SkeletonMap`.

**The diagnostic decision is option (2), and the transport ring's precedent is
what argues against (1) rather than for it.** `liveTransport` enumerates events
because its receiver raises two a caller must tell apart; the decoder makes one
distinction. Three neutral code names would have been a classification invented
at the boundary and mapped straight back onto one adapter code by every caller.
So a refusal is an `OscDecodeError` with a subject and a detail and no code, and
each adapter supplies its own — which is now *demonstrated* rather than
asserted: the same refusal reads `VRM_VMC_PACKET_MALFORMED` in one adapter and
`VRM_VRCHAT_OSC_PACKET_MALFORMED` in the other.

**The `t` argument was answered here rather than inherited.** It gets its own
unsigned `timeTag` field, so an argument's time tag and a bundle's are spelled
the same way and read the same way. OSC-0 found the defect and could not fix it;
this is the last moment before two adapters depend on the answer. Its size is
worth being plain about: NTP seconds have had their high bit set since 1968, so
the signed path was wrong for every time tag any sender emits today, not for a
far-future edge.

**Three things were paid for that the plan did not predict.** The sample
addresses in the moved suite were all `/VMC/...`, so they were replaced at
**identical length** — the suite asserts exact byte offsets, and a shorter name
would have rewritten every one of them while staying green. `tests/` is inside
this library's boundary check and outside `liveTransport`'s, because a decoder's
payloads are where a vendor address arrives and without the rule that
replacement is a convention the next author has not read. And the two
caller-bug guards now clear the refusal's subject: they used to overwrite a
whole `Diagnostic` by assignment, and a reused error left holding the previous
datagram's address would attribute a caller's mistake to a sender that sent
nothing wrong.

**One prediction VRC-0 made was wrong and the tree says so.** It recorded that
this adapter's binaries import no OpenUSD, and that this would change "the day a
decoder produces a pose … as a link line growing". The link line grew and the
closure did not, because `osc` links nothing at all — not even a socket. The
prediction still holds for a decoder that produces a *pose*; three comments that
stated it the old way are corrected rather than deleted.

### VRC-2 — tracker semantic decode

Known addresses decode to `TrackerSample`s; argument counts and type tags are
validated; an unknown VRChat OSC address is `VRM_VRCHAT_OSC_UNSUPPORTED_ADDRESS`
and the session continues. Generated corpus lands here; recorded corpus replays.

### VRC-3 — tracking-space normalisation

Unity/VRChat tracking space to the canonical basis, verified against the
recorded rest pose rather than against the documentation alone —
[the handedness episode](adapters-mocopi-vmc-ardy.md#96-cross-source-comparison)
is the precedent for why a documented basis is a hypothesis. **No avatar joint
is resolved in this layer.**

### VRC-4 — tracker frame assembly

A packet boundary is not a frame boundary, and the policy is stated rather than
emergent ([adapter plan §5.2](adapters-mocopi-vmc-ardy.md#52-frame-assembly-is-a-stated-policy-not-an-emergent-one)):
repeated updates for one tracker · partial tracker sets · timeout · stale
samples · the head reference · source reset · calibration discontinuity. Each is
a test, and each has a recorded or generated fixture that produces it.

### VRC-5 — the humanoid solve boundary

Tracker observations reach a `HumanoidPose`. Reuse the existing surface where
one exists; where none does, define a **generic** contract in the motion layer
rather than a solve inside the adapter ([§10](#10-contract-changes-this-plan-requires)).
No target-avatar-specific logic enters the adapter under any outcome — that is
[adapter plan §2](adapters-mocopi-vmc-ardy.md#2-what-an-adapter-is-allowed-to-be)
and it does not bend for a source that happens to need IK.

### VRC-6 — CLI and record

One tool, in the shape `vmc_record` and `mocopi_record` already have: listen ·
diagnostics · packet capture · `--inspect` · `--export-trace`. The library
authors no stage; the hand-off to the product is a `motion-capture-trace` file
([WORKSPACE.md §2](../architecture/WORKSPACE.md)).

Whether it is a third tool or a generic one is the question
[§3.4](#34-what-is-not-shared-and-must-not-become-shared) defers to this point,
with three implementations to measure instead of two.

### VRC-7 — cross-source evidence

[§11](#11-the-fourth-observation-of-one-session).

## 10. Contract changes this plan requires

Structural claims land in the contracts, in their own change, before this plan
depends on them ([docs/README.md](../README.md)).

- ✅ **A shared transport library had no home in the current contract.**
  [§3.2](#32-the-transport-ring--extract-before-the-third-consumer) rules out
  both obvious ones — `motionRuntime` would put a socket in the aggregate
  product's link closure, and `adapters/common/` is the forbidden sibling edge.
  A new leaf under `libs/` needed an identity row in
  [WORKSPACE.md §1](../architecture/WORKSPACE.md), edges in §2
  (`adapters/* -> libs/liveTransport`, and the prohibition that keeps it out of
  every product tool), and an aggregate-exclusion decision in §5 — where it
  takes the *adapter* side of the split, not `motionSource`'s, because a library
  the product must not link is excluded for the same reason an adapter is even
  though it carries no product name. **Done 2026-08-24**, in its own change
  ahead of any code: `liveTransport` has a §1 row, `adapters/* ->
  liveTransport` in §2, and a §5 artifact name and exclusion.

  Three things were decided rather than transcribed, and each narrows OSC-2.
  Its **edge set is empty** — the contract says none, not few, and that is a
  measurement: the six files being extracted include their own headers and the
  standard library and nothing else. Its exclusion is written on the **second of
  two clauses**, so §5's reader test now reads *producer-neutral **and** opens
  nothing* — `liveTransport` satisfies the first clause exactly as `motionBvh`
  does and is still out. And the diagnostic ring is split in the contract rather
  than in the extraction: the library owns the **vehicle**, an adapter's code
  enum stays frozen where it is, and a `liveTransport` holding one is a
  violation. What that left open was mechanical rather than structural, and
  OSC-2 solved it — `Diagnostic::code` was a per-adapter enum *by value*, and
  the vehicle is now `Diagnostic<Code, DefaultCode>` over the adapter's own
  enum. The default is a parameter because the two adapters disagree about it
  and both are right: each defaults to its own `PacketMalformed`, enumerator 0
  in one set and 6 in the other.

  Two of the three are now **executable rather than asserted**, in the four
  checks that already hold §2's neighbour prohibitions: `motionRuntime`,
  `vrmRetarget`, `motionSource` and `motionBvh` refuse the name
  `liveTransport` in their sources. Verified in both directions rather than by
  the token's presence — an injected `#include "liveTransport/UdpReceiver.h"`
  in `motionRuntime` fails the check with the token and **passes without it**,
  so the token is what fires. `liveTransport` needs naming where an adapter
  does not, and that asymmetry is the point: an adapter carries a product name
  those checks already refuse, while a shared transport carries none by
  contract.

  Not decided in the contract, deliberately — both are behaviour, both had
  committed fixtures or a frozen code set as their constraint, and a contract
  that pre-empted them would have been deciding an extraction it could not see.
  **The extraction decided both on 2026-08-24.** The magic stays per adapter and
  the header vocabulary converged. The silence timeout is unconditional in the
  library and its *code* is still per adapter, so it is in one adapter and not
  the other — which is neither of the two answers the question offered, and is
  the one the diagnostic split forces.
- ✅ **The adapter itself had no identity row, and this plan did not notice.**
  This section listed the two libraries it was about to create and none of the
  three rows the *adapter* needs, because §1's three reserved adapter identities
  read as covering a fourth. They do not: they are three named rows, and a
  scaffold landing under `adapters/liveCapture/vrchatOsc/` without one would have
  been the first adapter whose identity the tree asserted and the contract did
  not. Found on 2026-08-24 while starting VRC-0, and paid the same way
  `liveTransport` was — **in its own change, ahead of any code**. §2 needed
  nothing, which is the measurement worth keeping: `adapters/*` is already the
  rule, and this adapter needs no edge that rule does not already permit, so the
  row carries the one claim §2 cannot make — it is not a pose source. What the
  code then declared is *one* of the three, because VRC-0 produces no canonical
  value; that is the adapter's shape rather than the contract's, and it belongs
  in VRC-0's record below rather than here. §5 gains an
  artifact name and the aggregate exclusion on the terms every adapter already
  has, plus the member count a 0.22.x workstation will report once this
  adapter's CLI exists. Blocked VRC-0.
- ✅ **`libs/osc` is a second new identity**, with the same three rows and one
  addition: the boundary check in [§4](#4-what-libsosc-owns) is what makes its
  neutrality enforced rather than asserted. **Done 2026-08-29**, in its own
  change ahead of the move, and after the second consumer rather than before it
  ([§3.1](#31-libsosc--extract-after-the-second-consumer)) — which is the one
  respect in which this row could not follow `liveTransport`'s procedure.

  Three things were decided rather than transcribed. Its **edge set is empty
  and includes `liveTransport` in the prohibitions**, in both directions: every
  other shared-leaf rule in [WORKSPACE.md §2](../architecture/WORKSPACE.md) is
  asymmetric because one side is a layer and the other is what may reach it,
  and these two are the same layer twice — a decoder that can open a socket has
  become a receiver, and a receiver that can decode has become an adapter with
  no adapter around it. Its **exclusion from the aggregate needed a third
  question**: §5's two clauses are *does it name a product* and *would the
  product acquire I/O*, and an OSC decoder fails neither — it is out because no
  member of the product links it or can, since nothing in the product reads a
  datagram. That is a weaker reason than the transport's and §5 now says so, so
  that a product member with a reason to decode OSC re-argues the paragraph
  instead of quietly outgrowing it. And the **refusal type** is settled in the
  identity row rather than left to the extraction, because the row cannot state
  what the library holds without stating what it does not: see the next item.

  Two of the three are executable rather than asserted, in the four checks that
  already hold §2's neighbour prohibitions: `motionRuntime`, `vrmRetarget`,
  `motionSource` and `motionBvh` refuse `osc` in their sources. The token is
  `osc::` or `osc/` rather than the bare word, and that is not fastidiousness —
  `motionBvh`'s own parser header names the OSC decoder in a comment about a
  rule the two share, and a check that fired on prose would be answered by
  deleting the sentence. Verified in both directions in a copied tree: an
  injected include fails all four, a `osc::` call fails all four, and each
  passes without them.
- ⬜ **Whose diagnostic codes does a shared decoder raise?**
  [§8](#8-diagnostics) states the three options and the precedent. This is
  [MOTION_CONTRACT.md](../design/MOTION_CONTRACT.md)-adjacent but not its —
  adapter diagnostics are the adapter plan's §8 and this decision amends it.
  Blocks OSC-3.

  **The transport ring answered it for itself on 2026-08-24, and that is
  precedent rather than decision.** `liveTransport` took option (1): a neutral
  `TransportEvent`, mapped by each adapter onto its frozen set. It was cheap
  there because the receiver raised two codes and neither is golden. The
  decoder's version is not cheap in the same way — `VRM_VMC_PACKET_MALFORMED`
  has golden tests over its formatted line, so whatever the shared decoder does
  has to leave that string standing.

  **Decided 2026-08-29: option (2), and the precedent is what argues against
  (1) rather than for it.** `liveTransport` enumerates its events because its
  receiver genuinely raises two that a caller must tell apart — one adapter maps
  `BindFailed` and drops `Silence`, and that difference is real. The decoder
  makes **one** distinction: a datagram is decodable OSC or it is not. Every
  refusal in `OscPacket.cpp` today raises the same code, and the three neutral
  names §8 offered — `OSC_PACKET_MALFORMED`, `OSC_TYPE_TAG_INVALID`,
  `OSC_BUNDLE_INVALID` — would be a classification invented at the move,
  mapped straight back onto one adapter code by every caller, and trusted by
  the next reader as though the decoder could tell them apart. So the refusal
  is a typed value with no code in it: the offending address as its subject,
  and the byte offset in its detail, which are the two things the decoder
  actually knows. The adapter supplies the code, and `VRM_VMC_PACKET_MALFORMED`
  is spelled in exactly the place it was before.
- ⬜ **What an OSC `t` argument reads as.** OSC-0 froze the current answer and
  found it wrong for real senders: `t` shares `h`'s signed 64-bit path, so an
  NTP time tag — high bit set since 1968 — arrives as a negative `integer`.
  Whether the shared decoder widens `OscArgument` with an unsigned field, keeps
  the raw bits and documents the reinterpretation, or splits `t` out as its own
  member is an API decision, and it is cheaper before two adapters depend on
  the answer than after. Nothing in VMC or in the VRChat tracker surface sends a
  `t` argument, so it blocks nothing; it is here so the extraction decides it
  rather than inherits it. Belongs with OSC-3.
- ⬜ **A tracker observation has no representation in the motion contract.**
  `motionCore` carries `HumanoidPose`, which is post-solve. Whether a
  pre-IK tracker sample needs a contract there — a generic `TrackingSource` or
  tracker-sample type — or stays entirely inside the adapter until it becomes a
  pose is VRC-5's question, and it is the one place this plan could push a
  VRChat-shaped type into a vendor-neutral library. **No VRChat-specific type
  enters `motionCore` under any outcome**; if the generic form is not clear, the
  adapter keeps its own and the contract stays unwritten. Blocks VRC-5.
- ⬜ **The live-source bridge**, carried by
  [adapter plan §11](adapters-mocopi-vmc-ardy.md#11-contract-changes-this-plan-requires)
  and unchanged in substance. This plan supplies its third instance
  ([§3.3](#33-the-live-source-bridge--already-scheduled-and-the-third-instance-settles-it)),
  which is evidence that item was explicitly missing. Blocks nothing here; VRC-5
  is cheaper after it.
- ⬜ **The workspace graph gate still does not reach an adapter**, and a third
  adapter makes the gap wider rather than different
  ([report 34](../reports/ost/34-2026-07-29-v0.21.0-adapter-library-discovery-gap.md)).
  A new library under `libs/` *is* discovered, so the shared halves of this plan
  are gated even while the adapter consuming them is not — which is an odd
  arrangement to inherit and worth stating before someone reads the green result
  as coverage.

## 11. The fourth observation of one session

[Adapter plan §9.6](adapters-mocopi-vmc-ardy.md#96-cross-source-comparison) is
two of three paths as of 2026-08-15. This adds a fourth, and it is the most
informative one available, because it is the only path whose target format was
specified by someone with no interest in this repository:

```text
                   ┌─> mocopi native UDP  -> vrmAdapterMocopi ──┐
one physical ──────┼─> mocopi VRChat OSC  -> vrmAdapterVrchatOsc┼─> canonical
session            ├─> mocopi BVH export  -> motionBvh ─────────┤     motion
                   └─> VMC relay          -> vrmAdapterVmc ─────┘        ↓
                                                                    compared
```

Compared at the canonical layer and nowhere lower, on the terms §9.6 already
fixes: sample and frame rate · root translation · hips motion · major bone
rotation · coordinate signs · scale · missing joints · how each path represents
tracking loss and reconnection · what each path drops. Latency is a live-path
measurement only.

The tracker path adds one comparison the other three cannot make: it is the only
one where the pose is **solved** rather than transported, so a difference there
is attributable to the solve in a way no difference between the native and BVH
paths ever was. That makes the classification list longer by one and more useful:

```text
native only · VRChat OSC only · BVH only · relay only ·
conversion difference · solve difference · timing difference · unknown
```

**A difference outside tolerance is classified, never absorbed by widening the
tolerance.** The completion condition is not "all four agree" — it is that what
each path carries and what each path drops is written down from evidence, with
the same discipline that produced report 01's finding that 4.81 m of hips travel
reaches the recorded path and nothing at all reaches the live one.

## 12. What this boundary deliberately excludes

Not gates, and the architecture must not require them: Avatar Parameters ·
VRChat Input API · OSC eye tracking · OSCQuery discovery · two-way integration
with a VRChat client · generic expression parameter binding · arbitrary custom
OSC routing · an OSC sender or output API · a generic OSC router · realtime
viewport display · OpenExec of any kind (that stays
[its own plan](openexec-foundation.md)).

Explicit bind address and port is the whole of the configuration surface;
discovery is a UX problem and it is independent of whether the tracking data is
decoded correctly.

**Avatar Parameters is the one that will be argued for**, because
`/avatar/parameters/<name>` maps onto VRM expressions and Motion Phase G is
already in motion. It stays out: shipping tracker input, an IK boundary,
expression mapping, and an avatar-specific parameter vocabulary in one release
means none of the four gets its own evidence. What this plan guarantees instead
is that adding it later costs no rework — the decoder is generic, and the
adapter splits by address family:

```text
vrmAdapterVrchatOsc
├─ Tracking/
└─ later:  AvatarParameters/ · EyeTracking/ · Input/
```

## 13. PR splitting

The order is the review order, and it follows [§9](#9-milestones) exactly. The
two rules the census adds to
[adapter plan §12](adapters-mocopi-vmc-ardy.md#12-pr-splitting):

- **A file move and a semantic addition are never the same PR** — OSC-1 fixes,
  OSC-2 moves, and neither does the other's work.
- **No PR introduces the third copy of anything in [§2](#2-the-duplication-census)'s
  top four rows.** If a PR needs one before its extraction has landed, the
  extraction is what is missing, not the copy.

```text
1  OSC-0  characterisation tests, nothing moves                     ✅
2  OSC-1  transport divergences merged, one behaviour per commit    ✅
3  ——     contract change: libs/liveTransport identity and edges    ✅
4  OSC-2  transport extraction, no behaviour change                 ✅
5  VRC-0  adapter scaffold, manifest, diagnostics, recorder         ✅
6  VRC-1  real capture + address inventory report        ← decoder design input
7  ——     contract change: libs/osc identity, edges, boundary check
8  OSC-3  second consumer proven, then the OSC move
9  VRC-2  tracker semantic decode + generated corpus
10 VRC-3  tracking-space normalisation
11 VRC-4  frame assembly and its policies
12 VRC-5  humanoid solve boundary (after its contract change, if one is needed)
13 VRC-6  CLI, trace export, artifact-only smoke
14 VRC-7  cross-source evidence report
```

Every one of them checks what the adapter plan's §12 checks: standalone build ·
dependency direction · no reverse dependency · no vendor-name leakage ·
deterministic fixture tests · diagnostic stability · clean install · package
closure.

## 14. Risks

**A — mocopi's `VRChat (OSC)` is not the tracker subset anyone expects.**
Capture first. The inventory is the decoder's input, and a menu label is not a
specification. If the shape turns out to be something else entirely, VRC-1 is
where that is discovered, at the cost of one PR rather than the adapter.

**B — the tracker-to-humanoid solve has no home.** The honest outcome may be
that this repository has no generic IK and that writing one is a larger project
than this plan. Then VRC-5 lands as a stated limitation with the tracker frames
reaching a documented boundary and stopping, and the release claims tracker
*input* rather than tracker-driven *motion*. That is a smaller claim, not a
failed one, and it is preferable to an IK implementation smuggled in as an
adapter detail.

**C — the extraction becomes the whole project.** Three of the four rings are
extractions and only one is new code. Mitigated by ordering — characterise, fix,
move, each separately reviewable — and by the fact that OSC-2's done-condition
is "no behaviour change", which is checkable.

**D — a difference turns out to be an intentional choice.** The four in
[§2.1](#21-the-divergences-which-the-tree-already-documents) are named as
defects by the file that fixed them, so those are settled; the two non-defect
differences are not, and the silence timeout in particular may be a mocopi
feature rather than a VMC omission. OSC-1 decides each explicitly, and a
difference that survives it survives with a comment saying why.

**E — a client dependency in CI.** VRChat is never a test dependency
([§7](#7-corpus-policy)).

## 15. The shape this converges on

```text
            LIVE                              RECORDED
    ┌─────────┼──────────┐                        │
  VMC      mocopi     VRChat OSC                 BVH
  pose      native     trackers                   │
    │         │           │                       │
    └────┬────┴─────┬─────┘                       │
         │          │                             │
   libs/osc   libs/liveTransport                  │
         └──────────┴──────────┬──────────────────┘
                               ↓
                        canonical motion
                               ↓
                          motionRuntime
                               ↓
                           vrmRetarget
                               ↓
                            VRM avatar
```

Three live leaves and one recorded pipeline, sharing two libraries that know no
protocol and converging on one motion contract. The property is the same one the
adapter plan states and one clause longer: an input device, a relay application,
a tracking protocol, or a generation model can be replaced without changing
retarget, runtime, OpenExec, or the VRM application — **and no two of them
maintain the same socket twice.**
