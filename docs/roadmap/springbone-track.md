# The spring-bone track

**Status:** 🚧 Step 0 implemented locally; cross-platform export pins pending · **Target:** unscheduled
([status table](README.md#status-at-a-glance)) ·
**Policy:** [VRM motion policy](../design/VRM_MOTION_POLICY.md) §10

Spring bones evaluated on a VRM rig. The schema states what each spring
declares, and `vrmRig` solves it. Where the solver lives, what it may depend
on and what it is measured against are set by the policy. This document holds
what is left to build. The steps are this track's internal order, not
Product P0–P6.

## 1. Where it stands

- **Import is complete.** Springs, colliders and collider groups from both VRM
  versions are authored under `/Asset/rig/SecondaryMotion`. The `springbone`
  and `vrm0_frontbake` fixtures hold them, and the negative fixture
  `missing_spring_collider_group` holds the warning (`VRM190`) for a
  collider-group index that is out of range.
- **Nothing evaluates them.** No library, plugin or tool here moves a spring
  joint.
- **Schema contract v2 separates roots and chains** (policy §10.2).
  Step 0's implementation is present; its remaining acceptance is below.
- **Order adjusted 2026-10-27:** Step 0 was brought forward while the
  [export track](export-track.md)'s Step 2 remains open, to advance secondary
  motion. Its export reopening tests also run against the schema change.

## 2. Steps

### Step 0: VRM 0.x springs state their roots 🚧

- ✅ `vrm:rootJoints` on `VrmSpringBoneAPI`. The per-joint parameter arrays
  are parallel to whichever of `vrm:joints` or `vrm:rootJoints` is authored,
  and a spring authors exactly one of them. The schema is regenerated with
  `plugins/vrmSchema/tools/generate_schema.py`.
- ✅ [SCHEMA_CONTRACT.md](../../plugins/vrmSchema/docs/SCHEMA_CONTRACT.md)
  states the two shapes, and the schema contract version is bumped, because
  what a VRM 0.x stage authors changes meaning.
- ✅ The importer writes a VRM 0.x group's `bones` into `vrm:rootJoints`.
  VRM 1.0 spring data does not change; both versions carry contract v2.
- ✅ The validator reports a spring that authors both arrays or neither as
  `VRM256` (allocation approved 2026-10-28).
  `VRM253` checks the selected array, including empty declarations.
- ✅ Affected fixture goldens and corpus digests are regenerated, and the
  CHANGELOG states the stage change.
- ⬜ Remeasure the two `vrm_export_end_to_end` package SHA-256 pins on macOS
  arm64 before merge. The existing arm64 pins still describe contract v1;
  Windows x86-64 v2 pins are measured. Recheck Linux x86-64 against those pins.

**Done when** every VRM 0.x fixture and corpus avatar authors its roots in
`vrm:rootJoints` and no `vrm:joints`, every VRM 1.0 spring is unchanged, and a
reader can tell a chain from a set of roots without knowing the source
version.

### Step 1: the solver in `vrmRig` ⬜

- ⬜ A header beside `ExpressionResolver.h` and `LookAtEvaluator.h`. It holds
  the rig values (a joint tree with parent indices, rest transforms,
  per-joint parameters, colliders, collider groups and the center) and a
  stateful solver with a reset and a fixed step. The output convention is
  decided in that PR and stated in the
  [vrmRig README](../../libs/vrmRig/README.md). The candidate is joint-local
  rotations in the rig's joint order.
- ⬜ Root expansion: VRM 0.x roots into a joint tree, given the skeleton's
  topology. Each joint appears once (policy §10.2).
- ⬜ The end-of-chain rules for both versions, fixed against the references
  (policy §10.3).
- ⬜ A feature macro in the installed headers, as
  `VRMRIG_LOOKAT_DIRECTION_API` is for look-at directions, so that a consumer
  built against an older install fails with a clear message.
- ⬜ Unit tests on constructed rigs:
  - a chain settles under gravity, and stiffness returns it to rest;
  - a sphere collider and a capsule collider each push a joint out;
  - a VRM 0.x branching tree moves each joint exactly once;
  - a reset restores the same state, and a repeated run is identical.
- ⬜ A reference comparison: joint trajectories from three-vrm (both
  versions) and UniVRM (VRM 0.x) on generated inputs. They are committed as
  data with the script that regenerates them, and compared within a stated
  tolerance (policy §10.4).
- ⬜ `vrmRig`'s boundary check unchanged: the solver adds no dependency.

**Done when** the solver passes the unit tests and the reference comparison
on every CI lane, and an installed `vrmRig` package exposes it.

### Step 2: handed to the runtime ⬜

This repository's half of the integration. Reading the stage into `vrmRig`
values, the adapter, the host and the fixed-tick scheduling belong to
`usd-avatar-runtime` and are planned there (its
[physics orchestration milestones](https://github.com/animu-sphere/usd-avatar-runtime/blob/main/docs/roadmap/current.md#physics-orchestration-milestones),
Milestone E). They are not restated here.

- ⬜ Questions the runtime's integration raises about the solver's API are
  answered in `vrmRig`, never worked around in the adapter. An adapter that
  needs a second implementation is a finding about the library's API.
- ⬜ The runtime's evidence of a VRM 0.x and a VRM 1.0 avatar moving under a
  clip is cited from the [capability matrix](../reference/CAPABILITY_MATRIX.md)
  where it was measured, not restated as this repository's finding.

**Done when** that evidence was produced against an installed `vrmRig`, with
no checkout of this repository.

## 3. Open questions

- **SB-O1: extended colliders.** `VRMC_springBone_extended_collider`
  (inside spheres and capsules, planes) is not read by the importer. Adding
  it is an additive schema change, made when a corpus avatar uses it.
- **SB-O2: scale.** How a non-unit avatar placement, or a stage whose
  `metersPerUnit` is not 1, applies to hit radii, collider sizes and the
  VRM 0.x virtual tail.
- **SB-O3: colliders outside the avatar.** The environment and other avatars
  depend on `usd-physics-plugins`' shared-collider question (SM-O1) and on
  the runtime's world membership. Nothing is planned here until a consumer
  needs it.

## 4. Not this track

- A rigid-body world, or any physics backend (policy §10.1).
- An OpenExec computation for springs (policy §10.1).
- Baking springs into a clip. `motion_retarget` does not simulate, and an
  offline spring bake waits for an offline consumer to ask for one.
