# Delivery history

The granular delivery log for everything landed so far: the importer build-out,
the schema contract, the package resolver, the workspace split, and the
reliability/release tooling. It is **historical evidence**, reorganized out of
the original `ROADMAP.md` so the [roadmap](../roadmap/) holds only incomplete
work.

It spans two release states. [v0.1.0](../releases/v0.1.0.md) shipped §A, §B, §C,
and §F as a single `usdVrm` bundle. The workspace split (§D, §E) and the negative
corpus (§G) landed **after** that tag and are unreleased.

This is not a description of current behavior — see [architecture/](../architecture/)
and [reference/](../reference/) for that — nor of planned work, which is in the
[roadmap](../roadmap/). Per-version summaries are in [releases/](../releases/).

Legend: ✅ done

---

## A. Importer capabilities

Shipped in `usdVrmFileFormat` (the bundle was named `usdVrm` for most of this
work; it was renamed in workspace Phase 4, §E).

- ✅ **Read + canonicalize.** `.vrm` GLB read, VRM 0.x / 1.0 detection, and all
  version differences absorbed into `VrmCanonicalDocument` before any USD is
  authored.
- ✅ **Geometry / materials.** Meshes (points/normals/UV/indices), non-skinned
  node transforms preserved, `UsdPreviewSurface` materials with the full texture
  set (base color, metallic-roughness, normal, emissive, occlusion; wrap modes,
  `KHR_texture_transform`), and MToon metadata (`vrm:mtoon:raw`, shader tagged
  MToon).
- ✅ **Skeleton / skinning.** One `UsdSkelSkeleton` unified across all glTF
  skins, topologically ordered, bind from **inverse bind matrices**;
  `UsdSkelBindingAPI`.
- ✅ **Animation.** glTF skeletal animation → `UsdSkelAnimation` (joint TRS
  clips). Morph-weight animation is not authored.
- ✅ **Front-direction bake.** VRM 0.x −Z front baked into the data
  (mesh/normals/blendshapes/skeleton/clips), so every avatar shares one canonical
  +Z rest pose and a single shared clip library drives all of them. Provenance
  kept in `customData` (`vrm:sourceFrontAxis`, `vrm:frontAxisNormalized`).
  *(design policy §7)*
- ✅ **Lossless preservation + diagnostics.** `vrm:meta` / `specVersion` /
  `vrm:rawExtension` and import warnings (`/Asset.customData.vrm:warnings`).

## B. Schema contract (contract v1)

- ✅ **Every `/Asset/rig/*` control prim is typed:** `VrmHumanoidAPI`,
  `VrmExpressionAPI`, `VrmLookAtAPI`, `VrmSpringBoneAPI`, `VrmColliderAPI`,
  `VrmConstraintAPI`. Raw VRM blocks are still preserved in `customData` as the
  lossless fallback. *(design policy §5)*
- ✅ **Schema specification + versioning policy**
  ([`SCHEMA_CONTRACT.md`](../../plugins/vrmSchema/docs/SCHEMA_CONTRACT.md));
  stages and reports carry `schemaContractVersion = 1`.
- ✅ **Raw-extension ↔ typed-API correspondence table**, schema-compatibility
  tests, and public validator-rule docs.
