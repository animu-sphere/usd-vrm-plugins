# Current

The next milestone and the work still open around it. **Shipped work is not
repeated here**: what landed since the last tag is in the
[CHANGELOG](../../CHANGELOG.md)'s `[Unreleased]` section, what each release
shipped and did not close is in its [release record](../releases/), and the
older history is in the [delivery history](../reports/delivery-history.md). A
milestone's own detail lives in its track document. This file carries the
boundary, what is still open, and where the detail is.

Legend: ✅ done · 🚧 in progress · ⬜ not started · ⛔ blocked · ⚠️ accepted workaround

## Shipped: v0.9.0 — the OpenExec foundation (Workspace Phase 8 + Motion Phase E) ✅

**Tagged and published on 2026-09-17** ([release record](../releases/v0.9.0.md)).
This section stays only as the milestone `check_docs.py` reads, and until the
next release takes a number. What v0.9.0 left open is under
[Carried out of v0.9.0](#carried-out-of-v090).

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
| P0-3 — `motion_retarget` distribution | ✅ | — 2026-09-17: the product's tool bakes a textured `Seed-san.vrm` and reports its own loaded modules (`--load-report`), a Python host resolves the 28 embedded textures from the install with `PATH` alone, and a source-path scan finds no build directory, staging area or PDB path (Linux/macOS binaries keep the build machine's RPATH, below) |
| P0-4 — `execMotion` | ✅ | — decided 2026-09-17: the producer half goes to the motion migration's producer contract (MIG-0 / BND-0), and a one-joint clip's fallback-filled root is kept and pinned |
| P0-5 — `execVrm` | ✅ | — |
| P0-6 — OpenExec / offline parity | ✅ | — the values (414 598 compared, all `==`) and the diagnostics agree; decided 2026-09-17 that the rows no producer reaches stay asserted on the exec side and read on the tool's |
| P0-7 — display smoke, `UsdGeomXformable` | ✅ | — |
| P1-1 — retarget diagnostics | ✅ | — |
| P1-2 — scale policy | ✅ | — decided 2026-09-17: a bake carries the rig's rest scale, and a clip's animated scale raises `VRM_RETARGET_NON_UNIT_SCALE` ([scale policy](../design/MOTION_CONTRACT.md#scale-policy-v090)) |
| P1-3 — partial skeleton policy | ✅ | — the seven cases are a contract, each held by a test ([partial skeleton policy](../design/MOTION_CONTRACT.md#partial-skeleton-policy-v090), 2026-09-17) |

**Every gate row is closed** (2026-09-17). The last were P0-3's: Windows DLL
discovery and the artifact-only offline retarget's texture half, both run by
`scripts/artifact_only_exec_smoke.py` in `release.yml`. So the release dry run,
not a pull request, is where they are proven on all three OS. Unicode paths
closed on 2026-09-15.

**The boundary findings** the nodes produced — places the wrapper idiom was a
workaround, had a cost, or was ruled out — are collected in
[boundary consolidation §1](boundary-consolidation.md#findings-from-the-exec-layer-as-they-land),
which is the track that acts on them.

## Carried over from shipped releases

### Carried out of v0.9.0

- ⬜ **Rewrite or drop the build machine's RPATH in packaged Linux and macOS
  binaries.** The v0.9.0 dry run measured it: every `.so` / `.dylib` and tool
  keeps the runtime directory it was built against, and three plugins keep the
  build tree's `workspace-prefix/lib`. Nothing loads from them on a user's host,
  but a host where one exists searches it first. The fix is in packaging
  (upstream `ost`, or a CMake `INSTALL_RPATH` of `$ORIGIN` / `@loader_path`
  with the build-tree tests adjusted). `artifact_only_exec_smoke.py` counts
  them today, and should fail on them once fixed.

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
- ➡️ **No lane publishes an adapter artifact** — and none will from here.
  The adapters left with MIG-4 on 2026-09-21, so whether a release carries one
  is `motion-connectors`' decision, as BND-2 already said it would be.

### Carried out of v0.7.0 — evidence an operator produces

None of these closes by writing code, and since 2026-09-21 none of them closes
*here*: every item below is about a live session through an adapter, and the
adapters are `motion-connectors`'. They are kept in this list until that
repository's own roadmap carries them, so that a condition this product shipped
without is not lost in the move.

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
  [`recorded/manifest.json`](https://github.com/animu-sphere/motion-connectors/blob/main/libs/motionConnectorMocopi/tests/corpus/recorded/manifest.json)
  rows with no bytes, because a session is a real person's motion. A
  publishable one needs the vendor's `BVH Sender`, not a device.
- 🚧 **The live path from release artifacts alone.** The recorded path runs from
  an installed product, profiles included (2026-08-30). The live path now needs
  this product composed with a `motion-connectors` artifact, which makes it the
  cross-repository test MIG-5 owes rather than a lane this repository can run
  by itself.

### Carried out of Motion Phase G

Expressions, look-at and the avatar's override arbitration reach a rig; that
work is in the CHANGELOG's `[Unreleased]` section. What remains of the phase is
**live recording** and the **VRMA export investigation**, both in
[the backlog](backlog.md).

## Then: the motion migration 🚧 — generic motion to `usd-motion-plugins`, input to `motion-connectors`

**Decided 2026-09-17, and it replaces two sections that stood here:** boundary
consolidation as its own milestone, and a conditional repository split that
could end at its gate. The `usd-motion-plugins` design policy has since settled
the motion architecture where it is owned — generic motion lives there, device
and protocol input in `motion-connectors`, VRM and VRMA here — so the split is
no longer a question this repository measures, and the boundary work that
existed to prepare for it is folded into the move.

**Boundary:** every identity
[WORKSPACE.md §9.1](../architecture/WORKSPACE.md#91-destination-of-every-identity)
gives another destination lives there with its history, this repository
consumes it as an installed package and keeps no copy, and every parity baseline
is reproduced across the move. Planned in
[motion-foundation-split.md](motion-foundation-split.md), as MIG-0 to MIG-5 in
the dependency order of the moves; the motion-plugins policy calls the same
steps **Migration Phase A–F**.

It starts after v0.9.0, on purpose: the OpenExec foundation's findings are the
API defects the move fixes on arrival, and they only exist once its nodes do.
**MIG-0 is done (2026-09-19)** ([the track §2](motion-foundation-split.md#2-mig-0--preparation-)).
`motionCore` (MIG-1) and `motionRuntime` (MIG-2's first item, as
`motionSampling` and `motionRecording`) have arrived in `usd-motion-plugins`
with their history, and so have `vrmRetarget`'s generic half, `motionUsd`'s
authoring half and the recorded sources (MIG-3). **MIG-1 and MIG-2's library half is done too (2026-09-21).** `motionCore` and
`motionRuntime` are consumed packages: `motionCore`, `motionSampling` and
`motionRecording` from `usd-motion-plugins` v0.5.0, pinned by digest per target
in five descriptors, with the whole suite green against them.
**So is the retarget (2026-09-23)**: `vrmRetarget`'s generic half is the
consumed `motionRetarget`, both builder copies are calls to it, every parity
row came out identical, and what stayed is `vrmRig`
([the track §4](motion-foundation-split.md#4-mig-2--sampling-retarget-usd-bridge-)).
**And so is `motionUsd`'s reading half (2026-09-23)**: `motion_retarget`
reads the clip through the consumed `ReadMotionStage` and keeps only the
`vrm:` tracks, the first edge here that only a tool declares — what `ost`
0.23.3 made materializable
([report 44](../reports/ost/44-2026-09-23-v0.23.2-a-tool-edge-reaches-nothing-and-a-tree-keeps-its-runtime.md)).
**MIG-3 is done too (2026-09-23)**: `motionSource`, `motionBvh`, the BVH tools
and the profiles are deleted here, their non-ASCII path cases having moved
first, and the suites that baked a real capture read a clip the published
converter wrote. What is left of the consuming side is one thing: `execMotion`
waits on that repository publishing
a bundle artifact, without which the parity rows cannot be re-run against the
consumed package before the copy here is deleted.

**MIG-4 is done on both sides (2026-09-21).** All six connector-bound
identities arrived in `motion-connectors` — the two leaves, the tracker layer
and the three adapters with their recorders — and this repository deleted its
copies in one change, under [WORKSPACE.md §9.2](../architecture/WORKSPACE.md#92-moving-rules)
rule 7. `ost` 0.23.2 is what made the cross-repository edge declarable
([report 43](../reports/ost/43-2026-09-20-v0.23.1-the-root-build-cannot-see-an-external-library.md)),
so MIG-1..MIG-3's consuming change — this repository resolving `motionCore` and
the rest as installed packages — is unblocked and is the next thing the track
owes. MIG-4's other half, `motion_capture`, followed on 2026-09-23. It is
`usd-motion-plugins`' `motion_record`, and `motion_retarget`'s suite bakes a
clip the published recorder wrote
([the migration track](motion-foundation-split.md#6-mig-4--recording-and-live-input-) §6).
Until an identity moves, it takes fixes and the work v0.9.0 owes, and **no new
generic capability** ([WORKSPACE.md §9.1](../architecture/WORKSPACE.md#91-destination-of-every-identity)).

What becomes of [boundary consolidation](boundary-consolidation.md):

- ⬜ **BND-0 — the canonical producer contract** is proposed into
  `usd-motion-plugins`' motion contract as MIG-0's evidence hand-over, rather
  than frozen here for identities that are leaving.
- ⬜ **BND-1 — one reference pipeline** becomes MIG-5's cross-repository test,
  and runs here until an integration repository exists.
- ⬜ **BND-2 — the adapter distribution decision** is `motion-connectors`'.
- ⬜ **BND-3 to BND-5 — artifact closure as the release gate, checkable
  invariants, the workspace contract apart from its history** stay this
  repository's, and run beside the migration.

## After those: NPZ / AMASS and the ARDY adapter — no longer this repository's ⬜

Both were planned here behind the split gate, and both leave with the code they
would have extended:

- **NPZ / AMASS** belongs to `usd-motion-plugins`, behind the versioned NPZ
  payload contract its policy requires before any reader (its §28). The
  measurement that decides one identity or two
  ([the recorded track](recorded-motion-sources.md) §13) moves with
  `motionSource` in MIG-3.
- **The ARDY generation adapter** (Motion Phase F) is created in
  `motion-connectors`, behind the generator interface `usd-motion-plugins`
  specifies. Nothing of it starts here.

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

- ✅ **DLL dependency discovery on Windows** *(2026-09-17)*. The release lane's
  artifact-only smoke reads back every module the product's `motion_retarget`
  and a Python host loaded, and each of the product's libraries has to come
  from the install.
- ✅ **Real VRM smoke** (open + texture resolve) *(2026-09-17)*: `Seed-san.vrm`
  baked by the product's tool, and its 28 embedded textures resolved to bytes
  by the product's resolver, with a negative that hides the resolver. It runs in
  `release.yml`, not on a pull request.
- ✅ **The non-`ost` activation path on Windows** *(carried from v0.2.0 /
  v0.3.0, closed 2026-09-17)*. The mechanism is `PATH`, for an executable and
  for a Python host alike: the plugin libraries are loaded by OpenUSD's
  registry, not by Python's extension import, so no `os.add_dll_directory` is
  needed ([INSTALL.md](../guides/INSTALL.md)). The smoke builds the environment
  from the product's `openstrata.activation.json` by hand, with no `ost`
  process in the run. **What it does not cover:** the archive is still
  extracted with `ost plugin product install`, and the per-bundle archives
  composed by hand, which share the product's directory layout, are not run.

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
