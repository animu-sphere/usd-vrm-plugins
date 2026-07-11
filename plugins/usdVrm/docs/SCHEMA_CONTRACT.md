# usdVrm schema contract v1

This document freezes the public interpretation contract for the typed VRM
control schemas authored by the usdVrm importer. Downstream tools should read
the typed `Vrm*API` data first. Raw VRM JSON is preserved as a lossless fallback,
not as the primary runtime interface.

The schema source is `schema/schema.usda`; generated C++ and
`plugin/resources/usdVrm/generatedSchema.usda` are build artifacts derived from
that source.

## Contract version

Current contract version: `1`.

New importer output writes:

| Location | Value |
| --- | --- |
| `/Asset.customData.vrm:schemaContractVersion` | `1` |
| `vrm_report.py --json compatibility.schemaContractVersion` | `1` |

Versioning policy:

| Change type | Policy |
| --- | --- |
| Add an optional typed attribute or relationship | Allowed within v1 when old readers can ignore it. |
| Add a new optional API schema | Allowed within v1 when existing v1 prims keep their meaning. |
| Change a required path, attribute type, token meaning, relationship target meaning, or array ordering rule | Requires a new contract version. |
| Remove or repurpose an existing v1 property | Not allowed in v1. |
| Move a shipped control prim to a new canonical path | Requires a new contract version unless the old path remains authoritative. |

The validator treats a missing version as an old/exported-stage warning
(`VRM270`) and an unknown version as a contract error (`VRM271`).

## Stage layout decisions

The v1 contract keeps all VRM control semantics under `/Asset/rig`:

| Feature | Contract path |
| --- | --- |
| Humanoid | `/Asset/rig/Humanoid` |
| Expressions | `/Asset/rig/Expressions/<name>` |
| LookAt | `/Asset/rig/LookAt` |
| Spring bones | `/Asset/rig/SecondaryMotion/SpringBones/<name>` |
| Spring-bone colliders | `/Asset/rig/SecondaryMotion/Colliders/<group>/Collider_<n>` |
| Constraints | `/Asset/rig/Constraints/<name>` |

The v1 contract freezes the shipped `/Asset/rig/SecondaryMotion/*` layout; an
earlier design draft had sketched `/Asset/physics/*` instead. SpringBone data in
usdVrm is control metadata, not a physics simulation graph, and these paths are
already covered by fixtures, reports, and downstream-facing docs. A future
physics-oriented adapter may publish `/Asset/physics/*` as an additive view, but
v1 readers should treat `/Asset/rig/SecondaryMotion/*` as authoritative.

## Humanoid representation decision

The v1 contract uses one token attribute per human bone:

| Property | Meaning |
| --- | --- |
| `rel vrm:skeleton` | The `UsdSkelSkeleton` whose `joints` tokens are referenced. |
| `uniform token vrm:humanBones:<bone>` | A joint path token from `Skeleton.joints`. |

This freezes the shipped per-bone attributes; an earlier design draft had sketched
`token[] vrm:humanBoneNames` plus `rel vrm:humanBoneTargets` instead.
`UsdSkel` joints are tokens inside `Skeleton.joints`, not prims, so a USD
relationship cannot directly target an individual joint. Standard VRM bones are
schema builtins; non-standard or VRM-0.x-only names remain custom
`vrm:humanBones:<name>` attributes on the same prim so the mapping stays
lossless.

## Typed API contract

| API | Applied to | Required typed data | Raw fallback |
| --- | --- | --- | --- |
| `VrmHumanoidAPI` | `/Asset/rig/Humanoid` | `vrm:skeleton`, authored `vrm:humanBones:<bone>` tokens | `/Asset.customData.vrm:rawExtension` |
| `VrmExpressionAPI` | `/Asset/rig/Expressions/<name>` | `vrm:expressionType`, `vrm:isBinary`; optional `vrm:morphTargets` plus parallel `vrm:morphTargetWeights`; optional `vrm:materialColorTargets` plus parallel `vrm:materialColorTypes` and `vrm:materialColorValues` | `/Asset.customData.vrm:rawExtension` |
| `VrmLookAtAPI` | `/Asset/rig/LookAt` | `vrm:type`; optional `vrm:skeleton`, `vrm:leftEye`, `vrm:rightEye` joint tokens | `/Asset/rig/LookAt.customData.vrm:lookAt:raw` |
| `VrmSpringBoneAPI` | `/Asset/rig/SecondaryMotion/SpringBones/<name>` | `vrm:joints` plus parallel `vrm:stiffness`, `vrm:gravityPower`, `vrm:dragForce`, `vrm:hitRadius`, `vrm:gravityDir`; optional `vrm:center`; optional `vrm:colliderGroups` | `/Asset/rig/SecondaryMotion.customData.vrm:springBone:raw` |
| `VrmColliderAPI` | `/Asset/rig/SecondaryMotion/Colliders/<group>/Collider_<n>` | `vrm:shape`, `vrm:node`, `vrm:offset`, `vrm:radius`; `vrm:tail` for capsules | `/Asset/rig/SecondaryMotion.customData.vrm:springBone:raw` |
| `VrmConstraintAPI` | `/Asset/rig/Constraints/<name>` | `vrm:type`, `vrm:constrained`, `vrm:source`, optional `vrm:axis`, `vrm:weight` | `/Asset/rig/Constraints/<name>.customData.vrm:constraint:raw` |

