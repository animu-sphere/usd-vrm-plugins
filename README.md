# USD VRM Plugins

OpenUSD plugins for [VRM](https://vrm.dev/en/) avatars.

This repository currently ships one plugin:

| Plugin | Path | Role |
| --- | --- | --- |
| `usdVrm` | [plugins/usdVrm](plugins/usdVrm) | `SdfFileFormat`: imports `.vrm` (VRM 0.x / 1.0) as a normalized USD stage |

The repo is an [OpenStrata](https://github.com/animu-sphere/open-strata)
`usd-plugin-workspace`-style project (`openstrata.toml`) holding one or more
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
the design rationale. The importer build-out (geometry, materials, skeleton +
skinning, humanoid mapping, animation, front-direction bake, and typed `Vrm*API`
schemas across the whole rig) is implemented and verified against real VRM 1.0
avatars, plus reliability tooling: a standalone stage validator, a coded diagnostic
taxonomy, a compatibility report, and portable texture packaging.

**Import vs. evaluation vs. simulation.** The `usdVrm` plugin *authors data only*.
LookAt, node constraints, and spring bones are written as typed schema data; their
runtime **evaluation/simulation** is a separate layer (`execVrm`, roadmap P4) and is
never run by this importer. This keeps the import step pure and deterministic so the
runtime can be swapped without touching it.

Per-feature support status is in [docs/CAPABILITY_MATRIX.md](docs/CAPABILITY_MATRIX.md);
supported platforms, OpenUSD versions, and build requirements are in
[docs/SUPPORTED_CONFIGURATIONS.md](docs/SUPPORTED_CONFIGURATIONS.md). Release history
is in the [CHANGELOG](CHANGELOG.md) (the release version lives in the single-source
[VERSION](VERSION) file).

The forward direction — **doc/impl sync, a `v0.1.0` release, Windows runtime CI, and
a separate OpenExec runtime layer** — is set by the project
[design & development policy](docs/DESIGN_POLICY.md) and tracked, with live per-phase
status (**P0–P6**), in the [roadmap](docs/ROADMAP.md).

## Install

Binary bundles (per-target `tar.zst` + SHA-256 checksums + split debug
symbols) ship with each GitHub release; see
[docs/INSTALL.md](docs/INSTALL.md) for release-artifact, OpenStrata, and
from-source installation, verification, and troubleshooting.

## Build & test

### With OpenStrata (`ost`)

Requires `ost` 0.6+.

```sh
# One-time: adopt an OpenUSD install as the cy2026 runtime.
ost runtime pull cy2026 --profile usd --from-usd /path/to/openusd-install

# Build the bundle and run the L0-L6 verification pyramid.
ost plugin build plugins/usdVrm
ost plugin test  plugins/usdVrm

# Inspect a real avatar:
ost plugin run  plugins/usdVrm -- python plugins/usdVrm/tools/inspect_vrm.py avatar.vrm
ost plugin view plugins/usdVrm avatar.vrm        # usdview
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

### CI

CI is generated from the support matrix in `openstrata.ci.yaml`
(`ost ci generate github`). The PR lane
(`.github/workflows/ost-source-ci.yml`) builds from source on hosted
Windows / macOS arm64 / Linux runners against digest-pinned cy2026 runtimes
and runs `ost plugin test --up-to 5`; a weekly scheduled lane
(`ost-support-matrix.yml`) re-validates pinned runtime × plugin artifact
cells on a self-hosted real runtime. The hand-authored release workflow
(`release.yml`) is described below.

### Releasing

Pushing a tag `vX.Y.Z` (matching the repo `VERSION` file, with that
version's `CHANGELOG.md` section finalized) runs
`.github/workflows/release.yml`: it builds on all three OS cells, proves the
*packaged* artifact (packaged-artifact verification, clean-install smoke,
digest-reproducible packaging), and assembles a **draft** GitHub release —
per-target lean + debug bundles, a source archive, `SHA256SUMS`, and notes
rendered from `CHANGELOG.md` via `docs/RELEASE_NOTES_TEMPLATE.md`
(`scripts/make_release_notes.py`). Publishing the draft is a human decision.
Run the workflow manually (`workflow_dispatch`) for a dry run that creates
no release.

### Clean-install smoke

To verify the *packaged* plugin has no build-tree dependency, run:

```sh
python scripts/clean_install_smoke.py            # build + package + extract + smoke
python scripts/clean_install_smoke.py --skip-build   # reuse the current build
```

It packages the bundle with `ost`, extracts the artifact into a fresh directory
**outside** the repo, and runs the assertions in
`plugins/usdVrm/tests/clean_install_smoke.py` inside that extracted bundle's
runtime session: `.vrm` discovery served from the package, a textured fixture and
a corpus avatar open and validate, and an embedded texture resolves straight from
the `.vrm` container (no temp extraction). Needs `ost` + a validated `cy2026`
runtime.

## License

Original source and documentation: Apache-2.0 (see [LICENSE](LICENSE)).
Third-party components keep their own licenses; see
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). cgltf is **not** vendored — it
is fetched at configure time.

> Local test VRM avatars used during development are **not** part of this
> repository and are not redistributed here; mind their individual licenses.
