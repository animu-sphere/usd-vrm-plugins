# The MToon canonical-semantics track

**Status:** 🚧 in progress — Steps 1, 2 (unlit), 3 and 4 shipped · **Target:**
unscheduled ([status table](README.md#status-at-a-glance)) ·
**Policy:** [material policy](../design/MATERIAL_ARCHITECTURE_POLICY.md) §7

What is left of Product P5. The rule every step below serves is the policy's
§3: VRM/MToon *semantics* are stored on the material, and every rendering
realization — `/preview`, `/mtlx`, and a renderer that has never been
written — is generated from them and from nothing else.

## 1. The order, and why it changed

The plan through 2026-09-24 put the schemas **last**, so that the first
rendering improvements were not coupled to a schema redesign. Those
improvements have landed (Steps 1 and 2 below), and the order is now reversed
(2026-09-25): **the canonical schemas come next, before any further
realization work.**

The reasons are what Steps 1 and 2 left behind:

- Both realizations are generated from the importer's reading of the source
  glTF and `VRMC_materials_mtoon` JSON. Every approximation added that way —
  lit MaterialX, shade, rim, MatCap — is one more generator to re-point later,
  and one more place where a realization is the only statement of what the
  source said.
- MToon itself is still an opaque JSON blob (`vrm:mtoon:raw`). Nothing can read
  a shading-toony factor without re-parsing it, and the remaining candidate for
  the blown-out bangs of issue #119 — the hair's shade texture and shade
  colour — is exactly a value no realization can read until it is typed
  ([policy §8.2](../design/MATERIAL_ARCHITECTURE_POLICY.md#82-first-regression-target--issue-119)).
- Two consumers are waiting on something typed to read: expression
  material-colour binds (Step 7) and a native MToon renderer (Step 8).

## 2. Where it stands

- ✅ **Step 1 — PreviewSurface below `/preview`** (2026-08-13; policy §7.1).
- ✅ **Step 2 — `/mtlx` for unlit materials** (2026-08-14; policy §7.2 and
  §5.2.1). Lit materials carry `/preview` only; that half is now part of
  Step 6.
- ✅ **Step 3 — the canonical material schema contract** (2026-09-25; policy
  §6, §6.4.1, §7.3). `VrmMaterialAPI`, `VrmMToonAPI` and
  `VrmTextureInfoAPI` exist in `vrmSchema`, as Material interface inputs
  (`inputs:vrm:*`).
- ✅ **Step 4 — importer canonicalization** (2026-09-25; policy §6.6). Every
  imported material carries the Step 3 schemas, VRM 0.x and 1.0 alike; the
  raw block stays beside them, unchanged.
- **The realizations still read the source material.** `/preview` and
  `/mtlx` are generated from the importer's reading of the glTF core, not
  from the canonical attributes, until Steps 5–6. For a VRM 0.x MToon
  material the two can differ, since its canonical values come from
  `materialProperties` (below).
- **Expression colour binds already target a slot, not a shader input.** A
  VRM 1.0 `materialColorBinds` entry is typed on its expression prim as a
  relationship to the `UsdShadeMaterial` plus a VRM slot name (`color`,
  `shadeColor`, …) and a target value; `vrmRig`'s resolver resolves it, and
  `motion_retarget` warns and writes nothing, because the slot has no
  canonical attribute to land on. VRM 0.x `materialValues` binds are raw only
  (diagnostic `VRM150`), and VRM 1.0 `textureTransformBinds` raw only.
- **No renderer reads MToon.** `hydra-toon` does not exist yet.

## 3. Steps

Steps 3 → 4 → 5/6 are sequential; 5 and 6 are independent of each other.
Step 7 needs Step 3's slot table and open question 9, not Steps 5–6. Step 8
is another repository's.

### Step 3 — the canonical material schema contract ✅

`VrmMaterialAPI` (single-apply: base colour, emissive, alpha mode and cutoff,
double-sidedness), `VrmMToonAPI` (single-apply: the `VRMC_materials_mtoon`
semantics, grouped as policy §6.2 groups them) and `VrmTextureInfoAPI`
(multiple-apply, one instance per texture role, carrying the asset, the
texCoord set and the `KHR_texture_transform` offset / rotation / scale) in
`vrmSchema`. This is the schema, not the importer.

Decided in this step, from the policy's open questions: the field names
against the VRM 1.0 specification (§11 q1), the instance-name rule (q4), the
fate of `vrm:shaderModel` (q5), and whether the canonical attributes are
connectable (q9) — the last before any name is frozen, because it decides the
namespace.

**Done when:** the three schemas are generated and registered, their rows are
in the schema contract with their raw fallbacks (policy §10), the addition is
additive within contract v1, and a MToon parameter is read through the
generated API — not through JSON — on a hand-authored stage.

**Shipped 2026-09-25.** Each condition, and where it is shown:

- *generated and registered* — `vrmSchema` declares nine types, and
  `ost plugin test plugins/vrmSchema` registers them (L2);
- *rows and raw fallbacks* — the
  [schema contract](../../plugins/vrmSchema/docs/SCHEMA_CONTRACT.md#material-semantics-are-interface-inputs)'s
  typed-API table, raw-correspondence table and validator rules
  (`VRM223`–`VRM226`);
- *additive within v1* — the baseline diff is the schema, discovery, symbol
  and diagnostic catalogues; no stage digest moved;
- *read through the generated API* — `vrmschema_material_api` reads
  `shadingToonyFactor` and the rest of `tests/fixtures/basic.usda`'s
  `/Asset/mtl/Hair` through `UsdVrmMToonAPI`, `UsdVrmMaterialAPI` and
  `UsdVrmTextureInfoAPI`, and fails when the fixture's value changes.

The four questions: q1 — the policy's MToon field table matched the
specification, its texture list did not (`outlineWidthMultiply`); q4 — eleven
allowed roles; q5 — `vrm:shaderModel` stays; q9 — `inputs:vrm:*`, measured in
Storm (policy §6.4.1). What Step 4 inherits from q9: a realization connected
to an unauthored canonical input sees its own shader's default, not the
schema fallback, so the importer authors every value a graph connects to.

### Step 4 — importer canonicalization, VRM 0.x and 1.0 ✅

The importer authors the Step 3 schemas on every material. Both source
versions reach **the same** canonical fields: VRM 1.0 from the glTF material
core and `VRMC_materials_mtoon`, VRM 0.x from `materialProperties`. Version
spellings and legacy parameters are absorbed at the importer boundary, never
in the schema (policy §6.5). The raw block stays, unchanged.

The VRM 0.x mapping is not a rename for every field, so each conversion is
recorded with its fidelity class (Product P2's classification) rather than
invented at the call site (§11 q10).

**Done when:** every MToon field of both versions is readable through the
typed API on the vendored corpus; a VRM 0.x and a VRM 1.0 fixture describing
the same material author the same canonical values; semantic tests cover
source → attribute (policy §8.1); `vrm:mtoon:raw` is byte-identical to
before; and the baseline diff is additive.

**Shipped 2026-09-25.** q10 was decided as *UniVRM's migration, exactly*
(user's call): the conversion is UniVRM's `MigrationMToonMaterial`, its two
destructive choices included — a missing shade texture takes the lit texture,
and `rimLightingMixFactor` is always 1 — so a 0.x avatar and its UniVRM-migrated
1.0 file import to the same values. The per-field table, each row with its
fidelity class, is the
[schema contract's](../../plugins/vrmSchema/docs/SCHEMA_CONTRACT.md#vrm-0x-mtoon-normalizes-into-the-same-fields).
Each condition, and where it is shown:

- *readable on the vendored corpus* — `usdvrm_corpus_smoke` compares every
  material of both VRM 1.0 models (30, 23 of them MToon) with its source JSON,
  field for field, through `tests/material_oracle.py`. The vendored corpus has
  no VRM 0.x model; the 0.x half is the fixture pair below, and was also read
  on a local AliciaSolid (not committable);
- *same canonical values* — `mtoon_vrm0.vrm` is VRM 0.x `materialProperties`
  exercising every row of the table (render modes and queue ranking, the
  shading ramp, outline units, UV animation, tiling, shader defaults, a
  non-MToon material, a glTF core that disagrees on purpose);
  `mtoon_vrm1.vrm` is the same materials as UniVRM writes them in 1.0, every
  value a hand-worked literal. `check_mtoon_vrm0_matches_vrm1` requires every
  schema, attribute and texture to agree;
- *source → attribute* — `check_material_semantics` runs the oracle on every
  VRM 1.0 fixture. Mutating one 0.x conversion and one 1.0 rename each fails
  its check at the mutated field;
- *raw byte-identical* — all 36 `vrm:mtoon:raw` blocks (fixtures, corpus,
  AliciaSolid) compared before and after: identical;
- *additive* — the baseline diff adds `Vrm*API` entries to `apiSchemas`, the
  `inputs:vrm:*` values and the canonical texture assets, and nothing else; no
  `/preview` or `/mtlx` value moved.

`package_vrm.py` now packages a canonical texture file like a realization's:
it is what Steps 5–6 regenerate from, so it has to travel with the package.

### Step 5 — `/preview` generated from canonical semantics ⬜

The PreviewSurface generator's input becomes exactly the canonical field set:
a function from a material's canonical attributes to a graph, which the
importer calls and which can equally be run on a stage. No PreviewSurface
input is storage (policy §6.1), and MToon-only semantics are not squeezed in
(§5.1).

**Done when:** deleting `/preview` and regenerating it from the canonical
attributes alone reproduces the imported graph; no generator reads source
JSON or an extension block; and the baseline diff shows no value change —
with one expected exception, which is the point: a VRM 0.x MToon material
whose glTF core disagrees with its `materialProperties` (`mtoon_vrm0.vrm`, by
construction) moves to the canonical value.

### Step 6 — `/mtlx` generated from canonical semantics ⬜

The same re-pointing for MaterialX, and the realization's growth, in this
order:

1. the shipped unlit graph, re-pointed with no value change;
2. lit glTF PBR materials through the same `gltf_pbr` terminal, so every
   material carries both realizations (the former "Step 2 (lit)");
3. portable MToon approximations: shade colour and the shade-multiply
   texture, shading shift and toony as a toon transition, rim, MatCap.

Standard MaterialX nodes only; a custom node definition needs an
interoperability case (policy §5.2). Outline, screen-space width, render
ordering and MToon's transparency modes stay out — they are canonical
semantics no portable graph reproduces (§6.2). §5.2.1's table is re-measured
whenever the runtime moves.

**Done when:** every material carries `/mtlx`; deleting and regenerating it
from the canonical attributes reproduces it; every `info:id` resolves in Sdr;
and a focused visual regression covers MToon hair with its shade texture —
issue #119's asset is the first target — plus rim and MatCap once
implemented. PreviewSurface and MaterialX are held to separate fidelity
criteria and never to each other (§5.4).

### Step 7 — expression material binds onto canonical slots ⬜

An expression changes a material *semantic*, never a realization's shader
input (policy §6.6). The work:

- One table from VRM slot to canonical attribute — `color` →
  `VrmMaterialAPI` base colour, `emissionColor` → emissive, `shadeColor`,
  `matcapColor`, `rimColor`, `outlineColor` → their `VrmMToonAPI` factors —
  owned by the schema contract and shared by every writer and evaluator.
- VRM 0.x `materialValues` mapped onto the same slots, narrowing `VRM150` to
  what has no slot.
- `motion_retarget` authors resolved colours onto the canonical attributes
  instead of warning, and the realizations follow them (q9).
- `textureTransformBinds`: in or out of scope (q11).

The OpenExec half — `vrm.computeMaterialColorOverrides` — is the
[`ExecIr` track](execir-track.md)'s P1-1 and reads the same table.

**Done when:** a baked clip that drives a colour changes the canonical
attribute; the change shows through both `/preview` and `/mtlx` without a
re-bake; nothing writes below either graph; and `motion_retarget`'s "not
written" warning is gone.

### Step 8 — `hydra-toon` consumes the contract (owned elsewhere)

`hydra-toon`, a planned sibling repository not yet created, reads
`VrmMToonAPI` and `VrmTextureInfoAPI` directly and owns the full
realization: toon lighting, rim, MatCap, outline, UV animation, MToon
transparency and render ordering, first on Vulkan and WebGPU (policy §5.3).
The contract between the two repositories is the USD schema and nothing
else; neither links the other.

This repository's part is keeping Steps 3–4's contract stable. The work
itself leaves this roadmap once `hydra-toon` exists; it is listed so the
boundary is stated where the steps are. Per-renderer conformance images and
transparent-sorting behaviour go with it.

## 4. Contract changes this track requires

Owned by [policy §10](../design/MATERIAL_ARCHITECTURE_POLICY.md#10-contract-changes-this-policy-requires),
which lists each change with its owning document and the step that needs it.
