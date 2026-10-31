# usd-vrm-plugins — design & development policy

> English translation of the project's forward design/development policy. This is
> the canonical policy document referenced by [the roadmap](../roadmap/); the roadmap
> tracks live status against it. Section numbers are stable so the roadmap and the
> plugin docs can cite them (e.g. "policy §10").
>
> **Scope:** this document is canonical for the **importer** — the `.vrm` read
> path, the canonical model, the schema contract, diagnostics, CI, release, and
> Product P0–P6. How VRM and VRMA use motion — `.vrma` import, the VRM rig
> binding, expressions, look-at and `execVrm` — is
> [VRM_MOTION_POLICY.md](VRM_MOTION_POLICY.md), which extends §10 and replaces
> §17-P4's work list; where the two overlap, that policy wins. Generic motion is
> `usd-motion-plugins`' and is not defined in this repository.

## 1. Purpose

This document sets out the forward design, implementation, and release policy for
`animu-sphere/usd-vrm-plugins`, based on an assessment of where the project stands
today.

The goal of this project is **not** merely to display VRM on OpenUSD.

> Convert the semantics VRM carries — humanoid, expression, look-at, secondary
> motion, constraints, MToon — into a form that is reusable, preservable, and
> evaluable within the OpenUSD ecosystem.

From here on the focus is not just adding features. It is stabilizing the
**specification contract, documentation, validation, distribution, and runtime
evaluation layer**, so the project can graduate into an OSS that external projects
can depend on.

---

## 2. Current assessment

The repository has moved past the stage of an experimental VRM loader.

The following are the principal design assets worth preserving going forward:

- Separation of VRM/GLB reader, canonical model, and USD authorer
- Role separation across `/Asset/geo`, `/Asset/mtl`, `/Asset/skel`, `/Asset/rig`
- Normalization of multiple glTF skins into a single `UsdSkelSkeleton`
- Typed API-schema representation of VRM semantics
- Stage validator, diagnostic taxonomy, and compatibility report
- CI using both generated fixtures and a real VRM corpus
- OpenUSD runtime verification centered on Linux/macOS
- Improved texture portability via a package resolver

In one sentence, the project is at the following inflection point:

> Migrating from a plugin that displays VRM in USD, to a foundation that brings VRM
> semantics into the USD ecosystem.

---

## 3. Base architecture

### 3.1 Import pipeline

Keep the base structure:

```text
VRM / GLB
  ↓
Document Reader
  ↓
VrmCanonicalDocument
  ↓
UsdVrmAuthorer
  ↓
USD Stage
```

Each layer has a clear responsibility.

### Document Reader

Responsibilities:

- Read GLB/glTF structure
- Parse VRM 0.x / VRM 1.0 extensions
- Read buffers, accessors, images, materials, skins, animations
- Convert parser-specific data into the canonical model

Constraints:

- Do not expose USD types as an external contract
- Do not leak VRM 0.x vs 1.0 differences too far downstream
- Do not keep parser-library-specific types in the canonical model

### VrmCanonicalDocument

Responsibilities:

- A unified representation of VRM 0.x / 1.0
- Preserve the semantics of humanoid, expression, look-at, spring bone, constraint,
  MToon
- A shared contract between importer, validator, exporter, and runtime evaluator
- Classify information as lossless / normalized / derived / approximate

Constraints:

- No dependency on the glTF parser
- No dependency on OpenUSD types
- No dependency on renderer-specific representations

### UsdVrmAuthorer

Responsibilities:

- Build a USD stage from the canonical model
- Author geometry, material, skeleton, and rig semantics
- Apply the typed API schemas
- Emit stable relationships, tokens, and metadata
- Generate a diagnostic report

Constraints:

- Knows nothing of parser details
- Does not run runtime simulation
- Does not embed renderer-specific shading directly into the core importer

---

## 4. USD prim hierarchy

The standard prim structure is:

```text
/Asset
  /geo
  /mtl
  /skel
    /Skeleton
  /rig
    /Humanoid
    /Expressions
    /LookAt
    /SecondaryMotion
    /Constraints
```

