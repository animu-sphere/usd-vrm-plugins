# Current

The next release's open conditions and the work still open around them.
**Shipped work is not repeated here**: what landed since the last tag is in the
[CHANGELOG](../../CHANGELOG.md)'s `[Unreleased]` section, and what each
release shipped and did not close is in its [release record](../releases/).
Which release carries a track is the
[status table](README.md#status-at-a-glance).

Legend: ✅ done · 🚧 in progress · ⬜ not started · ⛔ blocked · ⚠️ accepted workaround

## Next: v0.10.0 — canonical materials reach Hydra 🚧

**Release boundary:** the canonical material semantics this repository
imports reach a Hydra renderer as data, including when an expression drives
them over time. VRM 0.x and 1.0 materials carry one canonical record
(`VrmMaterialAPI`, `VrmMToonAPI`, `VrmTextureInfoAPI:<role>`), both
realizations (`/preview`, `/mtlx`) are generated from it, and `vrmImaging`
exposes it to Hydra under the frozen `vrm` locators, with per-field
invalidation. The release closes the [imaging track](imaging-track.md)
(Steps I0–I5) and ships as the product plus a `lookdev` package of
`vrmImaging` that a Formation pins from a registry.

**Why this number, and why this line** (decided 2026-09-27): v0.10.0 ships
what has landed since v0.9.0 — the `[Unreleased]` section of the
[CHANGELOG](../../CHANGELOG.md) — and stops at `vrmImaging`. Steps I4 and I5
land before the tag, and nothing else is added. Every other track keeps what
it has on `main` and continues after the tag:

| Track | In v0.10.0 | After v0.10.0 |
| --- | --- | --- |
| Hydra imaging (`vrmImaging`) | Steps I0–I5: the whole track | — |
| MToon canonical semantics (Product P5) | Steps 3–7 (Steps 1–2 shipped earlier) | Step 8 is `hydra-toon`'s; the image regression test the P5 goal names |
| Native USD export (`vrm_export`) | Step 1 | Steps 2–4 |
| The motion split | complete: the product builds VRM and VRMA only | — |
| `ExecIr`, release closure, carried items | — | unscheduled |

### Work before the tag

Each item's detail and its done-when are in the
[imaging track](imaging-track.md); this table is the order.

| # | Item | Where | Status |
| --- | --- | --- | --- |
| 1 | **I4 — an expression bake reaches Hydra as time.** A `.vrm` fixture's expression colour, baked by `motion_retarget` onto the Material's `inputs:vrm:*`, is read through `vrmImaging` at each baked time, and a time move dirties that field's `vrm` locator (and a reading network parameter), never `material` and no geometry locator. | this repository | ✅ `workspace_expression_bake_imaging` |
| 2 | **I4 — a live `hydra-toon` slot.** The same bind on an MToon material (`expressions_mtoon.vrm`), baked and played in `hydra-toon`'s host session, changes the MToon slot, and no geometry, draw packet or pipeline is rebuilt, recorded as a renderer report. | `hydra-toon` | ⬜ |
| 3 | **I5 — plugin discovery in CI.** A suite in every workspace cell shows `PlugRegistry` discovering `vrmImaging` and UsdImaging constructing each adapter from `plugInfo.json` alone (policy §16, §17.1). | this repository | ⬜ |
| 4 | **I5 — invalidation regression.** Policy §17.4's cases, each held by a suite: the `matcap` texture edit is the one not yet asserted. | this repository | ⬜ |
| 5 | **I5 — a `.vrm` integration fixture.** The committed MToon fixtures imported by `usdVrmFileFormat` and read through `vrmImaging`, compared with the source semantics (policy §17.3). | this repository | ⬜ |
| 6 | **I5 — the suites against the packaged plugin.** The scene-index suites run against the plugin `ost plugin package` produced, in the release lane, where packaging runs. | this repository | ⬜ |
| 7 | **I5 — an OpenUSD compatibility statement** in [SUPPORTED_CONFIGURATIONS.md](../reference/SUPPORTED_CONFIGURATIONS.md): the scene-index path only, the runtime's usdImaging SDK, the version measured. | this repository | ⬜ |
| 8 | **I5 — numbered diagnostics** (policy §18), under the existing `VRMxxx` catalog; the proposal is in the track, and it is the user's call. | this repository | ✅ `VRM300`, `workspace_validate_imaging` |

### Cutting the release

In this order; each step is its own pull request unless it says otherwise.

1. ⬜ **Items 1–8 merged**, the I4 renderer report merged in `hydra-toon`.
2. ⬜ **Preparation:** the lockstep bump to `0.10.0` (`VERSION`,
   `openstrata.toml`, every descriptor `version:`, every `requires` range to
   `>=0.10,<0.11`, every CMake standalone fallback), the CHANGELOG's
   `[0.10.0]` section, the [release record](../releases/) `v0.10.0.md`, and
   this heading turned to `Shipped:`. Gate: the workspace graph, the suite,
   and `ost plugin package --workspace --product` on this workstation.
3. ⬜ **Dry run:** `gh workflow run release.yml --ref main` green on every
   cell — three workspace cells, two `lookdev` cells, the publish job. This is
   also the proof still owed by the reduced product:
   - ⬜ **The aggregate product installs and opens a `.vrm` and a `.vrma` from
     release artifacts, with the consumed packages resolved as
     dependencies.** Only a release run can show it: `release.yml` is
     hand-authored and no pull request executes it.
4. ⬜ **Tag `v0.10.0` and publish the draft** — the user's decision, after
   the dry run.
5. ⬜ **After the tag:** make the GHCR package
   `ghcr.io/animu-sphere/usd-vrm-plugins` public and pull a `vrmImaging`
   package anonymously; that closes Step I5's published package. Then
   `hydra-toon` re-pins its VRM Formation to the published digests, in its
   own repository.

## Release closure and checkable invariants

- ⬜ **One release-closure checklist**, generated or checked from the
  manifests, covering the workspace graph, a standalone bundle build, the
  aggregate package, an installed-consumer configure, the artifact-only smoke
  and installed data resolving — and run where a release runs, which a pull
  request does not ([ost report 39](../reports/ost/39-2026-09-01-v0.22.8-release-lane-first-execution.md)).
  The expectation is [scope policy §8](../design/INTEGRATION_SCOPE_POLICY.md#8-release-quality-is-artifact-closure).
- ⬜ **Make the review checklist checkable where it is cheap.** Inventory
  [scope policy §11](../design/INTEGRATION_SCOPE_POLICY.md#11-pr-review-checklist)
  against what is enforced, and record which invariants stay review
  convention on purpose ("a node is a wrapper" is not a grep).
- ⬜ **Separate the workspace contract from its history.**
  [WORKSPACE.md](../architecture/WORKSPACE.md) §9 records where every moved
  identity went, and §8 the Workspace phases; both are history inside a
  binding contract. Moving them to the [archive](../archive/) means migrating
  the section citations that documents here and in two sibling repositories
  make, so it is one change with the citations updated in it.

## Carried over from shipped releases

### Carried out of v0.9.0

- ⬜ **Rewrite or drop the build machine's RPATH in packaged Linux and macOS
  binaries.** Every `.so` / `.dylib` and tool keeps the runtime directory it
  was built against, and three plugins keep the build tree's
  `workspace-prefix/lib`. Nothing loads from them on a user's host, but a host
  where one exists searches it first. The fix is in packaging (upstream `ost`,
  or a CMake `INSTALL_RPATH` of `$ORIGIN` / `@loader_path` with the build-tree
  tests adjusted). `artifact_only_exec_smoke.py` counts them today, and should
  fail on them once fixed.

### Carried out of v0.8.0

- ⬜ **Freeze the Linux and macOS symbol baselines.** `tests/baseline/symbols/`
  holds `windows-x86_64.txt` only, and `--check` skips a platform with no
  committed file. Run `tools/baseline_freeze.py --update` on a Linux and a
  macOS host and commit the result.
- ⬜ **Reinstate a real-runtime compatibility lane.** It needs a real runner, a
  republished plugin artifact and a re-added `lane: scheduled` cell
  ([report 38](../reports/ost/38-2026-08-30-v0.22.8-workspace-cell-verbs-and-orphaned-lanes.md)
  §5).
- ⬜ **Three bundle cells exist to reach a verb, not a bundle.** A
  `kind: workspace` cell cannot spell `ost plugin test --workspace` or
  `ost plugin package --workspace`, so `usdVrmFileFormat` keeps one per-bundle
  cell per platform. Closing it is upstream (`verify: pyramid`,
  `verify: package`), and would take the matrix to four cells (report 38 §2).
- 🚧 **`ost` cannot tell us a workspace member ran no tests.**
  `workspace_ctest_labels` fails when any member suite registers nothing. That
  covers the workspace cells and not a standalone per-bundle cell, and it
  counts registrations rather than tests run, so per-member attribution in
  `ost test --json` is still asked upstream (report 38 §4).
- ⚠️ **`release.yml` is hand-authored** and hand-mirrors the CI contract's X11
  step, `ost` pin and runtime digests; `check_docs.py` compares the three pin
  sites, and a green PR lane still proves nothing about the release lane.
  Adopting `ost`'s `release:` contract is the fix and is not scoped.
- ⚠️ **The last bundle in `release.yml`'s build loop decides every package's
  libraries.** `scripts/check_product_libraries.py` fails by library and
  bundle when that stops covering a package. The fix is upstream
  ([ost report 40](../reports/ost/40-2026-09-13-v0.22.10-one-workspace-prefix-for-every-bundle.md)
  P1).

Operator evidence carried out of v0.7.0 — a VMC relay session, a device
recovery, a labelled rolled VRChat OSC take, a redistributable mocopi capture,
and the live path from release artifacts — is about live input, and is
[`motion-connectors`' roadmap](https://github.com/animu-sphere/motion-connectors/blob/main/docs/roadmap/current.md)'s.

## Standing: product tracks with work still open

Only the open items are listed.

### Product P0 — documentation & implementation sync 🚧

*Goal: no contradiction between the docs and the code; a new user understands
the workspace layout, the output structure, and the import/runtime boundary.*
(design policy §15, §17-P0)

- ✅ Documentation consolidated around one owner per subject (2026-09-25):
  generic motion and input link to their owning repositories, completed plans
  are [archived](../archive/), and superseded design documents are stubs
  ([contributing/documentation.md](../contributing/documentation.md)).
- 🚧 Align build / test / install examples with what CI actually runs.
- ⬜ The workspace contract apart from its history
  ([above](#release-closure-and-checkable-invariants)).

Done when: the component table matches the manifests, no document describes
`usdVrm` as a bundle id, every local link resolves, and a consistency check
guards all of it in CI.

### Product P1 — release stabilization 🚧

- ⛔ **A second OpenUSD version cell** (min vs latest). Today CI runs cy2026 /
  OpenUSD 26.08 only. **Blocked externally:** no min-version runtime artifact
  is published yet — this needs an open-strata runtime build and publish per
  OS, then a fourth cell in `openstrata.ci.yaml`.

### Product P3 — runtime verification 🚧

The OS axis, the workspace graph gate, Unicode paths, Windows DLL discovery,
the real-VRM smoke and the non-`ost` activation path are covered. Remaining:

- ⬜ **The archive is still extracted with `ost plugin product install`**, and
  the per-bundle archives composed by hand, which share the product's layout,
  are not run by the artifact-only smoke.

## Workspace Phase 5 — per-bundle + aggregate packaging 🚧

**Status:** aggregate product shipped; the standalone dependency-registration
P0 is blocked on `ost` · **Contract:**
[WORKSPACE.md](../architecture/WORKSPACE.md) §5

- ⛔ **A dependency bundle's USD registration half is never staged.** `ost`
  stages `libvrmSchema` + its CMake package into `runtime/libraries/`, but not
  `plugInfo.json` or `generatedSchema.usda` — so a packaged importer links
  against schemas it can no longer register, and a bare per-bundle
  `--from-package` fails at L3/L4. This is the **P0 upstream ask**, and it is
  why the release ships all three VRM bundles
  ([report 25](../reports/ost/25-2026-07-18-v0.18.0-from-package-workspace-correction.md)).
- ⬜ **Retire the hand-rolled closure in `scripts/clean_install_smoke.py`.**
  It remains the release lane's packaged-artifact gate. Needs the P0 above.
