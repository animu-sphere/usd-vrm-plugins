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
> **Scheduled on 2026-08-03.** This track was written as unscheduled work
> alongside the OpenExec plan. It is now the repository's *primary* line:
> Milestone A shipped in [v0.6.0](../releases/v0.6.0.md) and Milestones B–D are
> [v0.7.0](current.md). The [OpenExec foundation](openexec-foundation.md) moved
> behind it, because its parity comparison wants recorded sessions from a real
> device and real senders as input, and only this track produces those. **The
> mocopi native adapter is a committed deliverable**, not a decision to be taken
> later — see [§6](#6-phase-2--the-mocopi-native-adapter).
>
> **Narrowed the same day: this plan is *live* input.** A capture product also
> writes recorded files, and reading those is a file-format problem with its own
> layering, its own diagnostics, and its own second-producer requirement. It is
> [recorded-motion-sources.md](recorded-motion-sources.md), and it is deliberately
> not a mocopi importer. This document keeps sockets; that one keeps files; they
> meet at `motionCore`. The one place they are compared is
> [§9.6](#96-cross-source-comparison), on a single session observed both ways.

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
| `MocopiSkeletonMap` | source joints → canonical humanoid semantics; unsupported joints ignored, missing bones declared, source-name provenance kept |
| `MocopiCoordinateConverter` | handedness, up axis, quaternion convention, translation units, root orientation |
| `MocopiLiveSource` | pushes decoded frames at `LiveCaptureSource`, sets source metadata |

`MocopiLiveSource` is a bridge. If it acquires buffering, interpolation, or
filtering, it has become a second motion runtime. `vrmAdapterMocopi` does not
use `vrmAdapterVmc` internally (§2.1), and its output meets the *same* canonical
contract — not a superset with vendor fields bolted on. Product-specific
metadata is isolated as provenance, never as a new value type.

The same transport-last implementation order applies: recorded decoder →
mapping → live-source bridge → thin receiver.

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
| `adapter-integration-loopback` | every PR (VMC) | no |
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

## 10. Milestones

| Milestone | Release | State |
| --- | --- | --- |
| A — VMC decoding | v0.6.0 | shipped |
| B — VMC live receipt | v0.6.0 code, v0.7.0 evidence | the socket, the tool, and the corpus shipped; what a real sender does is v0.7.0 |
| C — capture integration and offline E2E | v0.7.0 | 🚧 |
| D — the mocopi native adapter | v0.7.0 | ⬜ |
| E — the generator contract | unscheduled | ⬜ |
| F — the generation adapter | unscheduled | ⬜ |

Milestone B is split across two releases on purpose. Everything in it that can be
verified from committed bytes shipped with v0.6.0; everything in it that needs an
operator, a sender application, or a device is v0.7.0, and no amount of code
closes those.

### Milestone A — VMC decoding ✅ (v0.6.0, one item carried)

`adapters/liveCapture/vmc` scaffold · recorded packet fixture format · OSC
decoder · VMC message decoder · bone mapping · coordinate conversion · canonical
pose output · diagnostics · deterministic unit tests

- ✅ **Scaffold and diagnostics.** `adapters/liveCapture/vmc` builds as a plain
  library in the root workspace tree, declares its two permitted edges, and
  carries a boundary check that fails on a plugin manifest, a stage/exec API, a
  sibling adapter, or a link to anything but `motionCore` and `motionRuntime`.
  The eight `VRM_VMC_*` codes of §8 are frozen with it — before a decoder
  exists, so the set describes the protocol rather than whichever failure was
  hit first.
- ✅ **The recorded input format and the first fixtures.** `vmc-packet-capture`
  v1 records the datagrams a session delivered, verbatim, with the instant each
  arrived — line-oriented text with an ASCII gutter, so a fixture diffs in a
  pull request and an address pattern is legible without a decoder. It is
  deliberately not a `motion-capture-trace`: a trace records what an adapter
  produced, and only a capture can represent a truncated datagram, a duplicate
  delivery, or a restart mid-frame. Five generated captures land with it,
  pinning the bundled and unbundled sender shapes, ignorable-but-valid traffic,
  ten packet-level refusals, and the arrival-order phenomena. Two tests hold the
  corpus: the C++ writer's canonical form, and the generator that authored it.
- ✅ **The OSC decoder.** Addresses, type tags, arguments, and bundles flattened
  into wire order — and no VMC semantics at all, which is what keeps both layers
  testable. Three rules are decided rather than emergent: a datagram decodes
  entirely or not at all (a half-decoded frame leaves the assembler unable to
  tell which half it got); every OSC 1.0 and 1.1 type tag is sized, including
  the fourteen VMC never sends, because refusing a message over a tag the decoder
  cannot skip blames the sender for the decoder's gap; and the only code this
  layer raises is `VRM_VMC_PACKET_MALFORMED`, since it cannot tell an
  unimplemented address from any other one. The corpus decodes to counts derived
  from the generator's structure, so the encoder and the decoder agree without
  either having been written from the other.
- ✅ **The VMC message decoder.** Seven address patterns — availability, the
  sender's clock, the model, the root and bone transforms, blend-shape values and
  their apply — decoded into VMC's own terms: a bone name is plain text and a
  quaternion stays in the sender's `(x, y, z, w)` order, because handedness, up
  axis, units and the map to a `HumanBone` belong to the layer that knows what
  the numbers are for. Four rules are decided rather than emergent, and the first
  is the OSC layer's inverted: **a message is refused, never a packet** — the
  framing is already established, so one malformed bone costs that bone and not
  the twenty-one that arrived with it. An **unimplemented address is not a
  defect** (info, recoverable, and the packet still decodes), a **known address
  with the wrong arguments is malformed** — OSC puts an `f` and a `d` in the same
  field, so accepting `,sddddddd` as a bone pose would pin nothing about the wire
  format — and **arguments past the known form are counted, never interpreted**,
  so a sender emitting one of the longer forms in the wild is reported rather
  than refused or guessed at. Two captures land with it, because the packet-level
  corpus could not reach either rule: `malformed-forms` is valid OSC refused one
  layer up — including **a bad bone inside an otherwise whole frame**, which is
  the "a message, never a packet" rule stated as a recorded session rather than
  as a unit test — and `extended-forms` carries nine arguments that are counted
  and not read. `vrmAdapterVmc_vmcCorpus` runs both layers over all seven to
  counts derived from the generator's structure, and checks three claims counts
  cannot: the neutral capture is all-identity with its root at the origin, the
  sender-restart capture's backwards clock decodes without complaint (since
  `VRM_VMC_TIMESTAMP_REGRESSION` needs the assembler's memory of the previous
  frame), and the bad bone's datagram still yields the twenty-two messages that
  arrived with it.
- ✅ **The canonical contract can be compared.** `motionCore` gained exact
  equality and a tolerant `NearlyEqual` before the layer that produces a
  `HumanoidPose` exists, so the bone mapping's corpus test compares a decoded
  pose against a committed one under a tolerance the contract states rather than
  one the test invents. First of §11's three debts.
- ✅ **The skeleton map.** `SkeletonMap.h` is the first layer that knows a
  `motion::HumanBone` exists, and the last one of Milestone A's: it converts and
  it does not decide. Two decisions carry the risk. The vocabulary is Unity's
  `HumanBodyBones` rather than VRM 1.0's, and the two disagree about **more than
  case for the thumb** — VRM 1.0 renamed the chain one joint down, so a map that
  lowercased the first letter would land every thumb rotation one joint out
  while the rest of the hand arrived correctly. And the basis change is **VRM
  1.0's reflection through X**, not VRM 0.x's through Z: `(x, y, z)` →
  `(-x, y, z)` and `(x, y, z, w)` → `(w, (x, -y, -z))`, where the two sign flips
  are one from the axis and one from the reversed sense of rotation.
  Un-normalised quaternions are normalised (senders emit them, and a composed
  skew is not a rotation); a zero-length or non-finite one is refused as
  `VRM_VMC_PACKET_MALFORMED`, because the value that would have to be invented
  to carry on is exactly the identity a reader could not tell from a real
  sample. `vrmAdapterVmc_skeletonMapCorpus` maps all seven captures — 493 bones
  and 24 roots, none unsupported, 232 reflected off the X axis — and pins the
  sign flip against recorded bytes: the arm-raise capture's rotations about
  Unity's −Z come out about the canonical +Z at the five angles the generator
  wrote, which is one left arm going up on both sides of a conversion that moved
  it from −X to +X.

  Two questions it deliberately leaves open, because answering either here would
  be a guess this repository cannot check. **A VMC bone rotation is the sender's
  local rotation**, which equals the rotation away from rest only when the
  sender's humanoid rest is identity; a sender where it is not needs
  `vrmRetarget`'s `SourceRestPose`, and manufacturing one from the first frame
  seen is exactly the kind of invention §2 forbids. **A bone's position has
  nowhere canonical to go** — `HumanoidPose` carries rotations and one
  `RootMotion` — so it is converted and handed on unread, and whether the hips
  offset composes with `/VMC/Ext/Root/Pos` is the frame assembler's decision
  with a real sender's evidence behind it. Both are Milestone B's to settle.
- ⬜ **An unknown bone name is unit-tested and not in the corpus.** The refusal
  path (`VRM_VMC_UNSUPPORTED_MESSAGE`, the bone dropped and the frame kept) has
  no recorded capture behind it, and inventing one would be guessing at what a
  sender emits — which is the same argument §9.2 makes about the rest of the
  generated set. **Carried to Milestone B and v0.7.0**, with the other two paths
  of the same shape; it is the one item of this milestone that no amount of code
  closes.

### Milestone B — VMC live receipt 🚧 (code v0.6.0 · evidence v0.7.0)

UDP receiver · frame assembler · stale/partial/out-of-order policy · source
reset · `LiveCaptureSource` bridge · loopback integration test · VMC record tool
· recorded trace corpus · at least two sender applications validated · one
capture device validated through a VMC relay

- ✅ **The frame assembler.** `FrameAssembler.h` is the first layer that
  *decides* rather than converts, and the decision the protocol forces is where a
  frame begins. The corpus already holds two sender shapes that disagree about
  it: the bundled sender's `/VMC/Ext/T` **opens** its frame and the unbundled
  sender's **closes** it, so either convention read as a rule produces one frame
  per two on the other sender — off by half a frame, with every rotation in it
  still individually correct, which is the kind of defect no per-message test can
  see. Two rules cover both: **a second clock ends the frame**, and **a repeat
  ends it unless it arrived in the same datagram**, where the same repetition is
  `VRM_VMC_DUPLICATE_BONE` instead. That exception is the only place a datagram
  boundary is load-bearing anywhere in the adapter, and it is why the assembler
  consumes packets rather than a flattened message stream.

  A backwards clock means three different things, told apart by one comparison
  against the last accepted frame, and the sender-restart capture records all
  three: **equal or slightly earlier** is `VRM_VMC_TIMESTAMP_REGRESSION` and the
  frame is refused — which is what stops a duplicated datagram from becoming a
  duplicated pose, since the same bytes twice are the same instant twice;
  **earlier by more than the restart threshold** is `VRM_VMC_SOURCE_RESTARTED`,
  accepted as the first frame of a new session, with everything the old session
  taught dropped; anything later is accepted. A restart is *reported and not
  repaired* — the sender's new clock comes out verbatim, and a caller that
  ignores `beginsNewSession` will see `LiveCaptureSource` refuse the new session
  as stale, which is the correct outcome for a caller that has not decided what a
  restart means to it. Offsetting the stream here would be manufacturing
  continuity out of a discontinuity, which is the class of invention §2 forbids.

  The assembler **holds nothing forward**: a bone the session has observed and
  this frame did not carry is reported as missing and the frame is still emitted,
  because `MissingBonePolicy` is the intake's answer and an adapter that baked
  one in would be a second motion runtime. A bone missing past the staleness
  horizon is additionally `VRM_VMC_STALE_JOINT`, raised **once per crossing**
  rather than per frame, or a 30 Hz stream buries its own session in
  diagnostics. Both are measured against the rig the session has actually
  observed rather than the full 55-bone humanoid — a sender that solves no
  fingers is complete, not incomplete forty times a second.
  `vrmAdapterVmc_frameAssemblerCorpus` assembles all seven captures to counts
  derived from the layer below (every one of the 493 bones and 24 roots the
  skeleton map produced is grouped and none dropped), and makes the claim this
  layer exists for: **both sender shapes yield five frames at the same 30 Hz
  cadence**, with the unbundled one's arm rising 15° per frame in the order it
  was sent.

  Two things it deliberately leaves open. **Blend-shape values are seen and
  dropped** — `motionCore` has no expression sample yet (§11), and inventing a
  place to put one is a contract change rather than an adapter's decision, so
  expression/body synchronisation stays unanswered until Motion Phase G. And the
  **hips offset is reachable and not composed** with `/VMC/Ext/Root/Pos`, closing
  the question the skeleton map handed here only halfway: a `HumanoidPose` has
  nowhere to put fifty-four of a frame's fifty-five local positions, so those are
  dropped, and whether the fifty-fifth is body translation or rig geometry is
  still a real sender's to say. `root transform update rate` and `source clock
  drift` are likewise untested here for want of a sender that varies either.
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
- ✅ **The bridge into the runtime.** `LiveSource.h` is the last layer that is
  still this adapter's, and deliberately the thinnest: it hands assembled frames
  to `LiveCaptureSource` and answers `IMotionSource` by forwarding, so a recorded
  capture now samples like any clip and the whole path is verifiable in CI from
  committed bytes. What it contributes to a pose is nothing — buffering,
  interpolation, smoothing, confidence gating, missing-bone resolution and
  root-motion intake exist once, in `motionRuntime`, and the tests run the same
  input under both `MissingBonePolicy` settings so the answer visibly changes
  with the runtime's configuration rather than with the adapter.

  **The one decision it takes is what a sender restart costs**, and it is the
  deadlock the two halves would otherwise reach: the assembler emits the new
  session's clock verbatim, which for the intake is a frame arriving behind the
  newest it holds — refused, forever. `Reset` drops the intake's history and
  admits the new session; `Refuse` lets the session visibly stop. `Reset` is the
  default because the alternative is a stream that dies the first time an
  operator restarts their sender application, and because it is what the layer
  below already does with everything it learned. Splicing the two sessions by
  offsetting the new timestamps is offered nowhere in this adapter (§2). A
  restart also invalidates the intake's clock offset, which only the consumer can
  re-align, so it is latched and handed back rather than repaired.

  What a pose cannot carry stays readable. The hips offset, the `missing` and
  `stale` sets, and the session flag reach a `HumanoidPose` nowhere at all, so
  the bridge opens a window on the frames it just delivered rather than being
  where they stop being visible — the hips-offset question is Milestone B's to
  settle with a real sender's session in front of it, and a recording tool
  should not have to drive the assembler separately to see one.

  Three smaller things are settled with it. Provenance **applies from when the
  sender sent it** — `/VMC/Ext/VRM` may arrive mid-session, and poses buffered
  before it are not retroactively taught a title the session did not know yet.
  Staleness reaches an operator as `VRM_VMC_STALE_JOINT` and never as a bone this
  layer unbound, because a second missing-bone policy inside the adapter would
  disagree with the configured one invisibly. And **the datagram's lifetime stops
  here**: every string view a decoded packet holds has become a value before the
  push returns, so a receiver may hand this API the buffer it is about to
  overwrite — which is the hazard `VmcMessage.h` names and no overload can
  refuse. `vrmAdapterVmc_liveSourceCorpus` replays all seven captures from bytes
  and makes the cross-layer claim: every frame the assembler emitted was admitted
  by the intake, because the assembler emits strictly advancing frames within a
  session and that is exactly the ordering `LiveCaptureSource::Push` requires.
  The restart capture is then replayed under both policies — six frames against
  four, on the same bytes — so what a restart costs is recorded as a choice.
- ✅ **The receiver, and the thread question it had to answer first.**
  `UdpReceiver.h` is the last layer written and the first one a live session
  touches, which is the order §5 insists on — everything below it was already
  verifiable from committed bytes, so this is the only part of the adapter whose
  tests need a socket at all.

  **The answer to §11's thread debt is to move the boundary rather than to lock
  the buffer.** Motion policy §11.4 put a network thread on one side of a
  "thread-safe timestamped pose buffer" that does not exist, and the resolution
  is that the hand-off happens on **raw datagrams, before the decoder**:
  `DatagramQueue` is the only synchronised object in the whole path, and this
  adapter's five layers and all of `motionRuntime` stay on one thread, exactly as
  their tests are written. A consumer that already has a tick needs no second
  thread at all — `Receive` with a zero timeout is a true poll — so the queue is
  for the narrow case of a consumer that cannot drain often enough, and exists
  mainly so that the first caller who meets it reaches for a queue rather than
  for a mutex around the runtime. The policy is amended to say so
  ([§11.4](../design/MOTION_ARCHITECTURE_POLICY.md)).

  Four smaller decisions carry the rest. **Every wait has a timeout**, for
  cancellation rather than latency: a thread parked in `recvfrom` can only be
  woken by closing the socket underneath it, which races the descriptor's reuse
  on every platform here. **Nothing arrives truncated** — the buffer is
  `MaxDatagramBytes`, so truncation is impossible rather than configurable, and
  that is a decision about blame, since a truncated datagram is
  indistinguishable at the OSC layer from a malformed one and a smaller buffer
  would let the receiver manufacture `VRM_VMC_PACKET_MALFORMED` against a sender
  that did nothing wrong. **The clock is monotonic**, which the recorded capture
  format requires rather than prefers: it forbids backwards receive times, and a
  wall clock steps for reasons that have nothing to do with the session. And
  **the frozen set needed no ninth code** — `VRM_VMC_SOCKET_BIND_FAILED` is the
  only socket failure a session cannot continue past, so a lost datagram, a
  transient error and an empty poll are counts rather than diagnostics.

  `vrmAdapterVmc_loopbackCorpus` replays all seven captures **through a real
  socket** — 168 datagrams sent to a bound port and read back off it — and makes
  the claim this layer exists for: the 22 poses that come out are `operator==`
  identical to the ones the same bytes produce read from the file, with the
  arrival clock the only thing the wire is allowed to have changed. One buffer is
  reused for the whole replay, so the bridge's lifetime claim is checked by the
  poses matching rather than by an assertion about bytes.
- ✅ **The record tool, and what it is for.** `vmc_record` is the adapter's CLI
  and the one part of it that meets a real sender. Everything under it is
  verifiable from committed bytes, which is the build order's whole point and
  also its limit: the corpus is *generated*, so it reproduces the protocol's
  shapes and not what any application emits. Every item still open in this
  milestone is that same shape — two senders validated, a device through a
  relay, a recorded corpus — and none of them closes by writing more code. They
  close by an operator pointing a sender at a port, so the tool's job is to turn
  one such session into the two things this repository can keep: a capture file,
  and a statement of what was in it.

  **The datagram reaches the file before the decoder sees it**, and that is the
  only rule here. A recorder whose decoder could refuse a datagram would record
  what the adapter already understands, and the sessions worth recording are
  exactly the ones it might not. The decode still runs in the same loop rather
  than afterwards, because an operator with a sender open needs to know *now*
  whether the session is worth keeping — what it produces is a report, and a
  report is not a filter.

  The report is one block, ordered the way the questions are asked when a live
  session is not working: is anything arriving, does it decode, does it become
  motion, what went wrong. Each line is a tally some layer already keeps, and
  the report is the one place they are read together — `UdpReceiverStats` cannot
  see a bone and `VmcFrameStats` cannot see a datagram that never decoded. Two
  lines are not statistics at all: **`hips offset` and `root` are the evidence
  the two open questions above need**, reported as how far each value moved and
  never as what it means, because this tool is in no better position to decide
  what a sender means by a field than the layer that declined to. A third,
  `model`, says that the sender's model title is in the recorded bytes — it is
  never *used*, since naming a capture after the avatar it happened to see would
  put someone's title in a fixture's header as well as its payload.

  `--inspect` decodes a recorded capture and prints the same block with no
  socket at all, which is what makes the CLI testable in CI over the committed
  corpus — `vmc_record_inspect` reports all seven captures to the same 168
  datagrams and 22 frames the corpus tests below it are written against. It also
  answers "is this fixture still what I thought it was" for a capture recorded
  months ago. `vmc_record_loopback` then makes the claim the tool exists for,
  which is `vrmAdapterVmc_loopbackCorpus`'s raised to the CLI: the datagrams
  that come off a real socket are byte-identical to the ones that went in, and
  the recorded file reports the same motion as the file it was replayed from.
  The library test compares poses; this one compares the artifact an operator
  keeps.

  Two smaller things are settled with it. A session always has a stop condition
  — `--duration`, `--idle-timeout`, Ctrl-C, and a `--max-datagrams` bound that
  is on by default because the capture is held in memory until it is written —
  and **every session reports which one ended it**, since a recording that
  stopped because a flag said so and one that stopped because the socket failed
  are different sessions and the file cannot tell them apart afterwards. And the
  tool links `vrmAdapterVmc` and nothing else: §2 permits an adapter tool to
  drive `vrmRetarget` and author a stage, this one needs neither, and a second
  path from a VMC session to an avatar would be the fork §2 forbids —
  `motion_capture` is where a session becomes a clip (Milestone C).
- ✅ **A hosted runner does allow a loopback socket** — on all three, measured
  rather than assumed. The `kind: workspace` cells picked the two socket tests up
  with no CI edit, exactly as they picked up every other adapter test, and
  `vrmAdapterVmc_udpReceiver` and `vrmAdapterVmc_loopbackCorpus` ran and passed
  in `workspace-pr-{windows,macos-arm64,linux}` (41/41 per cell, PR #84). So
  §9.5's `adapter-integration-loopback` lane needs no cell of its own: it is the
  workspace cells, and the only thing that would have to change is an exclusion
  if some future runner refuses. They are separate CTest names for exactly that,
  and they bind loopback on an OS-assigned port — never 39539, which would fight
  a developer's real sender for it. `adapter-hardware-opt-in` is still the one
  lane in §9.5 with no expressible shape.

  The CLI's two tests landed the same way, taking the root suite from 41 names
  to 43: `vmc_record_inspect` needs no socket and `vmc_record_loopback` binds
  one, split for the same reason and excludable the same way.
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
  > sender that is available during v0.7.0 gets recorded and reported — but the
  > release no longer waits on lining up two or three applications. What replaces
  > it as the gate is [§9.6](#96-cross-source-comparison), which needs only the
  > device: one session, observed natively and as a file, with a VMC relay added
  > where one is running.

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
- ⬜ **The root and hips decision record.** §5.2 left it open twice — the
  skeleton map handed it to the assembler, the assembler handed it to a real
  sender — and `vmc_record` already reports both values as movement without
  interpreting either. v0.7.0 closes it as a record whether or not it closes it
  as a policy. If a policy is chosen it is one of `RootOnly`, `HipsOnly`,
  `RootPlusHipsOffset`, or an explicit per-sender profile, and it is **never**
  synthesised from a sender-specific guess (§2). If none is chosen, the record
  still states: what was observed, what differed between senders, which value is
  canonical today, what stays open, and what the open part costs downstream.
- ⬜ **The three unit-tested paths with no capture behind them** close here or
  are stated as unclosable: an unknown bone name, the receive-clock fallback when
  a frame carries no `/VMC/Ext/T`, and the leading-bone reordering the repeat
  rule's characterisation test pins. Each needs a real sender to *emit* it;
  inventing a capture would be the guessing §9.2 argues against.

### Milestone C — capture integration and offline E2E 🚧 (v0.7.0)

VMC session → `motion-capture-trace` · that trace → canonical samples → semantic
clip reproducible in CI · semantic clip + VRM avatar → **unchanged**
`motion_retarget` → retargeted `UsdSkelAnimation` · artifact-only adapter and
retarget smoke

The first item read "`motion_capture` accepts a live VMC source" until §11
costed that edge and rejected it. The integration is a file rather than a link:
`vmc_record --export-trace` ends the adapter's half at the canonical trace, and
every tool after it is one the product already ships and this milestone does not
touch. That is why the third item can say *unchanged* about `motion_retarget`
and this one can say it about `motion_capture` too.

- ✅ **The first three items landed 2026-08-04.** `--export-trace` writes what
  the adapter delivered, one trace per session; `vmc_record_endToEnd` drives
  `arm-raise-30hz` through both product tools onto a rig and checks the result
  through a `UsdSkelSkeletonQuery` — **by name**: the three joints the session
  drove are the three UsdSkel resolves as moving, and a session that moves
  nothing fails the test (verified by running it against
  `neutral-standing-30hz`, which is how anyone knows the check works).

  The test lives with the adapter rather than with the product, and that is the
  same rule one level down: a product test that spawned `vmc_record` would be
  the dependency this arrangement exists to avoid, in a test directory instead
  of a link line, which is worse for being harder to see. It is skipped by a
  `TARGET` guard in a build tree without the product's tools.

  Its rig is a fixture of its own rather than the design avatar, because the
  committed session that moves is an arm raise and the design avatar has four
  joints and no arms. Widening a design contract fixture to suit a test would
  move a contract to make a test pass.
- ⬜ **The artifact-only leg is not done.** Nothing here has been run from
  packaged artifacts, and `ost` 0.21.0 cannot package an adapter at all (§11),
  so the chain is verified from the workspace build only.

**Found on the way, and fixed in the layer that owned it.** The trace writer
could emit a file its own reader refused: `provider`, `protocol` and `sourceId`
were written verbatim and read back one token at a time, and a VMC session's
`sourceId` is the model title a person typed — `"Example Avatar"`. Every trace
in this repository was generated until now, and a generated `sourceId` is
`walk-01`. The header takes the rest of the line since, and what no
line-oriented format can carry is refused before the first byte.

### Milestone D — the mocopi native live adapter ⬜ (v0.7.0)

`adapters/liveCapture/mocopi` scaffold · packet-capture fixture format · packet
decoder · joint mapping · coordinate conversion · tracking state and confidence ·
frame assembly · `LiveCaptureSource` bridge · thin UDP receiver · reconnection ·
opt-in real-device test · **the cross-source comparison of §9.6**

Same build order as Milestone A, for the same reason: recorded decoder → mapping
→ live-source bridge → thin receiver. The transport arrives last so every layer
below it is testable from committed bytes, and the device is needed for
recording sessions rather than for running tests.

Two things this milestone does **not** get to do, and they are worth naming
because a native adapter is exactly where the temptation appears. It does not
grow buffering, interpolation, or filtering — `MocopiLiveSource` is a bridge, and
a bridge that acquires those has become a second motion runtime. And it does not
widen the canonical contract: product-specific metadata is isolated as
provenance, never as a new value type, so its output meets the *same* contract as
`vrmAdapterVmc`'s rather than a superset of it. The one contract addition this
phase legitimately needs is an explicit **tracking state** (§11), which is
vendor-neutral and belongs to `motionCore` rather than to mocopi.

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
depends on them ([docs/README.md](../README.md)).

- ✅ **Adapter identities and dependency directions.**
  [WORKSPACE.md](../architecture/WORKSPACE.md) §1 names `vrmAdapterMocopi`,
  `vrmAdapterVmc`, and `vrmAdapterArdy`; §2 carries the adapter-library vs
  adapter-tool split, `execMotion`/`execVrm` ⇸ `adapters/*`, and the
  no-sibling-dependency rule; §5 carries the artifact names and the aggregate
  exclusion. Landed 2026-07-28 with this document. **Amended 2026-07-29**, before
  Milestone A: the three identities are plain libraries plus their CLI tools, not
  plugin bundles, and §2's split is now expressible in a manifest instead of only
  in prose (§4 above).
- ✅ **VMC as a first-class generic input.** Motion policy §8.2 previously named
  one direct adapter only, and now carries the VMC-first ordering.
- 🚧 **The canonical contract owed three things before Milestone A ends; one is
  paid.** `motionCore` has `HumanoidPose`, `HumanoidAnimation`, `RootMotion`,
  and `MotionSourceMetadata`, which is most of what an adapter needs.
  - ✅ **Deterministic comparison**, landed 2026-08-02 — and as *two*
    comparisons, because the callers wanted different answers: `operator==` is
    the exact one `ExecTypeRegistry::RegisterType` requires
    ([OpenExec plan](openexec-foundation.md) P0-4), and `NearlyEqual` with a
    stated `MotionTolerance` is the one a corpus test here needs, since a
    fixture recorded through six decimals never equals the pose that produced it
    bit for bit. The two differ in three stated places, and the corpus-relevant
    one is that a quaternion and its negation are the same motion and a
    different value.
  - ⬜ An explicit **tracking state**, so "tracking lost" is distinguishable
    from "zero pose" and from "bone absent".
  - ⬜ An **expression sample**, which Motion Phase G owns but which the VMC
    blend-shape messages reach first.

  See [MOTION_CONTRACT.md](../design/MOTION_CONTRACT.md#comparison-semantics-v060).
- ✅ **`motionRuntime` is not thread-safe, and motion policy §11.4 assumed it
  was.** Answered by the receiver, and answered by moving the boundary rather
  than by locking anything: the hand-off is a bounded queue of **raw datagrams**
  between the network thread and the consumer's, so the decode path and all of
  `motionRuntime` keep the single-threaded contract their tests are written
  against, and `DatagramQueue` is the only synchronised object anywhere in the
  path. The alternative — the synchronisation the policy assumed — would have
  made `LiveCaptureSource` safe against itself while leaving every `GetIntake()`
  caller racing on the same buffer, which is a worse fault for looking like a
  fixed one. `motionRuntime` is unchanged and stays unchanged; policy §11.4
  carries the amended arrangement, and `UdpReceiver.h` the argument. Landed
  2026-08-03.
- ✅ **`motion_capture` does not grow a live source, and the hand-off is a
  file.** This item read "Milestone C adds `--source vmc --listen <addr>`
  alongside `--replay`" until the change was actually costed, and what it cost
  was three things at once: `motion_capture` is a product tool and every adapter
  is excluded from the product ([WORKSPACE.md §5](../architecture/WORKSPACE.md)),
  so the edge would have pulled a protocol decoder and a product name into the
  aggregate artifact — once per adapter, because `--source vmc` invites
  `--source mocopi` behind it; no tool in the product opens a transport today,
  which is what makes every clip reproducible by construction; and the adapter
  would have stopped being separately shippable.

  What is built instead was already the format's job. `vmc_record --export-trace`
  writes what the adapter delivered as a `motion-capture-trace` — *after
  protocol decode and coordinate conversion, before any intake policy*, which is
  that format's own definition of its content (`motionRuntime/CaptureTrace.h`) —
  and `motion_capture` replays it unchanged, knowing nothing about VMC. So
  §2 gains no edge, the identity note in §1 says the same thing after the first
  adapter as before it, and a session still becomes a clip in exactly one place.
  The cost is that a live session is two commands; the intermediate is canonical
  and carries no VMC vocabulary, so the second is the one a `.vrma` clip already
  goes through. Settled 2026-08-04, in the contract before the code.

  The release was already asking for this and nobody had read it that way: the
  v0.7.0 boundary requires that a session reach a real VRM avatar through
  **unchanged** `motion_capture` and `motion_retarget`
  ([current.md](current.md#done-when)). A `--source vmc` inside `motion_capture`
  would have made that condition unsatisfiable by the change meant to satisfy
  it.
- ⬜ **The generator contract has no home yet.** `IMotionGenerator` and
  `MotionGenerationRequest` are named in motion policy §16 Phase F as
  deliverables, but the interface itself will need a contract document before
  Milestone E, in the way [MOTION_CONTRACT.md](../design/MOTION_CONTRACT.md)
  serves the shipped motion foundation.
- ⬜ **Adapter CI lanes are not in `openstrata.ci.yaml`.** The four lanes in §9.5
  are named here and expressed nowhere. `kind: workspace` cells (ost 0.21.0)
  cover the root tree, so `adapter-unit` may need no new cell shape; the
  hardware lane certainly does, since it must never gate a PR. **Measured with
  the scaffold:** the workspace cells picked the adapter's tests up on all three
  OS with no CI edit at all, so `adapter-unit` and `adapter-recorded-corpus` are
  covered as they land, and so is `adapter-integration-loopback`: its two CTest
  names ran green in the workspace cells on all three hosted OS with no CI edit
  (Milestone B), so three of §9.5's four lanes need no new cell shape at all.
  `adapter-hardware-opt-in` still has no expressible shape, since it must never
  gate a PR.
- ⬜ **The workspace graph gate does not reach an adapter.** `ost` 0.21.0
  discovers plain libraries in the project root's immediate subdirectories and
  under `libs/`, so `adapters/liveCapture/vmc/openstrata.library.yaml` is never
  loaded and its declared edges are never validated — silently, since the gate
  still reports "valid". WORKSPACE.md §2 records this rather than claiming the
  enforcement, and the ask is
  [report 34](../reports/ost/34-2026-07-29-v0.21.0-adapter-library-discovery-gap.md).
  Until it is answered, the per-adapter binary link check is the enforcement.
- ⬜ **An adapter cannot be packaged.** §13 makes each adapter its own shippable
  boundary and WORKSPACE.md §5 names the artifact, but `ost` 0.21.0 has no
  per-library packaging command — `plugin package` takes a bundle or a workspace
  of them (report 34 §2). This does not block Milestones A–C, which end at a
  retargeted `UsdSkelAnimation` from the workspace build; it blocks shipping the
  adapter separately from the product.

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

Three siblings converging on one motion contract. The property being bought is
narrow and worth stating plainly: an input device, a relay application, or a
generation model can be replaced without changing retarget, runtime, OpenExec,
or the VRM application.
