# usd-vrm-plugins {tag}

`usdVrm` — an OpenUSD plugin workspace for VRM avatars. `usdVrmFileFormat`
imports `.vrm` (VRM 0.x / 1.0) as typed USD stages; `vrmSchema` provides the
typed schemas it applies; `usdVrmPackageResolver` serves embedded textures — plus
the validation / reporting tooling around them. `usdVrmaFileFormat` imports
VRMA motion clips as canonical `UsdSkelAnimation` data.

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
| `usdVrmaFileFormat-{version}-<target>.tar.zst` | `.vrma` animation importer bundle for `<target>` (lean) |
| `usd-vrm-plugins-{version}-<target>-plugin-product.tar.zst` | aggregate product archive containing the exact four member bundles |
| `<bundle>-{version}-<target>.manifest.json` | OpenStrata manifest sidecar per bundle |
| `usd-vrm-plugins-{version}-src.tar.gz` | source archive at this tag |
| `SHA256SUMS` | SHA-256 checksums of every file above |

> **Install all three VRM bundles together.** They are separate artifacts but one
> product: `usdVrmFileFormat` alone registers the `.vrm` file format and then
> **fails to open a stage**, because the typed `Vrm*API` schemas it applies live
> in `vrmSchema` and are not staged into its package (OpenStrata stages a
> dependency bundle's link-time half without its USD registration half).
> Embedded-texture resolution likewise needs `usdVrmPackageResolver`. Put all
> three on the plugin path — see the
> [install guide](https://github.com/animu-sphere/usd-vrm-plugins/blob/{tag}/docs/guides/INSTALL.md).
>
> `usdVrmaFileFormat` is independently installable: it has no plugin-bundle
> dependency and is independently package-tested. The aggregate product artifact
> contains the exact four member bundles,
> manifests, checksums, and evidence in dependency order. It is produced by
> `ost plugin package --workspace --product` and is the recommended install
> starting point.

Each binary bundle carries a `buildInfo.json` stamp in its resources
(git commit / build OS / compiler / build type / OpenUSD version / schema
contract version); `tools/vrm_report.py` surfaces it as the report's `build`
section. Packaging is digest-reproducible for an unchanged build.

## SHA-256 checksums

```text
{checksums}
```