### `/Asset`

- `UsdSkelRoot` when a skin is present
- `UsdGeomXform` when no skin is present
- Holds asset metadata, VRM version, and source information
- Does not retain a root transform that exists only for format-compatibility
  correction

### `/Asset/geo`

- Renderable geometry
- Mesh topology
- Points, normals, UV, color
- Skinning primvars
- Morph target / blend shape
- Material binding

### `/Asset/mtl`

> **Refined** by
> [MATERIAL_ARCHITECTURE_POLICY.md](MATERIAL_ARCHITECTURE_POLICY.md) §4
> (2026-08-13): texture and shader nodes live below per-realization
> `UsdShadeNodeGraph` children (`/preview` shipped, `/mtlx` planned), leaving
> the material prim to carry identity, binding, and canonical semantics. Where
> the two disagree about the `/Asset/mtl` structure, the material policy wins.

- Material prim — identity and binding target; nothing binds below it
- One `UsdShadeNodeGraph` child per rendering realization
- Texture and shader nodes, inside a realization
- Portable approximation
- References to the source VRM MToon semantics

### `/Asset/skel`

- `UsdSkelSkeleton`
- Animation source
- Blend-shape animation
- Structure required for skin binding

### `/Asset/rig`

Where VRM-specific semantics live.

- Does not duplicate the joint hierarchy
- Holds a relationship to the skeleton prim
- Humanoid bones are referenced by joint token or joint path
- Holds the declarative data for expression, look-at, spring bone, and constraint
- A stable arrangement that a runtime evaluator can discover easily

---

## 5. Schema policy

VRM-specific features are exposed as typed applied API schemas.

Targets:

- `VrmHumanoidAPI`
- `VrmExpressionAPI`
- `VrmLookAtAPI`
- `VrmSpringBoneAPI`
- `VrmColliderAPI`
- `VrmConstraintAPI`
- `VrmMaterialAPI`, `VrmMToonAPI` and `VrmTextureInfoAPI` (shipped 2026-09-25;
  [material policy](MATERIAL_ARCHITECTURE_POLICY.md) §6)
- A `VrmMetaAPI` as needed

### Principles

1. Downstream consumers can discover it via `HasAPI`
2. Attribute names, relationship names, and token values are managed as a schema
   contract
3. The schema contract version is explicit
4. Preservation of the raw source extension is separated from the normalized schema
5. Breaking schema changes are managed by a major contract version

### Schema contract version

At minimum, record:

```text
vrm:schemaContract = "1"
vrm:sourceVersion = "0.x" or "1.0"
vrm:importerVersion = "<plugin version>"
```

When the schema changes in future, provide a conversion policy or migration tool.

---

## 6. Data-fidelity classification

Classify each canonical-model field and its USD output as follows.

### Lossless

Preserves the source information without semantic loss.

Examples:

- Humanoid bone mapping
- Meta information
- Spring-bone parameters
- Collider definitions
- Expression binds
- Raw preservation of MToon source parameters

### Normalized

Converts VRM 0.x vs 1.0, or exporter differences, into a unified representation.

Examples:

- Humanoid bone names
- Front direction
- Expression categories
- Spring-bone group structure
- Coordinate convention

### Derived

Computed from the source.

Examples:

- Skeleton joint union
- Parent-child joint order
- Bind transforms
- Compatibility classification
- Asset summary

### Approximate

Approximated toward a portable USD/Hydra representation.

Examples:

- MToon → `UsdPreviewSurface`
- Renderer-independent toon-shading approximation
- Approximation of unsupported blend modes

### USD-only

Information needed only within the USD ecosystem.

Examples:

- Diagnostic prims
- Import report
- Package-resolver metadata
- Schema-contract metadata
- Processing provenance

This classification feeds a future exporter and round-trip fidelity judgment.

---

## 7. Coordinate system & front direction

Rather than retaining a compatibility-correction transform on the root prim, bake
coordinate conversion into the canonical data and authored data wherever possible.

