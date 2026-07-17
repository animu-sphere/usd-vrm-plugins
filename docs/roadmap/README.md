# Roadmap

The roadmap holds only **incomplete** work. Shipped work lives in the
[delivery history](../reports/delivery-history.md) and, once `v0.1.0` is tagged,
in per-version [release records](../releases/). Design rationale lives in
[design/](../design/).

Legend: 🚧 in progress · ⬜ not started · ⛔ blocked

| Document | Contents |
| --- | --- |
| [current.md](current.md) | The next milestone (`v0.1.0`) and active carry-over work. |
| [backlog.md](backlog.md) | Ordered but unscheduled work: the milestone ladder beyond next, future phases, and cross-cutting open items. |

## Two phase systems, deliberately separate

This repository tracks two independent sequences. They are never abbreviated to
a bare "Phase 4" — always qualified:

| Sequence | Notation | What it tracks | Source of truth |
| --- | --- | --- | --- |
| Product roadmap | **Product P0–P6** | What the plugins do for users: docs, release, canonical contract, runtime verification, `execVrm`, MToon, round-trip. | [design/DESIGN_POLICY.md](../design/DESIGN_POLICY.md) §17 |
| Workspace migration | **Workspace Phase 0–6** | How the code is split into bundles: baseline, schema split, container extraction, resolver split, rename, packaging, `execVrm` bootstrap. | [architecture/WORKSPACE.md](../architecture/WORKSPACE.md) §8 |

Where they meet: **Workspace Phase 6 establishes the `execVrm` bundle boundary
and packaging. Product P4 implements runtime behavior incrementally within that
boundary.** The workspace sequence answers "where does the code live"; the
product sequence answers "what does it do".

An earlier draft used "Phase A–E" and an importer-specific "Phase 1–4"; both are
retired. The importer build-out those numbers tracked is complete and recorded
in the [delivery history](../reports/delivery-history.md).

## Status at a glance

- The importer feature build-out and the workspace split through **Workspace
  Phase 4** are complete.
- Current priorities: cut `v0.1.0`, land **Workspace Phase 5** packaging, widen
  runtime verification, and start the first `execVrm` vertical slice.

## Quality bar (applies to every phase)

- The importer authors data and never evaluates or simulates it.
- Authored stage semantics do not change without a schema contract bump.
- Every bundle builds standalone against installed packages, not just composed
  in the workspace tree.
- Dependency directions in [WORKSPACE.md](../architecture/WORKSPACE.md) §2 are
  enforced by CI, not convention. *(Partially met — the graph gate is not yet
  wired; see [current.md](current.md).)*
- Every documented command is one that has actually been run, and no document
  contradicts what CI does.
