---
status: historical
owner: usd-vrm-plugins
---

> **Historical only.** Former Product P0–P6 checklists at `d1dd81c`, archived
> 2026-10-31. Current tasks belong to the [roadmap](../../roadmap/); binding
> test and release criteria remain in [DESIGN_POLICY.md](../../design/DESIGN_POLICY.md#13-test-strategy).

## 17. Roadmap

> Live status against this roadmap is tracked in [the roadmap](../../roadmap).

### P0: Documentation & implementation sync

**Work**

- Unify phase notation in the README
- Update the front-direction description to the current implementation
- Explain the relationship between the package resolver and temp extraction
- Document schema contract v1
- Add a capability-status table
- State the difference between import / evaluation / simulation

**Done when**

- No clear contradiction between README and code
- A new user can understand the output structure and constraints
- The support status of each major VRM feature is listed

### P1: v0.1.0 release

**Work**

- Versioning
- Changelog
- Release workflow
- Platform artifacts
- Compatibility matrix
- Known limitations
- Install guide

**Done when**

- Installable from a clean environment
- Can open a sample VRM
- The schema plugin is discovered
- The validator passes
- Release artifacts are reproducible

### P2: Fix the canonical-model contract

**Work**

- Per-field fidelity classification
- Canonical-model documentation
- Stable serialization or debug dump
- Source preservation with an exporter in mind
- Explicit normalized fields
- Canonical validator

**Done when**

- Importer and authorer depend only on the canonical contract
- Parser-specific types and USD types do not leak into the canonical layer
- Lossless / normalized / derived / approximate is defined for the major fields

### P3: Windows CI & runtime verification

**Work**

- Windows hosted runner
- MSVC build
- Plugin discovery
- UTF-8 / Unicode paths
- DLL dependencies
- Package resolver
- Real VRM smoke test

**Done when**

- Windows build and open are continuously verified
- Real models with textures resolve
- Schema registration succeeds

### P4: The motion & runtime layer

> **Restructured 2026-07-18, and closed as a sequence on 2026-09-25.** P4 was
> an umbrella over the retired Motion Phase A–H; generic motion has since moved
> to `usd-motion-plugins`, and what remains of P4 here is VRM-specific —
> [VRM_MOTION_POLICY.md](../../design/VRM_MOTION_POLICY.md) and the
> [`ExecIr` track](../../roadmap/execir-track.md). The work list below is the
> pre-motion-policy plan, kept for rationale; it is not the plan of record.
> In particular the LookAt-first ordering is retired (the retarget core came
> first) and "Mocopi adapter prototype" is not a work item here at all — live
> input is `motion-connectors`'.

**Work** *(superseded)*

- `execVrm` module
- Schema-discovery node
- Humanoid-retarget node
- Expression evaluator
- Look-at evaluator
- Spring-bone solver
- Constraint evaluator
- Graph ordering
- Mocopi adapter prototype

**Done when** *(restated by motion policy §17)*

- A runtime graph can be built from a static USD stage
- The humanoid can be driven from a clip, a live capture, **or a generator** —
  through one shared pipeline, with the generator swappable without downstream
  change
- Look-at, expression, and spring bone can each be evaluated as independent nodes
- The runtime can be swapped without changing the importer

### P5: MToon realization

> **Restructured 2026-08-13, re-ordered 2026-09-25.** P5's detail lives in
> [MATERIAL_ARCHITECTURE_POLICY.md](../../design/MATERIAL_ARCHITECTURE_POLICY.md) §7, and
> its open steps in the [material track](../../roadmap/material-track.md). The
> work list below is the pre-material-policy plan, kept for rationale; it is
> no longer the plan of record. The PreviewSurface restructure and the unlit
> MaterialX realization shipped first; since 2026-09-25 the VRM material API
> schemas come **next**, ahead of any further realization work, and the full
> MToon renderer is `hydra-toon`'s, another repository. The steps are the
> internal order of P5 and **not** another phase sequence.

**Work** *(superseded — see material policy §7)*

- `VrmMToonAPI`
- Source-semantics contract
- MaterialX approximation
- Renderer adapter
- Outline
- Conformance images
- Transparent-sorting behavior

**Done when** *(restated by material policy §7.1–§7.3)*

- Source parameters are not lost
- A portable fallback exists
- At least one renderer reproduces the main MToon look
- An image regression test exists

### P6: Round-trip / exporter research

> Exporting **to** `.vrm`. Writing an imported `.vrm` as native USD —
> `.usda`, `.usdc`, `.usdz` — is the other direction and a different tool,
> `vrm_export` ([export policy](../../design/VRM_EXPORT_POLICY.md) §3.3).

**Work**

- Reverse mapping from USD to the canonical model
- Source-fidelity report
- VRM export feasibility
- Detection of unsupported USD edits
- Loss report
- Exporter prototype

**Done when**

- Round-trippable fields are explicit
- Loss on export can be reported to the user
- A limited VRM 1.0 export path can be verified

---

## 20. Recommended near-term actions

Recommended units of work.

### Sprint 1

- Fix README contradictions
- Compatibility table
- Schema-contract document
- Front-direction document
- Texture-resolver document

### Sprint 2

- v0.1.0 versioning
- Changelog
- Release workflow
- Install test
- Initial Windows CI lane

### Sprint 3

- Canonical fidelity classification
- JSON diagnostic report
- Standalone validator CLI
- Golden semantic diff

### Sprint 4

> **Superseded** by the Motion Phase ladder
> ([MOTION_ARCHITECTURE_POLICY.md](../../design/MOTION_ARCHITECTURE_POLICY.md) §16). The
> sequence below starts at `execVrm`; the motion policy starts at the contract
> freeze and the retarget core, and reaches OpenExec only at Motion Phase E.

- `execVrm` skeleton
- Schema discovery
- Humanoid mapping
- Mocopi input adapter
- Expression / look-at prototype

---
