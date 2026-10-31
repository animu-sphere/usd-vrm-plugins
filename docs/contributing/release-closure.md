# Release closure checklist

The product cells of [release.yml](../../.github/workflows/release.yml) run
this checklist on Windows, Linux and macOS arm64, on both tag builds and
manual dry runs. Each writes `release-closure.json`, uploaded with its
installed-consumer reports as `closure-<cell>`, including on failure.
The checker is [check_release_closure.py](../../scripts/check_release_closure.py).

| Condition | Evidence required in each product cell |
| --- | --- |
| Workspace graph | `ost plugin test --workspace --graph-only` succeeds. |
| Standalone bundles | Every plugin descriptor is built with `ost plugin build`, which composes its installed dependencies. The build list is read from the descriptors and checked against `openstrata.toml`. |
| Workspace behavior | The whole-workspace verification pyramid succeeds. |
| Aggregate package | Workspace product packaging succeeds; exact release member identities and kinds agree with the descriptors, and the product count agrees. |
| Reproducibility | A second packaging run yields the same archive digests. |
| Shared libraries | `check_product_libraries.py` finds each recorded shared library in the package. |
| Clean installation | The composed clean-install smoke opens and validates real avatars and resolves their embedded textures outside the checkout. |
| Installed execution | The exec, imaging and export smokes succeed against the installed product, including their missing-registration/tool refusal cases. |
| Packaged VRMA | The VRMA reader's `--from-package` pyramid succeeds. |
| Installed CMake consumers | `run_package_consumer_lane.py` reads every `find_package` row from the package contract, installs it with CMake, then configures, builds and runs its external consumer. |
| Installed project data | Not applicable while the manifest declares no `workspace.install_data`. Adding a mapping fails closure until an installed-data probe is wired into this checklist. |

The JSON records the commit, workflow run, cell, manifest-derived product
members and each gate's outcome and conclusion. Missing, skipped, cancelled
or failed gates fail closure. A failure hidden by `continue-on-error` also
fails. The checklist runs with `always()` before release staging: it cannot
make an earlier failure pass, and a failed checklist prevents staging.

The standalone build list keeps `vrmSchema` first and `usdVrmaFileFormat`
last. The last bundle must leave the shared `vrmContainer` binary in the
workspace prefix used by packaging; the library check still verifies the
bytes rather than trusting that order
([report 40](../reports/ost/40-2026-09-13-v0.22.10-one-workspace-prefix-for-every-bundle.md)).

## What a passing checklist establishes

One report answers for one product cell. Release closure requires all three
product cells to pass at the release commit. The separate lookdev jobs,
registry publication, final release assembly and the decision to publish a
draft remain the workflow's own gates. A report is evidence to inspect, not
an independent attestation of the workflow that produced it.

The installed CMake consumer check measures a `cmake --install` prefix,
not the aggregate product's CMake exports. The product smokes measure
runtime behavior; they do not close the standalone dependency-registration
P0 or the extracted bundle CMake-prefix question in
[PACKAGE_CONTRACT.md](../architecture/PACKAGE_CONTRACT.md#41-plugin-bundles).
Consumed motion profiles are owned and tested by `usd-motion-plugins`.
The RPATH and other accepted packaging carry-overs in
[package portability backlog](../roadmap/backlog.md#package-portability) remain open.

## Local verification

The checker and its failure cases need only Python 3.11 or later:

```sh
python scripts/check_release_closure.py --bundle-paths
python tests/test_release_closure.py
```

The graph and the installed-consumer lane can also run locally with the
materialized runtime and consumed libraries. The release checklist itself
needs the live job's GitHub steps context (`RELEASE_CLOSURE_STEPS`) and
`ost`'s package JSON; a fabricated success context is not release evidence.
