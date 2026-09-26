# The Hydra imaging track

**Status:** 🚧 in progress — Steps I0–I2 shipped · **Target:** unscheduled
([status table](README.md#status-at-a-glance)) ·
**Policy:** [imaging policy](../design/VRM_IMAGING_POLICY.md) §20

`vrmImaging`: the canonical material schemas — `VrmMaterialAPI`,
`VrmMToonAPI`, `VrmTextureInfoAPI:<role>` — exposed to Hydra as data on the
material prim, with invalidation as narrow as the Hydra data model permits.
What the plugin is, what it owns and what it may depend on is the policy's;
this document holds what is left to build. The steps I0–I5 are this track's
internal order, not a phase sequence.

## 1. Where it stands

- The schemas it reads shipped with Product P5 Steps 3–4
  ([material track](material-track.md)): every imported material carries
  them, VRM 0.x and 1.0 alike, as Material interface inputs `inputs:vrm:*`.
- All three canonical schemas reach Hydra (Steps I0–I2). Every field is
  under `vrm/material/<field>`, `vrm/mtoon/<field>` or
  `vrm/textureInfo/<role>/<field>` of the material prim, with per-field
  invalidation, under a locator hierarchy frozen in policy §28. A Hydra
  renderer can read every canonical value, and which image each role
  samples, as an asset path it resolves and loads itself.
- On every authored canonical edit UsdImaging also dirties the material's
  whole network, whatever reads it (policy §27 item 6). Step I1 measured
  that time never does, and decided to live with it (policy §28.3). Step I4
  inherits one condition: its high-frequency values arrive as time samples
  or through a scene index, not as per-frame authored edits.
- `hydra-toon` has not chosen its read path: its MAT-Q1 (Renderer Phase 1)
  lists an API-schema adapter per schema as a candidate, and `usd-mmd-plugins`
  has fixed that shape for its own schemas (planned `mmdImaging`).
- `vrmImaging` is an `ost` `usd-imaging` bundle and a member of the product
  (policy §19). `ost` 0.23.10 checks that the runtime has the usdImaging SDK
  and constructs both adapters from the package, but what they contribute
  is proved against the build tree only (Step I5).

## 2. Steps

### Step I0 — contract experiment ✅

- ✅ `plugins/vrmImaging/`: one UsdImaging API-schema adapter, for
  `VrmMToonAPI`, registered through `plugInfo.json` (policy §5, §16).
- ✅ A hand-authored stage, no `.vrm`: a `VrmMToonAPI` value read by a test
  consumer through `UsdImagingStageSceneIndex`, never by a stage query
  (policy §25).
- ✅ An attribute edit dirties only the expected locator of this plugin's
  contribution (policy §9); UsdImaging's own `material` locator beside it is
  not this plugin's (policy §27 item 6).
- ✅ The resulting scene-index prim inspected on OpenUSD 26.08 before any
  Hydra-facing name is frozen (policy §6.1, §27).

**Done when** `VrmMToonAPI.shadingToonyFactor` reaches the test consumer
through Hydra with no direct stage query, and an edit of it is observed as the
one locator it feeds.

**Shipped 2026-09-26.** Each condition, and where it is shown:

- *reaches the consumer through Hydra* — `vrmImaging_mtoon` reads
  `shadingToonyFactor` and every other field from the stage scene index's
  prim: authored values of each type the schema uses, fallbacks for the
  rest, and a field set equal to the schema's (19 fields);
- *an edit is the one locator* — three edits, each observed as its own
  `vrm/mtoon/<field>`. A mutation that dirties `vrm/mtoon` instead fails;
- *time* — a time-sampled field follows the scene index's time and is the
  only locator the time move dirties. A mutation that drops the time-varying
  locator fails;
- *the requirement* — `vrmImaging_mtoon_without_schema`: no contribution in
  a session without `vrmSchema`.

It went further than the done-when on purpose, and only in ways that cost no
frozen name: every `VrmMToonAPI` field rather than one, because the field set
is derived from the schema rather than listed. The Hydra path `hydra-toon`
reads is not measured, because it has none yet (Step I3).

### Step I1 — material core ✅

- ✅ `VrmMaterialAPI`: every field, with per-field invalidation (policy §6.2,
  §9). `VrmMToonAPI`'s fields already reach Hydra (Step I0).
- ✅ An answer to UsdImaging dirtying the whole network on every canonical
  edit (policy §27 item 6), or a recorded decision to live with it.
- ✅ The `vrm` locator hierarchy frozen, from what Step I0 measured.

**Done when** every non-texture canonical value has Hydra coverage.

**Shipped 2026-09-26.** Each condition, and where it is shown:

- *every non-texture canonical value* — `vrmImaging_material` reads all ten
  `VrmMaterialAPI` fields, authored and fallback, beside `VrmMToonAPI`'s
  nineteen (`vrmImaging_mtoon`), both under one `vrm` container. The one
  field without a schema fallback, `alphaMode`, reads as the schema's
  documented `OPAQUE` (policy §28.2). A mutation that drops it fails;
- *per-field invalidation* — four edits, each observed as its own
  `vrm/material/<field>`. A mutation that dirties `vrm/<group>` instead fails
  both suites;
- *the whole-material dirtying* — lived with (policy §28.3). An authored
  edit dirties `material`, pinned, whether a network reads the input or not.
  A time move never does: a time-sampled input no network reads dirties its
  `vrm` locator alone, and one the network reads dirties that and the one
  reading parameter;
- *the hierarchy* — one rule, `inputs:vrm:<group>:<name>` at
  `vrm/<group>/<name>`, frozen for all three schemas (policy §28.1), with no
  public header: a consumer spells the tokens, and the suites spell them
  literally.

Both adapters are one shared implementation that is given a schema name and
a group. It names no field except `alphaMode`.

### Step I2 — texture info ✅

- ✅ `VrmTextureInfoAPI:<role>`: all eleven allowed roles, with their
  multiple-apply instance identity preserved (policy §6.2, §17.6).
- ✅ Asset paths and sampling semantics exposed; no image loading (policy §11).

**Done when** a Hydra consumer can reconstruct the complete texture semantic
record from data sources.

**Shipped 2026-09-26.** Each condition, and where it is shown:

- *the complete record* — `vrmImaging_texture_info` reads every field of a
  role from `vrm/textureInfo/<role>/<field>`: `file` as an asset path, both
  authored and resolved, plus `texCoord`, `wrapS`, `wrapT`, `scale`,
  `strength` and `transform/{offset,rotation,scale}`. It reads them authored
  and at their fallbacks. An unresolvable image is delivered with an empty
  resolved path, and an unauthored `file` is absent, not empty
  (policy §29 items 3–4). A mutation that lists every field fails;
- *all eleven roles, identity preserved* — one material applies all eleven,
  each with its own image, and each role reads its own. The suite's list is
  checked against the schema's. A role the schema does not allow is not
  exposed. A mutation that drops that check fails, because USD does compose
  such a role (policy §29 item 2);
- *invalidation* — six edits, each observed as its own role field and
  nothing else of the contribution, including a nested `transform/offset`
  and a `file` appearing and disappearing. A time-sampled UV rotation
  dirties its one nested locator. Mutations that stop nesting, or that stop
  telling one role's properties from another's, fail;
- *no image loading* — nothing in the plugin reads a file. The resolved path
  is UsdImaging's own asset-path data source.

The shared implementation now binds an applied instance and nests a
namespaced field. It still names no field except `alphaMode`, and no role.

### Step I3 — `hydra-toon` handshake ⬜

- ⬜ `hydra-toon` selects MToon from `vrmImaging`'s data and normalizes it into
  its own `ToonMaterial` (policy §8, §14) — that repository's work, answered
  by its MAT-Q1.
- ⬜ This repository's part: the data it reads is stable, and the renderer has
  no VRM raw-JSON path, no importer dependency and no reading of `/preview` or
  `/mtlx` (policy §17.7).

**Done when** `VrmMToonAPI` → `vrmImaging` → `ToonMaterial::MToon` works in
`hydra-toon`.

### Step I4 — animated material semantics ⬜

- ⬜ Time-sampled and expression-driven canonical values through imaging
  (policy §10, §17.5), after Product P5 Step 7 lands expression binds on the
  canonical slots.

**Done when** a canonical colour change reaches a live `hydra-toon` material
slot without rebuilding geometry or shader pipelines.

### Step I5 — hardening ⬜

- ⬜ Plugin discovery in CI, an OpenUSD compatibility statement, invalidation
  regression tests and a representative `.vrm` integration fixture
  (policy §17.3).
- ✅ An `ost` plugin kind for a UsdImaging adapter: asked in
  [ost report 49](../reports/ost/49-2026-09-26-v0.23.8-no-plugin-kind-for-a-usdimaging-adapter.md),
  delivered in `ost` 0.23.9 as `usd-imaging`.
- ✅ A `usd`-profile runtime that can select that kind: asked in
  [ost report 50](../reports/ost/50-2026-09-26-v0.23.9-the-imaging-kind-arrives-and-the-usd-profile-cannot-select-it.md) (P1),
  delivered in `ost` 0.23.10 and adopted in
  [report 51](../reports/ost/51-2026-09-26-v0.23.10-vrmimaging-joins-the-product.md): a descriptor,
  `release_members`, the product (policy §19), and `usdvrm_baseline`'s
  session and frozen types.
- ⬜ The scene-index suites run against the packaged plugin, not only the
  build tree. `ost`'s L2 proves the packaged adapters construct, not what
  they contribute.
- ⬜ Diagnostics, numbered under the repository's policy (policy §18).

## 3. Non-goals

Humanoid, expression arbitration, look-at, spring bones, constraints,
`MotionPose` and VRMA playback are not imaging (policy §21). Renderer code in
any form is `hydra-toon`'s (policy §3.3).
