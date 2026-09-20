# Motion migration — generic motion to `usd-motion-plugins`, input to `motion-connectors`

**Status:** ✅ MIG-0; 🚧 MIG-1, MIG-2 and MIG-3, their consuming halves blocked on `ost` (report 41); **every sending half of MIG-2 has arrived** — `motionRetarget` 2026-09-19, `execMotion` and `motionUsd`'s reading half 2026-09-20; 🚧 MIG-4, its two leaf libraries arrived in `motion-connectors` 2026-09-19, and `motion_capture` arrived in `usd-motion-plugins` 2026-09-20 · **Target:** after the OpenExec foundation ·
**Structure:** [architecture/WORKSPACE.md §9](../architecture/WORKSPACE.md#9-destinations-under-the-motion-architecture) ·
**Policy:** the `usd-motion-plugins` design policy §37, and
[design/INTEGRATION_SCOPE_POLICY.md](../design/INTEGRATION_SCOPE_POLICY.md) §13 ·
**Added:** 2026-09-06 as the conditional split track · **Rewritten:** 2026-09-17

**What changed on 2026-09-17.** This track used to be conditional: it opened
with a measurement (MFS-0) of four preconditions — two non-VRM consumers, an
API free of VRM vocabulary, independent versioning, a diverged cadence — that
could end it, and its scope was `motionCore` + `motionRuntime` only. The
`usd-motion-plugins` design policy has since decided the question where the
motion architecture is owned: generic motion leaves this repository, and
device and protocol input goes to `motion-connectors`. So the gate is gone,
the scope is every identity
[WORKSPACE.md §9.1](../architecture/WORKSPACE.md#91-destination-of-every-identity)
gives a destination, and what is left to plan is **order and evidence**. The
filename is kept so that links to the track keep working.

The reversible half of the old plan survives as preparation (MIG-0), because
it was right either way: a component consumed through `find_package` as though
it were external is the only proof that it can leave.

## 1. Order

The v0.9.0 OpenExec foundation finishes **here** first. It is the first
consumer of `motionRuntime` and `vrmRetarget` that is not the tool beside them,
and its findings
([boundary consolidation §1](boundary-consolidation.md#findings-from-the-exec-layer-as-they-land))
are exactly the API defects that should be fixed once, in the destination,
rather than moved and fixed there later without the evidence that found them.

The moves then follow the dependency order
([WORKSPACE.md §9.2](../architecture/WORKSPACE.md#92-moving-rules), rule 6),
and each waits for its destination to publish an installable package. The
motion-plugins policy numbers its migration **Migration Phase A–F**; the
milestones below say which of them each one serves.

| Milestone | Migration Phase | Moves | Waits for |
| --- | --- | --- | --- |
| MIG-0 — preparation | A | nothing | v0.9.0 |
| MIG-1 — the core | A, B | `motionCore` | the `usd-motion-plugins` scaffold |
| MIG-2 — sampling, retarget, USD bridge | C | `motionRuntime`, the generic half of `vrmRetarget`, `motion_retarget`'s `StageIo`, `execMotion` | MIG-1 |
| MIG-3 — recorded sources | C | `motionSource`, `motionBvh`, the BVH tools, `profiles/motion/` | MIG-1 |
| MIG-4 — recording and live input | E | `motion_capture` to `usd-motion-plugins`; `liveTransport`, `osc`, `motionTracking`, the `vrmAdapter*` libraries and their record tools to `motion-connectors` | MIG-2, and `motion-connectors` existing |
| MIG-5 — nothing left behind | F | nothing; the workspace is reduced | MIG-1–MIG-4 |

Migration Phase D — `usd-mmd-plugins` consuming the same core — is that
repository's, and needs nothing from this one.

## 2. MIG-0 — preparation ✅

- ✅ **Check the API for VRM vocabulary, mechanically** (2026-09-19).
  `workspace_motion_vocabulary` scans every public header of `motionCore`,
  `motionRuntime` and the generic half of `vrmRetarget`, with comments
  removed, and every string literal of their sources. It checks what it finds
  against [`tests/boundary/motion-vocabulary.json`](../../tests/boundary/motion-vocabulary.json).
  The first run found 40 names. 16 are renames or identity names
  ([WORKSPACE.md §9.3](../architecture/WORKSPACE.md#93-names)), 8 are the
  retarget's diagnostic codes, and 16 belong to the look-at and expression
  findings of [§9.5](../architecture/WORKSPACE.md#95-the-line-through-vrmretarget).
  Two anchors pin the required-bone finding, whose names no pattern can see.
  A new VRM name in a moving header, a ledger row that no longer matches, and
  an anchor that disappears each fail, and three near-miss ledgers prove it.
- ✅ **Draw the line through `vrmRetarget`** (2026-09-19). The line is by
  header, and only `HumanoidMap::GetRequiredBones` is cut in two
  ([WORKSPACE.md §9.5](../architecture/WORKSPACE.md#95-the-line-through-vrmretarget)).
  The map itself is generic and moves as `RetargetMap`. What stays is VRM
  1.0's required-bone set, `ExpressionResolver` and `LookAtEvaluator`. Three
  findings are recorded there for the arrival: a caller-supplied required
  set, the look-at target's shape, and the expression channel's namespace.
- ✅ **Hand over the evidence** (2026-09-19, usd-motion-plugins #3). The
  shared core's contract starts from what this repository measured. The basis,
  root-and-hips and path-rule sections and the four sampling findings were
  already there. #3 adds what was missing, as cited evidence:
  - the v0.9.0 partial-skeleton and scale decisions, and the six retarget-side
    exec findings, in its `RETARGETING_POLICY.md` §4.1, §6.1 and §10;
  - recorded-source provenance and the tracker boundary, in its
    `MOTION_CONTRACT.md` §7.1 and §11.1;
  - the OpenExec driver contract, in a new `EXEC_CONTRACT.md`. It went there
    and not into the motion contract, because it describes an evaluation, not
    a value.
- ✅ **Carry the producer conventions v0.9.0 left unauthored** (2026-09-19,
  proposed in usd-motion-plugins #3, its `EXEC_CONTRACT.md` §5). The answer
  proposed: only `motion:timeCodesPerSecond` is a motion writer's to author,
  from the same number as the stage metadata, as a shim until OpenUSD delivers
  stage metadata to a computation. Everything else below is evaluation policy
  and belongs to the composed scene, never to a motion asset. For this
  repository that means the `.vrma` importer and the bake author the rate, and
  `vrm:retarget:*` stays a scene-side convention of `execVrm` until the shared
  `Bindings` prim exists. The one-joint fallback is closed on the producer side
  by writers always authoring both arrays. What the item asked:
  `motion:timeCodesPerSecond`, `motion:filter:*`, `motion:root:*`,
  `vrm:retarget:sourceSkeleton` and the four `vrm:retarget:*` root statements
  are read by the exec bundles and authored today only by fixtures and the
  parity harness. The OpenExec plan's P0-4 handed them here on 2026-09-17,
  together with the one-joint fallback the driver contract states: which
  producer authors each, or which upstream change retires it, is the producer
  contract's first question (BND-0).
- ✅ **Name the parity baselines each move must reproduce** (2026-09-19).
  Each move's own suites travel with it and must pass in the destination.
  The consumer-side rows stay here and are re-run against the consumed
  package before the in-tree copy is deleted
  ([WORKSPACE.md §9.2](../architecture/WORKSPACE.md#92-moving-rules), rule 2):

  | Move | Travels, and must pass there | Stays, and re-runs here against the package |
  | --- | --- | --- |
  | MIG-1 | `motionCore_unit`, `motionCore_compare` | the whole suite: every identity here links `motionCore` |
  | MIG-2 | `motionRuntime_unit`, `_liveCapture`, `_corpus`, `_traceGen`; the generic half of `vrmRetarget_unit`; every `execMotion_*` suite | `workspace_exec_driver` and the five `workspace_exec_parity_*` cases (**414 598 values, every one `==`**, at v0.9.0); `motion_retarget_design_triplet`; every `execVrm_*` suite |
  | MIG-3 | every `motionSource_*` and `motionBvh_*` suite; `motion_bvh_convert_clip`, `motion_bvh_inspect_report`; `workspace_motion_profiles` and its `_absent` pair | `workspace_bvh_end_to_end` (a BVH export through the product's tools onto a VRM) |
  | MIG-4 | `motion_capture_replay`; `liveTransport_packetCapture`; `motionTracking_trackerAssignment`, `_trackerSolve`; every `vrmAdapter*` capture and packet suite | `workspace_unicode_paths`, which runs the three recorders, loses them with this move |

  Boundary suites (`*_boundaries`) travel too, and they are rewritten there
  for that repository's edges rather than reproduced.

## 3. MIG-1 — the core 🚧

- ✅ `motionCore` arrives in `usd-motion-plugins` under the same identity, with its
  history, renamed to the shared names, under `openstrata::motion`
  (2026-09-19, usd-motion-plugins #2). Four contract questions were decided
  first: the 55-joint vocabulary is version 1, the generic stage's prims are
  `Skeleton` / `Body` / `Channels`, time codes are always 30 per second, and a
  channel's value is a `float`.
- ⛔ **Blocked on `ost`:** `requires.libraries` resolves only sibling members,
  so this repository cannot declare `motionCore` from another repository, and
  undeclaring the edge would hide it from the graph and provenance
  ([ost report 41](../reports/ost/41-2026-09-19-v0.22.10-a-library-from-another-repository.md)).
  The consuming change below waits for that ask, not for a workaround.
- ⬜ This repository consumes the installed package: `usdVrmaFileFormat`,
  `motionRuntime`, `vrmRetarget`, `motionSource`, `motionTracking` and the
  adapters switch their edge in the same change that deletes `libs/motionCore`
  ([WORKSPACE.md §9.2](../architecture/WORKSPACE.md#92-moving-rules), rule 1).
  Adapting code here to the renamed types is acceptable during migration
  (motion-plugins policy §37); keeping two cores is not.
- ⬜ The `.vrma` stage does not change: `/Animation`, `HumanoidSkeleton`,
  `BodyAnimation`, the `vrma` custom data. A standalone motion stage in
  `usd-motion-plugins`' shape is a separate decision, not a side effect.

## 4. MIG-2 — sampling, retarget, USD bridge 🚧

- 🚧 `motionRuntime` arrives as `motionSampling` and `motionRecording`, with
  the exec findings fixed on arrival: a status-carrying `SampleClip`, a
  stateless `PoseFilter` step, `ConditionRootMotion` as a free function, an
  N-way blend that can answer *nothing to blend*.
  - ✅ Arrived with its history (2026-09-19, usd-motion-plugins #4): 28
    commits, then a move-only split, then the rename. `CaptureRecorder` is
    `MotionRecorder` there; `LiveCaptureSource` keeps its name until the
    stream's published shape is decided (that repository's MC-O5). The trace
    format and the corpus came unchanged, and every suite MIG-0 named for
    `motionRuntime` passes there on three OSes.
  - ✅ The pose's provenance took the contract's shape after the move
    (usd-motion-plugins #5): `source` (optional) is `metadata` (always
    present), `SourceMetadata` gained the sample's `sourceTimestamp` and
    `sequenceNumber`, and `motion-capture-trace` is version 4. The consuming
    change here adapts to it: `.source` on a pose is `.metadata`, and a VMC
    frame that set no provenance now carries the default value rather than
    none.
  - ✅ The four exec findings, in a change of their own there (2026-09-19,
    [usd-motion-plugins #6](https://github.com/animu-sphere/usd-motion-plugins/pull/6)). Each is a pure function now, and the streaming class
    beside it calls it: `SampleClip`, `PoseFilter::Step`, an N-way
    `BlendPoses` that answers `std::optional`, and `ConditionRootMotion`. The
    consuming change here adapts to them. `execMotion`'s `SampleHistory`,
    `FilteredPose` and `RootMotionFrom` become calls to those functions, and
    `BlendedPose` reads the optional instead of checking for nothing weighted
    first.
  - ⛔ This repository consumes the packages and deletes `libs/motionRuntime`
    in the same change as MIG-1's `motionCore`, and for the same reason it
    waits: `requires.libraries` cannot name a library from another repository
    ([ost report 41](../reports/ost/41-2026-09-19-v0.22.10-a-library-from-another-repository.md)).
- 🚧 The generic retarget arrives as `motionRetarget`, with a
  `SkeletonDescriptor` built from joint tokens and rest matrices — the
  finding `execVrm` and `motion_retarget` both carry a copy of today.
  - ✅ Arrived with its history (2026-09-19,
    [usd-motion-plugins #9](https://github.com/animu-sphere/usd-motion-plugins/pull/9)):
    32 commits, cut along [WORKSPACE.md §9.5](../architecture/WORKSPACE.md#95-the-line-through-vrmretarget),
    with `ExpressionResolver` and `LookAtEvaluator` left out of the history.
    `TargetSkeleton` is `SkeletonDescriptor` there, `TargetJoint` is
    `SkeletonJoint`, `HumanoidMap` is `RetargetMap`, and the codes are
    `MOTION_RETARGET_*` with the event names unchanged. 24 of
    `vrmRetarget_unit`'s 56 tests travelled; the other 32 test what stays.
  - ✅ Two questions were decided for the import (2026-09-19). The
    destination's WS-O2: `motionRetarget` depends on `motionCore` alone, so
    `RetargetOptions::resampleRate` is gone and a caller resamples first, as
    `motion_retarget` already does. Its RT-O1: the published root-motion
    vocabulary is the imported `Hips` / `RootJoint` / `Ignore`.
  - ✅ §9.5's finding 1 was fixed there: the required-bone set is
    `RetargetOptions::requiredBones`, empty by default, and
    `GetRequiredBones` is gone. Under `Hips` root motion the hips stay
    required. This repository supplies VRM 1.0's set, which starts with the
    hips, so every list the parity rows compare is unchanged.
  - ✅ Both builders the OpenExec findings asked for exist there:
    `BuildSkeletonDescriptor` (tokens and rest matrices) and
    `BuildSourceRestPose` (a semantic skeleton), each with the refusals
    `execVrm`'s copy makes.
  - ⛔ The consuming change here deletes the generic half of
    `libs/vrmRetarget`. `ExecVrmRig`'s `TargetSkeletonFromRest` and
    `SourceRestFromSkeleton` and `motion_retarget`'s copies become calls to
    the two builders, and the caller passes VRM 1.0's required set. It
    re-runs the parity rows first, and it waits on
    [ost report 41](../reports/ost/41-2026-09-19-v0.22.10-a-library-from-another-repository.md)
    like MIG-1. Whether what stays keeps the name `vrmRetarget` is decided in
    that change.
- 🚧 `motionUsd`. The authoring half arrived on 2026-09-19
  ([usd-motion-plugins #7](https://github.com/animu-sphere/usd-motion-plugins/pull/7)). Its source was
  `motion_capture`'s `ClipWriter`, not `StageIo`. `StageIo` reads a clip and
  bakes it onto a VRM, and that writing half is VRM-specific and stays here.
  `motionUsd` authors that repository's own stage shape
  (`/Animation/{Skeleton,Body}`, always 30 time codes per second). The
  `.vrma` stage here does not change. `motion_capture` keeps its copy of the
  writer until MIG-4 moves the tool.
  - ✅ `StageIo`'s clip and skeleton *reading* arrived as `motionUsd`'s
    reading half (2026-09-20,
    [usd-motion-plugins #13](https://github.com/animu-sphere/usd-motion-plugins/pull/13)),
    which closes every sending half of MIG-2. 13 commits came through
    `git filter-repo` over the two files — 11 changes and the two merges that
    carried them — then a move-only commit, then the cut: a file cannot be
    filtered in two, so both halves arrived and the VRM half was removed there
    rather than carried. The bake stays here.
  - The library home is what the move was for. `PoseFromStageSample` takes
    values rather than a prim, so a caller holding a stage and an OpenExec
    node holding already-resolved inputs apply one rule — the clip → pose home
    the sampling finding asked for. `execMotion` there calls it and holds no
    copy (2026-09-20,
    [usd-motion-plugins #14](https://github.com/animu-sphere/usd-motion-plugins/pull/14)),
    which closes that finding rather than only giving it somewhere to be
    closed.

    **That switch changed what two of its nodes answer**, and the consuming
    change here inherits the change with the package. `motion.filterPose`
    smooths the root orientation — `PoseFilter::Options` defaults
    `filterRootOrientation` to true and the field was previously absent from
    every clip-sourced pose, so a clip authoring no policy at all is affected
    — and `motion.extractRootMotion` returns a root motion carrying one. The
    eight L5 goldens there did not move, because no fixture turns its hips;
    the parity rows here are the ones that would see it, and `vrmRetarget`
    reads no root orientation, so they should not. That is a prediction to
    check when the rows are re-run, not a measurement.

    The leaf-segment rule went with it: `motionCore` there gained
    `FindHumanJointByPath`, `HumanJointPath`'s inverse. **This repository
    still has two copies of that rule**, in `StageIo`'s `LeafToken` and
    `execMotion`'s `BoneForJointPath`, and the consuming change deletes both.
  - The two findings the destination's USD_MAPPING.md §7 names were fixed on
    arrival.
    `RootMotion::worldOrientation` is read: the hips rotation is the body's
    orientation as well as the local rotation, and **both** copies here drop
    it, so a clip read by either loses the body's facing. Consuming the
    package is therefore a behaviour change here too, not only a deletion. And the skeleton
    comes back as joint tokens and rest matrices rather than as a
    `SkeletonDescriptor` — the arrays `BuildSkeletonDescriptor` takes, whose
    descriptor `BuildSourceRestPose` takes after it — so reading a stage there
    links no retargeter.

    A third was found in review and fixed there: the clip's
    `nominalFrameRate` is the rate its samples were taken at, and the stage's
    `timeCodesPerSecond` is where they were written, always 30. The reader
    answered the stage's, so a 60 Hz capture came back claiming 30. It reads
    `customData.motion.nominalFrameRate` now. The `.vrma` reader here is not
    affected: it authors and reads one rate, the stage's.
  - The `Channels` prim is authored and read with it, so USD-O4 is
    implemented as well as decided. That settles nothing further in §8: its
    stage half was already answered on 2026-09-20 and its pose half is still
    open. What §8 gains is one rule the implementation produced — see there.
    The `.vrma` stage here still does not change.
  - ⛔ This repository deletes `StageIo`'s reading half in the consuming
    change, and for the same reason it waits
    ([ost report 41](../reports/ost/41-2026-09-19-v0.22.10-a-library-from-another-repository.md)).
    `ReadClip` becomes a call to `ReadMotionStage` plus this repository's own
    `vrm:` reading — the expression tracks, the gaze track and the clip's
    look-at offset — which the destination refused on purpose.
- ✅ `execMotion` arrived as `usd-motion-plugins`' optional
  `plugins/execMotion` (2026-09-20,
  [usd-motion-plugins #11](https://github.com/animu-sphere/usd-motion-plugins/pull/11)),
  ahead of that repository's v0.5.0. 13 commits came through `git filter-repo`
  into the directory that workspace had reserved, so no move-only commit was
  needed; the rename and the workspace join followed. `execVrm` stays here and
  reads its nodes by name exactly as it does now.
  - The four findings this bundle produced are the point of the arrival:
    every node is one library call there. `motion.filterPose` is
    `PoseFilter::Step`, `motion.extractRootMotion` is `ConditionRootMotion`,
    `motion.interpolatePose` is `SampleClip` over a clip held by reference,
    and `motion.blendPoses` reads the N-way `BlendPoses`' `std::optional`
    instead of checking for nothing weighted first. The wrapper code the
    findings were about is gone rather than moved.
  - What the library answers where a node refuses changed with them, so the
    pins moved too: nothing weighted is `nullopt`, and a NaN weight counts as
    no weight rather than poisoning the blend.
  - One thing the move did not close, and it is the graph's rather than the
    library's: an exec computation's value is a pose, so until a node
    publishes `StepResult::state` as a value of its own, a driver hands the
    result back as the next prior pose and a joint returning after a dropout
    is passed through where the streaming filter would slerp it.
  - Two decisions were taken there with it: that repository's EX-O2 — the
    rate stays a namespaced convention, no schema registers it — and, in
    [#12](https://github.com/animu-sphere/usd-motion-plugins/pull/12), USD-O4,
    the generic channel attribute names. Neither changes the `.vrma` stage:
    [§3](#3-mig-1--the-core-)'s last item still holds. USD-O4 does answer half
    of §8's open question about the `vrm:` expression weights — see there.
  - ⛔ This repository deletes `plugins/execMotion` in the consuming change,
    and for the same reason it waits
    ([ost report 41](../reports/ost/41-2026-09-19-v0.22.10-a-library-from-another-repository.md)).
- ⬜ What stays is re-read as a consumer: the VRM humanoid map,
  `ExpressionResolver`, `LookAtEvaluator`, `motion_retarget` as a VRM CLI,
  `execVrm`. The OpenExec parity values are re-run against the consumed
  packages before anything here is deleted.

## 5. MIG-3 — recorded sources 🚧

- ✅ `motionSource`, `motionBvh`, `motion_bvh_inspect`, `motion_bvh_convert`
  and the producer profiles arrived together, with their history, on
  2026-09-19 ([usd-motion-plugins #8](https://github.com/animu-sphere/usd-motion-plugins/pull/8)). That is ahead of
  that repository's v0.4.0, which still carries them. The profiles are
  installed data there. The arrival names:
  - `motion_bvh_convert` is `motion_convert`, and it authors through
    `motionUsd`, with the producer's rest.
  - `USDVRM_MOTION_PROFILE_PATH` is `USDMOTION_PROFILE_PATH`.
  - The `VRM_BVH_*` codes are `MOTION_BVH_*` (that repository's design policy
    §42.8).
- ⛔ This repository deletes its copies in the same consuming change as
  MIG-1, and for the same reason it waits (ost report 41). The change
  re-runs `workspace_bvh_end_to_end` against the consumed tools.
  `workspace_unicode_paths` loses `motion_bvh_convert` in it, so the
  non-ASCII path case needs a home in `usd-motion-plugins` first.
- ⬜ NPZ / AMASS is no longer this repository's track: its identity decision
  ([the recorded track](recorded-motion-sources.md) §13) moves with
  `motionSource`, behind the versioned NPZ payload contract the motion-plugins
  policy requires first (its §28).

## 6. MIG-4 — recording and live input 🚧

- 🚧 `motion_capture` arrives as `usd-motion-plugins`' recording tool.
  - ✅ Imported with its history as `motion_record`
    ([usd-motion-plugins #10](https://github.com/animu-sphere/usd-motion-plugins/pull/10), merged as f2e7e9b):
    17 commits, without the clip writer, which had arrived as `motionUsd`.
    A move-only commit and the rename followed. It authors through
    `motionUsd`, so its stage is that repository's `/Animation` shape and
    not the `/Capture` scope it authors here. `--missing-bones` is
    `--missing-joints` there, and `--clip-name` is gone.
  - ✅ `motion_capture_replay` travelled as `motion_record_replay`. Its last
    leg bakes the recorded clip onto `docs/design/fixtures/motion/avatar.usda`
    with `motion_retarget`. That leg is this repository's, because it reads a
    VRM avatar, and it stays here: the consuming change re-points it at the
    consumed tool. There, the stage is resolved through a
    `UsdSkelSkeletonQuery` instead.
  - ⬜ This repository deletes `tools/motionCapture` in the consuming change,
    and for the same reason it waits
    ([ost report 41](../reports/ost/41-2026-09-19-v0.22.10-a-library-from-another-repository.md)).
- 🚧 `liveTransport`, `osc`, `motionTracking`, `vrmAdapterVmc`,
  `vrmAdapterMocopi`, `vrmAdapterVrchatOsc` and their record tools arrive in
  `motion-connectors`, which depends on `usd-motion-plugins` and on nothing
  here. They arrive **together**, in `motion-connectors` v0.1.0, and leave
  here in one change ([WORKSPACE.md §9.2](../architecture/WORKSPACE.md#92-moving-rules)
  rule 7). The adapters lose the `vrm` prefix there, as
  `motionConnectorVmc`, `motionConnectorMocopi` and `motionConnectorVrchatOsc`.
  - ✅ The two leaves arrived with their history (2026-09-19):
    `liveTransport` as `motionConnectorTransport`
    ([motion-connectors #2](https://github.com/animu-sphere/motion-connectors/pull/2), 10 commits, merged as 35d01c6) and `osc` as
    `motionConnectorOsc` ([#3](https://github.com/animu-sphere/motion-connectors/pull/3), 6 commits, f600259).
    Each came with its history, then a move-only commit, then the rename.
    The namespaces are `openstrata::connectors::transport` and
    `openstrata::connectors::osc`, and the code is unchanged. Both link
    nothing, so they could go ahead of the `usd-motion-plugins` release that
    everything after them needs. `motion-connectors` rendered its CI with the
    first of them. Its capability matrix claims the capture format, the poll
    mapping and the diagnostic vehicle, but not yet receiving on a socket:
    the socket suites are the adapters' here, and travel with them.
  - ⬜ `motionTracking` and the three adapters link `motionCore`. Importing
    them needs `motion-connectors` to consume `usd-motion-plugins` as an
    installed package, and under `ost` that is the same gap as
    [report 41](../reports/ost/41-2026-09-19-v0.22.10-a-library-from-another-repository.md).
  - ⬜ The OSC suite's corpus half reads the VMC capture format over this
    repository's VMC fixtures. It stays here and travels with
    `vrmAdapterVmc`.
- ⬜ Recorded evidence — capture traces, the cross-source reports' inputs —
  moves with the adapter that produced it, under the same redistribution rule
  it has here.
- ⬜ The ARDY adapter is created there, behind the generator interface
  `usd-motion-plugins` specifies; Motion Phase F does not start here.

## 7. MIG-5 — nothing left behind ⬜

- ⬜ No generic motion source file remains here, checked mechanically.
- ⬜ WORKSPACE.md §1 and §2 describe the reduced tree, §9 becomes a record,
  and `PACKAGE_CONTRACT.md` drops the packages that left.
- ⬜ The aggregate product still installs and opens a `.vrm` and a `.vrma`,
  with the shared core resolved as a dependency, from release artifacts.
- ⬜ The cross-repository test — VRMA → `MotionClip` → a target VRM — runs
  somewhere by default. The motion-plugins policy puts such tests in a runtime
  or integration repository (its §30.6); until one exists it stays here,
  because this repository is its natural integrator.

## 8. Open questions

- **The OpenUSD pin across three repositories.** Each package is built against
  one exact OpenUSD release; a pin change becomes a coordinated release of
  `usd-motion-plugins`, `motion-connectors` and this repository. Who cuts first
  is unsettled.
- **Versions during migration.** Whether this repository requires a range of
  `usd-motion-plugins` releases or one exact version while its API is 0.x.
- **The `vrm:` expression weights on the pose.** Expression weights travel as
  names on today's `HumanoidPose`; in the shared core that is a
  `MotionChannelSet` with namespaced semantics (motion-plugins policy §5.3).
  The mapping is decided in MIG-1, and expansion onto a rig stays here.
  The **stage** half of it was decided there on 2026-09-20 (its USD-O4) and
  implemented with the reading half the same day: a generic channel is one
  typeless prim under `/Animation/Channels` carrying `motion:channelName` —
  the semantic verbatim, and the key — and a time-sampled
  `motion:channelValue`, and `vrm:expressionType` does not come across. The
  `.vrma` stage here keeps `/Animation/Expressions` and its `vrm:expression*`
  attributes; what is still open here is the **pose** half — which names the
  weights carry on a `MotionChannelSet` — and the expansion onto a rig.

  One rule came out of implementing it there, and the consuming change has to
  decide whether it holds here: **a channel is read back only where the stage
  keyed it**, because USD holds the last key forward and a held value is not
  one the producer reported. `ReadClip` here reads an expression weight with
  `UsdAttribute::Get` at the union of key times, so it takes the held value
  instead. Which is right for a `.vrma` is not obvious — the bake that
  consumes it wants a weight at every sample — so it is a question for the
  switch-over, not a defect recorded here.
- **`ExecIr`.** It is VRM-specific and stays, but a generic invertible rig
  could later interest the shared core; nothing is moved on speculation.

## 9. Done when

- Every identity [WORKSPACE.md §9.1](../architecture/WORKSPACE.md#91-destination-of-every-identity)
  gives another destination lives there, with its history.
- This repository consumes them as installed packages and holds no copy.
- Every parity baseline MIG-0 named was reproduced across the move.
- A consumer that has never heard of VRM — `usd-mmd-plugins` — builds against
  the shared core.
