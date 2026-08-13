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

**Steps 1 and 2 have landed (2026-08-13, 2026-08-14).** Every shader node lives
inside a realization graph, and unlit materials carry a second one
([UsdVrmAuthorer.cpp](../../plugins/usdVrmFileFormat/src/usd/UsdVrmAuthorer.cpp)):

```text
/Asset/mtl/<Name>                       UsdShadeMaterial
    outputs:surface       → preview.outputs:surface
    outputs:mtlx:surface  → mtlx.outputs:surface        (unlit materials only)
    + MaterialXConfigAPI, config:mtlx:version = "1.39"  (with /mtlx)
    /preview                            UsdShadeNodeGraph
        outputs:surface  → surface.outputs:surface
        /surface                        UsdPreviewSurface
        /stReader                       UsdPrimvarReader_float2
        /baseColorTexture               UsdUVTexture
        /baseColorTexture_xf            UsdTransform2d   (KHR_texture_transform only)
        /metallicRoughnessTexture       UsdUVTexture
        /emissiveTexture                UsdUVTexture
        /occlusionTexture               UsdUVTexture
        /normalTexture                  UsdUVTexture
    /mtlx                               UsdShadeNodeGraph
        outputs:surface  → surface.outputs:surface
        /surface                        ND_gltf_pbr_surfaceshader
        /st                             ND_texcoord_vector2      (textured only)
        /baseColorPlace                 ND_place2d_vector2       (KHR_texture_transform only)
        /baseColorImage                 ND_image_color4          (textured only)
        /baseColorFactor                ND_multiply_color4       (textured only)
        /baseColorSplit                 ND_separate4_color4      (textured only)
        /baseColorRgb                   ND_combine3_color3       (textured only)
```

Which materials get a `/mtlx` graph is decided by `KHR_materials_unlit`, the
same flag `/preview`'s unlit branch reads, so the two realizations never
disagree about what "unlit" means. On the vendored corpus that is 13 of 13
materials for one avatar and 10 of 17 for the other — the remaining 7 are
ordinary glTF PBR accessories (a backpack, glass, a logo), which is the lit
follow-up in §7.2, not an oversight.

The behavior below is what the restructure had to leave unchanged, and did —
the baseline diff is a path move (§7.1). It is still the behavior any later
change to the `/preview` graph has to account for:

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
follows. Step 1 moved nodes; it did not make MToon queryable. That is Step 3.

**Shader prim paths are load-bearing for the baseline.**
`tests/baseline/digests/**` keys materials by shader path (now
`"/Asset/mtl/Glass/preview/surface"`), and the golden layers spell the paths
out. Step 1 churned all of them once; §4.3 is what keeps a *later* graph
rewrite from churning the behavior tests as well.

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

Structural and connection tests assert on the graph boundary and its outputs,
not on interior node names, so a graph can be rewritten without a fixture
migration. This is the difference between the restructure being paid for once
and being paid for at every approximation improvement.

**What that does not cover** (recorded when Step 1 landed, so the claim is not
read wider than it is). Two kinds of test legitimately name interior nodes,
because a *value* is what they assert:

- the baseline freeze (`tests/baseline/**`), which keys the shader network by
  path — its whole purpose is that nothing changes silently;
- value tests such as "the base-color factor is folded into
  `UsdUVTexture.scale`", which cannot be written against the boundary alone.

So an interior rename is cheap in the test suite and not free in the baseline:
it regenerates as a path move and is reviewed as one. What §4.3 buys is that no
*structural* test has to be rewritten, and that the boundary is asserted
independently of whatever the graph does inside.

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

### 5.2.1 What the pinned runtime can actually render

**Measured on OpenUSD 26.08 / MaterialX 1.39.5 when Step 2 landed
(2026-08-14).** The preference above survives as an ordering rule but not as a
node choice: MaterialX's two direct ways to say *unlit* do not reach the screen
through hdSt.

| Terminal | Result in Storm |
| --- | --- |
| `ND_surface_unlit` | fails to compile — generated GLSL references undeclared `u_envRadianceMips` / `u_envLightIntensity` / `u_envMatrix`; the prim then draws as hdSt's flat grey fallback |
| `ND_convert_color4_surfaceshader` | fails to compile, same undeclared uniforms |
| `ND_surface` with an EDF and no BSDF | compiles and renders, but `opacity` has **no effect** — a VRM's alpha-blended hair and eyelashes come out solid |
| `ND_surface` with an EDF *and* a BSDF | works, opacity included |
| `ND_gltf_pbr_surfaceshader` | works, and is the only one whose alpha the renderer reads as glTF defines it |

The common factor in the failures is a surface with no BSDF. A dome light does
not help, and there is **no fallback**: a material carrying an unrenderable
`outputs:mtlx:surface` does not quietly fall back to the universal terminal — it
draws as hdSt's default surface, a flat grey, so the failure looks like an
untextured asset rather than an error (§5.5).

