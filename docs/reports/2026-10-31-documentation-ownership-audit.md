# Documentation ownership audit — 2026-10-31

This is a local documentation audit at `d1dd81c`, not a release dry run.
The working tree was clean before the audit. Package version is still
`0.10.0`; schema contract is **2**. No tag, publication, candidate-wide CI
success or new runtime/renderer measurement is asserted here.

## Inventory and overlapping responsibilities

| Surface inspected | Finding | Disposition |
| --- | --- | --- |
| Roadmap index/current/backlog and four track documents | Shipped release checklists, completed tracks, external work and research mixed with local tasks | Replaced current/backlog; track history archived with compatibility stubs; ExecIr explicitly optional. |
| Design policies | Binding constraints mixed with Product P0–P6 work lists; material/export policies linked to completed plans as open work | Product checklists archived; binding contracts retained; owner links repaired. |
| WORKSPACE/PACKAGE_CONTRACT | Current identities, dependency/ABI/artifact contracts mixed with completed Phase/MIG history | Full old workspace contract archived; current identities, dependency diagrams, artifact names, baseline rules and moving invariants retained. |
| Capability/supported-configuration references | Spring solver completion conflated with runtime integration; expression computations tied to optional adapter; export linked as ongoing | Point to current policy/API and separately scoped computation candidates. No new capability claim. |
| Release records and CHANGELOG | Historical citations into active plans | Link repairs only to released history; Unreleased retains implementation scope and pending closure statement. |
| docs/archive/reports/contributing indices | Category ownership existed but lifecycle enforcement still required an old milestone heading | Indices and task lifecycle updated; lint reads owner/target rows and distinguishes proposed records. |

Original current/backlog and Product checklists are preserved in
[release-preparation history](../archive/release-preparation/), and the full
old [workspace contract](../archive/workspace-migration/WORKSPACE.md) remains
available. No task was silently deleted.

The original tracked Markdown inventory was mechanically counted from the
same HEAD (task markers count explicit list markers, not every prose candidate):

| Category | Files | Headings | Marked checklist items |
| --- | --- | --- | --- |
| roadmap/ | 7 | 70 | 93 |
| design/ | 8 | 240 | 0 |
| architecture/ | 2 | 27 | 0 |
| reference/ | 2 | 16 | 0 |
| releases/ | 11 | 97 | 0 |
| contributing/ | 3 | 15 | 0 |

## Task disposition

Each row's status is the audited implementation/planning state, not proof of
release publication. Local owner shorthand **VRM** means `usd-vrm-plugins`.

