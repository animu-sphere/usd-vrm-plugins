# Supported configurations

The configurations `usd-vrm-plugins` targets and continuously verifies, for the
version in [`VERSION`](../../VERSION). Anything outside this list is not part of
the support contract — and since v0.6.0, the OpenUSD row is not merely
unsupported outside its one value but refused at configure time.

## OpenUSD

| | |
| --- | --- |
| Supported version | **26.08, exactly** (`openusd: "==26.08"` in every `plugins/*/openstrata.plugin.yaml`) |
| Enforced at configure time by | [`cmake/UsdVrmOpenUsd.cmake`](../../cmake/UsdVrmOpenUsd.cmake) |
| OpenExec | required — `exec`, `execGeom`, `execIr`, `execUsd`, `vdf`, `usdExecImaging` |
| Verified against | the `cy2026` runtime's 26.08, on all three OS |

**There is no tolerated range.** v0.6.0 retired the `>=25.05,<27.0` range
v0.1.0–v0.5.0 declared. Two reasons, and only the first applied before:

1. **OpenUSD guarantees no ABI stability across releases.** A plugin built
   against one OpenUSD and loaded into another is undefined behavior, and the
   range never made that safe — it only made it undiagnosed until load time.
2. **The workspace is committed to OpenExec**, whose API is not stable either
   ([the OpenExec plan §1](../roadmap/openexec-foundation.md)). Nothing links it
   yet — the `execMotion` / `execVrm` bundles are v0.8.0 — but the runtime is
   required to carry it from v0.6.0 on, so the refusal is in place before the
   first computation rather than after it.

Anything other than 26.08 is refused **at configure time**, by every entry
point that resolves OpenUSD — the root project, each bundle built standalone by
`ost plugin build`, each library under `libs/`, and each tool under `tools/`:

```text
CMake Error: Unsupported OpenUSD: found 26.05 (PXR_VERSION 2605), require
26.08 (PXR_VERSION 2608) exactly.
```

The same file probes OpenExec and refuses a 26.08 that does not carry it. 26.08
has no OpenExec build toggle — `build_usd.py` ships those six libraries
unconditionally — so this catches a slimmed or hand-stripped install, not a
build-option mistake. Both refusals are covered by the
`workspace_openusd_contract` test, which drives the module against fixture
OpenUSD installs; no runtime we own would ever take the reject path.

> `find_package(pxr 26.08 EXACT ...)` is **not** how this is done: OpenUSD
> installs no `pxrConfigVersion.cmake`, so any version argument makes
> `find_package` fail with "no config version file" whatever OpenUSD is present.
> `pxrConfig.cmake` does set `PXR_VERSION`, and that is what the module tests.

Rebuild the plugins against your target OpenUSD; there is no configuration in
which a mismatch is supported. A second OpenUSD version cell (min vs latest) is
a roadmap P1 item, and it is now a question of which *pinned* versions the
matrix carries, not of widening a range.

**v0.5.0 was the first release built and verified against 26.08 throughout;
v0.6.0 is the first that cannot be built against anything else.** (v0.4.0 was
verified against 26.05; the runtimes were re-pinned after it was tagged.) The
26.05 → 26.08 move changed no observable behavior of these plugins: every
Workspace Phase 0 baseline artifact is byte-identical across the two versions
except the exported symbol names, which differ only by OpenUSD's internal
`pxrInternal_v0_26_5` → `_26_8` namespace.

All three 26.08 runtimes are published and every lane pins them by digest.
Since 2026-08-25 they are leaves of `ost`'s own canonical OpenUSD runtime
matrix rather than runtimes this repository's maintainer built by hand:
`26.08-gl-windows-x86_64`, `26.08-gl-linux-x86_64` and
`26.08-metal-macos-arm64`
([report 36](../reports/ost/36-2026-08-25-v0.22.3-canonical-runtimes-and-release-membership.md);
the hand-built ones they replace are
[report 29](../reports/ost/29-2026-07-26-v0.20.0-openusd-2608-runtime-publish.md)
and
[report 30](../reports/ost/30-2026-07-26-v0.20.0-macos-2608-runtime-publish.md)).

