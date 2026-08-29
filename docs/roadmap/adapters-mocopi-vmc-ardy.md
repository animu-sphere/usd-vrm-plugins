# Input adapters — the VMC → mocopi → ARDY direction

The plan for `adapters/`: how real-time protocols, real capture devices, and
motion generators reach this repository's motion pipeline without any of them
reaching into it.

This document holds **boundaries, order, and completion conditions** only. Where
it touches structure it defers: adapter identities, dependency directions, and
artifact naming are settled in
[architecture/WORKSPACE.md](../architecture/WORKSPACE.md) §1, §2, §5, and motion
semantics in
[design/MOTION_ARCHITECTURE_POLICY.md](../design/MOTION_ARCHITECTURE_POLICY.md)
§8, §14, §15. Items this plan needs from those contracts are listed in
[§11](#11-contract-changes-this-plan-requires) rather than asserted here.

> **The order was reversed on 2026-07-29** — VMC first, the mocopi native
> adapter second. The reasoning is in [§1](#1-what-this-adds-and-why-in-this-order).
> The filename keeps the original triple so existing links and the v0.5.0
> changelog entry still resolve; it is not the implementation order.
>
> **Both pose leaves have shipped.** Milestone A–B landed in
> [v0.6.0](../releases/v0.6.0.md) and Milestones C–D in
> [v0.7.0](../releases/v0.7.0.md); what remains of them is evidence an operator
> produces, carried in [§10](#10-milestones). The
> [OpenExec foundation](openexec-foundation.md) was ordered behind this track
> because its parity comparison wants recorded sessions from a real device and
> real senders as input, and only this track produces those. What is still
> unbuilt here is the generation adapter, Milestones E–F.
>
> **Narrowed the same day: this plan is *live* input.** A capture product also
> writes recorded files, and reading those is a file-format problem with its own
> layering, its own diagnostics, and its own second-producer requirement. It is
> [recorded-motion-sources.md](recorded-motion-sources.md), and it is deliberately
> not a mocopi importer. This document keeps sockets; that one keeps files; they
> meet at `motionCore`. The one place they are compared is
> [§9.6](#96-cross-source-comparison), on a single session observed both ways.
>
> **Narrowed again on 2026-08-23: this plan is *pose* input.** A third live
> adapter is scheduled — VRChat OSC Trackers — and it is
> [osc-and-vrchat-trackers.md](osc-and-vrchat-trackers.md) rather than a fourth
> milestone here, for two reasons. It carries *tracker observations*, which are
> pre-IK, where every adapter in this document carries humanoid bone transforms;
> and it is the plan that owns the extraction of the OSC decoder and the
> transport code the two adapters here already duplicate (§4). This document
> keeps the two vendor pose leaves and the generator; that one keeps the shared
> floor under all of them.

Sequence context: the generic live-capture surface these adapters plug into
shipped in v0.5.0 (Motion Phase D). This plan is the vendor half of Motion
Phase D and the whole of Motion Phase F. It does not depend on the
[OpenExec foundation](openexec-foundation.md) in any direction that could block
it — [§3](#3-this-track-does-not-wait-for-openexec) states that as a commitment
rather than an observation — and the one dependency that does exist runs the
other way: this track's recorded corpus is that plan's parity input.

Legend: 🚧 in progress · ⬜ not started · ⛔ blocked

## 1. What this adds, and why in this order

Three inputs, added one at a time:

1. **A VMC Protocol adapter** — generic real-time input from any sender
   application. Shipped in [v0.6.0](../releases/v0.6.0.md).
2. **A direct capture-product adapter** (mocopi) — native input from a real
   device, built *after* the protocol path so the two can be compared. v0.7.0.
3. **A generation adapter** (ARDY) — generated motion behind a vendor-neutral
   generator contract. Unscheduled.

```text
VMC adapter ────┐
mocopi adapter ─┼─> motionCore / motionRuntime ─> vrmRetarget ─> VRM avatar
ARDY adapter ───┘
```

**Why VMC first.** It is not vendor-specific, so one adapter serves every sender
application — Virtual Motion Capture, VSeeFace, Warudo, Unity senders, in-house
senders, and capture products relayed through any of them. That buys six things
at once: a packet corpus that needs no hardware, deterministic replay tests,
early real-device validation through a mocopi application acting as a VMC
sender, compatibility evidence across senders, a CI-runnable stand-in for live
input, and — the one that matters later — **a common recorded input for the
offline/OpenExec parity comparison** (openexec plan P0-6).

**Why the direct adapter is second, not first.** The previous ordering put
mocopi first on the argument that only a real device produces evidence about
timestamp drift, tracking loss, dropped joints, confidence behavior, and
reconnection. That argument survives intact — but the same evidence arrives
through the relay path, without a second decoder, and it arrives as a *recorded
trace* CI can replay. So the protocol adapter comes first because it is the
cheaper way to reach a real device, not because the native one is optional.

**The native adapter is not conditional.** An earlier draft of this document
made it contingent: four triggers, and if the relay dropped nothing measurable
the native adapter would be deferred. That is retired. Building it is a decided
part of v0.7.0, and the relay comparison changed roles with the decision — it is
no longer a go/no-go, it is the phase's **distinguishing check**, the one that
says the two paths deliver the same motion and names every difference that
remains ([§6](#6-phase-2--the-mocopi-native-adapter)). A comparison that decides
whether to build something can only be run once; a comparison that verifies two
shipped paths runs on every recorded session, forever.

**Why generation is last.** Its prerequisite is a contract, not a device.

Each adapter is a leaf. Nothing in `motionCore`, `motionRuntime`, `vrmRetarget`,
`execMotion`, or `execVrm` learns that any of them exist.

## 2. What an adapter is allowed to be

An adapter's whole job is one conversion, and it ends at canonical motion:

```text
vendor / protocol input
        ↓  decode
        ↓  coordinate and unit normalization
        ↓  humanoid semantic mapping
        ↓  timestamp and confidence normalization
motionCore values  ->  motionRuntime source
```

It does **not** implement: target-skeleton discovery, retargeting, rest-pose
correction, its own interpolation, its own smoothing or filtering, an OpenExec
computation or its registration, `UsdSkelAnimation` authoring, per-frame stage
authoring, Hydra or usdview display, binding to a target avatar, use of the
importer's private canonical model, a dependency on a sibling adapter, or a
vendor branch inside the core.

Every one of those already exists once. An adapter that grows a second copy has
not extended the pipeline, it has forked it — and the fork is invisible until
two inputs disagree about the same avatar.

The adapter's **CLI tool** is a different matter: like `motion_retarget` and
`motion_capture`, it may drive `vrmRetarget` and author a stage. The library may
not. (WORKSPACE.md §2 states this as a dependency direction.)

### 2.1 A runtime route is not a build dependency

A mocopi application can act as a VMC sender, which makes one sentence easy to
write and wrong:

```text
runtime:   mocopi app  ->  VMC packet  ->  vrmAdapterVmc
build:     vrmAdapterVmc  ->  motionCore, motionRuntime
```

The first line is a data path a *user* assembles. It creates no edge in the
second. `vrmAdapterMocopi` handles native mocopi input and does not use
`vrmAdapterVmc` internally; `vrmAdapterVmc` knows nothing about which
application filled its datagrams. Collapsing the two is how a protocol adapter
acquires a vendor's assumptions
([WORKSPACE.md §2](../architecture/WORKSPACE.md), sibling rule).

## 3. This track does not wait for OpenExec

Everything below completes with no OpenExec dependency at all, and none of it
may be re-scoped around one:

```text
VMC / mocopi input · packet and SDK decode · canonical pose conversion ·
timestamped buffering · record and replay · filtering and interpolation ·
source switching · semantic clip authoring · offline retarget ·
UsdSkelAnimation output · corpus and golden tests
```

OpenExec connects *afterwards*, on top of a finished canonical pipeline:

```text
canonical motion snapshot  ->  execMotion  ->  execVrm  ->  evaluation result
```

**Since 2026-08-03 the schedule agrees with the dependency.** The OpenExec
foundation is v0.8.0 and this track's Milestones B–D are v0.7.0, so the recorded
sessions exist before anything compares against them:

```text
v0.7.0  recorded mocopi / VMC sessions  ──┬──>  offline pipeline
                                          └──>  v0.8.0 execMotion / execVrm
                                                      └── NearlyEqual ──┘
```

The arrow that matters is the one that is absent: nothing here reads back from
OpenExec. An adapter that could not be tested until an exec bundle existed would
have violated the paragraph above; supplying that bundle's input does not.

One consequence is worth stating because it would otherwise leak in as a hidden
blocker. This path has an independent upstream constraint in OpenUSD 26.08 —
`usdExecImaging` resolves prim adapters from a hard-coded list, so a `UsdSkel`
avatar cannot be displayed through the exec scene index
([migration report §8.2](../reports/openusd/26.08-openexec-migration.md#82-the-blocker-the-adapter-registry-is-hard-coded)):

```text
mocopi / VMC -> OpenExec -> UsdSkel joint transforms -> usdExecImaging -> usdview
```

**That display constraint is not part of any adapter's completion conditions.**
An adapter is done when a recorded session replays into a retargeted
`UsdSkelAnimation`, which is a claim this repository can verify on its own.

## 4. Layout

```text
adapters/
├─ liveCapture/
│  ├─ vmc/        include/vrmAdapterVmc/ · src/ · tests/fixtures/ · tests/corpus/
│  │              tools/ · CMakeLists.txt · openstrata.library.yaml
│  └─ mocopi/     include/vrmAdapterMocopi/ · src/ · tests/fixtures/ · tools/
│                 CMakeLists.txt · openstrata.library.yaml
└─ generators/
   └─ ardy/       include/vrmAdapterArdy/ · src/ · tests/fixtures/ · tools/
                  CMakeLists.txt · openstrata.library.yaml
```

The `liveCapture/` and `generators/` grouping is the one already fixed in
[WORKSPACE.md §1](../architecture/WORKSPACE.md); the source policy's flatter
sketch (`adapters/vrmAdapterVmc/`) is the same set of leaves and the contract
wins on structure.

The manifest is `openstrata.library.yaml`, and each `tools/` CLI carries an
`openstrata.tool.yaml` beside it. This document said `openstrata.plugin.yaml`
until 2026-07-29, which no adapter could ever have written: a plugin manifest
names an OpenUSD plugin kind and a `plugInfo.json`, and an adapter registers
nothing with OpenUSD. The reasoning and the corrected identities are in
[WORKSPACE.md §1](../architecture/WORKSPACE.md); nothing else in this plan moves
with it.

No `adapters/common/`. It gets extracted when two adapters demonstrably
duplicate code that carries no vendor semantics — after both exist, not in
anticipation.

> **That condition is now met, and the measurement is on the table** (2026-08-15,
> from the review of the mocopi bridge). `MocopiLiveSource` and `VmcLiveSource`
> are the same class apart from three stated differences: the restart-policy
> enumeration, the delivery loop, the diagnostic stamping, `ConsumeSessionRestart`,
> the `IMotionSource` forwarding and `Reset` are the same code with the same
> comments in both. Roughly 120 of 149 lines are shared behaviour maintained
> twice — and the two copies **had already diverged accidentally**: a datagram
> the decoder refused left the stale evidence window standing in the sibling and
> not in the new one, which is a bug that existed for eleven days in one copy and
> zero days in the other. That is the failure mode this rule was written to catch,
> arriving exactly on schedule.
>
> **It is deliberately not fixed in the change that found it.** `adapters/common/`
> is the wrong shape — [WORKSPACE.md §2](../architecture/WORKSPACE.md) forbids an
> adapter→adapter edge, and a shared leaf between two leaves is that edge wearing
> a hat. The candidate is a protocol-agnostic bridge beside `LiveCaptureSource`
> in `motionRuntime`, parameterised on the frame type, which is a **contract
> change**: it moves the restart-policy vocabulary into the motion layer, and
> [docs/README.md](../README.md) requires those to land in their own change before
> a plan depends on them. §11 carries it as such.
>
> Deciding it before `vrmAdapterArdy` is written is cheaper than after — but ARDY
> is a *generator* and may not have a datagram, a restart, or a session clock at
> all, so the third instance that would settle the shape may never arrive. Two
> instances and one measured divergence is the evidence available, and it is
> enough to schedule the question rather than enough to answer it here.
>
> **The third instance does arrive, and it is not ARDY** (2026-08-23). A VRChat
> OSC Trackers adapter has a datagram, a restart and a session clock, and it is
> [its own plan](osc-and-vrchat-trackers.md). It also arrives with a useful
> stress: a tracker stream's restart is not a sender's restart, so the
> parameterisation is settled by a third case that *disagrees* with the two
> existing ones rather than by one that copies them.
>
> **And the bridge is not the largest duplicate.** Widening the same measurement
> across the whole adapter pair
> ([the census](osc-and-vrchat-trackers.md#2-the-duplication-census), 2026-08-23:
> vendor identifiers erased, comments and blank lines stripped, then diffed)
> puts `PacketCapture` at **6 changed lines across 800** — one file written
> twice — and `UdpReceiver` at 161. The same measurement clears `FrameAssembler`
> and `SkeletonMap` completely — they differ by more lines than either copy
> contains, because assembling a frame *is* the protocol — so the census
> separates without a judgement call anywhere.
>
> **The receiver's divergence was documented before it was counted, and so was
> the trigger.** `vrmAdapterMocopi`'s `UdpReceiver.h` records that a review on
> 2026-08-11 found **four defects the sibling has identically, because they were
> copied along with everything else** — a silently truncated oversize datagram,
> a large finite timeout mapped onto "wait forever", an uninspected `revents`,
> and idle accounting charged to a call that had received something — fixed all
> four in the younger copy, and stated that they remain in the older one. All
> four are still in `vrmAdapterVmc` as of 2026-08-23. That file also names its
> own trigger, and names it exactly: *a **third** recorder — a third live
> adapter, or a tool that must drive both — is what turns the repetition into a
> library*. The OSC track is that third adapter arriving.
>
> That reorders the extraction rather than adding to it. The transport ring is
> duplicated **now**, and a third adapter's first deliverable is a packet
> recorder, so it is extracted *before* that adapter rather than after; the OSC
> decoder has only one consumer and keeps the rule as written, extracted after
> the second one proves the surface neutral. Where the shared transport can live
> is a narrower question than it looks: not `motionRuntime`, because
> `motion_capture` is in the aggregate product and §2 keeps a transport out of
> every product tool, and not `adapters/common/` for the reason above
> ([§3.2](osc-and-vrchat-trackers.md#32-the-transport-ring--extract-before-the-third-consumer)).

## 5. Phase 1 — the VMC Protocol adapter

**Goal:** one generic real-time input, so any sender application drives the
pipeline, and a recorded corpus exists that later phases — including OpenExec
parity — can replay.

```text
UDP datagram → OSC decode → VMC message decode → frame assembly
             → VRM bone mapping → HumanoidPose → LiveCaptureSource
```

| Component | Owns |
| --- | --- |
| `UdpReceiver` | socket, bind address/port, receive timestamps, size limits, socket diagnostics |
| `OscPacketDecoder` | OSC messages and bundles, type-tag validation, malformed rejection. **No VMC semantics.** |
| `VmcMessageDecoder` | VMC address patterns: bone pose, root pose, blend shape, look-at, status |
| `VmcFrameAssembler` | frame boundaries, timestamp association, partial frames, stale joints, duplicates, out-of-order packets, source reset |
| `VmcSkeletonMap` | VRM humanoid bone names → canonical semantics; unknown bones ignored or diagnosed, missing bones declared, source-model metadata kept |
| `VmcLiveSource` | pushes assembled frames at `LiveCaptureSource`, sets source metadata |

The OSC layer knowing nothing about VMC is what makes both testable: OSC has its
own malformed-input cases, and mixing the two produces a decoder that can only
be tested end to end.

**Implementation order — transport last.**

1. **Recorded packet decoder.** Produce frames from saved datagrams, so the
   adapter is CI-verifiable before a socket is opened.
2. **Semantic mapping.** Missing bones are passed to the existing missing-bone
   policy, never filled in by the adapter.
3. **Frame assembly.** The policy below, stated and tested.
4. **Live-source bridge.** Interpolation, filtering, and buffering come from
   `motionRuntime`.
5. **Thin receiver.** The UDP layer arrives last and stays separable, so the
   fixture-driven tests remain deterministic.

Building the receiver first is the tempting order and the wrong one: it makes
every subsequent test require a live sender.

### 5.1 The adapter does not resolve target joints

```text
forbidden:   VMC bone name -> a joint index in /Asset/skel/Skeleton

required:    VMC bone name -> HumanBone token -> canonical pose
             then, downstream:  VrmHumanoidAPI -> target mapping -> retarget
```

### 5.2 Frame assembly is a stated policy, not an emergent one

`VmcFrameAssembler` exists because VMC makes no promise that one datagram is one
frame. Each of the following is contracted and tested, rather than being
whatever the receive loop happens to do:

```text
packet arrival order          duplicate packet         sender restart
frame boundary                missing bone             sequence reset
late packet                   stale bone value         source clock drift
expression/body synchronization                 root transform update rate
```

The first version keeps no interpolation of its own: it resolves the received
state into timestamped samples and hands interpolation to `motionRuntime`.

First version covers body and root motion. Expression and look-at follow, in
step with Motion Phase G, so the adapter and the core grow that surface together.

Configuration is explicit, never inferred:

```yaml
listenAddress: "0.0.0.0"
listenPort: 39539
sourceModelPolicy: humanoid-bones
coordinateConversion: explicit
```

**Done when:** UDP receipt, OSC decoding, and VMC semantics are separable;
fixtures decode deterministically; body bone poses become canonical humanoid
semantics; the frame-assembly policy above is stated and tested rather than
emergent; malformed packets are refused with diagnostics and never crash the
receiver; source restart and timestamp reset are covered; a loopback sender
drives a pose end to end; LAN diagnostics exist; a real session records to a
trace and replays through the **unchanged** `motion_capture` and
`motion_retarget`; changing the sender application changes nothing downstream;
and no VMC-specific code has leaked into `motionCore` or `motionRuntime`.

## 6. Phase 2 — the mocopi native adapter

**Goal:** native **live** device input, with no third-party sender application in
the path — and a measured account of what the relay path does to the same motion.

```text
mocopi native UDP stream → vrmAdapterMocopi → canonical motion
```

> **This adapter is the live half only.** A capture product also writes recorded
> files, and those are not a mode of this adapter: a BVH file argues about a
> hierarchy, channel order, a frame time and a rest pose, where this argues about
> packets, arrival timestamps, restarts and tracking loss. The recorded half is
> [recorded-motion-sources.md](recorded-motion-sources.md), built as a generic BVH
> pipeline rather than as this product's importer, and the two meet at
> `motionCore` and nowhere earlier
> ([motion policy §8.3](../design/MOTION_ARCHITECTURE_POLICY.md),
> [WORKSPACE.md §2](../architecture/WORKSPACE.md)). Sharing a decoder between them
> is how a file reader acquires a socket's assumptions, or a socket a file's.
>
> They do meet again in one place that is worth the trouble: the **same physical
> session** can be captured live over UDP and exported to a file, so the two paths
> can be compared on motion that is genuinely the same. That comparison is
> [§9.6](#96-cross-source-comparison).

**Decided 2026-08-03: this is built, not gated.** The four triggers this section
used to list — metadata the VMC path drops, relay latency, confidence and sensor
state reaching the runtime's gating, and a direct connection with no third-party
sender running — are no longer conditions for starting. They are the list of
things the native path is expected to **recover**, and each one is a measurement
this phase owes rather than a hypothesis it tests first:

| What the relay may cost | What v0.7.0 records |
| --- | --- |
| SDK-specific confidence, sensor and device state | which fields exist natively and reach the runtime's confidence gating |
| latency through a sender application | the measured added latency, both paths, same session |
| timestamp quality | the native clock against the relayed one, and what alignment costs |
| root and hips semantics | what each path reports, against the open question in §5.2 |
| tracking loss and reconnect semantics | whether the relay can express them at all |
| operational dependency | that a session runs with no third-party application |

The two paths stay strictly separate in code no matter what the measurement
says: `vrmAdapterMocopi` does not use `vrmAdapterVmc` internally (§2.1), and a
relay-shaped assumption is exactly what a native decoder must not inherit.

| Component | Owns |
| --- | --- |
| `MocopiReceiver` | transport, receive timestamps, reconnection, transport diagnostics |
| `MocopiPacketDecoder` | packet syntax, joint and root samples, tracking state, malformed rejection |
| ↳ shipped as two | `PacketChunk` (the container: lengths and tags, nothing else) and `MotionPacket` (the two packet kinds), split for the reason the sibling splits OSC from VMC — a container has its own malformed-input cases, and a decoder that mixed the two could only be tested end to end. **Tracking state is not in this row's scope after all**: the measured grammar carries no per-joint confidence or state, so there is nothing here to decode into it (Milestone D) |
| `MocopiSkeletonMap` | source joints → canonical humanoid semantics; unsupported joints ignored, missing bones declared, source-name provenance kept |
| ↳ shipped as one header with the converter below | `SkeletonMap.h`, the sibling's arrangement, because the two are one question here: a joint id means nothing without the rig that declared it, so the map is *built from a skeleton packet* and validates the topology before it trusts an id. Three things it also owns and the plan did not name: the path composition for the five joints no canonical bone maps, the device's own **rest pose** (a relay sends none), and the hips translation as the body's placement (Milestone D) |
| `MocopiCoordinateConverter` | handedness, up axis, quaternion convention, translation units, root orientation — **all five measured as of 2026-08-12**: right-handed, +Y up, +Z forward, scalar-last (x, y, z, w), metres, root translation absolute. This component now converts a known basis rather than discovering one (Milestone D) |
| ↳ and the change of basis is the **identity** | Measured, not omitted: the device's basis and the canonical one are the same one, so nothing is permuted, mirrored or scaled and the only conversion is the quaternion's component order. Named and tested anyway — "nothing is converted" is a claim that can be wrong, and the sibling's answer to the same question is a reflection through X (Milestone D) |
| `MocopiLiveSource` | pushes decoded frames at `LiveCaptureSource`, sets source metadata |

`MocopiLiveSource` is a bridge. If it acquires buffering, interpolation, or
filtering, it has become a second motion runtime. `vrmAdapterMocopi` does not
use `vrmAdapterVmc` internally (§2.1), and its output meets the *same* canonical
contract — not a superset with vendor fields bolted on. Product-specific
metadata is isolated as provenance, never as a new value type.

The transport-last implementation order **does not** apply here, and the reason
is a property of this protocol rather than a preference: it is undocumented, so
there are no bytes to write a corpus from and the socket is the only way to
obtain any. The order is thin receiver → corpus → recorded decoder → mapping →
live-source bridge, amended 2026-08-11 and argued in
[Milestone D](#milestone-d--the-mocopi-native-live-adapter--v070-evidence-carried).

**Done when:** recorded fixtures yield a deterministic `HumanoidPose`; malformed
and truncated packets are refused with diagnostics; coordinate, unit, and
quaternion conventions are pinned by tests; confidence and tracking state reach
the runtime's confidence gating; missing bones reach the existing policy;
root-motion intake reaches the existing pipeline; a real session records and
replays through the **unchanged** tools; no mocopi name or branch has appeared
in any core library; and — the phase's distinguishing check — **the native and
VMC-relayed results for the same recorded motion differ only within a stated
tolerance**, with every difference outside it explained by a metadata field the
relay drops.

## 7. Phase 3 — the generation adapter (ARDY)

⛔ **Blocked on a contract, not on a dependency.** The vendor-neutral generator
interface is frozen first — `IMotionGenerator`, `MotionGenerationRequest`, text
intent, root waypoints, sparse joint constraints, pose history, output timing,
generated-clip representation, cancellation, timeout, diagnostics, provenance,
and cache key. The ARDY adapter must not be what defines them, or the first
generator's shape silently becomes the contract.

```text
MotionGenerationRequest → ARDY adapter → HumanoidAnimation / pose stream
                        → motionRuntime → vrmRetarget → VRM avatar
```

| Component | Owns |
| --- | --- |
| `ArdyClient` | process/service/API transport, timeout, cancellation, retry, diagnostics |
| `ArdyRequestEncoder` | vendor-neutral request → ARDY-specific request |
| `ArdyResponseDecoder` | ARDY output → adapter-local representation |
| `ArdyMotionMapper` | → `HumanoidPose` / `HumanoidAnimation`, root motion, per-joint confidence, source timing, generation provenance |
| `ArdyGenerator` | a thin `IMotionGenerator` implementation |

Generated motion goes to **canonical humanoid semantics**, never straight to a
target avatar's joint order — so replacing the generator leaves everything
downstream untouched. That indirection is the entire point of the phase.

**Done when:** the vendor-neutral contract exists first; request/response
fixtures give deterministic tests; generated motion converts to canonical
semantics; the adapter does not know the target VRM's joint order; retarget is
delegated to `vrmRetarget`; timeout, cancellation, and malformed output are
diagnosable; provider metadata and generation provenance survive; no network or
model dependency has leaked into the core; and swapping ARDY for another
generator changes nothing downstream.

## 8. Diagnostics

Each adapter owns a diagnostic namespace; none of them are core codes. The
vendor/protocol layer and the canonical layer are separate namespaces, so a
reader can tell a decode failure from a motion-contract violation without
knowing which adapter produced it:

```text
VRM_VMC_PACKET_MALFORMED        VRM_VMC_UNSUPPORTED_MESSAGE
VRM_VMC_TIMESTAMP_REGRESSION    VRM_VMC_DUPLICATE_BONE
VRM_VMC_INCOMPLETE_FRAME        VRM_VMC_SOURCE_RESTARTED
VRM_VMC_SOCKET_BIND_FAILED      VRM_VMC_STALE_JOINT

VRM_MOCOPI_SOCKET_BIND_FAILED   VRM_MOCOPI_TRACKING_LOST
VRM_MOCOPI_DEVICE_UNAVAILABLE   VRM_MOCOPI_TIMESTAMP_INVALID
VRM_MOCOPI_UNSUPPORTED_JOINT    VRM_MOCOPI_SOURCE_RESTARTED
VRM_MOCOPI_PACKET_MALFORMED     VRM_MOCOPI_FRAME_INCOMPLETE
VRM_MOCOPI_NON_FINITE_TRANSFORM

VRM_ARDY_REQUEST_REJECTED       VRM_ARDY_TIMEOUT
VRM_ARDY_OUTPUT_MALFORMED       VRM_ARDY_UNSUPPORTED_SEMANTIC
VRM_ARDY_GENERATION_CANCELLED

VRM_MOTION_SAMPLE_STALE         VRM_MOTION_REQUIRED_BONE_MISSING
VRM_MOTION_NON_FINITE_TRANSFORM VRM_MOTION_INVALID_ROTATION
```

The `VRM_` prefix was added on 2026-07-29; an earlier draft of this document
used bare `MOCOPI_*` / `VMC_*` / `ARDY_*`, which is the only diagnostic family
in the repository without it (compare `VRM_RETARGET_*` in the
[OpenExec plan](openexec-foundation.md) P1-1 and the importer's `VRM###`
codes). `VRM_OPENEXEC_*` is a **separate** namespace owned by that plan, not by
any adapter, and so is `VRM_BVH_*`
([recorded motion sources §6](recorded-motion-sources.md#6-diagnostics)) —
a file syntax error and a dropped packet are not the same class of event, and a
reader should not have to know which adapter it might be compared against.

The `VRM_MOCOPI_*` set gained four codes on 2026-08-03, so that it covers the
same ground the VMC set does rather than a subset of it: a bind failure, a source
restart, an incomplete frame, and a non-finite transform are all things the VMC
adapter learned it needed and there is no reason a second live adapter would not.
The set is still frozen before its decoder, which is the point — a code list
written afterwards describes whichever failures were hit first.

A diagnostic carries, where it applies: code, severity, source, timestamp,
joint or message name, packet sequence, a recoverable flag, and human-readable
detail. The recoverable flag matters more here than elsewhere — a live session
that can continue through a dropped packet should not be reported the same way
as one that cannot.

## 9. Testing

### 9.1 Unit tests

Transport and semantics separate, in that order:

```text
raw fixture → decoder test → mapping test → frame-assembly test → runtime bridge test
```

`vrmAdapterVmc`: OSC type mismatch · malformed address · invalid float · missing
field · duplicated bone · packet ordering · coordinate conversion · quaternion
normalization · stale state · sender reset.

`vrmAdapterMocopi`: native joint mapping · confidence conversion · tracking loss
· coordinate basis · timestamp conversion · unavailable-device behavior.

### 9.2 Corpus

The minimum **recorded** set, per live-capture adapter — from real senders and
real devices, which is Milestone B's work:

```text
neutral-standing           tracking-loss-and-recovery
head-turn                  expression-only
arm-raise                  body-and-expression
walk-root-motion           sender-restart
partial-upper-body         out-of-order-packets
```

The generated captures that landed with the packet format are not this set and
do not substitute for it. They reproduce the protocol's *shapes* — a bundled
frame, an unbundled one, ignorable traffic, ten packet-level refusals, a
duplicate, a backwards sender clock, a restart — so that a decoder is testable
with no hardware and no socket. What a real sender actually emits is evidence
only a real sender can give.

The corpus manifest carries **measured fields** and the generating tool's
`--check` mode verifies the committed traces still match — the same convention
the importer corpus and `tools/baseline_freeze.py --check` already use, so a
silently re-recorded fixture is a failing test rather than a quiet baseline
move.

#### Recorded evidence lives beside the generated corpus, never inside it

```text
adapters/liveCapture/<adapter>/tests/corpus/
├─ generated/     protocol shapes, committed, CI-runnable, no hardware
└─ recorded/
   ├─ redistributable/   real sessions cleared for publication
   └─ manifests/         everything else, as measured facts
```

A session that cannot be redistributed leaves **no bytes here at all**. It leaves
a manifest, and the manifest has to be enough to tell whether a claim still holds
without them: capture hash · recording tool version · sender identity and
version · device or relay identity · the measured statistics · expected
diagnostics · expected frame and pose counts · validation date · redistribution
status. Where the bytes are kept is the operator's business and is not recorded
in the repository.

This is the same split the VRM corpus already runs for models it cannot ship, and
it exists for the same reason: a fixture nobody can fetch is not evidence, but a
measurement nobody can reproduce is at least a claim with a date on it.

The **minimum recorded set for v0.7.0**, across mocopi and at least two more
senders of different shape:

```text
mocopi-neutral-standing        vmc-tool-a-neutral
mocopi-head-turn               vmc-tool-b-neutral
mocopi-arm-raise               vmc-tool-b-unbundled-or-different-clock-shape
mocopi-walk-root-motion        vmc-long-session
mocopi-tracking-loss-recovery
mocopi-sender-restart
```

Recorded where the session offers them, because each closes a question the
generated corpus was written around rather than into: `partial-upper-body` ·
`root-update-rate-differs` · `30hz-session` · `60hz-session` ·
`two-peer-observation` · `relay-reconnection`.

### 9.3 Integration

```text
VMC:     test sender → UDP loopback → adapter → motion-capture-trace → motion_capture → motion_retarget
mocopi:  recorded packets → adapter → motion-capture-trace → motion_capture → motion_retarget
ARDY:    request fixture → fake/recorded endpoint → adapter → HumanoidAnimation → motion_retarget
```

All three end at the *existing* tools, unchanged. That is the check that an
adapter really did converge on the common representation rather than build a
private path to the avatar. Once `execMotion` exists, the same VMC trace is also
the shared input for the offline/OpenExec parity comparison — one recorded
corpus, two evaluation paths, compared numerically.

### 9.4 Artifact-only

Run against the packaged artifacts alone, with no repository source tree and no
build tree on the path: adapter tool startup · trace replay · semantic clip
authoring · `motion_retarget` · plugin discovery · embedded resource resolution ·
(later) OpenExec computation discovery. This is the same gate
`scripts/clean_install_smoke.py` applies to the importer bundles.

### 9.5 Lanes

| Lane | Trigger | Hardware |
| --- | --- | --- |
| `adapter-unit` | every PR | no |
| `adapter-generated-corpus` | every PR | no |
| `adapter-recorded-public-corpus` | every PR | no |
| `adapter-integration-loopback` | every PR (both adapters) | no |
| `adapter-hardware-opt-in` | manual / scheduled | **yes** |
| `artifact-only-adapter-smoke` | release, and PRs where practical | no |

**The hardware lane is never a required PR gate**, and its purpose is not to be
green. Its purpose is to turn one session in front of a device into the two
things this repository can keep: a capture and a measured manifest. A device is
then needed once per behavior rather than once per run, and every subsequent run
replays bytes.

That is also why a red hardware lane is not a release blocker in itself — a
device that behaves differently from last time is a finding, and the response is
a new capture and an amended manifest, not a retry.

### 9.6 Cross-source comparison

One physical session can be observed three ways, and a device that writes files
as well as sending packets is what makes the comparison honest — the motion is
genuinely the same, not merely similar:

```text
                    ┌─> mocopi UDP     -> vrmAdapterMocopi ─┐
one recorded ───────┼─> VMC relay      -> vrmAdapterVmc ────┼─> canonical motion
session             └─> mocopi BVH     -> motionBvh ────────┘        ↓
                                                              compared here
```

Compared at the canonical layer, never at a decoder's: sample timing · bone
rotations · root translation · missing joints · provenance · metadata loss · how
each path represents tracking loss. **Latency is a live-path measurement only** —
a file has none, and reporting one for it would be inventing a number.

`NearlyEqual` for motion equivalence, `operator==` for recorded-value identity
([MOTION_CONTRACT.md](../design/MOTION_CONTRACT.md#comparison-semantics-v060)).
A difference outside tolerance is classified before it is accepted, and the useful
outcome of this comparison is not a green test — it is the list of what each path
cannot carry, written down once, from evidence.

The BVH half of this is planned in
[recorded-motion-sources.md](recorded-motion-sources.md#7-testing); it appears
here too because the comparison belongs to neither plan alone.

**The first comparison has happened, on the easy half** (2026-08-12). The UDP
path's rest skeleton and the mocopi BVH export's rest skeleton are the same 27
offsets, sign for sign, to 4.4e-7 m once centimetres and metres are reconciled —
two recordings a week apart, two transports that share no code. That is what
settled handedness for the live path (Milestone D), and it is worth recording
here as the first data point this section has.

It is also the *weakest* form of the comparison, and calling it more would be a
mistake. A rest pose is a calibration the application computes once; agreeing
about it says nothing about whether the two paths agree about a session's
movement, its timing, or what each drops.

**The real comparison happened on 2026-08-15, on two of the three paths**
([report 01](../reports/motion/01-2026-08-15-mocopi-cross-source.md)). One
physical session was recorded as UDP and exported as BVH over the same window,
and both halves were driven to a canonical clip and compared there.

- **They agree.** Both reach the same 22 canonical bones with neither carrying
  one the other lacks; the median of the per-bone median differences is
  **0.084°**, and no bone's median exceeds 0.13°.
- **The residual is timing, shown rather than asserted** — which is what this
  section asks for. Over 19759 bone-samples where the bone was turning slower
  than 0.05°/frame the median difference is **0.0000°**; the twenty worst
  samples in the whole comparison are all in the first 13 frames and each
  implies the same 3.7–4.4 frame lag. A residual that vanishes when the body is
  still and grows with speed is a sampling instant, not a decode.
- **The two clocks are not one clock**, which the alignment had to discover
  before it could say anything else: 3 frames of slip over 1800, about
  **1667 ppm**, between one application's two outputs of one session.
- **The list this section exists for is written**, in that report's §4. The
  entry that mattered was the body's travel: **4.81 m of hips path** reached the
  recorded path and nothing at all reached the live one, because no layer there
  composed a `RootMotion` while §5.2 was open. That entry was a fact about the
  open record rather than about the protocol, and it stopped being true on
  2026-08-23 when the record closed and `BodyPlacementPolicy::HipsOnly` became
  the assembler's default. The other four entries stand, and the comparison this
  section asks for gains a row it could not have had: the two paths' root
  motion, which are now the same canonical value.

**A VMC relay was not recorded**, so the third path is still unobserved and this
comparison is two of three.

**A fourth path is planned**, and it changes what the comparison can attribute
rather than only how many rows it has. The same capture product also emits
VRChat OSC, and that path is the only one of the four where the pose is
*solved* from tracker observations rather than transported as bone transforms —
so a difference there is attributable to the solve, which no difference between
the native and BVH paths ever was. It adds one classification category and one
plan: [osc-and-vrchat-trackers.md §11](osc-and-vrchat-trackers.md#11-the-fourth-observation-of-one-session).
The requirement it inherits from this section is the one that is easy to lose in
a device session: all four observations have to come off the **same physical
take**, or the comparison is between two performances.

## 10. Milestones

| Milestone | Release | State |
| --- | --- | --- |
| A — VMC decoding | v0.6.0 | shipped, one item carried |
| B — VMC live receipt | v0.6.0 code, v0.7.0 evidence | shipped; four items carried, and each needs a real sender |
| C — capture integration and offline E2E | v0.7.0 | shipped, one item carried |
| D — the mocopi native adapter | v0.7.0 | shipped; the operator evidence is carried |
| E — the generator contract | unscheduled | ⬜ |
| F — the generation adapter | unscheduled | ⬜ |

**What shipped is not restated here.** Milestones A–D are recorded per
capability in the [delivery history](../reports/delivery-history.md) §I and §J,
per release in [v0.6.0](../releases/v0.6.0.md) and
[v0.7.0](../releases/v0.7.0.md), and per decision in the two adapter READMEs and
the headers beside the code — which is where a reader of the code will look.
What stays below is what is **still open**, with the reason it could not close.

Milestone B was split across two releases on purpose. Everything in it that can
be verified from committed bytes shipped with v0.6.0; everything in it that needs
an operator, a sender application, or a device is the evidence half, and no
amount of code closes those. The carried items below all sit on that same line,
and the ones that are release conditions are tracked in
[current.md](current.md#carried-out-of-v070--evidence-an-operator-produces).

### Milestone A — VMC decoding ✅ (v0.6.0, one item carried)

`adapters/liveCapture/vmc` scaffold · recorded packet fixture format · OSC
decoder · VMC message decoder · bone mapping · coordinate conversion · canonical
pose output · diagnostics · deterministic unit tests.

Two questions the skeleton map deliberately left open were Milestone B's to
settle, and both are settled: a VMC bone rotation is the sender's local rotation
(a sender whose humanoid rest is not identity needs `vrmRetarget`'s
`SourceRestPose`, never a rest manufactured from the first frame), and how a
hips offset composes with `/VMC/Ext/Root/Pos` is the record in
[`MOTION_CONTRACT.md`](../design/MOTION_CONTRACT.md#root-and-hips-v070).

- ⬜ **An unknown bone name is unit-tested and not in the corpus.** The refusal
  path (`VRM_VMC_UNSUPPORTED_MESSAGE`, the bone dropped and the frame kept) has
  no recorded capture behind it, and inventing one would be guessing at what a
  sender emits — which is the same argument §9.2 makes about the rest of the
  generated set. **Carried to Milestone B**, with the other two paths of the
  same shape; it is the one item of this milestone that no amount of code
  closes.

### Milestone B — VMC live receipt ✅ code (v0.6.0) · 🚧 evidence

UDP receiver · frame assembler · stale/partial/out-of-order policy · source
reset · `LiveCaptureSource` bridge · loopback integration test · VMC record tool
· recorded trace corpus — shipped. A hosted runner does allow a loopback socket
on all three OS, so the socket path is covered by a PR lane rather than by an
opt-in one. The four transport defects this receiver shared with the mocopi one
were merged and fixed in OSC-1
([the OSC track](osc-and-vrchat-trackers.md#9-milestones)),
and the transport ring itself now lives once in `libs/liveTransport`.

What is open is what a real sender has to emit:

- ⬜ **The receive-clock fallback and the unknown bone have no capture.** A frame
  with no `/VMC/Ext/T` is stamped from arrival and flagged `timestampFromSender`;
  a bone name outside the Unity vocabulary costs that bone. Both are unit-tested
  and neither is in the corpus, for the reason §9.2 gives about the rest of the
  generated set — what a real sender emits is evidence only a real sender can
  give.
- ⬜ **The repeat rule assumes a sender's bone set does not change**, and the
  capture that would test it is `tracking-loss-and-recovery` from §9.2's
  recorded set. A bone the open frame does not already carry joins it whether or
  not the frame has met its clock, so an unbundled sender whose *first* message
  of a new frame is a bone the previous frame lacked hands that bone backwards
  one frame. The rule that would repair it — content after the clock begins a
  new frame — is true of the unbundled sender and false of the bundled one,
  which is the same asymmetry that made the clock's position unusable to begin
  with, so this is a limit of what a boundary can be inferred from rather than a
  defect with a fix. It is narrow in practice (a Unity sender walks
  `HumanBodyBones` in a fixed order, so only a *leading* bone going away and
  coming back reorders anything) and it is pinned by a characterisation test, so
  a later third rule has to change the header before it changes the behaviour.
- ⬜ **Real senders, of deliberately different shape** — a mocopi relay, a
  general avatar tracker, and a VTuber or DCC application, plus the repository's
  own loopback sender as the deterministic control. The roadmap names categories
  rather than products on purpose: the property being bought is *different sender
  behavior*, and pinning product names here would age faster than the document.
  The chosen tools are fixed at validation time and recorded in the manifest,
  because a sender's behavior is a property of its version.

  > **Best-effort as of 2026-08-03, not a release gate.** This was a numbered
  > v0.7.0 release condition until the BVH axis was added to that release. It is
  > still the right work and it is still where these questions get answered — a
  > sender that is available gets recorded and reported — but no release waits on
  > lining up two or three applications. What replaces it as the gate is
  > [§9.6](#96-cross-source-comparison), which needs only the device: one
  > session, observed natively and as a file, with a VMC relay added where one is
  > running.

  Per sender, recorded rather than described: application and version · platform
  · VMC output settings · sample rate · bundle usage · where `/VMC/Ext/T` sits
  and how often · root update frequency · the observed bone set · which messages
  it emits that we do not implement · restart behavior · packet count · refused
  message count · emitted frame count · admitted pose count · stop condition.

  Four of those lines are the ones the generated corpus could not produce, and
  each corresponds to a rule this adapter decided from one sender shape:
  `/VMC/Ext/T`'s position (the frame boundary), the observed bone set (the repeat
  rule, and the staleness horizon it is measured against), root update frequency
  (untested for want of a sender that varies it), and the unsupported message set
  (`VRM_VMC_UNSUPPORTED_MESSAGE` has no recorded capture behind it at all).
- ⬜ **The three unit-tested paths with no capture behind them** close here or
  are stated as unclosable: an unknown bone name, the receive-clock fallback when
  a frame carries no `/VMC/Ext/T`, and the leading-bone reordering the repeat
  rule's characterisation test pins. Each needs a real sender to *emit* it;
  inventing a capture would be the guessing §9.2 argues against.

### Milestone C — capture integration and offline E2E ✅ (v0.7.0, one item carried)

VMC session → `motion-capture-trace` · that trace → canonical samples → semantic
clip reproducible in CI · semantic clip + VRM avatar → **unchanged**
`motion_retarget` → retargeted `UsdSkelAnimation`.

The first item read "`motion_capture` accepts a live VMC source" until §11
costed that edge and rejected it. The integration is a file rather than a link:
`vmc_record --export-trace` ends the adapter's half at the canonical trace, and
every tool after it is one the product already ships and this milestone does not
touch. That is why the third item can say *unchanged* about `motion_retarget`
and the first can say it about `motion_capture` too.

- ⬜ **The artifact-only leg is not done.** Nothing here has been run from
  packaged artifacts. An adapter artifact is producible as of `ost` 0.22.3, so
  what remains is the run and the decision about whether a release carries one —
  both in [current.md](current.md#carried-into-v080).

### Milestone D — the mocopi native live adapter ✅ (v0.7.0, evidence carried)

`adapters/liveCapture/mocopi` scaffold · packet-capture fixture format · thin UDP
receiver · packet decoder · joint mapping · coordinate conversion · frame
assembly · `LiveCaptureSource` bridge · the loopback corpus · `mocopi_record` ·
**the cross-source comparison of §9.6** ([report
01](../reports/motion/01-2026-08-15-mocopi-cross-source.md)).

**The build order was Milestone A's and is not** — amended 2026-08-11, after the
receiver landed first. It was planned as recorded decoder → mapping →
live-source bridge → thin receiver, with the transport last so every layer below
it stayed testable from committed bytes. That order depends on a premise this
protocol does not supply: the VMC Protocol is published, so its corpus could be
*written*, and this one's cannot. What replaced it is **thin receiver → corpus →
recorded decoder → mapping → live-source bridge**. The device is still needed for
recording sessions rather than for running tests, which is the part of the
original order that was never about the transport.

**The wire format is not documented, and the evidence path is a sender rather
than a device** (established 2026-08-09). The vendor states the transport and
stops: UDP, port 12351 by default, IPv4 only, unencrypted. What is published
instead is Apache-2.0 **source** and a **`BVH Sender`** application that
transmits a BVH file over the same UDP format — and that produces a capture with
no device at all: pointed at a `.bvh` this repository wrote, it yields bytes
whose *encoding* is the vendor's and whose *content* is ours. That is the only
route to a committable, publicly CI-runnable mocopi capture, which is why
[the redistributable-capture carry-over](current.md#carried-out-of-v070--evidence-an-operator-produces)
names `BVH Sender` rather than a phone. Such a capture belongs in `generated/`
with its provenance saying what produced it: its bytes are genuinely the
vendor's protocol, so it is not generated in the sense the VMC corpus is, and
its motion never met a sensor, so it is not recorded evidence either.

Two things this adapter does **not** get to do, worth keeping because a native
adapter is exactly where the temptation appears. It does not grow buffering,
interpolation, or filtering — `MocopiLiveSource` is a bridge, and a bridge that
acquires those has become a second motion runtime. And it does not widen the
canonical contract: product-specific metadata is isolated as provenance, never
as a new value type, so its output meets the *same* contract as
`vrmAdapterVmc`'s rather than a superset of it.

- ⬜ **Tracking state, reconnection, and an opt-in real-device test.** A real
  source *restart* is recorded from hardware; *tracking loss* proved
  unproducible on this product, so `VRM_MOCOPI_TRACKING_LOST` stays frozen and
  unraised and the explicit tracking state §11 asks `motionCore` for is not
  built. Closing this is a recovery a device can actually produce, or a decision
  that this product cannot — [current.md](current.md#carried-out-of-v070--evidence-an-operator-produces).

### Milestone E — the generator contract ⬜

`IMotionGenerator` · `MotionGenerationRequest` · canonical generator output ·
cancellation · timeout · diagnostics · provenance · a fake generator to test
against

### Milestone F — the generation adapter ⬜

`adapters/generators/ardy` scaffold · request encoder · response decoder ·
canonical motion mapping · fake/recorded endpoint tests · CLI prototype ·
retarget integration test · provider metadata · packaging

## 11. Contract changes this plan requires

Structural claims belong in the contracts, in their own change, before this plan
depends on them ([docs/README.md](../README.md)). What has landed is in the
contracts themselves — adapter identities, dependency directions and artifact
names in [WORKSPACE.md](../architecture/WORKSPACE.md) §1, §2 and §5; the VMC-first
ordering and the datagram-queue hand-off in
[motion policy](../design/MOTION_ARCHITECTURE_POLICY.md) §8.2 and §11.4;
deterministic comparison in
[MOTION_CONTRACT.md](../design/MOTION_CONTRACT.md#comparison-semantics-v060).
One of them is worth keeping visible because it is a rule rather than a
capability:

- **`motion_capture` does not grow a live source, and the hand-off is a file.**
  This item read "Milestone C adds `--source vmc --listen <addr>` alongside
  `--replay`" until the change was costed. `motion_capture` is a product tool and
  every adapter is excluded from the product
  ([WORKSPACE.md §5](../architecture/WORKSPACE.md)), so the edge would have
  pulled a protocol decoder and a product name into the aggregate artifact —
  once per adapter, because `--source vmc` invites `--source mocopi` behind it;
  no tool in the product opens a transport, which is what makes every clip
  reproducible by construction; and the adapter would have stopped being
  separately shippable. What is built instead was already the format's job:
  `<adapter>_record --export-trace` writes what the adapter delivered as a
  `motion-capture-trace` — *after protocol decode and coordinate conversion,
  before any intake policy* (`motionRuntime/CaptureTrace.h`) — and
  `motion_capture` replays it unchanged, knowing nothing about VMC. The cost is
  that a live session is two commands. Settled 2026-08-04, in the contract
  before the code, and it is the reason a release can require that a session
  reach an avatar through **unchanged** `motion_capture` and `motion_retarget`.

Still owed:

- ⬜ An explicit **tracking state** in `motionCore`, so "tracking lost" is
  distinguishable from "zero pose" and from "bone absent". Milestone D found the
  raising side unproducible on one product, which is why this is still unbuilt
  rather than why it is unnecessary.
- ⬜ An **expression sample**, which Motion Phase G owns but which the VMC
  blend-shape messages reach first.
- ⬜ **The two live-source bridges are one class, and where the shared one lives
  is a contract question.** Measured 2026-08-15 (§4): `MocopiLiveSource` and
  `VmcLiveSource` differ in three stated places and are otherwise the same code,
  and the two copies had already diverged accidentally. The candidate is a
  frame-type-parameterised bridge beside `LiveCaptureSource` in `motionRuntime`,
  which moves the restart-policy vocabulary — `Reset` versus `Refuse`, and the
  rule that splicing the two sessions is offered nowhere — out of the adapters
  and into the motion contract. That is
  [MOTION_CONTRACT.md](../design/MOTION_CONTRACT.md)'s to state and
  [WORKSPACE.md §2](../architecture/WORKSPACE.md)'s to permit, in its own change,
  before either adapter is rewritten against it. **Its third instance is
  scheduled** — a VRChat OSC adapter has the datagram, restart and session clock
  ARDY may not
  ([the OSC track §3.3](osc-and-vrchat-trackers.md#33-the-live-source-bridge--already-scheduled-and-the-third-instance-settles-it)).
  This is the one duplication of §4 that the OSC track has *not* already
  resolved: the transport ring left both adapters for `libs/liveTransport`
  (OSC-2, 2026-08-24) and the OSC decoder for `libs/osc` (OSC-3, 2026-08-29).
- ⬜ **The generator contract has no home yet.** `IMotionGenerator` and
  `MotionGenerationRequest` are named in motion policy §16 Phase F as
  deliverables, but the interface itself will need a contract document before
  Milestone E, in the way [MOTION_CONTRACT.md](../design/MOTION_CONTRACT.md)
  serves the shipped motion foundation.
- ⬜ **`adapter-hardware-opt-in` has no expressible lane shape.** The other three
  lanes of §9.5 need none: `ost` 0.21.0's `kind: workspace` cells picked the
  adapters' tests up on all three OS with no CI edit at all, including the two
  that bind a socket. The hardware lane is the one that must never gate a PR,
  and nothing in the CI contract says that yet.

Two items that stood here through v0.7.0 have since closed, and are recorded
where they landed rather than repeated: plain-library discovery now reaches an
adapter, so the graph gate validates a declared adapter edge
([report 35](../reports/ost/35-2026-08-24-v0.22.2-release-artifact-membership.md) §1),
and `ost library package` composes `requires.libraries`, so an adapter is
separately shippable
([report 36](../reports/ost/36-2026-08-25-v0.22.3-canonical-runtimes-and-release-membership.md)).
Whether a release *carries* an adapter artifact is a decision, not a tool, and it
is [current.md](current.md#carried-into-v080)'s.

## 12. PR splitting

One PR never introduces an adapter boundary and a large feature together. The
order below is also the review order — each step is checkable on its own:

1. adapter directory, manifest, and build scaffold
2. recorded input format and fixtures
3. decoder
4. semantic mapping
5. frame assembly
6. runtime bridge
7. receiver / client
8. CLI
9. integration tests
10. packaging
11. documentation

Every one of them checks: standalone build · dependency direction · no reverse
dependency · no vendor-name leakage · deterministic fixture tests · diagnostic
stability · clean install · package closure.

## 13. Release boundaries

Adapters are never part of the aggregate product
([WORKSPACE.md §5](../architecture/WORKSPACE.md)), so each of these is its own
shippable boundary:

| Milestone | Release | Includes | Excludes |
| --- | --- | --- | --- |
| **VMC** (A–B, from bytes) | v0.6.0 | `vrmAdapterVmc`, the comparison additions to `motionCore`, the generated VMC corpus, `vmc_record`, loopback | any real-sender claim, OpenExec, `ExecIr`, any mocopi SDK |
| **device** (B–C, D) | v0.7.0 | `vrmAdapterMocopi` **live**, the real-session corpus and manifests, the §9.6 cross-source comparison, the root/hips record, offline retarget E2E | recorded-file reading (that is [the BVH plan](recorded-motion-sources.md)), OpenExec, usdview realtime skinning, `ExecIr`, the ARDY generator |
| **generation** (E–F) | unscheduled | the generator contract, `vrmAdapterArdy` | a hosted model as a build dependency |

The OpenExec and `ExecIr` milestones are the
[OpenExec plan](openexec-foundation.md)'s, and neither appears in a right-hand
column by accident: an adapter that cannot ship without them has violated §3.
That the OpenExec release is now scheduled *after* both adapter releases does not
change the rule — it makes it cheaper to keep.

## 14. The shape this converges on

```text
sender apps     ->  vrmAdapterVmc ────┐
capture device  ->  vrmAdapterMocopi ─┼─>  motionCore values
generator       ->  vrmAdapterArdy ───┘         ↓
                                          motionRuntime
                                                ↓
                                           vrmRetarget
                                                ↓
                                       execMotion / execVrm
                                                ↓
                                            VRM avatar
```

A fourth leaf joins the left-hand column from
[the OSC track](osc-and-vrchat-trackers.md) — `vrmAdapterVrchatOsc`, over shared
OSC and transport libraries that all the live leaves stand on. It changes
nothing to the right of the first arrow, which is the property this section is
about.

Three siblings converging on one motion contract. The property being bought is
narrow and worth stating plainly: an input device, a relay application, or a
generation model can be replaced without changing retarget, runtime, OpenExec,
or the VRM application.
