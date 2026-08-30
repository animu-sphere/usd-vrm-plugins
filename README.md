# USD VRM Plugins

OpenUSD plugins for [VRM](https://vrm.dev/en/) avatars.

This repository is an OpenUSD plugin **workspace**: it separates schema
definitions, file-format import, package resolution, and shared GLB container
parsing into independently buildable, independently testable components. The
v0.7.0 release adds the mocopi live-input adapter and a generic BVH
recorded-motion pipeline, bringing the workspace to four plugin bundles, eight
shared libraries, and six CLIs.

The importer reads VRM 0.x and 1.0, normalizes the differences away, and authors
a static USD stage. It **never evaluates or simulates** — that boundary is the
project's central design decision, and it is described below.

> **Built with [OpenStrata](https://github.com/animu-sphere/open-strata).**
> `usd-vrm-plugins` is OpenStrata's first external adopter, and the `ost` CLI is
> how this workspace is built, tested, packaged, and released. The record of
> adopting it — every version from pre-0.3 to 0.22.2, including what broke — is
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
| [`usdVrmaFileFormat`](plugins/usdVrmaFileFormat) | `SdfFileFormat` bundle (`usd-fileformat`) | `.vrma` motion clips → canonical `UsdSkelAnimation` | v0.3.0 |
| [`motionCore`](libs/motionCore) | Plain static CMake library | Vendor-neutral humanoid pose / animation / root-motion / constraint types | v0.3.0 |
| [`motionRuntime`](libs/motionRuntime) | Plain static CMake library | Timestamped pose buffer, interpolation, resample, filter, blend; live-capture intake, recorded traces, replay | v0.4.0 · v0.5.0 |
| [`vrmRetarget`](libs/vrmRetarget) | Plain static CMake library | Humanoid mapping, rest-pose correction, root-motion policy, pose retargeter | v0.4.0 |
| [`motion_retarget`](tools/motionRetarget) | CLI executable | Bakes a semantic clip onto a target rig as `UsdSkelAnimation` | v0.4.0 |
| [`motion_capture`](tools/motionCapture) | CLI executable | Replays a recorded capture session into a semantic clip the above consumes unchanged | v0.5.0 |
| [`vrmAdapterVmc`](adapters/liveCapture/vmc) | Plain static CMake library | VMC Protocol input: OSC-over-UDP datagrams → canonical humanoid motion | v0.6.0 |
| [`vmc_record`](adapters/liveCapture/vmc/tools/vmcRecord) | CLI executable | Records and inspects VMC packet captures with a decode report, and exports what the adapter delivered as a capture trace the tools above replay unchanged | v0.6.0 |
| [`vrmAdapterMocopi`](adapters/liveCapture/mocopi) | Plain static CMake library | Native live UDP input for one capture product, kept strictly separate from the relay path above | v0.7.0 |
| [`mocopi_record`](adapters/liveCapture/mocopi/tools/mocopiRecord) | CLI executable | Records and inspects mocopi UDP captures, and exports a capture trace `motion_capture` replays unchanged | v0.7.0 |
| [`motionSource`](libs/motionSource) | Plain static CMake library | Format-neutral source skeleton / animation model, the producer-profile contract, and the converter to canonical humanoid motion | v0.7.0 |
| [`motionBvh`](libs/motionBvh) | Plain static CMake library | BVH syntax and extraction only — no producer semantics, no default profile | v0.7.0 |
| [`motion_bvh_inspect`](tools/motionBvh) | CLI executable | Reports what a BVH file contains — hierarchy, channels in declaration order, frames, and per-column value ranges | v0.7.0 |
| [`motion_bvh_convert`](tools/motionBvh) | CLI executable | Converts a BVH file to the avatar-independent semantic clip under an explicitly named profile | v0.7.0 |
| [`liveTransport`](libs/liveTransport) | Plain static CMake library | The live half's shared leaf: UDP receiver, opt-in datagram queue, packet-capture file format, and the diagnostic vehicle every live adapter reports through — no protocol, no product name, no diagnostic code | Unreleased |
| [`osc`](libs/osc) | Plain static CMake library | The OSC 1.0 wire format, shared by every adapter that speaks it: packets, bundles, addresses, type tags, arguments, and a refusal that carries no diagnostic code — no address semantics, no product name, and an empty link line | Unreleased |
| [`vrmAdapterVrchatOsc`](adapters/liveCapture/vrchatOsc) | Plain static CMake library | VRChat OSC tracker input: numbered tracker observations, which are pre-IK, so it stops at a tracker frame and the humanoid solve stays outside it. Recorder and address inventory — no semantic decoder yet | Unreleased |
| [`vrchat_osc_record`](adapters/liveCapture/vrchatOsc/tools/vrchatOscRecord) | CLI executable | Records and inspects VRChat OSC packet captures. Recording reports the datagram envelope and nothing about a payload; `--inspect` adds the address inventory, read out of the bytes rather than out of the specification | Unreleased |
| `motionTracking` | Plain static CMake library | Which tracker is which body region: a generic region vocabulary that is not a bone list, an operator's explicit statement binding an opaque tracker identity to one, and a stated policy for an observed set it cannot place. No address literal, no adapter identity, and an empty link line | Unreleased |
| `usdVrm` | **Aggregate product name** | Composed distribution of the workspace | Shipped via `ost plugin package --workspace --product` |

`usdVrm` is not a bundle id — it names the product as a whole. It *was* the
file-format bundle's name until the workspace split; documentation and artifacts
that predate that rename use it in the old sense.

### The motion layer

`motionCore` and `usdVrmaFileFormat` were the v0.3.0 foundation; v0.4.0 added
`motionRuntime`, `vrmRetarget`, and the `motion_retarget` CLI, which together
make a `.vrma` clip play back on a real avatar. v0.5.0 adds the observation
side — a vendor-neutral `LiveCaptureSource`, a recorded-trace format, and the
`motion_capture` CLI — which produces the *same* semantic clip, so a live
session is baked by the retarget tool unchanged. The fixed contract is
[docs/design/MOTION_CONTRACT.md](docs/design/MOTION_CONTRACT.md). The `exec*`
identities remain reserved; runtime evaluation is not part of this release.
v0.6.0 supplies the first product-specific input leaf: `vrmAdapterVmc` decodes
VMC Protocol from OSC-over-UDP through frame assembly and VRM bone mapping into
the existing `LiveCaptureSource`; `vmc_record` records the same wire input for
inspection and corpus work, and `--export-trace` hands what the adapter
delivered to `motion_capture` as a plain capture trace — the product's tools
consume a live VMC session without linking the adapter, or knowing it exists.
v0.7.0 adds `vrmAdapterMocopi` and `mocopi_record` on that same shape, and a
body that travels: a rig whose only translating joint is the hips now composes
`RootMotion`, so a live session no longer retargets in place.

v0.7.0 supplies the other half of the input layer, and the first evidence off
real hardware. A capture product sends packets *and* writes files: the packets
go through `vrmAdapterMocopi`, a native UDP path for a wire grammar with no
published specification, and the files go through a **generic** BVH pipeline
(`motionBvh` + `motionSource` + a declarative producer profile) that is
deliberately not that product's importer. The two halves meet at `motionCore`
and nowhere earlier — and when one physical session is observed both ways, they
agree to a median **0.084°** per bone
([report 01](docs/reports/motion/01-2026-08-15-mocopi-cross-source.md)).
OpenExec evaluation follows, and uses those recordings as its parity input.
Schedule: [docs/roadmap/](docs/roadmap/README.md#status-at-a-glance).

| Component | Type | Role |
| --- | --- | --- |
| [`usdVrmaFileFormat`](plugins/usdVrmaFileFormat) | `SdfFileFormat` bundle | `.vrma` motion clips → `UsdSkelAnimation` on a *canonical semantic* humanoid skeleton |
| [`motionCore`](libs/motionCore) | Plain static CMake library | Vendor-neutral pose / animation / root-motion / constraint types |
| [`motionRuntime`](libs/motionRuntime) | Plain static CMake library | Timestamped pose buffer, interpolation, resample, filter, blend |
| [`vrmRetarget`](libs/vrmRetarget) | Plain static CMake library | Humanoid mapping, rest-pose correction, root-motion policy, pose retargeter |
| [`motion_retarget`](tools/motionRetarget) | CLI executable | The stage half: reads the rig and the clip, bakes the retargeted `UsdSkelAnimation`, binds `skel:animationSource` |
| `execMotion` | OpenExec bundle | Vendor-neutral motion nodes |
| `execVrm` | OpenExec bundle | VRM semantics: retarget, root motion, expression, look-at, avatar apply |
| `adapters/` | Optional plain libraries + their CLIs | **Live** input leaves — a VMC Protocol adapter first, then vendor-native and generator adapters. The **only** place product or protocol names are permitted *in code* (e.g. VMC, Mocopi, ARDY) |
| `motionSource` · `motionBvh` | Plain static CMake libraries | **Recorded-file** input: BVH syntax, a format-neutral source model, and conversion to canonical humanoid motion under an explicit producer profile |
| `profiles/motion/` | Package data | One declarative file per producer *and export preset*. Product names live here rather than in the libraries that read them |
| [`vrmAdapterVmc`](adapters/liveCapture/vmc) | Plain static CMake library | The first input leaf: VMC Protocol from OSC-over-UDP datagrams through frame assembly and VRM bone mapping to canonical humanoid semantics; includes a recorded-packet corpus and the `vmc_record` CLI |
| [`vrmAdapterMocopi`](adapters/liveCapture/mocopi) | Plain static CMake library | The second: a capture product's own UDP grammar, measured off five device sessions rather than read from a specification, through the same frame assembly and bridge; includes the `mocopi_record` CLI |
| [`liveTransport`](libs/liveTransport) | Plain static CMake library | What the live leaves stopped writing twice: the socket, the packet-capture format and the diagnostic vehicle. It is under `libs/` and still **outside** the aggregate product — no tool in the product opens a transport — and its allowed edge set is empty |
| [`osc`](libs/osc) | Plain static CMake library | The other thing the live leaves stopped writing twice, and the one that had to wait: an OSC decoder extracted on the strength of one caller is a decoder shaped like that caller, so it moved when a second consumer had decoded through it. Also outside the aggregate product, and its link line is empty of `liveTransport` too — the two are siblings, not a stack |
| [`vrmAdapterVrchatOsc`](adapters/liveCapture/vrchatOsc) | Plain static CMake library | The third, and the first that is not a pose source: this wire carries numbered tracker observations, and a tracker index is not a body role. Written on the near side of both extractions, so its capture format is one magic string, its receiver is a `switch` over two transport events, and its address inventory is a loop over a decoder it does not own |
| `motionTracking` | Plain static CMake library | What a tracker source is not allowed to decide: which tracker is on which body region. Generic, outside every adapter, and holding a region vocabulary that is deliberately not a bone list — an adapter that mapped `/tracking/trackers/1/*` onto a hips joint would have invented a calibration and hidden it in a decoder |

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

usdVrmaFileFormat ──────> vrmContainer, motionCore

motionRuntime ──────────> motionCore
vrmRetarget ────────────> motionCore, motionRuntime
motion_retarget (CLI) ──> vrmRetarget + OpenUSD stage APIs

vrmAdapterVmc ──────────> motionCore, motionRuntime, liveTransport, osc
vrmAdapterMocopi ───────> motionCore, motionRuntime, liveTransport
vrmAdapterVrchatOsc ────> motionCore, liveTransport, osc (no motionRuntime: it
                          stops at an observation, and a pose is what reaches
                          that library)
liveTransport ──────────> nothing — its allowed edge set is empty, not short
osc ────────────────────> nothing — the same, `liveTransport` included
motionTracking ─────────> nothing — the same again, and for a third reason: it
                          maps one vocabulary it owns onto another

motionSource ───────────> motionCore
motionBvh ──────────────> motionSource
motion_bvh_convert ─────> motionBvh, motionSource, OpenUSD stage

                          (planned)
execMotion ─────────────> motionCore, motionRuntime
execVrm ────────────────> vrmSchema, vrmRetarget
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
- A file reader knows a format and no semantics; `motionSource` knows semantics
  and no format. `motionBvh → motionSource` never reverses, so a second reader
  can be added without changing anything above it.
- **Live input and recorded files meet at `motionCore` and nowhere earlier.** An
  adapter never reaches for a reader, and a reader never reaches for an adapter.

The bundle graph is validated by `ost plugin test --workspace`, and each
consumer adds a binary link check proving what it does and does not import. Full
contract: [docs/architecture/WORKSPACE.md](docs/architecture/WORKSPACE.md).

## What the importer produces

`.vrm` is read as a GLB container (via vendored
[cgltf](https://github.com/jkuhlmann/cgltf) v1.15) and normalized — VRM 0.x and 1.0 differences are
absorbed into a canonical model before any USD is authored — into:

```
/Asset                     SkelRoot (or Xform when there is no skeleton), kind=component
  customData.vrm.*         sourceFormat / sourceVersion / specVersion / meta / rawExtension
  geo/                     Scope of UsdGeomMesh (one per glTF primitive)
    <Mesh>                 points/normals/st, material binding; skel binding when
                           skinned, else the glTF node transform as xformOp
  mtl/<Material>           UsdShadeMaterial: identity, binding target, VRM semantics
    preview/               UsdShadeNodeGraph holding the UsdPreviewSurface network
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

> **Install the components you use from the release artifacts.** Each release
> publishes four member bundles and one aggregate product archive. The three
> VRM bundles are installed together; `usdVrmaFileFormat` is independently
> installable because it has no plugin-bundle dependency. The
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

ost plugin build plugins/usdVrmaFileFormat
ost plugin test  plugins/usdVrmaFileFormat           # L0-L5 + VRMA golden

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

It packages the three VRM bundles with `ost`, extracts them into a fresh directory
**outside** the repo, and runs the assertions in
`plugins/usdVrmFileFormat/tests/clean_install_smoke.py` against that extracted
tree: `.vrm` discovery served from the package, a textured fixture and a corpus
avatar open and validate, and an embedded texture resolves straight from the
`.vrm` container. Needs `ost` + a validated `cy2026` runtime.

### CI

CI is generated from the support matrix in `openstrata.ci.yaml`
(`ost ci generate github`). The PR lane (`.github/workflows/ost-source-ci.yml`)
runs **seven cells** against digest-pinned cy2026 runtimes on hosted Windows /
macOS arm64 / Linux:

- **One graph cell** (`verify: graph`), which runs
  `ost plugin test --workspace --graph-only` — the [WORKSPACE.md §2](docs/architecture/WORKSPACE.md)
  dependency-direction gate — before anything is built, in milliseconds.
- **Three workspace cells** (`kind: workspace`), one per OS, which build the
  root CMake tree and run its CTest suite. This is the behavioral lane: the root
  tree is the only configuration in which the plain libraries and the CLI tools
  exist, and its suite also contains every bundle's own tests, so it is the
  coverage `motionCore`, `motionRuntime`, `vrmRetarget`, `vrmContainer`,
  `motion_retarget`, `motion_capture`, all four plugin bundles and the
  whole-workspace `usdvrm_baseline` gate get.
- **Three bundle cells** — `usdVrmFileFormat` on each OS — which build that
  bundle *standalone* (`ost plugin build`, no root tree in scope), run its
  pyramid (`--up-to 5`; Windows is capped at 4), and `ost plugin package` it.
  Neither the standalone configure nor packaging is reachable from a workspace
  cell, and they are per-platform, which is what these three are for.

There were sixteen cells until 2026-08-30 — all four bundles on all three OS.
Nine were removed as measured duplicates of the workspace suite; `openstrata.ci.yaml`
carries the evidence and what to re-run before adding them back. There is no
scheduled lane any more: its one cell targeted a self-hosted runner that does
not exist and had been cancelled weekly since 2026-07-27.

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

`usdVrmFileFormat` carries a `buildInfo.json` stamp (commit / toolchain /
OpenUSD release and `PXR_VERSION` / OpenExec components / build type / schema
contract version), surfaced by `tools/vrm_report.py`.

Every bundle is built against **OpenUSD 26.08 and nothing else**, and against a
26.08 that carries OpenExec. Both are enforced at configure time by
[`cmake/UsdVrmOpenUsd.cmake`](cmake/UsdVrmOpenUsd.cmake), for `ost` and
plain-CMake builds alike — see
[supported configurations](docs/reference/SUPPORTED_CONFIGURATIONS.md).

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
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). cgltf v1.15 is vendored under
[`third_party/cgltf`](third_party/cgltf) with its MIT license.

> Local test VRM avatars used during development are **not** part of this
> repository and are not redistributed here; mind their individual licenses.
