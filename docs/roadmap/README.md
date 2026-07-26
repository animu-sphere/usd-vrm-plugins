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
- **v0.5.0 is released** (Motion Phase D, live capture). It also put the motion
  layer under CI for the first time and pinned every lane to OpenUSD 26.08.
- Current priorities: close the remaining **Workspace Phase 5** packaging P0,
  widen runtime verification, and begin the OpenExec foundation.
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
  enforced by CI, not convention. *(Met since v0.5.0 — `ost plugin test
  --workspace` runs on every PR on all three OS in `motion-ci.yml`.)*
- Every documented command is one that has actually been run, and no document
  contradicts what CI does.