### Policy

- Handle the VRM 0.x vs 1.0 front-direction difference at canonicalization time
- Apply it consistently to related data: node transforms, meshes, animations,
  spring gravity direction, etc.
- Keep `/Asset`'s root transform available for user manipulation and placement
- Aim for no hidden compatibility transform remaining after import

### Acceptance conditions

- Bind pose does not break
- Animation rotation directions match
- Spring-bone gravity direction matches the visual coordinates
- Look-at axis matches the avatar front
- VRM 0.x / 1.0 comparison fixtures produce equivalent front directions

---

## 8. Texture & asset portability

Texture resolution goes through the package resolver as its primary path.

### Priority policy

1. Image lookup inside the package
2. Resolver cache
3. Explicit extraction/export operation
4. Temp-directory extraction only as a fallback or for development

### Requirements

- Authored USD does not depend on machine-local absolute temporary paths
- Opening the same VRM from multiple stages avoids unnecessary duplicate extraction
- Image identity and cache key are stable
- A packaged USD/VRM asset resolves in a different environment
- The cleanup policy is explicit

The README must accurately describe the primary path, fallback, cache, and package
behavior.

---

## 9. MToon policy

> **Extended** by
> [MATERIAL_ARCHITECTURE_POLICY.md](MATERIAL_ARCHITECTURE_POLICY.md)
> (2026-08-13). The three layers below still hold and are the foundation that
> policy builds on; it fixes the USD hierarchy each layer occupies, the schema
> surface for Layer 1, and the rule that one realization is never generated from
> another. Where the two documents disagree, the material policy wins.

MToon is separated into three layers:

```text
VRM MToon source semantics
        ↓
portable approximation
        ↓
renderer-specific realization
```

### Layer 1: Source semantics

The canonical model and schema preserve MToon's source parameters.

Example targets:

- Base color
- Shade color
- Shading shift
- Shading toony
- Rim lighting
- Matcap
- Emission
- Outline
- UV animation
- Alpha mode
- Render queue
- Double sided
- Texture transforms

### Layer 2: Portable approximation

Use `UsdPreviewSurface`, MaterialX Standard Surface, and a portable texture graph
to provide a minimally verifiable look even on non-toon renderers.

This does not guarantee full reproduction.

### Layer 3: Renderer-specific realization

A Hydra delegate or renderer adapter implements:

- Toon ramp
- Shade threshold
- Rim behavior
- Matcap
- Outline pass
- Render queue
- Alpha clipping / transparent ordering
- Double-sided normal behavior

Do not tightly couple renderer-specific shader implementations into the core
file-format plugin.

### Tests

- Canonical-parameter test
- MaterialX graph test
- Reference-image comparison
- Per-renderer conformance images
- With/without-outline comparison
- Transparent-material sorting test

---

## 10. Runtime semantics & OpenExec

> **Extended and partly superseded** by
> [MOTION_ARCHITECTURE_POLICY.md](MOTION_ARCHITECTURE_POLICY.md) (2026-07-18).
> The import/runtime boundary below still holds and is the foundation the motion
> policy builds on. Three specifics changed:
>
> - `execVrm` is split into **`execMotion`** (vendor-neutral motion runtime) and
>   **`execVrm`** (VRM semantics + rig application) — motion policy §11.
> - An **OpenExec-independent `vrmRetarget` library is completed first**;
>   OpenExec nodes are thin wrappers over it — motion policy §10, §18.12.
> - The "Mocopi integration" flow below is generalized: Mocopi becomes one
>   optional adapter behind a generic `LiveCaptureSource`, and product names are
>   forbidden in core — motion policy §8.
>
> Where the two documents disagree, the motion policy wins.

Separate the file-format plugin from the runtime evaluator.

```text
usdVrm
  ├─ import
  ├─ schemas
  ├─ canonicalization
  ├─ validation
  └─ diagnostics

execVrm
  ├─ humanoid retarget
  ├─ expression evaluation
  ├─ look-at evaluation
  ├─ spring bone simulation
  └─ VRM constraint evaluation
```