| ID / former task | Audited status / evidence | Owner repo | Disposition / destination / reason |
| --- | --- | --- | --- |
| Current: pre-tag/cut/re-pin v0.10.0 | done; [release record](../releases/v0.10.0.md) | VRM | archive [old current](../archive/release-preparation/current-before-cleanup.md); released facts stay in release record. |
| Release closure hosted proof | implemented checks, hosted proof pending at audited HEAD; [checklist](../contributing/release-closure.md) | VRM | keep active R02–R03; older export proof predates new gates. |
| Release scope/version/publication | unstarted for next release | VRM | active R01/R04; [preparation worksheet](../contributing/release-candidate.md), no premature numbered release record. |
| Checkable review invariants | unstarted | VRM | keep backlog B05, with review-only exceptions to document. |
| Workspace history separation | completed by this change | VRM | archive full old contract; keep binding moving rules and compatibility anchors current. |
| RPATH cleanup | open; artifact smoke counts build-machine paths | VRM / open-strata packaging dependency | keep B01; choose local/upstream fix before implementation. |
| Linux/macOS symbol baselines | unstarted; only Windows baseline committed | VRM | keep B02. |
| Real-runtime/min-version lane | blocked on artifacts/runner; [report 38](ost/38-2026-08-30-v0.22.8-workspace-cell-verbs-and-orphaned-lanes.md) | VRM / open-strata | B04 owns local lane, external publication is a dependency. |
| Workspace verbs/test attribution | upstream request, local workaround exists | open-strata | external dependency, not current work; report 38, `workspace_ctest_labels` guard retained. |
| Handwritten release workflow | accepted workaround, replacement unscoped | open-strata / VRM consumer | external dependency; pin comparison retained. |
| Last-bundle library staging | accepted workaround; [report 40](ost/40-2026-09-13-v0.22.10-one-workspace-prefix-for-every-bundle.md) | open-strata | external dependency; `check_product_libraries.py` retained. |
| Build/test/install examples | audit candidate | VRM | keep B06, with executable/provenance acceptance. |
| Standalone registration/hand-composed closure | upstream P0; [report 25](ost/25-2026-07-18-v0.18.0-from-package-workspace-correction.md) | open-strata / VRM consumer | B03 depends on upstream; do not remove current smoke workaround. |
| Material Steps 1–7 | done; [v0.10.0](../releases/v0.10.0.md), schema/importer/realization/imaging suites | VRM | archive [material plan](../archive/material/material-track.md); policy remains binding. |
| Material Step 8/image conformance | transferred | hydra-toon | owner link only; no local image test invented without expected images and CI scope. |
| Export Steps 1–3 | implemented and measured; [Step 2](2026-10-29-export-release-dry-run.md), [Step 3](2026-10-31-export-motion-release-dry-run.md) | VRM | archive [export plan](../archive/export/export-track.md); retain current policy and CLI reference. |
| Export Step 4 profiles/source retention/metadata | unscoped | VRM | research in [ideas](../ideas/README.md), pending concrete consumer/validation path. |
| Generic export helper extraction | no second workspace consumer | OpenStrata proposal / VRM research | research, not a new local identity. |
| Spring Steps 0–1 | done; [source CI](https://github.com/animu-sphere/usd-vrm-plugins/actions/runs/40624536250) and installed-consumer evidence in archived plan | VRM | archive [spring plan](../archive/springbone/springbone-track.md); solver/API constraints stay current. |
| Spring Step 2 scoped handoff | measured by runtime owner; [runtime evidence](https://github.com/animu-sphere/usd-avatar-runtime/blob/main/docs/reference/CAPABILITY_MATRIX.md#vrm-fixed-tick-session-and-host-evidence-2026-10-29) | usd-avatar-runtime | link owner, never claim resident rendering/Hydra parity here. |
| SB-O1 extended colliders / SB-O2 scale | open, consumer-dependent | VRM | keep backlog with triggers, acceptance and evidence locations. |
| SB-O3 world colliders / offline spring bake | unscoped, external contracts needed | VRM research / usd-physics-plugins / usd-avatar-runtime | ideas; no commitment to implement. |
| P2 fidelity | partly documented for materials, major-field audit remains | VRM | B07 addresses only remaining fields, keeps existing normalization contracts. |
| P2 debug dump/validator/source preservation for reverse export | no concrete extra consumer | VRM | research; shipped raw-source preservation remains binding; `vrmCore` still conditional. |
| P4 offline bake/execVrm body foundation | done; [v0.9.0](../releases/v0.9.0.md), current capability matrix | VRM | removed from open work; no new completion claim for expression/look-at computations. |
| P4 expression/look-at computations | unimplemented | VRM | [standard computation scope review](../roadmap/vrm-computations.md); independent of ExecIr. |
| ExecIr P0-1 and subsequent adapter/FK/switch/inverse/performance steps | optional, not started | VRM research / OpenUSD | [optional research track](../roadmap/execir-track.md); subsequent implementation conditional on audit. |
| Exec display/upstream asks | external blocker; existing display report | OpenUSD / VRM research | retained as research dependency, not a release gate; generic client owner remains EX-O1 upstream. |
| VRMA schema placement | undecided, plain namespaced attributes already shipped | VRM | keep M01; trigger is a typed-API consumer. |
| Morph overlap diagnostics | geometry-analysis consumer absent | VRM research | ideas, not a release task. |
| Live binding/assembly | local schema vocabulary awaits generic Bindings | VRM / usd-motion-plugins | keep local M02; generic implementation not mirrored. |
| Expression channel names/sampling | unresolved bridge contract | VRM / usd-motion-plugins | keep M03; explicit keyed-versus-held acceptance. |
| Coordinated OpenUSD pin | compatibility obligation, not standalone feature | consuming repos / open-strata artifacts | B04 dependency plus binding exact-pin rule. |
| Corpus/known-good VRMA | unstarted expansion axes; per-model licenses pending | VRM | B08; licensing and expected output required. |
| glTF morph-weight animation | documented gap | VRM | keep B09. |
| VRMA writer / P6 true VRM round-trip | feasibility only | VRM research | ideas, no target version; `vrm_export` remains native USD export. |
| Non-goals / shipped acceptance rows | policy or shipped evidence, not tasks | VRM | exclusions centralized in scope policy; binding test and release criteria stay in design policy; old planning rows archived. |
| Live device/operator evidence | transferred with input ownership | motion-connectors | external owner link only, removed from local current. |
| Vague “other advanced items” | no bounded action or acceptance | VRM | drop catch-all; concrete future items require normal intake. |

## Citation and contract audit

`rg` searched local Markdown and the available sibling `docs/` trees in
`usd-avatar-runtime`, `hydra-toon`, `usd-motion-plugins` and
`usd-physics-plugins` before moving plans. Notable external citations:

- Runtime current links the old spring track path: retained as a superseded
  stub linking policy, archived evidence and the runtime owner.
- Motion release records cite `WORKSPACE.md#92-moving-rules`: retained with
  the current binding rules. Its unanchored §9.5 citations still resolve to
  the VRM/generic boundary and link the historical header-level cut.
- Local historical §9 anchors and completed-track references were redirected
  to archived copies; active capability references use current policy/API.
- Old workspace §6/§7/§8/§9 anchors are preserved where headings changed.

The dependency diagrams and artifact-name block are unchanged. No manifest,
schema, public header, package descriptor, CMake dependency or plugin runtime
implementation is changed by this cleanup. The documentation lint's release
classification and lifecycle checks are updated with the documents.

## Validation

The original `python scripts/check_docs.py` passed before editing. After the
cleanup:

- `python scripts/check_docs.py`: passed, including all local Markdown paths
  and fragments, archive metadata, owners and package/schema version checks.
- `python tests/test_docs.py`: all 7 regression cases passed. The local Windows
  sandbox required TMP/TEMP to point inside the workspace scratch directory;
  the default sandbox temporary directory denied file creation.
- `ost plugin test --workspace --graph-only`: valid; 6 bundles, 4 bundle
  edges, 2 libraries, 13 library edges and 2 tools.
- Dependency/artifact code blocks were compared against the original contract;
  the allowed/forbidden dependency diagrams and artifact-name block are equal.
- `git diff --check`: passed.

These local checks do not replace a hosted release dry run. Runtime C++ suites
and release packaging were not rerun for this documentation/lint-only change.
