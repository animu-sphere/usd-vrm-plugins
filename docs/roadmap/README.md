# Roadmap

The roadmap holds only **incomplete** work. Shipped work lives in the
[delivery history](../reports/delivery-history.md) and, once `v0.1.0` is tagged,
in per-version [release records](../releases/). Design rationale lives in
[design/](../design/).

Legend: 🚧 in progress · ⬜ not started · ⛔ blocked

| Document | Contents |
| --- | --- |
| [current.md](current.md) | The next milestone and active carry-over work. |
| [backlog.md](backlog.md) | Ordered but unscheduled work: the milestone ladder beyond next, the motion layer, future phases, and cross-cutting open items. |
| [openexec-v0.6.0-v0.7.0.md](openexec-v0.6.0-v0.7.0.md) | The OpenExec direction: the OpenUSD 26.08 exact pin, the `execMotion` / `execVrm` foundation, and the `ExecIr` invertible rig. Two milestones out; kept separate because it is a plan, not a status list. |
| [adapters-mocopi-vmc-ardy.md](adapters-mocopi-vmc-ardy.md) | The input-adapter direction: a VMC Protocol adapter, then a direct capture-product adapter, then a generation adapter — unscheduled, and completing end to end with no OpenExec dependency. The filename keeps the original triple; the order inside was reversed on 2026-07-29. |

## Three sequences, deliberately separate

This repository tracks three independent sequences. They are never abbreviated
to a bare "Phase 4" or "Phase A" — always qualified:

| Sequence | Notation | What it tracks | Source of truth |
| --- | --- | --- | --- |
| Product roadmap | **Product P0–P6** | What the plugins do for users: docs, release, canonical contract, runtime verification, runtime layer, MToon, round-trip. | [design/DESIGN_POLICY.md](../design/DESIGN_POLICY.md) §17 |
| Workspace migration | **Workspace Phase 0–8** | Where the code lives: baseline, schema split, container extraction, resolver split, rename, packaging, motion libraries, `usdVrmaFileFormat`, `execMotion`/`execVrm`. | [architecture/WORKSPACE.md](../architecture/WORKSPACE.md) §8 |
| Motion runtime | **Motion Phase A–H** | How motion works: contract freeze, `.vrma` import, offline retarget, live capture, OpenExec, generation, expression/look-at, advanced. | [design/MOTION_ARCHITECTURE_POLICY.md](../design/MOTION_ARCHITECTURE_POLICY.md) §16 |

How they meet:

- The **workspace** sequence answers *where does the code live* — it establishes
  a boundary, manifest, and packaging, and nothing more.
- The **motion** sequence answers *how does motion work* — it fills those
  boundaries with behavior.
- The **product** sequence answers *what does the user get*. Product P4 is now
  the umbrella for the runtime layer and delegates its detail to Motion Phase
  A–H rather than enumerating nodes itself.

So Workspace Phase 8 creates the `execVrm` bundle; Motion Phase E implements its
nodes; Product P4 is "done" when a user can drive an avatar from a clip, a live
capture, or a generator without changing the importer.

An earlier draft used "Phase A–E" and an importer-specific "Phase 1–4"; both are
retired. The importer build-out those numbers tracked is complete and recorded
in the [delivery history](../reports/delivery-history.md). **The new Motion Phase
A–H is unrelated to that retired A–E** and always carries the "Motion" qualifier.

## Status at a glance

- The workspace covers **Workspace Phase 6b and 7**: `motionCore`,
  `motionRuntime`, `vrmRetarget`, and `usdVrmaFileFormat` implement Motion
  Phases A–D. Only Phase 8 (`execMotion` / `execVrm`) is unbuilt.
- **v0.5.0 is released** (Motion Phase D, live capture). Every lane is pinned to
  OpenUSD 26.08 — and since v0.6.0's first change, no other OpenUSD will
  configure at all.
- **The motion layer has CI.** `ost` 0.21.0's `kind: workspace` cells build the
  root tree and run its whole CTest suite on all three OS; the v0.5.0
  hand-written lane is deleted.
- Current priorities: the **OpenExec foundation** (v0.6.0), closing the
  remaining **Workspace Phase 5** packaging P0, and widening runtime
  verification. The [input adapters](adapters-mocopi-vmc-ardy.md) are planned
  but unscheduled — and deliberately **not** sequenced behind v0.6.0: they
  complete from input to retargeted `UsdSkelAnimation` without OpenExec.
- v0.6.0's display slice is **re-scoped** (2026-07-29). OpenUSD 26.08 resolves
  exec prim adapters from a hard-coded list, so a skinned VRM avatar cannot be
  displayed through the exec scene index at all. v0.6.0 proves the mechanism on
  an exec-computed `UsdGeomXformable`; realtime skinned display is a later
  milestone, not a v0.6.0 or v0.7.0 release condition.
- The milestone ladder is **v0.6.0 → v0.7.0**:

  | Release | Theme | Sequences |
  | --- | --- | --- |
  | v0.6.0 | OpenExec VRM runtime foundation | Workspace Phase 8, Motion Phase E, Product P4 |
  | v0.7.0 | `ExecIr` invertible VRM humanoid rig | Motion Phase E cont., Product P4 |

  v0.6.0 and v0.7.0 are planned in
  [openexec-v0.6.0-v0.7.0.md](openexec-v0.6.0-v0.7.0.md).

## Quality bar (applies to every phase)

- The importer authors data and never evaluates or simulates it.
- Authored stage semantics do not change without a schema contract bump.
- Every bundle builds standalone against installed packages, not just composed
  in the workspace tree.
- Dependency directions in [WORKSPACE.md](../architecture/WORKSPACE.md) §2 are
  enforced by CI, not convention. *(Met since the `ost` 0.21.0 adoption: the
  `workspace-graph-pr` cell runs `ost plugin test --workspace --graph-only` on
  every PR, before anything is built.)*
- Every documented command is one that has actually been run, and no document
  contradicts what CI does.
