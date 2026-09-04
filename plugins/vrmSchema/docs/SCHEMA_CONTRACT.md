# usdVrm schema contract v1

This document freezes the public interpretation contract for the typed VRM
control schemas authored by the usdVrm importer. Downstream tools should read
the typed `Vrm*API` data first. Raw VRM JSON is preserved as a lossless fallback,
not as the primary runtime interface.

The schema source is `schema/schema.usda`; generated C++ and
`plugin/resources/vrmSchema/generatedSchema.usda` are build artifacts derived from
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

### Shading networks are not contract paths

Under `/Asset/mtl` the contract covers the **material prim** — its path, its
`vrm:shaderModel` attribute, and its `customData.vrm:mtoon:raw` fallback — and
nothing below it. The generated shader network is a rendering *realization*, so
its internal layout may change within v1:

| In the contract | Not in the contract |
| --- | --- |
| `/Asset/mtl/<material>` as the binding target | the shader prims below it |
| `vrm:shaderModel`, `customData.vrm:mtoon:raw` on that prim | node names, node count, graph nesting |

Consumers should reach the surface through `UsdShadeMaterial`'s terminal
(`ComputeSurfaceSource()`), never by assuming a prim path inside the material.
Since 2026-08-13 the UsdPreviewSurface network lives one level down, in a
`/preview` `UsdShadeNodeGraph`, and a MaterialX `/mtlx` sibling is planned
([material policy](../../../docs/design/MATERIAL_ARCHITECTURE_POLICY.md) §4);
neither is a contract-version change, because no v1 path moved and no v1
property changed meaning.

## Expression identity is an attribute, not a prim name

`vrm:expressionName` (added 2026-09-01, additive within v1) carries the
**canonical VRM 1.0 expression name**. The prim name does not, and cannot: a
source name is an arbitrary string, a USD prim name is an identifier, and this
bundle's producers sanitize independently — `usdVrmFileFormat` through its own
hashed-fallback table and `usdVrmaFileFormat` through `TfMakeValidIdentifier`.
A name outside ASCII, or one that had to take a `_2` collision suffix, therefore
lands on a **different prim name on the avatar and on the clip that drives it**.

For a VRM 1.0 file the canonical name is verbatim what the file spelled. For a
**VRM 0.x preset it is the VRM 1.0 name that preset migrates to** — 0.x names
its presets with the `BlendShapePreset` enum, a different vocabulary, and of its
seventeen entries only `neutral`, `angry` and `blink` are spelled the same in
1.0:

| VRM 0.x | VRM 1.0 | | VRM 0.x | VRM 1.0 |
| --- | --- | --- | --- | --- |
| `joy` | `happy` | | `a` `i` `u` `e` `o` | `aa` `ih` `ou` `ee` `oh` |
| `sorrow` | `sad` | | `blink_l` `blink_r` | `blinkLeft` `blinkRight` |
| `fun` | `relaxed` | | `lookup` `lookdown` … | `lookUp` `lookDown` … |

A `.vrma` clip is a VRM 1.0-era file and only ever spells the 1.0 names, so a
0.x avatar keeping its own vocabulary could never be driven by one. A 0.x custom
name (no `presetName`, or `unknown`) is not a preset and is carried through
untouched, as is a `presetName` outside the enum. The raw 0.x block at
`/Asset.customData.vrm:rawExtension` keeps every original spelling, so the
migration decides the canonical identity and not the record.

**The key is unique per stage.** Two expressions answering to one name are not
two expressions — a resolver would silently bind whichever it reached first — so
the importer keeps the first declaration and reports the rest as `VRM152`. This
is reachable in both versions: 1.0 can declare the same name under both
`expressions.preset` and `expressions.custom`, and 0.x `blendShapeGroups` is an
array. The prim names stay distinct through the uniquifier either way; it is the
key that is deduplicated.