`hdSt`'s material tag — opaque, masked, or translucent — is chosen from the
*terminal node's family*, with dedicated rules for `UsdPreviewSurface`,
`standard_surface`, `open_pbr_surface` and `gltf_pbr` and a generic fallback for
everything else. That is why `standard_surface` handles opacity correctly even
though its own implementation graph ends in the same `surface` node that fails
here: the decision never looks inside the graph.

So the shipped choice is `gltf_pbr` with the lit response zeroed
(`base_color = 0`, `specular = 0` — leaving specular at its default puts a
highlight on a toon face) and colour carried on `emissive`. It resembles the
PreviewSurface workaround and is not one: it is the glTF material model applied
to a glTF source, `alpha_mode` and `alpha_cutoff` are native so MASK is a real
cutout rather than an `ifgreater` emulated against a blended draw, and §7.2's
lit follow-up wants the same terminal.

Revisit this table when the runtime moves. If a later OpenUSD renders
`surface_unlit`, that is the better statement of intent and the graph should
change — which §4.3 already makes cheap.

### 5.2.2 Colour space

The base-colour texture is sRGB, declared as `colorSpace = "srgb_texture"`
metadata on the MaterialX image node's `file` attribute (`/preview` says the
same thing through `UsdUVTexture.sourceColorSpace`, which is an input rather
than metadata).

**Alpha is not decoded with the colour**, verified rather than assumed, because
a silently gamma-decoded alpha would weaken every cutout and blend in a way that
reads as an art problem: a texture whose alpha is 128/255 = 0.502 masks at a
0.45 cutoff and is discarded at 0.55, and the `srgb_texture` and `raw` renders
are identical for both MASK and BLEND. MaterialX's own
`srgb_texture_to_lin_rec709_color4` agrees — it converts the RGB and passes the
fourth channel through untouched.

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

### 5.5 A realization is chosen, not merged

A renderer picks **one** terminal. `UsdShadeMaterial::ComputeSurfaceSource`
walks the render contexts the delegate advertises, in order, and takes the first
that is connected; the universal terminal is consulted last. Storm advertises
`mtlx`, so from the moment `outputs:mtlx:surface` exists it is the network that
draws, and `/preview` becomes the path for consumers that do *not* understand
MaterialX.

Two consequences worth stating rather than rediscovering:

- **`/mtlx` is not a quiet addition to a material.** Authoring it changes what
  `usdview` shows, which is the point — and also why §8.2's regression target
  has to be re-checked against whichever realization the renderer selects
  rather than against the one that used to draw.
- **A broken MaterialX graph is not survivable.** There is no automatic
  fallback to `/preview`; the prim draws as a flat grey default surface, which
  is worse than an error because it reads as an untextured asset. That is what
  makes §8.1's "every `info:id` resolves in Sdr" check worth its cost: a
  misspelled `ND_*` id is silent at author time and grey at render time.

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

> **Shipped 2026-08-13** (unreleased; see the changelog's `[Unreleased]`
> entry). Every "done when" below is met. The one thing worth carrying
> forward: the digest diff is a *mechanically* verified path move — the
> committed digests, with the rename applied and nothing else, equal the
> regenerated ones for all 28 inputs including both corpus avatars — rather than
> a reviewed-by-eye one. Doing that check by hand across 44 baseline files would
> not have been believable.

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

> **Unlit shipped 2026-08-14** (unreleased; see the changelog's `[Unreleased]`
> entry). The node choice is not the one this section originally assumed — see
> §5.2.1, which is the measurement that changed it. Lit materials are the
> remaining half and keep `/preview` alone until then.
>
> Two things are worth carrying forward. The baseline diff is *additive*: no
> `/preview` shader, `/preview` graph output, or universal terminal moved or
> changed value across all 28 inputs, verified mechanically rather than by eye,
> so anything §7.1 froze still says what it said. And the visible win landed
> where §8.2 predicted — alpha-blended hair and eyelashes that `/preview` draws
> as opaque quads composite correctly through `/mtlx`.

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

Two additions Step 2 made, for failure modes the layers above do not reach:

- **Node-id resolution.** Every `info:id` inside `/mtlx` must resolve in the Sdr
  registry. A misspelled `ND_*` is accepted at author time, opens fine, and then
  fails to draw with no fallback (§5.5) — the registry is the same one the
  renderer consults, so asking it is the check. It skips where MaterialX node
  definitions are unavailable rather than failing (§11 q6a).
- **A fixture for the combination avatars are made of.** Unlit *and* textured
  *and* alpha-masked in one material, which nothing else covered: sampled alpha,
  the factor multiply, and the sampler wrap-mode translation are only reachable
  along that path.
