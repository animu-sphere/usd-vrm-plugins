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
| `usdVrmFileFormat-{version}-<target>.tar.zst` | relocated `usdVrmFileFormat` bundle for `<target>` (lean, no debug symbols) — see the install guide |
| `usdVrmFileFormat-{version}-<target>-debug.tar.zst` | split debug symbols matching the lean bundle |
| `usd-vrm-plugins-{version}-src.tar.gz` | source archive at this tag |
| `SHA256SUMS` | SHA-256 checksums of every file above |

> **Binary artifacts ship the importer bundle only.** `vrmSchema` and
> `usdVrmPackageResolver` are not yet published as release artifacts, so the
> binary bundle above cannot apply its typed schemas or resolve embedded
> textures on its own. Build from source (or from the source archive) for a
> complete workspace until the aggregate product artifact lands — see
> [WORKSPACE.md §5](https://github.com/animu-sphere/usd-vrm-plugins/blob/{tag}/docs/architecture/WORKSPACE.md)
> and the [roadmap](https://github.com/animu-sphere/usd-vrm-plugins/blob/{tag}/docs/roadmap/current.md).

Each binary bundle carries a `buildInfo.json` stamp in its resources
(git commit / build OS / compiler / build type / OpenUSD version / schema
contract version); `tools/vrm_report.py` surfaces it as the report's `build`
section. Packaging is digest-reproducible for an unchanged build.

## SHA-256 checksums

```text
{checksums}
```