| Side | Prim | Carries |
| --- | --- | --- |
| avatar (`.vrm`) | `/Asset/rig/Expressions/<sanitized>` | `vrm:expressionName`, plus the binds |
| clip (`.vrma`) | `/Animation/Expressions/<sanitized>` | `vrm:expressionName`, plus the weight |

A consumer joining a clip's expression weight to an avatar's morph and
material-colour binds **matches on `vrm:expressionName` and never on a path**.
That is the key `ExpressionResolve` is specified against
([motion policy](../../../docs/design/MOTION_ARCHITECTURE_POLICY.md) §4.1); the
clip half shipped it in v0.8.0 and the avatar half is the prerequisite it was
waiting on.

A stage authored before this attribute existed has expression prims without it.
That is a v1-legal old stage, not an error: old readers ignore the attribute and
a new reader that finds none has only the sanitized name to work with, which is
exactly the situation the attribute exists to end.

## Two co-active expressions are arbitrated, not summed

Expressions accumulate on their targets, and two that bind *different* morph
targets still fight when those targets displace the same vertices: an eyelid
driven by `blink` and by a `happy` that raises the cheek is driven roughly
twice as far as shut, with nothing in either weight array out of range. VRM 1.0
gives exactly one mechanism for this, and v1 carries it verbatim:

| Attribute | Says |
| --- | --- |
| `uniform token vrm:overrideBlink` | What this expression does to `blink`, `blinkLeft`, `blinkRight` while it is on |
| `uniform token vrm:overrideLookAt` | The same over `lookUp`, `lookDown`, `lookLeft`, `lookRight` |
| `uniform token vrm:overrideMouth` | The same over `aa`, `ih`, `ou`, `ee`, `oh` |

Each is `none`, `block` (the category is off while this expression is on at all)
or `blend` (the category is attenuated by this expression's own weight). Three
things about that are the contract rather than the implementation:

**A category is a set of preset names, not one expression.** The override names
"the mouth", never `aa` — and a custom expression is in no category, because VRM
reserves the preset names. A VRM 0.x rig lands in the same sets, since the
importer migrates `presetName` to the 1.0 spelling on the way in.

**An unauthored attribute is not `none`.** The importer authors one only where
the source file stated it, so a VRM 0.x expression — 0.x has no such field —
carries none of the three rather than three tokens it never said.

**The token is carried as spelled.** There is no `allowedTokens` list on these
attributes: a value outside the three reaches a consumer as data, with the
importer's `VRM153` beside it, rather than failing schema validation and taking
the rest of the avatar with it. A source value that is not a token at all — a
number, `null`, an empty string — cannot be authored onto a token attribute, so
it survives in the raw block alone and is reported under the same code. A
consumer that cannot read a token must refuse it out loud — reading it as "no
arbitration" is a face that renders wrong with nothing in the log.

Applying the rule needs a whole sample, because an override is a statement one
expression makes about *others*, so it belongs to the consumer step
(`ExpressionResolve`) and never to this layer. Additive within v1: an old reader
ignores all three and gets exactly the behaviour it had before they existed.

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
| `VrmExpressionAPI` | `/Asset/rig/Expressions/<name>` | `vrm:expressionName`, `vrm:expressionType`, `vrm:isBinary`; optional `vrm:overrideBlink`, `vrm:overrideLookAt`, `vrm:overrideMouth`; optional `vrm:morphTargets` plus parallel `vrm:morphTargetWeights`; optional `vrm:materialColorTargets` plus parallel `vrm:materialColorTypes` and `vrm:materialColorValues` | `/Asset.customData.vrm:rawExtension` |
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
| VRM 1.0 `expressions.preset/custom` / VRM 0.x `blendShapeMaster.blendShapeGroups` | `VrmExpressionAPI` expression prims, morph binds, material-color binds, and the VRM 1.0 `overrideBlink` / `overrideLookAt` / `overrideMouth` tokens | Full VRM block at `/Asset.customData.vrm:rawExtension`; VRM 0.x materialValues that are not typed are diagnostic `VRM150` |
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
