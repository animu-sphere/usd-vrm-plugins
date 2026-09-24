# vrmSchema — typed VRM API schemas for OpenUSD

The schema bundle of the usdVrm plugin workspace
([docs/architecture/WORKSPACE.md](../../docs/architecture/WORKSPACE.md)). It
owns the authored-data contract: the six single-apply API schemas the importer
stamps onto `/Asset/rig/*` control prims —

| Schema | Applied to |
| --- | --- |
| `VrmHumanoidAPI` | humanoid bone → skeleton joint mapping |
| `VrmExpressionAPI` | expression (blend-shape / material) controls |
| `VrmLookAtAPI` | look-at parameters |
| `VrmSpringBoneAPI` | spring-bone chains |
| `VrmColliderAPI` | spring-bone colliders |
| `VrmConstraintAPI` | node constraints |

— and the three canonical material schemas applied to each `/Asset/mtl/<name>`
`UsdShadeMaterial`, whose properties are Material interface inputs
(`inputs:vrm:*`) so every rendering realization reads them through a UsdShade
connection ([material policy](../../docs/design/MATERIAL_ARCHITECTURE_POLICY.md)
§6):

| Schema | Carries |
| --- | --- |
| `VrmMaterialAPI` | the glTF material core: base colour, metallic/roughness, emissive, alpha, double-sidedness, unlit |
| `VrmMToonAPI` | the `VRMC_materials_mtoon` 1.0 model, field names verbatim |
| `VrmTextureInfoAPI:<role>` | one texture by role (multiple-apply, eleven allowed roles): asset, UV set, sampler wrap, `KHR_texture_transform` |

plus the schema tokens and the **schema contract version** (see
[docs/SCHEMA_CONTRACT.md](docs/SCHEMA_CONTRACT.md); tracked in the manifest as
`schema.contract`, independent of `plugin.version`).

Split out of the `usdVrm` file-format bundle (workspace Phase 1). This bundle
depends on nothing but OpenUSD — never on the importer, resolver, or exec
bundles (WORKSPACE.md §2).

## Layout

```
openstrata.plugin.yaml            bundle contract (identity, schema.contract, provides, tests)
CMakeLists.txt                    builds libvrmSchema.{dll,dylib,so} into lib/ + CMake package export
cmake/OpenStrataPlugin.cmake      self-contained scaffold helpers (ost template)
cmake/vrmSchemaConfig.cmake.in    find_package(vrmSchema CONFIG) package config
schema/schema.usda                schema source (usdGenSchema input)
src/vrmSchema/                    committed usdGenSchema output, compiled into libvrmSchema
plugin/resources/vrmSchema/       plugInfo.json(.in) + generatedSchema.usda (USD registration)
tools/generate_schema.py          regenerates the committed output from schema/schema.usda
tests/                            schema-only tests (no .vrm importer involved)
tests/consumer/                   installed-package consumer smoke (find_package + link)
```

## Consuming

```cmake
find_package(vrmSchema CONFIG REQUIRED)
target_link_libraries(myTarget PRIVATE vrmSchema::vrmSchema)
```

```cpp
#include <vrmSchema/vrmHumanoidAPI.h>
UsdVrmHumanoidAPI api = UsdVrmHumanoidAPI::Apply(prim);
```

The C++ class names keep the pre-split `UsdVrm*` spelling (schema identifier
`Vrm*API`, tokens class `UsdVrmTokens`, export macro `USDVRM_API`) — the split
must not change the public C++/authored surface (WORKSPACE.md §7).

## Regenerating the schema code

Only when `schema/schema.usda` changes:

```sh
python plugins/vrmSchema/tools/generate_schema.py --usd-root <openusd-install>
```

`ost plugin build plugins/vrmSchema` regenerates against the resolved runtime
automatically; the committed sources are the plain-CMake fallback.
