# USD VRM Plugins

OpenUSD plugins for [VRM](https://vrm.dev/en/) avatars.

This repository is an OpenUSD plugin **workspace**: it separates schema
definitions, file-format import, package resolution, and shared GLB container
parsing into independently buildable, independently testable components. It
currently ships three plugin bundles and one shared library.

The importer reads VRM 0.x and 1.0, normalizes the differences away, and authors
a static USD stage. It **never evaluates or simulates** — that boundary is the
project's central design decision, and it is described below.

> **Built with [OpenStrata](https://github.com/animu-sphere/open-strata).**
> `usd-vrm-plugins` is OpenStrata's first external adopter, and the `ost` CLI is
> how this workspace is built, tested, packaged, and released. The record of
> adopting it — every version from pre-0.3 to 0.19.0, including what broke — is
> published in [docs/reports/ost/](docs/reports/ost/). The repo is
> **dual-mode**: everything also builds with plain CMake against any OpenUSD
> install, with no `ost` involved.

## Workspace components

| Component | Type | Role | Status |
| --- | --- | --- | --- |
| [`vrmSchema`](plugins/vrmSchema) | USD schema bundle (`usd-schema`) | VRM typed API schemas + the schema contract | Shipped |
| [`usdVrmFileFormat`](plugins/usdVrmFileFormat) | `SdfFileFormat` bundle (`usd-fileformat`) | `.vrm` parsing, canonicalization, USD authoring | Shipped |
| [`usdVrmPackageResolver`](plugins/usdVrmPackageResolver) | `ArPackageResolver` bundle (`usd-package-resolver`) | Embedded resource resolution from `.vrm` | Shipped |
| [`vrmContainer`](libs/vrmContainer) | Plain CMake library | GLB parsing + byte-range validation | Shipped |
| `usdVrm` | **Aggregate product name** | Composed distribution of the workspace | Shipped via `ost plugin package --workspace --product` |

`usdVrm` is not a bundle id — it names the product as a whole. It *was* the
file-format bundle's name until the workspace split; documentation and artifacts
that predate that rename use it in the old sense.

### The motion layer (planned, not built)

Nothing below exists in the tree yet. The identities and dependency edges are
reserved in the workspace contract so the boundaries are fixed before any code
lands. Rationale:
[docs/design/MOTION_ARCHITECTURE_POLICY.md](docs/design/MOTION_ARCHITECTURE_POLICY.md).

| Component | Type | Role |
| --- | --- | --- |
| `usdVrmaFileFormat` | `SdfFileFormat` bundle | `.vrma` motion clips → `UsdSkelAnimation` on a *canonical semantic* humanoid skeleton |
| `motionCore` | Plain CMake library | Vendor-neutral pose / animation / root-motion / constraint types |
| `motionRuntime` | Plain CMake library | Timestamped pose buffer, resample, filter, blend |
| `vrmRetarget` | Plain CMake library | Humanoid mapping, rest-pose correction, root-motion policy |
| `execMotion` | OpenExec bundle | Vendor-neutral motion nodes |
| `execVrm` | OpenExec bundle | VRM semantics: retarget, root motion, expression, look-at, avatar apply |
| `adapters/` | Optional bundles | The **only** place product names are permitted (e.g. Mocopi, ARDY) |

`.vrm` and `.vrma` are deliberately **separate** file-format plugins with
symmetric structure, and they compose by **reference**, not `subLayer` — a
subLayer stack cannot express which skeleton a clip applies to. A third
binding/assembly layer relates them.

### Dependencies

```text
usdVrmFileFormat ───────> vrmSchema
        │
        └───────────────> vrmContainer

usdVrmPackageResolver ──> vrmContainer

                          (planned)
usdVrmaFileFormat ──────> vrmContainer, motionCore
motionRuntime ──────────> motionCore
vrmRetarget ────────────> motionCore, motionRuntime
execMotion ─────────────> motionCore, motionRuntime
execVrm ────────────────> vrmSchema, vrmRetarget
adapters/* ─────────────> motionCore, motionRuntime
```

Five rules keep those edges honest:

- `vrmSchema` depends on no other bundle or library.
- `usdVrmPackageResolver` never links the file-format bundle; the importer's
  dependency on the resolver is runtime-only, never link-time.
- `execVrm` reads the schema contract from the stage — never the importer's
  private API or canonical model.
- `vrmRetarget` does not depend on OpenExec. The retarget core is finished and
  testable before any OpenExec node exists; the nodes are thin wrappers.
- Adapters depend on the core. The core never depends on an adapter, and
  `motionCore` never sees a vendor SDK, a network protocol, or a product name.

The bundle graph is validated by `ost plugin test --workspace`, and each
consumer adds a binary link check proving what it does and does not import. Full
contract: [docs/architecture/WORKSPACE.md](docs/architecture/WORKSPACE.md).

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
  rig/Humanoid             vrm:humanBones:<bone> joint tokens, typed VrmHumanoidAPI
```

Every `/Asset/rig/*` control prim carries typed schema data. The **schema types
themselves are provided by the `vrmSchema` bundle**; `usdVrmFileFormat` depends
on schema contract version 1 and authors against it. Raw VRM blocks stay in
`customData` as the lossless fallback.

## Runtime boundary

```text
Import:   VRM bytes ──> canonical model ──> USD stage
Runtime:  USD stage + vrmSchema ──> OpenExec / DCC / renderer runtime
```

The importer **authors data only**:

- Import is deterministic. The same bytes produce the same stage.
- LookAt, node constraints, and spring bones are *written as typed schema data*,
  never executed.
- Evaluation and simulation belong to `execVrm` (planned) or an external
  runtime.
- No physics runs at import time.

This keeps import pure, so a runtime can be swapped without touching the
importer.

## Feature support

VRM 0.x / 1.0 detection and canonicalization, geometry, `UsdPreviewSurface`
materials with the full texture set, MToon source preservation
(`vrm:mtoon:raw`; renderer-specific realization is not implemented), unified
skeleton + skinning from inverse bind matrices, skeletal animation, humanoid
mapping, front-direction normalization, and a coded diagnostic taxonomy.

Per-feature status is in
[docs/reference/CAPABILITY_MATRIX.md](docs/reference/CAPABILITY_MATRIX.md).
Supported platforms, OpenUSD versions, and build requirements are in
[docs/reference/SUPPORTED_CONFIGURATIONS.md](docs/reference/SUPPORTED_CONFIGURATIONS.md).
The schema contract is in
[plugins/vrmSchema/docs/SCHEMA_CONTRACT.md](plugins/vrmSchema/docs/SCHEMA_CONTRACT.md).

## Install

See [docs/guides/INSTALL.md](docs/guides/INSTALL.md) for release-artifact,
OpenStrata, and from-source installation, verification, and troubleshooting.

> **Install the complete product from the release artifacts.** Each release
> publishes the three member bundles and one aggregate product archive. The
> member bundles are separately addressable, while the aggregate archive keeps
> the exact workspace closure together. See the
> [install guide](docs/guides/INSTALL.md) for extraction and verification.

## Build and test

### Whole workspace, with OpenStrata (`ost`)

Requires `ost` 0.19+, so `requires.bundles` and `requires.libraries` are
composed automatically.

```sh
# One-time: adopt an OpenUSD install as the cy2026 runtime.
ost runtime pull cy2026 --profile usd --from-usd /path/to/openusd-install

# Validate the bundle graph, then test every bundle in dependency order.
ost plugin test --workspace
```

### A single bundle

```sh
ost plugin build plugins/usdVrmFileFormat
ost plugin test  plugins/usdVrmFileFormat            # L0-L5 verification pyramid

# Inspect a real avatar. build/test/run/package compose the manifest's
# requires.bundles closure automatically:
ost plugin run plugins/usdVrmFileFormat \
    -- python plugins/usdVrmFileFormat/tools/inspect_vrm.py avatar.vrm

# `view` / `test-view` are the exception: they load only what --with names, so
# the runtime siblings must be spelled out or the schema apply fails.
ost plugin view plugins/usdVrmFileFormat avatar.vrm \
    --with plugins/vrmSchema --with plugins/usdVrmPackageResolver
```

### With plain CMake (no OpenStrata)

The workspace root composes every bundle:

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/openusd-install
cmake --build build --config Release
ctest --test-dir build -C Release
```

Each bundle also builds standalone against *installed* sibling packages
(`find_package(vrmSchema CONFIG REQUIRED)`), which is what CI proves; a bundle
never reaches sideways into a sibling's source tree.

The built `libUsdVrmFileFormat.{dll,so,dylib}` lands in
`plugins/usdVrmFileFormat/lib/`; add
`plugins/usdVrmFileFormat/plugin/resources/usdVrmFileFormat` to
`PXR_PLUGINPATH_NAME` and the `lib/` dir to your dynamic-loader path to use it.

### Clean-install smoke

Verifies the *packaged* bundles have no build-tree dependency:

```sh
python scripts/clean_install_smoke.py               # build + package + extract + smoke
python scripts/clean_install_smoke.py --skip-build   # reuse the current build
```

It packages all three bundles with `ost`, extracts them into a fresh directory
**outside** the repo, and runs the assertions in
`plugins/usdVrmFileFormat/tests/clean_install_smoke.py` against that extracted
tree: `.vrm` discovery served from the package, a textured fixture and a corpus
avatar open and validate, and an embedded texture resolves straight from the
`.vrm` container. Needs `ost` + a validated `cy2026` runtime.

### CI

CI is generated from the support matrix in `openstrata.ci.yaml`
(`ost ci generate github`). The PR lane (`.github/workflows/ost-source-ci.yml`)
runs **nine cells** — each of the three bundles on hosted Windows / macOS arm64 /
Linux against digest-pinned cy2026 runtimes — building, testing
(`--up-to 5`; Windows is capped at 4), and packaging each. A weekly scheduled
lane (`ost-support-matrix.yml`) re-validates pinned runtime × plugin artifact
cells on a self-hosted real runtime.

## Release artifacts

Pushing a tag `vX.Y.Z` (matching [`VERSION`](VERSION), with that version's
`CHANGELOG.md` section finalized) runs `.github/workflows/release.yml`: it builds
on all three OS cells, proves the *packaged* artifact (packaged-artifact
verification, clean-install smoke, digest-reproducible packaging), and assembles
a **draft** GitHub release — per-target lean + debug bundles, a source archive,
`SHA256SUMS`, and notes rendered from `CHANGELOG.md` via
[docs/contributing/RELEASE_NOTES_TEMPLATE.md](docs/contributing/RELEASE_NOTES_TEMPLATE.md).
Publishing the draft is a human decision. Run the workflow manually
(`workflow_dispatch`) for a dry run that creates no release.

Each bundle carries a `buildInfo.json` stamp (commit / toolchain / OpenUSD /
build type / schema contract version), surfaced by `tools/vrm_report.py`.

## Documentation

[docs/](docs/) is organized by responsibility — the same layout `open-strata`
and `hydra-merlin` use:

| | |
| --- | --- |
| [docs/architecture/](docs/architecture/) | The binding workspace contract: identities, dependency directions, artifact naming |
| [docs/guides/](docs/guides/) | How to install |
| [docs/reference/](docs/reference/) | What is supported, on what |
| [docs/roadmap/](docs/roadmap/) | What is planned next (incomplete work only) |
| [docs/releases/](docs/releases/) | Per-version release records |
| [docs/design/](docs/design/) | Why the significant decisions were made |
| [docs/reports/](docs/reports/) | Evidence from real runs: the `ost` dogfooding series + the delivery log |

Release history is in the [CHANGELOG](CHANGELOG.md); the release version lives
in the single-source [VERSION](VERSION) file.

## License

Original source and documentation: Apache-2.0 (see [LICENSE](LICENSE)).
Third-party components keep their own licenses; see
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). cgltf is **not** vendored — it
is fetched at configure time.

> Local test VRM avatars used during development are **not** part of this
> repository and are not redistributed here; mind their individual licenses.
