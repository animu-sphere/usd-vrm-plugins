# Installing usdVrm

How to get the `usdVrm` OpenUSD plugin (a `.vrm` file-format importer) into an
existing OpenUSD environment — from a GitHub release, with
[OpenStrata](https://github.com/animu-sphere/open-strata) (`ost`), or from
source. Check [SUPPORTED_CONFIGURATIONS.md](SUPPORTED_CONFIGURATIONS.md) first:
a binary bundle only loads against a matching OpenUSD build (platform, OpenUSD
version, Python ABI — the bundle's `<target>` name spells all three out, e.g.
`cy2026-windows-x86_64-py313-usd`).

## From a GitHub release (binary bundle)

Each release ships one lean bundle per target plus split debug symbols, a
source archive, and a `SHA256SUMS` file.

1. **Download** `usdVrmFileFormat-<version>-<target>.tar.zst` for your target and verify
   it:

   ```sh
   sha256sum --check --ignore-missing SHA256SUMS
   ```

2. **Extract** it anywhere (the bundle is relocatable):

   ```sh
   tar --zstd -xf usdVrmFileFormat-<version>-<target>.tar.zst -C /opt/usdVrm
   ```

3. **Register** the plugin with your OpenUSD environment:

   - `PXR_PLUGINPATH_NAME` → the directory holding `plugInfo.json`:
     `<extract-dir>/plugin/resources/usdVrmFileFormat`
   - **Windows only:** also add `<extract-dir>/lib` to `PATH` so dependent
     DLLs resolve. (On Linux/macOS the plugin library is found through
     `plugInfo.json`'s relative `LibraryPath`; your OpenUSD libraries must
     already be loadable as usual.)

4. **Verify** — any `.vrm` now opens as a USD stage (the bundle ships license-
   clear fixtures under `<extract-dir>/tests/`):

   ```sh
   python -c "from pxr import Usd; s = Usd.Stage.Open('<extract-dir>/tests/fixtures/textures.vrm'); print(s.GetDefaultPrim().GetPath())"
   ```

   Provenance of the binary (git commit / compiler / OpenUSD / build type /
   schema contract version) is stamped in
   `<extract-dir>/plugin/resources/usdVrmFileFormat/buildInfo.json`, and
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

See the repo [README](../README.md#build--test): `ost plugin build
plugins/usdVrmFileFormat` (OpenStrata) or plain CMake with `CMAKE_PREFIX_PATH` pointing
at an OpenUSD install. The clean-install smoke
(`python scripts/clean_install_smoke.py`) proves a packaged build works with no
build-tree reference.

## Troubleshooting

- **`.vrm` does not open / format not recognized** — `PXR_PLUGINPATH_NAME`
  does not point at the directory that contains `plugInfo.json` (step 3), or
  the bundle target does not match your OpenUSD build. Compare
  `buildInfo.json` (`openusdVersion`, `buildOs`) against your environment.
- **Plugin found but the library fails to load** (Windows: silent, or
  `Plug` load errors) — `<extract-dir>/lib` is missing from `PATH`
  (Windows), or your OpenUSD came from a different compiler/Python than the
  bundle target.
- **Textures don't resolve** — embedded textures are served straight from the
  `.vrm` container by the bundled `ArPackageResolver`; if a stage authored on
  another machine references `avatar.vrm[images/...]`, the original `.vrm`
  must sit at the referenced path.
