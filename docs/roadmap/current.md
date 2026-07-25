# Current

The next milestone and active carry-over work. Shipped detail is in the
[delivery history](../reports/delivery-history.md).

Legend: 🚧 in progress · ⬜ not started · ⛔ blocked

## v0.4.0 — offline retarget 🚧

**Release boundary:** Workspace Phase 6b + Motion Phase C. The branch ends at a
`.vrma` clip playing back on a real avatar; it does **not** begin live capture
or OpenExec.

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

### Open for this release

- ⬜ **No CI lane covers the motion layer.** `ost ci generate` emits one job per
  *bundle* cell, and `ost plugin test --workspace` tests bundles — so
  `motionRuntime`, `vrmRetarget`, and `motion_retarget` are compiled and tested
  only in the plain-CMake root build, which no lane runs. Their manifest edges
  *are* validated by `ost plugin test --workspace` (it discovers plain
  libraries; see [WORKSPACE.md §8](../architecture/WORKSPACE.md)). Filed as the
  P0 ask in
  [report 28](../reports/ost/28-2026-07-26-v0.20.0-motion-layer-ci-gap.md).
- ⬜ **`usdvrm_baseline` is red — a v0.3.0 carry-over, not a v0.4.0 change.**
  `tools/baseline_freeze.py` looked its bundle up by `kind: usd-fileformat`,
  which stopped being unique when `usdVrmaFileFormat` shipped; v0.4.0 changes
  the lookup to key on `provides: usd-fileformat:vrm`, which fixes the crash and
  exposes the real problem underneath: `tests/baseline/discovery.json` was
  frozen before `usdVrmaFileFormat` existed, so the registered-type union no
  longer matches. Closing it means **re-freezing the baseline** under a full
  workspace session — a deliberate, reviewable change that does not belong in a
  feature branch.
- ⬜ **Dry-run, tag, and publish** once the two items above are settled.

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
  `ost plugin test <bundle>` per cell (twelve cells: four bundles × three OS).
  Workspace Phase 6b raised the cost of this gap: the gate validates plain
  libraries too, and it caught a real version mismatch locally that no
  per-bundle cell would have seen.
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
