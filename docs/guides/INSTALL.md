# Installing usdVrm

How to get the `usdVrm` VRM plugins into an existing OpenUSD environment — from
a GitHub release, with [OpenStrata](https://github.com/animu-sphere/open-strata)
(`ost`), or from source. Check
[SUPPORTED_CONFIGURATIONS.md](../reference/SUPPORTED_CONFIGURATIONS.md) first: a
binary bundle only loads against a matching OpenUSD build (platform, OpenUSD
version, Python ABI — the bundle's `<target>` name spells all three out, e.g.
`cy2026-windows-x86_64-py313-usd`).

## From a GitHub release (binary bundle)

> **Install all three bundles.** Since [v0.2.0](../releases/v0.2.0.md) the
> workspace ships as three separate artifacts, and they are not optional
> extras: `usdVrmFileFormat` alone registers the `.vrm` file format and then
> **fails to open a stage**, because the typed `Vrm*API` schemas it applies live
> in `vrmSchema`. Embedded textures additionally need `usdVrmPackageResolver`.
> A single aggregate artifact is still pending upstream work (see the
> [roadmap](../roadmap/current.md)).
>
> Installing [v0.1.0](../releases/v0.1.0.md) instead? That release predates the
> split and ships one self-contained `usdVrm-0.1.0-<target>.tar.zst`; its asset
> names do not match the commands below.

Each release ships, per target, one lean bundle plus split debug symbols for
each of the three bundles, a manifest sidecar each, a source archive, and a
`SHA256SUMS` file.

1. **Download** all three bundles for your target and verify them:

   ```sh
   sha256sum --check --ignore-missing SHA256SUMS
   ```

2. **Extract** them anywhere (the bundles are relocatable):

   ```sh
   for b in vrmSchema usdVrmFileFormat usdVrmPackageResolver; do
     mkdir -p /opt/usdVrm/$b
     tar --zstd -xf $b-<version>-<target>.tar.zst -C /opt/usdVrm/$b
   done
   ```

3. **Register** all three with your OpenUSD environment:

   - `PXR_PLUGINPATH_NAME` → the three directories holding `plugInfo.json`,
     separated by your platform's path separator (`:` on Linux/macOS, `;` on
     Windows):

     ```text
     /opt/usdVrm/vrmSchema/plugin/resources/vrmSchema
     /opt/usdVrm/usdVrmFileFormat/plugin/resources/usdVrmFileFormat
     /opt/usdVrm/usdVrmPackageResolver/plugin/resources/usdVrmPackageResolver
     ```

   - **Windows only:** also add each `<extract-dir>/lib` to `PATH` so dependent
     DLLs resolve. (On Linux/macOS the plugin libraries are found through
     `plugInfo.json`'s relative `LibraryPath`; your OpenUSD libraries must
     already be loadable as usual.)

4. **Verify** — any `.vrm` now opens as a USD stage (the bundle ships license-
   clear fixtures under `<extract-dir>/tests/`):

   ```sh
   python -c "from pxr import Usd; s = Usd.Stage.Open('/opt/usdVrm/usdVrmFileFormat/tests/fixtures/textures.vrm'); print(s.GetDefaultPrim().GetPath())"
   ```

   If this fails to open the layer while the format itself is recognized,
   `vrmSchema` is missing from `PXR_PLUGINPATH_NAME` — see
   [Troubleshooting](#troubleshooting).

   Provenance of the binary (git commit / compiler / OpenUSD / build type /
   schema contract version) is stamped in
   `<extract-dir>/plugin/resources/<bundle>/buildInfo.json`, and
   `tools/vrm_report.py` prints it in its report's `build` section.

## With OpenStrata (`ost`)

`ost` resolves the runtime and session environment for you — no manual
environment variables. An extracted release bundle is a valid relocated bundle:

```sh
ost artifact import <dir-with-archive+manifest>   # or: ost artifact pull <oci-ref>
ost artifact extract <archive-digest> /opt/usdVrm
ost plugin run /opt/usdVrm -- python -c "from pxr import Usd; print(Usd.Stage.Open('avatar.vrm'))"
ost plugin view /opt/usdVrm avatar.vrm            # usdview
```

## From source

This is the supported way to get the **complete** workspace (importer + schemas
+ resolver). See the repo [README](../../README.md#build-and-test): `ost plugin
test --workspace` (OpenStrata) or plain CMake with `CMAKE_PREFIX_PATH` pointing
at an OpenUSD install. The clean-install smoke
(`python scripts/clean_install_smoke.py`) proves the packaged bundles work with
no build-tree reference.

## Troubleshooting

- **`.vrm` does not open / format not recognized** — `PXR_PLUGINPATH_NAME`
  does not point at the directory that contains `plugInfo.json` (step 3), or
  the bundle target does not match your OpenUSD build. Compare
  `buildInfo.json` (`openusdVersion`, `buildOs`) against your environment.
- **Format is recognized but the stage fails to open** (`Failed to open layer`,
  often `Used null prim`) — `vrmSchema` is not on `PXR_PLUGINPATH_NAME`. The
  importer authors prims with typed `Vrm*API` schemas; without the schema
  bundle registered, USD resolves the `.vrm` format and then cannot build the
  layer. Register all three directories from step 3.
- **Plugin found but the library fails to load** (Windows: silent, or
  `Plug` load errors) — `<extract-dir>/lib` is missing from `PATH`
  (Windows), or your OpenUSD came from a different compiler/Python than the
  bundle target.
- **Textures don't resolve** — embedded textures are served straight from the
  `.vrm` container by an `ArPackageResolver`; if a stage authored on another
  machine references `avatar.vrm[images/...]`, the original `.vrm` must sit at
  the referenced path. In v0.1.0 that resolver is compiled into the single
  bundle; from v0.2.0 it is the separate `usdVrmPackageResolver` bundle, which
  must be registered too.
