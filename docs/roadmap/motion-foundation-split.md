# Motion migration — generic motion to `usd-motion-plugins`, input to `motion-connectors`

**Status:** 🚧 MIG-0 in progress · **Target:** after the OpenExec foundation ·
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

## 2. MIG-0 — preparation 🚧

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
- ⬜ **Hand over the evidence.** The shared core's contract starts from what
  this repository measured: [MOTION_CONTRACT.md](../design/MOTION_CONTRACT.md)'s
  basis, root-and-hips, path-rule and tracker sections, the OpenExec driver
  contract, and the exec layer's findings. They are proposed into
  `usd-motion-plugins`' `MOTION_CONTRACT.md` and `RETARGETING_POLICY.md` as
  cited evidence, not re-derived there.
- ⬜ **Carry the producer conventions v0.9.0 left unauthored.**
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

## 3. MIG-1 — the core ⬜

- ⬜ `motionCore` arrives in `usd-motion-plugins` under the same identity, with its
  history, renamed to the shared names, under `openstrata::motion`.
- ⬜ This repository consumes the installed package: `usdVrmaFileFormat`,
  `motionRuntime`, `vrmRetarget`, `motionSource`, `motionTracking` and the
  adapters switch their edge in the same change that deletes `libs/motionCore`
  ([WORKSPACE.md §9.2](../architecture/WORKSPACE.md#92-moving-rules), rule 1).
  Adapting code here to the renamed types is acceptable during migration
  (motion-plugins policy §37); keeping two cores is not.
- ⬜ The `.vrma` stage does not change: `/Animation`, `HumanoidSkeleton`,
  `BodyAnimation`, the `vrma` custom data. A standalone motion stage in
  `usd-motion-plugins`' shape is a separate decision, not a side effect.

## 4. MIG-2 — sampling, retarget, USD bridge ⬜

- ⬜ `motionRuntime` arrives as `motionSampling` and `motionRecording`, with
  the exec findings fixed on arrival: a status-carrying `SampleClip`, a
  stateless `PoseFilter` step, `ConditionRootMotion` as a free function, an
  N-way blend that can answer *nothing to blend*.
- ⬜ The generic retarget arrives as `motionRetarget`, with a
  `SkeletonDescriptor` built from joint tokens and rest matrices — the
  finding `execVrm` and `motion_retarget` both carry a copy of today.
- ⬜ `StageIo`'s clip and skeleton reading and writing arrive as `motionUsd`,
  which is also the library home for clip → pose the sampling finding asked
  for.
- ⬜ `execMotion` arrives as `usd-motion-plugins`' optional
  `plugins/execMotion`; `execVrm` stays and reads its nodes by name exactly
  as it does now.
- ⬜ What stays is re-read as a consumer: the VRM humanoid map,
  `ExpressionResolver`, `LookAtEvaluator`, `motion_retarget` as a VRM CLI,
  `execVrm`. The OpenExec parity values are re-run against the consumed
  packages before anything here is deleted.

## 5. MIG-3 — recorded sources ⬜

- ⬜ `motionSource`, `motionBvh`, `motion_bvh_inspect`, `motion_bvh_convert`
  and the producer profiles arrive together (motion-plugins policy §26–§27);
  the profiles are installed data there.
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
