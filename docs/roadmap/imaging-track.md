# The Hydra imaging track

**Status:** 🚧 in progress — Step I0 shipped · **Target:** unscheduled
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
- `VrmMToonAPI` reaches Hydra (Step I0): every field under
  `vrm/mtoon/<field>` of the material prim, with per-field invalidation.
  `VrmMaterialAPI` and `VrmTextureInfoAPI` do not yet. Without them a Hydra
  renderer sees only the `/preview` or `/mtlx` network, and no realization
  connects to a canonical input.
- On every canonical edit UsdImaging also dirties the material's whole
  network, whatever reads it (policy §27 item 6). That is outside this
  plugin and open for Step I4.
- `hydra-toon` has not chosen its read path: its MAT-Q1 (Renderer Phase 1)
  lists an API-schema adapter per schema as a candidate, and `usd-mmd-plugins`
  has fixed that shape for its own schemas (planned `mmdImaging`).
- `ost` 0.23.8 has no plugin kind for a UsdImaging adapter, so the plugin can
  be built and tested by the root build but not packaged as a bundle (policy
  §19; Step I5).

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

### Step I1 — material core ⬜

- ⬜ `VrmMaterialAPI`: every field, with per-field invalidation (policy §6.2,
  §9). `VrmMToonAPI`'s fields already reach Hydra (Step I0).
- ⬜ An answer to UsdImaging dirtying the whole network on every canonical
  edit (policy §27 item 6), or a recorded decision to live with it.
- ⬜ The `vrm` locator hierarchy frozen, from what Step I0 measured.

**Done when** every non-texture canonical value has Hydra coverage.

### Step I2 — texture info ⬜

- ⬜ `VrmTextureInfoAPI:<role>`: all eleven allowed roles, with their
  multiple-apply instance identity preserved (policy §6.2, §17.6).
- ⬜ Asset paths and sampling semantics exposed; no image loading (policy §11).

**Done when** a Hydra consumer can reconstruct the complete texture semantic
record from data sources.

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
- ⬜ An `ost` plugin kind for a UsdImaging adapter, then a descriptor,
  `release_members` and the product (policy §19).
- ⬜ Diagnostics, numbered under the repository's policy (policy §18).

## 3. Non-goals

Humanoid, expression arbitration, look-at, spring bones, constraints,
`MotionPose` and VRMA playback are not imaging (policy §21). Renderer code in
any form is `hydra-toon`'s (policy §3.3).
