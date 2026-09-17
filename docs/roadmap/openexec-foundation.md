# The OpenExec foundation

> **After v0.9.0 (decided 2026-09-17).** The foundation finishes here. Then
> `execMotion` moves to `usd-motion-plugins` as its optional OpenExec bundle,
> while `execVrm` and the `ExecIr` track (§7) stay
> ([WORKSPACE.md §9](../architecture/WORKSPACE.md#9-destinations-under-the-motion-architecture),
> [the migration](motion-foundation-split.md) MIG-2). The findings the nodes
> produced are fixed in the destination libraries on arrival.

> **Target: v0.9.0** — the number it took when v0.8.0 was cut on 2026-09-01.
> The one place a track carries a version is the
> [roadmap status table](README.md#status-at-a-glance); this block mirrors it and
> nothing else in this document states a release number for its own work. The
> `ExecIr` invertible rig is on its own track after this one.
>
> **Re-ordered 2026-09-06, to the front.** The fourth move, and the first
> forward one. The 2026-08-29 order below is reversed for its own pair only —
> packaging hardening and the tracker path shipped in v0.8.0, and what changes is
> that the NPZ/AMASS reader and the producer contract now come *after* this plan
> rather than before it. The argument is the packaging track's, one layer out:
> every node here is a **thin wrapper** over a `motionRuntime` or `vrmRetarget`
> call (§3), which makes this foundation the first consumer of those libraries
> that is not the tool that grew up beside them, and a boundary nothing outside
> has consumed is a boundary nobody has measured. A node that cannot be written
> as a wrapper is a finding about the library API — produced by an implementation
> rather than predicted by a review — and
> [boundary consolidation](boundary-consolidation.md) is scheduled immediately
> after this plan to act on those findings. **What it costs is stated rather
> than hedged**: no new source shape informs this plan's node set, and the
> producer contract is written after `execVrm` exists, so a fifth crossing the
> exec layer needs arrives as rework. Nothing here is withdrawn by that move
> either.
>
> **Re-ordered 2026-08-29**, and that was the third time this plan's position had
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

**`build_usd.py` has no OpenExec toggle.** It ships the exec libraries — `vdf`,
`ef`, `esf`, `esfUsd`, `exec`, `execGeom`, `execIr`, `execUsd` — and both
imaging-side components unconditionally, so every runtime we publish carries
them; the CMake build *does* have one (`PXR_BUILD_EXEC`, default `ON`). So the
gate "the build fails on an OpenExec-less runtime" stays a *detection*
requirement. `usdExecImaging` is not evidence of OpenExec, because it is built
either way; `usdIrImaging` is, because its CMakeLists returns early when the
toggle is off. That is the pair the P0-1 probe swapped.

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
   — the probe's component list needed amending, and was: nine components,
   `ef`/`esf`/`esfUsd` added and `usdIrImaging` in `usdExecImaging`'s place
   (P0-1).
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

### P0-1 — OpenUSD 26.08 exact pin ✅

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
exec component with no headers, a `PXR_BUILD_EXEC=OFF` install, and one missing
a component the contract deliberately does *not* require) and asserts both that
it refuses and why.

**The capability probe carries what the audit found** *(2026-09-06,
[report §9.1](../reports/openusd/26.08-openexec-migration.md#9-what-this-changes-in-the-plan))*.
It probes nine components rather than six. `ef`, `esf` and `esfUsd` were
unprobed but are transitively required by the public exec headers — a runtime
missing them fails at *compile* time inside a bundle, which is the failure the
probe exists to move earlier — and `usdExecImaging` is gone from the required
set, since it is built whether or not `PXR_BUILD_EXEC` is on. The imaging side
is `usdIrImaging` now, which OpenUSD does gate on that toggle, so the component
that refuses a `core` runtime leaf
([SUPPORTED_CONFIGURATIONS.md](../reference/SUPPORTED_CONFIGURATIONS.md)) says
something about OpenExec as well as about imaging. Which runtimes are refused
did not change: a `core` leaf lacks both.

`workspace_openusd_contract` went from five fixture cases to eleven — one per
added component, alternating the probe's two halves so both stay exercised; a
`PXR_BUILD_EXEC=OFF` install as it actually looks, with every exec library and
`usdIrImaging` absent while `usdExecImaging` still ships all of its headers,
which is the shape where the old list's imaging component was the one that would
have *passed*; and an install with no `usdExecImaging` at all, which is now
**accepted**, because a demotion nothing asserts is only a reordering. The two
accepting cases check the reported component list exactly, which is what catches
a component dropped from the module and the fixture in one edit — the one
mistake neither file can catch on its own.

### P0-2 — motion layer CI ✅

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
and the OpenExec/offline parity case, which needs P0-4 and P0-5 first. *The
parity case exists since 2026-09-13*: five `workspace_exec_parity_*` names in
the root suite, labelled `motion.openexec`, and the three over the recorded
export `motion.real-corpus` too, the first use of that label.

**All seven labels are assigned, and a check reads them back** *(2026-09-15)*.
What each one names:

| Label | Tests | Where from |
|---|---|---|
| `motion.core` | 3 | every test of `libs/motionCore` |
| `motion.runtime` | 5 | every test of `libs/motionRuntime` |
| `motion.retarget` | 2 | every test of `libs/vrmRetarget` |
| `motion.cli` | 7 | every test of the three product tools, `motion_retarget`, `motion_capture` and `motion_bvh` |
| `motion.integration` | 8 | the compositions only the root build guarantees: `workspace_bvh_end_to_end`, `workspace_real_avatar_bake`, `workspace_unicode_paths` and the five parity cases |
| `motion.openexec` | 25 | every test of both exec bundles, the driver contract and the parity cases |
| `motion.real-corpus` | 11 | every test that reads the recorded mocopi export, in `motionBvh`, `motion_bvh_convert`, the root suite and the parity cases |

The adapters' recorder tools are CLIs and are not `motion.cli`: they are
adapter artifacts rather than product members, and the label is the product's
tools. No adapter test is `motion.real-corpus`, because every recorded device
and sender session is a manifest with no bytes.

`workspace_ctest_labels` lists the root build's registrations through CTest and
fails on four things: a member's `tests/CMakeLists.txt` that registers no test,
a label a directory in the table should contribute and does not, a test missing
a label its whole directory carries, and a `motion.*` label not in the table.
The first is the local half of the per-member attribution asked of `ost`
([current.md](current.md), "`ost` cannot tell us a workspace member ran no
tests"): it covers the 24 member suites of the root build. On the tree before
the labels it reported 28 findings, none of them about members, and a copy of
the tree with `usdVrmaFileFormat`'s registrations emptied reported exactly that
member. CTest's listing mode rewrites `Testing/Temporary/LastTest.log`, which a
CTest run of the same tree has open, so the check lists a copy of the tree's
`CTestTestfile.cmake` files. `workspace_ctest_labels_selftest` holds each rule
to a case.

**The required coverage, against the suite** *(2026-09-15)*. Every item has a
CTest name but one, which is covered outside CTest:

| Required coverage | Where |
|---|---|
| quaternion interpolation, pose filtering, resampling, missing sample hold | `motionRuntime_unit` |
| root-motion policy | `motionRuntime_liveCapture` (intake), `vrmRetarget_unit` (modes) |
| non-identity rest-pose correction | `vrmRetarget_unit`, and `workspace_bvh_end_to_end`, whose fixture rig a broken correction cannot pass |
| partial humanoid mapping | `vrmRetarget_unit` (unmapped joints stay at rest and are reported) |
| invalid mapping | `vrmRetarget_unit` (gaps and collisions), `motion_retarget_design_triplet` (an absent or unreadable `--humanoid-map`) |
| output/input path collision | `motion_retarget_design_triplet` |
| resolved `UsdSkel` transforms | `motion_retarget_design_triplet`, `workspace_real_avatar_bake` |
| packaged CLI execution | not CTest: `release.yml`'s artifact-only BVH and exec smokes, from the installed product |
| OpenExec/offline parity | `workspace_exec_parity_*` |
| Windows Unicode paths | `workspace_unicode_paths` |

**The last row found three defects when it was written** *(2026-09-15)*. It
runs every executable the workspace ships — the four product tools and the
three adapter recorders — and both importers, against a directory named
`ユニコード-é`, which no single ANSI code page can spell, and holds each leg to
the same run from an ASCII directory. On Windows, before the fix:
`motion_retarget` could not find the avatar, `motion_bvh_convert` read `é` as
`e`, and `usdVrmFileFormat` could not open the `.vrm` from any host, Python
included. A Windows `main` receives its arguments in the process's ANSI code
page and a narrow `std::ifstream` opens a string the same way, while OpenUSD
reads every path as UTF-8. So each executable now embeds a manifest that makes
its code page UTF-8 (`cmake/UsdVrmUtf8CodePage.cmake`), and both file formats
read through Ar as OpenUSD's own formats do, since a plugin runs in a host whose
code page is not ours. The two plugin legs run in the test's own process for
that reason. Each fix was taken out once and the test failed on the leg it
names. The first attempt passed with the `CanRead` fix reverted, because opening
a layer never calls `CanRead`, so the test now calls it directly. This row was
also Product P3's Unicode item ([current.md](current.md)).

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

*Part of that smoke runs since 2026-09-13*, inside P0-4's packaged run: the
product's `motion_retarget` bakes `Seed-san.vrm` and a `.vrma` walk from the
installed prefix, and `exec_parity` evaluates the same inputs against the bake
bit for bit — plugin discovery, humanoid map loading, retarget execution and
evaluated joint transforms, from the artifact. What that run does not check is
embedded texture resolution, and the build-tree scan covers the harness's
process, not the tool's.

### P0-4 — minimal `execMotion` bundle ✅

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
`extractRootMotion` → `interpolatePose` → `blendPoses`. All five are done; what
is left of this task is not a node (see "Still open here" below).

**Step 1 landed on 2026-09-06** — `plugins/execMotion`, and the seven items above
are done as one: `motion::HumanoidPose` registers as an execution value type, a
`motion.identityPose` computation on `UsdSkelAnimation` returns the identity pose
over the bones a clip's `joints` name, and `execMotion_mechanism` drives all of
request-compile, compute, an unchanged recompute, an authored-value
invalidation, a time change and an explicit invalidation against the built
bundle. The pose carries no timestamp, which is a measurement rather than a
shortcut: a computation is handed a frame, `HumanoidPose::timestamp` is seconds,
and the rate between them is stage metadata exec does not deliver to a callback
— so **P0-4's remaining nodes need the rate as an explicit input**, decided
before `motion.sampleAnimation` rather than after
([the mechanism report](../reports/openusd/26.08-openexec-mechanism.md) §5). Discovery goes through `plugInfo.json` and `PXR_PLUGINPATH_NAME` and the
test does not link the plugin, so moving that file aside turns the test red with
`Failed to find computation` — the audit's §2.1 prediction, confirmed as a
behaviour rather than restated as a risk. The identity computation is the whole
of the behaviour on purpose: with no algorithm in the bundle, a wrong answer can
only be a wrong mechanism.

Four measurements came out of it and they are in
[the mechanism report](../reports/openusd/26.08-openexec-mechanism.md); three
change tasks below. The one that changes this task is **`execMotion` now owns
the `UsdSkelAnimation` schema**, because 26.08 allows exactly one plugin to
declare a schema and drops the loser's computations silently.

**`motion.sampleAnimation` landed the same day**, and with it the rate question
step 1 raised. The node reads `joints`, `rotations`, `translations` and
`motion:timeCodesPerSecond` off the clip plus the builtin `computeTime`, and
returns the pose the clip states at the frame the system is evaluating, stamped
in seconds. `execMotion_sample` drives one request at four times — the default
time code and frames 0, 100 and 50 — over three fixtures differing by one thing
each: the keyed clip, the same clip holding still, and one that states no rate.

**The rate enters the graph, from an authored attribute**, and the reason is the
*next* node rather than this one. The alternative the mechanism report named —
stamp the pose on its way out of exec, where the caller holds the stage — reaches
`motion.filterPose` as time zero, and `motion::PoseFilter`'s whole property is
that its cutoff is frame-rate independent because it derives each step's weight
from the elapsed time between poses. So `motion:timeCodesPerSecond` is a
`.Required()` input on the clip, **a clip that states none is refused rather than
stamped** — an error and no value at all, because `timestamp` has no absent state
and a guessed second is indistinguishable downstream from a measured one — and
the attribute is a shim for an upstream gap that goes away if exec ever delivers
stage metadata to a callback. Nothing in this repository authors it yet; §9 has
the producer half.

**Four measurements, in [the sampling report](../reports/openusd/26.08-openexec-sampling.md),
and two of them change tasks below.** `.Required()` **does not refuse a missing
attribute** — the request compiles, `IsValid()` is true, and the callback runs
with an input that has no value, which is the mechanism report's metadata finding
on a second kind of input and makes it general: *every* node in P0-4 and P0-5
owes its own refusal for anything it cannot compute without. And **between two
keys the answer is USD's** — an exec input arrives already resolved at the
evaluated frame, and 26.08 slerps a `quatf[]` — so an exec sampler is not
`motion::SampleAnimation`, and P0-6 has two samplers to compare rather than one
implementation to check, at the clip's own key times or not at all. Also
measured: **a keyed attribute and the builtin `computeTime` are each enough alone
to make a value key time-dependent**, so a node that declares `computeTime` is
recomputed on every frame change even when nothing it reads has moved — which
makes it something the later nodes declare only if they use it — while
`motion.identityPose`, whose one input is `uniform`, goes on being reported to
nothing; and the default time code resolves
a clip that authors only time samples to **nothing**, so the first compute after
a `BuildRequest` — or after an `InvalidateAll()` — is an empty pose rather than
the first frame.

**This node is the plan's first "not a wrapper" finding, and it is the one this
re-order was scheduled to produce.** Reading a `UsdSkelAnimation` into a
canonical pose does exist in this repository — in `tools/motionRetarget`'s
`StageIo.cpp`, in a *tool*, where a bundle cannot call it — so `execMotion` keeps
its own seam over plain values and the duplication is recorded rather than
hidden. It is [boundary consolidation](boundary-consolidation.md)'s to act on.

**`motion.filterPose` landed the same day, and with it the first node that is a
recurrence.** `motion::PoseFilter` derives each step's weight from the seconds
since the pose before it, and a computation is handed one frame with no way to
reach another — so the state it normally keeps cannot live in the callback,
which the purity rule forbids, nor in the scene, which would author a derived
value. It is passed in: `motion.priorPose` is a computation whose ordinary value
is the clip's own pose at the evaluated frame, and whose purpose is to be
replaced through `ExecUsdSystem::ComputeWithOverrides`. **Exec does the step and
the driver owns the sequence** — which is where the state already is for a live
source, in `motionRuntime`'s pose buffer. Un-overridden the node is
`motion.sampleAnimation`, because a filter with zero elapsed time reseeds and
passes its pose through; nothing in the bundle special-cases that. The clip may
state `motion:filter:cutoffHz`, `motion:filter:rootPosition` and
`motion:filter:rootOrientation`, and an absent one is left at
`PoseFilter::Options`' own default rather than at one this bundle picked — a
different judgement from the rate, and the difference is what an absent value
costs: a missing rate produces a second no consumer can tell from a measured
one, a missing cutoff selects the library's documented behaviour.

**Four measurements, in [the filtering report](../reports/openusd/26.08-openexec-filtering.md),
and three change tasks below.** A computation **reads another computation** on
the same prim and the registered aggregate crosses that link unchanged, so the
chain in §5 is links rather than one node — and `Computation<T>()` is not a
connection, so §5.1's one-connection rule stays `blendPoses`'s problem. **Time
dependence propagates across the link**: `motion.filterPose` declares neither
`computeTime` nor a keyed attribute and is still reported when the frame moves,
which makes the sampling report's "declare `computeTime` only if you use it" free
to follow rather than something every downstream node has to undo. **A request
is armed by its first `Compute`** — a `ChangeTime` before any compute reaches no
callback at all, which cost a red run and is the shape a naive event-driven
driver would take. And **an override reaches every dependent of the key it names,
is visible as that key's own value, leaks into no sibling, and does not survive
the call.**

**The boundary finding this node produced** is smaller than the sampler's and
the same kind: `motion::PoseFilter` has no stateless one-step entry point, so
`execMotion` composes one from two `Apply` calls — a seed and a step. The
algorithm stays in the library, so the node is a wrapper; the idiom is a
workaround for a missing signature, and a `Step(prior, pose, options)` free
function is the ask for
[boundary consolidation](boundary-consolidation.md) §1.

**And it has a measured cost, which P0-6 inherits.** A pose is not the whole of a
filter's state: `PoseFilter` keeps a dropped bone's history in its state and out
of its result, and only a result can travel back in as the next prior pose. So a
bone returning after a missing frame is passed through here — **45.0° against
23.8°** streamed — which is nothing for a clip, whose `joints` are `uniform` so
no bone drops out, and real for a live source. Reproducing the carry-forward rule
in the bundle would be the second algorithm the wrapper rule forbids, so the
difference is asserted in both directions in `execMotion_pose` and the ask above
is sharpened by it: the one-step entry point has to return the *state* as well.
**Parity has a third known divergence to compare, beside the two samplers.**

**`motion.extractRootMotion` landed the same day, and it is the first node that
answers in something other than a pose.** It returns a `motion::RootMotion` —
where the body is at the evaluated frame, under the intake policy the clip
states in `motion:root:intake`: `motion::RootMotionIntake`'s own three, reached
as a token. `passthrough` is the root the pose carries, `ignore` **clears**
rather than zeroes it — the distinction the presence flags exist for, since a
cleared root leaves a rig its own placement and a zeroed position puts the body
at the origin — and `deriveVelocity`, which is the library's default and so what
an absent attribute selects, fills in a linear velocity from the prior pose. It
reads `motion.sampleAnimation` rather than `motion.filterPose` because that is
the *library's* ordering: `LiveCaptureSource` conditions the root of the frame
as it arrived and smooths afterwards, so a node differentiating a filtered
position would answer a question P0-6 then has to explain rather than measure.

**Four measurements, in [the root-motion report](../reports/openusd/26.08-openexec-root-motion.md),
and three change tasks below.** A bundle **registers more than one value type**,
and a computation may answer in a type other than the one it reads — one request
returns a pose and a root motion side by side — so nothing forces the remaining
nodes through `HumanoidPose`. **Time dependence follows the link and not the
type**: this node declares no `computeTime`, inherits its input's, and the change
of result type costs nothing. **One override drives every node that depends on
the key it names**: a single `motion.priorPose` substitution steps the filter and
derives the velocity in the same `ComputeWithOverrides`, which *shortens* the
driver contract below — a driver holds one previous answer per prim, not one per
node. And **an input the callback reads but `.Inputs()` does not declare is
silent**: the bundle compiles, the request is valid, the callback runs, and the
pointer is null, so the node takes its absent-value path with nothing anywhere
reporting the missing declaration. That is the third shape of 26.08's one
property — a callback cannot tell *absent* from *not asked for* — and it makes
the node's own refusal the only refusal there is.

**A refusal sets no value at all, and that is now the bundle's shape rather
than this node's.** Review of the change found the node's refusal returning a
default-constructed `motion::RootMotion` — which is `motion:root:intake =
"ignore"`'s own answer, bit for bit, so a misspelled `passthrough` got the
behaviour of a deliberate `ignore` for anyone not reading `TfError`s. 26.08 has
a channel for it: `.Callback<T>` accepts a void-returning callback that sets its
result through `VdfContext::SetOutput` or **`SetEmptyOutput`**, and an empty
value is what `ExecUsdCacheView::Get` then hands back. So a refusal sets none,
in every node here — an empty value is the one shape no computation ever
produces as an *answer*, which is the only thing that keeps a refusal
distinguishable — and **a refusal propagates**, since a dependent handed no
value refuses in turn rather than filtering a pose nobody sampled. It corrects
one sentence of [the sampling report](../reports/openusd/26.08-openexec-sampling.md),
in [the root-motion report](../reports/openusd/26.08-openexec-root-motion.md) §6
rather than in the audit, because an audit is history.

**The absent-input rule is now general, because this attribute answers it both
ways.** The rate is refused when absent, the filter's cutoff is defaulted when
absent, and `motion:root:intake` is **defaulted when absent and refused when
stated and unrecognized**. One rule underneath: default where an absent value
selects the library's documented behaviour, refuse where it would produce a
number no consumer can tell from a measured one — and never default a value the
clip *stated* that this layer cannot honour, or a misspelled `ignore` gets the
root motion it asked not to have.

**The boundary finding is the third, and the first where the wrapper idiom was
tried and ruled out rather than skipped.** The rule this node applies is
`motionRuntime`'s, and it lives in `LiveCaptureSource::_Condition` — a private
method of a capture *session* that owns a buffer, a filter, held-bone state and
statistics. Composing it the way `motion.filterPose` composes `PoseFilter`
(construct, push the prior, push the pose, read the head) gives the **wrong
answer in the ordinary case**: two poses at the same instant are a reseed for
`PoseFilter` and a *refusal* for `Push`, so the composed answer is the previous
frame's root, and an un-overridden node evaluates exactly that case. So the
derivation is three lines in the seam, asserted against its definition rather
than against the library, and the ask for
[boundary consolidation](boundary-consolidation.md) §1 is a stateless
`ConditionRootMotion(prior, pose, intake)` beside the session class.

**`motion.interpolatePose` landed on 2026-09-12, and it is the first node whose
input a driver hands in rather than feeds back.** It answers `IMotionSource`'s
one question — what is the pose at this evaluation time? — of a **snapshot**: a
timestamped history, the immutable snapshot §5 puts between a live source's
buffer and every computation, entering as an override on a new key,
`motion.poseHistory`, whose ordinary value is the clip's own pose as a history of
one. So a driver now fills two keys and they are different kinds of thing:
`motion.priorPose` is the graph's previous *answer*, fed back; `motion.poseHistory`
is the source's *input*, handed in. The node is `motion::ClipSource::Sample`
over that history at `motion.sampleAnimation`'s timestamp — one library call and
nothing else, the first in the bundle — and it answers the library's
`motion::PoseSampleResult` **whole**, because `ClipSource` stamps a hold at the
requested instant exactly as it stamps a sample, and the status is the only field
that tells a stopped source from a live one. Registering that type needed an
exact `operator==` on it, which `motionRuntime` now carries: the v0.6.0 ask,
answered for the first time outside `motionCore`.

**Four measurements, in [the interpolation report](../reports/openusd/26.08-openexec-interpolation.md),
and two of them change what P0-6's harness has to do.** An override of a key whose type is a **whole
history** (`motion::HumanoidAnimation`) reaches its dependent like a pose-typed
one, closing the root-motion report's open question for a second registered
type. **Two overrides of two keys in one call** each reach only their own
dependents, so a driver holds one previous answer *and one snapshot* per prim.
**A wrongly typed override is dropped, not refused** — 26.08 posts a coding error
naming the key and computes the key's *ordinary* value, so every dependent
answers plausibly, and an empty `VtValue` takes the same path: a driver cannot
push an absence into a key, and has to treat a coding error around
`ComputeWithOverrides` as a failed frame. And **the first result type with an
absent state of its own** answers where every earlier one had to refuse: an empty
history is the library's `Unavailable`, a value, and the node refuses only a
history whose timestamps are not finite or decrease — which the library's binary
search would answer with a bracket nobody measured — and the **default time
code**, where there is no instant: the sampler stamps 0.0 there harmlessly, but a
history sampled at a guessed 0.0 would answer a believable `Held`, and every
request is armed at that time code.

**The fourth boundary finding is the first where the wrapper works and the finding
is its cost.** The status-carrying answer exists only as a method on a *source
object*, and `ClipSource` owns the animation it serves, so every evaluation copies
the history into one; the free function beneath it, `motion::SampleAnimation`,
takes the history by reference and returns the pose without the status. The ask
for [boundary consolidation](boundary-consolidation.md) §1 is a free
`SampleClip(animation, t) -> PoseSampleResult` that `ClipSource::Sample` calls,
with the time-order precondition — which nothing states — written on it. And the
node gives **P0-6 an instrument**: a driver supplying a clip's own key poses as
the history evaluates `motion::SampleAnimation`'s rule beside USD's between the
same keys, in one request, where the sampling report left the second sampler
unreachable from exec.

**`motion.blendPoses` landed on 2026-09-13, and it is the first node that reads
poses from several prims.** A blend is a `UsdSkelAnimation` stating
`motion:blend:sources`, a relationship targeting the clips, and
`motion:blend:weights`, one weight per target in target order. The node reads
each target's `motion.sampleAnimation` and hands the poses to the N-way
`motion::BlendPoses`, each with the weight at its position. Like
`motion.interpolatePose` it is one library call, and unlike that one it costs no
copy.

**Four measurements, in [the blending report](../reports/openusd/26.08-openexec-blending.md),
and one corrects this plan.** A **relationship fan-in arrives in authored target
order**. That holds at first compile, after an edit that reorders the targets
while both source nodes are already compiled, and in a fresh system. So the
sentence this plan carried, "fan-in is reachable only through relationships with
no deterministic ordering", confused two accessors. The audit's "no
deterministic ordering" belongs to `IncomingConnections`, which nothing here
uses. **Two kinds of source vanish from a fan-in without a word.** A target that
does not provide the computation is skipped while the network compiles, and a
source that **refused** is skipped by the read iterator. With the node's check
disabled, a blend whose second clip refused answered the first clip exactly. So
the node reads the relationship a second time, for the builtin `computePath`,
and refuses when the counts disagree. **Invalidation crosses the relationship**:
time, an authored weight, and an edit of the relationship's targets each reach
the blend, the last with no request rebuilt. And **an override on one prim
reaches a dependent on another**, which is how a pose a driver holds enters a
blend. That adds a line to the driver contract: such a pose must be stamped at
the instant the other sources were sampled at.

**The sources must share one instant.** Each converts the evaluated frame at its
own `motion:timeCodesPerSecond`, so two clips at two rates are at two seconds on
the same frame. The library would interpolate between them: 0.625 s between
1.0 s and 0.5 s, measured with the check disabled. The node refuses instead,
exactly and for every source. It also refuses absent weights rather than
blending evenly, because a callback cannot tell an absent array from an empty
one.

**The fifth boundary finding is about the library's answer, not the call's
cost.** Over nothing weighted, `motion::BlendPoses` answers a default pose
stamped 0.0. It carries a NaN weight into NaN rotations. It interpolates its
sources' timestamps as though they were samples in time. And its fold depends
on order: three sources reversed land 4.247° apart, which its header does not
say. The node refuses the first three cases. The ask for
[boundary consolidation](boundary-consolidation.md) §1 is a blend that can say
*nothing to blend* and states its preconditions and its order dependence.

**One thing the chain cannot do, observed rather than measured.** Every
downstream node reads `motion.sampleAnimation` on its own prim by name, so a
blend's answer cannot be filtered, have its root extracted, or be sampled in the
graph. The relationship this node uses is the mechanism that would let a node
take its input from a sampler, a blend or an override without knowing which.
That is a design question for P0-5's retarget node, which has to choose a pose
to retarget.

Still open here, until 2026-09-17: a producer that authors the rate, the
filter policy and the intake policy (§9), and a decision on what a one-joint
clip's fallback-filled root means (P0-5's humanoid report). *Both were decided
for v0.9.0 on 2026-09-17.* **The producer half leaves this plan**: the
2026-09-17 motion decision gives the canonical producer contract to
`usd-motion-plugins` as MIG-0's evidence (BND-0), and no item of the foundation
release gate needs a producer. The parity harness states the four conventions
on the stage it compares, and that is how v0.9.0 ships them. **A one-joint
clip's fallback-filled root is kept and pinned**, not refused: the callback
cannot tell it from an authored origin, so `execMotion_sample` asserts it and
the driver contract states it
([MOTION_CONTRACT.md](../design/MOTION_CONTRACT.md#openexec-driver-contract-after-v080)).

**The driver contract is written, and it is code** *(2026-09-14)*. The rules
this section collected are ten lines in
[MOTION_CONTRACT.md, "OpenExec driver contract"](../design/MOTION_CONTRACT.md#openexec-driver-contract-after-v080).
None of them can be found from the computations themselves. They are: arm a
request with one compute and keep what it posted; name the instant before
computing; hold one previous answer and one snapshot per prim and hand both in
one call; check an override's type before exec sees it; request every key an
override names; treat a coding error as a failed frame; stamp a pose for a
blend at the other sources' instant; and rebuild a request exec has stopped
answering. `tests/parity/ExecDriver` follows them, and P0-6's harness, the
first client, now drives exec through it. Two rules came out of writing it
rather than out of an earlier report. **An override of a key nothing compiled
is skipped without a word, mistyped or not.** **A request whose every key
expired reports itself valid**, as an `InvalidateAll` leaves one
([the driver report](../reports/openusd/26.08-openexec-driver.md) §3, §4).

**The packaged-plugin half of step 7 ran on 2026-09-13**, and it is P0-6's
harness rather than a new suite: `scripts/artifact_only_exec_smoke.py` installs
the aggregate product outside the repository and runs the five parity cases
with the product's own tools and bundles, in an environment holding only the
runtime and the product's activation. `exec_parity` reports every plugin the
registry loaded and every module the process mapped, so "discovered from the
package" is read back rather than inferred from a variable: ExecMotion and
ExecVrm load from the prefix, no library of this workspace loads from anywhere
else, and with every copy of execMotion's `plugInfo.json` moved aside the case
refuses with ExecMotion loaded from nowhere. All five hold bit for bit from the
product, on Windows, macOS and Linux in `release.yml`'s dry run
([run 34758168103](https://github.com/animu-sphere/usd-vrm-plugins/actions/runs/34758168103)).
It runs in `release.yml`, not on a pull request.

**The first attempt could not open a `.vrm`**, and the cause was the release
lane rather than the bundles: the product carried no `vrmContainer` binary,
because packaging stages every bundle's library runtime out of one prefix that
the last `ost plugin build` refilled with its own closure — `execVrm`'s, which
is static
([ost report 40](../reports/ost/40-2026-09-13-v0.22.10-one-workspace-prefix-for-every-bundle.md)).
The loop order is the workaround and `scripts/check_product_libraries.py` the
check; the run above is what found it, which is the reason an artifact-only run
was on this list at all.

**`blendPoses` was last on purpose, and the reason held.** It is the one
computation that wants several inputs, and 26.08's builtin `computeValue`
forwards across exactly one connection and silently falls back when there are
two (§5, connection dataflow). So it reads through a relationship instead, and
that is where the fan-in surprises showed up: not in ordering, as this plan had
guessed, but in two kinds of source that vanish without a word.

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

### P0-5 — minimal `execVrm` bundle ✅

Computations: `vrm.computeHumanoidMap`, `vrm.computeTargetSkeleton`,
`vrm.computeRestPoseCorrection`, `vrm.humanoidRetarget`,
`vrm.computeJointLocalTransforms`.

**The bundle and its first two nodes landed on 2026-09-13**: `plugins/execVrm`,
with `vrm.computeTargetSkeleton` on `UsdSkelSkeleton` — the rig as `vrmRetarget`
reads it, each rest transform decomposed with scale and shear dropped — and
`vrm.computeHumanoidMap` on the applied `VrmHumanoidAPI`, which is
`HumanoidMap::SetJointToken` for each `vrm:humanBones:*` binding against the one
skeleton `vrm:skeleton` reaches. `vrmRetarget::TargetSkeleton` and
`HumanoidMap` register as execution value types; the exact `operator==` the
registry requires was added to `vrmRetarget` for this, the ask motionCore's
aggregates answered in v0.6.0. The bundle links neither `vrmSchema` nor its
generated class, and requires it as a bundle.

**Six measurements, in [the humanoid report](../reports/openusd/26.08-openexec-humanoid.md),
and the first reaches back into P0-4.** **An attribute a schema defines and the
stage gives no value reaches a callback as one element of the type's fallback**
— an empty token, an identity matrix, a zero vector — beside an executor
`TF_WARN`, not as nothing. The sampling report's "a missing input has no value"
is true of an attribute the prim does not have at all, and false of one its
schema defines. So a skeleton that authors no `joints` arrives as one joint
named `""` and is refused for it, an unbound bone and an authored empty token
are one value, a one-joint skeleton with no rest pose is answered as identity
because from inside the callback it *is* one — and, one bundle over, a one-joint
*clip* that keys nothing comes out of `motion.sampleAnimation` with a root at
the origin nobody stated (a probe; recorded in `ExecMotionPose.h`, not fixable
in the callback). **A computation on this workspace's own applied schema
resolves on a prim another plugin types** — the importer's humanoid is a
`UsdGeomScope`, `execGeom`'s — and **not** on a prim carrying every attribute
without the schema, which the offline tool would read. **The schema bundle is a
runtime edge**: exec resolves `UsdVrmHumanoidAPI` by type name when it reads the
Exec block, and without `vrmSchema` in the session the skeleton still computes
and the map is not found. **Fifty-five inputs declared in a loop work**, and
motionCore's vocabulary and the schema's properties are the same 55 names. And
**invalidation follows the dependency, not the value**: a rest-transform edit
reports the map, whose indices do not move, and a retargeted `vrm:skeleton`
reaches it with no request rebuilt.

**The refusals are this bundle's first P0-6 table.** Five statements a stage can
make that `motion_retarget` tolerates and `execVrm` refuses or cannot see — rest
transforms that do not pair, a binding to a joint the skeleton lacks, two bones
on one joint, no `vrm:skeleton`, the attributes without the schema — each of the
missing-field kind, none reachable from what the importer authors
([the report](../reports/openusd/26.08-openexec-humanoid.md) §7). As
`motion.blendPoses` does, the map reads its relationship twice, and here the
count decides the answer rather than the message: with it disabled, a humanoid
naming a skeleton and a `Scope` was answered against the skeleton.

**The sixth boundary finding is the sampler's again, one library over.**
Building a `TargetSkeleton` from rest transforms exists only in
`tools/motionRetarget`'s `StageIo.cpp`, so the decomposition is in the seam,
line for line; the ask is a library constructor from tokens and rest matrices
([boundary consolidation](boundary-consolidation.md) §1).

**`vrm.computeRestPoseCorrection` landed the same day, and it is the first node
that reads two rigs.** It is `vrmRetarget::ComputeRestPoseCorrection` over the
humanoid's own map, the target rig across `vrm:skeleton`, and the rig a clip was
authored against, across a second relationship, `vrm:retarget:sourceSkeleton`.
Both rigs are read through `vrm.computeTargetSkeleton`, so one implementation
decomposes them. The relationship names a *skeleton*, not an animation. An
animation states no rest, and the skeleton is the one prim from which both
halves of a clip can be reached. It is a convention of this bundle and nothing
authors it yet (§9). `vrmRetarget::RestPoseCorrection` gained the exact
`operator==` the registry requires.

**Five measurements, in [the correction report](../reports/openusd/26.08-openexec-rest-correction.md).**
**The node is one library call**: it equals the library's answer over the rigs
computed beside it, bit for bit. **One computation serves both rigs**, through
two relationships, one of them defined by no schema. **A path to nothing
arrives exactly as no relationship does**, so an absent source is refused rather
than given the library's default identity rest, which would hide a misspelled
path. That is the absent-input rule's first stated exception: a default is safe
only where *absent* and *dangling* can be told apart. **Invalidation follows the
dependency, not the value**, on a third node: a clip's rest translation reports a
correction that contains rotations only and does not change. And **the source
is read by name and the target never is**. A semantic clip's joint leaves are
the bone vocabulary by contract. Naming the avatar's own skeleton as the source
is refused, and so is a source naming one bone twice, where the offline tool
silently keeps the later rest. As on `vrm:skeleton`, the source relationship is
read twice. With the count taken from what came back, a source naming a skeleton
and a SkelRoot was answered.

**The seventh boundary finding**, and a question the next node inherits.
Assigning a clip skeleton's joints to bones exists only in
`tools/motionRetarget`'s `ReadClip`, and the ask is a `SourceRestPose` from a
semantic skeleton, beside the struct
([boundary consolidation](boundary-consolidation.md) §1). And `PoseRetargeter`
computes its own correction in its constructor and accepts none. A retarget node
that wraps it would redo, on every evaluation, what this node caches per rig
edit. That is visible in the API; `vrm.humanoidRetarget` has to measure it.
Four more divergences go to P0-6's table, two about missing fields and two about
stage shape: the tool reads a clip as its own stage, and exec reads one stage
that has to say where the clip's skeleton is.

**`vrm.humanoidRetarget` landed the same day, and it is the first computation
in either bundle that reads a value the other bundle computes.** It is
`vrmRetarget::PoseRetargeter` over the rig, the map, the clip's rest and the
root-motion options, asked for one pose: the clip's own sample, which is
`motion_retarget`'s call per sample with the same four arguments. The pose comes
from `execMotion`'s `motion.sampleAnimation` on the animation the clip's
skeleton binds in `skel:animationSource`. That is a second relationship hop, and
an exec input makes one, so a fifth computation the plan did not list,
`vrm.computeBoundPose` on `UsdSkelSkeleton`, forwards it. One relationship on
the humanoid then names the clip, and the rest and the motion cannot come from
two clips. The skeleton's binding is UsdSkel's, inheritance included: a sixth,
`vrm.computeBindingPose` on the applied `UsdSkelBindingAPI`, answers what an
ancestor binds, reached through `NamespaceAncestor`, so a clip bound on its
SkelRoot is retargeted. An explicit unbinding, which exec cannot tell from no
binding, is pinned. Where the root lands is four `vrm:retarget:*` attributes on the
humanoid, `motion_retarget`'s four flags word for word. `vrmRetarget::RetargetedPose`
gained the exact `operator==` the registry requires, and `execVrm` registers
`motion::HumanoidPose` as well as `execMotion` does.

**Six measurements, in [the retarget report](../reports/openusd/26.08-openexec-retarget.md).**
**The node is one library call**, bit for bit, and applies exactly the
correction `vrm.computeRestPoseCorrection` caches. **A value crosses bundles
unchanged, and exec says nothing when the other bundle is missing**: without
`execMotion` the animation drops out of the fan-in silently, and only the bound
pose's count notices. So `requires.bundles` names `execMotion`, a runtime edge
and not a link one. **A bundle that reads a value type registers it too**:
without that, every session that did not load `execMotion` first lost every
`execVrm` computation to a fatal error, while the suite that loads both stayed
green. **The fallback follows an attribute's existence, not its schema**: a
declared, valueless `vrm:retarget:translationScale` is a scale of 0, and is
pinned. **The default time code is refused**, because the sampler's empty pose
there becomes the rig's whole rest when it is retargeted. And **invalidation
reaches the retarget** from the frame, a key, the binding, the source and a
statement, with a driver's pose entering through either bundle's key.

**The eighth boundary finding answers the question the correction node left.**
`PoseRetargeter` computes its correction in its constructor and accepts none, so
the node recomputes, on every frame, the value `vrm.computeRestPoseCorrection`
caches per rig edit. On a full 55-bone humanoid that is 17.7 µs of a 21.2 µs
evaluation, against 1.9 µs for the retarget alone. The ask is a retargeter that
takes the correction ([boundary consolidation](boundary-consolidation.md) §1).
More rows go to P0-6's table: a binding on the SkelRoot (the tool reaches it
only through its "exactly one animation on the stage" fallback), a skeleton
bound nowhere, an explicit unbinding, a NaN scale (the tool parses it), a
valueless scale, and the default time code.

**Which pose, the question `motion.blendPoses` left, is answered for parity and
left open for the graph.** The retarget retargets the clip's own sample. A
filtered or blended pose reaches it as a driver's override. Choosing one *in the
graph* needs a node that takes its pose without knowing which computation is
behind it, and 26.08 names the computation at registration. That is the `ExecIr`
track's switch controller (§7, P0-4) rather than this node.

**`vrm.computeJointLocalTransforms` landed the same day, and P0-5 is complete
with it.** It is the humanoid's own retarget in the shape a `UsdSkelAnimation`
states at one time code: the rig's `joints`, read off `vrm.computeTargetSkeleton`
because a retargeted pose does not carry them, the pose's translations and
rotations and timestamp bit for bit, and one `(1, 1, 1)` scale per joint.
`vrmRetarget` gained the type, `JointLocalTransforms`, with the exact
`operator==` the registry requires. It answers components rather than matrices,
because the components are what `motion_retarget` authors and what P0-6
compares, and the matrices are UsdSkel's own composition of them.

**Five measurements, in [the joint-transforms report](../reports/openusd/26.08-openexec-joint-transforms.md).**
**Authored the way the tool authors it, the value is what UsdSkel resolves**:
`UsdSkelMakeTransforms` over it, exactly. Leave `scales` out, or make the arrays
one joint short, and UsdSkel resolves the rig's **rest**, silently. So the scales
are part of the value, and a driver's override that does not pair with the rig
is refused rather than answered, since answered it would bake to a rig standing
still. **The identity scale overwrites a scaled rest pose**: the fixture's arm
rests at scale 2 and bakes at 1, in both implementations. The importer can
produce it, on `Seed-san.vrm`'s hair and bag joints, at most 0.14% off unit.
That is P1-2's question and not a P0-6 row (P1-2 below). *Answered by P1-2 on
2026-09-17: a bake now states the rest scale, in both implementations.* **An unregistered
result type is fatal to every computation in the bundle**, as an unregistered
input type is. Invalidation reaches the sample from the frame, a statement, a
key and a rest edit.

**And one measurement about the tool, found by asking where a sample goes in
time.** `motion_retarget` places each sample at `timestamp × rate`, having read
`timestamp` as `timeCode / rate`. At 30, 60 and 120 fps that round trip misses
3 273 of the first 100 000 frames (and 9 175 at 25 and 50). A real bake of a
30 fps clip keyed at 62 writes `62.00000000000001`, and reading it back at 62
gives the hips `(1, 0, 1.74e-16, 0)`. P0-6's harness therefore compares a bake
at the bake's own time samples. The **ninth boundary finding** is that the
bake's shape (tokens beside the arrays, identity scales) is stated offline only
in two lines of the tool
([boundary consolidation](boundary-consolidation.md) §1).

Inputs: `vrm:humanBones:*`, the typed `Vrm*API` schemas, `UsdSkelSkeleton`,
`UsdSkelAnimation`, and explicit policies/relationships.

**Forbidden:** importer private models, reparsing the source `.vrm` / `.vrma`
bytes, joint-name heuristics, and duplicating an algorithm that already exists in
`vrmRetarget`.

**One obligation the audit added:** `execVrm`'s own `plugInfo.json` must carry an
`Info.Exec.Schemas` block naming every schema it registers on, because the block
lives with the *registering* library, not the schema owner, and nothing else
declares them. A missing block fails as "computation not found", not as a load
error
([report §2.1](../reports/openusd/26.08-openexec-migration.md#21-the-pluginfo-half)).

**And one correction to it, measured on 2026-09-06.** That block may name the
`Vrm*API` schemas and `UsdSkelSkeleton`; it may **not** name `UsdSkelAnimation`.
A schema has exactly one declarer per session, `execMotion` declares that one,
and a second declarer loses every computation it registered there — with a
coding error at metadata read and a "computation not found" much later, which is
the pair of symptoms least likely to be connected by whoever hits them. `execVrm`
reaches an animation through an **input accessor** instead. Where it needs to
compute on a prim whose typed schema belongs elsewhere — a `UsdGeomXformable`,
which `execGeom` owns — it registers on an applied API schema that prim carries;
that route is measured
([the mechanism report](../reports/openusd/26.08-openexec-mechanism.md) §2, §3)
and the rule is [WORKSPACE.md §2](../architecture/WORKSPACE.md).

### P0-6 — OpenExec / offline parity ✅

Compare `motion_retarget`'s offline result against the `execMotion` + `execVrm`
computed result on the same input: joint order, translations, rotations, identity
scales, root motion, rest-pose correction, unbound-bone behavior, time sampling,
and diagnostics. The numerical tolerance is written into the contract.

**The harness landed on 2026-09-13, and the values agree bit for bit.**
`tests/parity/exec_parity` is handed the tool's own arguments and the bake the
tool wrote from them. It builds one stage that sublayers the avatar's layer and
the clip's, so every path the tool saw is the path exec sees, and evaluates
`vrm.computeJointLocalTransforms` at each of the clip's keys. Five
`workspace_exec_parity_*` cases run it: the recorded mocopi export on the
fixture rig, on `Seed-san.vrm`, and under all four root-motion statements, the
design triplet's walk read as a `.vrma`, and a 30 fps clip keyed where the tool
misplaces a sample. **All 414 598 compared values are `==`**, which is stronger
than the gate's `NearlyEqual`. No sign flip and no rounding. Joint order, identity
scales and Seed-san's scaled rests all agree. The negative pairs are committed:
exec under each root-motion statement, held against the default bake, diverges
in translation only.

**Six measurements**
([the parity report](../reports/openusd/26.08-openexec-parity.md)). **The only
value that is not exact is placement**: frames 31 and 62 of an integer-keyed
30 fps clip bake 2.37e-16 s late. The recorded input never shows it, because
its keys round-trip. Compared at the bake's own samples, as the joint-transforms
report instructed, it costs nothing; read at the clip's frames, two rotations
differ in their bits and measure 0 rad apart. **The harness sees a difference**:
a map one bone short diverges on exactly that joint, and a gaze left in the
tool's bake diverges on exactly the two eye joints, which exec has no node for.
**Parity is conditional on five statements the harness makes on the stage**:
the rate, the source skeleton, the map, the rig, and the root-motion policy
(§9). **Two of the tool's own fixtures state human bones without
`VrmHumanoidAPI`**, which exec cannot see and the harness refuses. And
**diagnostics do not agree**. The tool names the clip bone a rig drops and the
required bones it lacks. Exec says neither, because `vrm.humanoidRetarget` hands
`PoseRetargeter` no `RetargetDiagnostics` and a computation has no channel for
one. *(Closed the same day, below.)*

**The tenth boundary finding**: which prim is the humanoid, the rig and the clip
is decided only in the tool, and the harness restates those rules to compare
the same input
([boundary consolidation](boundary-consolidation.md) §1).

The report collects the rows four earlier reports left for this task into one
table of 24. Every row the importer, the VRMA reader or the BVH converter can
produce is exercised or measured, and all agree except a gaze, which exec does
not compute until the `ExecIr` track's P1-2 (bake with `--no-look-at` to
compare the rest), and diagnostics. *The Linux and macOS lanes ran the five
cases for the first time on PR #185's own CI, and all five passed on both*
(`workspace-pr-linux`, `workspace-pr-macos-arm64`), beside Windows.

**The diagnostics agree too, line for line** *(2026-09-13)*. P1-1 made the
codes values, and `execVrm` now answers them in two computations:
`vrm.computeRigDiagnostics`, which is `DiagnoseRig` and reads no clip, and
`vrm.computeRetargetDiagnostics`, which is that list and then what retargeting
this sample reported. The harness is handed the tool's log beside its bake. It
merges exec's answers over the keys, which rebuilds the clip overload's order,
and compares whole formatted lines with the tool's library-raised ones. They
agree on all five cases, the parity report's `upperChest` on Seed-san included.
A committed negative pair drops one line from the tool's log and requires the
run to fail on exactly that line with no value moving
([the diagnostics report](../reports/openusd/26.08-openexec-diagnostics.md)).
Two findings came with it: a node nothing invalidates posts its refusal only at
the compute that arms a request, which is a driver-contract line; and the node
repeats the retarget, because the library reports a pose's diagnostics only
while retargeting it (the eleventh boundary finding, 23 µs a frame on the
fixture rig and 50–60 µs on Seed-san).

**Decided 2026-09-17: the rows no producer reaches are not run on both sides
for v0.9.0.** Each is asserted on the exec side and read on the tool's, and the
gate's row is agreement *on the same recorded input*, which all five cases
hold, values and diagnostics. Running a row no importer, VRMA reader or BVH
converter can produce would build a stage only a test authors, and compare two
answers to a question no user can ask. The rows stay listed in the parity
report's §7 table, so the day a producer can reach one, it becomes a parity
case rather than an assertion. The partial skeleton policy states the two of
them where the implementations part on purpose (rows 2 and 3,
[MOTION_CONTRACT.md](../design/MOTION_CONTRACT.md#partial-skeleton-policy-v090)).

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

*What exists as bytes, 2026-09-13*: one recorded input, the mocopi BVH export
`libs/motionBvh` may redistribute. It covers the arm raise and root motion, and
the harness runs it. The device sessions and the VRChat OSC session are
manifests with no bytes, as v0.7.0 and v0.8.0 recorded, so the right-hand
column is otherwise still a list of captures to make.

Two comparisons, not one, and they are not interchangeable
([MOTION_CONTRACT.md](../design/MOTION_CONTRACT.md#comparison-semantics-v060)):

- serialization and registered-value identity → `operator==`
- offline vs OpenExec motion equivalence → `NearlyEqual`

*How the second is applied to a bake* is now stated in
[MOTION_CONTRACT.md](../design/MOTION_CONTRACT.md#comparison-semantics-v060):
joint by joint over `JointLocalTransforms`, at the bake's own samples, under
`MotionTolerance`'s angle, distance and time. The run did not need the
tolerance, since every value was `==`.

**Compare a bake at its own time samples, not at the clip's frames**
*(measured 2026-09-13)*. `motion_retarget` rebuilds each sample's time code as
`(timeCode / rate) × rate`, which at 30 fps puts frame 62 at
`62.00000000000001`; read at 62, the bake answers a rotation 1.7e-16 off its
key. `vrm.computeJointLocalTransforms` is the value to compare against it
([the joint-transforms report](../reports/openusd/26.08-openexec-joint-transforms.md)
§8, §9). *The harness does this, and the tool is unchanged*: compared at its
own samples the bake is exact, so the fix to author at the time code it read
stays the ninth boundary finding's ask rather than a parity requirement
([the parity report](../reports/openusd/26.08-openexec-parity.md) §3).

**A failing case is classified, never widened.** Reaching for a larger epsilon is
how a real divergence becomes a tolerance. The categories, in the order they are
cheap to rule out: quaternion sign only · floating-point rounding ·
provenance-only difference · ordering difference · missing-field semantics ·
root-motion policy difference · actual algorithm divergence. Only the last is a
defect in this plan's sense; the rest are contract questions that get an answer
in the contract.

### P0-7 — display smoke, re-scoped to `UsdGeomXformable` ✅

**Its route narrowed on 2026-09-06 and the task did not.** "Prove the mechanism
on `UsdGeomXformable`" cannot mean *our* computation registered for that schema:
`execGeom` declares it, and a second declarer is refused. What is left is either
an applied API schema of ours on the Xformable prim — measured to work
([the mechanism report](../reports/openusd/26.08-openexec-mechanism.md) §3) — or
`execGeom`'s own computations. The upstream ask (option (c) below) is unaffected
and now has a second thing to ask for.

**It took `execGeom`'s own computation, and registered nothing for the schema**
*(2026-09-13)*. `execMotion` registers an **attribute expression** for
`motion:root:transform` on a clip: what `computeValue` answers for that
attribute is `motion.extractRootMotion` as a matrix. An Xformable that connects
its `xformOp:transform` to it is placed by `execGeom`'s
`computeLocalToWorldTransform`, because `computeValue` follows exactly one
connection to an attribute of the same type. No new schema and no applied API
was needed. `usdExecImaging`'s Xformable adapter hands that matrix to Hydra. In
Storm, the same stage draws the marker at its authored place with the exec scene
index off, and on the clip's hips path with it on
([the display report](../reports/openusd/26.08-openexec-display.md) §4).

`execMotion_display` asserts four of the five "done when" rows on every lane,
through the stage scene index with an observer where Hydra would be, no GL
needed. A frame change dirties exactly the prims the clip reaches. A clip edit
dirties them at `ApplyPendingUpdates`. A material edit dirties nothing and
invalidates nothing. The stage is checked for `xformOp:transform` only first.
**"From packaged plugins" is the fifth row, and P0-4's packaged run closes it**
*(2026-09-13)*: `scripts/artifact_only_exec_smoke.py` runs
`execMotion_display` against the installed product in the environment its
parity cases prove holds nothing of this repository's (P0-4 above). All five
rows are met, on all three OS in `release.yml`'s dry run
([run 34758168103](https://github.com/animu-sphere/usd-vrm-plugins/actions/runs/34758168103)).
What stays open is re-filed below rather than part of this task: the upstream
ask and skinned display.

Four findings came with it. **A refusal draws where `ignore` does**: `execGeom`
reads an absent local transform as the identity, so a misspelled intake, a
deliberate `ignore`, the default time code and a session without the bundle all
put the prop at its parent, and only the refusal posts an error. **A broken
route draws the authored value without a word**: two connections, an undeclared
target, or a target of another type. **The precondition is two-sided**:
`execGeom` also reads a `xformOp:transform` that `xformOpOrder` does not list,
which UsdGeom ignores. And **the first frame drawn from a scene camera is empty**,
with or without this bundle, an upstream behaviour seen only through an engine
([the display report](../reports/openusd/26.08-openexec-display.md) §3, §6, §7).

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
  advances it, so it is tracked whether or not it is answered. A second ask
  travels with it since 2026-09-13: an attribute input that computes no value
  should reach a callback as no value, or exec should offer a builtin saying
  whether an attribute has one — 26.08 fills it with the type's fallback and a
  warning the callback never sees
  ([the humanoid report](../reports/openusd/26.08-openexec-humanoid.md) §8).
  Three more since the display slice ran: an absent computed transform draws
  as the identity, so a refusal cannot reach a picture; the first frame an
  engine draws through the exec scene index from a scene camera is empty; and
  `execGeom` reads a `xformOp:transform` its prim's `xformOpOrder` does not
  list ([the display report](../reports/openusd/26.08-openexec-display.md) §3,
  §6, §7).
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

### P1-1 — retarget diagnostics ✅

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

**The retarget half is frozen, as values** *(2026-09-13)*.
`vrmRetarget/Diagnostics.h` holds the `VRM_RETARGET_*` set in one table, and a
retarget reports `RetargetDiagnostic` values — code, subject, detail — into a
list that keeps each code and subject once and compares exactly. That is P0-6's
ask from the parity report (§6): codes a node can answer beside the pose rather
than log. The contract is
[MOTION_CONTRACT.md, "Retarget diagnostics"](../design/MOTION_CONTRACT.md).

**Freezing after the raisers rather than before changed the list by one.** The
VMC, mocopi and BVH sets were frozen before their decoders; this library had
reported two bone lists and four prose warnings since v0.4.0, so the freeze
classified those six. The two lists and two of the warnings are drafted codes
(missing required, unbound driven, duplicate target, hierarchy). A root joint
the rig does not have matched none, and `VRM_RETARGET_INVALID_ROOT_JOINT` is
the one code added to the draft. A missing `hips` joint under root-motion mode
`hips`, which had its own sentence, is `MISSING_REQUIRED_BONE` with the dropped
root in its detail, since the cause is the bone.

**The set splits at the layer boundary, and three of the drafted codes cannot
be raised by the library.** `NON_UNIT_SCALE`, `TIME_RANGE_DERIVED` and
`OUTPUT_COLLIDES_WITH_INPUT` say what a stage or a file system added, and
`vrmRetarget` never sees either — it does not even receive a scale, since
`TargetSkeleton` drops one when a rest is decomposed. `motion_retarget` raises
the last two; `NON_UNIT_SCALE` waits for P1-2 to decide what raises it;
`vrmRetarget_boundaries` fails if the library names any of the three.

**One defect found on the way.** The clip overload asked only its first sample
which bones it drives, so a bone that first appears later was never reported.
Measured by a unit test that fails with the old rule restored. `motion_retarget`
could not reach it — a `UsdSkelAnimation`'s `joints` are uniform — but a
library caller with a live source's animation could.

**The exit codes are frozen, one class per input at fault** *(2026-09-14)*.
`motion_retarget` had exited 2 for a usage error and 1 for everything else.
Each refusal is now classified where it is raised, into the table above
([MOTION_CONTRACT.md, "`motion_retarget` exit codes"](../design/MOTION_CONTRACT.md)).
Only the raiser knows whether it was given a path the user typed or found a
stage missing something. The contract adds three rules to the drafted table:

- **A usage error is a 1.** It was a 2, and the fix is the same as for any
  other bad argument.
- **An `--output` naming an input is a 1, not a 5.** One file was named twice
  and nothing was written. The line still carries its retarget code.
- **Code 6 is reserved.** The tool evaluates nothing through OpenExec, so it
  never returns 6.

A missing file (1) and a file OpenUSD will not open (3) are told apart only
after `UsdStage::Open` fails, so the check cannot refuse a URI the resolver
would have opened. `motion_retarget_design_triplet` runs each class of refusal
and holds it to its code. The build before this change fails that test on 14
of its 22 runs. The collision case passes on both, because it was already a 1.
Review found one path the first draft sent to the wrong class: a directory
exists and is still not a layer, so reading "exists" as "there" made it a 3
with a line naming a file format for `'.'` files. "There" is a regular file,
and a directory is a 1 under its own line.

Exec answering the retarget codes was P0-6's row, and it closed the same day:
two `execVrm` computations, compared with the tool line for line (P0-6 above).

**The `VRM_OPENEXEC_*` codes are frozen and raised** *(2026-09-14)*. Their
table is in the driver contract
([MOTION_CONTRACT.md](../design/MOTION_CONTRACT.md#vrm_openexec_-codes)). It
waited for a driver, and P0-4's driver contract produced one.
`COMPUTATION_UNAVAILABLE` is a key the session cannot compute: no prim at its
path that exec computes on, or no computation of its name for that prim.
`TYPE_MISMATCH` is an override or an answer of a type other than its key's
declaration. `INVALIDATED` is a request exec stopped answering while every
provider is still there. It is a recoverable warning, since the driver rebuilds
the request and names the instant again. The draft's migration-report premise
held. 26.08 classifies none of these failures, so each code comes from the
driver's own checks: a runtime error or a coding error, a path lookup, a
declared type, and a one-key probe run only after a coding error. None comes
from exec's text, which travels in the detail. `exec_driver_contract` raises
each one by the failure it names and holds it to its key. With `execVrm` out of
the session, the parity harness now names both of its keys as unavailable where
it used to show empty values
([the driver report](../reports/openusd/26.08-openexec-driver.md)).
`NON_UNIT_SCALE` stays frozen and unraised until P1-2 decides what raises it.

### P1-2 — scale policy ✅

**Decided and landed 2026-09-17**, and stated in
[MOTION_CONTRACT.md, "Scale policy"](../design/MOTION_CONTRACT.md#scale-policy-v090):
a bake states each joint's **rest** scale, constant over the clip; scale is not
retargeted; a clip that animates scale raises `VRM_RETARGET_NON_UNIT_SCALE`
once and is never applied; a rig whose rest is scaled is not refused. OpenExec
and offline behave identically, because both call one decomposition,
`vrmRetarget::DecomposeRestTransform`, and carry `TargetJoint::restScale` —
the library entry point boundary consolidation §1 asked for, which the tool and
`execVrm` each duplicated until then.

What it measured: the fixture's arm, rested at 2, now bakes at 2 in
`execVrm_joint_transforms` and in `motion_retarget_design_triplet`; with the
tool mutated back to identity scales, the design triplet failed on the authored
scales, the resolved row lengths and the missing code, and
`workspace_exec_parity_recorded_real_avatar` failed on Seed-san's seven joints.
Three adapter end-to-end tests had compared quaternions extracted from scaled
matrices, which are not unit length, and read Seed-san's still joints as moving;
they now take the rotation with the scale removed.

The original statement, kept for what it was written against: *always author
identity scale; animated joint scale is unsupported; a non-unit animated scale
input is a structured warning; scale animation is never silently applied;
OpenExec and offline behave identically.* It formalized the fix that shipped
with the v0.4.0 tag —
[UsdSkel resolves a scale-less animation to the rest pose](../releases/v0.4.0.md#the-defect-that-made-the-whole-thing-visible)
— and did not name a rig whose rest is scaled.

**One case the rule does not yet name, measured 2026-09-13**
([the joint-transforms report](../reports/openusd/26.08-openexec-joint-transforms.md)
§4): a rig whose **rest** is scaled. A bake states every joint, UsdSkel takes an
animated joint's transform from the animation whole, and the rule states 1, so
the rest scale is replaced — the fixture's arm, rested at 2, bakes at 1, in
`motion_retarget` and `vrm.computeJointLocalTransforms` alike. `Seed-san.vrm`
has seven such joints, none of them humanoid bones, at most 0.14% off unit. The
decision this item owes: carry the rest scale in `scales` (which needs a scale
on `TargetJoint`, since `vrm.computeTargetSkeleton` drops it), refuse a rig
whose rest is scaled, or keep identity and state the cost.

### P1-3 — partial skeleton policy ✅

Make a contract of: a bone in the clip but not the target; a bone in the target
the clip does not drive; a missing required humanoid bone; missing optional
finger/eye/jaw bones; duplicate mappings; hierarchy mismatch; a non-identity
parent rest transform.

**Done 2026-09-17**, in
[MOTION_CONTRACT.md, "Partial skeleton policy"](../design/MOTION_CONTRACT.md#partial-skeleton-policy-v090):
seven rows, each with what the retarget does, what it reports, and the test that
holds it. Nothing about the behaviour changed. Three rows had no test, and have
one now: a rig with every required bone and no optional one reports nothing;
across a chain the two sides disagree about, each bone moves relative to its own
parent and an unbound intermediate bone's motion is dropped, not folded into its
child; and a duplicate mapping keeps the later bone a sample drives. The one
place the implementations part, a map `execVrm` refuses and the tool uses with a
warning, is stated there with the parity table rows that already record it.

### Foundation release gate

The boundary in one list: `execMotion` and `execVrm` both exist · the `motionCore`
aggregates are registered OpenExec value types · a computation evaluates an
immutable snapshot · no socket, device, or wall-clock I/O inside a computation ·
offline and OpenExec agree under `NearlyEqual` on the **same recorded input** ·
computation discovery succeeds from a packaged plugin rather than a build tree ·
the dependency boundary is checked by the workspace gate.

*Since 2026-09-15 the snapshot item and the no-I/O item are CTest names on
every lane: `execMotion_boundaries` and `execVrm_boundaries` (§9).* The graph
gate checks the bundle edges, and these two check each bundle's links and
schema declarations. *The Unicode paths row is `workspace_unicode_paths`, from
the same day (P0-2).*

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

*It starts from the foundation's driver* *(2026-09-14)*. `tests/parity/ExecDriver`
already holds one system per stage and its requests, follows the driver
contract, and raises the `VRM_OPENEXEC_*` codes. It is test support, because
the parity harness is its one client. This task is where it becomes a
workspace identity
([MOTION_CONTRACT.md](../design/MOTION_CONTRACT.md#openexec-driver-contract-after-v080)).

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
  somewhere binding rather than only here. *Since 2026-09-13 the path is
  measured rather than proposed*: a clip declares `motion:root:transform`, a
  prop connects its `xformOp:transform` to it, and `execGeom` and
  `usdExecImaging` do the rest
  ([the display report](../reports/openusd/26.08-openexec-display.md) §1). Two
  statements now need a home: that attribute and that connection are authored
  by the fixture and by nothing else, which is the rate's situation again and
  goes to BND-0; and a stage shown this way states `xformOp:transform` and no
  other op on every Xformable, in both directions (§6 there).
- ⬜ **Motion Phase E's scope grew.** Motion policy §16 describes Phase E as
  `execMotion` / `execVrm` nodes; this plan adds the display slice (P0-7) and the
  whole `ExecIr` rig track (§7). Either Phase E widens or the ladder
  gains a phase.
- ⬜ **A clip has to state the rate its frames are counted at, and nothing
  authors it.** `motion.sampleAnimation` reads `motion:timeCodesPerSecond` off
  the `UsdSkelAnimation` prim because a computation cannot reach the stage
  metadatum that means the same thing
  ([the sampling report](../reports/openusd/26.08-openexec-sampling.md) §2, and
  [the mechanism report](../reports/openusd/26.08-openexec-mechanism.md) §5).
  Today only the bundle's own fixtures author it, so the attribute is a test
  convention rather than a contract — and **P0-6 parity needs a real clip that
  carries it**, which means either `motion_retarget`'s bake and the `.vrma`
  reader author it, or the parity harness authors it onto the stage it compares.
  Whichever it is belongs in a contract before P0-6 leans on it, together with
  the statement that the attribute is a shim: it duplicates
  `timeCodesPerSecond`, it can disagree with it, and it is meant to be removed
  if exec ever delivers stage metadata to a callback. *Answered for P0-6 on
  2026-09-13, and only for P0-6*: the parity harness authors it onto the stage
  it compares, equal to the clip stage's rate, and leaves a rate the clip
  already states alone
  ([the parity report](../reports/openusd/26.08-openexec-parity.md) §5). No
  producer authors it, so the contract half is still BND-0's.
- ⬜ **A humanoid has to say which skeleton a clip was authored against, and
  nothing authors that either** *(2026-09-13)*. `vrm.computeRestPoseCorrection`
  reads the clip's rest across `vrm:retarget:sourceSkeleton`, a relationship on
  the humanoid that no schema defines
  ([the correction report](../reports/openusd/26.08-openexec-rest-correction.md)
  §1). The offline tool never needs it, because it opens the clip as a separate
  stage. Exec evaluates one stage, and that stage has to name the clip's
  skeleton. It is the rate's situation for a second input: P0-6's harness authors
  it onto the stage it compares, or a producer does, and whichever it is belongs
  in a contract, and in BND-0's producer contract if a producer does it. Unlike the rate it
  is not a shim for a gap upstream; it is the scene stating which clip drives
  which avatar, which nothing on a stage says today. *Since 2026-09-13 it also
  carries the motion*: `vrm.humanoidRetarget` reads the clip's pose through the
  same skeleton's `skel:animationSource`, which `usdVrmaFileFormat` already
  authors, and four `vrm:retarget:*` attributes on the humanoid state where the
  root lands. Those four are `motion_retarget`'s flags, and nothing authors them
  either
  ([the retarget report](../reports/openusd/26.08-openexec-retarget.md) §7).
  *Answered for P0-6 as the rate is*: the parity harness states the
  relationship, the four statements for the flags it is given, and, for an
  avatar with no humanoid, `--humanoid-map` as `vrm:humanBones:*` on a prim it
  defines. With those five statements the two implementations agree bit for
  bit ([the parity report](../reports/openusd/26.08-openexec-parity.md) §5).
- ⬜ **Every node that reads an attribute owes a fallback decision, and one
  schema may want to state it.** 26.08 hands an unauthored schema attribute
  to a callback as one element of Sdf's default for the type
  ([the humanoid report](../reports/openusd/26.08-openexec-humanoid.md) §4), so
  for each such input the question is what that one element looks like against
  what it pairs with. *Widened on 2026-09-13*: an attribute no schema defines
  that the prim declares with no value, or blocks, arrives the same way, so
  `vrm.humanoidRetarget`'s valueless `translationScale` is a scale of 0
  ([the retarget report](../reports/openusd/26.08-openexec-retarget.md) §4). `motion.sampleAnimation` has a shape where it pairs (a
  one-joint clip) and cannot tell; `vrm.computeHumanoidMap` reads it as unbound,
  and a `""` fallback on `VrmHumanoidAPI`'s bone attributes would make that the
  schema's statement and silence one executor warning per unbound bone. That is
  a schema-contract change, and it is filed with BND-0 in
  [boundary consolidation](boundary-consolidation.md) §1 rather than made by a
  node. The upstream half — an input with no value reaching a callback as no
  value, or a builtin that says whether an attribute has one — is filed with
  P0-7's ask.
- ✅ **The snapshot-input rule is checked, not only stated** *(2026-09-15)*. §5
  requires that a computation evaluate an immutable snapshot and perform no
  I/O. Motion policy §11.4 and WORKSPACE.md §2 stated it, and until now no
  test read it. `execMotion_boundaries` and `execVrm_boundaries` read it in
  four places. **The source** is scanned by header and by name for sockets,
  file I/O and watching, a wall clock, a private thread pool, mutable global
  state, and stage access. **The built library's imports** catch the same
  capabilities arriving through a header or a macro. **The target's link
  libraries** are held to an allow-list. **The `plugInfo.json` schema
  declarations** are held to the registrations and to the partition. The
  rule's tables are written once, in `execMotion`'s check, and `execVrm`'s
  imports them.

  **Two of the four halves exist because of a measurement.** The link half
  exists because a binary cannot show what it would need to show. With
  `liveTransport` linked into `execMotion`, the built DLL imported neither it
  nor `ws2_32`: the library is static and nothing called it, so only CMake knew
  about the edge. The import half reads the C++ runtime's clock, not KERNEL32's.
  MSVC's CRT stub imports `QueryPerformanceCounter` and
  `GetSystemTimeAsFileTime` into every DLL, and it did so into both bundles
  with no clock in either source. `std::chrono`'s clocks reach
  `_Query_perf_counter` and `_Xtime_get_ticks` instead, which nothing else
  imports.

  Each half was run against a mutation and failed on the line it names. A
  mutable `static` and a `steady_clock::now()` in one callback, and a
  `std::thread` and a `getenv` in another, failed in both the source and the
  imports. With the source restored and the library left built, they failed in
  the imports alone. A forbidden link failed in the links alone. A schema
  declared by both bundles, one declared with nothing registered, and one
  registered and not declared each failed in the schema half.

  **Review then found two tables that could not fail, and a hand-run mutation
  had not tried either.** MSVCP140 exports `_Fiopen`, which every file stream
  reaches, only by its decorated name. The plain-name pattern never matched,
  and a DLL built with `std::ifstream` passed the first version. The `static`
  detector took `static const T* last` for a constant, although the pointer is
  reassigned freely. It took `static std::function<void()> f` for a function,
  and it reported `constexpr static int k` as state. All four are fixed.
  `execMotion_boundaries_selftest` now holds the tables to cases: source text,
  and symbol lists spelled as each platform's tool prints them, so the Linux
  and macOS tables are checked on every lane. Seven mutations of the fixed
  check each fail it on the case they break. The scan still misses three
  shapes, which the check states and the self-test pins as missed: a
  direct-initialised `static T t(1);`, which reads like a function; a
  namespace-scope variable with no `static`; and state behind a `mutable`
  member.
Landed on 2026-09-06, and stated in the contract rather than here: **an
OpenExec schema has exactly one declarer, so `execMotion` and `execVrm`
partition them** — `UsdSkelAnimation` to the first, the `Vrm*API` applied schemas
and `UsdSkelSkeleton` to the second, neither declaring the other's, and a bundle
reaching a prim it does not own the schema of through an input accessor or an
applied API schema ([WORKSPACE.md §2](../architecture/WORKSPACE.md)). This was
not a change anyone predicted: it came out of running the mechanism, and it
would have cost `execVrm` a silent loss of every computation it registered on an
animation.

Two of this plan's contract asks have landed and are stated in the contracts
rather than here: the `motionCore` aggregates carry **two** comparisons — the
exact `operator==` that `ExecTypeRegistry::RegisterType` requires and the
tolerant `NearlyEqual` that P0-6 parity needs
([MOTION_CONTRACT.md](../design/MOTION_CONTRACT.md#comparison-semantics-v060)) —
and the OpenUSD version contract is one supported version with two enforcing
mechanisms
([SUPPORTED_CONFIGURATIONS.md](../reference/SUPPORTED_CONFIGURATIONS.md)).