- **A non-identity `KHR_texture_transform`, asserted against glTF's matrix.**
  Not against the other realization: the two nodes divide where the other
  multiplies, subtract where it adds, and disagree about whether to negate the
  rotation, so "both realizations agree" is a weaker claim than it sounds and
  "the triple was passed through unchanged" passes it. Every transform in the
  vendored corpus is the identity, where each wrong answer coincides with the
  right one, so this is a case the corpus structurally cannot cover.

### 8.2 First regression target — issue #119

The blown-out MToon/unlit investigation — front bangs clipping to near-white in
`usdview` — is the early regression target for both Step 1 and Step 2.
Base-color factor preservation has already landed, so the remaining candidates
must be isolated one at a time:

1. ~~source base-color texture/factor semantics~~ — landed;
2. ~~the current emissive/unlit approximation behavior (§2)~~ — **a real
   contributor, and fixed by Step 2 for unlit materials.** `/preview` draws the
   asset's alpha-blended hair as opaque quads, so the bang strands read as flat
   blocks over the forehead; `/mtlx` composites them. What remains blown out
   after that is the *shade* path, not the alpha path;
3. ~~MaterialX unlit behavior~~ — characterized in §5.2.1;
4. ~~color-management differences~~ — the sRGB decode is confined to RGB and
   leaves alpha alone in both realizations (§5.2.2). Render-context selection is
   now understood rather than suspected (§5.5);
5. asset-specific source data actually consumed by the VRM renderer — **the
   remaining candidate.** The asset's hair carries `_ShadeTexture` and
   `_ShadeColor`, which no realization reads yet; that is Step 3 plus the
   MToon follow-up in §7.2, not an alpha or colour-space problem.

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
| ✅ `/Asset/mtl` gains the `/preview` and `/mtlx` NodeGraph structure | [DESIGN_POLICY.md](DESIGN_POLICY.md) §4 | Step 1 |
| ✅ Confirm whether moving shader prims under `/preview` requires a schema contract bump — **it does not**, and the contract now says so rather than leaving it inferable (§11 q3) | [SCHEMA_CONTRACT.md](../../plugins/vrmSchema/docs/SCHEMA_CONTRACT.md) | Step 1 |
| `VrmMaterialAPI`, `VrmMToonAPI`, `VrmTextureInfoAPI` added to the typed API table, with their raw fallbacks | [SCHEMA_CONTRACT.md](../../plugins/vrmSchema/docs/SCHEMA_CONTRACT.md) | Step 3 |
| The MToon row (`vrm:shaderModel` + PreviewSurface fallback) restated in terms of the typed schemas | [SCHEMA_CONTRACT.md](../../plugins/vrmSchema/docs/SCHEMA_CONTRACT.md) | Step 3 |

---

## 11. Open questions

| # | Question | Blocks |
| --- | --- | --- |
| 1 | Exact `VRMC_materials_mtoon` field names against the VRM 1.0 specification. | Step 3 |
| ~~2~~ | ~~Render-context terminal naming for the pinned OpenUSD version.~~ **`outputs:mtlx:surface`** (settled 2026-08-14). `UsdShadeMaterial::CreateSurfaceOutput("mtlx")` authors exactly that on 26.08, and Storm advertises the `mtlx` context, so it is also the terminal that draws (§5.5). | — |
| ~~3~~ | ~~Does the Step 1 path move need a schema contract bump?~~ **No** (settled 2026-08-13). Contract v1 freezes control-prim paths under `/Asset/rig`, the material prim path, and `vrm:mtoon:raw` on that prim — none of which moved. The shader network below a material was never a contract path, and the contract now states that explicitly instead of leaving it to inference. | — |
| 4 | Restrict `VrmTextureInfoAPI` instance names via schema metadata, or allow arbitrary instances? | Step 3 |
| 5 | Does `vrm:shaderModel` remain once `VrmMToonAPI` exists, or become redundant? | Step 3 |
| ~~6~~ | ~~MaterialX availability across the supported runtime matrix.~~ **The plugin path is clean** (settled 2026-08-14): authoring `/mtlx` is `UsdShade` prim writing and nothing else — no MaterialX link, no `usdMtlx` dependency, no build-system change on any platform. MaterialX is needed only to *resolve* the node ids, which is why §8.1's Sdr check skips rather than fails where the definitions are absent. `MaterialXConfigAPI` applies from the schema registry without linking. | — |
| 6a | Confirm the non-Windows runtimes ship the MaterialX `libraries/` tree, so the Sdr check runs rather than skips in the Linux and macOS CI cells. | — |
| 7 | Is `/Asset/mtl/_shared` ever needed, or do per-material graphs suffice? | deferred |
| ~~8~~ | ~~Does `COLOR_0` participate in MToon appearance for the issue #119 asset?~~ **No** — the asset has no `COLOR_0` on any primitive (settled on the issue, 2026-08-12). Kept as a question for other assets, not this one. | — |