**The variant in those names is a requirement, not a preference.** The same
matrix publishes a `core` leaf per platform, built `--no-imaging`, and
`cmake/UsdVrmOpenUsd.cmake` refuses any runtime without `usdExecImaging` — one
of the six OpenExec components it probes, and the one that lives under
`pxr/usdImaging`. A `core` runtime therefore cannot configure this workspace at
all, and `gl`/`metal` is the floor rather than an upgrade. What the imaging
leaves add beyond that is evidence: their producer verified loader, physical
device and render, where the runtimes they replace recorded `not-run` for all
three.

## Toolchain

| | |
| --- | --- |
| CMake | ≥ 3.22 |
| C++ standard | C++17 (`CMAKE_CXX_STANDARD 17`, no compiler extensions) |
| Build type | Release (default; OpenUSD installs we build against are Release-only) |
| Python | 3.13 — required for the tools (validator, report, packaging, fixture/schema generation) and tests |

`usdGenSchema` (a Python tool from OpenUSD) is needed only to **regenerate** the
typed schema sources; the generated C++ and `generatedSchema.usda` are committed
as the plain-CMake fallback, so a normal build does not run it.

## Platforms & architectures (CI-verified)

These match the per-PR CI matrix in `.github/workflows/ost-source-ci.yml`
(each cell: build → `ost plugin test --up-to 5` → package):

| OS | Runner | Arch | ABI |
| --- | --- | --- | --- |
| Windows | `windows-2022` | x86_64 | MSVC toolset 143 |
| macOS | `macos-15` | arm64 (Apple silicon) | libc++ |
| Linux | `ubuntu-24.04` | x86_64 | libstdc++ (glibc ≥ 2.38 floor) |

Other host OS versions / architectures (e.g. Linux arm64, x86_64 macOS) are not
part of the verified matrix.

These cells cover the four plugin bundles. Every plain library under `libs/`,
every adapter under `adapters/`, and every CLI are covered by the three
`kind: workspace` cells that `ost` 0.21.0 made expressible — they build the root
CMake tree, which is the only configuration in which those targets exist, and
run its whole CTest suite on the same three runners
([report 33](../reports/ost/33-2026-07-28-v0.21.0-workspace-ci-adoption.md)). A
fourth workspace cell, `workspace-graph-pr`, runs the WORKSPACE.md §2
dependency-direction gate before anything is built.

## Build outputs

The file-format plugins are **shared** libraries
(`libUsdVrmFileFormat.{dll,so,dylib}` and
`libUsdVrmaFileFormat.{dll,so,dylib}`) — USD loads them dynamically. There is
no supported static-plugin build. `motionCore`, `motionRuntime`, and
`vrmRetarget` are intentionally static and are linked into their consumers;
`motion_retarget` and `motion_capture` are ordinary executables and register
nothing with OpenUSD.
Discovery follows OpenUSD's standard mechanism: add
the required bundle's `plugin/resources/<bundle>` directory to
`PXR_PLUGINPATH_NAME` and its `lib/` directory to the dynamic-loader path.

## Versioning relationship

- The **package version** is the single value in the repo-root
  [`VERSION`](../../VERSION) file. CMake reads it; the git tag (`vX.Y.Z`), the
  [`CHANGELOG`](../../CHANGELOG.md), and the `ost` bundle manifest mirror it.
- The **schema contract version** is independent (currently `1`) and changes only
  under the policy in
  [`../plugins/vrmSchema/docs/SCHEMA_CONTRACT.md`](../../plugins/vrmSchema/docs/SCHEMA_CONTRACT.md).

## Non-goals

See the [`CHANGELOG`](../../CHANGELOG.md#non-goals-for-v010) and the
[roadmap non-goals](../roadmap/backlog.md#non-goals).
