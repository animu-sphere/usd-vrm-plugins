# Backlog

Ordered but unscheduled work. The next milestone and active carry-overs are in
[current.md](current.md); shipped detail is in the
[delivery history](../reports/delivery-history.md).

Legend: 🚧 in progress · ⬜ not started

## Milestone ladder (beyond next)

The version each track targets is fixed in the
[roadmap status table](README.md#status-at-a-glance); this section is the work,
not the schedule.

| Release | Theme | Sequences | Plan |
| --- | --- | --- | --- |
| after v0.8.0 | NPZ / AMASS recorded sources | — | [recorded-motion-sources.md](recorded-motion-sources.md) §13 |
| after v0.8.0 | canonical motion producer contract | — | [below](#canonical-motion-producer-contract) |
| after those | OpenExec VRM runtime foundation | Workspace Phase 8, Motion Phase E | [openexec-foundation.md](openexec-foundation.md) §6 |
| after the foundation | `ExecIr` invertible VRM humanoid rig | Motion Phase E cont. | [openexec-foundation.md](openexec-foundation.md) §7 |

**Re-ordered 2026-08-29.** Two producer-side tracks moved in front of the
compute layer, and OpenExec lost its version with the move
([the status table](README.md#status-at-a-glance) has the argument). Neither new
row carries a phase number, and that is the §8 rule rather than an oversight:
the Workspace ladder tracks the migration out of the single `usdVrm` bundle, and
a greenfield reader takes its identity and edges from
[WORKSPACE.md](../architecture/WORKSPACE.md) §1 and §2 exactly as `motionSource`
and `motionBvh` did.

- ⬜ **Workspace Phase 8 — `execMotion` + `execVrm` bootstrap**, then **Motion
  Phase E** inside it. The OpenUSD 26.08 exact pin that was part of this
  milestone landed early, in v0.6.0, along with the `motionCore` `operator==`
  that OpenExec type registration requires.

### Canonical motion producer contract

*Freeze what a motion producer hands over, before the number of producers grows
again.* Four categories exist and each was designed on its own terms: recorded
sources (`motionSource` + a profile), live pose sources (`vrmAdapterVmc`,
`vrmAdapterMocopi`), tracker sources (`vrmAdapterVrchatOsc`), and generated
sources (none yet). They already agree in practice; nothing states the agreement,
so the fifth producer restates it.

**What is unified is the canonical value boundary, not an I/O API.** How a
producer gets its bytes is its own business — a socket, a file, a model — and
every attempt to unify *that* would put a transport shape into a library that
must not have one. The boundary is what crosses into canonical motion:

```text
Recorded:   SourceAnimation                  -> motion::HumanoidAnimation
Live:       timestamp + HumanoidPose
Tracker:    timestamp + TrackerFrame
Generator:  request/context                  -> HumanoidAnimation or a pose stream
```

- ⬜ State each of the four crossings as a contract, from the code that already
  implements three of them.
- ⬜ **Generation sits behind a vendor-neutral `IMotionGenerator`.** A research
  model or a commercial generator is an adapter under `adapters/generators/`,
  reaching canonical motion through this contract and never through a fifth
  shape of its own — which is the same rule
  [WORKSPACE.md §1](../architecture/WORKSPACE.md) already applies to
  `vrmAdapterArdy`.
- ⬜ Say what a tracker source may **not** do, once, rather than per adapter:
  the solve from tracker observations to humanoid bones is generic and outside
  every adapter ([the OSC track](osc-and-vrchat-trackers.md) §5).

Workspace phases establish boundaries; Motion phases fill them. They are never
the same milestone. Workspace Phase 6b and Motion Phase C both landed in v0.4.0
— the boundary and the behaviour together, because the retarget core is only
meaningful once something drives it end to end. Motion Phase D needed no new
boundary at all: v0.5.0 filled the `motionRuntime` boundary Phase 6b had already
established, and v0.6.0 added the first vendor leaf over it without moving
either. Workspace Phase 8 and Motion Phase E land together for the same reason
as v0.4.0.

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

**Motion Phases A–D have shipped** and are not restated here: the frozen
contract and `motionCore` (v0.3.0), `.vrma` import (v0.3.0), `vrmRetarget` and
`motion_retarget` (v0.4.0), and the live-capture surface (v0.5.0) with its
vendor half — the VMC adapter (v0.6.0) and the mocopi native live adapter
(v0.7.0). See [MOTION_CONTRACT.md](../design/MOTION_CONTRACT.md), the
[delivery history](../reports/delivery-history.md) §H–§K and the
[release records](../releases/). The ladder needs no OpenExec up to here: it
ends at a retargeted `UsdSkelAnimation`.

**Recorded-file ingestion shipped with v0.7.0 and does not extend the ladder.**
`motionSource` + `motionBvh` is the *other* surface of the same capture product,
and in kind it is Phase B's territory — a recorded clip becoming a canonical
semantic clip — with a different container and an explicit producer profile
where `.vrma` has a specification. Adding "Motion Phase I" for it would make the
string "Motion Phase A–H", which four documents repeat, mean something different
for no gain in what anyone can check; the same argument
[WORKSPACE.md §8](../architecture/WORKSPACE.md) makes about the workspace ladder
and greenfield libraries. The second format family is
[the recorded track](recorded-motion-sources.md) §13.

Still ahead:

- ⬜ **Motion Phase E — `execMotion` / `execVrm`.** ClipSample, PoseBuffer,
  HumanoidRetarget, RootMotionResolve, AvatarApply. Nodes are thin wrappers over
  `motionRuntime` and `vrmRetarget`, and each evaluates an immutable snapshot
  rather than a live source (motion policy §11.4). The
  [OpenExec plan](openexec-foundation.md) adds a display slice — re-scoped to
  `UsdGeomXformable`, because 26.08 cannot register a `UsdSkel` exec imaging
  adapter — and the optional `ExecIr` rig track on top of this description; §9
  there records that Phase E's scope needs to widen, or the ladder needs another
  phase, in the motion policy itself. Its parity input is the recorded corpus
  Motion Phase D's vendor half produces, which is why it is sequenced behind it.
- ⬜ **Motion Phase F — generation adapter.** `IMotionGenerator`,
  `MotionGenerationRequest`, text intent, root waypoints, sparse joint
  constraints, pose history, clip-ification. The contract is frozen before the
  first generator adapter is written, not derived from it
  ([adapters-mocopi-vmc-ardy.md](adapters-mocopi-vmc-ardy.md) §6, Milestones
  E–F).
- ⬜ **Motion Phase G — expression / look-at / recording.** VRMA **expression**
  animation landed 2026-08-23 (`/Animation/Expressions`, weights carried
  verbatim onto the pose and never expanded); what remains is VRMA look-at
  animation, `ExpressionResolve`, `LookAtEvaluate`, live recording, bake, and
  the VRMA export investigation.
  - ✅ **`ExpressionResolve` has its join key** *(2026-09-01)*. Both sides now
    author `vrm:expressionName` verbatim — on the avatar side as a
    `VrmExpressionAPI` builtin, additive within schema contract v1 — so the
    resolve step joins on that attribute and never on a prim name, which the
    two sides still sanitize with their own private tables. The importer's
    `VrmMakeUniqueNames` carried the counting-by-bases bug the clip side had
    already fixed, and it was **not** hypothetical: five source meshes named
    `Body`, `Body`, `Body_2`, `顔` and `""` imported as four prims, because
    `Define` on the duplicate path returns the existing prim rather than
    failing. It uniquifies against claimed names now, with a `usdvrm_path_util`
    unit test and the collision shape added to the `names.vrm` fixture.
  - ✅ **`ExpressionResolve` resolves** *(2026-09-01)*. `vrmRetarget`'s
    `ExpressionResolver` expands a named weight onto a rig's N morph targets
    across M meshes plus its material colours, joining on `vrm:expressionName`
    and never on a prim name. It takes plain values like the rest of that
    library, so the resolve is testable with no stage and `execVrm`'s future
    `Vrm.ExpressionResolve` node is a wrapper over it rather than a second
    implementation. Its decisions: a reported zero is authored and an unreported
    name contributes nothing, the specification's `[0, 1]` clamp lands in this
    layer and is reported per name, `isBinary` rounds on the way to the binds,
    and a material colour is carried as `(totalWeight, weightedTarget)` with an
    `Apply(base)` lerp so the material's own value never reaches a library that
    does not read stages. **What is left is the authoring**: nothing writes
    `blendShapeWeights` or a `skel:blendShapes` binding onto a stage yet, so no
    tool's output has changed.
- ⬜ **Motion Phase H — advanced.** Blending, IK / foot locking, contact
  handling, latency compensation, multi-performer sync, simulation bridge,
  generated-motion cache, publish pipeline.

### Motion-layer open questions

- ⬜ **Do the VRMA animation schemas belong in `vrmSchema`?** Motion policy §4.1
  names `VrmAnimationExpressionAPI` and `VrmAnimationLookAtAPI` as
  "equivalents" without fixing an owner. Adding them to `vrmSchema` is a schema
  contract change ([WORKSPACE.md](../architecture/WORKSPACE.md) §3); a separate
  `vrmaSchema` bundle avoids that but splits the contract. The expression half
  shipped ahead of the answer as **namespaced attributes on plain prims** —
  `vrm:expressionName`, `vrm:expressionType`, `vrm:expressionWeight` under
  `/Animation/Expressions/<name>` — which is what a typed API would carry
  anyway, so applying one later moves nothing and reverses nothing. That buys
  time; it does not answer the question, and the answer is owed before the
  look-at half repeats the pattern.
- ⬜ **Is the `motion:` USD namespace (motion policy §13) a typed schema or
  namespaced attributes?** Motion Plans are the one place the policy authors USD
  outside a file-format plugin.
- ⬜ **Where does the binding/assembly layer (motion policy §3.3) get authored
  from?** It is neither importer output nor retarget output; today nothing owns
  it.

## Product P5 — MToon realization

*Goal: source parameters preserved; a portable fallback exists; at least one
renderer reproduces the main MToon look; an image regression test exists.*
(design policy §9, §17-P5; the plan of record is
[material policy](../design/MATERIAL_ARCHITECTURE_POLICY.md) §7)

Today: source data is preserved, `UsdPreviewSurface` is the fallback, and
`vrm:mtoon:raw` carries the raw block. Renderer-specific realization is **not
implemented**.

The three steps below are P5's internal order, not a phase sequence. The schemas
come **last** so the first rendering improvements are not coupled to the schema
redesign.

- ✅ **Step 1 — shipped 2026-08-13.** The PreviewSurface network moved below a
  `/preview` `UsdShadeNodeGraph`, terminals run material → graph → shader, and
  the baseline diff was verified to be a path move and nothing else
  ([material policy](../design/MATERIAL_ARCHITECTURE_POLICY.md) §7.1)
- 🟡 **Step 2 — unlit shipped 2026-08-14.** Unlit materials carry a `/mtlx`
  `UsdShadeNodeGraph` on `outputs:mtlx:surface`, which is the terminal a
  MaterialX-aware renderer draws; the baseline diff is additive and `/preview`
  is untouched (§7.2). The node choice is `gltf_pbr` with the lit response
  zeroed, because MaterialX's direct unlit terminals do not render on the pinned
  runtime — §5.2.1 records what was measured and when to revisit it
- ⬜ **Step 2 (lit)** — the remaining half: glTF PBR materials through the same
  `gltf_pbr` terminal, so every material carries both realizations (§7.2)
- ⬜ **Step 3** — `VrmMaterialAPI` / `VrmMToonAPI` / `VrmTextureInfoAPI` as the
  canonical semantics both generators consume (§7.3)
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
- 🚧 **A motion corpus.** Two generated halves have shipped:
  [six synthetic traces](../../libs/motionRuntime/tests/corpus/README.md) in
  v0.5.0, and seven VMC packet captures in v0.6.0 — both generated by
  construction precisely so they carry no redistribution gate, and both
  reproducing *shapes* rather than any device's behavior. The real half is
  v0.7.0, and it splits: redistributable captures are committed, and everything
  else survives as a measured manifest with no bytes
  ([adapters plan §9.2](adapters-mocopi-vmc-ardy.md#92-corpus)). The BVH corpus
  lands under the same rule with one addition of its own — **a second producer
  from the start** ([BVH plan §8](recorded-motion-sources.md#8-corpus)), because
  a pipeline validated against one writer cannot tell its own assumptions from
  the format's. Still open beyond that: `.vrma` clips with known-good expected
  output, where licensing is the same gate the VRM corpus hit.

## Non-goals

Out of scope for these plugins — handle via schema, adapter, an OpenExec task,
or another plugin (design policy §15, §19; motion policy §8, §18):

- Full VRM runtime physics execution → `execVrm`
- Pixel-perfect MToon across all renderers
- Auto-repair of arbitrary broken glTF
- A full VRM exporter (P6 is research only)
- DCC-specific UI
- **Product-specific motion support in core.** Mocopi, VMC, ARDY, and any other
  named system are optional leaf adapters, never a core dependency or a branch
  condition. A **producer profile is the one exception, and it is data**: a
  `profiles/motion/*.yaml` may be named for a product because the library that
  reads it has no name for one — no producer identifier in code, no default
  profile, and a conversion that refuses rather than guesses. Ship every profile
  and the libraries are byte-identical; that is the test
  ([WORKSPACE.md §1](../architecture/WORKSPACE.md)).
- **A capture product's file format as that product's importer.** Recorded
  motion is read by a generic reader plus an explicit profile, never by a
  vendor-branded parser — otherwise the first writer's export silently becomes
  the format (motion policy §8.3).
- **Per-frame USD stage authoring for live playback.** Live evaluation produces
  transient poses; USD animation is authored only on bake / record / publish
  (motion policy §12.1).
- **Model latent representations in a shared USD schema** (motion policy §13).
- **I/O inside an OpenExec computation.** Sockets, SDK polling, file watching, a
  wall clock, mutable global state, or a private thread pool in a callback.
  Receiving belongs to an adapter and buffering to `motionRuntime`; a
  computation evaluates an immutable snapshot (motion policy §11.4).
- **`ExecIr` as the canonical motion contract**, or as a prerequisite for the
  standard pipeline. It is an optional experimental adapter (motion policy
  §11.5).

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
