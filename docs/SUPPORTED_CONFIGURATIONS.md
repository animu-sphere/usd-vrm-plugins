# Supported configurations

The configurations `usd-vrm-plugins` targets and continuously verifies. Anything
outside this list may work but is not part of the `v0.1.0` support contract.

## OpenUSD

| | |
| --- | --- |
| Tolerated range | `>=25.05, <27.0` (declared in `plugins/usdVrmFileFormat/openstrata.plugin.yaml`) |
| Authored against | 25.05 |
| Verified against | 26.08 (the certified point in the `cy2026` runtime) |

The importer builds against a single OpenUSD version per runtime; the runtime
supplies the certified point within the tolerated range. **No ABI stability is
guaranteed across OpenUSD versions** — rebuild the plugin against your target
OpenUSD. A second OpenUSD version cell (min vs latest) is a roadmap P1 item; CI
currently exercises `cy2026` (26.08) only.

## Toolchain

| | |
| --- | --- |
| CMake | ≥ 3.22 |
| C++ standard | C++17 (`CMAKE_CXX_STANDARD 17`, no compiler extensions) |
| Build type | Release (default; OpenUSD installs we build against are Release-only) |
| Python | 3.13 — required for the tools (validator, report, packaging, fixture/schema generation) and tests |

`usdGenSchema` (a Python tool from OpenUSD) is needed only to **regenerate** the
typed schema sources; the generated C++ and `generatedSchema.usda` are committed
as the plain-CMake fallback, so a normal build does not run it.

## Platforms & architectures (CI-verified)

These match the per-PR CI matrix in `.github/workflows/ost-source-ci.yml`
(each cell: build → `ost plugin test --up-to 5` → package):

| OS | Runner | Arch | ABI |
| --- | --- | --- | --- |
| Windows | `windows-2022` | x86_64 | MSVC toolset 143 |
| macOS | `macos-15` | arm64 (Apple silicon) | libc++ |
| Linux | `ubuntu-24.04` | x86_64 | libstdc++ (glibc ≥ 2.38 floor) |

Other host OS versions / architectures (e.g. Linux arm64, x86_64 macOS) are not
part of the verified matrix for `v0.1.0`.

## Build outputs

The plugin is a **shared** library (`libUsdVrmFileFormat.{dll,so,dylib}`) — USD
file-format plugins are loaded dynamically. There is no supported static-plugin
build. Discovery follows OpenUSD's standard mechanism: add the plugin's
`plugin/resources/usdVrmFileFormat` directory to `PXR_PLUGINPATH_NAME` and the `lib/`
directory to the dynamic-loader path.

## Versioning relationship

- The **package version** is the single value in the repo-root
  [`VERSION`](../VERSION) file. CMake reads it; the git tag (`vX.Y.Z`), the
  [`CHANGELOG`](../CHANGELOG.md), and the `ost` bundle manifest mirror it.
- The **schema contract version** is independent (currently `1`) and changes only
  under the policy in
  [`../plugins/vrmSchema/docs/SCHEMA_CONTRACT.md`](../plugins/vrmSchema/docs/SCHEMA_CONTRACT.md).

## Non-goals for v0.1.0

See the [`CHANGELOG`](../CHANGELOG.md#non-goals-for-v010) and the
[roadmap non-goals](ROADMAP.md#non-goals-policy-15-19).
