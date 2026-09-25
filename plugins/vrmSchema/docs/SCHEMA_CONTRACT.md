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
`vrm:shaderModel` attribute, its `customData.vrm:mtoon:raw` fallback, and the
canonical material schemas applied to it
([below](#material-semantics-are-interface-inputs)) — and nothing below it. The
generated shader network is a rendering *realization*, so its internal layout
may change within v1:

| In the contract | Not in the contract |
| --- | --- |
| `/Asset/mtl/<material>` as the binding target | the shader prims below it |
| `vrm:shaderModel`, `customData.vrm:mtoon:raw` on that prim | node names, node count, graph nesting |
| `VrmMaterialAPI`, `VrmMToonAPI`, `VrmTextureInfoAPI:<role>` on that prim | which realization input a canonical value is connected to |

Consumers should reach the surface through `UsdShadeMaterial`'s terminal
(`ComputeSurfaceSource()`), never by assuming a prim path inside the material.
Since 2026-08-13 the UsdPreviewSurface network lives one level down, in a
`/preview` `UsdShadeNodeGraph`, and a MaterialX `/mtlx` sibling followed on
2026-08-14 ([material policy](../../../docs/design/MATERIAL_ARCHITECTURE_POLICY.md) §4);
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
([VRM motion policy §3.3](../../../docs/design/VRM_MOTION_POLICY.md#33-expressions)); the
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

## Material semantics are interface inputs

Added 2026-09-25 (Product P5 Step 3), additive within v1. Three API schemas
carry a material's source semantics on the `UsdShadeMaterial` itself, and
every rendering realization — `/preview`, `/mtlx`, and any renderer's own —
is generated from them and from nothing else
([material policy](../../../docs/design/MATERIAL_ARCHITECTURE_POLICY.md) §3,
§6):

| Schema | Kind | Carries |
| --- | --- | --- |
| `VrmMaterialAPI` | single-apply | the glTF material core: `baseColorFactor` (RGB) and `baseColorAlphaFactor`, `metallicFactor`, `roughnessFactor`, `emissiveFactor`, `emissiveStrength` (`KHR_materials_emissive_strength`), `alphaMode`, `alphaCutoff`, `doubleSided`, `unlit` (`KHR_materials_unlit`) |
| `VrmMToonAPI` | single-apply | every non-texture field of `VRMC_materials_mtoon` 1.0, spelled as the specification spells it |
| `VrmTextureInfoAPI:<role>` | multiple-apply | one texture: `file`, `texCoord`, `wrapS`, `wrapT`, the contribution scalars `scale` / `strength`, and `KHR_texture_transform` under `transform:` (`offset`, `rotation`, `scale`) |

Five rules are the contract rather than the implementation.

**Every property is an `inputs:` attribute.** The names are
`inputs:vrm:material:<field>`, `inputs:vrm:mtoon:<field>` and
`inputs:vrm:textureInfo:<role>:<field>` — the `vrm:` namespace, inside
UsdShade's `inputs:`, as UsdLux nests `inputs:shaping:*`. UsdShade connects
only `inputs:` and `outputs:` attributes, so this is the only way a
realization graph can *read* a canonical value rather than hold a copy of it,
and the only way an animated value — an expression's material-colour bind —
reaches whichever realization the renderer selects. Measured on 26.08 in
Storm: a time-sampled `inputs:vrm:material:baseColorFactor` drives both
`/preview` and `/mtlx` frame by frame through a NodeGraph interface
connection, while the same connection to a plain `vrm:` attribute is discarded
as an invalid source and the surface draws black with no error.

**A fallback is not what a connected realization sees.** Each property's
schema fallback is the specification default, and a *reader* gets it from an
unauthored attribute. A UsdShade connection resolves to an *authored* value
only, so a graph connected to an unauthored canonical input silently takes
its own shader's default instead. A writer therefore authors every canonical
value a realization connects to. `alphaMode` has no fallback at all (its
generated C++ token would be `OPAQUE`, a Windows macro); unauthored means
`OPAQUE`, as in glTF.

**The texture roles are exactly eleven**: the glTF core `baseColor`,
`metallicRoughness`, `normal`, `occlusion`, `emissive`, and the MToon
`shadeMultiply`, `shadingShift`, `matcap`, `rimMultiply`,
`outlineWidthMultiply`, `uvAnimationMask` — each the specification's texture
name without `Texture`. The schema declares them as its allowed instance
names; `CanApplyAPI` refuses any other, but `ApplyAPI` does not check, so the
validator reports one (`VRM224`). Whether a role is colour or data is fixed by
the role and is not stored.

**Token values are glTF's and VRM's, not a realization's.** `alphaMode` is
`OPAQUE`, `MASK` or `BLEND`; `outlineWidthMode` is `none`, `worldCoordinates`
or `screenCoordinates`; `wrapS` / `wrapT` are `repeat`, `clampToEdge` or
`mirroredRepeat` — not UsdUVTexture's `clamp` / `mirror`, which is a
realization's translation. As for the expression overrides, the attributes
carry no `allowedTokens` list; the validator reports a value outside the set
(`VRM225`).

**`vrm:shaderModel` stays.** It predates `VrmMToonAPI` and contract v1 cannot
remove it, so it keeps its meaning and the two must agree: a material with
`VrmMToonAPI` applied says `vrm:shaderModel = "MToon"` (`VRM226`). The raw
block at `customData.vrm:mtoon:raw` stays the lossless fallback, and is never a
runtime API — a value a consumer needs and cannot find in the typed schemas is
a missing field, not a reason to parse JSON.

**The importer authors them on every material** (P5 Step 4, 2026-09-25):
`VrmMaterialAPI` always, `VrmMToonAPI` on an MToon material, and one
`VrmTextureInfoAPI` instance per texture the material samples, every value
written, the specification defaults included — except a texture's
`transform:*`, written only when the source states a `KHR_texture_transform`
(the fallback is the identity, so a reader sees the same mapping; a
realization can tell whether the source said it). `/preview` is generated from
these attributes alone (Step 5); `/mtlx` still reads the source material until
Step 6 ([material track](../../../docs/roadmap/material-track.md)).

### VRM 0.x MToon normalizes into the same fields

A VRM 1.0 material is a rename: the glTF core into `VrmMaterialAPI`,
`VRMC_materials_mtoon` field for field into `VrmMToonAPI`, each textureInfo
into its role. A VRM 0.x MToon material (`shader` containing `MToon`) is
`materialProperties[i]` — Unity shader property names — and lands in the
**same** fields, with no version switch in the schema. The conversion is
UniVRM's own 0.x → 1.0 migration
([`MigrationMToonMaterial.cs`](https://github.com/vrm-c/UniVRM/blob/d3665db37d5c1e97f962d0a451a7b7938df3e30a/Packages/VRM10/Runtime/Migration/Materials/MigrationMToonMaterial.cs),
[`MToon10Migrator.cs`](https://github.com/vrm-c/UniVRM/blob/d3665db37d5c1e97f962d0a451a7b7938df3e30a/Packages/VRM10/MToon10/Runtime/MToon10Migrator.cs)),
destructive choices included, so a 0.x avatar and the 1.0 file UniVRM migrates
it to carry the same canonical values (`mtoon_vrm0.vrm` / `mtoon_vrm1.vrm`
prove it). For such a material the 0.x block is the source of the
`VrmMaterialAPI` half too: the glTF core beside it is the exporter's fallback,
and older exporters wrote its colours in the wrong space.

Each row's fidelity class is DESIGN_POLICY §6's:

| VRM 0.x | Canonical | Conversion | Fidelity |
| --- | --- | --- | --- |
| `_Color` | `material:baseColorFactor`, `baseColorAlphaFactor` | RGB sRGB → linear; alpha as-is | Normalized |
| `_MainTex` | `textureInfo:baseColor` | — | Normalized |
| `_MainTex` tiling `[ox, oy, sx, sy]` | every role's `transform:offset` = `(ox, 1 − oy − sy)`, `transform:scale` = `(sx, sy)`, except `matcap` | Unity's bottom-left origin to glTF's top-left | Normalized |
| `_BlendMode` 0 / 1 / 2 / 3 | `material:alphaMode` `OPAQUE` / `MASK` / `BLEND` / `BLEND`; `mtoon:transparentWithZWrite` true for 3 only | — | Normalized |
| `_Cutoff` | `material:alphaCutoff` | cutout only; otherwise 0.5 | Normalized |
| `renderQueue` | `mtoon:renderQueueOffsetNumber` | ranked among the file's materials of the same mode: transparent, highest queue 0 then −1, −2 … (to −9); with z-write, lowest 0 then +1 … (to +9); opaque and cutout 0 | Approximate — order kept, spacing lost |
| `_CullMode` 0 / 1 / 2 | `material:doubleSided` true / true / false | glTF cannot cull front faces | Normalized; Front is Approximate |
| — | `material:unlit` = true | UniVRM marks every MToon material `KHR_materials_unlit` | Derived |
| `_ShadeColor` | `mtoon:shadeColorFactor` | sRGB → linear | Normalized |
| `_ShadeTexture` | `textureInfo:shadeMultiply` | **absent: `_MainTex` takes its place** (UniVRM's destructive choice — 0.x's GI hid a missing shade texture) | Normalized; the stand-in is Approximate |
| `_BumpMap`, `_BumpScale` | `textureInfo:normal`, its `scale` | — | Normalized |
| `_ShadeShift`, `_ShadeToony` | `mtoon:shadingShiftFactor`, `shadingToonyFactor` | with min = shift, max = lerp(1, shift, toony): shift′ = clamp(−(max + min)/2, −1, 1), toony′ = clamp((2 − (max − min))/2, 0, 1) | Normalized |
| `_IndirectLightIntensity` | `mtoon:giEqualizationFactor` | clamp(1 − x, 0, 1) | Approximate — the GI models differ |
| `_EmissionColor`, `_EmissionMap` | `material:emissiveFactor`, `textureInfo:emissive` | already linear | Normalized |
| `_SphereAdd` | `textureInfo:matcap`; `mtoon:matcapFactor` white if present, black if not | no tiling | Approximate — 1.0's MatCap is not 0.x's |
| `_RimColor`, `_RimFresnelPower`, `_RimLift`, `_RimTexture` | `mtoon:parametricRimColorFactor` (sRGB → linear), `parametricRimFresnelPowerFactor`, `parametricRimLiftFactor`, `textureInfo:rimMultiply` | — | Normalized |
| `_RimLightingMix` | `mtoon:rimLightingMixFactor` = **1**, whatever the source says | UniVRM's destructive choice: 1.0 merges rim with MatCap | Approximate |
| `_OutlineWidthMode` 0 / 1 / 2 (> 2 is 0) | `mtoon:outlineWidthMode` `none` / `worldCoordinates` / `screenCoordinates` | — | Normalized |
| `_OutlineWidth` | `mtoon:outlineWidthFactor` | world: × 0.01 (cm → m); screen: × 0.005 (percent of half the height → fraction of the height); none: 0 | Normalized |
| `_OutlineWidthTexture` | `textureInfo:outlineWidthMultiply` | — | Normalized |
| `_OutlineColor` | `mtoon:outlineColorFactor` | sRGB → linear | Normalized |
| `_OutlineColorMode`, `_OutlineLightingMix` | `mtoon:outlineLightingMixFactor` | the mix for MixedLighting (1); 0 for FixedColor (0) | Normalized |
| `_UvAnimMaskTexture`, `_UvAnimScrollX`, `_UvAnimScrollY`, `_UvAnimRotation` | `textureInfo:uvAnimationMask`, `mtoon:uvAnimationScrollXSpeedFactor`, `uvAnimationScrollYSpeedFactor` (negated: V runs the other way), `uvAnimationRotationSpeedFactor` (× 2π: turns/s → rad/s) | — | Normalized |
| `_LightColorAttenuation`, `_ReceiveShadowRate` / `_ReceiveShadowTexture`, `_ShadingGradeRate` / `_ShadingGradeTexture`, `_OutlineScaledMaxDistance`, `_OutlineCullMode`, and Unity's derived state (`_SrcBlend`, `_DstBlend`, `_ZWrite`, `_MToonVersion`, `_DebugMode`, `keywordMap`, `tagMap`) | not typed | no 1.0 field; UniVRM drops them too | Lossless in `vrm:mtoon:raw` only |

Kept from the glTF core, as UniVRM keeps them: `metallicFactor`,
`roughnessFactor`, `emissiveStrength`, the `metallicRoughness` and `occlusion`
textures, and any core texture the 0.x block does not name. **The one
departure from UniVRM:** a property absent from the 0.x block takes the MToon
0.x shader's default (`_Color` white, `_ShadeColor` (0.97, 0.81, 0.86),
`_ShadeToony` 0.9, `_IndirectLightIntensity` 0.1, `_RimFresnelPower` 1,
`_OutlineWidth` 0.5, `_CullMode` Back, the rest 0), where UniVRM's migration
would take C#'s zero — a black, transparent base colour for a missing
`_Color`. A 0.x material that is not MToon (`VRM_USE_GLTFSHADER`, the legacy
`VRM/Unlit*` shaders) carries its glTF core only and no `VrmMToonAPI`.

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
| `VrmMaterialAPI` | `/Asset/mtl/<material>` (a `UsdShadeMaterial` only) | `inputs:vrm:material:*` — every property a realization connects to is authored | none on the stage: the glTF core is typed, and what is not (sampler filters, other extensions) lives only in the source file |
| `VrmMToonAPI` | `/Asset/mtl/<material>` (a `UsdShadeMaterial` only), with `vrm:shaderModel = "MToon"` | `inputs:vrm:mtoon:*` — every property a realization connects to is authored | `/Asset/mtl/<material>.customData.vrm:mtoon:raw` |
| `VrmTextureInfoAPI:<role>` | `/Asset/mtl/<material>` (a `UsdShadeMaterial` only), one of eleven roles | `inputs:vrm:textureInfo:<role>:file`, `texCoord`; the rest where the source states them | as for the API that owns the role |

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
| glTF material core | `VrmMaterialAPI`, and `VrmTextureInfoAPI` for its five textures, on every material | none on the stage (the source file keeps what is not typed) |
| VRM 1.0 `VRMC_materials_mtoon` | `VrmMToonAPI`, and `VrmTextureInfoAPI` for its six textures; `vrm:shaderModel = "MToon"` | `/Asset/mtl/<material>.customData.vrm:mtoon:raw` (the extension block) |
| VRM 0.x `materialProperties[i]` (MToon) | The same `VrmMaterialAPI` / `VrmMToonAPI` / `VrmTextureInfoAPI` fields, converted per [the 0.x table](#vrm-0x-mtoon-normalizes-into-the-same-fields); `vrm:shaderModel = "MToon"` | `/Asset/mtl/<material>.customData.vrm:mtoon:raw` (the whole entry, Unity names and all) |
| KHR texture transform | `VrmTextureInfoAPI:<role>`'s `transform:*`, as glTF states it; each realization graph also carries its own node | Original material JSON remains under the raw VRM block |

## Public validator rules

`tools/validate_vrm.py` validates the stage contract without re-reading the
source `.vrm`. It fails on any `ERROR` or `FATAL`; `tools/vrm_report.py` merges
those diagnostics with import-time diagnostics.

Schema-contract-specific validator rules:

| Codes | Rule |
| --- | --- |
| `VRM270`, `VRM271` | `/Asset` carries a supported schema contract version. |
| `VRM222`-`VRM226` | Canonical material schemas apply only to a `UsdShadeMaterial`, use one of the eleven texture roles, carry token values from their documented sets, and agree with `vrm:shaderModel`; a canonical texture asset resolves. |
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
| Sampler filters | glTF `magFilter` / `minFilter` are not typed on `VrmTextureInfoAPI`; no realization reads them. Preserved in the raw VRM block. |
| Canonical-rest provenance per bone | The stage is already front-normalized and carries `vrm:sourceFrontAxis` / `vrm:frontAxisNormalized`; per-bone rest provenance is deferred. |
