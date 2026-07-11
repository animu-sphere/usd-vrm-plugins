# Changelog

All notable changes to `usd-vrm-plugins` are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
[Semantic Versioning](https://semver.org/spec/v2.0.0.html). The release version
is the single value in the repo-root [`VERSION`](VERSION) file; the git tag
(`vX.Y.Z`), this changelog, and the OpenStrata bundle manifest mirror it.

The **schema contract version** is tracked separately from the package version
(it changes only when the typed `Vrm*API` interpretation contract changes — see
[`plugins/usdVrm/docs/SCHEMA_CONTRACT.md`](plugins/usdVrm/docs/SCHEMA_CONTRACT.md)).
Current schema contract version: **1**.

## [Unreleased]

### Added
- Release contract: repo-root `VERSION` single source of truth (consumed by
  CMake), this `CHANGELOG.md`, [`docs/CAPABILITY_MATRIX.md`](docs/CAPABILITY_MATRIX.md),
  and [`docs/SUPPORTED_CONFIGURATIONS.md`](docs/SUPPORTED_CONFIGURATIONS.md).

## [0.1.0] — unreleased

First tagged release of the `usdVrm` OpenUSD file-format plugin: a `.vrm`
(VRM 0.x / 1.0) importer that normalizes into a canonical model and authors a
typed USD stage, plus the reliability tooling around it. This entry will be
finalized when `v0.1.0` is tagged.

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
[roadmap](docs/ROADMAP.md) non-goals and design policy §15/§19):
- Full/pixel-perfect MToon shading reproduction across renderers.
- A VRM exporter.
- SpringBone / physics runtime simulation.
- Mocopi or other live input streaming.
- Coverage of every glTF extension.
- ABI stability guarantees across all OpenUSD versions (see
  [`docs/SUPPORTED_CONFIGURATIONS.md`](docs/SUPPORTED_CONFIGURATIONS.md)).

[Unreleased]: https://github.com/animu-sphere/usd-vrm-plugins/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/animu-sphere/usd-vrm-plugins/releases/tag/v0.1.0
