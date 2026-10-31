# Native USD motion export on hosted runners

- Reviewed: 2026-10-31.
- Source CI commit: `1638b827bff93048cb9d1a36e21c5d3c51839da8`.
- Source CI: [run 41096542640](https://github.com/animu-sphere/usd-vrm-plugins/actions/runs/41096542640),
  pull request #285.
- Release dry run: [run 41158135473](https://github.com/animu-sphere/usd-vrm-plugins/actions/runs/41158135473),
  `workflow_dispatch` on `main` at `bc43c026f15573fa169e22610322c8626a373951`.
- Toolchain: pinned `ost 0.23.13`, cy2026 / OpenUSD 26.08.

## Hosted workspace result

All three workspace cells passed all 61 registered tests, including
`vrm_export_end_to_end` and `vrm_export_motion`.

| Workspace cell | Job | `vrm_export_motion` |
| --- | --- | --- |
| Windows x86-64 | [workspace-pr-windows](https://github.com/animu-sphere/usd-vrm-plugins/actions/runs/41096542640/job/124203661670) | PASS, 230.72 s |
| Linux x86-64 | [workspace-pr-linux](https://github.com/animu-sphere/usd-vrm-plugins/actions/runs/41096542640/job/124203661679) | PASS, 174.23 s |
| macOS arm64 | [workspace-pr-macos-arm64](https://github.com/animu-sphere/usd-vrm-plugins/actions/runs/41096542640/job/124203661506) | PASS, 180.60 s |

[`test_vrm_export_motion.py`](../../tools/vrmExport/tests/test_vrm_export_motion.py)
compares five avatar/clip pairs with the existing `motion_retarget` bake:
Seed-san with `canonical_walk` and `expressive_face`, the MToon expression
fixture with `expressive_face`, the look-at fixture with `gazing_head`, and
the VRM 0.x front-bake fixture with `canonical_walk`.

Each pair is exported to `.usda`, `.usdc` and `.usdz`, both composed and
flattened: 30 animated outputs per host. Plugin-free readback compares keyed
attributes, resolved UsdSkel joint-local transforms, stage metrics and
texture digests with the bake, then repeats after hiding the source files.
The suite also checks dependency-report byte counts, repeated export bytes,
temporary-file cleanup and failure cases for unavailable inputs, VRMA
registration and the bake tool.

## Release artifact result

The complete release dry run passed: all three product cells, both lookdev
cells, the publish job's dry-run path and release assembly. The
`release-dry-run` artifact was uploaded. This dispatch created no GitHub
release and performed no registry push.

| Product cell | Job | Artifact-only export smoke |
| --- | --- | --- |
| Windows x86-64 | [release-windows](https://github.com/animu-sphere/usd-vrm-plugins/actions/runs/41158135473/job/124409432354) | PASS |
| Linux x86-64 | [release-linux](https://github.com/animu-sphere/usd-vrm-plugins/actions/runs/41158135473/job/124409432275) | PASS |
| macOS arm64 | [release-macos-arm64](https://github.com/animu-sphere/usd-vrm-plugins/actions/runs/41158135473/job/124409432263) | PASS |

[`artifact_only_export_smoke.py`](../../scripts/artifact_only_export_smoke.py)
verifies and installs the product outside the checkout, with inherited
plugin paths and checkout loader paths removed. Its static cases still
check Seed-san (668 prims, 86 nonempty asset attributes) and `mtoon_vrm0`
(155 prims, 23 asset attributes) in plugin-free `.usdz` readback.

The motion case runs the installed `vrm_export` with
`--motion canonical_walk.vrma --check` on Seed-san. Plugin-free readback
resolves one animated skeleton whose joint-local transforms differ between
the first and last stage times, with no unresolved asset attributes. Removing the
product's `motion_retarget` executable makes motion export refuse with exit
4 naming the tool. Removing the product's importer registration makes the
static export refuse with exit 3 naming `usdVrmFileFormat`. All three logs
report `artifact-only export smoke: PASS`.

## Boundary of the finding

This closes the [export track](../roadmap/export-track.md)'s Step 3 hosted
workspace and installed-artifact proof. The product smoke covers one body
clip and `.usdz`; the workspace suite covers the wider format, flattening,
expression and gaze matrix. The earlier
[2026-10-29 release dry run](2026-10-29-export-release-dry-run.md) predates
`--motion` and covers static export only. No renderer playback or spring
simulation is measured by this suite.

The [build-machine RPATH carry-over](../roadmap/current.md#carried-out-of-v090)
remains: the exec smoke counted 6 checkout RPATH entries in 6 binaries on
Linux, and 8 in 6 binaries on macOS, with nothing loaded from those paths.
