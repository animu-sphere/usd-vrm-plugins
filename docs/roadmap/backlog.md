# Backlog

Ordered but unscheduled work. The next milestone (`v0.1.0`) and active
carry-overs are in [current.md](current.md); shipped detail is in the
[delivery history](../reports/delivery-history.md).

Legend: ⬜ not started

## Milestone ladder (beyond next)

- ⬜ **Workspace Phase 6 — `execVrm` bootstrap.** Establishes the bundle
  boundary, manifest, and packaging for the runtime layer. Product P4 then
  implements behavior inside that boundary; the two are not the same milestone.
- ⬜ **Product P4 — the first `execVrm` vertical slice** (see below).

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

## Product P4 — OpenExec runtime bundle (`execVrm`)

*Goal: build a runtime graph from a static USD stage; drive the humanoid from
Mocopi or animation input; evaluate look-at / expression / spring bone as
independent nodes; swap the runtime without touching the importer.*
(design policy §10, §17-P4)

The first vertical slice is **LookAt only**. Spring bones and expressions are
explicitly not in the first PR.

1. ⬜ `vrmSchema` discovery from a stage
2. ⬜ `VrmLookAtAPI` reader
3. ⬜ Target input
4. ⬜ Head / eye output
5. ⬜ Deterministic evaluator test
6. ⬜ Verification that it uses no importer private API
7. ⬜ OpenExec graph integration
8. ⬜ Mocopi adapter prototype

**Boundary:** `execVrm` reads the schema contract from the stage only — never
importer internals, never the canonical model
([WORKSPACE.md](../architecture/WORKSPACE.md) §2, §3).

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
- ⬜ **Multi-plugin session dogfooding.** The `execVrm` bundle will exercise
  `ost plugin run/view --with` and the workspace closure. The repo root already
  globs `plugins/*`, so a second bundle drops in without edits.
- ⬜ **Morph-weight animation** authoring (glTF morph targets → USD), currently
  the one documented importer animation gap.

## Non-goals

Out of scope for these plugins — handle via schema, adapter, an OpenExec task,
or another plugin (design policy §15, §19):

- Full VRM runtime physics execution → `execVrm`
- Pixel-perfect MToon across all renderers
- Auto-repair of arbitrary broken glTF
- A full VRM exporter (P6 is research only)
- DCC-specific UI

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