### usdVrm responsibilities

- Read data
- Build the USD stage
- Describe semantics with schemas
- Provide compatibility and diagnostics

### execVrm responsibilities

- Discover authored schemas
- Evaluate based on animation, camera, input, and time
- Output results to skeleton, blend shapes, and transforms
- Be composable as nodes in an OpenExec graph

### Recommended evaluation order

```text
source animation
  ↓
humanoid retarget
  ↓
VRM constraints
  ↓
look-at
  ↓
expressions
  ↓
spring bone
  ↓
final skeleton / blend shape / transform output
```

Build this as a graph according to the actual dependencies; do not make it a fixed
monolithic update function.

### Mocopi integration

> **Superseded** by motion policy §8.2. The flow below is correct in shape but
> is now expressed generically: a motion-capture system decodes into a
> `LiveCaptureSource` producing `motion::MotionPose`, and Mocopi is one
> concrete adapter under `adapters/liveCapture/mocopi/`. No core code, shared
> schema, retarget API, or shared OpenExec node names it.
>
> Two further corrections since: the **first** adapter written is the generic
> VMC Protocol one, not the Mocopi-native one — a Mocopi application acting as a
> VMC sender reaches the same pipeline today — and the "retarget node" below is
> not a prerequisite. The whole path completes offline, through `vrmRetarget`
> and `motion_retarget`, with no OpenExec involvement (motion policy §11.4).

The recommended flow for Mocopi input:

```text
Mocopi motion stream
  ↓
source skeleton adapter
  ↓
humanoid semantic mapping
  ↓
retarget node
  ↓
VrmHumanoidAPI target
  ↓
constraints / look-at / spring bone
```

Keep Mocopi-specific processing out of the VRM importer; put it in an adapter on the
OpenExec runtime-plugin side.

---

## 11. Diagnostics & compatibility report

Report not just import success/failure, but per-feature quality.

### Capability status

Standardize these states:

- `supported`
- `approximated`
- `preserved`
- `unsupported`
- `invalid`
- `repaired`

Meaning:

- `supported`: semantics preserved and usable in the expected runtime
- `approximated`: approximated to a portable USD representation
- `preserved`: source information kept, but no evaluation capability
- `unsupported`: discarded on read, or not yet supported
- `invalid`: source violates the spec or is inconsistent
- `repaired`: the importer applied a safe repair

### Diagnostic categories

- parser
- buffer/accessor
- node hierarchy
- skin/skeleton
- humanoid
- expression
- look-at
- spring bone
- collider
- constraint
- material/MToon
- texture resolver
- schema contract
- runtime capability

### Severity

- info
- warning
- error
- fatal

### Output targets

- C++ diagnostic API
- USD metadata or diagnostic prim
- CLI-readable report
- JSON report
- CI artifact

---

## 12. Validation policy

The validator must be runnable independently against an already-generated USD
stage, not only as the importer's internal check.

### Validation items

- `/Asset` hierarchy
- Schema application
- Relationship targets
- Skeleton joint tokens
- Skinning primvars
- Blend-shape targets
- Material binding
- Texture resolution
- Front-direction normalization
- Required humanoid bones
- Spring/collider references
- Constraint cycles
- Schema contract version
- Package portability

### CLI examples

```bash
usdvrm validate avatar.vrm
usdvrm validate avatar.usda
usdvrm inspect avatar.vrm
usdvrm report avatar.vrm --json report.json
```

The CLI name and format are still to be decided, but there must be a validation
entry point usable by both CI and users.

---

## 13. Test strategy

### 13.1 Unit tests

Targets:

- Accessor decode
- Canonicalization
- Coordinate conversion
- Joint remapping
- Inverse bind matrix
- Expression normalization
- Spring-bone parameter conversion
- Schema attribute authoring
- Diagnostic classification

### 13.2 Generated fixtures

Uses:

