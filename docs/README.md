# USD VRM Plugins documentation

Documentation is organized by responsibility, and each category answers one
class of question — the layout `usd-motion-plugins`, `motion-connectors`,
`open-strata` and `hydra-merlin` share. Every subject has one owning
document; this page says which.

When a document disagrees with the implementation, the implementation wins
and the document is a bug. When a summary disagrees with
[architecture/WORKSPACE.md](architecture/WORKSPACE.md) about *structure*, the
contract wins — structural changes go there first, in their own PR.

| Category | Answers | Start here |
| --- | --- | --- |
| [design/](design/) | What the intended contracts are, and why. | [DESIGN_POLICY.md](design/DESIGN_POLICY.md) · [VRM_MOTION_POLICY.md](design/VRM_MOTION_POLICY.md) |
| [architecture/](architecture/) | Which components exist, how they depend on each other, and what each installed package promises. | [WORKSPACE.md](architecture/WORKSPACE.md) · [PACKAGE_CONTRACT.md](architecture/PACKAGE_CONTRACT.md) |
| [reference/](reference/) | What is implemented now, and on what. | [CAPABILITY_MATRIX.md](reference/CAPABILITY_MATRIX.md) · [SUPPORTED_CONFIGURATIONS.md](reference/SUPPORTED_CONFIGURATIONS.md) |
| [roadmap/](roadmap/) | What incomplete work remains. | [README.md](roadmap/README.md) · [current.md](roadmap/current.md) |
| [guides/](guides/) | How to perform a task. | [INSTALL.md](guides/INSTALL.md) · [VIEWING_MOTION.md](guides/VIEWING_MOTION.md) |
| [releases/](releases/) | What shipped in a released version. | [README.md](releases/README.md) |
| [reports/](reports/) | What was measured or observed. | [README.md](reports/README.md) |
| [archive/](archive/) | What used to be planned or authoritative and is now superseded. | [README.md](archive/README.md) |
| [contributing/](contributing/) | How these documents are maintained. | [documentation.md](contributing/documentation.md) |

## Source of truth

| Question | Owner |
| --- | --- |
| How far this repository goes, and what it does not own | [design/INTEGRATION_SCOPE_POLICY.md](design/INTEGRATION_SCOPE_POLICY.md) |
| The `.vrm` importer, the canonical model, the import / evaluation boundary, Product P0–P6 | [design/DESIGN_POLICY.md](design/DESIGN_POLICY.md) |
| How VRM and VRMA use motion: `.vrma` import, composition, the VRM rig binding, expressions, look-at, the bake, `execVrm`, `ExecIr` | [design/VRM_MOTION_POLICY.md](design/VRM_MOTION_POLICY.md) |
| The `/Asset/mtl` hierarchy and MToon | [design/MATERIAL_ARCHITECTURE_POLICY.md](design/MATERIAL_ARCHITECTURE_POLICY.md) |
| Workspace identities, dependency directions, artifact naming, Workspace Phase 0–8 | [architecture/WORKSPACE.md](architecture/WORKSPACE.md) |
| What a consumer writes to use an installed package | [architecture/PACKAGE_CONTRACT.md](architecture/PACKAGE_CONTRACT.md) |
| The VRM schemas | [plugins/vrmSchema/docs/SCHEMA_CONTRACT.md](../plugins/vrmSchema/docs/SCHEMA_CONTRACT.md) |
| Implemented capabilities; platforms and OpenUSD versions | [reference/](reference/) |
| Incomplete work, and which release carries it | [roadmap/](roadmap/) |
| Released history | [releases/](releases/) and the [CHANGELOG](../CHANGELOG.md) |
| How documents here are maintained, and how they cite sibling repositories | [contributing/documentation.md](contributing/documentation.md) |

Where the design policies overlap, the narrower one wins:
VRM_MOTION_POLICY.md and MATERIAL_ARCHITECTURE_POLICY.md over
DESIGN_POLICY.md on their subjects, and none of them over WORKSPACE.md on
structure.

## Owned elsewhere

This repository consumes generic motion and does not define it. Each of these
is linked, never restated here:

| Subject | Owner |
| --- | --- |
| Generic motion: values, sampling, filtering, recording, retargeting, the OpenUSD motion mapping, generic OpenExec evaluation | [`usd-motion-plugins`](https://github.com/animu-sphere/usd-motion-plugins/tree/main/docs) |
| Device and protocol input, source profiles, tracker observations | [`motion-connectors`](https://github.com/animu-sphere/motion-connectors/tree/main/docs) |

## Superseded

[design/MOTION_ARCHITECTURE_POLICY.md](design/MOTION_ARCHITECTURE_POLICY.md)
and [design/MOTION_CONTRACT.md](design/MOTION_CONTRACT.md) were this
repository's motion policy and motion contract until the motion split. Each
is now a stub mapping its former sections to their current owners.

## Per-component documentation

Component-specific docs live with their component, not here:

| Component | Docs |
| --- | --- |
| `vrmSchema` | [README](../plugins/vrmSchema/README.md) · [SCHEMA_CONTRACT.md](../plugins/vrmSchema/docs/SCHEMA_CONTRACT.md) |
| `usdVrmFileFormat` | [README](../plugins/usdVrmFileFormat/README.md) · [DIAGNOSTICS.md](../plugins/usdVrmFileFormat/docs/DIAGNOSTICS.md) · [CORPUS.md](../plugins/usdVrmFileFormat/tests/corpus/CORPUS.md) |
| `usdVrmPackageResolver` | [README](../plugins/usdVrmPackageResolver/README.md) |
| `usdVrmaFileFormat` | [README](../plugins/usdVrmaFileFormat/README.md) |
| `execVrm` | [README](../plugins/execVrm/README.md) |
| `vrmContainer` | [README](../libs/vrmContainer/README.md) |
| `vrmRig` | [README](../libs/vrmRig/README.md) |
| `motion_retarget` | [README](../tools/motionRetarget/README.md) |
