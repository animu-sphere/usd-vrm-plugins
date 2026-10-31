# Building and testing

How to build this workspace, run its tests, and what CI and the release lane
run. Installing a released product is [INSTALL.md](INSTALL.md); supported
platforms and OpenUSD versions are
[SUPPORTED_CONFIGURATIONS.md](../reference/SUPPORTED_CONFIGURATIONS.md).

## Whole workspace, with OpenStrata (`ost`)

Requires `ost` 0.19+, so `requires.bundles` and `requires.libraries` are
composed automatically.

```sh
# One-time: adopt an OpenUSD install as the cy2026 runtime.
ost runtime pull cy2026 --profile usd --from-usd /path/to/openusd-install

# Validate the bundle graph, then test every bundle in dependency order.
ost plugin test --workspace
```

## A single bundle

```sh
ost plugin build plugins/usdVrmFileFormat
ost plugin test  plugins/usdVrmFileFormat            # L0-L5 verification pyramid

ost plugin build plugins/usdVrmaFileFormat
ost plugin test  plugins/usdVrmaFileFormat           # L0-L5 + VRMA golden

# Inspect a real avatar. build/test/run/package compose the manifest's
# requires.bundles closure automatically:
ost plugin run plugins/usdVrmFileFormat \
    -- python plugins/usdVrmFileFormat/tools/inspect_vrm.py avatar.vrm

# `view` / `test-view` are the exception: they load only what --with names, so
# the runtime siblings must be spelled out or the schema apply fails.
ost plugin view plugins/usdVrmFileFormat avatar.vrm \
    --with plugins/vrmSchema --with plugins/usdVrmPackageResolver
```

## With plain CMake (no OpenStrata)

