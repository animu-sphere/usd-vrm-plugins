# The native USD export track

**Status:** 🚧 in progress — Steps 1–3 implemented; Step 3 hosted proof pending · **Target:** v0.10.0 carries Step 1;
Steps 2–4 unscheduled ([status table](README.md#status-at-a-glance)) ·
**Policy:** [export policy](../design/VRM_EXPORT_POLICY.md) §11

`vrm_export`: an imported `.vrm` written as a native `.usda`, `.usdc` or
`.usdz` that opens with no VRM plugin installed. What the tool is, where it
sits and what it may depend on is the policy's; this document holds what is
left to build. The steps are this track's internal order, not Product P0–P3.

## 1. Where it stands

- **Step 1 is implemented** ([`tools/vrmExport`](../../tools/vrmExport/README.md),
  2026-09-26): `.usda`, `.usdc` and `.usdz` from 16 fixtures and corpus
  avatars, each reopened in a process with no VRM plugin and equal to its
  source prim by prim and texture by texture.
- The importer's Python
  [`package_vrm.py`](../../plugins/usdVrmFileFormat/tools/package_vrm.py)
  already writes a loose `.usda` + `textures/`. It is the predecessor, and
  stays as the importer's development tool (policy q1, decided 2026-10-26).
- **Step 2 is implemented:** validation and usability, including the
  installed product's exporter measured on Windows, Linux and macOS arm64
  in the [2026-10-29 release dry run](../reports/2026-10-29-export-release-dry-run.md).

## 2. Steps

### Step 1 — native export ✅

- ✅ `tools/vrmExport/`: the `vrm_export` executable, with the export
  operation as a target of its own, separate from `main.cpp` (policy §4).
- ✅ `.usda` / `.usdc` / `.usdz` from the output extension; `.vrm` input only
  (policy §5–§6).
- ✅ Dependency localization into `textures/<hash>.<ext>`, anchored, next
  to a loose output or inside the package (policy §6.2).
- ✅ `.usdz` through `UsdUtilsCreateNewUsdzPackage` from a temporary
  directory that is always removed (policy §6.3).
- ✅ `--check`, `--overwrite`, `--verbose`, `--help`, `--version`, and the exit
  codes of policy §5.3.
- ✅ Unit tests of the export operation: format detection, an unsupported
  extension, a non-`.vrm` input, an unreadable input, an existing output,
  error propagation.
- ✅ Integration tests: every output format over the fixtures and the corpus —
  VRM 0.x and 1.0, textured, skinned, several materials, MToon, expressions,
  look-at, spring bones — each output reopened **in a process with no VRM
  plugin**, and no source path left in the bytes of a `.usda` (policy §7–§8).
- ✅ Workspace wiring: `openstrata.toml` members and `release_members`, the
  root build, `check_cmake_boundaries.py`'s row, WORKSPACE.md's identity table,
  the root README, and `workspace_unicode_paths` extended to the second
  executable the workspace ships.

**Done when** `vrm_export Seed-san.vrm -o Seed-san.usdz --check` exits 0 on
every CI lane, and the result opens in a plugin-free OpenUSD session with its
textures resolved. `vrm_export_end_to_end` is that claim, over every fixture.

Not covered by Step 1's tests: a `.vrm` whose image is an **external** file
rather than embedded. Step 2 covers it.

### Step 2 — validation and usability ✅

- ✅ Determinism: policy q2 decided (byte-identical across hosts and time
  zones of one CPU architecture), the staged files' times pinned, a
  re-export asserted equal byte for byte, and two fixtures' packages held to
  a committed digest per architecture (policy §9).
- ✅ A `.vrm` with external (non-embedded) images, so the other half of
  localization is tested: written by `vrm_export_end_to_end` at run time, held
  to everything the committed fixtures are, and refused with exit 4 once an
  image is taken away.
- ✅ `--flatten`, as an explicit opt-in (policy §6.1), with no source-file
  comment.
- ✅ A package dependency report, `vrm_export`'s own (2026-10-29):
  `--dependency-report` emits the portable JSON inventory of policy §8.1.
  Tests compare every format's paths and byte counts with the written files,
  including embedded/external image deduplication and repeatability.
  Policy q1 keeps `package_vrm.py` as the importer's development tool.
- ✅ Richer diagnostics (2026-10-29): unreadable dependencies name their
  referring USD attributes and anchored paths, with separate checks for
  external images and embedded-image resolver registration. Destination
  failures name the blocked path and recovery checks; validation lists every
  unresolved dependency and locates non-portable asset attributes. Operation
  messages escape line breaks and tabs; exit codes are unchanged.
- ✅ The product's `vrm_export` exporting a `.vrm` from the release artifact,
  in the artifact-only smoke: `scripts/artifact_only_export_smoke.py`, a step
  of `release.yml`'s workspace lanes. The
  [2026-10-29 release dry run](../reports/2026-10-29-export-release-dry-run.md)
  passed on Windows, Linux and macOS arm64, including plugin-free readback
  and refusal with the product's importer registration removed.

### Step 3 — motion assembly 🚧

- ✅ `--motion <clip.vrma>`: avatar and clip composed into one artifact. The
  `.vrm` and `.vrma` file formats stay independent; `vrm_export` composes
  them. Policy q3 is settled for offline export: invoke the shipped
  `motion_retarget` bake, then replace its avatar reference with a native,
  content-named layer. Live binding vocabulary remains outside this decision.
- ✅ An animated `.usdz`, with the avatar layer and textures inside it;
  `--flatten` instead writes one composed layer. Loose `.usda` / `.usdc`
  retain the same composition and portable dependencies.
- ✅ `vrm_export_motion` measured on Windows: five body/expression/gaze and
  VRM 0.x/1.0 cases, each in all three formats with and without flattening.
  Plugin-free keyed attributes, resolved joint transforms, metrics and
  texture bytes equal the existing bake, including after hiding the sources.
  Reports count the native layer too; repeat exports are byte-identical.
- ✅ The release smoke now exercises `--motion` from the installed product
  and requires a moving UsdSkel skeleton in plugin-free readback.
  The Windows local product passed with `ost 0.23.14` and an explicit
  unmanaged-output packaging override; this is not hosted release evidence.
- ⬜ Hosted Windows/Linux/macOS workspace and release-artifact measurements
  of Step 3. Step 2's dated release report predates this option.

### Step 4 — distribution pipeline ⬜

- ⬜ Package profiles (an ARKit-constrained `.usdz` would be one).
- ⬜ Optional source preservation (`--keep-source`, policy §6.4).
- ⬜ Package metadata.
- ⬜ Generic helper extraction to OpenStrata, only if a second workspace needs
  the same workflow (policy §10).