Array ordering is part of the contract: every parallel array listed above uses
the same index order as its relationship or `vrm:joints` token array.

## Raw extension correspondence

| VRM source | Typed/schema destination | Preservation |
| --- | --- | --- |
| VRM 1.0 `humanoid.humanBones` / VRM 0.x `humanoid.humanBones[]` | `VrmHumanoidAPI` per-bone token attrs | Full VRM block at `/Asset.customData.vrm:rawExtension` |
| VRM 1.0 `expressions.preset/custom` / VRM 0.x `blendShapeMaster.blendShapeGroups` | `VrmExpressionAPI` expression prims, morph binds, material-color binds | Full VRM block at `/Asset.customData.vrm:rawExtension`; VRM 0.x materialValues that are not typed are diagnostic `VRM150` |
| VRM 1.0 / 0.x `lookAt` | `VrmLookAtAPI` type and eye joint tokens | Raw lookAt curves at `/Asset/rig/LookAt.customData.vrm:lookAt:raw` |
| VRM 1.0 `springBone` / VRM 0.x `secondaryAnimation` | `VrmSpringBoneAPI` and `VrmColliderAPI` | Raw spring-bone block at `/Asset/rig/SecondaryMotion.customData.vrm:springBone:raw` |
| `VRMC_node_constraint` | `VrmConstraintAPI` | Raw constraint block at each constraint prim's `customData.vrm:constraint:raw` |
| VRM meta/license | `/Asset.customData.vrm:meta` | Same location as the readable source of truth |
| MToon material extension | `vrm:shaderModel = "MToon"` plus UsdPreviewSurface fallback | `/Asset/mtl/<material>.customData.vrm:mtoon:raw` |
| KHR texture transform | `UsdTransform2d` node in the shader graph | Original material JSON remains under the raw VRM block |

## Public validator rules

`tools/validate_vrm.py` validates the stage contract without re-reading the
source `.vrm`. It fails on any `ERROR` or `FATAL`; `tools/vrm_report.py` merges
those diagnostics with import-time diagnostics.

Schema-contract-specific validator rules:

| Codes | Rule |
| --- | --- |
| `VRM270`, `VRM271` | `/Asset` carries a supported schema contract version. |
| `VRM230`-`VRM232` | Humanoid prim applies `VrmHumanoidAPI`, resolves `vrm:skeleton`, and each authored bone token names a skeleton joint. |
| `VRM240`-`VRM244` | Expression relationships resolve and all parallel arrays line up with their target relationships. |
| `VRM245`-`VRM247` | LookAt prim applies `VrmLookAtAPI`; eye tokens resolve when a skeleton relationship is authored. |
| `VRM250`-`VRM255` | Spring-bone joint tokens, collider group targets, parallel arrays, collider API application, and collider shape tokens are valid. |
| `VRM262`-`VRM264` | Constraint prims apply `VrmConstraintAPI`, use known constraint type tokens, and hierarchical joint tokens resolve. |

The complete diagnostic catalog is in `DIAGNOSTICS.md`.

## Deferred schema gaps

These are intentionally outside v1:

| Gap | v1 treatment |
| --- | --- |
| `VrmColliderGroupAPI` | Collider groups remain structural scope prims; spring chains target them with `vrm:colliderGroups`. |
| Expression texture-transform binds | Preserved in the raw VRM block. The typed v1 expression contract covers morph and material-color binds. |
| Human-bone axis metadata | Not authored as per-bone API data in v1. Consumers should use the normalized +Z stage, `UsdSkel` rest/bind transforms, and raw fallback when they need source-axis detail. |
| Canonical-rest provenance per bone | The stage is already front-normalized and carries `vrm:sourceFrontAxis` / `vrm:frontAxisNormalized`; per-bone rest provenance is deferred. |