- ✅ **Two design-policy divergences reconciled** — contract v1 freezes the
  shipped shapes: spring bones stay under `/Asset/rig/SecondaryMotion/*` (not
  policy §4's `/Asset/physics/*`), and humanoid stays per-bone
  `vrm:humanBones:<bone>` token attributes (not policy §5's
  `token[] vrm:humanBoneNames` + relationship sketch).

Schema sources and generation moved to the standalone `vrmSchema` bundle in
workspace Phase 1 (§E); `usdVrmFileFormat` now depends on schema contract
version 1 rather than compiling the types itself.

## C. Package resolution

*Goal, met: hand an import to another machine and textures still resolve, with
no temp-dir dependency (design policy §8).*

- ✅ **`ArPackageResolver` for `.vrm` containers.** Embedded PNG/JPEG textures
  are authored as `avatar.vrm[images/<hash>.<ext>]` and served directly from the
  source container, removing the OS-temp extraction dependency for normal
  imports.
- ✅ **Malformed / truncated / out-of-range byte requests rejected.**
- ✅ **Texture export bundle** + portable asset-path policy + a `package`
  command (`tools/package_vrm.py`: package-local `textures/` + relative USD
  asset paths).
- ✅ **Asset inventory** output (`package_report.json`).

Split into the standalone `usdVrmPackageResolver` bundle in workspace Phase 3
(§E), with package-path semantics unchanged.

## D. Shared container library

- ✅ **`vrmContainer`** extracted as a plain CMake shared library: GLB
  header/chunk parsing, buffer-view access, byte-range validation, immutable
  byte views.
- ✅ **Deliberately not a plugin bundle** — no `plugInfo.json`, no
  registration, no OpenUSD types in its public API, enforced by a repo check.
- ✅ Consumed by both `usdVrmFileFormat` and `usdVrmPackageResolver`, each with
  a binary link check (`dumpbin`/`nm`) proving it imports `vrmContainer` and not
  the other bundles' libraries.

## E. Workspace migration (Workspace Phases 0–4)

Contract: [`architecture/WORKSPACE.md`](../architecture/WORKSPACE.md). Every
phase gate compared authored stages against the Phase 0 baseline; a phase that
changed a baseline artifact was a regression by definition.

| Workspace Phase | Deliverable | Landed as |
| --- | --- | --- |
| 0 | Baseline snapshots + regression criteria | `tests/baseline/` |
| 1 | `vrmSchema` bundle split | `plugins/vrmSchema` |
| 2 | `vrmContainer` extraction | `libs/vrmContainer` |
| 3 | `usdVrmPackageResolver` bundle split | `plugins/usdVrmPackageResolver` |
| 4 | `usdVrmFileFormat` purification / rename | `plugins/usdVrmFileFormat` |

- ✅ **Baseline freeze** — per-fixture USDA snapshots, schema contract snapshot,
  plugin discovery results, public symbol lists, clean-install results, and the
  diagnostics code table, generated and verified by `tools/baseline_freeze.py`.
- ✅ **Dependency direction enforced** — `ost plugin test --workspace` validates
  the bundle graph declared via `requires.bundles` / `requires.libraries` with
  stable `WORKSPACE_*` issue codes before any bundle's verification.
- ✅ **`usdVrm` retired as a bundle id** — it names the aggregate product only.

Workspace Phases 5 (packaging) and 6 (`execVrm`) are **not** done; they are in
the [roadmap](../roadmap/).

## F. Reliability and release tooling

- ✅ **Standalone stage validator** — `tools/validate_vrm.py`: a validation
  contract over the imported stage (default prim, `/Asset`,
  skinned-mesh→skeleton binding, parent-before-child joint order, `JOINTS_0`
  range, material/texture/humanoid/expression/spring targets, raw↔typed source
  mapping). Runs over an already-imported stage and never re-reads the `.vrm`,
  keeping it separate from the importer (design policy §10, §12). Emits typed
  diagnostics; non-zero exit on ERROR/FATAL.
- ✅ **Error/warning code taxonomy** (Fatal/Error/Warning/Info) replacing
  free-text warnings. Stable `VRMxxx` codes, canonical severity catalog in
  `tools/vrm_diagnostics.py`, reference table in
  [`DIAGNOSTICS.md`](../../plugins/usdVrmFileFormat/docs/DIAGNOSTICS.md).
- ✅ **Compatibility report** (`tools/vrm_report.py`) — human-readable + machine
  JSON, merging coded import-time diagnostics with the validator's findings plus
  a feature-presence matrix.
- ✅ **Versioning + changelog + release-note template.** Repo-root `VERSION` is
  the single source (CMake reads it); notes render from the tagged version's
  changelog section and fail if that section is not finalized.
- ✅ **Release workflow** (`.github/workflows/release.yml`, hand-authored): tag
  `vX.Y.Z` → three OS bundles (lean + split debug symbols) from digest-pinned
  runtimes, source archive, `SHA256SUMS`, draft GitHub release. Gates on
  tag == `VERSION`, digest-reproducible packaging, packaged-artifact
  verification (`ost plugin test --from-package`), and the clean-install smoke on
  all three OS. Each bundle carries a `buildInfo.json` stamp (commit / toolchain
  / OpenUSD / build type / schema contract version).
- ✅ **Install guide + clean-environment install test.** `clean_install_smoke.py`
  packages, extracts to a fresh dir outside the repo, and asserts
  discovery/open/validate/texture-resolution against the packaged artifacts.
- ✅ **OS axis in CI** — the PR lane runs `windows-2022`, `macos-15`, and
  `ubuntu-24.04`, each build → `ost plugin test` → package on hosted runners
  against digest-pinned cy2026 runtimes.
- ✅ **Coordinate/precision unit tests** locking `util/TransformUtil`
  conversions.

## G. Test corpus foundation

- ✅ **Corpus reorganized** into `spec-samples/` (vendored) · `vroid/` (fetched,
  git-ignored) · `conformance/` · `generated/`, with a machine-readable
  `manifest.json` (provenance + SHA-256 + roles + feature tags + expected
  diagnostics) driving `test_usdvrm_corpus.py`. Selection policy in
  [`CORPUS.md`](../../plugins/usdVrmFileFormat/tests/corpus/CORPUS.md).
- ✅ **Vendored avatars:** Seed-san (VirtualCast) and VRM1 Constraint Twist
  (pixiv), both VRM 1.0 with `allowRedistribution: true`.
- ✅ **Negative corpus:** `generated/malformed/` holds nine deliberately-broken,
  license-clean `.vrm` authored by `tools/generate_negative.py`, each pinning one
  importer diagnostic via `negative-manifest.json` + `test_usdvrm_negative.py`.
  This added coded diagnostics `VRM003` (container unreadable, FATAL), `VRM111`
  (skin joint index out of range), `VRM141` (duplicate humanoid bone), `VRM151`
  (expression morph index out of range), and `VRM190` (spring collider-group
  index out of range) — the importer's prior silent sanitizations now emit a
  stable code.

Remaining corpus axes (VRM 0.x, VRoid, animation clips, KTX2, multi-skin) are in
the [roadmap](../roadmap/backlog.md).
