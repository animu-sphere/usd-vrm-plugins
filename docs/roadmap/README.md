# Roadmap

The roadmap holds only **incomplete** work owned by this repository. Shipped
work is in the [CHANGELOG](../../CHANGELOG.md) and the
[release records](../releases/); completed and superseded plans are in the
[archive](../archive/). Rationale lives in [design/](../design/).

Generic motion work is planned in
[`usd-motion-plugins`' roadmap](https://github.com/animu-sphere/usd-motion-plugins/tree/main/docs/roadmap)
and input work in
[`motion-connectors`' roadmap](https://github.com/animu-sphere/motion-connectors/tree/main/docs/roadmap);
neither is mirrored here.

Legend: ✅ done · 🚧 in progress · ⬜ not started · ⛔ blocked

| Document | Contents |
| --- | --- |
| [current.md](current.md) | The next release's open conditions, and work carried out of shipped releases. |
| [backlog.md](backlog.md) | Ordered but unscheduled work: the product tracks, VRM motion open questions, cross-cutting items, non-goals. |
| [execir-track.md](execir-track.md) | The `ExecIr` invertible VRM humanoid rig, expression and look-at computations, and skinned display — the OpenExec work this repository still owns. |
| [export-track.md](export-track.md) | `vrm_export`: an imported `.vrm` written as a native `.usda`, `.usdc` or `.usdz` that opens with no VRM plugin installed. |
| [material-track.md](material-track.md) | Product P5's open steps: the canonical MToon schemas first, then importer canonicalization, both realizations generated from them, expression material binds, and the `hydra-toon` boundary. |

## Sequences

Two sequences are live, and a reference to either is always qualified with
its name — `usd-vrm-plugins` Product P4, Workspace Phase 5 — never a bare
"Phase 4":

| Sequence | Notation | What it tracks | Source of truth |
| --- | --- | --- | --- |
| Product roadmap | **Product P0–P6** | What the plugins do for users: docs, release, canonical contract, runtime verification, VRM semantics on motion, MToon, round-trip. | [design/DESIGN_POLICY.md](../design/DESIGN_POLICY.md) §17 |
| Workspace migration | **Workspace Phase 0–8** | Where the code lives. Every phase has landed except Phase 5's packaging P0. | [architecture/WORKSPACE.md](../architecture/WORKSPACE.md) §8 |

A phase is not a release; which release carries a track is the table below.
Retired vocabularies — Motion Phase A–H, MIG-0 to MIG-5, BND-0 to BND-5, and
`usd-motion-plugins`' Migration Phase A–F as this repository used it — appear
only in the [archive](../archive/), release records and reports.

## Status at a glance

**This table is the single source of truth for which release a track lands
in.** Every other document names a track and defers the version here; where a
document must repeat one, `scripts/check_docs.py` checks it against this
table.

| Track | Status | Target |
| --- | --- | --- |
| OpenExec foundation | Shipped | v0.9.0 |
| release-artifact proof of the reduced product | Planned | the next release |
| release closure checklist and checkable invariants | Planned | unscheduled |
| `ExecIr` invertible VRM humanoid rig | Planned | unscheduled |
| MToon canonical semantics (Product P5) | In progress | unscheduled |
| Native USD export (`vrm_export`) | Planned | unscheduled |

How the motion tracks were ordered before they left this repository is
[archive/motion-split/roadmap-orderings.md](../archive/motion-split/roadmap-orderings.md).

## Quality bar (applies to every phase)

- The importer authors data and never evaluates or simulates it.
- Authored stage semantics do not change without a schema contract bump.
- Every bundle builds standalone against installed packages, not only
  composed in the workspace tree;
  [PACKAGE_CONTRACT.md](../architecture/PACKAGE_CONTRACT.md) records which
  packages have been measured rather than only reviewed.
- Dependency directions in [WORKSPACE.md](../architecture/WORKSPACE.md) §2 are
  enforced by CI (`ost plugin test --workspace --graph-only`), not convention.
- Every documented command is one that has actually been run, and no document
  contradicts what CI does.
- A package's dependency closure is explicit: every `PUBLIC`/`INTERFACE` edge
  is a `find_dependency` in its config and a row in PACKAGE_CONTRACT.md.
- The line between public API and internal implementation can be stated for
  every identity; the installed header root is that line.
- A sibling repository's contract is linked, never restated
  ([contributing/documentation.md](../contributing/documentation.md)).