- Minimal cases
- Error cases
- Boundary values
- Missing extensions
- Invalid references
- Cycles
- Sparse accessors
- Multiple skins
- Unusual transforms
- Embedded/external images

### 13.3 Real-world corpus

Use redistributable VRM.

Uses:

- Exporter-specific differences
- Complex node hierarchies
- Multiple skins
- Many morph targets
- MToon parameters
- Texture packaging
- Real humanoid mappings

### 13.4 Golden USD tests

For stable fixtures, compare the structure of the generated USD.

Compared:

- Prim hierarchy
- Type names
- Applied schemas
- Relationships
- Attribute values
- Token ordering
- Diagnostic summary

Prefer a semantic normalized comparison over a binary diff.

### 13.5 Image conformance tests

Introduce reference-image comparison for MToon and deformation.

- Bind pose
- Animation frame
- Expression
- Look-at
- Spring bone
- Outline
- Transparent material

---

## 14. CI / platform policy

At minimum, provide these platform lanes.

### Linux

- Supported glibc lower bound
- Release build
- Plugin discovery
- VRM open
- Schema registration
- Texture resolver
- CLI validation
- Real-model smoke test

### macOS

- Apple Silicon
- x86_64 as needed
- OpenUSD runtime
- Plugin discovery
- Package resolver
- Real-model smoke test

### Windows

Raise the priority and add:

- MSVC release build
- DLL discovery
- Plugin registry
- `.vrm` open
- Schema registration
- Texture resolution
- Path encoding
- Real-model smoke test

### OpenUSD compatibility matrix

State the supported versions explicitly.

Example:

```text
OpenUSD 24.xx: supported
OpenUSD 25.xx: supported
OpenStrata bundled runtime: supported
older versions: best effort
```

Decide the actual version range from CI results.

---

## 15. Documentation

The current top priority is closing the contract gap between implementation and
documentation.

### Required updates

1. Unify phase notation
2. List of implemented features
3. The current front-direction method
4. The texture resolver's primary path
5. Handling of temp extraction
6. Schema contract v1
7. Definitions of supported / approximated / preserved / unsupported
8. That runtime simulation is a separate layer
9. Platform compatibility
10. Known limitations
11. Build / install / plugin discovery
12. Sample inspect / validate output

### Proposed document structure

```text
README.md
docs/
  architecture.md
  usd-layout.md
  canonical-model.md
  schema-contract.md
  compatibility.md
  diagnostics.md
  mtoon.md
  openexec-runtime.md
  testing.md
  release.md
```

README focuses on overview and navigation; detailed specs are split into `docs/`.

---

## 16. Release policy

The next milestone target is `v0.1.0`.

### What v0.1.0 means

- Does not mean production-ready
- Publication of schema contract v1
- The first stable version of the import architecture
- A distribution boundary that external users can try
- The point where breaking changes start to be managed in release notes

### Release artifacts

Provide, as far as feasible:

- Source archive
- Linux binary
- macOS binary
- Windows binary
- Schema resources
- Package-resolver resources
- Sample VRM
- Sample generated USD
- Compatibility report
- Checksums

### Release notes

- Supported OpenUSD versions
- Supported platforms
- Implemented VRM features
- Approximations
- Known limitations
- Schema contract version
- Upgrade notes

---

<a id="17-roadmap"></a>

## 17. Product planning vocabulary

Product P0–P6 was the implementation planning sequence. Its old work lists
are [archived](../archive/release-preparation/product-sequences.md), not
ongoing obligations. Current open tasks and targets are owned by the
[roadmap](../roadmap/README.md); uncommitted feasibility work is in
[ideas](../ideas/README.md). The contracts in §1–§16 remain binding.

<a id="p0-documentation--implementation-sync"></a>

### P0: Documentation and implementation sync

Document lifecycle and ownership are defined by
[documentation.md](../contributing/documentation.md).

### P1: Release stabilization

Release quality is defined by the
[closure checklist](../contributing/release-closure.md).

### P2: Fix the canonical-model contract

