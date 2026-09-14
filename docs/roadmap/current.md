# Current

The next milestone and the work still open around it. **Shipped work is not
repeated here**: what landed since the last tag is in the
[CHANGELOG](../../CHANGELOG.md)'s `[Unreleased]` section, what each release
shipped and did not close is in its [release record](../releases/), and the
older history is in the [delivery history](../reports/delivery-history.md). A
milestone's own detail lives in its track document. This file carries the
boundary, what is still open, and where the detail is.

Legend: 🚧 in progress · ⬜ not started · ⛔ blocked · ⚠️ accepted workaround

## Next: v0.9.0 — the OpenExec foundation (Workspace Phase 8 + Motion Phase E) 🚧

**Release boundary:** `execMotion` and `execVrm` bundles exist and evaluate a
humanoid through OpenExec, proven equal to the offline result on the same input.
Nodes are thin wrappers over `motionRuntime` and `vrmRetarget`, never a second
implementation, and each evaluates an immutable snapshot rather than a live
source. Planned in [openexec-foundation.md](openexec-foundation.md); the gate is
its [§6, "Foundation release gate"](openexec-foundation.md#foundation-release-gate).

It took its number when v0.8.0 was cut (2026-09-01). It leads the queue because
every node is specified as a wrapper, which makes this foundation the first
consumer of those libraries that is not the tool beside them. A node that cannot
be written as a wrapper is a finding about the library API, and
[boundary consolidation](boundary-consolidation.md) comes next to act on those
findings. The re-order and what it cost are in the
[roadmap README](README.md#status-at-a-glance).

Not in this boundary: realtime skinned display (OpenUSD 26.08 resolves exec prim
adapters from a hard-coded list, so P0-7 ships an exec-computed
`UsdGeomXformable` instead), any `ExecIr` dependency, and network I/O inside a
computation, which is a permanent non-goal.

### Where it stands

Each task's record, including what each step measured, is in
[the plan's §6](openexec-foundation.md#6-foundation-tasks) and the
`docs/reports/openusd/26.08-openexec-*.md` reports it cites.

| Task | Status | What is left |
| --- | --- | --- |
| P0-1 — OpenUSD 26.08 exact pin | ✅ | — |
| P0-2 — motion layer CI | ✅ | — all seven labels assigned and read back by `workspace_ctest_labels`; the last coverage row, Windows Unicode paths, is `workspace_unicode_paths` (2026-09-15) |
| P0-3 — `motion_retarget` distribution | 🚧 | the artifact-only smoke's embedded-texture half and a build-tree scan of the tool's own process; the non-`ost` Windows install and DLL discovery (Product P3 below) |
| P0-4 — `execMotion` | 🚧 | the producer half of `motion:timeCodesPerSecond` and the `motion:filter:*` / `motion:root:*` statements, which nothing authors yet; a decision on what a one-joint clip's fallback-filled root means |
| P0-5 — `execVrm` | ✅ | — |
| P0-6 — OpenExec / offline parity | 🚧 | the values (414 598 compared, all `==`) and the diagnostics agree; left is **a decision**: whether the rows no producer reaches must be run, rather than asserted on the exec side and read on the tool's |
| P0-7 — display smoke, `UsdGeomXformable` | ✅ | — |
| P1-1 — retarget diagnostics | ✅ | — |
| P1-2 — scale policy | ⬜ | **a decision** on a rig whose *rest* is scaled: carry the rest scale in `scales`, refuse such a rig, or keep identity and state the cost (`Seed-san.vrm`: seven non-humanoid joints, at most 0.14% off unit) |
| P1-3 — partial skeleton policy | ⬜ | the seven cases as a contract |

The gate rows still open are P0-3's: **Windows DLL discovery** and the
**artifact-only offline retarget**'s texture half. Unicode paths closed on
2026-09-15.

**The boundary findings** the nodes produced — places the wrapper idiom was a
workaround, had a cost, or was ruled out — are collected in
[boundary consolidation §1](boundary-consolidation.md#findings-from-the-exec-layer-as-they-land),
which is the track that acts on them.

## Carried over from shipped releases

### Carried out of v0.8.0

What [v0.8.0](../releases/v0.8.0.md) shipped and did not close is its record's
"Known limitations". The items below are the ones with work still to do.

- ⬜ **Freeze the Linux and macOS symbol baselines.** `tests/baseline/symbols/`
  holds `windows-x86_64.txt` only, and `--check` skips a platform with no
  committed file. Run `tools/baseline_freeze.py --update` on a Linux and a
  macOS host and commit the result.
- ⬜ **Reinstate a real-runtime compatibility lane.** The scheduled lane was
  removed on 2026-08-30: its self-hosted runner never existed, and it paired a
  26.05-built plugin with a 26.08 runtime. It needs a real runner, a
  republished plugin artifact and a re-added `lane: scheduled` cell, all three
  ([report 38](../reports/ost/38-2026-08-30-v0.22.8-workspace-cell-verbs-and-orphaned-lanes.md)
  §5).
- ⬜ **Three bundle cells exist to reach a verb, not a bundle.** A
  `kind: workspace` cell cannot spell `ost plugin test --workspace` or
  `ost plugin package --workspace`, so `usdVrmFileFormat` keeps one per-bundle
  cell per platform. Closing it is upstream (`verify: pyramid`,
  `verify: package`), and would take the matrix to four cells (report 38 §2).
- 🚧 **`ost` cannot tell us a workspace member ran no tests.** Since 2026-09-15
  the root build reports it for itself: `workspace_ctest_labels` fails when any
  of the 24 member suites registers nothing. That covers the workspace cells
  and not a standalone per-bundle cell, and it counts registrations rather than
  tests run, so per-member attribution in `ost test --json` is still asked
  upstream (report 38 §4).
- ⚠️ **`release.yml` is hand-authored** and hand-mirrors the CI contract's X11
  step, `ost` pin and runtime digests; `check_docs.py` compares the three pin
  sites, and a green PR lane still proves nothing about the release lane.
  Adopting `ost`'s `release:` contract is the fix and is not scoped.
- ⚠️ **The last bundle in `release.yml`'s build loop decides every package's
  libraries.** The loop ends with `usdVrmaFileFormat`, whose closure holds
  `vrmContainer`, and `scripts/check_product_libraries.py` fails by library and
  bundle when that stops covering a package. The fix is upstream
  ([ost report 40](../reports/ost/40-2026-09-13-v0.22.10-one-workspace-prefix-for-every-bundle.md)
  P1).
- ⬜ **No lane publishes an adapter artifact.** `ost library package` produces
  one; whether a release carries it is a decision, scheduled as
  [BND-2](boundary-consolidation.md#4-bnd-2--settle-the-adapter-distribution-decision-).

### Carried out of v0.7.0 — evidence an operator produces

None of these closes by writing code.

- ⬜ **A VMC relay session, compared at the canonical layer.** Two paths of three
  agree to a median 0.084° per bone
  ([report 01](../reports/motion/01-2026-08-15-mocopi-cross-source.md)). Until a
  relay is recorded, the VMC half of the root/hips decision stays open and a
  VMC session retargets in place.
- ⬜ **A recovery a device can actually produce, or a decision that this product
  cannot.** Removing a sensor puts the mocopi app into re-tracking, so the
  stream never carries a lost sensor, and `VRM_MOCOPI_TRACKING_LOST` stays
  frozen and unraised. The source *restart* is recorded.
- ⬜ **A labelled *rolled* take — the cheapest item here.** The VRChat OSC Euler
  order is measured to three compositions of six, because nobody tilted in the
  2026-08-30 session. Twenty seconds settles it: a head tilted onto one
  shoulder and held, or a foot rolled onto its outer edge, with the side written
  down. Until then the residual is median 0.21°, 12.33° at worst
  ([report 03](../reports/motion/03-2026-08-30-vrchat-osc-tracking-space.md)
  §2.3).
- ⬜ **A redistributable mocopi capture.** The five device sessions are
  [`recorded/manifest.json`](../../adapters/liveCapture/mocopi/tests/corpus/recorded/manifest.json)
  rows with no bytes, because a session is a real person's motion. A
  publishable one needs the vendor's `BVH Sender`, not a device.
- 🚧 **The live path from release artifacts alone.** The recorded path runs from
  an installed product, profiles included (2026-08-30). The live path needs the
  product composed with an adapter's own artifact, which `ost library package`
  can produce and nothing has run.

### Carried out of Motion Phase G

Expressions, look-at and the avatar's override arbitration reach a rig; that
work is in the CHANGELOG's `[Unreleased]` section. What remains of the phase is
**live recording** and the **VRMA export investigation**, both in
[the backlog](backlog.md).

## Then: boundary consolidation ⬜

**Boundary:** the agreements nine identities and four producer categories
arrived at separately are stated as one set, the ones that can be checked are
checked, and the three decisions the workspace has been carrying as open are
settled. Planned in [boundary-consolidation.md](boundary-consolidation.md); the
direction it serves is
[design/INTEGRATION_SCOPE_POLICY.md](../design/INTEGRATION_SCOPE_POLICY.md),
adopted 2026-09-06.

It adds no format, no adapter, no node and no package. Its items:

- ⬜ **BND-0 — the canonical producer contract**, *moved here from the
  recorded-source milestone on 2026-09-06*. Four categories produce motion and
  each was designed alone: recorded sources, live pose sources, tracker sources,
  and generated sources. What is unified is the **canonical value boundary**,
  not an I/O API — `SourceAnimation → HumanoidAnimation` for recorded,
  `timestamp + HumanoidPose` for live, `timestamp + TrackerFrame` for trackers,
  and `request/context → HumanoidAnimation or a pose stream` for generators.
  Done when a fifth producer is added by *naming* a crossing.
- ⬜ **BND-1 — one reference pipeline, proved once for every category.** Every
  source reaches `UsdSkelAnimation` today along its own tested path, and no
  single test says the same thing happens to all of them. One integration test,
  three sources — a `.vrma` clip, a BVH export, a recorded live trace — through
  an identical downstream call sequence. A source needing a downstream branch
  has found a defect, which is the point of running them together. This is the
  test NPZ/AMASS later joins **without changing it**.
- ⬜ **BND-2 — settle the adapter distribution decision.** Open since v0.7.0:
  `ost library package` produces an adapter artifact and no lane publishes one,
  so "the adapters are optional artifacts" is a design statement with nothing
  behind it. Recommended answer — one version, separate artifact membership.
- ⬜ **BND-3 — artifact closure as the release gate**, as one checklist a
  release passes or does not, rather than seven lanes and some prose.
- ⬜ **BND-4 / BND-5 — make the invariants checkable, and finish separating the
  workspace contract from its history.** The `DEPENDENCY_RULES.md` split carried
  from Product P0 lands here: doing it alongside a repository split is one
  migration of the section-number citations instead of two.

## Then: the motion foundation repository split ⬜ — and it can end at its gate

**Boundary:** `motionCore` and `motionRuntime` build, test, package and version
independently, and this workspace consumes them as an external dependency.
**Scope decided 2026-09-06: the motion foundation only** — `vrmRetarget` stays,
and so do `motionSource`, `motionBvh`, `motionTracking`, `liveTransport` and
`osc`. Planned in [motion-foundation-split.md](motion-foundation-split.md).

**It is scheduled ahead of its own preconditions on purpose, so it opens with a
measurement that can close it.**
[Scope policy §10](../design/INTEGRATION_SCOPE_POLICY.md) requires two consumers
outside VRM before a component leaves; after Motion Phase E there is exactly one
— `execMotion`, vendor-neutral by specification — and two of the four conditions
are not met at all. MFS-0 measures them and the answer is allowed to be no.

The track divides at a one-way door, and only the last part is behind it.
**Reversible:** the public API checked free of VRM vocabulary as a property
rather than a belief, its own version, its own package, its own suite run
against the installed artifact, and the workspace consuming it through
`find_package` as though it were external. Every one of those improves this
repository whether or not anything moves, which is why an inconclusive gate
wastes none of it. **Irreversible:** moving the history, and turning an in-tree
edge into a pinned external dependency — a second release contract, a second CI
configuration, and the loss of one-PR changes across the boundary, bought only
when someone outside VRM is actually consuming it.

## After those: NPZ / AMASS recorded sources, and the ARDY generation adapter ⬜

Two producer additions, and they are last because they are the ones that *use* a
boundary rather than fix one. Both were ahead of OpenExec until 2026-09-06.

- ⬜ **NPZ / AMASS through the existing `motionSource` boundary.** A reader is
  allowed format syntax and storage interpretation, and never the VRM target
  rig, the target rest pose, the retarget policy, stage authoring, an OpenExec
  graph, or a vendor runtime. **A container is not a format** — the same
  `.npz` means different things from AMASS, SMPL-X, a HumanML3D derivative or a
  custom dump, so what ships is a container reader plus an explicit profile.
  Whether that is one identity (`motionNpz`) or two (`motionNpz` +
  `motionAmass`) is settled by **measuring a few files of the real corpus
  first**. A file-format plugin is not part of this.
  [The recorded track](recorded-motion-sources.md) §13.
- ⬜ **The ARDY generation adapter** (Motion Phase F), behind the vendor-neutral
  `IMotionGenerator` that BND-0 freezes. The generator implementation itself is
  never in this repository
  ([scope policy §2](../design/INTEGRATION_SCOPE_POLICY.md)). Done when a
  generated take and a `.vrma` clip go through the same code path from the
  retarget onwards. [The adapters track](adapters-mocopi-vmc-ardy.md) §7.

## Standing: corpus policy — recorded evidence is not the generated corpus

The generated corpora stay. Real-session evidence goes beside them, never mixed
in, and the same shape serves both halves of the release:

```text
<adapter or library>/tests/corpus/
├─ generated/     protocol or format shapes, committed, CI-runnable, no hardware
└─ recorded/
   ├─ manifest.json      every recorded file, with or without its bytes
   └─ redistributable/   real sessions and files cleared for publication
```

Whether a file's bytes are committed is a **field**, not a location: a row that
changed directory when its redistribution status changed would break every
reference to it for a reason that has nothing to do with the file.

A capture or a file that cannot be redistributed leaves **no bytes** in the
repository. It leaves a manifest: hash, recording or exporting tool version,
sender or producer identity and version, device or relay identity, the measured
statistics, the expected diagnostics, expected frame and pose counts, the
validation date, and the redistribution status. A BVH manifest additionally
carries the profile id, frame time, joint and channel counts, coordinate
convention, unit, root policy, and the bones it is expected to map. That is
enough for a later reader to tell whether a claim still holds without the bytes,
and it is the same convention the VRM corpus already uses for models it cannot
ship.

Public CI runs the redistributable half. Hardware validation is an **opt-in
lane** that never gates a pull request — its output is a capture and a manifest,
not a green tick. A device is needed once per behavior, not once per run.

## Standing: product tracks with work still open

Only the open items are listed. What each track has already delivered is in the
[delivery history](../reports/delivery-history.md) and the
[release records](../releases/).

### Product P0 — documentation & implementation sync 🚧

*Goal: no contradiction between the docs and the code; a new user understands
the workspace layout, the output structure, and the import/runtime boundary.*
(design policy §15, §17-P0)

- 🚧 Describe `vrmSchema`, `usdVrmFileFormat`, `usdVrmPackageResolver`,
  `usdVrmaFileFormat`, `execMotion` and `execVrm` as separate bundles;
  `vrmContainer`, `motionCore`, `motionRuntime`, `vrmRetarget`, `motionSource`,
  `motionBvh`, `motionTracking`, `liveTransport`, `osc` and the three
  `vrmAdapter*` leaves as plain libraries; `motion_retarget`, `motion_capture`,
  `motion_bvh_convert` and the `*_record` tools as CLIs; and `usdVrm` as the
  aggregate product name only.
- 🚧 Unify phase notation to **Product P0–P6**, **Workspace Phase 0–8**, and
  **Motion Phase A–H** — three sequences, never a bare "Phase N".
- 🚧 Align build / test / install examples with what CI actually runs.
- 🚧 Adopt the house documentation taxonomy shared with `open-strata` and
  `hydra-merlin`.
- ⬜ **Finish separating the workspace contract from its history.** Of the
  three-way split of [WORKSPACE.md](../architecture/WORKSPACE.md) proposed on
  2026-08-29, only the package half was taken
  ([PACKAGE_CONTRACT.md](../architecture/PACKAGE_CONTRACT.md)). The dependency
  split is scheduled as BND-5 of
  [boundary consolidation](boundary-consolidation.md), so the section-number
  citations five documents make migrate once. A `docs/decisions/` directory
  would leave the taxonomy this repository shares with `open-strata` and
  `hydra-merlin`, so it is a decision for all three.

Done when: the component table matches the manifests, no document describes
`usdVrm` as a bundle id, every local link resolves, and a consistency check
guards all of it in CI.

### Product P1 — release stabilization 🚧

- ⛔ **A second OpenUSD version cell** (min vs latest) in the compatibility
  matrix. Today CI runs cy2026 / OpenUSD 26.08 only. **Blocked externally:**
  GHCR has no published min-version (e.g. OpenUSD 25.05 / cy2025) runtime
  artifact yet — this needs an open-strata runtime build + publish per OS, then
  a fourth cell in `openstrata.ci.yaml`. The OS axis already runs three cells.

### Product P3 — runtime verification 🚧

*Goal: builds and opens are continuously verified on all three OS; textured real
models resolve; schema registration succeeds.* (design policy §14, §17-P3)

The OS axis, the workspace graph gate and Unicode paths are covered. The last
is `workspace_unicode_paths`, on every workspace cell, Windows included: every
executable and both importers are run against paths no ANSI code page can
spell. Remaining:

- ⬜ Explicit **DLL dependency discovery** coverage on the Windows cell.
- ⬜ **Real VRM smoke test** (open + texture resolve) exercised in CI, not just
  fixtures.
- ⬜ **Verify the non-`ost` install path on Windows** *(carried from v0.2.0 /
  v0.3.0)*. The published bundles are only exercised through `ost`; a user
  composing them by hand against a plain OpenUSD environment is uncovered.
  `libUsdVrmFileFormat` links against `libvrmSchema` and `vrmContainer`, which
  are staged under `runtime/libraries/{lib,bin}` rather than beside the plugin —
  and Python 3.8+ dropped `PATH` from the DLL search for dynamically loaded
  modules, so the correct mechanism (`PATH` / `os.add_dll_directory` /
  co-location) is **unestablished**. [INSTALL.md](../guides/INSTALL.md) names the
  directories and the failure signature but deliberately prescribes no recipe.
  Closing this needs a non-`ost` install lane, not a docs edit.

## Workspace Phase 5 — per-bundle + aggregate packaging 🚧

**Status:** aggregate product shipped; the standalone dependency-registration P0
is blocked on `ost` · **Contract:**
[WORKSPACE.md](../architecture/WORKSPACE.md) §5

- ⛔ **A dependency bundle's USD registration half is never staged.** `ost`
  stages `libvrmSchema` + its CMake package into `runtime/libraries/`, but not
  `plugInfo.json` or `generatedSchema.usda` — so a packaged importer links
  against schemas it can no longer register, and a bare per-bundle
  `--from-package` fails at L3/L4. This is the **P0 upstream ask**, and it is
  why the release must ship all three VRM bundles. `--from-package --workspace`
  *does* compose and is green, but it works by putting the dependency's separate
  package on the path rather than by making any one package self-closed
  ([report 25](../reports/ost/25-2026-07-18-v0.18.0-from-package-workspace-correction.md)
  measures both).
- ⬜ **Retire the hand-rolled closure in `scripts/clean_install_smoke.py`.**
  It remains the release lane's packaged-artifact gate — it extracts outside the
  repo and drives textured avatars end to end, where the composed `ost` verb
  covers `minimal.vrm` per bundle. Needs the P0 above; the composed verb narrows
  but does not remove the need.
