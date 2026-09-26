# vrmImaging

The canonical VRM material schemas made visible to Hydra: a UsdImaging
API-schema adapter per applied schema, contributing data sources to the Hydra
material prim and turning a changed USD property into the one locator it feeds.
The policy is [VRM_IMAGING_POLICY.md](../../docs/design/VRM_IMAGING_POLICY.md);
the open work is the [imaging track](../../docs/roadmap/imaging-track.md).

It describes what a VRM material says. How that is drawn is a renderer's
decision — `hydra-toon`'s for MToon — and nothing here is shader code, a GPU
resource, an image loader or a pipeline choice.

## What exists: Step I0, `VrmMToonAPI`

| Adapter | Applied schema | Contribution on the material prim |
| --- | --- | --- |
| `UsdVrmImagingMToonAPIAdapter` | `VrmMToonAPI` | `vrm/mtoon/<field>`: one sampled data source per field |

- **Field names are the schema's.** Every property of the registered
  `VrmMToonAPI` definition named `inputs:vrm:mtoon:<field>` becomes `<field>`.
  No field is named in the code, and a property authored under that prefix
  that the schema does not define is never exposed.
- **Values are resolved.** An unauthored field carries its schema fallback, so
  a consumer never restates the schema's defaults.
- **Values are sampled, not copied.** A time-sampled attribute follows the
  scene index's time, and moving the time dirties that field alone.
- **Invalidation is per field.** An edit of `inputs:vrm:mtoon:<field>` dirties
  `vrm/mtoon/<field>` and nothing else of the contribution.
- **The material network is left alone.** The contribution sits beside
  UsdImaging's `material` container, never inside it, so `/preview` and
  `/mtlx` reach Hydra as they always have.

The `vrm` locator hierarchy is **not frozen**: it is the Step I0 experiment's
shape, and there is no public header for it until Step I1 settles it.

### What it does not control

On OpenUSD 26.08, UsdImaging's own material adapter dirties the **whole**
`material` locator whenever any interface input of a Material changes, whether
or not a network reads it — and every canonical attribute is an interface input
(`inputs:vrm:*`, material policy §6.4.1). So a Hydra consumer sees `material`
dirty beside `vrm/mtoon/<field>` on every canonical edit. The suite pins that,
so a runtime that narrows it is noticed.

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

## Not an `ost` bundle yet

`ost` 0.23.8 has no plugin kind for a UsdImaging adapter, so there is no
`openstrata.plugin.yaml`: the root `CMakeLists.txt` adds this directory by name,
the root build compiles and tests it, and no package carries it. Packaging is
Step I5 and waits on that kind.

## Tests

| CTest | What it measures |
| --- | --- |
| `vrmImaging_mtoon` | discovery; authored and fallback values under `vrm/mtoon`; the field set equal to the schema's; per-field invalidation; a time-sampled field following time |
| `vrmImaging_mtoon_without_schema` | no contribution in a session without `vrmSchema` |

The suite opens a hand-authored stage (`tests/fixtures/mtoon_materials.usda`)
and never a `.vrm`, so what reaches Hydra is the schema contract and not the
importer. It does not link the plugin: discovery goes through
`PXR_PLUGINPATH_NAME` and the staged `plugInfo.json`.
