# Changelog

All notable changes to `usd-vrm-plugins` are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
[Semantic Versioning](https://semver.org/spec/v2.0.0.html). The release version
is the single value in the repo-root [`VERSION`](VERSION) file; the git tag
(`vX.Y.Z`), this changelog, and the OpenStrata bundle manifest mirror it.

The **schema contract version** is tracked separately from the package version
(it changes only when the typed `Vrm*API` interpretation contract changes — see
[`plugins/vrmSchema/docs/SCHEMA_CONTRACT.md`](plugins/vrmSchema/docs/SCHEMA_CONTRACT.md)).
Current schema contract version: **1**.

## [Unreleased]

## [0.2.0] — 2026-07-18

First release of the multi-bundle workspace. **Artifact-breaking:** consumers of
the v0.1.0 `usdVrm-0.1.0-*` asset names must move to
`usdVrmFileFormat-0.2.0-<target>`, and the typed schemas now ship as their own
`vrmSchema-0.2.0-<target>` asset that must be installed alongside it — see
[the release notes](https://github.com/animu-sphere/usd-vrm-plugins/blob/v0.2.0/docs/releases/v0.2.0.md)
for the install order.

### Added
- **Workspace Phase 2 — `vrmContainer` extraction**: a standalone plain shared
  CMake library (`libs/vrmContainer`) now validates GLB 2 headers/chunks and
  byte ranges, exposes immutable non-owning views, and owns stable embedded
  resource hashing. It exports an installed `find_package(vrmContainer)`
  package, carries OpenUSD/plugin boundary checks, and is consumed by both the
  importer reader and the co-located resolver without changing Phase 0 output.
- **Negative test corpus** (`plugins/usdVrmFileFormat/tests/corpus/generated/malformed/`):
  nine deliberately-broken, license-clean `.vrm` fixtures (authored by
  `tools/generate_negative.py`) that pin the importer's diagnostic contract —
  each provokes exactly one code, driven by `negative-manifest.json` +
  `tests/test_usdvrm_negative.py` (registered as the `usdvrm_negative` CTest and
  in the bundle's `tests.smoke` / `tests.negative` lists).

### Changed
- **Release artifacts now ship the whole workspace.** The release lane packages
  and publishes all three bundles — `vrmSchema`, `usdVrmFileFormat`, and
  `usdVrmPackageResolver` — via `ost plugin package --workspace`, instead of the
  importer bundle alone. This is required, not cosmetic: since the Workspace
  Phase 1 schema split, an `usdVrmFileFormat` package on its own registers the
  `.vrm` file format but **cannot open a stage**, because OpenStrata stages a
  dependency bundle's link-time half (`libvrmSchema` + its CMake package) without
  its USD registration half (`plugInfo.json`, `generatedSchema.usda`), so the
  typed `Vrm*API` schemas are never discovered. Installing all three bundles on
  the plugin path is therefore the supported configuration. Upstream asks are
  filed in [ost report 23](https://github.com/animu-sphere/usd-vrm-plugins/blob/v0.2.0/docs/reports/ost/23-2026-07-18-v0.18.0-workspace-packaging-v0.19.0-asks.md).
- **Workspace Phase 4 — `usdVrm` → `usdVrmFileFormat` rename**: the file-format
  bundle now carries the identity the workspace contract assigns it
  (`docs/architecture/WORKSPACE.md` §1). The bundle directory
  (`plugins/usdVrm` → `plugins/usdVrmFileFormat`), the manifest `plugin.name`,
  and the USD resource directory (`plugin/resources/usdVrm` →
  `plugin/resources/usdVrmFileFormat`) all move together. `usdVrm` is retired as
  a bundle id and now names only the aggregate product.
  **Release-artifact rename:** the per-bundle artifacts and manifest sidecar
  ship as `usdVrmFileFormat-<version>-<target>.tar.zst` (§5) — anyone consuming
  the v0.1.0 `usdVrm-*` asset names must update. No functional change: the USD
  registry name (`UsdVrmFileFormat`), the registered `.vrm` file format, the
  library name (`libUsdVrmFileFormat`), authored stage output, and the
  diagnostic codes are all untouched, and the Phase 0 baseline verifies
  byte-identical (53/53).
- **OpenStrata 0.15 workspace composition**: CI and source builds now consume
  the `requires.bundles` closure directly, removing the repo-owned vrmSchema
  bootstrap and restoring the importer PR lanes from L1 to the full L5 gate.
  `vrmContainer` is staged through `requires.runtime_libs`; portable
  `requires.libraries` remains reserved/fail-closed in ost 0.15.
- **Workspace Phase 1 — `vrmSchema` bundle split**: the six typed `Vrm*API`
  schemas (sources, generated C++, `generatedSchema.usda`, USD registration,
  `tools/generate_schema.py`, `docs/SCHEMA_CONTRACT.md`) moved from the
  `usdVrm` bundle into the new standalone `plugins/vrmSchema` bundle
  (`kind: usd-schema`, `schema.contract: 1`, CMake package export +
  installed-package consumer smoke). `usdVrm` now consumes it via
  `find_package(vrmSchema)` / `requires.bundles` per
  `docs/architecture/WORKSPACE.md` §2. Authored stages, the schema contract,
  the registered-type set, and the exported C++ surface are unchanged
  (Phase 0 baseline verified byte-identical).
- **Importer diagnostics**: malformed inputs the importer previously sanitized or
  rejected silently now emit stable codes. New codes: `VRM003` (container
  unreadable — FATAL, stage fails to open), `VRM111` (skin `JOINTS_0` index out of
  range; clamped to root), `VRM141` (duplicate humanoid bone; first mapping kept),
  `VRM151` (expression morph-target index out of range; bind skipped), `VRM190`
  (spring collider-group index out of range; dropped). See
  `plugins/usdVrmFileFormat/docs/DIAGNOSTICS.md`.

## [0.1.0] — 2026-07-12

First tagged release of the `usdVrm` OpenUSD file-format plugin: a `.vrm`
(VRM 0.x / 1.0) importer that normalizes into a canonical model and authors a
typed USD stage, plus the reliability tooling around it.

### Added
- **Importer** (`SdfFileFormat` for `vrm`): GLB read via cgltf, VRM 0.x/1.0
  detection, all version differences absorbed into `VrmCanonicalDocument` before
  any USD is authored.
- **Geometry & materials**: `UsdGeomMesh` (points/normals/UV/indices), non-skinned
  node transforms preserved; `UsdPreviewSurface` materials with the full texture
  set (base color, metallic-roughness, normal, emissive, occlusion; wrap modes,
  `KHR_texture_transform`).
- **Skeleton & skinning**: one `UsdSkelSkeleton` unified across all glTF skins,
  topologically ordered, bind from inverse bind matrices; `UsdSkelBindingAPI`.
- **Skeletal animation**: glTF joint TRS clips → `UsdSkelAnimation`.
- **Front-direction bake**: VRM 0.x −Z front normalized to a canonical +Z rest
  pose; provenance in `customData` (`vrm:sourceFrontAxis`, `vrm:frontAxisNormalized`).
- **Typed schemas** (compiled, co-located in `usdVrm`): `VrmHumanoidAPI`,
  `VrmExpressionAPI`, `VrmLookAtAPI`, `VrmSpringBoneAPI`, `VrmColliderAPI`,
  `VrmConstraintAPI`. VRM constraints / LookAt / SpringBone are authored as
  **data only** — evaluation and simulation are the future `execVrm` runtime layer.
- **MToon**: preserved as `vrm:mtoon:raw` + `vrm:shaderModel` tag, with a
  `UsdPreviewSurface` fallback (approximation, not a shading reproduction).
- **Lossless preservation**: `vrm:meta` / `specVersion` / `vrm:rawExtension` in
  `customData`.
- **Reliability tooling**: standalone stage validator (`tools/validate_vrm.py`,
  non-zero exit on ERROR/FATAL), coded diagnostic taxonomy (`VRMxxx`), compatibility
  report (`tools/vrm_report.py`), portable texture packaging (`ArPackageResolver`
  + `tools/package_vrm.py`).
- **Schema contract v1** documented and versioned; stages/report carry
  `schemaContractVersion = 1`.
- **Redistributable corpus** seed: Seed-san (VirtualCast) and VRM1 Constraint
  Twist (pixiv), both VRM 1.0 with `allowRedistribution: true`.
- **Corpus foundation** — `tests/corpus/` organized into `spec-samples/`
  (vendored, license-clear), `vroid/` (fetched, git-ignored), `conformance/`, and
  `generated/`; a machine-readable `tests/corpus/manifest.json` (provenance,
  SHA-256, roles, feature tags, expected diagnostics + max severity) drives
  `test_usdvrm_corpus.py` and asserts the diagnostic-code contract.
  `scripts/verify_corpus.py` (SHA-256) and `scripts/fetch_corpus.py` (pinned,
  license-gated fetch for the VRoid + Alicia candidates). No third-party binaries
  are committed.
- **Release contract**: repo-root `VERSION` single source of truth (consumed by
  CMake), this `CHANGELOG.md`, [`docs/reference/CAPABILITY_MATRIX.md`](docs/reference/CAPABILITY_MATRIX.md),
  and [`docs/reference/SUPPORTED_CONFIGURATIONS.md`](docs/reference/SUPPORTED_CONFIGURATIONS.md).
- **Clean-install / plugin-discovery smoke** — `scripts/clean_install_smoke.py`
  packages the bundle, extracts the artifact into a fresh directory outside the
  repo, and runs `plugins/usdVrm/tests/clean_install_smoke.py` inside that
  extracted bundle's session: `.vrm` discovery served from the package, a
  textured fixture + a corpus avatar open and validate, and an embedded texture
  resolves straight from the `.vrm` container — proving no build-tree dependency.
- **Release workflow** ([`.github/workflows/release.yml`](.github/workflows/release.yml)) —
  a tag `vX.Y.Z` (must match `VERSION`, with this changelog's section finalized)
  builds Windows / macOS arm64 / Linux bundles against digest-pinned cy2026
  runtimes, gates on **digest-reproducible packaging**, packaged-artifact
  verification (`ost plugin test --from-package`) and the clean-install smoke on
  all three OS, then assembles a **draft** GitHub release: per-target lean +
  debug-symbol bundles, a source archive, `SHA256SUMS`, and notes rendered from
  this changelog via [`docs/contributing/RELEASE_NOTES_TEMPLATE.md`](docs/contributing/RELEASE_NOTES_TEMPLATE.md)
  (`scripts/make_release_notes.py`). `workflow_dispatch` = dry run.
- **Build-metadata stamp** — CMake configures `buildInfo.json` next to
  `plugInfo.json` (git commit, build OS, compiler, build type, OpenUSD version,
  plugin version, schema contract version), shipped inside every packaged bundle;
  `tools/vrm_report.py` surfaces it as the report's `build` section. The stamp
  carries no timestamp so packaging stays digest-reproducible.
- **Install guide** ([`docs/guides/INSTALL.md`](docs/guides/INSTALL.md)) — release-artifact,
  OpenStrata, and from-source installation with verification and troubleshooting.
- **CI & packaging**: generated 3-OS source lanes (`ost ci generate github`)
  bootstrap-pinned to **ost 0.13.0**; packages are **lean by default** (debug
  symbols split into a sibling `*-debug.tar.zst`) and digest-reproducible for an
  unchanged build.

### Known limitations
- MToon is a `UsdPreviewSurface` fallback plus preserved raw data — **not** a
  shading reproduction (full realization is roadmap P5).
- **Morph-weight (blend-shape) animation is not authored** — only joint TRS
  animation clips are.
- No runtime **evaluation/simulation** (LookAt, constraints, spring bones are
  data only; the `execVrm` runtime layer is roadmap P4).
- Compressed / unsupported embedded texture formats (e.g. KTX2) are skipped with
  a coded warning (`VRM101`/`VRM102`).
- No VRM **exporter** (round-trip is research only, roadmap P6).

### Non-goals for v0.1.0
Explicitly out of scope for this release (tracked in the
[roadmap](docs/roadmap/) non-goals and design policy §15/§19):
- Full/pixel-perfect MToon shading reproduction across renderers.
- A VRM exporter.
- SpringBone / physics runtime simulation.
- Mocopi or other live input streaming.
- Coverage of every glTF extension.
- ABI stability guarantees across all OpenUSD versions (see
  [`docs/reference/SUPPORTED_CONFIGURATIONS.md`](docs/reference/SUPPORTED_CONFIGURATIONS.md)).

[Unreleased]: https://github.com/animu-sphere/usd-vrm-plugins/compare/v0.2.0...HEAD
[0.2.0]: https://github.com/animu-sphere/usd-vrm-plugins/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/animu-sphere/usd-vrm-plugins/releases/tag/v0.1.0
