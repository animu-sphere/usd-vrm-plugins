# Backlog

Ordered but unscheduled work owned by this repository. The next milestone and
active carry-overs are in [current.md](current.md); shipped detail is in the
[release records](../releases/) and the
[delivery history](../reports/delivery-history.md). Generic motion work is
`usd-motion-plugins`' backlog and input work `motion-connectors`'; neither is
repeated here.

Legend: 🚧 in progress · ⬜ not started

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

## Product P4 — VRM semantics on motion

*Goal: drive a VRM avatar from any canonical clip — a `.vrma`, a converted
recording, a live session, a generated take — through one retarget and one
set of VRM semantics, without changing the importer.* (design policy §10,
§17-P4; [VRM_MOTION_POLICY.md](../design/VRM_MOTION_POLICY.md))

The offline half is done: a clip bakes onto a VRM rig with its expressions,
its gaze and its rest scales, and `execVrm` evaluates the body retarget equal
to that bake. What remains:

- ⬜ **The [`ExecIr` track](execir-track.md)** — an invertible humanoid rig,
  expression and look-at as computations, and skinned display.
- ⬜ **VRMA export investigation.** Whether a baked or recorded clip can be
  written back as `.vrma`, and what is lost. Research, like Product P6.
- ⬜ **VRM-specific advanced items** as they arise — spring-bone evaluation
  above all. Generic ones (IK-assisted retarget, contacts, blending) are
  `usd-motion-plugins`'.

**Boundaries** ([WORKSPACE.md](../architecture/WORKSPACE.md) §2): `execVrm`
reads the schema contract from the stage only — never importer internals,
never the canonical model — and `vrmRig` never depends on OpenExec.

### VRM motion open questions

