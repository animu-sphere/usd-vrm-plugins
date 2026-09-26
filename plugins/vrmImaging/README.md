# vrmImaging

The canonical VRM material schemas made visible to Hydra: a UsdImaging
API-schema adapter per applied schema, contributing data sources to the Hydra
material prim and turning a changed USD property into the one locator it feeds.
The policy is [VRM_IMAGING_POLICY.md](../../docs/design/VRM_IMAGING_POLICY.md);
the open work is the [imaging track](../../docs/roadmap/imaging-track.md).

It describes what a VRM material says. How that is drawn is a renderer's
decision — `hydra-toon`'s for MToon — and nothing here is shader code, a GPU
resource, an image loader or a pipeline choice.

## What exists: Steps I0–I1

| Adapter | Applied schema | Contribution on the material prim |
| --- | --- | --- |
| `UsdVrmImagingMaterialAPIAdapter` | `VrmMaterialAPI` | `vrm/material/<field>`: one sampled data source per field |
| `UsdVrmImagingMToonAPIAdapter` | `VrmMToonAPI` | `vrm/mtoon/<field>`: one sampled data source per field |

Both are `UsdVrmImagingSchemaAdapter` given a schema name and a group.
`VrmTextureInfoAPI` is Step I2.

- **Field names are the schema's.** Every property of the registered
  definition named `inputs:vrm:<group>:<field>` becomes `vrm/<group>/<field>`
  (policy §28.1). No field is named in the code, and a property authored under
  that prefix that the schema does not define is never exposed.
- **One `vrm` container.** UsdImaging overlays the adapters' contributions, so
  a material with both schemas has one `vrm` holding `material` and `mtoon`.
- **Values are resolved.** An unauthored field carries its schema fallback, so
  a consumer never restates the schema's defaults. `alphaMode`, which the
  schema cannot give a fallback, reads as its documented `OPAQUE`
  (policy §28.2).
- **Values are sampled, not copied.** A time-sampled attribute follows the
  scene index's time, and moving the time dirties that field alone.
- **Invalidation is per field.** An edit of `inputs:vrm:mtoon:<field>` dirties
  `vrm/mtoon/<field>` and nothing else of the contribution.
- **The material network is left alone.** The contribution sits beside
  UsdImaging's `material` container, never inside it, so `/preview` and
  `/mtlx` reach Hydra as they always have.

The `vrm` locator hierarchy is **frozen** (policy §28.1). There is no public
header: a Hydra consumer spells the tokens itself and links nothing of this
repository.

### What it does not control

On OpenUSD 26.08, UsdImaging's own material adapter dirties the **whole**
`material` locator whenever any interface input of a Material is edited,
whether or not a network reads it — and every canonical attribute is an
interface input (`inputs:vrm:*`, material policy §6.4.1). So a Hydra consumer
sees `material` dirty beside `vrm/<group>/<field>` on every authored canonical
edit. Moving time never does that: a time-sampled canonical value dirties its
`vrm` locator and, if a network reads it, that one parameter. The decision to
live with it is policy §28.3. The suites pin both, so a runtime that narrows
the edit case is noticed.

## Requirements

- **`vrmSchema` registered in the session.** Nothing of it is linked: the field
  set is read from the schema registry by the schema's name. Without it,
  UsdImaging never asks the adapter — no prim's definition includes an API
  schema the registry does not know — and there is no `vrm` contribution at
  all (`vrmImaging_mtoon_without_schema`).
- **The stage scene index.** UsdImaging consults API-schema adapters only when
  it populates through `UsdImagingStageSceneIndex`; the legacy
  `UsdImagingDelegate` never does.
- **External UsdImaging plugins enabled.** `USDIMAGING_ENABLE_PLUGINS=0` drops
  every adapter not marked internal, this one included.

## Packaging

An `ost` bundle of kind `usd-imaging`, and a member of the product. Its
`provides` names one key per adapter, `usd-imaging:VrmMaterialAPI` and
`usd-imaging:VrmMToonAPI`, and `ost` holds that list to `plugInfo.json` (L0).
It requires `vrmSchema` as a bundle and the usdImaging SDK of the runtime
(L1). Its L2 builds a native checker that registers the plugin and constructs
both adapters, from the build tree and from the package alike. It needs no
viewer profile: `ost` 0.23.10 lets the `usd` profile select the kind
([report 51](../../docs/reports/ost/51-2026-09-26-v0.23.10-vrmimaging-joins-the-product.md)).

L2 proves the packaged adapters construct. What they contribute, and what they
dirty, is proved by the suites below, against the build tree only.

## Tests

| CTest | What it measures |
| --- | --- |
| `vrmImaging_mtoon` | discovery; authored and fallback values under `vrm/mtoon`; the field set equal to the schema's; per-field invalidation; a time-sampled field following time |
| `vrmImaging_mtoon_without_schema` | no contribution in a session without `vrmSchema` |
| `vrmImaging_material` | `vrm/material` beside `vrm/mtoon` in one container; authored, fallback and documented-default values; per-field invalidation; time never dirtying the whole `material`, with and without a network reading the input |

The suites open hand-authored stages (`tests/fixtures/*.usda`) and never a
`.vrm`, so what reaches Hydra is the schema contract and not the importer. They
do not link the plugin: discovery goes through `PXR_PLUGINPATH_NAME` and the
staged `plugInfo.json`.
