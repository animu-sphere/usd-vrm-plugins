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
[current.md](current.md) and is not part of this plan. The
[input adapters](adapters-mocopi-vmc-ardy.md) are a separate track that
deliberately does not wait for anything here.

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
- **A computation evaluates an immutable snapshot and performs no I/O.** No
  socket read, no SDK poll, no file watch, no mutable global state, no hidden
  clock, no private thread pool competing with the OpenExec scheduler (§5).
- **Nothing else in the repository waits for this plan.** Input adapters,
  record/replay, offline retarget, and semantic-clip authoring complete without
  OpenExec, by
  [design](adapters-mocopi-vmc-ardy.md#3-this-track-does-not-wait-for-openexec).
  This plan connects to a finished canonical pipeline; it does not gate one.
- General-purpose rig mechanics come from **`ExecIr`**. `execVrm` does not
  reimplement them.
- **`ExecIr` is an optional, experimental adapter and never a prerequisite.**
  Every dependency on it is confined to an **adapter layer**, and the v0.6.0
  pipeline (canonical motion → retarget → `UsdSkelAnimation`) must remain whole
  with that adapter absent (§7.0).

## 2. Release themes

### v0.6.0 — OpenUSD 26.08 / OpenExec VRM runtime foundation

Fix 26.08 as the only supported runtime, raise the existing offline motion
pipeline to product quality, and complete the first OpenExec VRM pose
evaluation: a VRMA semantic clip evaluated to retargeted joint transforms,
proven equal to the offline result.

```text
VRMA semantic clip
        ↓  OpenExec SampleAnimation
        ↓  OpenExec HumanoidRetarget
computed joint-local transforms
        ↓  numerical parity against motion_retarget      (P0-6)
```

Display is a **separate, narrower** claim in v0.6.0: an exec-computed
`UsdGeomXformable` shown through `usdExecImaging`, because 26.08 cannot register
a `UsdSkel` prim adapter at all (P0-7).

```text
computed xform  ->  usdExecImaging  ->  Hydra / usdview
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

- ✅ Reject any OpenUSD other than 26.08 at configure time. Not through
  `find_package(pxr 26.08 EXACT ...)`: OpenUSD installs no
  `pxrConfigVersion.cmake`, so a version argument makes `find_package` fail
  with "no config version file" whatever OpenUSD is present. `pxrConfig.cmake`
  does publish `PXR_VERSION`, and `cmake/UsdVrmOpenUsd.cmake` tests that,
  included by every entry point that resolves OpenUSD — a bundle built
  standalone by `ost plugin build` never composes the root project, so the pin
  travels with the `find_package` call rather than with the root.
- ✅ Bring the bundle manifests (`openusd: "==26.08"`) and the reference docs
  into agreement. `openstrata.ci.yaml` and the release workflow already pinned
  26.08 runtimes by digest; `VERSION` moves at release prep.
- ✅ Record OpenUSD release, `PXR_VERSION`, and OpenExec availability in
  `buildInfo.json` (`buildInfoSchema` 2). Commit, compiler, and build type were
  already stamped. `openusdVersion` is now the release name (`26.08`) rather
  than `pxrConfig.cmake`'s `PXR_MAJOR.MINOR.PATCH`, which reads `0.26.8`
  because OpenUSD's major version is 0.

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

One correction to the original plan is worth stating: **`build_usd.py` has no
OpenExec toggle.** It ships `exec`, `execGeom`, `execIr`, `execUsd`,
`usdExecImaging`, and `vdf` unconditionally, so every runtime we publish carries
them. The CMake build *does* have one — `PXR_BUILD_EXEC`, default `ON`, forced
off only for Emscripten — so the gate "the build fails on an OpenExec-less
runtime" stays a *detection* requirement, and now defends a configuration a
third party can actually produce. `usdExecImaging` is built either way and is
therefore not evidence of OpenExec; see the migration report
[§1](../reports/openusd/26.08-openexec-migration.md#1-what-2608-actually-ships).

### 4.3 26.08 OpenExec migration report

✅ Done: [reports/openusd/26.08-openexec-migration.md](../reports/openusd/26.08-openexec-migration.md).
It reads the published runtime — the same artifact every CI cell pins — plus the
`v26.08` sources, and covers the whole list this section asked for: `exec`,
`execUsd`, `execIr`, `vdf`, `usdExecImaging`, computation registration,
callable/capturing-lambda callbacks, USD-connection dataflow, cache and
invalidation, batch requests, and Hydra scene-index integration. The published
runtimes were deliberately built with `--examples`, and that sample code
(`share/exec/examples/…/irExampleAuthoringCode`, `pxr.IrExampleAuthoringCode`,
`pxr.IrExampleUsdviewPlugin`) is read in
[§7.4](../reports/openusd/26.08-openexec-migration.md#74-what-the-shipped-example-demonstrates).

The plan's core bet survives: a computation really can be a thin wrapper, because
the registration language is declarative and a callback is a pure function of
resolved inputs. Five findings change scope, each carried into the task below it:

1. **`VtArray` is not an execution value type**, so a pose crosses a computation
   boundary as a registered aggregate — this decides every P0-4 and P0-5
   signature.
2. **`usdExecImaging`'s adapter registry is hard-coded** to `UsdGeomXformable`
   and `ExecIrXformable`, so no `UsdSkel` adapter can be registered — P0-7 is
   re-scoped.
3. **`PXR_BUILD_EXEC` exists** (§4.2 above, corrected), and `usdExecImaging` is
   not evidence of OpenExec — the probe's component list needs amending.
4. **`ExecIr` is per-prim scalar avars in world space**, against `UsdSkel`'s
   joint arrays in joint-local space — a v0.7.0 design item, not an integration
   item.
5. **Inversion is a plugin-level construct in 26.08**, with an in-source TODO
   saying it moves into the core later.

The report's [§9](../reports/openusd/26.08-openexec-migration.md#9-what-this-changes-in-the-plan)
lists all nine consequences; [§10](../reports/openusd/26.08-openexec-migration.md#10-what-this-audit-did-not-do)
is what it did *not* verify — nothing was compiled or run.

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

That rule decides where a live input meets OpenExec, so it is worth drawing:

```text
network / device thread
        ↓
adapter                      (decode, normalize, map to canonical semantics)
        ↓
thread-safe timestamped pose buffer      (motionRuntime)
        ↓
immutable snapshot
        ↓
OpenExec computation
```

A computation is handed a snapshot; it never reaches back for one. OpenExec's
job here is evaluation, dependency tracking, caching, and invalidation — not
receiving. The temptation this forbids is concrete: a `Motion.LivePoseReceive`
computation that opens the socket itself would be shorter to write and would
make every OpenExec property in P0-7 untestable.

**USD connection dataflow.** v0.6.0 restricts itself to a one-to-one chain
(`AnimationSource → SampleAnimation → FilterPose → HumanoidRetarget →
JointTransforms`) within 26.08's limited connection support. Multi-input
connections and graph authoring wait for v0.7.0 or later. The audit sharpened
this from a self-imposed restriction to a documented behavior: the builtin
`computeValue` forwards a computed value across **exactly one** connection to a
same-typed attribute, and silently falls back to the attribute's own resolved
value when there are two
([report §5.1](../reports/openusd/26.08-openexec-migration.md#51-the-connection-rule-the-plan-half-guessed)).
One connection per link is a correctness requirement, and only we can enforce it.
Fan-in exists today through `Relationship().TargetedObjects<T>()` and
`IncomingConnections<T>()`, the latter with no deterministic ordering.

**`usdExecImaging`.** The official route from computed results to Hydra/usdview.
A custom viewer, or writing results back to the stage every frame, is explicitly
not a v0.6.0 requirement — the latter is a standing non-goal
(motion policy §12.1). The plumbing is one environment variable
(`USDIMAGINGGL_ENGINE_ENABLE_EXEC_SCENE_INDEX`), but 26.08 resolves prim
adapters from a hard-coded list rather than from plugins
([report §8.2](../reports/openusd/26.08-openexec-migration.md#82-the-blocker-the-adapter-registry-is-hard-coded)),
which is why P0-7 proves the mechanism on `UsdGeomXformable` and leaves skinned
display to a later milestone.

## 6. v0.6.0 tasks

### P0-1 — OpenUSD 26.08 exact pin 🚧

Reject non-26.08 at configure time; publish the three runtimes (§4.2); update
manifests, docs, and the release workflow; add an OpenUSD/OpenExec capability
probe; write the migration report.

**Done when:** all three OS use one OpenUSD version and one runtime digest per
OS; a runtime without the OpenExec libraries fails the build explicitly; and
`buildInfo.json` reports OpenExec availability.

- ✅ **The pin and the probe are in the tree** (§4.1). One module,
  `cmake/UsdVrmOpenUsd.cmake`, included by the root project and by each bundle,
  library, and tool that resolves OpenUSD.
- ✅ **The refusals are tested.** Every runtime this repo builds against
  satisfies the contract, so on a normal build the pin and the probe are code
  that never fires. `workspace_openusd_contract` drives the module against
  fixture OpenUSD installs — too old, too new, an exec library with no imported
  target, an exec component with no headers — and asserts both that it refuses
  and *why*. A gate nothing exercises is a gate nobody can trust.
- ✅ **Three OS, one OpenUSD, one digest each** (§4.2), since v0.5.0.
- ✅ **The 26.08 OpenExec migration report** (§4.3) —
  [reports/openusd/26.08-openexec-migration.md](../reports/openusd/26.08-openexec-migration.md).
  P0-4 and P0-5 have the input they were waiting on.
- ⬜ **Amend the capability probe** with what the audit found
  ([report §9.1](../reports/openusd/26.08-openexec-migration.md#9-what-this-changes-in-the-plan)):
  `esf`, `esfUsd` and `ef` are unprobed but are transitively required by the
  public exec headers — a runtime missing them fails at *compile* time inside a
  bundle, which is the failure the probe exists to move earlier — and
  `usdExecImaging` carries no information, since it is built whether or not
  `PXR_BUILD_EXEC` is on. `workspace_openusd_contract` gains a fixture case per
  component.

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

✅ **Unblocked and largely delivered by `ost` 0.21.0.** The lane shape existed
nowhere: `ci generate` emitted one job per bundle cell and had no cell for a
library or a workspace, filed as the P0 ask in
[report 28](../reports/ost/28-2026-07-26-v0.20.0-motion-layer-ci-gap.md). Four
`kind: workspace` cells now build the root tree and run its whole CTest suite on
all three OS
([report 33](../reports/ost/33-2026-07-28-v0.21.0-workspace-ci-adoption.md)).
What remains of this task is coverage, not lane shape: the CTest labels above and
the OpenExec/offline parity case, which needs P0-4 and P0-5 first.

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

✅ **Unblocked by `ost` 0.21.0** — no member archive could carry an executable
([report 29](../reports/ost/29-2026-07-26-v0.20.0-openusd-2608-runtime-publish.md),
ask 5). `tools/*/openstrata.tool.yaml` now makes `motion_retarget` and
`motion_capture` tool members of the aggregate product, and `release.yml` stages
them with the bundles. Still open here: the artifact-only smoke above, and two
existing carry-overs — the unverified non-`ost` Windows install path and the
DLL-discovery question in [INSTALL.md](../guides/INSTALL.md).

### P0-4 — minimal `execMotion` bundle ⬜

Computations: `motion.sampleAnimation`, `motion.filterPose`,
`motion.extractRootMotion`, `motion.interpolatePose`, `motion.blendPoses` — each
calling the existing `motionRuntime` API, and each taking a **canonical motion
snapshot** as input rather than a live source (§5).

**Not in scope:** new retarget algorithms, stage mutation, vendor-specific
sources, network input, live device state.

**Mechanism before behavior.** The first spike registers no real computation at
all, so a failure is attributable:

1. type registration for the canonical aggregate
2. an identity computation
3. request compile
4. compute
5. cache hit
6. explicit invalidation
7. discovery from a **packaged** plugin, not a build tree

Only then the real ones, in that order: `sampleAnimation` → `filterPose` →
`extractRootMotion` → `interpolatePose` → `blendPoses`.

**`blendPoses` is last on purpose.** It is the one computation that wants
multiple inputs, and 26.08's builtin `computeValue` forwards across exactly one
connection and silently falls back when there are two (§5, connection dataflow).
Fan-in is reachable only through relationships with no deterministic ordering,
so blending is where a fan-in surprise would surface — after the single-input
chain is proven, not during it.

**One precondition is not yet met.** `motionCore`'s aggregates carry no
`operator==` today, and `ExecTypeRegistry::RegisterType` requires one; step 1
above cannot start until it exists. It is the same addition the adapter corpus
tests want ([adapters plan §11](adapters-mocopi-vmc-ardy.md#11-contract-changes-this-plan-requires))
and the same one P0-6 parity needs, so it is one change serving three callers.

**The value-type constraint is settled, not open**
([report §4](../reports/openusd/26.08-openexec-migration.md#4-value-types-and-the-vtarray-rule)):
`VtArray` is rejected by `ExecTypeRegistry::RegisterType` and by every
`Computation<T>`, so a pose crosses a computation boundary as a `motionCore`
value type registered with `ExecTypeRegistry::RegisterType` — which requires
`operator==` on it, the same thing P0-6 parity needs. Array-valued USD inputs are
declared with their *element* type and consumed with `VdfReadIterator<T>`.

### P0-5 — minimal `execVrm` bundle ⬜

Computations: `vrm.computeHumanoidMap`, `vrm.computeTargetSkeleton`,
`vrm.computeRestPoseCorrection`, `vrm.humanoidRetarget`,
`vrm.computeJointLocalTransforms`.

Inputs: `vrm:humanBones:*`, the typed `Vrm*API` schemas, `UsdSkelSkeleton`,
`UsdSkelAnimation`, and explicit policies/relationships.

**Forbidden:** importer private models, reparsing the source `.vrm` / `.vrma`
bytes, joint-name heuristics, and duplicating an algorithm that already exists in
`vrmRetarget`.

**One obligation the audit added:** `execVrm`'s own `plugInfo.json` must carry an
`Info.Exec.Schemas` block naming every schema it registers on — the `Vrm*API`
schemas, `UsdSkelSkeleton`, `UsdSkelAnimation` — because the block lives with the
*registering* library, not the schema owner, and nothing else declares them. A
missing block fails as "computation not found", not as a load error
([report §2.1](../reports/openusd/26.08-openexec-migration.md#21-the-pluginfo-half)).

### P0-6 — OpenExec / offline parity ⬜

Compare `motion_retarget`'s offline result against the `execMotion` + `execVrm`
computed result on the same input: joint order, translations, rotations, identity
scales, root motion, rest-pose correction, unbound-bone behavior, time sampling,
and diagnostics. The numerical tolerance is written into the contract.

This is the check that keeps a computation a wrapper. v0.4.0 already produced the
mechanism it needs: the design triplet is compared through USD composition at the
value level, not by byte-comparing a layer.

### P0-7 — display smoke, re-scoped to `UsdGeomXformable` ⬜

**Originally:** avatar stage + VRMA semantic animation + an OpenExec request →
computed transforms → `usdExecImaging` → usdview, with a skinned avatar moving.

**Not reachable as written in 26.08.** `UsdExecImagingPrimAdapterInterface` is a
public header that reads like a plugin point, but the registry behind it is a
hard-coded pair of `IsA<>` checks — `UsdGeomXformable` and `ExecIrXformable` —
with a source TODO promising generic plugin registration later. A VRM avatar
posed through `UsdSkel` skinning is neither, so no adapter can be registered
([report §8.2](../reports/openusd/26.08-openexec-migration.md#82-the-blocker-the-adapter-registry-is-hard-coded)).

**Decided 2026-07-29: prove the mechanism on `UsdGeomXformable`.** v0.6.0 ships
a display test over an exec-computed `UsdGeomXformable`, not a skinned avatar:

```text
canonical motion / time input
        ↓  OpenExec computed xform
        ↓  usdExecImaging
usdview
```

**Done when:** changing time recomputes; changing the motion input recomputes;
an unrelated material change does **not** recompute the motion network; it works
from packaged plugins, not a build tree; and the test asserts the
`xformOp:transform` precondition below.

Everything except the prim adapter is then exercised for real — request
compilation, computed dataflow into Hydra, the invalidation properties, and
packaged discovery — so the plumbing risk is retired on the half we control, and
what remains blocked is isolated to one upstream registry.

The other two options considered are **not** dropped; they are re-filed:

- ⬜ **File the upstream ask** for plugin registration of exec imaging adapters.
  This is the only route to the original slice and nothing else in the plan
  advances it, so it is tracked whether or not it is answered.
- ⬜ **Real `UsdSkel` skinning display is its own milestone**, after v0.7.0's
  `ExecIr` track, and is not a v0.6.0 or v0.7.0 release condition. Four routes
  exist, in the order they should be tried: the upstream ask above; an adapter
  via `ExecIrXformable`, where a prim adapter *is* registrable — at the cost of
  the §7.2 shape mismatch; a custom Hydra scene index; or integration outside
  OpenExec entirely, in an application or DCC. **A custom scene index is not the
  first choice**: it carries a standing maintenance cost against an OpenUSD
  version this repository pins exactly.

A display test must assert the stage uses `xformOp:transform` only, or disable
the geom adapter: the exec `UsdGeomXformable` computation reads that one
attribute and **ignores `xformOpOrder`**. Our importer happens to author exactly
that and nothing else, but a composed third-party avatar would draw wrong with no
diagnostic
([report §8.3](../reports/openusd/26.08-openexec-migration.md#83-the-xformoptransform-only-rule)).

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

The three `VRM_OPENEXEC_*` codes have to be produced by our own checks around the
request. 26.08 has exactly one structured compilation error
(`ExecValidationErrorType::DataDependencyCycle`); everything else arrives as
free-text `TF_ERROR` / `TF_RUNTIME_ERROR`, detectable with a `TfErrorMark` but
not classifiable
([report §6](../reports/openusd/26.08-openexec-migration.md#6-requests-evaluation-cache-and-invalidation)).

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
retarget · OpenExec/offline parity · **xform-based** `usdExecImaging` display
smoke · Windows DLL discovery · Unicode paths · resolved-transform validation ·
reproducible packaging · documentation consistency.

Explicitly **not** a v0.6.0 gate: realtime skinned-avatar display (P0-7), any
`ExecIr` dependency, and any input adapter — the last of those ships on its own
track ([adapters plan §13](adapters-mocopi-vmc-ardy.md#13-release-boundaries)).

## 7. v0.7.0 tasks

### 7.0 What `ExecIr` is, and is not

`ExecIr` is an **optional experimental adapter**, evaluated on its own track. It
is not a prerequisite for anything below it in the stack:

```text
required, and finished first:
mocopi / VMC  ->  canonical motion  ->  standard retarget pipeline

optional, connected afterwards:
UsdSkel / VRM semantics  ↕  ExecIr adapter  ↕  ExecIr representation
```

Forbidden, in addition to
[WORKSPACE.md §2](../architecture/WORKSPACE.md):

```text
usdVrmFileFormat  -X->  authoring ExecIr prims as a requirement
motionCore        -X->  ExecIr
vrmRetarget       -X->  ExecIr
adapters/*        -X->  emitting ExecIr values directly
ExecIr            -X->  being the canonical motion contract
```

The last one is the one that would do real damage. `ExecIr` is per-prim scalar
avars in world space; the canonical contract is quaternion arrays in joint-local
space (§7.2 of the migration report). Letting the first shape define the second
would push a rig representation, still documented upstream as "not yet ready for
production use", into every adapter and every offline test in the repository.

### P0-1 — `ExecIr` responsibility audit ⬜

Compare the `execVrm` design against `ExecIr` across joint representation,
controller representation, FK computation, controller switching, compensation,
forward and inverse evaluation, and transform publication. Anything general moves
to `ExecIr`; only VRM semantics stay in `execVrm`.

Start from the shape table in
[report §7.2](../reports/openusd/26.08-openexec-migration.md#72-the-shape-mismatch-with-usdskel).
The real question is not what `execVrm` duplicates but what a `UsdSkel`↔`ExecIr`
conversion costs and where it lives: `ExecIr` is one prim per joint with scalar
Euler avars in **world** space, against one prim holding quaternion arrays in
**joint-local** space. Also note that every `ExecIr` schema's own docstring says
it is "not yet ready for production use", and the switch controller is hard-coded
to two rigs literally named `rig1` and `rig2`.

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

Spring-bone simulation proper, full-body IK, foot locking, contact solving, GPU
computation, per-frame stage write-back, editor UI, Python computation
registration, production-grade arbitrary rig authoring, and realtime skinned
display through `usdExecImaging` (P0-7). Several are permanent non-goals — see
[backlog.md](backlog.md#non-goals).

**Corrected 2026-07-29.** This list previously also deferred "motion generation,
vendor SDK integration, Mocopi / ARDY adapters proper, network transport, live
device discovery". Those are not deferred *behind* this plan — they are a
[parallel track](adapters-mocopi-vmc-ardy.md) that starts from the shipped
v0.5.0 live-capture surface and reaches a retargeted `UsdSkelAnimation` with no
OpenExec involvement. They are unscheduled, which is a different statement.

## 9. Contract changes this plan requires

Structural claims belong in the contracts, in their own change, before this plan
depends on them ([docs/README.md](../README.md)). Open:

- ⬜ **`ExecIr` adapter is not in the workspace contract.**
  [WORKSPACE.md §2](../architecture/WORKSPACE.md)'s dependency tables have no
  `ExecIr adapter` row, and §1's identity table has no adapter entry. §3 and
  §7.0 above state the edges — they need to move into the contract. *(The
  `execVrm -X-> GLB parser` rule and the four §7.0 `ExecIr` prohibitions landed
  in WORKSPACE.md §2 on 2026-07-29; the identity row did not.)*
- ⬜ **`usdExecImaging` has no declared place.** It is an OpenUSD component, not
  a workspace member, but the presentation path through it should be named
  somewhere binding rather than only here.
- ⬜ **Motion Phase E's scope grew.** Motion policy §16 describes Phase E as
  `execMotion` / `execVrm` nodes; this plan adds the display slice (v0.6.0 P0-7)
  and the whole `ExecIr` rig track (v0.7.0). Either Phase E widens or the ladder
  gains a phase.
- ⬜ **The snapshot-input rule needs a contract home.** §5 requires that a
  computation evaluate an immutable snapshot and perform no I/O; motion policy
  §11.4 now states it, but nothing enforces it. The obvious enforcement is a
  `execMotion`/`execVrm` link check for socket, clock, and threading symbols,
  in the way each bundle already proves what it links.
- ⬜ **`operator==` on the `motionCore` aggregates** (P0-4). Required by
  `ExecTypeRegistry::RegisterType`, by P0-6 parity, and by the adapter corpus
  tests. It belongs in
  [MOTION_CONTRACT.md](../design/MOTION_CONTRACT.md) as a stated property of the
  value types, not as an implementation detail discovered at registration time.
- ✅ **The OpenUSD version contract was stated as a range.**
  [SUPPORTED_CONFIGURATIONS.md](../reference/SUPPORTED_CONFIGURATIONS.md) now
  records one supported version and the two mechanisms that enforce it (the
  manifests' `==26.08` and the configure-time module), which is what §4.1
  landed. `scripts/check_docs.py` keeps the doc, the module, and the four
  manifests from drifting apart.
