# Roadmap

This index owns priorities and target versions. **Implemented**, **verified**
and **released** are separate findings: current capabilities are in the
[reference](../reference/); shipped history is in the
[CHANGELOG](../../CHANGELOG.md) and [release records](../releases/).

| Document | Responsibility |
| --- | --- |
| [current.md](current.md) | Open work for the next milestone only; no completed checklists. |
| [backlog.md](backlog.md) | Unstarted candidates owned by this repository, with triggers and acceptance evidence. |
| [vrm-computations.md](vrm-computations.md) | Scope review for standard VRM expression/look-at computations, independent of ExecIr. |
| [execir-track.md](execir-track.md) | Optional research; implementation is conditional on a responsibility audit. |
| [ideas/](../ideas/) | Uncommitted export, round-trip and integration research. |

The old material, export and spring-bone track paths are compatibility stubs;
their completed plans are in the [archive](../archive/).

## Status at a glance

This is the only table assigning target versions. A proposal is not a release
commitment. P0 precedes P1; P2 candidates wait for their recorded trigger.

| Track | Status | Priority | Owner | Target |
| --- | --- | --- | --- | --- |
| Native export Steps 2–3, spring solver and schema contract v2 | Implemented; existing source/export evidence linked from the audit; candidate-wide closure pending | P0 | usd-vrm-plugins | v0.11.0 candidate |
| Release closure and publication | active; latest hosted gate proof still required | P0 | usd-vrm-plugins | v0.11.0 candidate |
| Package portability: RPATH, symbols, bundle resolution and CI coverage | backlog; upstream conditions in backlog | P1 | usd-vrm-plugins | v0.12.0 proposal |
| VRM expression/look-at computations | backlog; scope review before implementation | P2 | usd-vrm-plugins | v0.13.0 proposal, uncommitted |
| ExecIr adapters, native export profiles and true VRM round-trip | research; no implementation commitment | P2 | usd-vrm-plugins | unscheduled |
| Other importer/schema candidates | backlog; trigger required | P2 | usd-vrm-plugins | unscheduled |

MToon semantics and Hydra data delivery are complete here; rendering and image
conformance belong to [hydra-toon](https://github.com/animu-sphere/hydra-toon/tree/main/docs/roadmap).
Spring-bone resident rendering, fixed-tick hosts and Hydra/direct parity belong
to [usd-avatar-runtime](https://github.com/animu-sphere/usd-avatar-runtime/tree/main/docs/roadmap).
Generic motion and input belong to
[usd-motion-plugins](https://github.com/animu-sphere/usd-motion-plugins/tree/main/docs/roadmap)
and [motion-connectors](https://github.com/animu-sphere/motion-connectors/tree/main/docs/roadmap).
They are dependencies, not duplicated milestones here.

## Quality bar

- The importer authors data; it does not evaluate or simulate.
- Authored semantics change only with the appropriate schema contract and migration notes.
- Every bundle builds standalone against installed dependencies.
- Dependency directions in [WORKSPACE.md](../architecture/WORKSPACE.md) are enforced by graph and CMake boundary checks.
- Every installed CMake package has an explicit closure and row in [PACKAGE_CONTRACT.md](../architecture/PACKAGE_CONTRACT.md).
- Release claims require the [release closure](../contributing/release-closure.md) at the candidate commit.
- Lifecycle and owner rules are in [documentation.md](../contributing/documentation.md).
