# Current

The next milestone and active carry-over work. Shipped detail is in the
[delivery history](../reports/delivery-history.md).

Legend: 🚧 in progress · ⬜ not started · ⛔ blocked

## Next milestone: the first workspace release

**Status:** 🚧 in progress · **Depends on:** the workspace split through
Workspace Phase 4 (shipped) and the release workflow (shipped).

[v0.1.0](../releases/v0.1.0.md) is **released** — tagged and published
2026-07-11 — and shipped the pre-split single `usdVrm` bundle. Everything since
(the `vrmSchema` split, `vrmContainer` extraction, the resolver split, and the
`usdVrm` → `usdVrmFileFormat` rename) is unreleased.

[v0.2.0](../releases/v0.2.0.md) is therefore the first to ship the multi-bundle
workspace, and it is **artifact-breaking**: consumers of the `usdVrm-0.1.0-*`
asset names must move to `usdVrmFileFormat-0.2.0-<target>` **and** additionally
install `vrmSchema` and `usdVrmPackageResolver`.

- ✅ **`VERSION` bumped to `0.2.0`**, with all four manifests and their
  `requires` ranges (`>=0.1,<0.2` → `>=0.2,<0.3`) moved together.
- ✅ **Changelog `[0.2.0]` finalized** and verified to render.
- ⬜ **Dry run → tag → publish the draft.**

### Product P0 — documentation & implementation sync 🚧

*Goal: no contradiction between the docs and the code; a new user understands
the workspace layout, the output structure, and the import/runtime boundary.*
(design policy §15, §17-P0)

The importer-era docs described a single `usdVrm` bundle with co-located
schemas. Since Workspace Phase 4 that is wrong in every particular.

- 🚧 Describe `vrmSchema`, `usdVrmFileFormat`, and `usdVrmPackageResolver` as
  separate bundles, `vrmContainer` as a plain library, and `usdVrm` as the
  aggregate product name only.
- 🚧 Unify phase notation to **Product P0–P6**, **Workspace Phase 0–8**, and
  **Motion Phase A–H** — three sequences, never a bare "Phase N".
- 🚧 Align build / test / install examples with what CI actually runs.
- 🚧 Adopt the house documentation taxonomy shared with `open-strata` and
  `hydra-merlin`.

Done when: the component table matches the manifests, no document describes
`usdVrm` as a bundle id, every local link resolves, and a consistency check
guards all of it in CI.

### Product P1 — release stabilization ⬜

- ✅ **Decided: the release ships all three bundles.** `release.yml` now builds,
  tests, packages (`ost plugin package --workspace`), and publishes `vrmSchema`,
  `usdVrmFileFormat`, and `usdVrmPackageResolver` per target. This was forced,
  not preferred: an `usdVrmFileFormat` package alone registers the `.vrm` format
  and then **fails to open a stage** (L3/L4, `Used null prim`), because ost
  0.18.0 stages a dependency bundle's link half without its USD registration
  half. Measured in [report 23 §2.1](../reports/ost/23-2026-07-18-v0.18.0-workspace-packaging-v0.19.0-asks.md).
- ✅ **Replaced the packaged-artifact gate.** `ost plugin test --from-package`
  takes a single bundle and is incompatible with `--workspace`, so it can only
  test the configuration that fails. The lane now gates on the composed
  `scripts/clean_install_smoke.py`, which opens and validates real models from
  the packaged artifacts.
- ⬜ **Dry-run, tag, and publish:** dry run (`workflow_dispatch`) → tag →
  publish the draft.
- ⛔ **A second OpenUSD version cell** (min vs latest) in the compatibility
  matrix. Today CI runs cy2026 / OpenUSD 26.05 only. **Blocked externally:**
  GHCR has no published min-version (e.g. OpenUSD 25.05 / cy2025) runtime
  artifact yet — this needs an open-strata runtime build + publish per OS, then
  a fourth cell in `openstrata.ci.yaml`. The OS axis already runs three cells.

### Product P3 — runtime verification (carry-over) ⬜

*Goal: builds and opens are continuously verified on all three OS; textured real
models resolve; schema registration succeeds.* (design policy §14, §17-P3)

The OS axis is shipped. Remaining:

- ⬜ **Wire the workspace graph gate into CI.**
  [WORKSPACE.md §2](../architecture/WORKSPACE.md) specifies
  `ost plugin test --workspace` as the enforcement for the dependency
  directions, and §8 called for it to be a required PR-lane gate from Workspace
  Phase 1 on — but **no lane runs it**. The generated PR lane runs
  `ost plugin test <bundle>` per cell (nine cells: three bundles × three OS).
  The directions are currently enforced only by the per-bundle binary link
  checks and `vrmContainer`'s boundary check, which catch a bad *link* but not
  a bad *manifest edge*. The command works locally today.
- ⬜ Explicit **UTF-8 / Unicode path** and **DLL dependency discovery** coverage
  on the Windows cell.
- ⬜ **Real VRM smoke test** (open + texture resolve) exercised in CI, not just
  fixtures.

## Workspace Phase 5 — per-bundle + aggregate packaging ⛔

**Status:** ⛔ blocked on `ost` · **Contract:**
[WORKSPACE.md](../architecture/WORKSPACE.md) §5

`ost` 0.18.0 moved this forward but did not unblock it. What landed:
`ost plugin package --workspace` (adopted by the release lane), and a `bundles`
key in `dependencies.json`. What did not:

- **A dependency bundle's USD registration half is never staged.** 0.18.0 stages
  `libvrmSchema` + its CMake package into `runtime/libraries/`, but not
  `plugInfo.json` or `generatedSchema.usda` — so the packaged importer links
  against the schemas it can no longer register, and `--from-package` fails at
  L3/L4. This is now the **P0 upstream ask**; it is also why the release must
  ship all three bundles.
- **No aggregate product artifact.** `--workspace` emits three archives, not
  `usdVrmPlugins-<version>-<target>.tar.zst`.
- **`--from-package` still incompatible with `--workspace`**, so no ost verb can
  verify the configuration we ship.

`scripts/clean_install_smoke.py` therefore still hand-rolls the closure and is
now the release lane's packaged-artifact gate. The asks are re-filed in
[report 23 §6](../reports/ost/23-2026-07-18-v0.18.0-workspace-packaging-v0.19.0-asks.md)
against `ost` v0.19.0.

- ✅ Adopt `ost plugin package --workspace`.
- ⬜ Emit the aggregate artifact and gate it with `--from-package --workspace`.
- ⬜ Retire the hand-rolled closure in `clean_install_smoke.py`.
