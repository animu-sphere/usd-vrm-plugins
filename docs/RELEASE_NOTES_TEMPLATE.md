# usd-vrm-plugins {tag}

`usdVrm` — an OpenUSD `SdfFileFormat` plugin that imports `.vrm` (VRM 0.x / 1.0)
avatars as typed USD stages, plus the validation / reporting tooling around it.

- **Schema contract version:** {schema_contract}
  ([SCHEMA_CONTRACT.md](https://github.com/animu-sphere/usd-vrm-plugins/blob/{tag}/plugins/vrmSchema/docs/SCHEMA_CONTRACT.md))
- **Supported configurations:** [SUPPORTED_CONFIGURATIONS.md](https://github.com/animu-sphere/usd-vrm-plugins/blob/{tag}/docs/SUPPORTED_CONFIGURATIONS.md)
- **Capability matrix:** [CAPABILITY_MATRIX.md](https://github.com/animu-sphere/usd-vrm-plugins/blob/{tag}/docs/CAPABILITY_MATRIX.md)
- **Install guide:** [INSTALL.md](https://github.com/animu-sphere/usd-vrm-plugins/blob/{tag}/docs/INSTALL.md)

{changelog}

## Artifacts

| artifact | contents |
| --- | --- |
| `usdVrmFileFormat-{version}-<target>.tar.zst` | relocated plugin bundle for `<target>` (lean, no debug symbols) — see the install guide |
| `usdVrmFileFormat-{version}-<target>-debug.tar.zst` | split debug symbols matching the lean bundle |
| `usd-vrm-plugins-{version}-src.tar.gz` | source archive at this tag |
| `SHA256SUMS` | SHA-256 checksums of every file above |

Each binary bundle carries a `buildInfo.json` stamp in its resources
(git commit / build OS / compiler / build type / OpenUSD version / schema
contract version); `tools/vrm_report.py` surfaces it as the report's `build`
section. Packaging is digest-reproducible for an unchanged build.

## SHA-256 checksums

```text
{checksums}
```
