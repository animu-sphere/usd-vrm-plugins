---
status: accepted
owner: usd-vrm-plugins
---

# usd-vrm-plugins — Hydra imaging policy (`vrmImaging`)

> The canonical policy for `vrmImaging`, the plugin that exposes the
> repository's canonical `Vrm*API` material data to Hydra without moving
> renderer behaviour into the format repository. It is a companion to
> [MATERIAL_ARCHITECTURE_POLICY.md](MATERIAL_ARCHITECTURE_POLICY.md), which
> stays canonical for what the material schemas mean, and it answers to
> [INTEGRATION_SCOPE_POLICY.md](INTEGRATION_SCOPE_POLICY.md) for whether an
> identity may exist at all.
>
> Related repositories: [`hydra-toon`](https://github.com/animu-sphere/hydra-toon)
> consumes these semantics and owns the native MToon rendering realization;
> `usd-mmd-plugins` plans the same bridge for its own schemas (§23).
>
> Section numbers are stable so other documents can cite them ("imaging
> policy §9"). Later revisions may add subsections; a numbered section never
> changes meaning.
>
> **This introduces no new phase sequence.** The steps in §20, I0–I5, are the
> internal order of one track, not a sequence beside Product P0–P6 and
> Workspace Phase 0–8 ([roadmap](../roadmap/README.md#sequences)). The open
> steps are the [imaging track](../roadmap/imaging-track.md).
>
> **Step I0 implemented 2026-09-26** (`plugins/vrmImaging`). What it measured
> on OpenUSD 26.08 is §27; one finding bounds §9 from outside this plugin —
> UsdImaging dirties a Material's whole network whenever any interface input
> changes, so every canonical edit dirties `material` as well as its own
> `vrm/…` locator.

---

## 1. Decision

Add a **`vrmImaging` plugin** to `usd-vrm-plugins`.

Its role is:

> Translate canonical VRM USD schemas into Hydra-facing data sources and
> invalidation signals, without implementing a renderer-specific MToon look.

The intended boundary is:

```text
VRM / VRM 0.x
    ↓
usdVrmFileFormat
    ↓
UsdShadeMaterial
  + VrmMaterialAPI
  + VrmMToonAPI
  + VrmTextureInfoAPI:<role>
    ↓
vrmImaging
    ↓
Hydra data sources
    ↓
hydra-toon adapter / normalization
    ↓
ToonMaterial
    ↓
Vulkan | WebGPU
```

The source of truth remains the composed USD stage and the schemas authored on
it.

`vrmImaging` does **not** become another canonical model.

---

## 2. Why `vrmImaging` is needed

The canonical material contract already exists:

- `VrmMaterialAPI`
- `VrmMToonAPI`
- `VrmTextureInfoAPI:<role>`

Their attributes live on `UsdShadeMaterial` as Material interface inputs:

```text
inputs:vrm:material:*
inputs:vrm:mtoon:*
inputs:vrm:textureInfo:<role>:*
```

That is the correct USD representation.

However, a Hydra renderer normally consumes the Hydra representation produced
by UsdImaging rather than arbitrarily querying every applied USD API schema
itself.

The native MToon renderer therefore needs a stable bridge:

```text
canonical USD semantics
        ↓
UsdImaging
        ↓
Hydra-visible semantics
```

Without this bridge, `hydra-toon` would have to reach back into the stage and
interpret `Vrm*API` directly. That would:

- couple the renderer adapter to VRM schema details;
- bypass Hydra's normal dirty/invalidation flow;
- duplicate schema interpretation inside the renderer;
- make other Hydra consumers unable to reuse the VRM imaging work;
- blur the boundary between format semantics and rendering realization.

`vrmImaging` closes that gap.

---

## 3. Ownership boundary

### 3.1 `usd-vrm-plugins` owns

`usd-vrm-plugins` owns:

- VRM source parsing;
- VRM 0.x → canonical VRM 1.0-style normalization;
- `VrmMaterialAPI`;
- `VrmMToonAPI`;
- `VrmTextureInfoAPI`;
- the meanings, defaults and validation rules of those schemas;
- `vrmImaging`;
- mapping USD property changes to Hydra invalidation;
- exposing canonical semantics to Hydra.

### 3.2 `hydra-toon` owns

`hydra-toon` owns:

- `ToonMaterial`;
- MToon shader implementation;
- toon lighting;
- shading shift / toony;
- rim;
- MatCap;
- outline rendering;
- transparency behavior;
- render queue behavior;
- GPU resource representation;
- Vulkan pipelines;
- WebGPU pipelines;
- renderer-specific normalization and caching.

### 3.3 `vrmImaging` must not own

Do **not** put these into `vrmImaging`:

```text
MToon shader code
SPIR-V / WGSL generation
ToonMaterial
Vulkan descriptors
pipeline selection
inverted-hull rendering
render queue execution
Hydra-toon-specific C++ types
```

The key rule is:

> `vrmImaging` describes what the VRM material says.  
> `hydra-toon` decides how that meaning is rendered.

---

## 4. Repository placement

Recommended workspace layout:

```text
usd-vrm-plugins/
  plugins/
    vrmSchema/
    vrmImaging/                 # new
      CMakeLists.txt
      plugin/
        plugInfo.json
      src/
        materialAPIAdapter.cpp
        materialAPIAdapter.h
        mtoonAPIAdapter.cpp
        mtoonAPIAdapter.h
        textureInfoAPIAdapter.cpp
        textureInfoAPIAdapter.h
        materialDataSource.cpp
        materialDataSource.h
        tokens.cpp
        tokens.h
    usdVrmFileFormat/
    usdVrmPackageResolver/
    usdVrmaFileFormat/
    execVrm/
```

Names may be adjusted to match the repository's CMake conventions, but the
plugin itself should remain a distinct build target.

Recommended target name:

```text
vrmImaging
```

Do not fold it into:

```text
vrmSchema
usdVrmFileFormat
hydra-toon
```

Those have different dependency and lifecycle boundaries.

---

## 5. Primary implementation mechanism

Use **UsdImaging API schema adapters** as the first implementation path.

Conceptually:

```text
UsdImagingAPISchemaAdapter
    ├─ VrmMaterialAPI adapter
    ├─ VrmMToonAPI adapter
    └─ VrmTextureInfoAPI adapter
```

The relevant responsibilities are:

```text
GetImagingSubprimData(...)
    USD schema → Hydra data-source contribution

InvalidateImagingSubprim(...)
    changed USD properties → Hydra dirty locators
```

No additional Hydra child prim is required for the initial material path.

The adapters contribute data to the primary Hydra material prim.

### Why API-schema adapters first

The semantics already exist as applied API schemas.

Therefore the natural direction is:

```text
applied USD API schema
        ↓
UsdImaging API schema adapter
        ↓
Hydra material prim contribution
```

rather than inventing a second USD prim hierarchy only for rendering.

This also keeps `VrmTextureInfoAPI:<role>` naturally tied to its
multiple-apply instance name.

---

## 6. Hydra representation

### 6.1 Do not overwrite the portable material realization

The existing Hydra material representation may already contain the ordinary
surface network originating from:

```text
/preview
/mtlx
```

`vrmImaging` must not destroy or rewrite those networks merely to expose MToon
semantics.

Instead, publish the VRM canonical semantics as an **additional Hydra-visible
data-source contribution**.

Conceptually:

```text
Hydra material prim
├─ material
│   └─ existing PreviewSurface / MaterialX network
│
└─ vrm
    ├─ material
    ├─ mtoon
    └─ textureInfo
```

The exact token/container shape should be finalized against OpenUSD 26.08
before freezing it, but the ownership model should remain this shape.

### 6.2 Proposed logical data model

```text
vrm
├─ material
│   ├─ baseColorFactor
│   ├─ baseColorAlphaFactor
│   ├─ metallicFactor
│   ├─ roughnessFactor
│   ├─ emissiveFactor
│   ├─ emissiveStrength
│   ├─ alphaMode
│   ├─ alphaCutoff
│   ├─ doubleSided
│   └─ unlit
│
├─ mtoon
│   ├─ shadeColorFactor
│   ├─ shadingShiftFactor
│   ├─ shadingToonyFactor
│   ├─ giEqualizationFactor
│   ├─ matcapFactor
│   ├─ parametricRimColorFactor
│   ├─ parametricRimFresnelPowerFactor
│   ├─ parametricRimLiftFactor
│   ├─ rimLightingMixFactor
│   ├─ outlineWidthMode
│   ├─ outlineWidthFactor
│   ├─ outlineColorFactor
│   ├─ outlineLightingMixFactor
│   ├─ uvAnimationScrollXSpeedFactor
│   ├─ uvAnimationScrollYSpeedFactor
│   ├─ uvAnimationRotationSpeedFactor
│   ├─ transparentWithZWrite
│   └─ renderQueueOffsetNumber
│
└─ textureInfo
    ├─ baseColor
    ├─ metallicRoughness
    ├─ normal
    ├─ occlusion
    ├─ emissive
    ├─ shadeMultiply
    ├─ shadingShift
    ├─ matcap
    ├─ rimMultiply
    ├─ outlineWidthMultiply
    └─ uvAnimationMask
```

Each texture role exposes its canonical fields:

```text
file
texCoord
wrapS
wrapT
scale
strength
transform.offset
transform.rotation
transform.scale
```

This representation is an imaging view of the canonical schemas.

It is **not** a new public USD contract.

---

## 7. Preserve canonical naming

Where practical, Hydra-facing names should be mechanically derived from the
canonical schema names.

Avoid mappings such as:

```text
shadingToonyFactor → toonSharpness
outlineWidthFactor → edgeThickness
```

Prefer:

```text
shadingToonyFactor → shadingToonyFactor
outlineWidthFactor → outlineWidthFactor
```

Renderer-friendly terminology belongs below the imaging boundary.

This minimizes:

- translation tables;
- documentation drift;
- ambiguity;
- renderer-specific leakage.

---

## 8. Material selection remains outside `vrmImaging`

`vrmImaging` should expose facts, not choose a renderer pipeline.

For example, it may expose:

```text
VrmMToonAPI is applied
alphaMode = BLEND
outlineWidthMode = worldCoordinates
transparentWithZWrite = true
```

It should **not** emit:

```text
pipeline = mtoon_transparent_outline
```

That decision belongs to `hydra-toon`.

The expected consumer logic is approximately:

```text
if VrmMToon semantics are present:
    ToonShadingModel::MToon
else:
    use ordinary Hydra material network / PreviewSurface path
```

---

## 9. Dirty propagation

Dirty propagation is one of the most important responsibilities of
`vrmImaging`.

A changed canonical USD property should invalidate only the Hydra data needed
for that semantic.

Examples:

```text
inputs:vrm:material:baseColorFactor
    ↓
vrm.material.baseColorFactor

inputs:vrm:mtoon:shadeColorFactor
    ↓
vrm.mtoon.shadeColorFactor

inputs:vrm:textureInfo:matcap:file
    ↓
vrm.textureInfo.matcap.file
```

Do not invalidate an entire material blindly when a precise locator can be
returned.

This matters because expression-driven material changes may eventually occur
at high frequency.

### Structural versus value changes

Treat these differently.

Value-like changes:

```text
baseColorFactor
shadeColorFactor
shadingShiftFactor
shadingToonyFactor
rim factors
outline width/color
UV animation values
```

should normally update only the relevant parameter data.

Structural changes:

```text
VrmMToonAPI applied / removed
texture identity changed
alpha mode changed
double-sidedness changed
texture role added / removed
```

may require broader material re-normalization downstream.

`vrmImaging` reports the Hydra dirtiness; the renderer decides the resulting
GPU work.

---

## 10. Sampled values

Do not assume canonical material values are permanently uniform.

The material policy explicitly allows runtime or time-sampled changes,
including expression-driven material color changes.

Therefore the imaging representation must preserve time-sampling where the USD
attribute supports it.

Desired path:

```text
time-sampled inputs:vrm:mtoon:shadeColorFactor
        ↓
sampled Hydra data source
        ↓
hydra-toon material parameter update
```

Avoid copying values once at population time into immutable retained data when
that would lose time variation.

---

## 11. Texture assets

`VrmTextureInfoAPI` already owns texture semantics.

`vrmImaging` should expose its asset/path information and sampling semantics,
but it should not become an image loader.

Do not add:

```text
stb_image
KTX decoder
GPU upload
sampler allocation
descriptor allocation
```

to `vrmImaging`.

The renderer/runtime remains responsible for resource realization.

The imaging layer exposes enough information to make that realization
possible.

---

## 12. Renderer independence

The following must be true:

```text
usd-vrm-plugins
    does NOT link hydra-toon

hydra-toon
    does NOT link usdVrmFileFormat

hydra-toon
    may consume Hydra data produced by vrmImaging
```

`vrmImaging` may depend on:

```text
vrmSchema
usd
usdImaging
hd
tf
plug
```

as required by the OpenUSD adapter implementation.

It should not depend on:

```text
Vulkan
WebGPU
Slang
hydra-toon renderer core
usdVrmFileFormat importer internals
```

This preserves the repository's existing rule that the composed USD stage and
schemas are the integration boundary.

---

## 13. Relationship with `/preview` and `/mtlx`

All three paths coexist:

```text
canonical semantics
   ├─ /preview     → generic PreviewSurface fallback
   ├─ /mtlx        → portable MaterialX approximation
   └─ vrmImaging   → native Hydra semantic exposure
                         ↓
                    hydra-toon
                         ↓
                    full MToon
```

None is derived from another.

Do not implement:

```text
/preview → vrmImaging
/mtlx → vrmImaging
hydra-toon → inspect /mtlx to recover MToon
```

Instead:

```text
Vrm*API → all three independently
```

---

## 14. `hydra-toon` integration

The initial `hydra-toon` integration should be:

```text
Hydra material prim
        ↓
read vrmImaging data
        ↓
normalize into ToonMaterial
        ↓
persistent material slot
        ↓
fixed MToon pipeline set
```

`hydra-toon` should keep its own renderer-private structure:

```cpp
enum class ToonShadingModel {
    PreviewSurface,
    MToon,
    MMD
};
```

`vrmImaging` must not export `ToonMaterial`.

That representation is deliberately renderer-private.

---

## 15. Legacy Hydra versus scene-index path

The long-term target is the **scene-index/data-source path**.

Do not design `vrmImaging` primarily around legacy `HdMaterialNetworkMap`
structures.

If a compatibility bridge is required for a currently supported host, keep it
small and isolated.

Priority:

```text
1. OpenUSD 26.08 scene-index / data-source path
2. compatibility bridge only where measured necessary
3. no duplicate permanent architecture
```

The repository should document the exact OpenUSD versions for which the
adapter is tested.

---

## 16. Plugin discovery

`vrmImaging` must be a normal OpenUSD plugin discovered through
`plugInfo.json`.

A runtime containing:

```text
vrmSchema
vrmImaging
hydra-toon
```

should require no application-side registration code.

Validation should prove:

```text
PlugRegistry
  ↓
vrmImaging discovered
  ↓
API schema adapters registered
  ↓
Hydra stage population exposes VRM semantics
```

This should work through OpenStrata runtime composition.

---

## 17. Testing strategy

### 17.1 Adapter discovery test

Verify:

```text
vrmImaging plugin is discovered
VrmMaterialAPI adapter registered
VrmMToonAPI adapter registered
VrmTextureInfoAPI adapter registered
```

### 17.2 Canonical → Hydra tests

For a hand-authored material:

```text
UsdShadeMaterial
+ VrmMaterialAPI
+ VrmMToonAPI
+ VrmTextureInfoAPI:baseColor
```

verify that the Hydra scene index contains the corresponding values.

Do not involve a `.vrm` parser in this test.

This proves that `vrmImaging` consumes the schema contract rather than importer
internals.

### 17.3 Import → imaging integration test

For a small `.vrm` fixture:

```text
.vrm
 ↓
usdVrmFileFormat
 ↓
Vrm*API
 ↓
vrmImaging
 ↓
Hydra data
```

compare selected source semantics end-to-end.

### 17.4 Invalidation test

Modify one value:

```text
shadeColorFactor
```

and assert that only its expected Hydra locator is invalidated.

Repeat with:

```text
baseColorFactor
outlineWidthFactor
matcap texture
```

### 17.5 Time-sampling test

Author:

```text
inputs:vrm:material:baseColorFactor
```

with at least two time samples and confirm that Hydra observes both.

### 17.6 Multiple-apply texture-role test

Apply several instances simultaneously:

```text
VrmTextureInfoAPI:baseColor
VrmTextureInfoAPI:shadeMultiply
VrmTextureInfoAPI:matcap
```

and verify that instance identity is preserved.

### 17.7 Renderer integration smoke test

With `hydra-toon` available:

```text
VRM material
 ↓
vrmImaging
 ↓
hydra-toon
```

verify that MToon is selected without:

- parsing raw JSON;
- inspecting importer state;
- relying on `/preview`;
- relying on `/mtlx`.

---

## 18. Validation rules

Add validation/reporting for the imaging layer where useful.

Candidate checks:

```text
VRMI001  vrmImaging plugin not discoverable
VRMI002  VrmMaterialAPI applied but imaging contribution missing
VRMI003  VrmMToonAPI applied but imaging contribution missing
VRMI004  invalid VrmTextureInfoAPI role not exposed
VRMI005  canonical property changed without expected invalidation
```

These codes are illustrative; use the repository's existing diagnostic
numbering policy before freezing them.

Do not duplicate schema validation already owned by `vrmSchema`.

---

## 19. CMake / packaging

Add `vrmImaging` as an independent plugin target.

Conceptually:

```text
vrmImaging
  PUBLIC/PRIVATE
    vrmSchema
    usd
    usdImaging
    hd
    tf
    plug
```

Exact linkage should follow OpenUSD's exported target names in the supported
runtime.

Packaging must include:

```text
plugin binary
plugInfo.json
required resources
```

The plugin should be composable independently through OpenStrata.

Recommended runtime relationship:

```text
usd-vrm runtime
  ├─ vrmSchema
  ├─ usdVrmFileFormat
  ├─ usdVrmPackageResolver
  └─ vrmImaging
```

A headless conversion-only deployment may eventually omit `vrmImaging`, so do
not make the importer depend on it.

---

## 20. Implementation steps

### Step I0 — contract experiment

Goal: resolve the remaining material-imaging question before freezing public
Hydra tokens.

Work:

- build one minimal `UsdImagingAPISchemaAdapter`;
- target OpenUSD 26.08;
- expose one `VrmMToonAPI` value;
- inspect the resulting scene-index prim;
- confirm invalidation after changing the USD attribute;
- verify behavior in the Hydra path `hydra-toon` will read (its MAT-Q1,
  Renderer Phase 1); until `hydra-toon` has a material path, the test
  consumer stands in for it.

Done when:

```text
VrmMToonAPI.shadingToonyFactor
    ↓
Hydra
    ↓
test consumer
```

works without direct stage querying.

### Step I1 — material core

Implement:

```text
VrmMaterialAPI
VrmMToonAPI
```

Expose scalar/vector/token/bool semantics and precise invalidation.

Done when all non-texture canonical values have Hydra coverage.

### Step I2 — texture info

Implement:

```text
VrmTextureInfoAPI:<role>
```

including all eleven allowed roles and their multiple-apply instance identity.

Done when a Hydra consumer can reconstruct the complete texture semantic
record from data sources.

### Step I3 — `hydra-toon` handshake

Integrate with `hydra-toon`.

Done when:

```text
VrmMToonAPI
    ↓
vrmImaging
    ↓
ToonMaterial::MToon
```

works and the renderer contains no VRM raw-JSON path.

### Step I4 — animated material semantics

Verify time-varying canonical properties and expression-driven values.

Done when a canonical color change reaches a live `hydra-toon` material slot
without rebuilding geometry or shader pipelines.

### Step I5 — hardening

Add:

- plugin discovery CI;
- OpenUSD compatibility matrix;
- invalidation regression tests;
- documentation;
- OpenStrata composition test;
- representative VRM integration fixture.

---

## 21. Initial non-goals

The first `vrmImaging` release does not need to expose all VRM runtime
semantics.

Do not expand the first implementation into:

```text
humanoid retargeting
expression arbitration
look-at evaluation
spring-bone simulation
constraints
MotionPose
VRMA playback
```

Those belong to the motion/runtime architecture.

The first objective is deliberately narrow:

> Make canonical VRM material semantics first-class Hydra input.

Other `Vrm*API` imaging adapters can be added later when a concrete Hydra
consumer needs them.

---

## 22. Future extension

Once the material path is proven, the same architectural pattern may be
appropriate for other descriptive VRM semantics where Hydra visibility is
useful.

Possible later areas:

```text
VRM expression metadata
look-at metadata
avatar annotations
humanoid semantic annotations
```

But runtime evaluation should remain outside imaging.

A useful distinction is:

```text
vrmImaging = description → Hydra visibility
exec/runtime = state → evaluated result
```

Do not turn `vrmImaging` into an execution engine.

---

## 23. Cross-repository symmetry

The intended ecosystem becomes:

```text
usd-vrm-plugins
  vrmSchema
  vrmImaging
       │
       └──────────────┐
                      ↓
                 hydra-toon
                      ↑
       ┌──────────────┘
       │
usd-mmd-plugins
  mmdSchema
  mmdImaging
```

This symmetry is useful, but the two imaging plugins do not need to share
implementation prematurely.

First make both contracts work independently.

Common infrastructure may be extracted later if genuine duplication appears.

---

## 24. Key architectural invariants

The following should be treated as non-negotiable unless a later design
decision explicitly supersedes them.

1. **The composed USD stage is the repository boundary.**
2. **`Vrm*API` remains the canonical VRM semantic contract.**
3. **`vrmImaging` is a bridge, not another canonical model.**
4. **Raw VRM JSON is never an imaging/runtime API.**
5. **Portable `/preview` and `/mtlx` graphs remain independent realizations.**
6. **`vrmImaging` contains no native MToon renderer.**
7. **`hydra-toon` owns `ToonMaterial` and GPU realization.**
8. **Dirty propagation should be as narrow as the Hydra data model permits.**
9. **Time-varying canonical values must remain time-varying through imaging.**
10. **Vulkan/WebGPU dependencies never enter `vrmImaging`.**
11. **The importer must remain usable without the imaging plugin.**
12. **The imaging plugin must be reusable by a Hydra renderer other than
    `hydra-toon`.**

---

## 25. Recommended immediate work

The next concrete task should be **Step I0 only**.

Create the smallest possible `vrmImaging` proof:

```text
plugins/vrmImaging/
  CMakeLists.txt
  plugin/plugInfo.json
  src/mtoonAPIAdapter.cpp
  tests/
```

Use one hand-authored stage:

```usda
def Material "Hair" (
    prepend apiSchemas = ["VrmMaterialAPI", "VrmMToonAPI"]
)
{
    float inputs:vrm:mtoon:shadingToonyFactor = 0.35
}
```

(Not 0.9, which is the schema's fallback for the field: a test reading 0.9
cannot tell the attribute from the definition. §27.)

Then prove:

```text
USD value
  ↓
VrmMToonAPI
  ↓
UsdImaging API-schema adapter
  ↓
Hydra data source
  ↓
test consumer
```

and:

```text
attribute edit
  ↓
InvalidateImagingSubprim
  ↓
only the expected Hydra locator dirtied
```

Do not freeze the complete Hydra-facing token hierarchy until this experiment
has been inspected on OpenUSD 26.08.

Once that experiment succeeds, Steps I1–I3 can proceed with much lower
architectural risk.

---

## 26. Conclusion

`usd-vrm-plugins` already has the correct semantic foundation for native MToon:
typed canonical schemas on `UsdShadeMaterial`.

The missing piece is not another material representation and not renderer code.

It is the imaging bridge:

```text
Vrm*API
   ↓
vrmImaging
   ↓
Hydra
   ↓
hydra-toon
```

Adding `vrmImaging` makes the responsibility boundary explicit:

> `usd-vrm-plugins` explains VRM to Hydra.  
> `hydra-toon` explains Hydra's VRM semantics to the GPU.

That boundary keeps the format plugin reusable, the renderer independent, and
the MToon semantics stable across Vulkan, WebGPU, and future Hydra consumers.

---

## 27. Measured in Step I0

Step I0 (`plugins/vrmImaging`, 2026-09-26) built the `VrmMToonAPI` adapter on
OpenUSD 26.08 and read it through `UsdImagingStageSceneIndex` from a test
consumer, on a hand-authored stage (`vrmImaging_mtoon`). What it settled, and
what it found:

1. **The shape holds.** The adapter's container overlays the material prim
   beside UsdImaging's `material` container, which is untouched; a material
   without the schema has no `vrm` container. The adapter is registered
   through `plugInfo.json` (`Types`, `bases: UsdImagingAPISchemaAdapter`,
   `apiSchemaName: VrmMToonAPI`) and loaded by UsdImaging's adapter registry
   the first time a prim carrying the schema is populated. No application
   code registers anything (§16).
2. **Names are mechanical (§7).** The adapter names no field. The field set
   is every `inputs:vrm:mtoon:<field>` property of the registered
   definition, read from the schema registry by the schema's name. So
   `vrmSchema` is needed in the session and not linked, and a property
   authored under the prefix that the schema does not define is never
   exposed.
3. **Values are resolved, not only authored.** An unauthored field carries
   its schema fallback, so a consumer never restates the schema's defaults.
   Decided here, because the material network does the opposite: UsdShade
   resolves an interface connection to authored values only (material policy
   §6.4.1).
4. **Sampled, not copied (§10).** Each field is a
   `UsdImagingDataSourceAttribute`. Moving the scene index's time dirties a
   time-sampled field's own locator and nothing else, and its value follows.
   UsdImaging records a field as time-varying when its data source is
   **built**, so a field no consumer has read is never dirtied by time. A
   renderer reads what it draws first, so this costs nothing, but a test has
   to read before it moves the time.
5. **This plugin's invalidation is per field (§9).** An edit of
   `inputs:vrm:mtoon:<field>` dirties `vrm/mtoon/<field>` and nothing else of
   the contribution. A mutation that dirties `vrm/mtoon` instead fails the
   suite.
6. **UsdImaging's is not.** On every canonical edit, OpenUSD 26.08 also
   dirties the whole `material` locator, whether or not any network reads
   the input: `UsdImagingDataSourceMaterialPrim::Invalidate` dirties the
   network on any interface-input change ("TODO, invalidate specifically
   connected node parameters. FOR NOW: just dirty the whole material"). Every
   canonical attribute is an interface input (material policy §6.4.1, q9).
   A consumer therefore cannot tell a value edit from a network edit by the
   `material` locator. It sees both locators and has to decide from the
   `vrm` one. This bears on Step I4's high-frequency expression colours, is
   outside this plugin, and is pinned by the suite so a runtime that narrows
   it is noticed. Candidate answers, not taken yet: an upstream fix of that
   TODO, or a filtering scene index downstream. Moving the canonical
   attributes out of `inputs:` is excluded, because q9 decided the namespace
   so an animated value can reach a realization.
7. **Scene index only (§15).** UsdImaging consults API-schema adapters only
   when it populates through the stage scene index. The legacy
   `UsdImagingDelegate` never does. A host on the legacy path sees no `vrm`
   data at all.
8. **Two ways to be silently absent.** With no `vrmSchema` registered, no
   prim's definition includes `VrmMToonAPI` and the adapter is never asked
   (`vrmImaging_mtoon_without_schema`). With `USDIMAGING_ENABLE_PLUGINS=0`,
   UsdImaging drops every adapter not marked internal, this one included.
   Neither raises anything; diagnostics are §18's business.
9. **`ost` has no plugin kind for a UsdImaging adapter** (0.23.8 knows
   file formats, asset and package resolvers, exec, schemas and usdview
   plugins). So `vrmImaging` has no descriptor. The root build adds it by
   name, builds it and tests it, and no package carries it. §19's packaging
   waits on that kind (Step I5).
10. **The §25 fixture could not tell authored from fallback.** Its 0.9 is
    `shadingToonyFactor`'s schema fallback. The fixture and §25 use 0.35.

Not measured: the Hydra path `hydra-toon` will read (§20 Step I0's last
item). `hydra-toon` has no material path yet — its MAT-Q1 is Renderer
Phase 1 — so the test consumer stands in for it, and the handshake is
Step I3.
