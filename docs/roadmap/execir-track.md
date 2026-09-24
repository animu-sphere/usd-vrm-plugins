# The `ExecIr` track

**Status:** ⬜ not started · **Target:** unscheduled
([status table](README.md#status-at-a-glance)) ·
**Policy:** [VRM_MOTION_POLICY.md §8](../design/VRM_MOTION_POLICY.md#8-execir-is-optional-never-a-prerequisite)

The OpenExec work this repository still owns. The foundation it builds on —
`execVrm` over the consumed `execMotion`, equal to the offline bake bit for
bit — shipped in [v0.9.0](../releases/v0.9.0.md); its plan is
[archived](../archive/motion-split/openexec-foundation.md) and its
measurements are the [OpenUSD 26.08 reports](../reports/openusd/README.md).
What is left is VRM-specific: an invertible humanoid rig through `ExecIr`,
expression and look-at as computations, and the display and contract items the
foundation left open.

## 1. What `ExecIr` is, and is not

`ExecIr` is an **optional experimental adapter**, evaluated on its own track.
Nothing below it in the stack waits for it:

```text
required, and finished first:
canonical motion  ->  standard retarget pipeline  ->  execVrm

optional, connected afterwards:
UsdSkel / VRM semantics  ↕  ExecIr adapter  ↕  ExecIr representation
```

The prohibitions are the policy's (§8 there) and
[WORKSPACE.md §2](../architecture/WORKSPACE.md)'s. The one that would do real
damage is `ExecIr` as the canonical motion contract: it is per-prim scalar
avars in world space, the canonical contract quaternion arrays in joint-local
space ([migration report §7.2](../reports/openusd/26.08-openexec-migration.md#72-the-shape-mismatch-with-usdskel)).

## 2. Tasks

### P0-1 — `ExecIr` responsibility audit ⬜

Compare `execVrm` against `ExecIr` across joint representation, controller
representation, FK, controller switching, compensation, forward and inverse
evaluation, and transform publication. Anything general moves to `ExecIr`;
only VRM semantics stay in `execVrm`. Start from the shape table in the
migration report §7.2. The real question is what a `UsdSkel`↔`ExecIr`
conversion costs and where it lives. Every `ExecIr` schema's docstring says it
is "not yet ready for production use", and the switch controller is
hard-coded to two rigs named `rig1` and `rig2`.

### P0-2 — VRM humanoid → `ExecIr` adapter ⬜

`VrmHumanoidAPI` + `UsdSkelSkeleton` + `vrm:humanBones:*` → an `ExecIr` joint
scope and controller representation. **Done when:** a VRM humanoid builds an
`ExecIr`-compatible rig without importer private APIs, missing mappings are
diagnosable, and disabling the adapter leaves the offline and `execVrm`
pipelines working.

### P0-3 — FK controller ⬜

Hips, spine chain, neck and head, arms, legs, hands and feet; full fingers are
P1. **Done when:** controller values forward-compute a joint pose, the target
rest pose is preserved, non-driven bones hold rest, and cache and
invalidation are correct.

### P0-4 — switch controller ⬜

Explicit switching between motion sources (a VRMA clip, manual FK, a live
source, a blended pose). **Done when:** switching suppresses pose
discontinuity, compensation is selectable, the switch is authorable as stage
data, and no source-name heuristic exists.

### P0-5 — inverse evaluation ⬜

Recover controller state from a pose, first for the humanoid FK controller,
hips translation, head orientation and the major arm and leg joints. **Not in
scope:** full-body IK, foot locking, contact solving, arbitrary constraints.

### P0-6 — OpenExec evaluation client ⬜

One `UsdStage`, one long-lived `ExecUsdSystem`, reusable batch request sets —
never a per-frame system. Needs batch requests, invalidation callbacks,
time-range invalidation, result dumps, a performance trace, and
provider/computation diagnostics. It starts from `tests/parity/ExecDriver`,
which already follows the driver contract
([`usd-motion-plugins` EXEC_CONTRACT.md §3](https://github.com/animu-sphere/usd-motion-plugins/blob/main/docs/design/EXEC_CONTRACT.md#3-the-driver-contract));
this task is where a driver becomes a library, and where it lives is that
contract's EX-O1.

### P0-7 — invalidation tests ⬜

Only the necessary values are invalidated by each of: animation time,
humanoid mapping, skeleton rest pose, controller value, switch source,
root-motion policy, look-at target, expression weight, and an unrelated
material attribute.

### P1-1 — expression computations ⬜

`vrm.resolveExpressionWeights`, `vrm.computeMorphTargetWeights`,
`vrm.computeMaterialColorOverrides`, `vrm.computeTextureTransformOverrides`,
each a wrapper over `vrmRig`'s resolver
([VRM_MOTION_POLICY.md §5](../design/VRM_MOTION_POLICY.md#5-expressions-on-a-rig)).
Results are produced, not written back to the stage continuously.

### P1-2 — look-at computations ⬜

`vrm.computeLookAt` and `vrm.applyLookAtToPose`, over `vrmRig`'s evaluator:
head transform, eye origin, target position, VRM range maps and the
bone/expression mode in; yaw/pitch, eye rotations, expression weights and an
updated pose out.

### P1-3 — performance baseline ⬜

Cold graph build, first compute, warm compute, time-only, controller, mapping
and skeleton updates, at 1, 10 and 100 avatars: build time, compute time,
cache hit ratio, invalidated node count, peak memory, thread scaling.

### P1-4 — graph diagnostics ⬜

Registered computation names, provider resolution, dependency edges, result
types, batch request contents, invalidation cause, cache hit/miss and timings,
visible during development.

### Release gate

A VRM humanoid builds an `ExecIr`-compatible rig · FK forward evaluation
works · switching and compensation work · limited inverse evaluation works ·
no responsibility overlap between the offline tool, `execVrm` and `ExecIr` ·
expression or look-at works end to end · controller results are visible in
usdview · the OpenExec plugins work from packaged artifacts alone ·
invalidation and cache reuse are testable · every experimental `ExecIr`
dependency is inside the adapter.

## 3. Display, and the upstream asks

OpenUSD 26.08 resolves exec prim adapters from a hard-coded list, so a
`UsdSkel`-skinned avatar cannot be displayed through the exec scene index.
v0.9.0 proved the mechanism on an exec-computed `UsdGeomXformable` instead
([the display report](../reports/openusd/26.08-openexec-display.md)).

- ⬜ **File the upstream asks.** Plugin registration of exec imaging adapters
  (the only route to skinned display through exec); an attribute input with no
  value reaching a callback as no value, or a builtin saying whether it has
  one ([the humanoid report](../reports/openusd/26.08-openexec-humanoid.md) §8);
  an absent computed transform drawing as identity; the empty first frame from
  a scene camera; and `execGeom` reading a `xformOp:transform` its prim's
  `xformOpOrder` does not list (the display report §3, §6, §7).
- ⬜ **Real `UsdSkel` skinning display is its own milestone**, after this
  track, and a release condition for nothing. Routes, in order: the upstream
  ask; an adapter via `ExecIrXformable`, at the cost of the shape mismatch; a
  custom Hydra scene index; integration outside OpenExec. A custom scene index
  is not the first choice — it is a standing maintenance cost against an
  exactly pinned OpenUSD.

## 4. Contract changes this track requires

- ⬜ **The `ExecIr` adapter has no identity** in
  [WORKSPACE.md §1](../architecture/WORKSPACE.md); its edges are stated in §2
  and the identity row is not.
- ⬜ **`usdExecImaging` has no declared place.** It is an OpenUSD component,
  not a member, but the presentation path through it — a prop's
  `xformOp:transform` connected to an exec-computed transform, and no other
  op on any Xformable shown that way — should be stated somewhere binding.
- ⬜ **An unbound humanoid bone owes a schema statement.** 26.08 hands an
  unauthored `vrm:humanBones:*` to a callback as one element of the type's
  fallback; `vrm.computeHumanoidMap` reads it as unbound, one executor warning
  per bone. A `""` fallback on `VrmHumanoidAPI`'s bone attributes would make
  that the schema's statement — a schema contract change
  ([SCHEMA_CONTRACT.md](../../plugins/vrmSchema/docs/SCHEMA_CONTRACT.md)).
- ⬜ **Who authors what a stage states for a computation.** The rate
  (`motion:timeCodesPerSecond`), `vrm:retarget:sourceSkeleton` and the four
  `vrm:retarget:*` root statements are authored today by the parity harness
  alone. The generic attributes are `usd-motion-plugins`' EXEC_CONTRACT §5
  and wait on its `Bindings` prim (USD-O5); the `vrm:retarget:*` half is this
  repository's to place, with the binding layer
  ([backlog](backlog.md#vrm-motion-open-questions)).

## 5. Deferred past this track

Spring-bone simulation proper, full-body IK, foot locking, contact solving,
GPU computation, per-frame stage write-back, editor UI, Python computation
registration, production-grade arbitrary rig authoring, and realtime skinned
display (§3). Several are permanent non-goals
([backlog](backlog.md#non-goals)).
