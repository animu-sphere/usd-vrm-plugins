# The OpenExec foundation

> **Target: no version — it follows the producer tracks.** The one place a track
> carries a version is the
> [roadmap status table](README.md#status-at-a-glance); this block mirrors it and
> nothing else in this document states a release number for its own work. The
> `ExecIr` invertible rig is on its own track after this one.
>
> **Re-ordered 2026-08-29**, and this is the third time this plan's position has
> moved. It was v0.6.0, then v0.8.0, and now it sits behind packaging hardening,
> the tracker path, the NPZ/AMASS reader and the canonical producer contract. The
> argument is the same one that moved it the first two times, applied to what is
> now unfinished: a compute layer is worth what the contracts under it are worth,
> and two of those contracts are open — no external consumer has ever resolved
> this workspace's package closure, and four input categories each answered the
> producer question separately. **Nothing here is withdrawn.** The prerequisites
> §6 records as met stay met, and the re-order changes when this plan starts, not
> what it is. It takes a version number when the release before it is cut.
>
> **Renamed 2026-08-03**, from `openexec-v0.6.0-v0.7.0.md`. The filename carried
> two version numbers and both moved: v0.6.0 shipped
> [VMC input](../releases/v0.6.0.md) instead, and this plan moved back so that
> [v0.7.0](adapters-mocopi-vmc-ardy.md)'s recorded sessions from a real
> device and real senders exist *before* they are used as parity input (§4.6). A
> version-free name is what stops the next re-ordering from leaving a filename
> behind — and it has now saved two renames rather than one.

This document holds the **boundaries, priorities, and completion conditions**
only. Where it touches structure it defers: bundle identities and dependency
directions are settled in
[architecture/WORKSPACE.md](../architecture/WORKSPACE.md), and motion semantics
in [design/MOTION_ARCHITECTURE_POLICY.md](../design/MOTION_ARCHITECTURE_POLICY.md).
Items this plan needs from those contracts are listed in
[§9](#9-contract-changes-this-plan-requires) rather than asserted here.

Sequence context: the canonical pipeline this plan attaches to is already
finished. v0.5.0 shipped **Motion Phase D** (live capture), v0.6.0 the
**VMC adapter** over it, and v0.7.0 the mocopi native adapter and the generic
BVH pipeline beside it. This plan re-evaluates that pipeline through OpenExec — it does
not extend it, and nothing in it is a prerequisite for anything in
[the adapter track](adapters-mocopi-vmc-ardy.md). The dependency runs the other
way, and only for evidence: v0.7.0's recorded corpus is this plan's parity input.

Legend: 🚧 in progress · ⬜ not started · ⛔ blocked

## 1. Governing decisions

- OpenUSD is pinned to **26.08 exactly**. The `>=25.05, <27.0` tolerated range
  is retired. This landed early, in v0.6.0, and is the one part of this plan
  already in the tree (§4.1).
- OpenExec is a **first-class execution basis** once this plan lands, not an
  optional experiment. Until then the 26.08 pin requires an OpenExec-capable
  runtime without anything depending on it, which is deliberate: the refusal is
  in place before the first computation, not after it.
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
  Every dependency on it is confined to an **adapter layer**, and the shipped
  pipeline (canonical motion → retarget → `UsdSkelAnimation`) must remain whole
  with that adapter absent (§7.0).

## 2. Release themes

### The foundation — OpenExec VRM runtime evaluation

Complete the first OpenExec VRM pose evaluation: a semantic clip evaluated to
retargeted joint transforms, proven equal to the offline result **on the same
recorded input**.

```text
VRMA semantic clip / recorded VMC or mocopi session
        ↓  OpenExec SampleAnimation
        ↓  OpenExec HumanoidRetarget
computed joint-local transforms
        ↓  numerical parity against motion_retarget      (P0-6)
```

Display is a **separate, narrower** claim: an exec-computed `UsdGeomXformable`
shown through `usdExecImaging`, because 26.08 cannot register a `UsdSkel` prim
adapter at all (P0-7).

```text
computed xform  ->  usdExecImaging  ->  Hydra / usdview
```

In parallel, offline retarget must be reachable from published artifacts alone:

```text
avatar.vrm + walk.vrma  →  motion_retarget  →  retargeted.usda
```

### The track after it — OpenExec invertible VRM humanoid rig

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

## 4. OpenUSD 26.08 adoption — done

The exact pin, the three digest-pinned runtimes and the migration report all
landed before this plan starts, and each is recorded where it belongs:
`cmake/UsdVrmOpenUsd.cmake` and the bundle manifests' `==26.08` carry the pin
(the mechanism, and why `find_package(pxr 26.08 EXACT)` can never work, is in
[SUPPORTED_CONFIGURATIONS.md](../reference/SUPPORTED_CONFIGURATIONS.md), kept
from drifting by `scripts/check_docs.py`); `buildInfo.json` schema 2 stamps the
release, `PXR_VERSION` and OpenExec availability; the Windows, Linux and macOS
arm64 runtimes were published 2026-07-26 with digests and evidence in
[report 29](../reports/ost/29-2026-07-26-v0.20.0-openusd-2608-runtime-publish.md)
and
[report 30](../reports/ost/30-2026-07-26-v0.20.0-macos-2608-runtime-publish.md);
and the audit is
[reports/openusd/26.08-openexec-migration.md](../reports/openusd/26.08-openexec-migration.md),
read off the published runtime and the `v26.08` sources with nothing compiled or
run.

Two results of that work shape the tasks below rather than merely recording
them.

**`build_usd.py` has no OpenExec toggle.** It ships `exec`, `execGeom`,
`execIr`, `execUsd`, `usdExecImaging` and `vdf` unconditionally, so every
runtime we publish carries them; the CMake build *does* have one
(`PXR_BUILD_EXEC`, default `ON`). So the gate "the build fails on an
OpenExec-less runtime" stays a *detection* requirement, and `usdExecImaging` is
not evidence of OpenExec because it is built either way.

**The plan's core bet survives — a computation really can be a thin wrapper,
because the registration language is declarative and a callback is a pure
function of resolved inputs — and five findings change scope**, each carried
into the task below it:

1. **`VtArray` is not an execution value type**, so a pose crosses a computation
   boundary as a registered aggregate — this decides every P0-4 and P0-5
   signature.
2. **`usdExecImaging`'s adapter registry is hard-coded** to `UsdGeomXformable`
   and `ExecIrXformable`, so no `UsdSkel` adapter can be registered — P0-7 is
   re-scoped.
3. **`PXR_BUILD_EXEC` exists** and `usdExecImaging` is not evidence of OpenExec
   — the probe's component list needs amending.
4. **`ExecIr` is per-prim scalar avars in world space**, against `UsdSkel`'s
   joint arrays in joint-local space — an `ExecIr`-track design item, not an
   integration item.
5. **Inversion is a plugin-level construct in 26.08**, with an in-source TODO
   saying it moves into the core later.

The report's [§9](../reports/openusd/26.08-openexec-migration.md#9-what-this-changes-in-the-plan)
lists all nine consequences; [§10](../reports/openusd/26.08-openexec-migration.md#10-what-this-audit-did-not-do)
is what it did *not* verify.

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

**USD connection dataflow.** This plan restricts itself to a one-to-one chain
(`AnimationSource → SampleAnimation → FilterPose → HumanoidRetarget →
JointTransforms`) within 26.08's limited connection support. Multi-input
connections and graph authoring wait for the `ExecIr` track or later. The audit sharpened
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
not a requirement here — the latter is a standing non-goal
(motion policy §12.1). The plumbing is one environment variable
(`USDIMAGINGGL_ENGINE_ENABLE_EXEC_SCENE_INDEX`), but 26.08 resolves prim
adapters from a hard-coded list rather than from plugins
([report §8.2](../reports/openusd/26.08-openexec-migration.md#82-the-blocker-the-adapter-registry-is-hard-coded)),
which is why P0-7 proves the mechanism on `UsdGeomXformable` and leaves skinned
display to a later milestone.

## 6. Foundation tasks

### P0-1 — OpenUSD 26.08 exact pin 🚧

Reject non-26.08 at configure time; publish the three runtimes (§4.2); update
manifests, docs, and the release workflow; add an OpenUSD/OpenExec capability
probe; write the migration report.

**Done when:** all three OS use one OpenUSD version and one runtime digest per
OS; a runtime without the OpenExec libraries fails the build explicitly; and
`buildInfo.json` reports OpenExec availability.

The pin, the probe, the three runtimes and the migration report are done (§4).
The refusals are *tested* rather than merely present: every runtime this repo
builds against satisfies the contract, so on a normal build both are code that
never fires — `workspace_openusd_contract` drives the module against fixture
OpenUSD installs (too old, too new, an exec library with no imported target, an
exec component with no headers) and asserts both that it refuses and why. What
is left:

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

**The lane shape is delivered**: four `ost` 0.21.0 `kind: workspace` cells build
the root tree and run its whole CTest suite on all three OS
([report 33](../reports/ost/33-2026-07-28-v0.21.0-workspace-ci-adoption.md)).
What remains of this task is coverage, not lane shape — the CTest labels above
and the OpenExec/offline parity case, which needs P0-4 and P0-5 first.

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

**The CLIs ship**: `tools/*/openstrata.tool.yaml` makes `motion_retarget` and
`motion_capture` tool members of the aggregate product and `release.yml` stages
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

**The precondition step 1 was blocked on is met.** `ExecTypeRegistry::RegisterType`
requires `operator==` on the type it registers, and `motionCore`'s aggregates
carried none until v0.6.0 added it — together with `NearlyEqual`, because the
adapter corpus
([adapters plan §11](adapters-mocopi-vmc-ardy.md#11-contract-changes-this-plan-requires))
and P0-6 parity wanted a tolerant comparison rather than the exact one. One
change, three callers, and the two answers are documented as different questions
rather than as one comparison with a knob
([MOTION_CONTRACT.md](../design/MOTION_CONTRACT.md#comparison-semantics-v060)).

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

**The representative input is recorded, not generated** — and that is the whole
reason this plan sits behind the adapter releases. Parity over generated fixtures
proves that two implementations agree about generated data; it says nothing about
either one against what a device or a sender actually emits. So the redistributable
captures v0.7.0 records are the primary parity input, and the generated corpus
stays as the shape coverage a real session cannot be relied on to contain:

```text
generated neutral                     recorded mocopi neutral
generated interpolation               recorded mocopi arm raise
generated missing bone                recorded mocopi root motion
                                      recorded tracking loss and recovery
                                      recorded sender restart
                                      recorded second VMC sender shape
```

Two comparisons, not one, and they are not interchangeable
([MOTION_CONTRACT.md](../design/MOTION_CONTRACT.md#comparison-semantics-v060)):

- serialization and registered-value identity → `operator==`
- offline vs OpenExec motion equivalence → `NearlyEqual`

**A failing case is classified, never widened.** Reaching for a larger epsilon is
how a real divergence becomes a tolerance. The categories, in the order they are
cheap to rule out: quaternion sign only · floating-point rounding ·
provenance-only difference · ordering difference · missing-field semantics ·
root-motion policy difference · actual algorithm divergence. Only the last is a
defect in this plan's sense; the rest are contract questions that get an answer
in the contract.

### P0-7 — display smoke, re-scoped to `UsdGeomXformable` ⬜

**Originally:** avatar stage + VRMA semantic animation + an OpenExec request →
computed transforms → `usdExecImaging` → usdview, with a skinned avatar moving.

**Not reachable as written in 26.08.** `UsdExecImagingPrimAdapterInterface` is a
public header that reads like a plugin point, but the registry behind it is a
hard-coded pair of `IsA<>` checks — `UsdGeomXformable` and `ExecIrXformable` —
with a source TODO promising generic plugin registration later. A VRM avatar
posed through `UsdSkel` skinning is neither, so no adapter can be registered
([report §8.2](../reports/openusd/26.08-openexec-migration.md#82-the-blocker-the-adapter-registry-is-hard-coded)).

**Decided 2026-07-29: prove the mechanism on `UsdGeomXformable`.** This plan
ships a display test over an exec-computed `UsdGeomXformable`, not a skinned avatar:

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
- ⬜ **Real `UsdSkel` skinning display is its own milestone**, after the
  `ExecIr` track, and is a release condition for neither. Four routes
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

### Foundation release gate

The boundary in one list: `execMotion` and `execVrm` both exist · the `motionCore`
aggregates are registered OpenExec value types · a computation evaluates an
immutable snapshot · no socket, device, or wall-clock I/O inside a computation ·
offline and OpenExec agree under `NearlyEqual` on the **same recorded input** ·
computation discovery succeeds from a packaged plugin rather than a build tree ·
the dependency boundary is checked by the workspace gate.

Every item green, no exceptions: OpenUSD 26.08 exact · OpenExec-capable runtime ·
three-OS root workspace build · all libraries, bundles, and tools tested ·
`ost plugin test --workspace` · packaged `motion_retarget` · artifact-only offline
retarget · OpenExec/offline parity · **xform-based** `usdExecImaging` display
smoke · Windows DLL discovery · Unicode paths · resolved-transform validation ·
reproducible packaging · documentation consistency.

Explicitly **not** a gate here, each for a different reason:

- **realtime skinned-avatar display** — upstream-blocked (P0-7);
- **any `ExecIr` dependency** — a later track, and optional even there (§7.0);
- **network I/O inside a computation**, and any mocopi SDK reaching OpenExec —
  permanent non-goals, not deferrals ([backlog](backlog.md#non-goals));
- **`ExecIr` as a requirement** rather than an adapter;
- **merging an adapter into an exec bundle** — the two are separate boundaries
  ([adapters plan §13](adapters-mocopi-vmc-ardy.md#13-release-boundaries));
- **runtime evaluation moved back into the importer** — the project's central
  boundary, and it does not move for this;
- **expression, look-at, and spring-bone at once** — those follow the `ExecIr`
  track and Motion Phase G;
- **any input adapter** — that track ships on its own release boundary.

## 7. The `ExecIr` track

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
This track produces the results; it does not write them back to the stage
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

### `ExecIr` track release gate

A VRM humanoid builds an `ExecIr`-compatible rig · FK forward evaluation works ·
switch controller and compensation work · limited inverse evaluation works · no
responsibility overlap between offline, `execMotion`, `execVrm`, and `ExecIr` ·
expression or look-at works end to end · controller results are visible in
usdview · the OpenExec plugins work from packaged artifacts alone · invalidation
and cache reuse are testable · every experimental `ExecIr` dependency is inside
the adapter.

## 8. Deferred past the `ExecIr` track

Spring-bone simulation proper, full-body IK, foot locking, contact solving, GPU
computation, per-frame stage write-back, editor UI, Python computation
registration, production-grade arbitrary rig authoring, and realtime skinned
display through `usdExecImaging` (P0-7). Several are permanent non-goals — see
[backlog.md](backlog.md#non-goals).

**Corrected 2026-07-29, and again on 2026-08-03.** This list previously also
deferred "motion generation, vendor SDK integration, Mocopi / ARDY adapters
proper, network transport, live device discovery". Those were never deferred
*behind* this plan — they are the
[adapter track](adapters-mocopi-vmc-ardy.md), which starts from the shipped
v0.5.0 live-capture surface and reaches a retargeted `UsdSkelAnimation` with no
OpenExec involvement. The July correction called them "unscheduled"; they are now
scheduled **ahead** of this plan, and one of them is a prerequisite for its
evidence rather than a parallel curiosity (§4.6, P0-6).

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
  `execMotion` / `execVrm` nodes; this plan adds the display slice (P0-7) and the
  whole `ExecIr` rig track (§7). Either Phase E widens or the ladder
  gains a phase.
- ⬜ **The snapshot-input rule needs a contract home.** §5 requires that a
  computation evaluate an immutable snapshot and perform no I/O; motion policy
  §11.4 now states it, but nothing enforces it. The obvious enforcement is a
  `execMotion`/`execVrm` link check for socket, clock, and threading symbols,
  in the way each bundle already proves what it links.
Two of this plan's contract asks have landed and are stated in the contracts
rather than here: the `motionCore` aggregates carry **two** comparisons — the
exact `operator==` that `ExecTypeRegistry::RegisterType` requires and the
tolerant `NearlyEqual` that P0-6 parity needs
([MOTION_CONTRACT.md](../design/MOTION_CONTRACT.md#comparison-semantics-v060)) —
and the OpenUSD version contract is one supported version with two enforcing
mechanisms
([SUPPORTED_CONFIGURATIONS.md](../reference/SUPPORTED_CONFIGURATIONS.md)).
