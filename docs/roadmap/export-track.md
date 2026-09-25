# The native USD export track

**Status:** ⬜ not started · **Target:** unscheduled
([status table](README.md#status-at-a-glance)) ·
**Policy:** [export policy](../design/VRM_EXPORT_POLICY.md) §11

`vrm_export`: an imported `.vrm` written as a native `.usda`, `.usdc` or
`.usdz` that opens with no VRM plugin installed. What the tool is, where it
sits and what it may depend on is the policy's; this document holds what is
left to build. The steps are this track's internal order, not Product P0–P3.

## 1. Where it stands

- The whole Step 1 pipeline has been prototyped in Python against the pinned
  runtime, and it holds (policy §2): the localized `.usdz` of `Seed-san.vrm`
  passes all five `usdUtilsValidators` and opens with no VRM plugin
  registered.
- The importer's Python
  [`package_vrm.py`](../../plugins/usdVrmFileFormat/tools/package_vrm.py)
  already writes a loose `.usda` + `textures/`. It is the predecessor, and
  what becomes of it is policy q1.

## 2. Steps

### Step 1 — native export ⬜

- ⬜ `tools/vrmExport/`: the `vrm_export` executable, with the export
  operation as a target of its own, separate from `main.cpp` (policy §4).
- ⬜ `.usda` / `.usdc` / `.usdz` from the output extension; `.vrm` input only
  (policy §5–§6).
- ⬜ Dependency localization into `textures/<fnv1a64>.<ext>`, anchored, next
  to a loose output or inside the package (policy §6.2).
- ⬜ `.usdz` through `UsdUtilsCreateNewUsdzPackage` from a temporary
  directory that is always removed (policy §6.3).
- ⬜ `--check`, `--overwrite`, `--verbose`, `--help`, `--version`, and the exit
  codes of policy §5.3.
- ⬜ Unit tests of the export operation: format detection, an unsupported
  extension, a non-`.vrm` input, an unreadable input, an existing output,
  error propagation.
- ⬜ Integration tests: every output format over the fixtures and the corpus —
  VRM 0.x and 1.0, textured, skinned, several materials, MToon, expressions,
  look-at, spring bones — each output reopened **in a process with no VRM
  plugin**, and no source path left in the bytes of a `.usda` (policy §7–§8).
- ⬜ Workspace wiring: `openstrata.toml` members and `release_members`, the
  root build, `check_cmake_boundaries.py`'s row, WORKSPACE.md's identity table,
  the root README, and `workspace_unicode_paths` extended to the second
  executable the workspace ships.

**Done when** `vrm_export Seed-san.vrm -o Seed-san.usdz --check` exits 0 on
every CI lane, and the result opens in a plugin-free OpenUSD session with its
textures resolved.

### Step 2 — validation and usability ⬜

- ⬜ Determinism: decide policy q2, then assert it.
- ⬜ `--flatten`, as an explicit opt-in (policy §6.1).
- ⬜ A package dependency report — possibly `package_vrm.py`'s inventory
  (policy q1).
- ⬜ Richer diagnostics.
- ⬜ The product's `vrm_export` exporting a `.vrm` from the release artifact,
  in the artifact-only smoke.

### Step 3 — motion assembly ⬜

- ⬜ `--motion <clip.vrma>`: avatar and clip composed into one artifact. The
  `.vrm` and `.vrma` file formats stay independent; `vrm_export` composes
  them. Needs policy q3 — who authors the binding layer.
- ⬜ An animated `.usdz`.

### Step 4 — distribution pipeline ⬜

- ⬜ Package profiles (an ARKit-constrained `.usdz` would be one).
- ⬜ Optional source preservation (`--keep-source`, policy §6.4).
- ⬜ Package metadata.
- ⬜ Generic helper extraction to OpenStrata, only if a second workspace needs
  the same workflow (policy §10).
