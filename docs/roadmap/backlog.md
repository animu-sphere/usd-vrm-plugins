# Backlog

Unstarted candidates owned by `usd-vrm-plugins`. Targets and priorities are
assigned only by the [index](README.md#status-at-a-glance); active release
work is in [current.md](current.md). P1 packaging candidates precede P2 feature
candidates. Every row below is **backlog** until its trigger is met.

## Package portability

Owner for every task: **usd-vrm-plugins**. Upstream dependencies are listed
separately below; this table commits no upstream work.

| ID | Candidate | Trigger / next action | Done when / evidence destination |
| --- | --- | --- | --- |
| B01 | Remove build-machine RPATHs | Choose local CMake INSTALL_RPATH or an upstream packaging fix; inspect Linux/macOS products. | Only relocatable loader paths remain, with clean-host artifact smoke and no absolute build/runtime paths; dated report and `artifact_only_exec_smoke.py` assertions. |
| B02 | Linux/macOS symbol baselines | Run `tools/baseline_freeze.py --update` on those hosts. | Both platform baselines are committed and `--check` compares them in CI; `tests/baseline/symbols/`. |
| B03 | Independently extracted bundle consumption | Once upstream registration closure is available, exercise the extracted archives without the hand-composed closure. | Importer resolves schemas and runtime assets and exported CMake prefixes configure outside the checkout; artifact-only report and package contract. |
| B04 | Real-runtime and min-version compatibility lanes | Obtain compatible published runtime/plugin artifacts and a runner; coordinate the exact pin across consumers. | Explicit scheduled/min-version cells execute and report instead of staying orphaned; CI contract and dated report. |
| B05 | Checkable review invariants | Audit [scope policy §11](../design/INTEGRATION_SCOPE_POLICY.md#11-pr-review-checklist). | Each invariant names an existing check, a justified new guard, or an intentional review-only rule; contributing documentation. |
| B06 | Build/test/install example audit | Compare guide commands with generated CI and the release workflow. | Every retained example is exercised or cites measured provenance; guides and dated report. |

### External dependencies (not local tasks)

| Dependency | External owner / evidence | Unblock condition for local work |
| --- | --- | --- |
| Standalone bundle dependency USD registration | open-strata; [report 25](../reports/ost/25-2026-07-18-v0.18.0-from-package-workspace-correction.md), [package limitation](../architecture/PACKAGE_CONTRACT.md#41-plugin-bundles) | Installed dependency closure contains `plugInfo.json` and `generatedSchema.usda`; B03 can remove the composed workaround. |
| Workspace test attribution and workspace verbs | open-strata; [report 38](../reports/ost/38-2026-08-30-v0.22.8-workspace-cell-verbs-and-orphaned-lanes.md) | Supported workspace test/package verbs and machine-readable member attribution; then simplify local bundle cells. `workspace_ctest_labels` remains the guard. |
| Package libraries depend on the last build-loop member | open-strata; [report 40](../reports/ost/40-2026-09-13-v0.22.10-one-workspace-prefix-for-every-bundle.md) | Per-package library closure is staged independently; keep `check_product_libraries.py` meanwhile. |
| Hand-authored release workflow | open-strata release contract, not yet scoped; [pin check](../../scripts/check_docs.py) | The contract expresses all current release gates before replacing the workflow. |

No upstream issue URL was recorded for these requests in the audited local
documents. The reports preserve the actual asks; do not invent issue numbers
or treat an upstream request as an active local milestone.

## Importer and corpus

Owner for every task: **usd-vrm-plugins** (P2 candidates).

| ID | Candidate | Trigger / next action | Done when / evidence destination |
| --- | --- | --- | --- |
| B07 | Major-field fidelity classifications | Audit fields outside the already documented material conversion. | Lossless/normalized/derived/approximate classes and normalization rationale live beside the canonical contract; schema/reference tests cover affected fields. |
| B08 | Corpus and known-good VRMA clips | Select a missing VRM 0.x, VRoid, KTX2, multi-skin or motion axis and verify per-asset redistribution terms. | Licensed fixture plus expected semantic output runs in CI; corpus manifests and regression suites. |
| B09 | glTF morph-weight animation import | Use the documented `.vrm` animation gap as a minimal case. | glTF morph keys author compatible USD weights and agree with the existing VRMA bake conventions; importer integration suite. |

The private canonical-model debug format and validator are
[research candidates](../ideas/README.md#canonical-model-tooling), pending a
concrete diagnostic consumer. Creating `vrmCore` remains conditional on a
second consumer ([WORKSPACE.md](../architecture/WORKSPACE.md#1-bundles-and-libraries)).

## Spring bones

Owner for every task: **usd-vrm-plugins** (P2 candidates). The implemented
solver's constraints are in [vrmRig](../../libs/vrmRig/README.md#spring-bones).

| ID | Candidate | Trigger / next action | Done when / evidence destination |
| --- | --- | --- | --- |
| SB-O1 | Extended colliders | A licensed corpus avatar requires `VRMC_springBone_extended_collider`. | Schema/importer support and positive/negative fixtures describe inside sphere/capsule and plane semantics; schema contract and corpus suite. |
| SB-O2 | Non-unit scale and stage units | Agree the conversion boundary with the runtime consumer before extending accepted inputs. | Hit radii, colliders and virtual tails have explicit units/scale rules with reference fixtures; motion policy, solver tests and runtime evidence link. |

Resident rendering and Hydra/direct parity are
[runtime-owned](https://github.com/animu-sphere/usd-avatar-runtime/tree/main/docs/roadmap).
World colliders (SB-O3) remain [research](../ideas/README.md#spring-and-world-integration).

## VRM motion open questions

Owner for every task: **usd-vrm-plugins** (P2 candidates).

| ID | Candidate | Trigger / next action | Done when / evidence destination |
| --- | --- | --- | --- |
| M01 | VRMA animation schema placement | A consumer needs typed `VrmAnimationExpressionAPI` or `VrmAnimationLookAtAPI`. | Decide `vrmSchema` versus a separate bundle, with migration and discovery proof; schema contract. Existing namespaced attributes remain valid meanwhile. |
| M02 | Live binding/assembly vocabulary | Coordinate the VRM half with usd-motion-plugins' `Bindings` prim (USD-O5). | One owner authors `vrm:retarget:*`, separate from importer and bake output, and an external consumer evaluates it; motion policy and parity suite. |
| M03 | Expression channel names and sampling | A MotionChannelSet consumer needs the VRMA bridge. | Namespace and keyed-only versus held sampling are explicit and tested against the bake; motion policy and VRMA integration fixtures. |

Standard expression/look-at computation scope is owned once in
[vrm-computations.md](vrm-computations.md). Optional FK/switching/inverse work
is [research](execir-track.md), not a prerequisite for those computations.

## Scope

Permanent exclusions are defined only by the
[scope policy](../design/INTEGRATION_SCOPE_POLICY.md#2-what-this-repository-does-not-own).
Uncommitted native export profiles, VRMA export and VRM round-trip are in
[ideas](../ideas/README.md), outside release backlog.

<a id="non-goals"></a>
The former non-goals anchor is retained as a link to that policy.
