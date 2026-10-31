# Native USD export from the release product

- Measured: 2026-10-29; results reviewed 2026-10-31.
- Commit: `8c1f999131cec47a706cf9f5439fa2c3c7ae1673`.
- Run: [release dry run 40883692973](https://github.com/animu-sphere/usd-vrm-plugins/actions/runs/40883692973),
  `workflow_dispatch` on `main`.
- Toolchain: pinned `ost 0.23.13`, cy2026 / OpenUSD 26.08, `usd` profile
  for the product and `lookdev` for the two imaging packages.

## Result

The complete workflow passed, including all three product cells, both
lookdev cells, the publish job's dry-run path and release assembly. The
`release-dry-run` workflow artifact was uploaded; dispatch creates no GitHub
release and performs no registry push.

| Product cell | Job | Artifact-only export smoke |
| --- | --- | --- |
| Windows x86-64 | [release-windows](https://github.com/animu-sphere/usd-vrm-plugins/actions/runs/40883692973/job/123481718294) | PASS |
| Linux x86-64 | [release-linux](https://github.com/animu-sphere/usd-vrm-plugins/actions/runs/40883692973/job/123481718499) | PASS |
| macOS arm64 | [release-macos-arm64](https://github.com/animu-sphere/usd-vrm-plugins/actions/runs/40883692973/job/123481718664) | PASS |

## What the export smoke measured

[`artifact_only_export_smoke.py`](../../scripts/artifact_only_export_smoke.py)
verifies and installs the freshly packaged product outside the checkout.
It removes inherited plugin paths and checkout loader paths, then uses the
runtime environment and the installed product's activation.

The installed `vrm_export` writes each input to `.usdz` with `--check`.
A separate process then opens the output with the product's plugin paths
removed. The counts below were identical on all three hosts:

| Input | Traversed prims | Nonempty asset attributes checked | Readback |
| --- | --- | --- | --- |
| `Seed-san.vrm` (VRM 1.0) | 668 | 86 | Every asset resolves to nonempty bytes inside the package; no VRM plugin registered |
| `mtoon_vrm0.vrm` (VRM 0.x) | 155 | 23 | The same |

The asset counts are references on USD attributes, not distinct image files.
Both outputs have `defaultPrim = Asset`. With the product's one
`usdVrmFileFormat` registration moved aside, exporting Seed-san refuses with
exit 3 and names `usdVrmFileFormat` on every host.

## Boundary of the finding

This closes the [export track](../archive/export/export-track.md)'s Step 2 condition
that the exporter run from the release artifact. The full run also passed
the build-tree verification pyramid, repeat packaging with equal digests,
the packaged-library check, clean-install smoke, artifact-only exec and
imaging smokes, and the packaged VRMA reader.

The export smoke covers these two textured inputs and `.usdz`; the
build-tree end-to-end suite holds the wider fixture and output-format matrix.
The existing [build-machine RPATH carry-over](../archive/release-preparation/current-before-cleanup.md#carried-out-of-v090)
remains: the exec smoke counted 6 checkout RPATH entries in 6 binaries on
Linux, and 8 in 6 binaries on macOS, while reporting that no module loaded
from those paths. Motion assembly remains Step 3's work.
