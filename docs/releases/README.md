# Release records

Each released version gets an immutable record here: its objective, the
capabilities it shipped, compatibility notes, and known limitations. Release
records are history — once written for a released version they are not
rewritten; new work goes to a new record. Active, incomplete work lives in the
[roadmap](../roadmap/), not here.

| Version | Record | Theme |
| --- | --- | --- |
| v0.3.0 | [v0.3.0.md](v0.3.0.md) | Motion foundation: `motionCore` + `usdVrmaFileFormat`; canonical `.vrma` animation import (no retarget yet) |
| v0.2.0 | [v0.2.0.md](v0.2.0.md) | The multi-bundle workspace: `vrmSchema` / `usdVrmFileFormat` / `usdVrmPackageResolver` ship separately (artifact-breaking) |
| v0.1.0 | [v0.1.0.md](v0.1.0.md) | First public release: VRM 0.x / 1.0 import, typed schemas, schema contract v1 |

## How a release is cut

`.github/workflows/release.yml` fires on a `vX.Y.Z` tag that **matches the repo
[`VERSION`](../../VERSION) file**, with that version's changelog section
finalized. It builds every bundle on all three OS cells, proves the packaged
artifacts (reproducible packaging + the composed clean-install smoke — see
[v0.2.0](v0.2.0.md) for why a per-bundle `--from-package` gate cannot stand in),
and assembles a **draft** release with notes rendered via
[contributing/RELEASE_NOTES_TEMPLATE.md](../contributing/RELEASE_NOTES_TEMPLATE.md).
Publishing the draft is a human decision. `workflow_dispatch` runs a dry run
that creates no release.

Other sources of truth:

- **What shipped, granularly:** the [delivery history](../reports/delivery-history.md).
- **What ships in an artifact:** [reference/SUPPORTED_CONFIGURATIONS.md](../reference/SUPPORTED_CONFIGURATIONS.md).
