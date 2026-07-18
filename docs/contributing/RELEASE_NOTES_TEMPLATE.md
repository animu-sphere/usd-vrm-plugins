# usd-vrm-plugins {tag}

`usdVrm` — an OpenUSD plugin workspace for VRM avatars. `usdVrmFileFormat`
imports `.vrm` (VRM 0.x / 1.0) as typed USD stages; `vrmSchema` provides the
typed schemas it applies; `usdVrmPackageResolver` serves embedded textures — plus
the validation / reporting tooling around them.

- **Schema contract version:** {schema_contract}
  ([SCHEMA_CONTRACT.md](https://github.com/animu-sphere/usd-vrm-plugins/blob/{tag}/plugins/vrmSchema/docs/SCHEMA_CONTRACT.md))
- **Supported configurations:** [SUPPORTED_CONFIGURATIONS.md](https://github.com/animu-sphere/usd-vrm-plugins/blob/{tag}/docs/reference/SUPPORTED_CONFIGURATIONS.md)
- **Capability matrix:** [CAPABILITY_MATRIX.md](https://github.com/animu-sphere/usd-vrm-plugins/blob/{tag}/docs/reference/CAPABILITY_MATRIX.md)
- **Install guide:** [INSTALL.md](https://github.com/animu-sphere/usd-vrm-plugins/blob/{tag}/docs/guides/INSTALL.md)
- **Workspace layout:** [WORKSPACE.md](https://github.com/animu-sphere/usd-vrm-plugins/blob/{tag}/docs/architecture/WORKSPACE.md)

{changelog}

## Artifacts

| artifact | contents |
| --- | --- |
| `vrmSchema-{version}-<target>.tar.zst` | typed `Vrm*API` schema bundle for `<target>` (lean) |
| `usdVrmFileFormat-{version}-<target>.tar.zst` | `.vrm` importer bundle for `<target>` (lean) |
| `usdVrmPackageResolver-{version}-<target>.tar.zst` | embedded-texture package resolver for `<target>` (lean) |
| `<bundle>-{version}-<target>-debug.tar.zst` | split debug symbols matching each lean bundle |
| `<bundle>-{version}-<target>.manifest.json` | OpenStrata manifest sidecar per bundle |
| `usd-vrm-plugins-{version}-src.tar.gz` | source archive at this tag |
| `SHA256SUMS` | SHA-256 checksums of every file above |

> **Install all three bundles together.** They are separate artifacts but one
> product: `usdVrmFileFormat` alone registers the `.vrm` file format and then
> **fails to open a stage**, because the typed `Vrm*API` schemas it applies live
> in `vrmSchema` and are not staged into its package (OpenStrata stages a
> dependency bundle's link-time half without its USD registration half).
> Embedded-texture resolution likewise needs `usdVrmPackageResolver`. Put all
> three on the plugin path — see the
> [install guide](https://github.com/animu-sphere/usd-vrm-plugins/blob/{tag}/docs/guides/INSTALL.md).
>
> A single aggregate artifact
> (`usdVrmPlugins-{version}-<target>.tar.zst`) is still pending upstream work —
> see [WORKSPACE.md §5](https://github.com/animu-sphere/usd-vrm-plugins/blob/{tag}/docs/architecture/WORKSPACE.md)
> and the [roadmap](https://github.com/animu-sphere/usd-vrm-plugins/blob/{tag}/docs/roadmap/current.md).

Each binary bundle carries a `buildInfo.json` stamp in its resources
(git commit / build OS / compiler / build type / OpenUSD version / schema
contract version); `tools/vrm_report.py` surfaces it as the report's `build`
section. Packaging is digest-reproducible for an unchanged build.

## SHA-256 checksums

```text
{checksums}
```
