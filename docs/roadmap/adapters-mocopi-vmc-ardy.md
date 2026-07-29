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

Sequence context: the generic live-capture surface these adapters plug into
shipped in v0.5.0 (Motion Phase D). This plan is the vendor half of Motion
Phase D and the whole of Motion Phase F. It is independent of the v0.6.0/v0.7.0
[OpenExec direction](openexec-v0.6.0-v0.7.0.md) and neither blocks the other —
[§3](#3-this-track-does-not-wait-for-openexec) states that as a commitment
rather than an observation.

Legend: 🚧 in progress · ⬜ not started · ⛔ blocked

## 1. What this adds, and why in this order

Three inputs, added one at a time:

1. **A VMC Protocol adapter** — generic real-time input from any sender
   application.
2. **A direct capture-product adapter** (mocopi) — native input from a real
   device, added on measured evidence that the protocol path loses something.
3. **A generation adapter** (ARDY) — generated motion behind a vendor-neutral
   generator contract.

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
trace* CI can replay. So the native adapter is now justified by what the relay
is **measured** to drop rather than by what it might drop, and the four triggers
are written down in [§6](#6-phase-2--the-mocopi-native-adapter) before any code
is written against them.

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

**Goal:** native device input, for the cases the relay path is measured to fail.

It is added when at least one of these is demonstrated, not assumed:

- metadata the VMC path drops is actually needed (SDK-specific confidence,
  device and sensor state, tracking-quality signals);
- latency through a sender application is measurably too high;
- tracking confidence or sensor state must reach the runtime's confidence
  gating;
- a direct connection is required with no third-party sender running.

```text
mocopi native SDK / native stream → vrmAdapterMocopi → canonical motion
```

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

VRM_MOCOPI_DEVICE_UNAVAILABLE   VRM_MOCOPI_TRACKING_LOST
VRM_MOCOPI_UNSUPPORTED_JOINT    VRM_MOCOPI_TIMESTAMP_INVALID
VRM_MOCOPI_PACKET_MALFORMED

VRM_ARDY_REQUEST_REJECTED       VRM_ARDY_TIMEOUT
VRM_ARDY_OUTPUT_MALFORMED       VRM_ARDY_UNSUPPORTED_SEMANTIC
VRM_ARDY_GENERATION_CANCELLED

VRM_MOTION_SAMPLE_STALE         VRM_MOTION_REQUIRED_BONE_MISSING
VRM_MOTION_NON_FINITE_TRANSFORM VRM_MOTION_INVALID_ROTATION
```

The `VRM_` prefix was added on 2026-07-29; an earlier draft of this document
used bare `MOCOPI_*` / `VMC_*` / `ARDY_*`, which is the only diagnostic family
in the repository without it (compare `VRM_RETARGET_*` in the
[OpenExec plan](openexec-v0.6.0-v0.7.0.md) P1-1 and the importer's `VRM###`
codes). `VRM_OPENEXEC_*` is a **separate** namespace owned by that plan, not by
any adapter.

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

```text
adapter-unit                  every PR
adapter-integration-loopback  every PR (VMC; no hardware)
adapter-recorded-corpus       every PR (fixtures from real sessions)
adapter-hardware-opt-in       separate; never gates a PR
```

Hardware results are saved as reproducible traces and fed back into the recorded
corpus, so a device is needed once per behavior rather than once per run.

## 10. Milestones

### Milestone A — VMC decoding 🚧

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
- ⬜ Everything else above, in the §5 order: the OSC decoder, then VMC
  semantics.

### Milestone B — VMC live receipt ⬜

UDP receiver · frame assembler · stale/partial/out-of-order policy · source
reset · `LiveCaptureSource` bridge · loopback integration test · VMC record tool
· recorded trace corpus · at least two sender applications validated · one
capture device validated through a VMC relay

### Milestone C — capture integration and offline E2E ⬜

`motion_capture` accepts a live VMC source · VMC trace → canonical samples →
semantic clip reproducible in CI · semantic clip + VRM avatar → `motion_retarget`
→ retargeted `UsdSkelAnimation` · artifact-only adapter and retarget smoke

### Milestone D — the mocopi native adapter ⬜

`adapters/liveCapture/mocopi` scaffold · packet decoder · joint mapping ·
coordinate conversion · tracking state and confidence · thin receiver ·
reconnection · trace recording · opt-in real-device test · **native vs
VMC-relayed comparison within a stated tolerance**

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
- ⬜ **The canonical contract owes three things before Milestone A ends.**
  `motionCore` has `HumanoidPose`, `HumanoidAnimation`, `RootMotion`, and
  `MotionSourceMetadata`, which is most of what an adapter needs. Missing:
  a **deterministic comparison** (`operator==`) on the pose aggregate — needed
  by corpus tests here and required outright by `ExecTypeRegistry::RegisterType`
  later ([openexec plan](openexec-v0.6.0-v0.7.0.md) P0-4); an explicit
  **tracking state**, so "tracking lost" is distinguishable from "zero pose" and
  from "bone absent"; and an **expression sample**, which Motion Phase G owns
  but which the VMC blend-shape messages reach first. See
  [MOTION_CONTRACT.md](../design/MOTION_CONTRACT.md).
- ⬜ **`motion_capture` grows a live source.** WORKSPACE.md §1 describes it as
  replaying a recorded trace. Milestone C adds `--source vmc --listen <addr>`
  alongside `--replay`, which makes the CLI a consumer of an adapter and needs
  the identity note updated when it lands. Exact syntax is fixed at
  implementation time.
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
  covered as they land. `adapter-integration-loopback` opens a socket and needs
  its own decision about a hosted runner; `adapter-hardware-opt-in` still has no
  expressible shape.
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

| Milestone | Includes | Excludes |
| --- | --- | --- |
| **Adapter** (A–C) | `vrmAdapterVmc`, the canonical-contract additions, the VMC corpus, record/replay, offline retarget E2E | OpenExec, usdview realtime skinning, `ExecIr`, any mocopi SDK |
| **mocopi** (D) | `vrmAdapterMocopi`, native input, the VMC-path comparison, packaging and platform support | any OpenExec requirement, any `ExecIr` requirement |
| **generation** (E–F) | the generator contract, `vrmAdapterArdy` | a hosted model as a build dependency |

The OpenExec and `ExecIr` milestones are the
[OpenExec plan](openexec-v0.6.0-v0.7.0.md)'s, and neither appears in the
right-hand column by accident: an adapter that cannot ship without them has
violated §3.

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
