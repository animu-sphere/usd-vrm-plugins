# usd-vrm-plugins — VRM material / look architecture policy

> The canonical policy for how VRM materials are represented in USD: the
> `/Asset/mtl` hierarchy, the separation between source semantics and render
> realizations, and the schema surface that carries MToon. It is a companion to
> [DESIGN_POLICY.md](DESIGN_POLICY.md), not a replacement: that document remains
> canonical for the importer, the schema contract, and Product P0–P6. Where the
> two overlap — the `/Asset/mtl` structure, the MToon layering, and the P5 work
> list — **this document wins**, and DESIGN_POLICY §4, §9, and §17-P5 carry
> forward-notes saying so.
>
> Section numbers are stable so other documents can cite them (e.g. "material
> policy §4"). Later revisions may add *subsections* under an existing number; a
> numbered section never changes meaning.
>
> **This introduces no new phase sequence.** The three steps in §7 are the
> internal order of **Product P5**, not a fourth sequence alongside Product
> P0–P6, Workspace Phase 0–8, and Motion Phase A–H
> ([roadmap](../roadmap/README.md#three-sequences-deliberately-separate)). The
> release P5 lands in is fixed by the
> [roadmap status table](../roadmap/README.md#status-at-a-glance), never here.

---

## 1. Purpose

VRM's material model is MToon: a toon shading model with parameters that no
portable USD shading network reproduces. The importer therefore has to answer two
different questions at once — *what did the source material say* and *how should
this look in a renderer that has never heard of MToon* — and the failure mode is
answering the first with the second.

This policy fixes the split. Its goals:

- Keep `/Asset/mtl` as the canonical material namespace.
- Keep each `UsdShadeMaterial` as the identity and binding unit for one source
  material.
- Keep renderer-specific shader implementation details out of the material prim's
  immediate children.
- Keep `UsdPreviewSurface` as a broad compatibility fallback.
- Add MaterialX as the preferred portable approximation of MToon.
- Preserve VRM/MToon source semantics independently of any rendering backend.
- Leave room for a future `hdVrmMToon` without changing the canonical
  representation.
- Keep the generated hierarchy legible in `usdview`, `usdcat`, and DCC tooling.

It maps one-to-one onto the three layers DESIGN_POLICY §9 already names:

| DESIGN_POLICY §9 layer | Here |
| --- | --- |
| Layer 1 — source semantics | `VrmMaterialAPI` / `VrmMToonAPI` / `VrmTextureInfoAPI` on the Material (§6) |
| Layer 2 — portable approximation | the `/preview` and `/mtlx` NodeGraphs (§5) |
| Layer 3 — renderer-specific realization | `hdVrmMToon` or equivalent, out of scope here (§5.3) |

---

## 2. Current state

Every shader node is a direct child of the material
([UsdVrmAuthorer.cpp](../../plugins/usdVrmFileFormat/src/usd/UsdVrmAuthorer.cpp)):

```text
/Asset/mtl/<Name>                       UsdShadeMaterial
    /Surface                            UsdPreviewSurface
    /stReader                           UsdPrimvarReader_float2
    /baseColorTexture                   UsdUVTexture
    /baseColorTexture_xf                UsdTransform2d   (KHR_texture_transform only)
    /metallicRoughnessTexture           UsdUVTexture
    /emissiveTexture                    UsdUVTexture
    /occlusionTexture                   UsdUVTexture
    /normalTexture                      UsdUVTexture
```

Behavior that must survive the restructure unchanged:

- **Unlit** routes base color to `emissiveColor`, with `diffuseColor = 0`,
  `metallic = 0`, `roughness = 1`, so scene lights do not carve facets into a
  low-poly surface.
- **factor × texture** is folded into `UsdUVTexture.scale` rather than an extra
  multiply node — base color, plus scale/bias folding for glTF occlusion strength
  and normal scale.
- Lit-only slots are skipped on unlit materials, which is also what keeps the
  emissive texture from clobbering the base-color→emissive connection: a single
  `UsdShade` input takes one source.

Two facts about this state shape everything below.

**MToon has no queryable representation.** It survives only as
`vrm:shaderModel = "MToon"` plus an opaque JSON blob at
`/Asset/mtl/<material>.customData.vrm:mtoon:raw`
([schema contract](../../plugins/vrmSchema/docs/SCHEMA_CONTRACT.md)). Nothing can
read a shading-shift factor without re-parsing JSON, which is precisely the
"typed data first, raw as fallback" rule that every other `Vrm*API` already
follows.

**Shader prim paths are load-bearing for tests.** `tests/baseline/digests/**`
keys materials by shader path (`"/Asset/mtl/Glass/Surface"`), and the golden
layers spell the paths out. Any rename churns all of them — see §7.1.

---

## 3. Core principle

Separate **material semantics** from **render realizations**.

```text
VRM / glTF source material
        ↓
canonical material semantics
        ↓
   ┌────────────┬────────────────┬──────────────┐
   ↓            ↓                ↓
UsdPreviewSurface   MaterialX        hdVrmMToon
fallback            portable         full
```

Canonical data must not depend on how any realization happens to encode
appearance, and **one realization is never derived from another**.

Avoid:

```text
MToon → MaterialX → PreviewSurface
```

Prefer:

```text
canonical semantics
  ├→ PreviewSurface
  ├→ MaterialX
  └→ renderer-specific realization
```

The rule has a cost and buys one thing. The cost is that the same source
parameter is read three times by three generators. What it buys is that deleting
a realization destroys no information, and adding a fourth one requires reading
nothing but the canonical data.

---

## 4. Canonical material hierarchy

### 4.1 Shape

One material prim per source material; each rendering realization is a
material-local `UsdShadeNodeGraph`:

```text
/Asset/mtl/<MaterialName>          UsdShadeMaterial
    /preview                       UsdShadeNodeGraph
        /surface                   UsdShadeShader
        /baseColorTexture          UsdShadeShader
        …
    /mtlx                          UsdShadeNodeGraph
        /baseColorImage            UsdShadeShader
        /baseColorMultiply         UsdShadeShader
        /surface                   UsdShadeShader
        …
```

The rule is:

> **Material** = identity, binding, and canonical semantics.
> **NodeGraph** = one rendering realization.
> **Shader** = an implementation node inside that realization.

Terminals connect material → graph, never material → an internal shader:

```text
Hair.outputs:surface       → Hair/preview.outputs:surface
Hair.outputs:mtlx:surface  → Hair/mtlx.outputs:surface
```

Material **bindings** continue to target `/Asset/mtl/<Name>` and nothing below
it. A binding that reaches into a realization defeats the whole separation.

### 4.2 Why material-local NodeGraphs

The alternative is a global implementation hierarchy —
`/Asset/mtl/_graphs/mtlx/<MaterialName>` or `/Asset/mtl/_impl/mtlx/<MaterialName>`.
Material-local wins because it gives:

- strong locality between a material and its realizations;
- easier inspection in `usdview`;
- easier deletion or replacement of a single material;
- clear ownership of generated shader nodes;
- no name-correlation between separate parallel hierarchies;
- a natural fit for VRM, where each material usually carries its own MToon
  parameter set rather than sharing one.

A shared implementation library (`/Asset/mtl/_shared`) may be introduced later if
a concrete case appears. Generated per-material graphs stay material-local by
default.

### 4.3 Naming stability

`preview` and `mtlx` are part of the authored contract. The shader node names
*inside* a realization are realization-local and deliberately are **not**.

Tests assert on the graph boundary and its outputs, not on interior node names,
so a graph can be rewritten without a fixture migration. This is the difference
between the restructure being paid for once and being paid for at every
approximation improvement.

---

## 5. Realization roles

### 5.1 UsdPreviewSurface — compatibility fallback

UsdPreviewSurface remains supported as the generic USD fallback. It is **not**
the authoritative representation of MToon.

It should give a useful approximation wherever PreviewSurface is understood but
MaterialX is not, aiming for:

- correct base color;
- correct texture/factor multiplication where representable;
- opacity / alpha approximation;
- normal map support;
- emissive support;
- a usable unlit fallback.

MToon-specific behavior — shading shift, shading toony, shade color, rim, MatCap,
outline, MToon render ordering — must **not** distort the canonical
representation in order to fit PreviewSurface.

### 5.2 MaterialX — preferred portable realization

The first MaterialX implementation is deliberately a **portable MToon
approximation**, not a complete MToon implementation. Initial scope:

- base color factor;
- base color texture;
- factor × texture semantics;
- alpha where portable;
- emission where appropriate;
- an MToon-compatible unlit approximation.

For unlit-compatible cases, prefer MaterialX's unlit surface model over
expressing unlit appearance by driving `UsdPreviewSurface.emissiveColor`. That
routing (§2) is a PreviewSurface workaround, not a semantic, and must not
propagate into a second realization.

Later iterations can explore portable approximations for shade color, shading
shift, shading toony, GI equalization, rim, and MatCap. Features that are
inherently renderer- or multi-pass-dependent — outline above all — stay canonical
semantics even when MaterialX cannot reproduce them.

**Portability rule.** Prefer standard MaterialX nodes. Do not make the portable
fallback depend on custom node definitions without a strong interoperability
case. The MaterialX graph is a generated representation, never a source of truth.

### 5.3 Renderer-specific realization

Out of scope for P5's three steps. The architecture only has to leave room for
it: a delegate-side implementation consumes the same canonical semantics and adds
no requirement to the canonical representation. Consistent with DESIGN_POLICY §9
Layer 3, renderer-specific shader implementations are not coupled into the
file-format plugin.

### 5.4 Responsibility and expected fidelity

| Representation | Responsibility | Expected fidelity |
| --- | --- | --- |
| `VrmMaterialAPI` | generic source material semantics | lossless |
| `VrmMToonAPI` | MToon source semantics | lossless |
| `VrmTextureInfoAPI` | source texture semantics | lossless |
| `/preview` graph | broad USD fallback | low / medium |
| `/mtlx` graph | portable MToon approximation | medium / high |
| `hdVrmMToon` or equivalent | complete renderer implementation | high |

This split is stated in code comments and asserted in tests. PreviewSurface and
MaterialX get **separate** expected-fidelity criteria and are never required to
produce identical images.

---

## 6. Canonical VRM material semantics

VRM semantics are grouped by schema and property namespace, applied to the
`UsdShadeMaterial` itself:

```text
/Asset/mtl/Hair
    + VrmMaterialAPI
    + VrmMToonAPI
    + VrmTextureInfoAPI:baseColor
    + VrmTextureInfoAPI:shadeMultiply
    + VrmTextureInfoAPI:matcap
```

Do **not** add a child prim such as `/Asset/mtl/Hair/vrm` to group VRM metadata.
The hierarchy under a material stays reserved for shading realizations; grouping
is what namespaces are for.

### 6.1 `VrmMaterialAPI`

Generic source material semantics needed for VRM/glTF reconstruction: base color
factor, emissive factor, alpha mode, alpha cutoff, double-sidedness.

The final property set covers source semantics that need a stable USD
representation, without duplicating values that already have a canonical home
elsewhere.

> **Rule.** UsdPreviewSurface inputs are never authoritative storage.
> PreviewSurface is a realization.

### 6.2 `VrmMToonAPI`

A **single-apply** API schema representing `VRMC_materials_mtoon` semantics — not
a shader implementation. Property names stay close to VRM/MToon terminology so
importer/exporter mapping is obvious by inspection.

Candidate fields, by group:

| Group | Fields |
| --- | --- |
| version | `specVersion` |
| rendering | `transparentWithZWrite`, `renderQueueOffsetNumber` |
| shading | `shadeColorFactor`, `shadingShiftFactor`, `shadingToonyFactor`, `giEqualizationFactor` |
| matcap | `matcapFactor` |
| rim | `parametricRimColorFactor`, `parametricRimFresnelPowerFactor`, `parametricRimLiftFactor`, `rimLightingMixFactor` |
| outline | `outlineWidthMode`, `outlineWidthFactor`, `outlineColorFactor`, `outlineLightingMixFactor` |
| UV animation | `uvAnimationScrollXSpeedFactor`, `uvAnimationScrollYSpeedFactor`, `uvAnimationRotationSpeedFactor` |

Names are verified against the target VRM 1.0 specification before schema
generation (§11).

**Semantic rule.** Properties that no realization can reproduce still live in
`VrmMToonAPI`. Outline is the archetype:

```text
outline semantics
    PreviewSurface → ignored
    MaterialX      → ignored or approximated
    hdVrmMToon     → fully rendered
```

That is the architecture working, not failing.

### 6.3 `VrmTextureInfoAPI:<slot>`

A generic **multiple-apply** API schema, so texture data does not inflate
`VrmMToonAPI`. Candidate instances:

| Group | Instances |
| --- | --- |
| glTF core | `baseColor`, `normal`, `emissive` |
| MToon | `shadeMultiply`, `shadingShift`, `matcap`, `rimMultiply`, `outlineWidth`, `uvAnimationMask` |

Per-instance properties: source file, texCoord set, and the UV transform
(offset / rotation / scale).

Do not conflate **UV transform scale** with **MToon texture contribution
scalars**. A shading-shift texture's contribution scale is a different quantity
from a UV scale and needs its own property; collapsing them is a silent
correctness bug that no structural test catches.

### 6.4 Naming and schema-contract constraints

Two constraints come from the shipped
[schema contract](../../plugins/vrmSchema/docs/SCHEMA_CONTRACT.md):

- **Namespace.** Everything shipped uses the single `vrm:` prefix
  (`vrm:humanBones:<bone>`, `vrm:skeleton`, `vrm:expressionType`). Material
  properties follow it — `vrm:mtoon:shadeColorFactor`, `vrm:material:alphaMode`,
  `vrm:textureInfo:<instance>:file` — rather than introducing sibling top-level
  namespaces. Multiple-apply properties are declared with the instance
  placeholder, e.g. `vrm:textureInfo:__INSTANCE_NAME__:file`.
- **Versioning.** Contract v1 permits adding optional typed attributes and new
  optional API schemas, so §7.3 is additive within v1 provided existing prims
  keep their meaning.

The raw fallback stays: `customData.vrm:mtoon:raw` remains the lossless fallback
alongside the typed data, matching every other `Vrm*API` in the contract.

---

## 7. Product P5 in three steps

The order is chosen so each step is independently reviewable, and so the first
rendering improvements are not coupled to the schema redesign. It is the internal
order of Product P5 and not a phase sequence (see the header).

### 7.1 Step 1 — restructure the PreviewSurface hierarchy

**Objective.** Contain PreviewSurface implementation details below
`/Asset/mtl/<MaterialName>/preview`, a `UsdShadeNodeGraph`.

```text
before                          after
/Asset/mtl/Hair                 /Asset/mtl/Hair
    /Surface                        /preview
    /stReader                           /surface
    /baseColorTexture                   /stReader
    /normalTexture                      /baseColorTexture
    …                                   /normalTexture
                                        …
```

**Work**

- Define the `/preview` NodeGraph; move the generated shader nodes below it.
- Expose a surface output on `/preview`; connect the material terminal to it.
- Keep material binding paths unchanged (§4.1).
- Keep visible behavior identical unless fixing an already-identified bug.
- Update path assumptions in readers, writers, validators, and tests.
- Regenerate `tests/baseline/digests/**` and the golden `.usda` fixtures.

**Done when**

- Existing VRM fixtures still convert successfully.
- Materials remain renderable through the PreviewSurface path.
- The generated hierarchy matches `/Asset/mtl/<name>/preview`.
- Shader nodes no longer appear among the material's immediate children.
- No material binding points at an internal shader node.
- The baseline digest diff shows **path moves only, no value changes**.

### 7.2 Step 2 — add the MaterialX realization

**Objective.** Add `/Asset/mtl/<MaterialName>/mtlx` as a `UsdShadeNodeGraph` and
make MaterialX the preferred portable representation, with PreviewSurface
remaining the fallback.

**Initial scope.** Base color factor, base color texture, correct factor ×
texture, alpha where supported, emission, an unlit-compatible surface, and the
MaterialX terminal on the Material. The feature is not blocked on full MToon
shading.

**Follow-up scope.** Once the basic path is stable: shade color, shading shift,
shading toony, GI equalization, rim, MatCap. Outline stays outside the minimum
acceptance criteria.

**Done when**

- The material carries both realizations.
- All MaterialX nodes live below `/mtlx`, which exposes a stable surface output.
- Standard MaterialX nodes are used where practical.
- PreviewSurface remains usable where MaterialX is unavailable.
- The MaterialX graph is generated from source material data, never by
  translating the generated PreviewSurface graph.
- Focused visual regression tests cover representative unlit/MToon materials.

### 7.3 Step 3 — the VRM material API schemas

**Objective.** Move source semantics out of the realizations into
`VrmMaterialAPI`, `VrmMToonAPI`, and `VrmTextureInfoAPI:<slot>`.

**Work**

- Fix schema ownership and namespaces (§6.4).
- Map VRM/glTF material fields and `VRMC_materials_mtoon` to canonical
  attributes.
- Define texture slot instance names and texture transform semantics.
- Author the schemas in the importer; consume them in readers and exporters.
- Re-point **both** generators at canonical semantics.
- Add schema round-trip tests, plus validation of allowed values and slot names.

**Migration rule.** Never carry two competing authoritative representations. The
end state is:

```text
API schema semantics
  ├→ PreviewSurface generator
  └→ MaterialX generator
```

not:

```text
API schema ↔ PreviewSurface ↔ MaterialX
```

**Done when**

- MToon parameters survive a USD round-trip independently of any approximation.
- Deleting `/preview` or `/mtlx` destroys no MToon semantic information.
- Both graphs can be regenerated from canonical schema data alone.
- Features no realization supports remain present in canonical attributes.

### 7.4 Resulting shape

```text
/Asset/mtl/<MaterialName>                 # UsdShadeMaterial
    │
    ├── canonical VRM semantics           # API schemas + namespaced attributes
    │     VrmMaterialAPI
    │     VrmMToonAPI
    │     VrmTextureInfoAPI:<slot>
    │
    ├── /preview                          # UsdShadeNodeGraph
    │     └── UsdPreviewSurface network
    │
    └── /mtlx                             # UsdShadeNodeGraph
          └── MaterialX network
```

In `usdview` the material reads as two children and nothing else:

```text
Hair [Material]
├── preview [NodeGraph]
└── mtlx    [NodeGraph]
```

The rule to preserve:

> **VRM semantics belong to schemas on the Material. Rendering implementations
> belong to child NodeGraphs.**

---

## 8. Testing

### 8.1 Test layers

Each step is independently testable.

| Layer | Asserts |
| --- | --- |
| Structural | `/Asset/mtl/<name>` is a Material; `/preview` and `/mtlx` are NodeGraphs |
| Connection | material terminal → `/preview` output; MaterialX render-context terminal → `/mtlx` output; graph output → internal surface shader; textures → expected internal inputs |
| Semantic | source MToon values → API attributes; API attributes → exported VRM values; texture slots preserve asset, texCoord, and transform; unsupported renderer features stay losslessly represented |
| Visual regression | plain base color, textured, alpha/cutout, unlit, typical MToon hair, shade texture, rim/MatCap once implemented, outline semantics checked separately from single-pass appearance |

Structural and connection tests assert the graph boundary, not interior node
names (§4.3).

### 8.2 First regression target — issue #119

The blown-out MToon/unlit investigation — front bangs clipping to near-white in
`usdview` — is the early regression target for both Step 1 and Step 2.
Base-color factor preservation has already landed, so the remaining candidates
must be isolated one at a time:

1. source base-color texture/factor semantics;
2. the current emissive/unlit approximation behavior (§2);
3. MaterialX unlit behavior;
4. color-management / render-context differences;
5. asset-specific source data actually consumed by the VRM renderer.

Do not assume `COLOR_0` must be multiplied into MToon appearance without checking
the source material and the applicable VRM/MToon specification behavior. That
these five are hard to separate today is itself an argument for the restructure.

---

## 9. Non-goals

The three steps do not require:

- complete MToon reproduction in MaterialX;
- renderer-independent outline rendering;
- a custom Hydra delegate;
- custom MaterialX node definitions;
- removal of `UsdPreviewSurface`;
- identical images from PreviewSurface and MaterialX;
- solving every DCC's MaterialX compatibility behavior.

These are addressed incrementally once the canonical structure is stable.

---

## 10. Contract changes this policy requires

Listed here rather than asserted elsewhere, so the owning documents change in
their own PRs:

| Change | Owner | Needed by |
| --- | --- | --- |
| `/Asset/mtl` gains the `/preview` and `/mtlx` NodeGraph structure | [DESIGN_POLICY.md](DESIGN_POLICY.md) §4 | Step 1 |
| Confirm whether moving shader prims under `/preview` requires a schema contract bump — the v1 path table covers `/Asset/rig/*` and the MToon raw blob stays on the material, so probably not | [SCHEMA_CONTRACT.md](../../plugins/vrmSchema/docs/SCHEMA_CONTRACT.md) | Step 1 |
| `VrmMaterialAPI`, `VrmMToonAPI`, `VrmTextureInfoAPI` added to the typed API table, with their raw fallbacks | [SCHEMA_CONTRACT.md](../../plugins/vrmSchema/docs/SCHEMA_CONTRACT.md) | Step 3 |
| The MToon row (`vrm:shaderModel` + PreviewSurface fallback) restated in terms of the typed schemas | [SCHEMA_CONTRACT.md](../../plugins/vrmSchema/docs/SCHEMA_CONTRACT.md) | Step 3 |

---

## 11. Open questions

| # | Question | Blocks |
| --- | --- | --- |
| 1 | Exact `VRMC_materials_mtoon` field names against the VRM 1.0 specification. | Step 3 |
| 2 | Render-context terminal naming (`outputs:mtlx:surface`) for the pinned OpenUSD version. | Step 2 |
| 3 | Does the Step 1 path move need a schema contract bump? (§10) | Step 1 |
| 4 | Restrict `VrmTextureInfoAPI` instance names via schema metadata, or allow arbitrary instances? | Step 3 |
| 5 | Does `vrm:shaderModel` remain once `VrmMToonAPI` exists, or become redundant? | Step 3 |
| 6 | MaterialX availability across the supported runtime matrix — 26.08 ships MaterialX 1.39.5, which has already constrained the Linux runtime builds. Confirm the plugin path is clean before the Step 2 CI lane. | Step 2 |
| 7 | Is `/Asset/mtl/_shared` ever needed, or do per-material graphs suffice? | deferred |
| 8 | Does `COLOR_0` participate in MToon appearance for the issue #119 asset? | #119 |
