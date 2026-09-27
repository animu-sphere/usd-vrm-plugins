# The Hydra imaging track

**Status:** 🚧 in progress — Steps I0–I3 shipped · **Target:** v0.10.0
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
- `hydra-toon` reads that data (Step I3). Its MAT-Q1 is answered for MToon
  by this plugin's `vrm` container, read from the terminal scene index:
  structural changes in the material Sprim's `Sync`, value-only changes by
  observing the scene index, because no `Sync` follows them (policy §28.3
  item 3). `usd-mmd-plugins` has fixed the same shape for its own schemas
  (planned `mmdImaging`).
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

### Step I3 — `hydra-toon` handshake ✅

- ✅ `hydra-toon` selects MToon from `vrmImaging`'s data and normalizes it into
  its own `ToonMaterial` (policy §8, §14): the probe stage's material and all
  12 of AliciaSolid's select MToon with `vrmSchema` and `vrmImaging` in the
  session, and none without them
  ([renderer report 03](https://github.com/animu-sphere/hydra-toon/blob/main/docs/reports/renderer/03-2026-09-26-material-sprim.md)). A value-only
  change reaches the same slot with no `Sync`
  ([renderer report 04](https://github.com/animu-sphere/hydra-toon/blob/main/docs/reports/renderer/04-2026-09-26-material-value-route.md); policy
  §28.3 item 3).
- ✅ This repository's part: the data it reads is the frozen locator hierarchy
  (policy §28.1), and the renderer has no VRM raw-JSON path, no importer
  dependency and no reading of `/preview` or `/mtlx` (policy §17.7). It
  selects from the `vrm` container alone and links nothing of this
  repository ([renderer report 02](https://github.com/animu-sphere/hydra-toon/blob/main/docs/reports/renderer/02-2026-09-26-mat-q1-material-inputs.md)).
- ✅ The same session in `testusdview`, composed as an `ost` Formation of the
  `lookdev` runtime, the packaged `vrmImaging` and the renderer, run by
  its own declared command
  ([renderer report 06](https://github.com/animu-sphere/hydra-toon/blob/main/docs/reports/renderer/06-2026-09-27-vrm-formation.md);
  [hydra-toon ost report 06](https://github.com/animu-sphere/hydra-toon/blob/main/docs/reports/ost/06-2026-09-27-v0.23.13-report-05-reverified.md)).
  It pins this workstation's `vrmImaging`: a published `lookdev` package is
  Step I5's.

**Done when** `VrmMToonAPI` → `vrmImaging` → `ToonMaterial::MToon` works in
`hydra-toon`.

### Step I4 — animated material semantics ✅

Unblocked: Product P5 Step 7 landed expression binds on the canonical slots
(2026-09-27), so an expression bake is time samples on the Material's
`inputs:vrm:*` (policy §10, §17.5).

- ✅ **An expression bake reaches Hydra as time.** The generated `expressions.vrm`
  fixture's colour bind, baked by `motion_retarget`, read through
  `UsdImagingStageSceneIndex` with `vrmImaging` in the session: the value at
  each baked time equals the bake's sample, and a time move dirties the
  field's `vrm/<group>/<field>` locator and, where a network reads the slot,
  that parameter — never `material`, and no locator of the mesh the material
  is bound to. That is policy §28.3's condition met: the values arrive as
  time samples, not as per-frame authored edits, so the whole-material
  dirtying stays lived with.
- ✅ **A live `hydra-toon` slot** (`hydra-toon`'s part, recorded as
  [renderer report 11](https://github.com/animu-sphere/hydra-toon/blob/main/docs/reports/renderer/11-2026-09-27-expression-bake.md)): the bake of `expressions_mtoon.vrm` — the same bind on an MToon
  material — played in its host session changes the MToon parameter slot,
  and the frame rebuilds no geometry, draw packet or pipeline.
  `expressions.vrm`'s `Face_Mat` is a glTF material, which `hydra-toon`
  draws with fallback PreviewSurface values until it reads a surface network,
  so its bake moves nothing there.

**Done when** a canonical colour change reaches a live `hydra-toon` material
slot without rebuilding geometry or shader pipelines.

**This repository's half, 2026-09-27.** The root build bakes
`expressive_clip.usda` onto `expressions.vrm` with `motion_retarget`
(`workspace_expression_bake`, a CTest fixture), and
`workspace_expression_bake_imaging` reads the layer it wrote through the stage
scene index with `vrmImaging` in the session:

- *the bake's samples* — `happy` drives `Face_Mat`'s emission, and at 0, 15
  and 30 `vrm/material/emissiveFactor` is black, half red and red (the clip's
  1.5 clamped). So is every network parameter that reads the slot: `/preview`'s
  `emissiveColor` and `/mtlx`'s `emissive`, both required, since a bake that
  reached one realization is one the other renderer never sees;
- *a time move* — dirties that `vrm` locator and those parameters, in each
  render context they appear in (`material//`, `material/__all/`,
  `material/mtlx/`), and nothing else of the material: never `material`, and
  no locator of `/Asset/geo/Face`, the mesh it is bound to. Every data source
  of both prims is read first, as a renderer that has drawn them has.

A bake that writes nothing on the slot fails, and so does a time-varying
attribute on the bound mesh, which dirties its own locator. Policy §28.3's
condition is met, and the whole-material dirtying stays lived with.

The same suite reads the bake of `expressions_mtoon.vrm`
(`workspace_expression_bake_mtoon`, `…_mtoon_imaging`): that fixture's
`Face_Mat` is MToon and its `happy` binds the same emission and no morph
target, so the bake moves a material slot and the skeleton and nothing of the
mesh. The samples and the dirtied set are the same, except that `/mtlx` reads
the slot as the `in2` of MToon's `withEmission` add instead of a surface's
`emissive`.

**`hydra-toon`'s half, 2026-09-27** ([renderer report 11](https://github.com/animu-sphere/hydra-toon/blob/main/docs/reports/renderer/11-2026-09-27-expression-bake.md)).
The bake of `expressions_mtoon.vrm`, flattened and played in `testusdview`
through a Formation of the `lookdev` runtime, `vrmImaging` and `toon`,
rewrites `Face_Mat`'s parameter slot once per time move, writes the joint
buffer for the skeleton, and uploads no points, topology, skin or texture and
creates no pipeline. At 15 the half-red emission is on screen (red over green
0.99 → 1.62); without `vrmImaging` the material is PreviewSurface and no move
writes it. The step's done-when is met.

### Step I5 — hardening ⬜

Everything below lands before v0.10.0 is tagged, except the published
package, which only the tag's run can close
([current.md](current.md#next-v0100--canonical-materials-reach-hydra-)).

- ✅ An `ost` plugin kind for a UsdImaging adapter: asked in
  [ost report 49](../reports/ost/49-2026-09-26-v0.23.8-no-plugin-kind-for-a-usdimaging-adapter.md),
  delivered in `ost` 0.23.9 as `usd-imaging`.
- ✅ A `usd`-profile runtime that can select that kind: asked in
  [ost report 50](../reports/ost/50-2026-09-26-v0.23.9-the-imaging-kind-arrives-and-the-usd-profile-cannot-select-it.md) (P1),
  delivered in `ost` 0.23.10 and adopted in
  [report 51](../reports/ost/51-2026-09-26-v0.23.10-vrmimaging-joins-the-product.md): a descriptor,
  `release_members`, the product (policy §19), and `usdvrm_baseline`'s
  session and frozen types.
- ⬜ **Plugin discovery in CI** (policy §16, §17.1). `ost`'s L2 constructs
  the adapters, but it runs only in the release lane: every workspace cell
  is `verify: test`. A suite in the workspace build shows `PlugRegistry`
  discovering `vrmImaging` from its `plugInfo.json` and UsdImaging
  constructing one adapter per schema, with no registration code.
- ⬜ **Invalidation regression tests** (policy §17.4). Its cases are
  `shadeColorFactor`, `baseColorFactor` and `outlineWidthFactor`, which the
  suites already assert, and a `matcap` texture edit, which they do not.
- ⬜ **A representative `.vrm` integration fixture** (policy §17.3). The
  committed `mtoon_vrm0.vrm` and `mtoon_vrm1.vrm`, imported by
  `usdVrmFileFormat` and read through `vrmImaging`, with selected values
  compared to what `tests/material_oracle.py` expects of the source.
- ⬜ **The scene-index suites against the packaged plugin.** `ost`'s L2
  proves the packaged adapters construct, not what they contribute. The
  release lane, where `ost plugin package` runs, reruns the suites with the
  package's `plugInfo.json` in place of the build tree's.
- ⬜ **An OpenUSD compatibility statement** in
  [SUPPORTED_CONFIGURATIONS.md](../reference/SUPPORTED_CONFIGURATIONS.md):
  OpenUSD 26.08, the only version measured (a second cell is Product P1's,
  blocked on a published runtime); the scene-index path only, since the
  legacy `UsdImagingDelegate` never calls an API-schema adapter; the runtime's
  usdImaging SDK; and `USDIMAGING_ENABLE_PLUGINS=0` dropping the plugin
  without a word (policy §27).
- ✅ **Numbered diagnostics** (policy §18). Proposed, and taken by the user
  on 2026-09-27:
  §18's candidates go into the existing `VRMxxx` catalog
  ([DIAGNOSTICS.md](../../plugins/usdVrmFileFormat/docs/DIAGNOSTICS.md)),
  not a new `VRMI` series, and only where something can observe them at run
  time. That is one new code, `VRM300` (WARNING, `validate`): a stage whose
  materials apply `Vrm*API` is validated in a session that has `vrmSchema`
  but not `vrmImaging`, so a Hydra renderer sees the realizations only.
  `validate_vrm.py` raises it when asked to check imaging, because a
  headless deployment may omit the plugin on purpose (policy §19). The other
  candidates are not diagnostics: a missing contribution and a missed
  invalidation are what the suites above assert, and a role the schema does
  not allow is `VRM224`, which `vrmSchema`'s validation already owns.
  **Landed 2026-09-27.** `validate_vrm.py --check-imaging` finds the
  adapters the way UsdImaging's registry does, from each plugin's declared
  `UsdImagingAPISchemaAdapter` types and their `apiSchemaName`, honouring
  `USDIMAGING_ENABLE_PLUGINS`. It reads the stage's schemas as authored, so a
  session without `vrmSchema` is caught too. One `VRM300` per stage names each
  schema left out and why. The rule is held by `usdvrm_validate`
  (`check_imaging_rules`). `workspace_validate_imaging` holds it against real
  sessions: with `vrmImaging` (quiet), without it, with the switch off, and
  without `vrmSchema` over the avatar flattened to `.usda`.
- ⬜ **A published `lookdev` package of `vrmImaging`**, so that a Formation
  (`hydra-toon`'s VRM session, Step I3) pins it from a registry rather than
  from one workstation. The release lane builds and publishes it
  (`lane: lookdev`, 2026-09-27) and has passed a dry run; no tag has run it.
  Closed when the v0.10.0 package is public on
  `ghcr.io/animu-sphere/usd-vrm-plugins` and pulls anonymously.

## 3. Non-goals

Humanoid, expression arbitration, look-at, spring bones, constraints,
`MotionPose` and VRMA playback are not imaging (policy §21). Renderer code in
any form is `hydra-toon`'s (policy §3.3).
