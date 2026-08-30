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
| `usd-vrm-plugins-{version}-<target>-plugin-product.tar.zst` | aggregate product archive containing the exact seven members: the four bundles above and three CLI tools |
| `motion_retarget` · `motion_capture` · `motion_bvh` | tool members of the product archive — the retarget, capture-replay and BVH CLIs |
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
> contains the exact seven members, manifests, checksums, and evidence in
> dependency order. It is produced by `ost plugin package --workspace --product`
> and is the recommended install starting point.
>
> **The live-capture adapters and their `*_record` CLIs are not in any
> artifact.** That is deliberate —
> [WORKSPACE.md](https://github.com/animu-sphere/usd-vrm-plugins/blob/{tag}/docs/architecture/WORKSPACE.md)
> §5 keeps optional network and SDK dependencies out of the core distribution —
> and they build from the source archive.
>
> **The product carries the motion profiles.** `motion_bvh_convert` needs a
> producer profile, and `ost plugin product install --prefix <dir>` puts the
> shipped ones at `<prefix>/share/usd-vrm-plugins/profiles/motion/` where the
> tool finds them with no flag — verified per release by
> `scripts/artifact_only_bvh_smoke.py`. No *member* archive carries them: a
> `motion_bvh` archive unpacked on its own is its two executables, so that route
> still needs a `cmake --install` prefix or a `--profile` path.

Each binary bundle carries a `buildInfo.json` stamp in its resources
(git commit / build OS / compiler / build type / OpenUSD version / schema
contract version); `tools/vrm_report.py` surfaces it as the report's `build`
section. Packaging is digest-reproducible for an unchanged build.

## SHA-256 checksums

```text
{checksums}
```
