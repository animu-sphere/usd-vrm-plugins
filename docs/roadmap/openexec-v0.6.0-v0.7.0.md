# v0.6.0 / v0.7.0 — the OpenExec direction

The milestone plan for the two releases after live capture. It fixes OpenUSD at
**26.08 exactly**, promotes OpenExec from an experiment to the product's
execution layer, and then connects VRM humanoids to 26.08's invertible-rig
machinery.

This document holds the **boundaries, priorities, and completion conditions**
only. Where it touches structure it defers: bundle identities and dependency
directions are settled in
[architecture/WORKSPACE.md](../architecture/WORKSPACE.md), and motion semantics
in [design/MOTION_ARCHITECTURE_POLICY.md](../design/MOTION_ARCHITECTURE_POLICY.md).
Items this plan needs from those contracts are listed in
[§9](#9-contract-changes-this-plan-requires) rather than asserted here.

Sequence context: v0.5.0 is **Motion Phase D** (live capture); it is tracked in
[current.md](current.md) and is not part of this plan.

Legend: 🚧 in progress · ⬜ not started · ⛔ blocked

## 1. Governing decisions

- OpenUSD is pinned to **26.08 exactly**. The `>=25.05, <27.0` tolerated range
  is retired.
- OpenExec is a **first-class execution basis from v0.6.0**, not an optional
  experiment.
- `motionCore`, `motionRuntime`, and `vrmRetarget` stay **OpenExec-independent**.
  This is already the binding rule in
  [WORKSPACE.md §2](../architecture/WORKSPACE.md); the plan does not relax it.
- An OpenExec computation is a **thin wrapper over the existing plain C++
  implementation**. It never becomes a second algorithm.
- General-purpose rig mechanics come from **`ExecIr`**. `execVrm` does not
  reimplement them.
- `ExecIr` may be experimental at 26.08, so every dependency on it is confined
  to an **adapter layer**.

## 2. Release themes

### v0.6.0 — OpenUSD 26.08 / OpenExec VRM runtime foundation

Fix 26.08 as the only supported runtime, raise the existing offline motion
pipeline to product quality, and complete the first OpenExec VRM pose
evaluation: a minimal vertical slice from a VRMA semantic clip to retargeted
joint transforms visible in usdview.

```text
VRMA semantic clip
        ↓  OpenExec SampleAnimation
        ↓  OpenExec HumanoidRetarget
computed joint-local transforms
        ↓  usdExecImaging
Hydra / usdview
```

In parallel, offline retarget must be reachable from published artifacts alone:

```text
avatar.vrm + walk.vrma  →  motion_retarget  →  retargeted.usda
```

### v0.7.0 — OpenExec invertible VRM humanoid rig

Connect VRM humanoids to the `ExecIr` invertible-rig support 26.08 strengthened,
so both directions work: controller/avar → pose, and pose → controller state.

```text
VRM humanoid semantics
        ↓  ExecIr-compatible rig
        ↓  FK / switch controller
forward and inverse evaluation
        ↓
Hydra / authoring integration
```

## 3. Layering

The execution and presentation layers are new; the import and core layers are
what v0.1.0–v0.4.0 already shipped.

| Layer | Members | Produces |
| --- | --- | --- |
| Import | `usdVrmFileFormat`, `usdVrmaFileFormat`, `usdVrmPackageResolver`, `vrmSchema` | deterministic USD data |
| Core | `motionCore`, `motionRuntime`, `vrmRetarget` | plain C++ values and algorithms |
| Execution | `execMotion`, `execVrm`, the `ExecIr` adapter | OpenExec computations |
| Presentation / application | `usdExecImaging`, Hydra / usdview, `motion_retarget` | user-visible results |

Required directions, additional to
[WORKSPACE.md §2](../architecture/WORKSPACE.md):

```text
ExecIr adapter  -> execVrm, OpenExec / ExecIr
adapters/*      -> motionCore, motionRuntime
```

Forbidden, additional to the same section:

```text
execVrm  -X-> GLB / VRM JSON reparse
```

`execVrm`'s only input contract is what is on the stage: typed schemas,
attributes, relationships, and `UsdSkel` data.

## 4. OpenUSD 26.08 adoption

### 4.1 Exact pin

- Reject any OpenUSD other than 26.08 at configure time —
  `find_package(pxr 26.08 EXACT REQUIRED CONFIG)`, plus a header/build-metadata
  check if `pxrConfig.cmake`'s version test proves insufficient.
- Bring `VERSION`, the bundle manifests, `openstrata.ci.yaml`, the release
  workflow, and the reference docs into agreement.
- Record the OpenUSD version, commit, compiler, ABI, and OpenExec availability in
  `buildInfo.json`.

### 4.2 Runtime artifacts

Digest-pinned 26.08 runtimes on all three OS, fixing the OpenUSD source commit,
compiler / standard library ABI, Python, TBB, MaterialX, and the artifact digest.

- ✅ **Windows x86_64** — published 2026-07-26.
- ✅ **Linux x86_64** — published 2026-07-26.
- ✅ **macOS arm64** — published 2026-07-26. "All three OS on one OpenUSD
  version" is met. It required Apple clang 16 and the macOS 15.2 SDK; the
  macOS 14.5 SDK cannot compile 26.08's Hd data sources under C++17.

Digests and verification evidence are in
[report 29](../reports/ost/29-2026-07-26-v0.20.0-openusd-2608-runtime-publish.md)
(Windows, Linux) and
[report 30](../reports/ost/30-2026-07-26-v0.20.0-macos-2608-runtime-publish.md)
(macOS arm64).

One correction to the original plan is worth stating: **26.08 has no OpenExec
build toggle.** `build_usd.py` ships `exec`, `execGeom`, `execIr`, `execUsd`,
`usdExecImaging`, and `vdf` unconditionally. So the gate "the build fails on an
OpenExec-less runtime" is a *detection* requirement — probe for the libraries and
headers — not a build-option requirement.

### 4.3 26.08 OpenExec migration report

Verify against real 26.08 headers and sample code, and write it up:
`exec`, `execUsd`, `execIr`, `vdf`, `usdExecImaging`, computation registration,
callable/capturing-lambda callbacks, USD-connection dataflow, cache and
invalidation, batch requests, and Hydra scene-index integration.

Output: `docs/reports/openusd/26.08-openexec-migration.md` (a new `reports/`
subdirectory, alongside the `ost` series).

The published runtimes were deliberately built with `--examples`, so the audit
has working 26.08 code to read: `share/exec/examples/…/irExampleAuthoringCode`,
`pxr.IrExampleAuthoringCode`, and `pxr.IrExampleUsdviewPlugin` ship inside the
runtime artifact.

## 5. 26.08 features this plan leans on

**Invertible rigs / `ExecIr`.** Joint scope, FK controller, switch controller,
rig switching, switch compensation, forward and inverse evaluation. The split is
`execVrm` = VRM semantics, `ExecIr` = general-purpose rig mechanics; `execVrm`
builds no FK/controller framework of its own. Because `ExecIr` may be
experimental, the dependency sits behind an adapter.

**Callable computation callbacks.** OpenExec callbacks call the existing plain
C++ API directly. Each callback is a pure function: no side effects, no stage
mutation, no mutable global state, no network or file I/O, no hidden clock, and
no private thread pool competing with the OpenExec scheduler.

**USD connection dataflow.** v0.6.0 restricts itself to a one-to-one chain
(`AnimationSource → SampleAnimation → FilterPose → HumanoidRetarget →
JointTransforms`) within 26.08's limited connection support. Multi-input
connections and graph authoring wait for v0.7.0 or later.

**`usdExecImaging`.** The official route from computed results to Hydra/usdview.
A custom viewer, or writing results back to the stage every frame, is explicitly
not a v0.6.0 requirement — the latter is a standing non-goal
(motion policy §12.1).

## 6. v0.6.0 tasks

### P0-1 — OpenUSD 26.08 exact pin ⬜

Reject non-26.08 at configure time; publish the three runtimes (§4.2); update
manifests, docs, and the release workflow; add an OpenUSD/OpenExec capability
probe; write the migration report.

**Done when:** all three OS use one OpenUSD version and one runtime digest per
OS; a runtime without the OpenExec libraries fails the build explicitly; and
`buildInfo.json` reports OpenExec availability.

### P0-2 — motion layer CI ⬜

Per-bundle cells do not cover plain libraries or executables, so a root-workspace
lane becomes mandatory: configure the root workspace, build every library, bundle,
and `motion_retarget`, then run `ctest`, `ost plugin test --workspace`, the
clean-install smoke, and the artifact-only smoke.

CTest labels: `motion.core`, `motion.runtime`, `motion.retarget`, `motion.cli`,
`motion.integration`, `motion.openexec`, `motion.real-corpus`.

Required coverage: quaternion interpolation, pose filtering, resampling, missing
sample hold, root-motion policy, non-identity rest-pose correction, partial
humanoid mapping, invalid mapping, output/input path collision, resolved
`UsdSkel` transforms, Windows Unicode paths, packaged CLI execution, and
OpenExec/offline parity.

⛔ **Blocked on `ost`** for the lane shape — `ci generate` emits one job per
bundle cell and has no cell for a library or a workspace. Filed as the P0 ask in
[report 29](../reports/ost/29-2026-07-26-v0.20.0-openusd-2608-runtime-publish.md).

### P0-3 — `motion_retarget` distribution ⬜

Ship the CLI in the aggregate product artifact:

```text
bin/motion_retarget · plugins/ · runtime/libraries/ · share/usd-vrm-plugins/ · licenses/ · buildInfo.json
```

Requires Windows DLL discovery, a non-`ost` install path, artifact-only
execution, build-tree dependency and source-path leak scans, an executable
checksum, and `--version` / `--build-info`.

Artifact-only smoke: `motion_retarget --avatar avatar.vrm --animation walk.vrma
--output retargeted.usda`, checking plugin discovery, embedded texture
resolution, humanoid map loading, retarget execution, animation binding,
evaluated joint transforms, and the absence of any build-tree dependency.

⛔ **Blocked on `ost`** — no member archive can carry an executable
([report 29](../reports/ost/29-2026-07-26-v0.20.0-openusd-2608-runtime-publish.md),
ask 5). Two existing carry-overs land here too: the unverified non-`ost` Windows
install path, and the DLL-discovery question in
[INSTALL.md](../guides/INSTALL.md).

### P0-4 — minimal `execMotion` bundle ⬜

Computations: `motion.sampleAnimation`, `motion.interpolatePose`,
`motion.filterPose`, `motion.blendPoses`, `motion.extractRootMotion` — each
calling the existing `motionRuntime` API.

**Not in scope:** new retarget algorithms, stage mutation, vendor-specific
sources, network input, live device state.

### P0-5 — minimal `execVrm` bundle ⬜

Computations: `vrm.computeHumanoidMap`, `vrm.computeTargetSkeleton`,
`vrm.computeRestPoseCorrection`, `vrm.humanoidRetarget`,
`vrm.computeJointLocalTransforms`.

Inputs: `vrm:humanBones:*`, the typed `Vrm*API` schemas, `UsdSkelSkeleton`,
`UsdSkelAnimation`, and explicit policies/relationships.

**Forbidden:** importer private models, reparsing the source `.vrm` / `.vrma`
bytes, joint-name heuristics, and duplicating an algorithm that already exists in
`vrmRetarget`.

### P0-6 — OpenExec / offline parity ⬜

Compare `motion_retarget`'s offline result against the `execMotion` + `execVrm`
computed result on the same input: joint order, translations, rotations, identity
scales, root motion, rest-pose correction, unbound-bone behavior, time sampling,
and diagnostics. The numerical tolerance is written into the contract.

This is the check that keeps a computation a wrapper. v0.4.0 already produced the
mechanism it needs: the design triplet is compared through USD composition at the
value level, not by byte-comparing a layer.

### P0-7 — `usdExecImaging` vertical slice ⬜

Avatar stage + VRMA semantic animation + an OpenExec request → computed
transforms → `usdExecImaging` → usdview.

**Done when:** changing animation time updates the displayed pose; changing the
humanoid mapping invalidates the right network; an unrelated material change does
*not* recompute the motion network; and the whole thing works from packaged
plugins.

### P1-1 — retarget diagnostics ⬜

Freeze the codes:

```text
VRM_RETARGET_MISSING_REQUIRED_BONE   VRM_RETARGET_UNBOUND_DRIVEN_BONE
VRM_RETARGET_DUPLICATE_TARGET        VRM_RETARGET_INVALID_HIERARCHY
VRM_RETARGET_NON_UNIT_SCALE          VRM_RETARGET_TIME_RANGE_DERIVED
VRM_RETARGET_OUTPUT_COLLIDES_WITH_INPUT
VRM_OPENEXEC_COMPUTATION_UNAVAILABLE VRM_OPENEXEC_TYPE_MISMATCH
VRM_OPENEXEC_INVALIDATED
```

…and the CLI exit codes: `0` success, `1` invalid user input, `2` unsupported
source feature, `3` stage/plugin failure, `4` retarget contract violation,
`5` output authoring failure, `6` OpenExec evaluation failure.

### P1-2 — scale policy ⬜

Always author identity scale; animated joint scale is unsupported; a non-unit
animated scale input is a structured warning; scale animation is never silently
applied; OpenExec and offline behave identically. This formalizes the fix that
shipped with the v0.4.0 tag — see
[UsdSkel resolves a scale-less animation to the rest pose](current.md).

### P1-3 — partial skeleton policy ⬜

Make a contract of: a bone in the clip but not the target; a bone in the target
the clip does not drive; a missing required humanoid bone; missing optional
finger/eye/jaw bones; duplicate mappings; hierarchy mismatch; a non-identity
parent rest transform.

### v0.6.0 release gate

Every item green, no exceptions: OpenUSD 26.08 exact · OpenExec-capable runtime ·
three-OS root workspace build · all libraries, bundles, and tools tested ·
`ost plugin test --workspace` · packaged `motion_retarget` · artifact-only offline
retarget · OpenExec/offline parity · `usdExecImaging` display smoke · Windows DLL
discovery · Unicode paths · resolved-transform validation · reproducible
packaging · documentation consistency.

## 7. v0.7.0 tasks

### P0-1 — `ExecIr` responsibility audit ⬜

Compare the `execVrm` design against `ExecIr` across joint representation,
controller representation, FK computation, controller switching, compensation,
forward and inverse evaluation, and transform publication. Anything general moves
to `ExecIr`; only VRM semantics stay in `execVrm`.

### P0-2 — VRM humanoid → `ExecIr` adapter ⬜

`VrmHumanoidAPI` + `UsdSkelSkeleton` + `vrm:humanBones:*` → an `ExecIr` joint
scope / controller representation.

**Done when:** a VRM humanoid mapping builds an `ExecIr`-compatible rig without
importer private APIs; missing mappings are diagnosable; and disabling the
adapter leaves the core and offline pipelines working.

### P0-3 — FK controller ⬜

Hips, spine chain, neck/head, arms, legs, hands/feet — humanoid major bones
first, full finger coverage is P1. **Done when:** controller values forward-
compute a joint pose, the target rest pose is preserved, non-driven bones hold
rest, and OpenExec cache/invalidation is correct.

### P0-4 — switch controller ⬜

Explicit switching between motion sources (VRMA clip, manual FK, live adapter,
blended pose). **Done when:** switching suppresses pose discontinuity,
compensation is selectable, the switching logic is authorable as stage data, and
no source-name heuristic exists.

### P0-5 — inverse evaluation ⬜

Recover controller/avar state from a pose, initially for the humanoid FK
controller, hips translation, head orientation, and arm/leg major joints.
**Not in scope:** full-body IK, foot locking, contact solving, arbitrary
constraint solving.

### P0-6 — OpenExec evaluation client ⬜

One `UsdStage`, one long-lived `ExecUsdSystem`, reusable batch request sets —
never a per-frame `ExecUsdSystem`. Needs batch requests, invalidation callbacks,
time-range invalidation, result dump, performance trace, and
provider/computation diagnostics.

### P0-7 — invalidation tests ⬜

Confirm that only the necessary values are invalidated by each of: animation
time, humanoid mapping, skeleton rest pose, controller value, switch source,
root-motion policy, look-at target, expression weight, and an unrelated material
attribute.

### P1-1 — expression computations ⬜

`vrm.resolveExpressionWeights`, `vrm.computeMorphTargetWeights`,
`vrm.computeMaterialColorOverrides`, `vrm.computeTextureTransformOverrides`.
v0.7.0 produces the results; it does not write them back to the stage
continuously.

### P1-2 — look-at computations ⬜

`vrm.computeLookAt` and `vrm.applyLookAtToPose`. In: head transform, eye origin,
target position, VRM range maps, expression/bone mode. Out: yaw/pitch, eye
rotations, expression weights, updated humanoid pose.

### P1-3 — performance baseline ⬜

Measure cold graph build, first compute, warm compute, time-only update,
controller update, mapping update, and skeleton update, at 1, 10, and 100
avatars. Metrics: graph build time, compute time, cache hit ratio, invalidated
node count, peak memory, thread scaling.

### P1-4 — graph diagnostics ⬜

Make visible during development: registered computation names, provider
resolution, dependency edges, result types, batch request contents, invalidation
cause, cache hit/miss, and computation timings.

### v0.7.0 release gate

A VRM humanoid builds an `ExecIr`-compatible rig · FK forward evaluation works ·
switch controller and compensation work · limited inverse evaluation works · no
responsibility overlap between offline, `execMotion`, `execVrm`, and `ExecIr` ·
expression or look-at works end to end · controller results are visible in
usdview · the OpenExec plugins work from packaged artifacts alone · invalidation
and cache reuse are testable · every experimental `ExecIr` dependency is inside
the adapter.

## 8. Deferred past v0.7.0

Spring-bone simulation proper, full-body IK, foot locking, contact solving,
motion generation, GPU computation, vendor SDK integration, Mocopi / ARDY
adapters proper, network transport, live device discovery, per-frame stage
write-back, editor UI, Python computation registration, and production-grade
arbitrary rig authoring. Several are permanent non-goals — see
[backlog.md](backlog.md#non-goals).

## 9. Contract changes this plan requires

Structural claims belong in the contracts, in their own change, before this plan
depends on them ([docs/README.md](../README.md)). Open:

- ⬜ **`ExecIr` adapter is not in the workspace contract.**
  [WORKSPACE.md §2](../architecture/WORKSPACE.md)'s dependency tables have no
  `ExecIr adapter` row and no `execVrm -X-> GLB / VRM JSON reparse` rule; §1's
  identity table has no adapter entry. §3 above states both — they need to move
  into the contract.
- ⬜ **`usdExecImaging` has no declared place.** It is an OpenUSD component, not
  a workspace member, but the presentation path through it should be named
  somewhere binding rather than only here.
- ⬜ **Motion Phase E's scope grew.** Motion policy §16 describes Phase E as
  `execMotion` / `execVrm` nodes; this plan adds the `usdExecImaging` slice
  (v0.6.0 P0-7) and the whole `ExecIr` rig track (v0.7.0). Either Phase E widens
  or the ladder gains a phase.
- ⬜ **The OpenUSD version contract is stated as a range.**
  [SUPPORTED_CONFIGURATIONS.md](../reference/SUPPORTED_CONFIGURATIONS.md) records
  the tolerated range that §4.1 retires. It changes when the pin actually lands,
  not before.