The canonical boundary and fidelity rules remain in §6; remaining candidates
are in the [backlog](../roadmap/backlog.md#importer-and-corpus).

### P3: Windows CI & runtime verification

Measured configurations are in the
[reference](../reference/SUPPORTED_CONFIGURATIONS.md), not a standing checklist.

### P4: The motion & runtime layer

VRM semantics are defined by [VRM_MOTION_POLICY.md](VRM_MOTION_POLICY.md).
Generic motion and input are owned by their respective repositories.

### P5: MToon realization

Schemas, importer canonicalization and generated realizations are complete
here; current constraints remain in
[MATERIAL_ARCHITECTURE_POLICY.md](MATERIAL_ARCHITECTURE_POLICY.md).
Full rendering and conformance images belong to `hydra-toon`.

### P6: Round-trip / exporter research

VRM reverse conversion is [research](../ideas/README.md), without a release
commitment. Native USD export is a different operation, governed by
[VRM_EXPORT_POLICY.md](VRM_EXPORT_POLICY.md).

---

## 18. Recommended repository layout

> **Superseded for structure.** The layout sketched below was the pre-split
> proposal, where `usdVrm` was a single bundle with co-located schemas and
> resolver. The binding structural contract is now
> [architecture/WORKSPACE.md](../architecture/WORKSPACE.md), which splits these
> responsibilities into `vrmSchema`, `usdVrmFileFormat`,
> `usdVrmPackageResolver`, and the `vrmContainer` library, and retires `usdVrm`
> as a bundle id. The *responsibility boundaries* below still hold; only their
> packaging changed. Kept for rationale, and because §-numbers are cited
> elsewhere.

```text
usd-vrm-plugins/
  plugins/
    usdVrm/
      reader/
      canonical/
      authoring/
      schema/
      validation/
      diagnostics/
      resolver/
    execVrm/
      nodes/
      evaluators/
      adapters/
  tools/
    usdvrm/
  docs/
  tests/
    unit/
    fixtures/
    corpus/
    golden/
    images/
  examples/
  cmake/
```

`execVrm` is a separate plugin or module, to keep the file-format plugin's maturity
intact.

---

## 19. Design principles

Prioritize these in future decisions.

1. **Semantics first** — preserve VRM semantics, not just the look.
2. **Canonical boundary** — separate the dependency boundaries of parser, USD,
   renderer, and runtime through the canonical model.
3. **Portable core, specialized runtime** — keep the core portable; make
   renderer/runtime-specific processing adapters.
4. **Explicit approximation** — do not silently convert incompletely-supported
   processing; report it as an approximation.
5. **Discoverable schemas** — let downstream consumers discover semantics from types
   and relationships.
6. **No hidden compatibility transforms** — bake format-compatibility correction
   into the data wherever possible; do not pollute the root.
7. **Real asset validation** — continuously verify with a real VRM corpus, not just
   synthetic fixtures.
8. **Release-driven stability** — treat schema, CLI, diagnostics, and platform
   support as a release contract.

---

## 20. Planning intake

The former sprint checklist is [archived](../archive/release-preparation/product-sequences.md#20-recommended-near-term-actions).
New work enters the [roadmap](../roadmap/README.md) with an owner, trigger,
acceptance criteria and evidence destination; there is no parallel sprint plan here.

---

## 21. Conclusion

`usd-vrm-plugins` is already past being a simple file-reading experiment.

Its greatest present value is that it has begun decomposing VRM's application-level
semantics into the next layers:

- File parsing
- Canonical representation
- USD scene description
- Typed schema
- Diagnostics
- Runtime evaluation
- Renderer realization

Going forward, the goal is not just to increase the feature count. Prioritize:

1. Documentation/implementation sync
2. Schema-contract stabilization
3. The `v0.1.0` release
4. Runtime CI including Windows
5. Separation of the OpenExec runtime layer
6. Separation of MToon source semantics and renderer realization
7. Fixing the canonical model with round-trip in mind

Following this policy moves the project to its next state:

> Not a VRM importer, but a VRM interoperability foundation on OpenUSD.
