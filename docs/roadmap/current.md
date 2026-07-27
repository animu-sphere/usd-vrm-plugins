# Current

The next milestone and active carry-over work. Shipped detail is in the
[delivery history](../reports/delivery-history.md).

Legend: 🚧 in progress · ⬜ not started · ⛔ blocked

## Next: v0.6.0 — the OpenExec foundation (Workspace Phase 8 + Motion Phase E) ⬜

**Release boundary:** `execMotion` and `execVrm` bundles exist and evaluate a
humanoid through OpenExec. Nodes are thin wrappers over `motionRuntime` and
`vrmRetarget`, never a second implementation. Planned in
[openexec-v0.6.0-v0.7.0.md](openexec-v0.6.0-v0.7.0.md).

### Landed so far

- ✅ **OpenUSD is pinned to 26.08, and the pin is enforced rather than
  declared** (plan P0-1 / §4.1). The `>=25.05,<27.0` tolerated range is retired
  in all four bundle manifests, and `cmake/UsdVrmOpenUsd.cmake` refuses anything
  else at configure time — from the root project, from a standalone
  `ost plugin build`, and from a plain-CMake build that never sees `ost`. The
  same module probes the six OpenExec libraries and refuses a 26.08 without
  them, which is a *detection* check: 26.08 has no OpenExec build toggle.
  `buildInfo.json` (schema 2) records the release, `PXR_VERSION`, and the
  OpenExec components. `workspace_openusd_contract` drives the module against
  fixture installs so the refusals are tested — no runtime we own takes that
  path — and `scripts/check_docs.py` keeps the manifests, the module, and
  [SUPPORTED_CONFIGURATIONS.md](../reference/SUPPORTED_CONFIGURATIONS.md) from
  drifting apart.