Two installed prefixes, and nothing else: an OpenUSD 26.08 install, and a
`cmake --install` of [`usd-motion-plugins`](https://github.com/animu-sphere/usd-motion-plugins)
— which is consumed as a package, never as a sibling source tree.

```sh
# usd-motion-plugins, once
cmake -S usd-motion-plugins -B build-motion -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH=<openusd-prefix> -DCMAKE_INSTALL_PREFIX=<motion-prefix>
cmake --build build-motion --config Release
cmake --install build-motion --config Release

# this workspace
cmake -S . -B build "-DCMAKE_PREFIX_PATH=<openusd-prefix>;<motion-prefix>" \
      -DPython3_EXECUTABLE=<the python OpenUSD's bindings are built for>
cmake --build build --config Release
ctest --test-dir build -C Release
```

Two things `ost` supplies that a plain build states itself:

- **The Python OpenUSD was built against, where it was built against it.**
  An OpenUSD install's CMake package names that Python's headers by absolute
  path, so pass its interpreter as `Python3_EXECUTABLE`, and have its headers
  and `libpython` where the install expects them. For the pinned Linux
  runtime, that is deadsnakes' `python3.13-dev` on Ubuntu 24.04.
  [Plain CMake](../reference/SUPPORTED_CONFIGURATIONS.md#plain-cmake)
  explains why no `Python3_*` hint can point it elsewhere.
- **`-DUSDVRM_EXEC_MOTION_ROOT=<dir>`**, an extracted
  [`execMotion`](https://github.com/animu-sphere/usd-motion-plugins/tree/main/plugins/execMotion)
  bundle, for the suites that compose it (the `execVrm` retarget cases and
  the OpenExec parity rows). Without it they are not registered, and
  `workspace_ctest_labels` says which labels that leaves empty.

[`plain-cmake.yml`](../../.github/workflows/plain-cmake.yml) runs exactly this on
Linux, taking the usd-motion-plugins commit its pinned packages were built from
([`scripts/plain_cmake_inputs.py`](../../scripts/plain_cmake_inputs.py)).

The motion layer's suites carry CTest labels, so one layer runs on its own:
`motion.retarget`, `motion.cli`, `motion.integration`, `motion.openexec` and
`motion.real-corpus` (`ctest --test-dir build -C Release -L motion.openexec`).
[`scripts/check_ctest_labels.py`](../../scripts/check_ctest_labels.py)
(`workspace_ctest_labels`) holds the label set and fails when a label, or a
member suite, registers nothing.

Each member also builds standalone against *installed* packages
(`find_package(vrmSchema CONFIG REQUIRED)`), resolving only what it links: the
root resolves no dependency on a member's behalf, so `usdVrmFileFormat` configures
with no motion package on the prefix path at all. A member never reaches
sideways into a sibling's source tree, and
[`scripts/check_cmake_boundaries.py`](../../scripts/check_cmake_boundaries.py) holds
every member's edges to [WORKSPACE.md §2](../architecture/WORKSPACE.md).

The built `libUsdVrmFileFormat.{dll,so,dylib}` lands in
`plugins/usdVrmFileFormat/lib/`; add
`plugins/usdVrmFileFormat/plugin/resources/usdVrmFileFormat` to
`PXR_PLUGINPATH_NAME` and the `lib/` dir to your dynamic-loader path to use it.

## Clean-install smoke

Verifies the *packaged* bundles have no build-tree dependency:

```sh
python scripts/clean_install_smoke.py               # build + package + extract + smoke
python scripts/clean_install_smoke.py --skip-build   # reuse the current build
```

It packages the three VRM bundles with `ost`, extracts them into a fresh directory
**outside** the repo, and runs the assertions in
`plugins/usdVrmFileFormat/tests/clean_install_smoke.py` against that extracted
tree: `.vrm` discovery served from the package, a textured fixture and a corpus
avatar open and validate, and an embedded texture resolves straight from the
`.vrm` container. Needs `ost` + a validated `cy2026` runtime.

## CI

The PR entry point is [ci-routing.yml](../../.github/workflows/ci-routing.yml).
Every PR runs change classification, documentation/link/manifest consistency,
release-note input validation, and the fixed **`ci-required`** aggregate check.
There is no workflow-level PR path filter, so docs-only changes still report a
required result. Classification uses the event's base/head SHA and their merge
base, with a complete checkout and NUL-delimited git diff; it does not depend
on the GitHub files API's pagination limits. Renames include both paths.

Only root `README.md`, `CHANGELOG.md`, and `docs/**/*.md` select docs-only CI.
Two Markdown files remain executable validation/release inputs and select full
CI: `docs/architecture/PACKAGE_CONTRACT.md` and
`docs/contributing/RELEASE_NOTES_TEMPLATE.md`. Non-Markdown docs files, tests,
fixtures, manifests, CMake, CI definitions, dependencies, `VERSION`, unknown
paths, and empty diffs select full CI. A classification error fails the gate.
Add an exclusion in [changed_paths.py](../../scripts/ci/changed_paths.py) whenever
a build or test begins reading another Markdown file as input.

Full CI calls the generated OST workflow, plain-CMake workflow, and installed
package-consumer workflow. It also validates the CI manifest and checks generated
workflow drift. Code changes retain every OS and packaging claim; no tests or
matrix cells were removed. `ci-required` requires every selected workflow to
succeed, and accepts heavy-workflow skips only on a valid docs-only route.

The support matrix in `openstrata.ci.yaml` owns the OST jobs. OST 0.23.13 cannot
generate `workflow_call`, so [generate.py](../../scripts/ci/generate.py) adapts
only its trigger and job event guards into a reusable workflow. Build, runtime
verification, test and package steps are preserved verbatim. Unexpected generator
shapes or the wrong OST version fail generation. Regenerate using the pinned OST:

```sh
python scripts/ci/generate.py
python scripts/ci/generate.py --check
```

The adapter invokes `ost ci generate github --stdout`; do not overwrite the
adapted workflow with raw OST output or edit its jobs by hand. The same version
check applies locally and in the CI drift gate. Native OST reusable-workflow
support can replace the adapter when available.

The OST lane ([ost-source-ci.yml](../../.github/workflows/ost-source-ci.yml))
runs **seven cells** against digest-pinned cy2026 runtimes on hosted Windows /
macOS arm64 / Linux:

- **One graph cell** (`verify: graph`), which runs
  `ost plugin test --workspace --graph-only` — the [WORKSPACE.md §2](../architecture/WORKSPACE.md)
  dependency-direction gate — before anything is built, in milliseconds.
- **Three workspace cells** (`kind: workspace`), one per OS, which build the
  root CMake tree and run its CTest suite. This is the behavioral lane: the root
  tree is the only configuration in which the plain libraries and the CLI tools
  exist, and its suite also contains every bundle's own tests, so it is the
  coverage `vrmRig`, `vrmContainer`, `motion_retarget`, all four plugin bundles and the
  whole-workspace `usdvrm_baseline` gate get.
- **Three bundle cells** — `usdVrmFileFormat` on each OS — which build that
  bundle *standalone* (`ost plugin build`, no root tree in scope), run its
  pyramid (`--up-to 5`; Windows is capped at 4), and `ost plugin package` it.
  Neither the standalone configure nor packaging is reachable from a workspace
  cell, and they are per-platform, which is what these three are for.

Use **Actions → ci → Run workflow** (`workflow_dispatch`) to force all lanes,
including docs and drift validation. The individual source, plain-CMake and
package-consumer workflows also retain manual dispatch for focused diagnosis.
Release tag/manual validation stays in `release.yml`.

Branch protection should require **`ci-required`**, rather than the conditional
matrix cell names or reusable-workflow job names. Before enabling routing in a
repository with protected branches, replace any old required cell checks with
this fixed check. At the 2026-10-31 audit this repository's `main` had no branch
protection or effective rules; no remote settings were changed. See the
[baseline and claim audit](../reports/ci/2026-10-31-ci-audit.md) for measurements
and the remaining cache/coverage experiments.

## Release artifacts

Pushing a tag `vX.Y.Z` (matching [`VERSION`](../../VERSION), with that version's
`CHANGELOG.md` section finalized) runs `.github/workflows/release.yml`: it builds
on all three OS cells, proves the *packaged* artifact (packaged-artifact
verification, clean-install smoke, digest-reproducible packaging), and assembles
a **draft** GitHub release — per-target lean + debug bundles, a source archive,
`SHA256SUMS`, and notes rendered from `CHANGELOG.md` via
[docs/contributing/RELEASE_NOTES_TEMPLATE.md](../contributing/RELEASE_NOTES_TEMPLATE.md).
Publishing the draft is a human decision. Run the workflow manually
(`workflow_dispatch`) for a dry run that creates no release.

Each product cell also runs the installed CMake consumers and writes the
[release closure checklist](../contributing/release-closure.md), comparing
manifest membership with the packages and requiring every closure gate to
have actually succeeded. Its JSON and consumer reports are uploaded as
`closure-<cell>` even when a check fails.

`usdVrmFileFormat` carries a `buildInfo.json` stamp (commit / toolchain /
OpenUSD release and `PXR_VERSION` / OpenExec components / build type / schema
contract version), surfaced by `tools/vrm_report.py`.

Every bundle is built against **OpenUSD 26.08 and nothing else**, and against a
26.08 that carries OpenExec. Both are enforced at configure time by
[`cmake/UsdVrmOpenUsd.cmake`](../../cmake/UsdVrmOpenUsd.cmake), for `ost` and
plain-CMake builds alike — see
[supported configurations](../reference/SUPPORTED_CONFIGURATIONS.md).
