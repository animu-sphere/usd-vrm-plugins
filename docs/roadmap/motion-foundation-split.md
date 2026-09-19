# Motion migration — generic motion to `usd-motion-plugins`, input to `motion-connectors`

**Status:** ✅ MIG-0; 🚧 MIG-1, MIG-2 and MIG-3, their consuming halves blocked on `ost` (report 41); `motionRetarget` arrived 2026-09-19 · **Target:** after the OpenExec foundation ·
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
  - ✅ Two questions were decided at the import. The destination's WS-O2:
    `motionRetarget` depends on `motionCore` alone, so
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
  - ⬜ `StageIo`'s clip and skeleton *reading* arrives as `motionUsd`'s
    reading half. That is also the library home for clip → pose that the
    sampling finding asked for.
- ⬜ `execMotion` arrives as `usd-motion-plugins`' optional
  `plugins/execMotion`; `execVrm` stays and reads its nodes by name exactly
  as it does now.
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

## 6. MIG-4 — recording and live input ⬜

- ⬜ `motion_capture` arrives as `usd-motion-plugins`' recording tool.
- ⬜ `liveTransport`, `osc`, `motionTracking`, `vrmAdapterVmc`,
  `vrmAdapterMocopi`, `vrmAdapterVrchatOsc` and their record tools arrive in
  `motion-connectors`, which depends on `usd-motion-plugins` and on nothing
  here. They arrive **together**, in `motion-connectors` v0.1.0, and leave
  here in one change ([WORKSPACE.md §9.2](../architecture/WORKSPACE.md#92-moving-rules)
  rule 7). The adapters lose the `vrm` prefix there, as
  `motionConnectorVmc`, `motionConnectorMocopi` and `motionConnectorVrchatOsc`.
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
- **`ExecIr`.** It is VRM-specific and stays, but a generic invertible rig
  could later interest the shared core; nothing is moved on speculation.

## 9. Done when

- Every identity [WORKSPACE.md §9.1](../architecture/WORKSPACE.md#91-destination-of-every-identity)
  gives another destination lives there, with its history.
- This repository consumes them as installed packages and holds no copy.
- Every parity baseline MIG-0 named was reproduced across the move.
- A consumer that has never heard of VRM — `usd-mmd-plugins` — builds against
  the shared core.
