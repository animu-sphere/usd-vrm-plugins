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
closed as of 2026-08-24** ([OSC-1](#the-foundation-half-shipped));
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

### 2.3 A third ring the census did not look at: the change of basis

*Added 2026-08-30, by VRC-3, and it is an observation rather than a plan.* The
census above compared adapter against adapter, so it could not see a ring whose
other copy is in `libs/`. Turning a source's axes into canonical ones now exists
three times: `vrmAdapterVmc/SkeletonMap.cpp` hard-codes the reflection through
X, `motionSource/CanonicalConversion.cpp` derives the general signed permutation
from a profile, and
[`vrmAdapterVrchatOsc/TrackingSpace.cpp`](../../adapters/liveCapture/vrchatOsc/src/TrackingSpace.cpp)
is the third.

**It is not extracted, and the reason is a contract rather than a judgement.**
The general implementation already exists and lives in the recorded half;
[WORKSPACE.md §2](../architecture/WORKSPACE.md) gives an adapter four edges and
`motionSource` is not among them, so reaching it is forbidden and the honest
alternatives are a third copy or a contract change. VRC-3 took the copy, because
a milestone that is *about* measuring a basis should not also be moving the code
that applies one.

What the third instance adds is the argument the next reader needs: this one
composes **Euler angles**, which neither existing copy does the same way — VMC
sends quaternions, and `motionSource`'s composition is driven by a profile
enumerator no live adapter has. A shared primitive in `motionCore` — the signed
permutation, its determinant, and one angle composition — would have all three
as consumers, which is one more than the rule this track keeps invoking asks
for. It is a contract change ([§10](#10-contract-changes-this-plan-requires))
and belongs with [the producer contract](backlog.md#canonical-motion-producer-contract),
where the four producer categories are already being unified; doing it inside
this track would settle a workspace-wide boundary from one adapter's needs.

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
are answered as of 2026-08-24** ([OSC-2](#the-foundation-half-shipped));
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
  so that a file move never carries a fix inside it ([OSC-1](#the-foundation-half-shipped)).
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

### 5.1 Assignment is a third thing, and it belongs to neither end

*Stated explicitly 2026-08-29, because the near-term plan named it as its own
task and the pipeline above had it hiding inside "solve".*

Three separable decisions sit between a tracker index and a bone, and collapsing
any two of them is how VRChat semantics leak upward:

| Decision | Owner | What it may know |
| --- | --- | --- |
| **Decode** — bytes to `TrackerSample` | the adapter | addresses, type tags, argument order. No body roles, no basis. |
| **Assignment** — which tracker is which body region | a **generic** policy, outside the adapter | tracker count, relative rest geometry, an operator's explicit statement. **Never a VRChat address literal.** |
| **Solve** — assigned observations to `HumanoidPose` | the motion layer | canonical bones, target-independent. Never an avatar. |

**Assignment is not a lookup and it is not IK either.** A three-point setup, a
six-point setup and a full-body setup differ in what is observable, not in what
is solvable, so the policy has to state what it does with a set it cannot place:
refuse, place partially, or hold. That is the same class of stated-policy
question `vrmAdapterMocopi` answered for a frame missing three bones, and it
gets the same treatment — a decision with a fixture, not emergent behaviour.

The default this plan expects to start from is **explicit assignment**: an
operator names which tracker is which region, exactly as `motion_bvh_convert`
requires a named profile rather than detecting one. Automatic assignment from
rest geometry is a later aid over the same contract, never the only path — a
detector written first would settle the contract on whichever calibration
happened to be recorded first, which is the failure this repository has now
avoided twice by the same argument (`libs/osc`'s second consumer, the BVH
corpus's second producer).

Where the assignment policy *lives* was the one question VRC-5 could not defer:
it is generic, so it is not the adapter's, and it names tracker regions, which
`motionCore` has no vocabulary for. **Answered 2026-08-31, in the contract and
ahead of any code** ([§10](#10-contract-changes-this-plan-requires)): a new leaf
`libs/motionTracking`, on the terms
[WORKSPACE.md §1](../architecture/WORKSPACE.md) states, with the region
vocabulary as its own rather than as an alias for `HumanBone` — the aliasing is
what would collapse this table's middle row into its first, and the contract
forbids it by name.

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
| VRC-1 — real mocopi capture and address inventory | adapter | ✅ |
| VRC-2 — tracker semantic decode | adapter | ✅ |
| VRC-3 — tracking-space normalisation | adapter | ✅ |
| VRC-4 — tracker frame assembly | adapter | ✅ |
| VRC-4a — tracker assignment policy | neither end | ✅ |
| VRC-5 — the humanoid solve boundary | the motion layer | ✅ |
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

### The foundation half shipped

OSC-0 (2026-08-24) froze `OscPacket`'s public behaviour in seven
characterisation tests with `src/` untouched, checked by six mutations that each
fail a test named for the behaviour they break. OSC-1 (2026-08-24) merged the
four transport divergences into `vrmAdapterVmc`, one behaviour per commit and no
file moved. OSC-2 (2026-08-24) extracted `libs/liveTransport` — 1485 lines left
`vrmAdapterVmc` and 337 arrived; 1781 left `vrmAdapterMocopi` and 359 arrived —
and every committed capture in both corpora still round-trips byte for byte.
VRC-0 (2026-08-25) added `adapters/liveCapture/vrchatOsc/` with its frozen
ten-code set and `vrchat_osc_record`, where the extraction's receipt is the file
sizes: a capture format that was ~400 lines written twice is one magic string
and four forwarding calls here, and the receiver is a `switch` over two
transport events. OSC-3 (2026-08-29) moved the decoder to `libs/osc` on
measured evidence rather than belief — an address inventory written in
`vrmAdapterVrchatOsc` decoded real bytes through the VMC-owned decoder first and
needed five VMC tokens, every one of them the name, exactly the three couplings
[§3.1](#31-libsosc--extract-after-the-second-consumer) predicted and nothing
else.

Five decisions from those milestones constrain the work still ahead, so they are
kept here rather than left in the changelog:

- **A refusal carries no code.** `OscDecodeError` has a subject and a detail,
  and each adapter supplies its own code — the same refusal reads
  `VRM_VMC_PACKET_MALFORMED` in one adapter and `VRM_VRCHAT_OSC_PACKET_MALFORMED`
  in the other. `liveTransport` enumerates events because its receiver raises two
  a caller must tell apart; the decoder makes one distinction, so three neutral
  names would have been a classification invented at the boundary and mapped
  straight back onto one adapter code by every caller. VRC-2 inherits this.
- **A `t` argument has its own unsigned `timeTag` field**, so an argument's time
  tag and a bundle's are spelled and read the same way. OSC-0 found the defect
  and could not fix it; OSC-3 was the last moment before two adapters depended
  on the answer. NTP seconds have had their high bit set since 1968, so the
  signed path was wrong for every time tag any sender emits today.
- **`tests/` is inside this library's boundary check** and outside
  `liveTransport`'s, because a decoder's payloads are where a vendor address
  arrives — without the rule, replacing them is a convention the next author has
  not read.
- **The sibling rule is verified by injection in every direction**, not by its
  green result: an added `vrmAdapterVmc/OscPacket.h` include fails this
  adapter's check, an added `vrmAdapterVrchatOsc` include fails each sibling's,
  and each file passes without it. This is where that rule stops being a
  formality — `vrmAdapterVmc` held the only OSC decoder here and this adapter
  reads the same wire format, so reaching across would *work*.
- **VRC-0 predicted that this adapter's binaries import no OpenUSD "until a
  decoder produces a pose", and half of that was wrong.** The link line grew at
  OSC-3 and the closure did not, because `osc` links nothing at all — not even a
  socket. The prediction still holds for a decoder that produces a *pose*, which
  is VRC-2's, and the comments that stated it the old way are corrected rather
  than deleted.

The recorder's own report deliberately refuses to group by address: that
grouping is the first thing anybody would read off a real session, with every
number conditional on an untested assumption. The address inventory is VRC-1's,
measured from bytes.

### VRC-1 — real mocopi capture and address inventory

A real `VRChat (OSC)` session recorded and inventoried
([§6](#6-the-adapter-capture-precedes-decoder)). This milestone's output is a
measurement report, and it is the input to VRC-2's design. Done when the
inventory answers, from bytes, which subset of the VRChat OSC Trackers surface
this sender actually uses.

**Done 2026-08-30** — six captures, 44 918 datagrams, and the measurement is
[report 02](../reports/motion/02-2026-08-30-vrchat-osc-address-inventory.md) with
[the session manifest](../../adapters/liveCapture/vrchatOsc/tests/corpus/recorded/manifests/2026-08-30-mocopi-vrchat-osc.json)
beside it. **The subset is eight addresses**: three numbered trackers and a named
`head`, each with `position` and `rotation`, every one of them `,fff`, one
message per datagram and no bundle anywhere.

Four things that milestone bought, none of which a specification would have
given:

- **`head` is a name in the position an integer occupies**, so a decoder that
  parses that segment as an index drops the head and reports nothing wrong. This
  is the row that justifies the whole capture-before-decode order.
- **A rotation is three floats**, not four, so the Euler convention becomes
  VRC-3's problem against a recorded rest pose.
- **The eight arrive as a burst in a fixed cycle** — `head` first, rotation
  before position, 99.7 % of consecutive pairs advancing one step, the whole
  cycle inside a median 0.053 ms — which is VRC-4's frame boundary handed to it
  as a measurement.
- **About a third of the frames never arrive**, the sender aiming at ~58 Hz and
  delivering ~39, with the residual single-address loss falling 96 % on
  `/tracking/trackers/1/rotation`. Not attributable from the receiving end, and
  the concrete case behind `VRM_VRCHAT_OSC_TRACKER_PARTIAL`.

**One finding lands outside this adapter.** A restart on this wire is marked by
the sender's *source port* and by nothing else — no session id, no rest table, no
handshake — and the capture format carries one peer in its header and none per
datagram, so that marker does not survive into a file. The live session saw two
peers; `--inspect` on the same capture sees one. The asymmetry is deliberate and
documented in
[`liveTransport/UdpReceiver.h`](../../libs/liveTransport/include/liveTransport/UdpReceiver.h);
what is new is that it now has a measured cost, because a fixture-driven restart
test can exercise the silence and not the identity change. Widening the format
touches a shared library, three adapters and two committed corpora, so it is
scheduled rather than done — see [§10](#10-contract-changes-this-plan-requires).

**The tool it was measured with predates the session.**
`vrmAdapterVrchatOsc::InventoryAddresses` and `vrchat_osc_record --inspect`
produce one row per address *and type tag string* a capture carried, with
message and datagram counts and the span each row covers. The pair is the key
rather than the address, because a sender that spells one address two ways is
the finding a table keyed on the address alone would average away.

It carries **no list of addresses it expects**, which is the property this
milestone actually needed and the reason the result reads the way it does: an
inventory that reported absences of expected rows would have said *four
predicted trackers are missing*, where this one said **`head` is here and is not
a number**. The risk being tested was that mocopi's `VRChat (OSC)` output is not
the tracker subset anyone assumes, and an address nobody predicted appeared as a
row.

Every datagram in both suites is built byte by byte, and the tool's test encodes
OSC in Python rather than calling anything the tool links — so a decoder that
agreed with this repository's own encoder and with nothing else fails it.

Two things this milestone did **not** produce, both of them properties of the
product rather than of the session, and both of them now facts the plan is built
on rather than risks it carries: no take has a BVH export beside it, because this
application records none while sending OSC; and no take shares a physical
performance with a native-wire recording, because the transfer format is
exclusive. [§11](#11-the-fourth-observation-of-one-session) carries what that
costs.

### VRC-2 — tracker semantic decode

Known addresses decode to `TrackerSample`s; argument counts and type tags are
validated; an unknown VRChat OSC address is `VRM_VRCHAT_OSC_UNSUPPORTED_ADDRESS`
and the session continues. Generated corpus lands here; recorded corpus replays.

**Done 2026-08-30** —
[`TrackerMessage.h`](../../adapters/liveCapture/vrchatOsc/include/vrmAdapterVrchatOsc/TrackerMessage.h),
its suite, and twelve generated captures written by
[`tools/generate_packets.py`](../../adapters/liveCapture/vrchatOsc/tools/generate_packets.py)
from report 02's measurements. The counts the corpus is replayed against come
from the generator's *structure* rather than from a run, a capture with no
expectation in the test fails, and six mutations of the decoder — the head
segment unread, type tags matched as a prefix, a bad identity reported as an
unsupported address, a non-finite component let through, components reordered,
an out-of-range index accepted — each fail both the unit suite and the corpus
replay, with the restored source green.

**It decodes to a `TrackerMessage`, and this section's first paragraph says
`TrackerSample`.** The difference is this wire's rather than a liberty taken
with the plan: [§5](#5-a-tracker-source-is-not-a-pose-source)'s sample carries a
position *and* a rotation, and on this wire those arrive in **separate
datagrams**, so no message decoder can fill both halves of one. A decoder that
returned a sample per message would default the other half — and a defaulted
rotation of (0, 0, 0) is bit-for-bit what a tracker at rest reports, so the
reader could not tell the invented value from the measured one. The sample is
therefore constructed by [VRC-4](#vrc-4--tracker-frame-assembly), which owns the
window that makes two messages one observation, and `TRACKER_PARTIAL` is raised
there rather than here: a single message is *always* partial, so a message layer
that reported it would warn about once a datagram forever.

Three more decisions the session settled, each of which a specification would
have settled the other way:

- **The identity is the path segment, and the index is a reading of it.** `head`
  has no index and every table in this adapter keys on the segment, so the head
  is never a special case in ordering, grouping or equality — it is a tracker
  whose name is not a number.
- **`,fff` exactly, where `vrmAdapterVmc` counts arguments past the form it
  knows.** That sibling's rule is right for a protocol whose messages grew by
  appending fields to a stable leading form. Here the arity *is* the meaning: a
  three-float rotation is Euler and a four-float one is a quaternion whose first
  three components are not Euler angles, so reading the known prefix of a longer
  form is a confident misreading rather than a partial read. The refusal quotes
  both tag strings. A sender emitting the four-float form would be *measured*
  into the table, not assumed into it.
- **`TRACKER_ID_INVALID` is held apart from `UNSUPPORTED_ADDRESS`.** `0`, `9`,
  `01` and `hip` are the first; `/avatar/parameters/…` and an unread tracker
  channel are the second. Collapsing them would make a sender's bad index
  indistinguishable from a part of VRChat's surface this adapter has not
  implemented, which is the one distinction an operator reading a session log
  needs from this layer.

Trackers **4–8 are accepted although nothing here has ever sent one**, and that
is a decision about VRChat's surface rather than about one sender: refusing them
would make this decoder call a legal address a protocol violation the first time
somebody connects a fuller setup.

**One capture of the twelve is the session's own shape**, and the manifest says
so per capture: `session` for that one, `derived` for five whose every address
and ordering the session carried but whose arrangement it did not, `unobserved`
for six carrying something it never sent — the numbered surface above among
them. A corpus that cannot say how far a recording stands behind each fixture is
one whose next reader has to guess, and the ratio is worth seeing: this
protocol's evidence is one measured arrangement, and everything else is
constructed from it or from the specification.

**What did not land: the recorded corpus does not replay**, because it has no
bytes and by policy will not get any from that session
([§7](#7-corpus-policy)). The generated half is the evidence a decoder has; a
redistributable session would be picked up by the same CTest names with no
change, and the milestone's replay condition stays open on the file rather than
on the code.

### VRC-3 — tracking-space normalisation

Unity/VRChat tracking space to the canonical basis, verified against the
recorded rest pose rather than against the documentation alone —
[the handedness episode](adapters-mocopi-vmc-ardy.md#96-cross-source-comparison)
is the precedent for why a documented basis is a hypothesis. **No avatar joint
is resolved in this layer.**

**Done 2026-08-30** —
[`TrackingSpace.h`](../../adapters/liveCapture/vrchatOsc/include/vrmAdapterVrchatOsc/TrackingSpace.h),
its suite and
[report 03](../reports/motion/03-2026-08-30-vrchat-osc-tracking-space.md), which
re-reads the VRC-1 session rather than recording a new one. The documented space
is the measured one — metres, +Y up, +Z forward, left-handed — so the conversion
is VRM 1.0's reflection through X, which is the sibling's line.

**Four of the five readings were settled by a rest pose; the fifth needed a
label.** A person standing still puts their head at 1.5178, their hips at 0.8922
and both feet at 0.092, which is the unit, the up axis and the floor in one
table. Handedness is not in that table and cannot be: nothing in a stream of
numbers says which way a body turned, and the answer came from the take whose
note reads "head **left**, centre, right" — a left turn reports a yaw of -77.5°,
so the body's left is -X and the basis is left-handed. **This is the row the
same device could have failed on**: its native wire is right-handed with +X on
the body's *left*, so one application's two outputs disagree about the sign of
X, and a decoder that carried the native reading across would have mirrored
every session silently.

**The Euler order is measured to three of six, and the residual is quantified
rather than waved at.** A head turning left and right does not roll, so the
three compositions that do not apply the yaw outermost are refused by the data —
they drag 12–18° of pitch into 12–21° of apparent roll at the ends of an 80°
turn, where the three that do hold the head within 2.6° of level. Which of X and
Z sits inside the yaw is **not** measurable from this session, because nobody
tilted: across 44 918 messages the second-largest component of any orientation
is 25.2°. The three survivors disagree by a median of 0.21°, 1.75° at the 95th
percentile and 12.33° at worst, with 96 % of samples inside 2°; the six disagree
by up to 25.7°. The adapter composes `Ry · Rx · Rz`, the survivor Unity
documents — documentation breaking a tie the measurement narrowed, rather than
standing in for one. **What closes it is one take**: a labelled *rolled* head or
foot, held, which is twenty seconds of hardware
([the operator-evidence list](current.md#carried-out-of-v070--evidence-an-operator-produces)).

Two consequences beyond the arithmetic. **The adapter's binaries load OpenUSD
for the first time** — producing a canonical value is what took the `motionCore`
edge; VRC-0 predicted this at VRC-2 and was a milestone early. Making the
boundary check *see* that took a second change, found in review: a static
archive contributes only the objects a binary references, so the exe the gate
inspected imported no OpenUSD at all and would have gone on passing whatever the
conversion reached for. The gate now names the suite that links the widest layer
the adapter touches, and `tests/CMakeLists.txt` states that as a rule. Measured
in both directions — with `gf` removed from the allowlist the new binary fails
and the old one passes, which is the defect itself.
And **the corpus cannot check any of it**: the generated fixtures' numbers are
this repository's own invention, so the corpus pass asserts that every decoded
message converts and that the corpus is not all identity, and the basis itself
is asserted only against the session. Six mutations — the mirror dropped, the
position mirror dropped, VRM 0.x's mirror through Z, the yaw moved innermost,
the angles read as radians, the channel guard removed — each fail the suite,
with the restored source green.

### VRC-4 — tracker frame assembly

A packet boundary is not a frame boundary, and the policy is stated rather than
emergent ([adapter plan §5.2](adapters-mocopi-vmc-ardy.md#52-frame-assembly-is-a-stated-policy-not-an-emergent-one)):
repeated updates for one tracker · partial tracker sets · timeout · stale
samples · the head reference · source reset · calibration discontinuity. Each is
a test, and each has a recorded or generated fixture that produces it.

**Done 2026-08-30** —
[`FrameAssembler.h`](../../adapters/liveCapture/vrchatOsc/include/vrmAdapterVrchatOsc/FrameAssembler.h),
its suite and three new corpus fixtures. This is the first file in this adapter
that **decides** rather than converts, and the first whose failure mode is
invisible per value: every sample in a wrongly-cut frame is individually
correct and only the grouping is wrong.

**The boundary came from report 02 rather than from a convention.** VMC marks
a frame with a clock message and this wire has none, so what stands in for one
is the measurement: a frame here is a **burst** — eight datagrams inside a
median 0.053 ms — with ~17 ms between bursts, three hundred times wider. Two
rules are stated rather than one, because they fail differently. *A repeat
closes the frame*, which needs no clock at all and survives loss: the frame
that lost `/tracking/trackers/1/rotation` — 96 % of this wire's single-address
loss — is closed by the next `head/rotation` exactly as an intact one is, and
comes out one sample short rather than merged with its successor. *A gap closes
the frame*, at a 5 ms window that sits ninety times above the burst and three
times below the interval. **On the recorded sender the gap gets there first**,
because the window is checked when a datagram arrives and 17 ms is past it
before the repeat inside that datagram is read — and the suite measures that
the two agree by running one stream twice with the window on and off and
comparing the frames sample for sample.

**A silence is not a restart, and that is the line the format change bought.**
The measured restart is a new ephemeral source port and nothing else, so the
policy is split along what is observable: a peer that differs **is** a restart
(reported, and the old session's trackers forgotten — never repaired), and a
silence of any length is `SOURCE_TIMEOUT` and nothing more. **A caller that
supplies no peer therefore never sees a restart**, which is the honest outcome
rather than a limitation: guessing one from silence would make every fixture
written before the `p` line report a session boundary nothing observed. The
corpus carries both halves — `session-restart` and `silent-gap` are the same
4.8452 s gap, told apart by identity alone.

**A partial sample is emitted and never repaired.** `hasPosition` and
`hasRotation` say which halves are real and the absent half is left at its
default, because a defaulted rotation of identity is bit-for-bit what a tracker
at rest reports — a reader that ignored the flag could not tell the invented
value from a measured one. `VRM_VRCHAT_OSC_TRACKER_PARTIAL` is raised here and
nowhere below, which is what the decode layer deliberately left to this one.

**Missing and stale carry no diagnostic code, deliberately.** The ten codes
were frozen before this directory existed and an eleventh is a contract change,
so a per-tracker absence is data on the frame where a consumer applies its own
policy — and both are measured against the trackers the session has *observed*
rather than the eight the surface defines, or a three-point setup would report
an incomplete frame fifty-eight times a second forever.

**A recalibration is told from motion by simultaneity, not by size**, and the
threshold is the one policy here with no measurement under it: no recorded take
contains a recalibration, so `calibration-jump` is marked `unobserved` and the
default's safety is arithmetic — 0.5 m in one frame period at 58 Hz is 29 m/s.
One tracker jumping is a tracking glitch and raises nothing.

Eleven mutations, each a plausible wrong *policy* rather than a syntax error,
each failing a case named for what it breaks, with the restored source green:
the gap rule removed, the repeat rule removed, a cross-datagram repeat read as
a duplicate, a long silence read as a restart, a restart that keeps the old
session's trackers, one jumping tracker read as a recalibration, a single
tracker allowed to be a simultaneity, staleness reported per frame rather than
per crossing, a partial sample reported as complete, the new-session flag
scoped to one `Push`, and the channel guard removed.

**Eight of the nine were caught on the first run, and the ninth is the one
worth recording.** "A restart that keeps the old session's trackers" passed
the suite unchanged, because the restart case ran the *same four trackers*
either side of the boundary — where an assembler that forgets everything and
one that forgets nothing produce identical frames. The case now restarts into
a three-point rig four metres away, which observes both halves of the policy:
the old rig is gone rather than reported missing, and the old positions go
with it, so a restart that moved does not raise `CALIBRATION_REQUIRED` for a
space that ended. The mutation found a hole in the test rather than in the
code, which is the outcome this repository's mutation passes exist to produce.

**A review found two more of the same kind, and the last two mutations are
them.** `beginsNewSession` was a local of `Push`, so it was *dropped* whenever
the datagram carrying the new peer contributed no message this layer accepts —
which port 9000 makes ordinary, since anything on the network may send to it
and `mixed-traffic` is a whole fixture of that shape. The diagnostics said the
session restarted and no frame said it began. It is now held on the assembler
until a frame opens. The second was memory rather than policy: the channel
indexes two fixed-width arrays, and a `TrackerChannel::Count` from a
caller-built packet — a supported way to drive this class — read and then wrote
past the end of both before the conversion's own guard could refuse it. It is
refused first now, as every caller-precondition failure in this adapter is.
**Neither is observable from a fixture**: both take a hand-built packet, so the
corpus could not have found them and the cases covering them are unit cases by
necessity. A third finding was a counter that could only ever be zero —
`framesRefusedEmpty`, for a state no code path reaches, since the only thing
that opens a frame is a message that has already converted — and it is gone
rather than documented.

Two things this milestone did not do. **No body role is named**, so assignment
(VRC-4a) is still entirely outside this adapter and the frame carries tracker
identities rather than regions. And **the corpus expectations moved rather than
the code**, twice, when a first run disagreed with them: `duplicate-and-reordered`
assembles to four frames because a repeat in its own datagram is the next turn
of the cycle by this layer's rule — so the twice-sent address opens a frame of
its own instead of a keep-first or keep-last policy choosing a value to lose —
and `tracker-dropout` reports `missing` and nothing stale, because the capture
is 0.086 s long against a half-second horizon.

### VRC-4a — tracker assignment policy

Which tracker is which body region, as a **generic** contract with an explicit
operator statement as its first and only required path
([§5.1](#51-assignment-is-a-third-thing-and-it-belongs-to-neither-end)). Its
home is named in the contracts before it is implemented, its behaviour on a set
it cannot place is stated with a fixture per outcome, and it contains no VRChat
address literal — a policy that did would have made "generic" a claim about
where a file sits.

Automatic assignment from rest geometry is **not** in this milestone. It is an
aid over this contract once the contract exists, in the same relationship
`motion_bvh_inspect`'s candidate profiles have to `--profile`.

**Done 2026-08-31** — [`libs/motionTracking`](../../libs/motionTracking), named
in the contract first ([§10](#10-contract-changes-this-plan-requires)) and
implemented second. It is the first library here that holds a *policy* and no
format at all: nothing in it reads a byte, and its whole surface is two
vocabularies and the rules relating them.

**A region is not a bone, and the check reads the sources to say so.** Every
other boundary rule in this repository is about an edge, and an edge is visible
on a link line; this one is about an **alias**, which leaves no link line to
fail on — so `TrackerRegion` is refused the `HumanBone` enumerators that are not
regions, in the sources, while `Head`, `Chest` and `Hips` are deliberately
absent from that list because a region named `Chest` is the point. The two rigs
that make the distinction real are the ordinary ones: a knee tracker sits on a
strap between two bones and there is no knee joint for it to be, and a chest
strap observes a ribcage rather than the joint a solve produces.

**Three answers, and the interesting one reads two directions.** `Refuse` ·
`Ignore` · `Hold` are `§5.1`'s three verbatim, and what implementing them
settled is that an observation can miss a statement two ways: a tracker the
statement does not place is *unplaced*, a stated tracker that did not arrive is
*absent*. `Refuse` reads the first and `Ignore` reads neither, so under both an
absence is data and a partial rig still assigns — the rule
`TrackerFrame::missing` already follows. **`Hold` reads both, and it has to**: a
rig coming up one device at a time is short of a *stated* tracker rather than
carrying an extra one, so a `Hold` watching only the unplaced side would never
fire for the case it exists for and would fire for the case waiting cannot fix.
`Refuse` and `Hold` are still two refusals rather than one, and the enumerator
is what a live caller acts on: `UnplacedTracker` will still be true next frame
so a caller stops and tells the operator, `Held` may not be so a caller keeps
the assignment it had. `NothingPlaced` catches an empty binding set no policy
objected to, which is what makes `Ignore` a refusal rather than a success with
nothing in it, and it is unreachable under `Hold` — stated in the enum rather
than pretended.

**Fourteen mutations and twelve boundary injections, and the mutation pass found
a guard no input could reach**: a second `=` in a statement needed no rule of its
own, because `head=hips` is already not a region this vocabulary carries and the
refusal below it already names what it saw. It is deleted rather than
documented, on VRC-4's precedent with `framesRefusedEmpty`.

**The prohibition that needed enforcing was the one in the *other* direction.**
`adapters/* -> motionTracking` is now a refused source token in all three
adapters' checks, and it is the first name on those lists that had to be: every
other one is also a link edge, so the CMake allowlist would catch it anyway,
while this package is enums and a policy over them — an adapter could include
its header and name `TrackerRegion` with no link line to fail on. That is this
library's own bone-enum argument read from the other end, and it was missed
until review.

Two things this milestone did not do. **It names no adapter and no adapter names
it** — the CLI that will hold both a tracker frame and an assignment is VRC-6's,
and `adapters/*/tools/* -> motionTracking` is a permission in the contract with
no taker yet. And **automatic assignment is still not here**, which is the
paragraph above holding rather than an omission.

### VRC-5 — the humanoid solve boundary

Tracker observations reach a `HumanoidPose`. Reuse the existing surface where
one exists; where none does, define a **generic** contract in the motion layer
rather than a solve inside the adapter ([§10](#10-contract-changes-this-plan-requires)).
No target-avatar-specific logic enters the adapter under any outcome — that is
[adapter plan §2](adapters-mocopi-vmc-ardy.md#2-what-an-adapter-is-allowed-to-be)
and it does not bend for a source that happens to need IK.

**Done 2026-08-31** — `SolveTrackerPose` in
[`libs/motionTracking`](../../libs/motionTracking), on the contract written the
same day and ahead of it ([§10](#10-contract-changes-this-plan-requires)). The
milestone's column changed with it: this is the **motion layer's**, not the
adapter's, and no adapter names it.

**The solve is direct, and the stopping point is stated rather than
approached.** It authors what it observed and infers nothing: an observed
orientation becomes the local rotation of the bone its region names, a joint
nobody observed stays at rest, and an observed **position** is consumed in
exactly one place — the hips, where the root/hips record already says what a
body translation observed at one place is. Every other position is *reported
unused*, which is the difference between a stopping point and a silent drop:
consuming a hand's position is IK, IK needs limb lengths, and limb lengths
belong to a target rig this layer does not have and
[§2](adapters-mocopi-vmc-ardy.md#2-what-an-adapter-is-allowed-to-be) will not
let it acquire. So the release claims tracker **input** reaching the canonical
layer, which is the second of the two branches
[the roadmap](current.md#done-when) offered.

**The invariant is what makes "stays at rest" a measurement.** A pose carries
rotations local to the semantic parent and a tracker reports a world
orientation, so each placed bone takes
`inverse(parent chain) * observed`, composed from what the solve has already
authored with every unauthored bone contributing identity. What follows is
testable for any rig: **composing the authored locals from the root down to a
placed bone returns the orientation that bone's tracker reported.** Adding a
chest strap mid-session changes what the head's local rotation *is* and not what
its world orientation is, which is the case that says the composition is a
composition rather than a copy.

**The knees and the elbows are refused, and that is this library's own argument
read forwards.** `TrackerRegion` exists because a knee is not a joint; this
solve's answer is that it does not know which of the two bones the strap
observes, because with no limb lengths a bent knee and a rotated thigh are the
same observation. They are reported in `unsolved` rather than refused — a rig
carrying them still produces a pose, and the operator is told which devices
drive nothing.

**One mutation proved the fixture wrong before it proved the code right.**
Fourteen mutations, and the composition-order one *survived*: the fixture turned
the hips and the chest about the same axis, and two rotations about one axis
commute, so a chain composed backwards reproduced the observation anyway. The
axis changed and the mutation was caught; the case is the only reason anyone
knows the check was vacuous. Nineteen boundary injections beside it, each
refused and each shown to pass without the injection.

**The boundary check became per file, which is what the one new edge cost.**
`motionTracking -> motionCore` is a link line the assignment half must not use,
and one static library links what it links — so the vocabulary and the
assignment are still scanned for a bone, for `motionCore` and for OpenUSD in any
form, the solve is scanned for everything except those, and the alias is
forbidden in both halves in either direction. **A file in neither half is an
error**, so the next file chooses its rules deliberately or not at all.

Two things this milestone did not do. **No IK**, which is the paragraph above
holding rather than an omission: an IK solve is a second function over the same
values, taking the rest pose this one refuses to invent and producing the same
`TrackerSolve`. And **nothing consumes it yet** — `adapters/*/tools/* ->
motionTracking` is still a permission with no taker, so no session has reached
an avatar by this path. That is VRC-6's tool and an operator's twenty minutes.

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
- ✅ **A capture can carry the only restart marker this wire has** *(landed
  2026-08-30, after being measured the same day)*. Measured
  ([report 02](../reports/motion/02-2026-08-30-vrchat-osc-address-inventory.md) §4):
  this sender marks a restart with a new ephemeral **source port** and with
  nothing else — no session identifier, no rest table, no handshake — and
  `liveTransport`'s capture format holds one `peer` in its header and none per
  datagram. The live session saw two peers; `--inspect` on that capture sees one.
  The asymmetry is deliberate and `UdpReceiver.h` states it; what changed is that
  it now has a cost, because every fixture-driven test of restart behaviour
  exercises the silence and not the identity change — and the silence alone
  cannot separate a session that paused from a second session that began, which
  is what `Reset` versus `Refuse` is made of.

  It was a **format** question and therefore not this adapter's to answer
  alone: a per-record peer, or a header list of them, touches
  `liveTransport`, three adapters, and every committed fixture in two
  corpora. It landed as its own change ahead of VRC-4, which is the
  milestone that first needs to tell the two apart.

  **A per-record peer, and the header list was never the alternative it
  looked like**: a list says a file saw two senders and still cannot say
  which datagram came from which, so it answers the count and not the
  question. The format gains `p <endpoint>`, naming the peer of every
  record after it until the next one, with `p -` for the peer that has
  gone away — carried forward rather than repeated, so a restart is two
  lines in a fixture and not one line per datagram.

  **No committed fixture changed a byte**, which is what made it a small
  change rather than a corpus migration: the writer emits a `p` only where
  the peer changes, so a capture whose records name nobody is written
  exactly as it was, and the format version stays at 1 because the reader
  compares it for equality — a bump would refuse both corpora outright.
  The `d` line is untouched too, so the strictness that refuses a fourth
  token on a record survives.

  The evidence is in both directions. `liveTransport_packetCapture` is the
  first test of this format in the library that owns it — the three
  adapters' suites test their own magic, and this line belongs to no
  adapter — and it fails at its first peer assertion against the reverted
  reader and writer. With the `--inspect` change reverted, the recorder's
  own harness reports `1 (192.168.1.8:51662)` for a two-session capture,
  which is the reading report 02 measured; with it, two. **What this does
  not do is re-record anything**: the native corpus's restart fixture was
  recorded before a file could carry a peer and still cannot say which
  session a datagram belongs to, so a restart fixture that carries the
  identity is a new recording rather than a re-read of an old one.
- ✅ **The assignment policy had no home, and neither end could be given it**
  *(landed 2026-08-31, in its own change ahead of the implementation)*.
  [§5.1](#51-assignment-is-a-third-thing-and-it-belongs-to-neither-end) separates
  three decisions and names an owner for two of them; the middle one had an
  owner described only by what it is not — generic, so not the adapter's, and
  naming regions, which `motionCore` has no vocabulary for. It is
  `libs/motionTracking`, a new identity on `liveTransport`'s and `osc`'s
  procedure: [WORKSPACE.md](../architecture/WORKSPACE.md) §1 row, §2 edges, §5
  side, written before a file existed.

  **Three things were decided rather than transcribed.** Its **edge set is
  empty**, `motionCore` included, and that is the row that carries the weight
  here rather than a tidy property: a region vocabulary that resolved to
  `HumanBone` would make assignment a lookup and leave the solve nothing to do,
  so §2 forbids the alias *by name* and the check reads the sources rather than
  the link line — an enum copied by hand leaves no link line to fail on. It
  takes the **product side** of §5's split, and it is the first identity where
  all three of that section's questions pass and no product member links it
  anyway: `motion_bvh_convert` is the shape of the tool that eventually will,
  so the exclusion `liveTransport` and `osc` carry is the wrong reading of an
  absent artifact. And it belongs to the **adapter's tool, never the adapter**,
  which is a different reason from `vrmRetarget`'s on the same line: retarget
  is refused there because a library that retargeted would be a second
  pipeline, and assignment is refused because it is not the adapter's decision
  to make.

  What the contract deliberately did **not** decide is the policy's *content* —
  what happens to an observed set it cannot place. That is behaviour with a
  fixture per outcome, and a contract that pre-empted it would be deciding an
  implementation it could not see, exactly as the transport ring's magic and
  timeout were left to OSC-2. VRC-4a decides it.
- ✅ **A tracker observation has no representation in the motion contract**
  *(answered 2026-08-31, in its own change ahead of VRC-5's code)*. `motionCore`
  carries `HumanoidPose`, which is post-solve, and the question was which of two
  answers to take: a generic tracker-sample type there, or nothing there at all
  and the observation stays in the adapter until it becomes a pose.

  **Neither, and the third answer was in the tree already.** The question was
  posed when the only two places to put anything were `motionCore` and an
  adapter; VRC-4a's `libs/motionTracking` is a third, and it is the layer that
  already holds two of [§5.1](#51-assignment-is-a-third-thing-and-it-belongs-to-neither-end)'s
  three decisions. So the observation lands beside the vocabulary and the
  assignment, where the solve that consumes it lives.

  **What settled it against `motionCore` is who would read one.** Every consumer
  of that header takes a pose — retarget, the trace format, the comparison
  semantics, the exec nodes — so a tracker sample there would be a value with
  no reader in the aggregate product, carrying an equality, a comparison and a
  trace-format obligation regardless, because
  [MOTION_CONTRACT.md](../design/MOTION_CONTRACT.md) requires all three of
  anything added to the value types. And what settles it against the adapter is
  the milestone itself: a solve inside an adapter is the second motion pipeline
  [adapter plan §2](adapters-mocopi-vmc-ardy.md#2-what-an-adapter-is-allowed-to-be)
  forbids, so an observation that never left the adapter would have kept the
  solve there with it.

  **The edge is the price, and it is one line.** `motionTracking -> motionCore`
  leaves the forbidden list for the solve alone
  ([WORKSPACE.md](../architecture/WORKSPACE.md) §2); the region vocabulary and
  the assignment keep the empty edge set they were given, and the alias
  prohibition is unchanged — what the boundary check does in exchange is scope
  its bone rule to the two files that must never name one, rather than drop it.
  **No VRChat-specific type enters `motionCore` under any outcome** still holds,
  and now holds trivially: nothing about a tracker enters it at all.
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

### One physical session is not available, and that is measured

**Amended 2026-08-30, from the VRC-1 session**
([report 02](../reports/motion/02-2026-08-30-vrchat-osc-address-inventory.md) §5).
The diagram above asks for one performance observed four ways. This product
cannot give it, for two reasons that compose:

- its **transfer format is exclusive**, so the native wire and `VRChat (OSC)`
  never run together; and
- it **records no BVH while sending OSC**, so two takes cannot be chained through
  a common file export either — which is the trick that made report 01 honest,
  since the native wire *does* allow an internal recording alongside it.

So the VRChat OSC path cannot share a physical take with **any** other
observation of the same motion. Report 01's comparison remains a genuine
one-take comparison of two paths; a comparison involving this path is between two
performances of one labelled sequence, and it is weaker in a specific and
statable way rather than in a vague one.

**What survives, and what does not.** Per-sample timing agreement does not: report
01's central result — a median 0.084° per bone once a 1667 ppm clock difference
was modelled — is meaningless between two performances, and no tolerance widening
rescues it. What survives is everything the comparison was actually for: which
canonical bones each path reaches, what each path carries and drops, coordinate
signs, scale, how each represents restart, and whether body travel arrives.
Those are properties of a path, not of a take.

**VRC-7's completion condition is therefore rewritten rather than deferred**: the
per-path carry/drop list is produced from separately-performed takes of the same
labelled sequences, and the report states which of its rows a shared take would
have strengthened. Waiting for a session this product cannot record would be
waiting forever, and saying so is cheaper than discovering it again in six
months.

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

> **It was not, and it was discovered there** (2026-08-30,
> [report 02](../reports/motion/02-2026-08-30-vrchat-osc-address-inventory.md)):
> three numbered trackers out of eight, plus a **named** `head` sharing the path
> position a number occupies. A decoder written from the specification would have
> read that segment as an integer, dropped the head, and reported nothing wrong.
> The mitigation worked exactly as designed and cost one session; this risk is
> closed as *realised and paid*, not as *avoided*.

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
