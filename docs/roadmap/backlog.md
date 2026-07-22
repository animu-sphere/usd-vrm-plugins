# Backlog

Ordered but unscheduled work. The next milestone and active carry-overs are in
[current.md](current.md); shipped detail is in the
[delivery history](../reports/delivery-history.md).

Legend: ⬜ not started

## Milestone ladder (beyond next)

- ⬜ **Motion Phase C — offline retarget.** The first end-to-end evaluation
  point of the whole motion layer (motion policy §16-C).
- ⬜ **Workspace Phase 6b — `motionRuntime` + `vrmRetarget` bootstrap.**
- ⬜ **Workspace Phase 8 — `execMotion` + `execVrm` bootstrap**, then **Motion
  Phase E** inside it.

Workspace phases establish boundaries; Motion phases fill them. They are never
the same milestone.

## Product P2 — fix the canonical-model contract

*Goal: the importer and the authorer depend only on the canonical contract;
parser and USD types never leak into it; fidelity is defined per field.*
(design policy §6, §17-P2)

- ⬜ Per-field **fidelity classification** (lossless / normalized / derived /
  approximate) for the major canonical fields.
- ⬜ **Canonical-model documentation** + a stable serialization / debug dump.
- ⬜ **Source preservation with an exporter in mind**; explicit normalized
  fields.
- ⬜ **Canonical validator**, distinct from the shipped stage validator (which
  checks the authored USD, not the model).

The canonical model stays **private to the importer**. `vrmCore` is not created
until a second consumer outside the importer actually exists
([WORKSPACE.md](../architecture/WORKSPACE.md) §1).

## Product P4 — the motion & runtime layer

*Goal: drive a VRM avatar from a clip, a live capture, or a generator — through
one shared humanoid pipeline — without changing the importer.*
(motion policy; design policy §10, §17-P4)

**Restructured 2026-07-18.** P4 previously read "OpenExec runtime bundle
(`execVrm`)" and enumerated a LookAt-first vertical slice ending in a Mocopi
adapter. [MOTION_ARCHITECTURE_POLICY.md](../design/MOTION_ARCHITECTURE_POLICY.md)
supersedes that plan in three ways:

| Was | Now | Why |
| --- | --- | --- |
| One `execVrm` bundle | `execMotion` (vendor-neutral) + `execVrm` (VRM semantics) | Motion runtime is reusable beyond VRM (motion policy §11) |
| OpenExec first, LookAt slice | `vrmRetarget` first, OpenExec last | The retarget core must be complete and testable before OpenExec (motion policy §18.12) |
| Mocopi as a P4 work item | Mocopi as an optional leaf adapter | Product names never appear in core (motion policy §8.1) |

P4 is now an umbrella. Its detail lives in the Motion Phase ladder below;
**LookAt-first is retired** — the first end-to-end target is offline retarget of
a `.vrma` clip onto a real avatar (Motion Phase C).

**Boundaries** ([WORKSPACE.md](../architecture/WORKSPACE.md) §2):
`execVrm` reads the schema contract from the stage only — never importer
internals, never the canonical model. `motionCore` never sees a vendor SDK, a
network protocol, or a product name. `vrmRetarget` never depends on OpenExec.

**Done when** (motion policy §17): a `.vrma` clip retargets onto a target
skeleton and plays back in a stock USD environment; a live capture feeds the
same retarget core with jitter absorbed and missing bones tolerated; and
swapping the generator changes nothing downstream.

## Motion Phase ladder (Product P4 detail)

Source of truth:
[MOTION_ARCHITECTURE_POLICY.md](../design/MOTION_ARCHITECTURE_POLICY.md) §16.
Always written "Motion Phase X", never a bare "Phase X".

- ✅ **Motion Phase A — frozen in v0.3.0.** The hand-authored contract and
  `motionCore` type surface are in
  [`MOTION_CONTRACT.md`](../design/MOTION_CONTRACT.md).
- ✅ **Motion Phase B — shipped in v0.3.0.** GLB/glTF animation read,
  humanoid rotation, hips translation, canonical `HumanoidSkeleton`,
  `UsdSkelAnimation`, time range, provenance. Not expression, not look-at, not
  retarget, not live.
- ⬜ **Motion Phase C — offline retarget.** `vrmRetarget` + a `motion_retarget`
  CLI: humanoid mapping from the target VRM, rest-pose correction, expansion to
  target joint order, `UsdSkelAnimation` output, `skel:animationSource` binding.
  **The first end-to-end evaluation point.**
- ⬜ **Motion Phase D — live-capture prototype.** Generic `LiveCaptureSource`,
  timestamped `PoseBuffer`, reproducible tests from recorded samples, missing
  bones / confidence / root-motion evaluation. Product-specific support is an
  optional adapter.
- ⬜ **Motion Phase E — `execMotion` / `execVrm`.** ClipSample, PoseBuffer,
  HumanoidRetarget, RootMotionResolve, AvatarApply. Nodes are thin wrappers over
  `motionRuntime` and `vrmRetarget`.
