# USD VRM Plugins documentation

Documentation is organized by responsibility: each category answers one class of
question. This mirrors the layout used across `open-strata` and `hydra-merlin`,
so the three repositories read the same way.

When a summary disagrees with the implementation, the implementation wins and
the summary is a bug. When a summary disagrees with
[architecture/WORKSPACE.md](architecture/WORKSPACE.md) about *structure*, the
contract wins — structural changes go there first, in their own PR.

| Category | Answers | Start here |
| --- | --- | --- |
| [architecture/](architecture/) | How the workspace is structured: bundle identities, dependency directions, artifact naming. | [WORKSPACE.md](architecture/WORKSPACE.md) |
| [guides/](guides/) | How to accomplish a task. | [INSTALL.md](guides/INSTALL.md) |
| [reference/](reference/) | Factual contracts: what is supported, on what. | [CAPABILITY_MATRIX.md](reference/CAPABILITY_MATRIX.md) |
| [roadmap/](roadmap/) | What is planned next (only incomplete work). | [README.md](roadmap/README.md) |
| [releases/](releases/) | Immutable per-version release records. | [README.md](releases/README.md) |
| [design/](design/) | Why significant decisions were made. | [DESIGN_POLICY.md](design/DESIGN_POLICY.md) |
| [reports/](reports/) | Evidence from real runs: the `ost` dogfooding series and the delivery log. | [README.md](reports/README.md) |
| [contributing/](contributing/) | Contributor procedures. | [RELEASE_NOTES_TEMPLATE.md](contributing/RELEASE_NOTES_TEMPLATE.md) |

## Canonical documents

- [design/DESIGN_POLICY.md](design/DESIGN_POLICY.md) is the long-form design &
  development policy — the source of truth for **Product P0–P6** and for the
  import / evaluation / simulation boundary.
- [architecture/WORKSPACE.md](architecture/WORKSPACE.md) is the binding workspace
  contract — the source of truth for bundle identities, dependency directions,
  artifact naming, and **Workspace Phase 0–6**.

The two phase systems are separate and always qualified; see
[roadmap/README.md](roadmap/README.md#two-phase-systems-deliberately-separate).

## Per-bundle documentation

Bundle-specific docs live with their bundle, not here:

| Bundle | Docs |
| --- | --- |
| `vrmSchema` | [README](../plugins/vrmSchema/README.md) · [SCHEMA_CONTRACT.md](../plugins/vrmSchema/docs/SCHEMA_CONTRACT.md) |
| `usdVrmFileFormat` | [README](../plugins/usdVrmFileFormat/README.md) · [DIAGNOSTICS.md](../plugins/usdVrmFileFormat/docs/DIAGNOSTICS.md) · [CORPUS.md](../plugins/usdVrmFileFormat/tests/corpus/CORPUS.md) |
| `usdVrmPackageResolver` | [README](../plugins/usdVrmPackageResolver/README.md) |
| `vrmContainer` | [README](../libs/vrmContainer/README.md) |
