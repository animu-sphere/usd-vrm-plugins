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
`usdVrm` → `usdVrmFileFormat` rename) is unreleased: 43 commits sit between the
tag and `main`.

The next release is therefore the first to ship the multi-bundle workspace, and
it is **artifact-breaking**: consumers of the `usdVrm-0.1.0-*` asset names must
move to `usdVrmFileFormat-<version>-<target>`.

- ⬜ **Bump `VERSION`.** It still reads `0.1.0`, which is already tagged and
  published. The release workflow gates on tag == `VERSION`, so the next release
  cannot be cut until this moves. The artifact rename argues for `0.2.0` rather
  than a patch.
- ⬜ **Retitle the changelog's `[Unreleased]` section** to the chosen version
  with a date; `make_release_notes.py` refuses to render an unfinalized section.

### Product P0 — documentation & implementation sync 🚧

*Goal: no contradiction between the docs and the code; a new user understands
the workspace layout, the output structure, and the import/runtime boundary.*
(design policy §15, §17-P0)

The importer-era docs described a single `usdVrm` bundle with co-located
schemas. Since Workspace Phase 4 that is wrong in every particular.

- 🚧 Describe `vrmSchema`, `usdVrmFileFormat`, and `usdVrmPackageResolver` as
  separate bundles, `vrmContainer` as a plain library, and `usdVrm` as the
  aggregate product name only.
- 🚧 Unify phase notation to **Product P0–P6** vs **Workspace Phase 0–6**.
- 🚧 Align build / test / install examples with what CI actually runs.
- 🚧 Adopt the house documentation taxonomy shared with `open-strata` and
  `hydra-merlin`.

Done when: the component table matches the manifests, no document describes
`usdVrm` as a bundle id, every local link resolves, and a consistency check
guards all of it in CI.

### Product P1 — release stabilization ⬜

- ⬜ **Decide what the release's binary artifacts contain.** `release.yml`
  builds, tests, and packages `plugins/usdVrmFileFormat` **only** — it does not
  mention `vrmSchema` or `usdVrmPackageResolver`. That was correct at v0.1.0,
  when the importer was one bundle with co-located schemas; since Workspace
  Phase 1 and 3 it means a published bundle cannot apply its own typed schemas
  or resolve embedded textures. Options: extend the release matrix to package
  all three bundles (independent of the aggregate artifact, which is blocked
  below), or state the limitation in the notes and make the source archive the
  supported path. The release-notes template currently states the limitation.
- ⬜ **Dry-run, tag, and publish** once the two items above are settled: dry run
  (`workflow_dispatch`) → tag → publish the draft.
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

Per-bundle packaging works today (`ost plugin package <bundle>`) and the release
lane ships it. The aggregate product artifact
(`usdVrmPlugins-<version>-<target>.tar.zst`) does not exist, and cannot be built
cleanly with `ost` 0.17.0:

- `--workspace` exists only on `ost plugin test`, not on `package`.
- A bundle package resolves `requires.libraries` (`vrmContainer` is staged under
  `runtime/libraries/` with a `dependencies.json` record) but **omits
  `requires.bundles` entirely** — an extracted `usdVrmFileFormat` package has no
  trace of the `vrmSchema` it needs.
- `--from-package` is incompatible with `--workspace`, so a composed workspace
  cannot be verified from its packaged artifacts.

Until then `scripts/clean_install_smoke.py` hand-rolls the closure with three
package/extract cycles and `ost plugin run --no-inject --plugin-path`. The asks
are filed upstream in
[report 22 §11](../reports/ost/22-2026-07-17-v0.17.0-evidence-gate-v0.18.0-asks.md)
against `ost` v0.18.0.

- ⬜ Adopt `ost plugin package --workspace` once it exists.
- ⬜ Emit the aggregate artifact and gate it with `--from-package --workspace`.
- ⬜ Retire the hand-rolled closure in `clean_install_smoke.py`.
