# Current

The next release's open conditions and the work still open around them.
**Shipped work is not repeated here**: what landed since the last tag is in the
[CHANGELOG](../../CHANGELOG.md)'s `[Unreleased]` section, and what each
release shipped and did not close is in its [release record](../releases/).
Which release carries a track is the
[status table](README.md#status-at-a-glance).

Legend: ✅ done · 🚧 in progress · ⬜ not started · ⛔ blocked · ⚠️ accepted workaround

## Shipped: v0.9.0 — the OpenExec foundation

The last release ([record](../releases/v0.9.0.md)). The next one has no
number yet; it takes one when it is cut. What v0.9.0 left open is
[below](#carried-out-of-v090).

## The next release

Since v0.9.0 the product builds VRM and VRMA only and consumes generic motion
as installed packages ([WORKSPACE.md](../architecture/WORKSPACE.md) §1). That
reduced product has passed every pull-request lane and has not yet been
released.

- ⬜ **The aggregate product installs and opens a `.vrm` and a `.vrma` from
  release artifacts, with the consumed packages resolved as dependencies.**
  Only a release run can show it: `release.yml` is hand-authored and no
  pull request executes it. The next release's `workflow_dispatch` dry run is
  the proof.

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
