# Installing usdVrm

How to get the `usdVrm` VRM plugins into an existing OpenUSD environment — from
a GitHub release, with [OpenStrata](https://github.com/animu-sphere/open-strata)
(`ost`), or from source. Check
[SUPPORTED_CONFIGURATIONS.md](../reference/SUPPORTED_CONFIGURATIONS.md) first: a
binary bundle only loads against a matching OpenUSD build (platform, OpenUSD
version, Python ABI — the bundle's `<target>` name spells all three out, e.g.
`cy2026-windows-x86_64-py313-usd`).

## From a GitHub release (binary bundle)

> **Which assets exist today.** The only published release is
> [v0.1.0](../releases/v0.1.0.md), which predates the workspace split: it ships
> `usdVrm-0.1.0-<target>.tar.zst`, a **single self-contained bundle** with the
> typed schemas and the package resolver compiled in. The commands below use
> those names. The next release renames the importer bundle to
> `usdVrmFileFormat-<version>-<target>.tar.zst` and splits the schemas and
> resolver into their own bundles — at which point the importer bundle alone is
> no longer sufficient, and [building from source](#from-source) is the
> supported way to get the complete workspace until the aggregate product
> artifact lands (see the [roadmap](../roadmap/current.md)).

Each release ships one lean bundle per target plus split debug symbols, a
source archive, and a `SHA256SUMS` file.

1. **Download** `usdVrm-<version>-<target>.tar.zst` for your target and verify
   it:

   ```sh
   sha256sum --check --ignore-missing SHA256SUMS
   ```

2. **Extract** it anywhere (the bundle is relocatable):

   ```sh
   tar --zstd -xf usdVrm-<version>-<target>.tar.zst -C /opt/usdVrm
   ```

3. **Register** the plugin with your OpenUSD environment:

   - `PXR_PLUGINPATH_NAME` → the directory holding `plugInfo.json`:
     `<extract-dir>/plugin/resources/usdVrm`
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
- **Plugin found but the library fails to load** (Windows: silent, or
  `Plug` load errors) — `<extract-dir>/lib` is missing from `PATH`
  (Windows), or your OpenUSD came from a different compiler/Python than the
  bundle target.
- **Textures don't resolve** — embedded textures are served straight from the
  `.vrm` container by an `ArPackageResolver`; if a stage authored on another
  machine references `avatar.vrm[images/...]`, the original `.vrm` must sit at
  the referenced path. In v0.1.0 that resolver is compiled into the single
  bundle; from the next release it is the separate `usdVrmPackageResolver`
  bundle, which must be registered too.