- ⬜ **Do the VRMA animation schemas belong in `vrmSchema`?**
  `VrmAnimationExpressionAPI` and `VrmAnimationLookAtAPI` are named as
  "equivalents" with no owner. Adding them to `vrmSchema` is a schema contract
  change ([WORKSPACE.md](../architecture/WORKSPACE.md) §3); a separate
  `vrmaSchema` bundle avoids that but splits the contract. Both halves shipped
  ahead of the answer as namespaced attributes on plain prims
  ([VRM_MOTION_POLICY.md §3.3–§3.4](../design/VRM_MOTION_POLICY.md#33-expressions)),
  so applying a typed API later moves nothing. That buys time; it does not
  answer the question.
- ⬜ **Should a rig be told when two expressions displace the same vertices?**
  VRM 1.0's override fields close the case where the avatar *states* the rule
  (2026-09-04, #170); VRM 0.x has no such field, and nothing measures the
  collision. A check reporting "expressions X and Y displace N shared points"
  would find it on either version. It needs mesh points and every expression's
  morph offsets, so it is a corpus-analysis feature, owed by whichever tool
  grows a geometry pass first.
- ⬜ **Who authors the binding / assembly layer**
  ([VRM_MOTION_POLICY.md §2](../design/VRM_MOTION_POLICY.md#2-composition))?
  It is neither importer nor retarget output. The generic half is
  `usd-motion-plugins`' `Bindings` prim (its USD-O5); the `vrm:retarget:*`
  statements `execVrm` reads are this repository's half, and today only the
  parity harness authors them ([ExecIr track §4](execir-track.md#4-contract-changes-this-track-requires)).
- ⬜ **Which names a VRMA clip's expression weights carry on a
  `MotionChannelSet`.** The shared stage shape is `usd-motion-plugins`'
  `/Animation/Channels` with `motion:channelName`; the `.vrma` stage keeps
  `/Animation/Expressions` and its `vrm:expression*` attributes. Open here: the
  pose-side names (`vrm:` namespaced or not), and whether the reader should
  read a weight only where the stage keyed it — USD holds the last key forward,
  and a held value is not one the producer reported — or at every sample the
  bake wants.
- ⬜ **The OpenUSD pin across three repositories.** Each package is built
  against one exact OpenUSD release, so a pin change is a coordinated release
  of `usd-motion-plugins`, `motion-connectors` and this repository. Who cuts
  first is unsettled.

## Product P5 — MToon realization

*Goal: source parameters preserved; a portable fallback exists; at least one
renderer reproduces the main MToon look — that renderer is `hydra-toon`'s, not
this repository's; an image regression test exists.*
(design policy §9, §17-P5; the plan of record is
[material policy](../design/MATERIAL_ARCHITECTURE_POLICY.md) §7)

P5's open steps are their own document, the
[material track](material-track.md). Since 2026-09-25 the order is
**semantics first**: the canonical schemas come next, ahead of any further
realization work, and the full MToon renderer is `hydra-toon`'s, another
repository.

- ✅ **Step 3** — `VrmMaterialAPI` / `VrmMToonAPI` / `VrmTextureInfoAPI` as
  the schema contract ([track §3](material-track.md#3-steps))
- ✅ **Step 4** — importer canonicalization of VRM 0.x and 1.0 into those
  schemas ([track §3](material-track.md#3-steps))
- ⬜ **Steps 5–7** — `/preview` and `/mtlx` generated from canonical
  semantics, expression material binds onto canonical slots
  ([track §3](material-track.md#3-steps))

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
- ⬜ **`.vrma` clips with known-good expected output.** Licensing is the same
  gate the VRM corpus hit; the committed clips are hand-authored.
- ⬜ **Morph-weight animation** authoring (glTF morph targets → USD), the one
  documented importer animation gap. It is the `.vrm` side of what
  `motion_retarget` already authors from a `.vrma`, and the two should land
  compatibly.

## Non-goals

Out of scope for these plugins — handle via schema, an OpenExec task, another
plugin, or a sibling repository (design policy §15, §19). These are the
individual cases of one boundary,
[scope policy §2](../design/INTEGRATION_SCOPE_POLICY.md#2-what-this-repository-does-not-own).

- Full VRM runtime physics execution → `execVrm` or a physics repository
- Pixel-perfect MToon across all renderers
- A native MToon renderer, outline pass or GPU pipeline → `hydra-toon`
  ([material policy §5.3](../design/MATERIAL_ARCHITECTURE_POLICY.md#53-renderer-specific-realization))
- Auto-repair of arbitrary broken glTF
- A full VRM exporter (P6 is research only)
- DCC-specific UI
- **Generic motion, or any device or protocol input.** They are
  `usd-motion-plugins`' and `motion-connectors`', consumed here as packages.
- **Per-frame USD stage authoring for live playback.** USD animation is
  authored only on bake, record or publish.
- **I/O inside an OpenExec computation.** A computation evaluates an immutable
  snapshot.
- **`ExecIr` as the canonical motion contract**, or as a prerequisite for the
  standard pipeline ([VRM_MOTION_POLICY.md §8](../design/VRM_MOTION_POLICY.md#8-execir-is-optional-never-a-prerequisite)).

## Acceptance criteria for a production-oriented importer

Tracked here so "done" stays unambiguous (design policy §16). Met criteria are
recorded in the [delivery history](../reports/delivery-history.md):

| # | Criterion | Status |
| --- | --- | --- |
| 1 | VRM 0.x/1.0 corpus continuously verified in CI | Product P3 / corpus expansion |
| 2 | Skinned mesh / skeleton / humanoid / expression / spring-bone inspectable on stage | ✅ shipped |
| 3 | Textures exportable as a portable package | ✅ shipped |
| 4 | MToon fallback vs fidelity responsibilities clear | Product P5 ([material track](material-track.md)) |
| 5 | Import warnings / fidelity loss retrievable as a report | ✅ shipped |
| 6 | Schema contract documented + versioned | ✅ shipped (contract v1) |
| 7 | External pipelines (OpenExec) can run the importer as a structured task | Product P4 ([`ExecIr` track](execir-track.md)) |
