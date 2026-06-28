# USD VRM Plugins

OpenUSD plugins for [VRM](https://vrm.dev/en/) avatars.

This repository currently ships one plugin:

| Plugin | Path | Role |
| --- | --- | --- |
| `usdVrm` | [plugins/usdVrm](plugins/usdVrm) | `SdfFileFormat`: imports `.vrm` (VRM 0.x / 1.0) as a normalized USD stage |

The repo is an [OpenStrata](https://github.com/animu-sphere/open-strata) project
(`openstrata.toml`) holding one or more
self-contained plugin **bundles** under `plugins/`. It is **dual-mode**: you can
build and verify it with the `ost` CLI, or with plain CMake against any OpenUSD
install.

## What the importer produces

`.vrm` is read as a GLB container (via [cgltf](https://github.com/jkuhlmann/cgltf),
fetched at configure time) and normalized — VRM 0.x and 1.0 differences are
absorbed into a canonical model before any USD is authored — into:

```
/Asset                     SkelRoot (or Xform when there is no skeleton), kind=component
  customData.vrm.*         sourceFormat / sourceVersion / specVersion / meta / rawExtension
  geo/                     Scope of UsdGeomMesh (one per glTF primitive)
    <Mesh>                 points/normals/st, material binding; skel binding when
                           skinned, else the glTF node transform as xformOp
  mtl/                     Scope of UsdShadeMaterial (UsdPreviewSurface)
  skel/Skeleton            single UsdSkelSkeleton unified across all glTF skins
                           (bind transforms from the inverse bind matrices)
  rig/Humanoid             vrm:skeleton rel + vrm:humanBones:<bone> joint tokens
```

See [plugins/usdVrm/README.md](plugins/usdVrm/README.md) for plugin specifics and
the design rationale, and the original implementation plan for the full Phase
roadmap. Phase 1 (geometry, materials, skeleton + skinning, humanoid mapping,
VRM provenance) is implemented and verified against real VRM 1.0 avatars.

## Build & test

### With OpenStrata (`ost`)

Requires `ost` 0.3+.

```sh
# One-time: adopt an OpenUSD install as the cy2026 runtime.
ost runtime pull cy2026 --profile usd --from-usd /path/to/openusd-install

# Build the bundle and run the L0-L6 verification pyramid.
ost plugin build plugins/usdVrm
ost plugin test  plugins/usdVrm

# Inspect a real avatar:
ost plugin run  plugins/usdVrm -- python plugins/usdVrm/tools/inspect_vrm.py avatar.vrm
ost plugin view plugins/usdVrm -- avatar.vrm     # usdview
```

### With plain CMake (no OpenStrata)

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/openusd-install
cmake --build build --config Release
ctest --test-dir build -C Release
```

The built `libUsdVrmFileFormat.{dll,so,dylib}` lands in `plugins/usdVrm/lib/`;
add `plugins/usdVrm/plugin/resources/usdVrm` to `PXR_PLUGINPATH_NAME` and the
`lib/` dir to your dynamic-loader path to use it.

## License

Original source and documentation: Apache-2.0 (see [LICENSE](LICENSE)).
Third-party components keep their own licenses; see
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). cgltf is **not** vendored — it
is fetched at configure time.

> Local test VRM avatars used during development are **not** part of this
> repository and are not redistributed here; mind their individual licenses.