- ⬜ **Motion Phase F — generation adapter.** `IMotionGenerator`,
  `MotionGenerationRequest`, text intent, root waypoints, sparse joint
  constraints, pose history, clip-ification.
- ⬜ **Motion Phase G — expression / look-at / recording.** VRMA expression and
  look-at animation, `ExpressionResolve`, `LookAtEvaluate`, live recording,
  bake, VRMA export investigation.
- ⬜ **Motion Phase H — advanced.** Blending, IK / foot locking, contact
  handling, latency compensation, multi-performer sync, simulation bridge,
  generated-motion cache, publish pipeline.

### Motion-layer open questions

- ⬜ **Do the VRMA animation schemas belong in `vrmSchema`?** Motion policy §4.1
  names `VrmAnimationExpressionAPI` and `VrmAnimationLookAtAPI` as
  "equivalents" without fixing an owner. Adding them to `vrmSchema` is a schema
  contract change ([WORKSPACE.md](../architecture/WORKSPACE.md) §3); a separate
  `vrmaSchema` bundle avoids that but splits the contract. **Decide before
  Motion Phase B.**
- ⬜ **Is the `motion:` USD namespace (motion policy §13) a typed schema or
  namespaced attributes?** Motion Plans are the one place the policy authors USD
  outside a file-format plugin.
- ⬜ **Where does the binding/assembly layer (motion policy §3.3) get authored
  from?** It is neither importer output nor retarget output; today nothing owns
  it.

## Product P5 — MToon realization

*Goal: source parameters preserved; a portable fallback exists; at least one
renderer reproduces the main MToon look; an image regression test exists.*
(design policy §9, §17-P5)

Today: source data is preserved, `UsdPreviewSurface` is the fallback, and
`vrm:mtoon:raw` carries the raw block. Renderer-specific realization is **not
implemented**.

- ⬜ `VrmMToonAPI` + source-semantics contract
- ⬜ MaterialX / custom-render-context approximation
- ⬜ Renderer adapter, outline, conformance images, transparent-sorting behavior

## Product P6 — round-trip / exporter research

*Goal: round-trippable fields explicit; export loss reportable; a limited VRM 1.0
export path verifiable.* (design policy §17-P6)

- ⬜ USD→canonical reverse mapping
- ⬜ Source-fidelity + loss report
- ⬜ Detection of unsupported USD edits
- ⬜ Limited VRM 1.0 export prototype

A full VRM exporter remains a **non-goal**; this is research toward feasibility,
not a commitment to ship one.

## Cross-cutting

- ⬜ **Corpus expansion.** The foundation is shipped (see the
  [delivery history](../reports/delivery-history.md) §G). Remaining axes: VRM
  0.x, VRoid, animation clips, KTX2, multi-skin. VRoid (Vita, Victoria_Rubin,
  Sendagaya_Shino, AvatarSample_A/B) and Alicia are declared fetch/opt-in
  candidates **pending per-model license verification**.
- ⬜ **Multi-plugin session dogfooding.** `usdVrmaFileFormat` will exercise
  `ost plugin run/view --with` and the workspace closure. The repo root already
  globs `plugins/*`, so a second bundle drops in without edits.
- ⬜ **Morph-weight animation** authoring (glTF morph targets → USD), currently
  the one documented importer animation gap. Motion Phase G covers the VRMA
  side; this is the `.vrm` side and the two should land compatibly.
- ⬜ **A motion corpus.** `.vrma` clips with known-good expected output, plus
  recorded live-capture samples for the reproducible tests Motion Phase D needs.
  Licensing is the same gate the VRM corpus hit.

## Non-goals

Out of scope for these plugins — handle via schema, adapter, an OpenExec task,
or another plugin (design policy §15, §19; motion policy §8, §18):

- Full VRM runtime physics execution → `execVrm`
- Pixel-perfect MToon across all renderers
- Auto-repair of arbitrary broken glTF
- A full VRM exporter (P6 is research only)
- DCC-specific UI
- **Product-specific motion support in core.** Mocopi, ARDY, and any other
  named system are optional leaf adapters, never a core dependency or a branch
  condition.
- **Per-frame USD stage authoring for live playback.** Live evaluation produces
  transient poses; USD animation is authored only on bake / record / publish
  (motion policy §12.1).
- **Model latent representations in a shared USD schema** (motion policy §13).

## Acceptance criteria for a production-oriented importer

Tracked here so "done" stays unambiguous (design policy §16). Met criteria are
recorded in the [delivery history](../reports/delivery-history.md):

| # | Criterion | Status |
| --- | --- | --- |
| 1 | VRM 0.x/1.0 corpus continuously verified in CI | Product P3 / corpus expansion |
| 2 | Skinned mesh / skeleton / humanoid / expression / spring-bone inspectable on stage | ✅ shipped |
| 3 | Textures exportable as a portable package | ✅ shipped |
| 4 | MToon fallback vs fidelity responsibilities clear | Product P5 |
| 5 | Import warnings / fidelity loss retrievable as a report | ✅ shipped |
| 6 | Schema contract documented + versioned | ✅ shipped (contract v1) |
| 7 | External pipelines (OpenExec) can run the importer as a structured task | Product P4 |