- ⬜ The rest of P0-1 is the [26.08 OpenExec migration
  report](openexec-v0.6.0-v0.7.0.md#43-2608-openexec-migration-report), which
  P0-4 and P0-5 need before either can be designed.

The two prerequisites this milestone was originally scoped around were already
met, both ahead of schedule:

- ✅ **One OpenUSD across three OS.** All three 26.08 runtimes are published to
  `ghcr.io/animu-sphere/openstrata-runtime-cy2026-usd` and every lane is pinned
  to them, including the workspace cells and the release workflow. They carry
  OpenExec (`exec`, `execGeom`, `execIr`, `execUsd`, `usdExecImaging`, `vdf` —
  198 headers under `include/pxr/exec`) and were built `--examples`, so the
  26.08 `ExecIr` samples ship inside the runtime. Digests and evidence:
  [report 29](../reports/ost/29-2026-07-26-v0.20.0-openusd-2608-runtime-publish.md)
  (Windows, Linux),
  [report 30](../reports/ost/30-2026-07-26-v0.20.0-macos-2608-runtime-publish.md)
  (macOS arm64).
- ✅ **The motion layer has CI.** v0.6.0's named P0-2 blocker is cleared, not by
  the v0.5.0 workaround (below) but by `ost 0.21.0`: four `kind: workspace`
  cells in `openstrata.ci.yaml` build the root tree and run its whole 22-test
  CTest suite, and `motion-ci.yml` is deleted. See
  [report 33](../reports/ost/33-2026-07-28-v0.21.0-workspace-ci-adoption.md).

### Carried into v0.6.0

- ⬜ **Freeze the Linux and macOS symbol baselines.** `tests/baseline/symbols/`
  holds `windows-x86_64.txt` only, because until the workspace cells landed no
  lane ran the Phase 0 gate anywhere else. `--check` skips a platform with no
  committed file (it has nothing to regress against) and every other baseline
  artifact is verified on all three OS, so the gap is symbols alone. Closing it
  means running `tools/baseline_freeze.py --update` on a Linux and a macOS host
  and committing the result.
- ⛔ **The scheduled lane's `plugin_artifact` is still a 26.05 build.**
  `usdvrmfileformat-support-windows-cy2026` pairs a 26.05-built plugin with a
  26.08 runtime. OpenUSD guarantees no ABI stability across versions, so that
  artifact must be republished before the lane's result means anything.
  Untouched by v0.5.0. It is also the reason `ost ci validate` exits non-zero on
  a workstation that holds the artifact (the evidence gate); hosted runners do
  not hold it, so the generated lanes stay green.
- ⚠️ **`release.yml` stays hand-authored, and hand-mirrors what the contract now
  expresses.** Its X11 step, its `ost` pin and its runtime digests are copies of
  `openstrata.ci.yaml` values; regeneration never touches them and a green PR
  lane proves nothing about it. The `ost` release contract (`release:` in the
  matrix) is the eventual fix; adopting it is not scoped yet.

### Closed by the ost 0.21.0 adoption

- ✅ **The Linux cells' `apt-get` step is in-contract.** `host_packages:
  {apt: [libx11-dev, libxt-dev]}` on every Linux cell renders the step, so
  `ost ci generate github --force` re-emits it instead of deleting a hand-added
  one. Only `release.yml` still hand-carries it.
- ✅ **Runtime digests are pinned in one place again.** `motion-ci.yml` is gone;
  a hand-written lane that still needs the pins can read them from
  `ost ci matrix --json` rather than copying them.
- ✅ **The CLIs ship in the release artifacts.** `tools/*/openstrata.tool.yaml`
  makes `motion_retarget` and `motion_capture` tool members of the aggregate
  product (6 members), and `release.yml` stages them with the bundles.

## Shipped: v0.5.0 — live capture

[v0.5.0](../releases/v0.5.0.md) is **released** — tagged 2026-07-26.

**Release boundary:** a generic `LiveCaptureSource` feeding the same retarget
core v0.4.0 shipped. It does **not** begin OpenExec — that is v0.6.0.

- ✅ **Motion Phase D — the live-capture surface.** `IMotionSource` with an
  explicit `PoseSampleStatus`, `ClipSource`, and `LiveCaptureSource`:
  timestamped intake into the `PoseBuffer` Phase 6b built for it, confidence
  gating, a missing-bone policy (hold vs unbound), root-motion intake
  (passthrough / ignore / derive-velocity), clock alignment, and statistics that
  say what was refused and why.
- ✅ **Reproducible by construction.** Nothing in the intake path opens a
  transport or reads a wall clock: an adapter decodes a frame and calls `Push`,
  and the caller drives the tick. So a recorded session replays exactly, on
  every run and every OS — which is what makes the tests real rather than mocks.
- ✅ **A recorded-trace format and a corpus.** `motion-capture-trace` v1 is
  line-oriented text that round-trips byte-identically, so a fixture is compared
  rather than merely parsed. The
  [six committed traces](../../libs/motionRuntime/tests/corpus/README.md) are
  generated by closed-form maths — deliberately synthetic, because a corpus
  recorded from a commercial SDK would inherit the VRM corpus's redistribution
  gate and CI could not run it.
- ✅ **The claim is falsifiable.** `motion_capture` authors the same
  avatar-independent semantic clip `usdVrmaFileFormat` produces, and the
  end-to-end test bakes it onto a real avatar with the **unchanged** Phase C
  tool, then resolves the result through a `UsdSkelSkeletonQuery`.
- 🚧 **A CI lane for the motion layer was written but is not wired** — the
  v0.4.0 carry-over is *not* closed. See below.
- ✅ **One humanoid taxonomy.** `HumanBoneParent` / `NearestPresentAncestor` /
  `HumanBoneJointPath` moved into `motionCore`; the `.vrma` reader's private
  copy is gone.

Explicitly deferred: any product-specific adapter, validation against a real
capture rig, and OpenExec evaluation (Motion Phases E–H).

### Attempted in v0.5.0 — the motion-layer CI lane 🚧

> **Superseded 2026-07-28.** `ost 0.21.0` made this expressible in the CI
> contract (`kind: workspace` cells), and `motion-ci.yml` was deleted with the
> adoption. The record below is what v0.5.0 actually shipped; the resolution is
> [report 33](../reports/ost/33-2026-07-28-v0.21.0-workspace-ci-adoption.md).

`ost ci generate` emits one job per *bundle* cell, so `motionCore`,
`motionRuntime`, `vrmRetarget`, `vrmContainer` and both CLIs get no lane.
v0.5.0 wrote `.github/workflows/motion-ci.yml` (deleted 2026-07-28) to cover
them by building the whole workspace with plain CMake — the only configuration
in which `libs/` and `tools/` targets exist.

**It is not wired.** It ships on `workflow_dispatch` only and does not gate pull
requests, so the layer's coverage is unchanged from v0.4.0. Shipping it red on
every PR would have been worse than shipping it off.

- ✅ **The parts that work**, verified green on all three OS: `ost` bootstrap,
  artifact pull, runtime materialisation, and the **WORKSPACE.md §2
  dependency-graph gate**. The gate has to be read out of
  `ost plugin test --workspace --up-to 0 --json`, because that verb couples
  graph validation to testing every bundle and so exits non-zero on a fresh
  checkout where nothing is built yet.
- ⛔ **What blocks it.** Configuring against the pulled runtime fails on all
  three OS: `pxrConfig.cmake` does
  `find_dependency(Python3 COMPONENTS Development ...)` and resolves to the
  paths of the Python the runtime was *built* against
  (`/usr/include/python3.13`), which do not exist on a hosted runner.
  `actions/setup-python` provides a dev-complete 3.13 and exports
  `Python3_ROOT_DIR`; passing it again as `-DPython3_ROOT_DIR` changed nothing.
  The generated lane never meets this because `ost plugin build` resolves
  Python itself.
- ⬜ **Untried next step:** `-DPython3_EXECUTABLE=$pythonLocation/bin/python3`,
  a stronger hint than `ROOT_DIR`; failing that, find out what
  `ost plugin build` passes that a bare `cmake` does not.

Diagnosis and the upstream asks:
[report 32](../reports/ost/32-2026-07-26-v0.20.0-motion-layer-ci-workaround.md).

### Closed in v0.5.0 anyway

- ✅ **The Phase 0 baseline is no longer stale.** It is registered only from the
  plain-CMake root build, which no lane runs, so nobody noticed the committed
  symbol baseline was still frozen against OpenUSD 26.05 after the 26.08 bump.
  Refreezing changed all 220 symbols by `pxrInternal_v0_26_5` → `_26_8` **and
  nothing else**; every other baseline artifact is byte-identical across the two
  OpenUSD versions. Found only because the lane work made someone run it — and
  it still runs nowhere automatically.
- ✅ **The baseline gate needs an explicit full-workspace session** (v0.4.0
  carry-over). The lane names every bundle with `--with`; when the lane is
  finished, the recurrence the v0.4.0 record predicted is handled.

## Shipped: v0.4.0 — offline retarget

[v0.4.0](../releases/v0.4.0.md) is **released** — tagged 2026-07-26.

**Release boundary:** Workspace Phase 6b + Motion Phase C. It ends at a `.vrma`
clip playing back on a real avatar; it does **not** begin live capture or
OpenExec.

This is the motion layer's **first end-to-end evaluation point** (motion policy
§16-C). Everything before it authored data that nothing consumed; from here a
clip and an avatar produce a bound, playable result.

- ✅ **Workspace Phase 6b — `motionRuntime`.** A plain static CMake library over
  `motionCore`: `PoseBuffer` (bounded, strictly ordered history with bracketed
  sampling and capped position-only extrapolation), `SlerpShortest` / `LerpPose`
  / `LerpRootMotion`, `Resample` / `SampleAnimation`, `PoseFilter`
  (frame-rate-independent exponential smoothing), and two- and N-pose
  `BlendPoses`. Two invariants hold throughout: a missing sample is held, never
  faded toward identity; and every orientation stays a unit quaternion on the
  short arc.
- ✅ **Workspace Phase 6b — `vrmRetarget`.** `TargetSkeleton`, `HumanoidMap`,
  `SourceRestPose` / `RestPoseCorrection`, `RootMotionPolicy`, and
  `PoseRetargeter`. It never opens a stage — the rig arrives as plain values —
  and it never depends on OpenExec, so `execVrm`'s future `HumanoidRetarget`
  node is a wrapper over it rather than a second implementation.
- ✅ **Motion Phase C — `motion_retarget`.** The CLI reads the target rig and
  the semantic clip off stages, retargets, authors the `UsdSkelAnimation`, and
  binds `skel:animationSource` on an override of the referenced skeleton, so the
  avatar keeps owning its rig. `--root-motion hips|root|ignore`,
  `--translation-scale`, `--preserve-target-height`, `--resample`, and
  `--humanoid-map` are covered by tests.
- ✅ **The design triplet is now executable.** `canonical_walk.usda` +
  `avatar.usda` → `expected_retargeted.usda` is checked at the value level
  through USD composition on both sides, rather than by byte-comparing a layer.
  The hand-off Motion Phase A froze is met, not just described.

Three decisions worth carrying forward:

- **Joint names are never guessed.** A binding comes from the avatar's
  `vrm:humanBones:<bone>` attributes or from an explicit `--humanoid-map`; an
  unbound bone is reported, never inferred from a name.
- **Root motion carries a delta, not a height.** The hips translation is applied
  relative to each rig's own rest, so a clip authored on a 1.0 m rig drives a
  1.6 m one without the avatar snapping to the source's hip height.
- **The tool reads `vrm:humanBones:*` as plain attributes**, not through
  `VrmHumanoidAPI`, so the motion layer needs no link against the `vrmSchema`
  bundle.

Explicitly deferred: live capture, generation, expression, look-at, OpenExec
evaluation, blending beyond the primitive, IK, and foot locking (Motion Phases
D–H).

Shipped with the tag, after the retarget work: the `.vrma` importer and the bake
both authored a `UsdSkelAnimation` without `scales`, which UsdSkel resolves as a
unit with translations and rotations. Both bound cleanly and then held every
joint at its rest pose — the clip did not move
([#64](https://github.com/animu-sphere/usd-vrm-plugins/issues/64)). Both halves
now author a constant identity array, and the tests drive a
`UsdSkelSkeletonQuery` rather than comparing authored values.

### Carried out of v0.4.0 — one closed, one still open

- ⬜ **No CI lane covers the motion layer.** Still true; see the attempt above. `ost ci generate` emits one job per
  *bundle* cell, and `ost plugin test --workspace` tests bundles — so
  `motionRuntime`, `vrmRetarget`, and `motion_retarget` are compiled and tested
  only in the plain-CMake root build, which no lane runs. Their manifest edges
  *are* validated by `ost plugin test --workspace` (it discovers plain
  libraries; see [WORKSPACE.md §8](../architecture/WORKSPACE.md)). Filed as the
  P0 ask in
  [report 28](../reports/ost/28-2026-07-26-v0.20.0-motion-layer-ci-gap.md).
- ✅ **The baseline gate needs an explicit full-workspace session.**
  `discovery.json` freezes the union across every bundle, but a bundle that
  registers a type without being a dependency (`usdVrmaFileFormat`) has to be
  named with `--with`. v0.4.0 wired that into the CTest env; the same gap will
  recur for the next bundle that is nobody's dependency. A generator that
  derives the session from the workspace graph would close it for good.

## Shipped: v0.3.0 — the VRMA motion foundation

[v0.3.0](../releases/v0.3.0.md) is **released** — tagged and published
2026-07-23 (`68a5d32`). It froze the motion contract (Motion Phase A), shipped
`motionCore` (Workspace Phase 6a) and `usdVrmaFileFormat` (Workspace Phase 7 /
Motion Phase B), and vendored `cgltf` v1.15 so no configure-time fetch is
needed. Its release lane required `ost` 0.20.0 for the aggregate-product
reproducibility gate.

Carried out of the v0.2.0/v0.3.0 releases as open work:

- ⬜ **Verify the non-`ost` install path on Windows.** The published bundles are
  only exercised through `ost`; a user composing them by hand against a plain
  OpenUSD environment is uncovered. `libUsdVrmFileFormat` links against
  `libvrmSchema` and `vrmContainer`, which are staged under
  `runtime/libraries/{lib,bin}` rather than beside the plugin — and Python 3.8+
  dropped `PATH` from the DLL search for dynamically loaded modules, so the
  correct mechanism (`PATH` / `os.add_dll_directory` / co-location) is
  **unestablished**. [INSTALL.md](../guides/INSTALL.md) names the directories
  and the failure signature but deliberately prescribes no recipe. Closing this
  needs a non-`ost` install lane, not a docs edit.

### Product P0 — documentation & implementation sync 🚧

*Goal: no contradiction between the docs and the code; a new user understands
the workspace layout, the output structure, and the import/runtime boundary.*
(design policy §15, §17-P0)

The importer-era docs described a single `usdVrm` bundle with co-located
schemas. Since Workspace Phase 4 that is wrong in every particular.

- 🚧 Describe `vrmSchema`, `usdVrmFileFormat`, `usdVrmPackageResolver`, and
  `usdVrmaFileFormat` as separate bundles; `vrmContainer`, `motionCore`,
  `motionRuntime`, and `vrmRetarget` as plain libraries; `motion_retarget` as a
  CLI; and `usdVrm` as the aggregate product name only.
- 🚧 Unify phase notation to **Product P0–P6**, **Workspace Phase 0–8**, and
  **Motion Phase A–H** — three sequences, never a bare "Phase N".
- 🚧 Align build / test / install examples with what CI actually runs.
- 🚧 Adopt the house documentation taxonomy shared with `open-strata` and
  `hydra-merlin`.

Done when: the component table matches the manifests, no document describes
`usdVrm` as a bundle id, every local link resolves, and a consistency check
guards all of it in CI.

### Product P1 — release stabilization 🚧

- ✅ **Decided: the release ships all four bundles.** `release.yml` builds,
  tests, packages (`ost plugin package --workspace`), and publishes `vrmSchema`,
  `usdVrmFileFormat`, `usdVrmPackageResolver`, and `usdVrmaFileFormat` per
  target. The three VRM bundles ship together because that was forced, not
  preferred: an `usdVrmFileFormat` package alone registers the `.vrm` format and
  then **fails to open a stage** (L3/L4, `Used null prim`), because ost stages a
  dependency bundle's link half without its USD registration half. Measured in
  [report 23 §2.1](../reports/ost/23-2026-07-18-v0.18.0-workspace-packaging-v0.19.0-asks.md).
- ✅ **Replaced the packaged-artifact gate.** A bare per-bundle
  `ost plugin test --from-package` tests the one configuration that provably
  fails (L3/L4 above). The lane gates on the composed
  `scripts/clean_install_smoke.py`, which opens and validates real models from
  the packaged artifacts. `--from-package --workspace` *does* compose and is
  green (see [report 25](../reports/ost/25-2026-07-18-v0.18.0-from-package-workspace-correction.md));
  it covers `minimal.vrm` per bundle, so it joins the smoke script rather than
  replacing it.
- ⬜ **Decide whether `motion_retarget` ships in the release artifacts.** It is
  an executable, not a bundle, so no member archive carries it today. The
  aggregate product is the obvious home; that needs an `ost` packaging answer
  first (see report 28).
- ⛔ **A second OpenUSD version cell** (min vs latest) in the compatibility
  matrix. Today CI runs cy2026 / OpenUSD 26.08 only. **Blocked externally:**
  GHCR has no published min-version (e.g. OpenUSD 25.05 / cy2025) runtime
  artifact yet — this needs an open-strata runtime build + publish per OS, then
  a fourth cell in `openstrata.ci.yaml`. The OS axis already runs three cells.

### Product P3 — runtime verification (carry-over) ⬜

*Goal: builds and opens are continuously verified on all three OS; textured real
models resolve; schema registration succeeds.* (design policy §14, §17-P3)

The OS axis is shipped. Remaining:

- ✅ **The workspace graph gate is wired.** [WORKSPACE.md §2](../architecture/WORKSPACE.md)
  specifies `ost plugin test --workspace` as the enforcement for the dependency
  directions, and §8 called for it to be a required PR-lane gate from Workspace
  Phase 1 on. The `workspace-graph-pr` cell runs `--graph-only` on every PR.
- ⬜ Explicit **UTF-8 / Unicode path** and **DLL dependency discovery** coverage
  on the Windows cell.
- ⬜ **Real VRM smoke test** (open + texture resolve) exercised in CI, not just
  fixtures.

## Workspace Phase 5 — per-bundle + aggregate packaging 🚧

**Status:** aggregate product shipped; standalone dependency-registration P0 is
blocked on `ost` · **Contract:**
[WORKSPACE.md](../architecture/WORKSPACE.md) §5

`ost` 0.19.0 moved this forward but did not unblock it. What landed:
`ost plugin package --workspace` (adopted by the release lane), and a `bundles`
key in `dependencies.json`. What did not:

- **A dependency bundle's USD registration half is never staged.** ost stages
  `libvrmSchema` + its CMake package into `runtime/libraries/`, but not
  `plugInfo.json` or `generatedSchema.usda` — so the packaged importer links
  against the schemas it can no longer register, and `--from-package` fails at
  L3/L4. This is the **P0 upstream ask**; it is also why the release must ship
  all three VRM bundles.

The aggregate product artifact is emitted by `--workspace --product` and is
adopted by the release lane. `--from-package` **does** compose with
`--workspace` — the shipped help text saying otherwise was stale, and this
roadmap repeated it. That verb verifies the composed configuration and is green;
it does not close the P0, because it works by putting the dependency's
*separate package* on the path rather than by making any one package
self-closed.
[Report 25](../reports/ost/25-2026-07-18-v0.18.0-from-package-workspace-correction.md)
measures both.

`scripts/clean_install_smoke.py` remains the release lane's packaged-artifact
gate: it extracts outside the repo and drives textured avatars end to end, where
the ost verb covers `minimal.vrm` per bundle.

- ✅ Adopt `ost plugin package --workspace`.
- ✅ Gate the composed packaged configuration with `--from-package --workspace`.
- ✅ Emit the aggregate artifact. `ost plugin package --workspace --product`
  is adopted by the release lane and ships one product archive containing the
  exact member archives, manifests, checksums, and evidence.
- ⬜ Retire the hand-rolled closure in `clean_install_smoke.py` (needs the P0
  above; the composed verb narrows but does not remove the need).
