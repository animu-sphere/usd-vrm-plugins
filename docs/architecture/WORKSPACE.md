# Workspace contract

This binding contract fixes current identities, dependency directions,
artifact naming and structural-change invariants. A contradictory structural
change first updates this contract in its own PR.

`usdVrm` names the aggregate product only. This workspace builds VRM and VRMA
components and consumes generic motion packages by digest. Generic motion is
owned by `usd-motion-plugins`; device/protocol input by `motion-connectors`.
VRM semantics are defined by [VRM_MOTION_POLICY.md](../design/VRM_MOTION_POLICY.md).

The [package contract](PACKAGE_CONTRACT.md) records installed-consumer behavior
and the standalone registration limitation. Migration history is
[archived](../archive/workspace-migration/WORKSPACE.md); §8–§9 keep old citations
resolvable and retain the rules that bind future moves.

## 1. Bundles and libraries

The workspace declares these VRM import identities:

| Identity | Kind | Role |
| --- | --- | --- |
| `vrmSchema` | plugin bundle (`usd-schema`) | VRM schema APIs (`VrmHumanoidAPI`, `VrmExpressionAPI`, `VrmLookAtAPI`, `VrmSpringBoneAPI`, `VrmColliderAPI`, `VrmConstraintAPI`; the material schemas `VrmMaterialAPI`, `VrmMToonAPI`, `VrmTextureInfoAPI`), schema tokens, `schema.usda` + generated sources, schema contract version |
| `usdVrmFileFormat` | plugin bundle (`usd-fileformat`) | `.vrm` `SdfFileFormat`, VRM 0.x / 1.0 detection, glTF/VRM parsing, canonical model (private), USD authoring (geometry, materials, skeleton, animation, schema application), import diagnostics |
| `usdVrmPackageResolver` | plugin bundle (`usd-package-resolver`) | `avatar.vrm[images/...]` package path resolution, embedded resource byte access, malformed/truncated/out-of-range rejection |
| `vrmContainer` | plain CMake library (`libs/`) | GLB header/chunk parsing, buffer-view access, byte-range validation, immutable byte views — shared by file format and resolver |
| `vrmSchemaSdk` | plain CMake library (`libs/vrmSchemaSdk/`) | Packages the `vrmSchema` bundle's whole install tree, headers included, for C++ consumers. It adds no source, and a consumer finds it as `vrmSchema` |
| `vrmCore` | plain CMake library (deferred) | canonical model, only if a second consumer beyond the importer appears |
| `usdVrm` | aggregate product name | retired as a bundle id; names the aggregate package composed of the bundles above |

VRMA and VRM motion identities (motion policy §2, §14):

| Identity | Kind | Role |
| --- | --- | --- |
| `usdVrmaFileFormat` | plugin bundle (`usd-fileformat`, v0.3.0) | `.vrma` `SdfFileFormat`, glTF/GLB animation parsing, canonical semantic `HumanoidSkeleton`, `UsdSkelAnimation` + provenance. Avatar-independent: it never resolves, binds to, or retargets onto a target VRM. |
| `execVrm` | plugin bundle (`usd-exec`, bootstrapped 2026-09-13) | VRM semantics applied to a target rig: humanoid retarget, root-motion resolve, expression, look-at, avatar apply — driven by the schema contract only. **The boundary, `vrm.computeTargetSkeleton` and `vrm.computeBoundPose` on `UsdSkelSkeleton`, `vrm.computeBindingPose` on the applied `UsdSkelBindingAPI`, and `vrm.computeHumanoidMap`, `vrm.computeRestPoseCorrection`, `vrm.humanoidRetarget` and `vrm.computeJointLocalTransforms` on the applied `VrmHumanoidAPI` exist (2026-09-13) — the OpenExec plan's five P0-5 nodes, and two it needed; expression, look-at and avatar apply do not exist yet.** It declares `UsdSkelSkeleton`, `UsdSkelBindingAPI` and `UsdVrmHumanoidAPI` and links nothing of `vrmSchema` or `execMotion`, and needs both in the session: exec resolves the second schema by type name, and the retarget's pose is `execMotion`'s `motion.sampleAnimation`, read by name (§2). `execMotion` is `usd-motion-plugins`' published bundle, pinned by digest in `requires.bundles`. |
| `vrmRig` | plain static CMake library (`libs/vrmRig/`) | What a VRM rig adds to a retarget, and none of it retargets: VRM 1.0's required-bone set, `ExpressionResolver` (named weights onto one rig's binds), `LookAtEvaluator` (a target point onto its eyes or gaze expressions), and `SpringBoneSolver` (stateful spring chains and root subtrees over plain rig/pose values). Links `motionCore` and nothing else of `usd-motion-plugins` (§9.5). |
| `motion_retarget` | CLI executable (`tools/motionRetarget`, v0.4.0) | Reads the target rig off a stage, and the semantic clip through `motionUsd` plus the clip's `vrm:` tracks, drives `motionRetarget` and `vrmRig` over plain values, authors the retargeted `UsdSkelAnimation` and its `skel:animationSource` binding. Not a bundle — it registers nothing with OpenUSD. |
| `vrm_export` | CLI executable (`tools/vrmExport`) | Writes an imported `.vrm` as a native `.usda`, `.usdc` or `.usdz` that opens with no VRM plugin: the root layer materialized, every texture localized, `.usdz` packaged by OpenUSD ([VRM_EXPORT_POLICY.md](../design/VRM_EXPORT_POLICY.md)). Links OpenUSD alone and opens a `.vrm` through the plugin registry. Its export operation is a target inside the tool, not a library identity, until a second consumer exists ([scope policy §3](../design/INTEGRATION_SCOPE_POLICY.md#3-the-test-for-a-new-identity)). Not a bundle. |

The canonical material schemas made visible to Hydra
([VRM_IMAGING_POLICY.md](../design/VRM_IMAGING_POLICY.md)):

| Identity | Kind | Role |
| --- | --- | --- |
| `vrmImaging` | plugin bundle (`usd-imaging`) | A UsdImaging API-schema adapter per applied material schema, contributing `vrm/…` data sources to the Hydra material prim beside UsdImaging's `material` container, and one locator per changed canonical property. Describes what a VRM material says and nothing about how it is drawn: no shader, GPU resource, image loading or pipeline choice (`hydra-toon`'s). Links OpenUSD alone and needs `vrmSchema` registered in the session, which its descriptor declares as a bundle edge. A product member. |

**What this workspace consumes is not listed here.** An identity in these
tables is one this workspace builds. The shared motion packages are
`usd-motion-plugins`': `motionCore`, `motionSampling`, `motionRetarget`,
`motionUsd` and the `execMotion` bundle. Each member that uses one names it in
its own descriptor, pinned by digest per target, and §2 says which member may
use which.

`scripts/check_cmake_boundaries.py` refuses an in-tree copy of a consumed
identity under either its former or published name (§9.2).

There is no `adapters/` tree. An input adapter is `motion-connectors`', and
this workspace has none. The `ExecIr` adapter named in
[the optional track](../roadmap/execir-track.md) is an internal insulation layer inside `execVrm` that confines a
possibly-experimental OpenUSD dependency.

Shared code is never a plugin bundle: `vrmContainer` and `vrmRig` have no
plugin registration and no `plugInfo.json`, and `vrmContainer` has no OpenUSD
types in its public API. `vrmRig` has no OpenUSD *stage* dependency, only value
types (`GfVec3f`, `GfQuatf`), and links one OpenUSD library beyond `gf`:
`js`, which parses a string into numbers and objects and touches no stage,
layer, plugin registry or exec. It is there because a VRM 0.x rig and a VRM
1.0 rig state the same four look-at curves in two different JSON shapes, and
making every consumer of `LookAtEvaluator` know both spellings would put the
importer's job in each of them.

Product names (`Mocopi`, `ARDY`, any SDK or research-model name) are forbidden
in every identity above. A device or protocol input is `motion-connectors`',
and a producer profile is `usd-motion-plugins'`. Such a name may otherwise
appear only in test fixtures and their provenance, `examples/`, and provider
metadata strings, and never as a branch condition in code (motion policy
§8.1, §9).

## 2. Dependency directions

Allowed:

```text
usdVrmFileFormat      -> vrmSchema, vrmContainer
usdVrmPackageResolver -> vrmContainer

usdVrmaFileFormat     -> vrmContainer, motionCore, motionUsd
vrmRig                -> motionCore
motion_retarget       -> vrmRig, motionCore, motionRetarget, motionSampling,
                         motionUsd, OpenUSD stage
execVrm               -> vrmRig, motionCore, motionRetarget
execVrm               -> vrmSchema   (runtime only: `requires.bundles`; exec
                         resolves UsdVrmHumanoidAPI by type name)
execVrm               -> execMotion  (runtime only: `vrm.computeBoundPose`
                         reads `motion.sampleAnimation` by name; the published
                         bundle is pinned by digest, nothing is linked, and
                         the reverse edge is not allowed)
execVrm               =: the Vrm*API applied schemas, UsdSkelSkeleton,
                         UsdSkelBindingAPI
tests/parity          -> motionCore, motionRetarget, motionSampling
vrm_export            -> OpenUSD only (usd, usdUtils, usdValidation, sdf, ar, js)
vrm_export            ~> usdVrmFileFormat, usdVrmPackageResolver, vrmSchema
                         (runtime only, through the plugin registry; nothing
                         is linked)
vrm_export            ~> motion_retarget executable, usdVrmaFileFormat
                         (--motion only; existing offline bake invoked without
                         a shell, VRMA opened through the plugin registry)
vrmImaging            -> OpenUSD only (usd, sdf, tf, vt, hd, usdImaging)
vrmImaging            ~> vrmSchema   (runtime only: UsdImaging asks an adapter
                         only about schemas a prim's definition includes, and
                         the field set is read from the schema registry by
                         name; nothing is linked)
```

Every `motion*` name above is a package `usd-motion-plugins` publishes, and
none is built here ([§9.2](#92-moving-rules) rule 1). Neither destination
depends on anything here (rule 4).

**Nothing here depends on `motion-connectors`.** A live session reaches an
avatar as a file that a connector's tool wrote: a capture trace, which
`usd-motion-plugins`' `motion_record` turns into a clip, which
`motion_retarget` bakes. So no member of the aggregate product opens a
transport or reads a wall clock. That is what makes every clip this
repository authors reproducible by construction, and it is the property
§2 held for `motion_capture` while that tool was here. The cost is the one it
always had: a live session is more than one command.

Forbidden (non-exhaustive; anything not allowed above is forbidden):

```text
vrmSchema             -> any other bundle or library
usdVrmPackageResolver -> usdVrmFileFormat, vrmSchema
usdVrmFileFormat      -> usdVrmPackageResolver (link-time; resolver is a
                         runtime bundle dependency only)
usdVrmFileFormat      -> usdVrmaFileFormat, motion generator, any motion package
usdVrmFileFormat      -> authoring ExecIr prims as a requirement of import
execVrm               -> usdVrmFileFormat private API, importer canonical model
execVrm               -> GLB parser (vrmContainer, cgltf), reparse of the
                         source .vrm / .vrma bytes, joint-name heuristics
execVrm               -> socket or device I/O, file watching, a wall clock, a
                         private thread pool, or mutable global state inside a
                         computation callback (see below)
execVrm               -> declaring UsdSkelAnimation, which the published
                         execMotion declares: an OpenExec schema has exactly
                         one declarer per session (see below)
vrmRig                -> network protocol, OpenExec, ExecIr
vrmRig                -> motionRetarget, and every other usd-motion-plugins
                         package but motionCore (the VRM half includes nothing
                         from the generic half, §9.5)
usdVrmaFileFormat     -> live receiver, generator, motionRetarget, vrmRig, a
                         target VRM
vrm_export            -> usdVrmFileFormat, vrmContainer, vrmSchema, vrmRig at
                         link time, the importer's canonical model, a GLB
                         parser of its own (it reaches a .vrm through
                         UsdStage::Open and reads the result as data)
vrmImaging            -> usdVrmFileFormat, vrmContainer, vrmSchema, vrmRig at
                         link time, the importer's canonical model, raw VRM
                         JSON (`vrm:*:raw`), an image decoder, a renderer or
                         GPU API (hydra-toon, Vulkan, WebGPU, Slang)
usdVrmFileFormat      -> vrmImaging  (the importer works in a session with no
                         imaging plugin; imaging policy §19)
any member            -> motion-connectors, a live transport, a protocol
                         decoder, a vendor SDK
any member            -> a copy of a usd-motion-plugins or motion-connectors
                         identity, under its name here or there (§9.2 rule 1)
any cycle, including self-cycles
```

Six of these are the motion layer's load-bearing invariants here, restated so a
reviewer can check them without opening the policy:

- **An OpenExec schema has exactly one declarer, so the two bundles partition
  them.** 26.08 keys its `Info.Exec.Schemas` metadata by schema type and refuses
  a second plugin that names a schema another already declared: the second one's
  computations for that schema are never registered, and the failure surfaces
  later as "computation not found" rather than as a load error. `execMotion`
  therefore declares `UsdSkelAnimation` and `execVrm` declares the `Vrm*API`
  applied schemas and `UsdSkelSkeleton`, and neither declares the other's. A
  bundle reaches a prim it does not own the schema of through an **input
  accessor**, or by registering on an applied API schema that prim carries —
  both measured
  ([the mechanism report](../reports/openusd/26.08-openexec-mechanism.md) §2,
  §3). This is a workspace rule and not a preference: the two bundles ship in one
  product and are loaded into one session, so a collision between them is not a
  configuration a user can avoid. Reading the *other* bundle's computation by
  name through such an accessor is allowed in one direction, `execVrm` reading
  `execMotion`'s `motion.sampleAnimation`, and it has two costs. The reading
  bundle names the other in `requires.bundles`, because exec drops a target that
  provides no computation from a fan-in without a word. And the reading bundle
  registers the value type itself, because `TargetedObjects<T>` checks the
  registration when the reading bundle's computations are registered, possibly
  before the other bundle has loaded. Both costs were measured by leaving them
  unmet ([the retarget report](../reports/openusd/26.08-openexec-retarget.md)
  §3).

- **`vrmRig` does not depend on OpenExec.** What a VRM rig adds to a retarget
  is finished and testable before any OpenExec node exists (motion policy
  §10.1, §18.12), as the retarget itself is in `motionRetarget`; `execMotion`
  / `execVrm` nodes are thin wrappers over them.
- **`usdVrmaFileFormat` is avatar-independent.** It authors a canonical semantic
  humanoid skeleton, never a target skeleton's joint order. Retarget is a
  separate, later step (motion policy §4.2, §4.3).
- **No member reaches a live input.** Device and protocol input is
  `motion-connectors`', and it arrives here as a file (above). This is what
  lets a capture product, a sender application or a generation model be
  swapped without touching the retarget, OpenExec or the importers.
- **An OpenExec computation evaluates an immutable snapshot and performs no
  I/O.** Receiving is a connector's job and buffering is `motionSampling`'s;
  a callback that opened a socket or read a clock would make cache reuse and
  invalidation untestable, which is the whole reason to be on OpenExec at all
  ([OpenExec plan §5](../archive/motion-split/openexec-foundation.md)). `execVrm_boundaries`
  checks it here, along with the bundle's links and its half of the schema
  partition above, on the source, the built library's imports, the target's
  link libraries and `plugInfo.json`. `execMotion_boundaries` checks the other
  half where the bundle lives
  ([execMotion's README](https://github.com/animu-sphere/usd-motion-plugins/blob/main/plugins/execMotion/README.md#how-the-rules-are-checked)).
- **`ExecIr` is optional and never a prerequisite.** It is confined to an
  adapter layer inside `execVrm`; the canonical motion contract is not derived
  from its representation, the importer never has to author its prims, and the
  offline pipeline stays whole with it absent
  ([OpenExec plan §7.0](../archive/motion-split/openexec-foundation.md)).

Enforcement: `ost plugin test --workspace` (ost >= 0.15.0) validates the
bundle graph declared via `requires.bundles` before running any bundle's
verification, with stable `WORKSPACE_*` issue codes (dependency missing,
version mismatch, contract mismatch, direction forbidden, cycle) and exit 5
on violation. Bundle manifests are the source of truth for these edges.

Plain-library edges (`requires.libraries`) became executable in ost 0.16.0: a
plain library carries an `openstrata.library.yaml` descriptor
(`libs/vrmContainer/`) giving it a workspace identity and CMake package/target,
and the workspace graph validates the `bundle -> library` edges (missing,
duplicate, version-incompatible, cyclic) alongside the bundle edges. `ost plugin
build/test/run` build and install the library into the workspace prefix before
its consumers and materialize its loader directory into the session; `ost plugin
package` stages the closure under `runtime/libraries/` with a
`dependencies.json` record. `vrmContainer`'s no-registration / no-OpenUSD
boundary is still enforced by its own repo check, and each consumer adds a
binary link check (`dumpbin`/`nm`) proving it imports `vrmContainer` and does not
import the other bundles' libraries (`usdVrmPackageResolver` proves it links
neither `usdVrmFileFormat` nor `vrmSchema`).

`openstrata.toml`'s `[workspace].members` names every descriptor, and a
descriptor no entry covers is a hard error rather than a silent omission.
**The consumed-package edges above are also checked as the build states
them**, by `scripts/check_cmake_boundaries.py` (`workspace_cmake_boundaries`),
statically and on every lane. The graph gate reads what a descriptor declares;
this reads what each member's CMake resolves and links and what its sources
include. It fails when those three disagree with each other, with the
descriptor, or with the allowed set here. `execVrm` carried a link to
`motionSampling` and `motionRecording` through a release after the last
include of either, and the graph gate could not see that: the descriptor
declared those same two packages as well. The `ALLOWED` table in that script
restates this section for consumed packages. A new member gets a row there
by hand, or the check refuses to run. The same script holds the other half
of rule 1 ([§9.2](#92-moving-rules)): an identity that left, under the name
it had here or the one it has there, fails it as a member, a target, a
file under `adapters/` or `profiles/motion/`, or a source that opens one of
their namespaces.

## 3. Schema contract versioning

- `vrmSchema` carries two independent versions: `plugin.version` (semantic
  implementation version) and `schema.contract` (authored-data contract).
- Compatible implementation releases keep `schema.contract` unchanged. A
  breaking type/property/token change increments it and requires
  authored-data migration notes.
- Consumers select the contract explicitly in their manifest:

```yaml
requires:
  bundles:
    - id: vrmSchema
      version: ">=0.10,<0.11"
      contract: 2
```

- `execVrm` reads the schema contract from the stage only — never importer
  internals.

## 4. Workspace root responsibilities

The root owns composition, not implementation:

- bundle discovery and workspace-wide configuration. A bundle is discovered
  by its `openstrata.plugin.yaml`, and no plugin directory is added by name
- integration tests (`tests/integration/`): schema+format, format+resolver,
  full composition, clean-install, aggregate packaging
- the CI matrix (`openstrata.ci.yaml`) and generated lanes
- aggregate packaging and compatibility reporting

The root must not own plugin C++ sources, schema sources, plugin
`plugInfo.json`, or bundle-specific third-party dependency setup. Bundles may
be composed with `add_subdirectory` in the workspace build, but every bundle
must also build standalone against installed packages
(`find_package(vrmSchema CONFIG REQUIRED)` etc.); sibling
`add_subdirectory(../otherBundle)` from inside a bundle is forbidden.

**The root resolves no dependency on a member's behalf.** A package this
workspace consumes from another repository — every `usd-motion-plugins`
library — is resolved by the member that links it, through
`usdvrm_consume_package()` (`cmake/UsdVrmConsumedPackage.cmake`), which also
makes the resolved targets global so a later member and a root-registered test
reuse the one definition. Each consumed package is
reached only as an installed prefix on `CMAKE_PREFIX_PATH`: never
`add_subdirectory` of its source, never `FetchContent`. `ost` puts the
digest-pinned artifact there; a plain-CMake build puts a `cmake --install`
there, and `.github/workflows/plain-cmake.yml` proves that path with no `ost`
at all.

## 5. Artifact naming and versioning

> Installed package consumption is owned by [PACKAGE_CONTRACT.md](PACKAGE_CONTRACT.md).

Per-bundle artifacts plus one aggregate:

```text
vrmSchema-<version>-<target>.tar.zst
usdVrmFileFormat-<version>-<target>.tar.zst
usdVrmPackageResolver-<version>-<target>.tar.zst
usdVrmaFileFormat-<version>-<target>.tar.zst
execVrm-<version>-<target>.tar.zst
vrmImaging-<version>-<target>.tar.zst
usd-vrm-plugins-<version>-<target>-plugin-product.tar.zst (aggregate)
```

`execMotion` is a consumed `usd-motion-plugins` artifact: `ost` embeds the verified bundle in
`execVrm`'s package under `runtime/bundles/execMotion/`, so the product still
carries it ([PACKAGE_CONTRACT.md §4.1](PACKAGE_CONTRACT.md)).

**The product is declared, not discovered.** `openstrata.toml`'s
`release_members` names the aggregate: the six bundles, `motion_retarget` and
`vrm_export`.
Packaging fails with `AGGREGATE_MEMBERSHIP_MISMATCH` when the discovered
bundle and tool ids minus `release_exclude` are not exactly that list.
`release_exclude` is empty. `release.yml`'s staging step keeps its own count beside `ost`'s check,
because the declaration is the thing a mistaken commit would edit: moving
`motion_retarget` into `release_exclude` satisfies `ost` and leaves the
product a CLI short.

**What the product may contain** is the rule this section enforced while the
adapters were here, and it binds whatever joins next. A library is in the
product only if it names no product **and opens nothing**:

- A producer name keeps it out, because the aggregate stays free of product
  names and of optional SDK, network and model dependencies and their license
  terms (motion policy §8.1).
- A transport keeps it out even when its name is neutral. A socket in the
  product's link closure ends the property that makes every clip
  reproducible by construction (§2).

Initial release rules: bundle identities and artifacts are separate; the git
tag is shared; all bundle versions stay synchronized with the repository
version; no independent release cadence until there is real demand.
Debug-symbol sidecars keep the ost `plugin package` convention
(`*-debug.tar.zst`).

<a id="6-migration-invariants"></a>

## 6. Structural-change invariants

Every structural-change PR must preserve all of these (schema changes follow §3):

1. Authored stage semantics do not change: all fixture stages produce
   baseline-identical output (committed snapshots are the reference).
2. One plugin boundary moves per PR; structural moves and feature changes
   never share a PR.
3. Each split PR adds (and CI runs) the standalone build of the bundle it
   creates, resolving siblings as installed packages.
4. Manifest and CMake package export are updated in the same PR as the code
   move; the manifest stays the source of truth for bundle metadata.
5. Plugin registration moves are proven by discovery tests in the same PR
   (no silently dropped or duplicated `plugInfo.json` registrations).
6. Package-path semantics (`avatar.vrm[images/...]`) do not change in a
   resolver move.
7. An identity rename adds no functionality.

<a id="7-stage-baseline-policy-phase-0"></a>

## 7. Stage baseline policy

Committed baseline evidence freezes behavior before structural moves;
every structural-change gate compares against it:

- per-fixture USDA snapshots (stage topology, material bindings, skeleton
  topology, animation output)
- schema contract snapshot (types, properties, tokens)
- plugin discovery results and public C++/Python symbol lists
- clean-install smoke results and embedded-texture resolution
- diagnostics codes (the corpus manifest's expected-code table)

A migration PR that changes any baseline artifact is a regression by
definition, regardless of tests passing.

The frozen evidence lives in `tests/baseline/` (see its README for the
artifact inventory and regression criteria) and is generated and verified by
`tools/baseline_freeze.py`; run
`ost plugin run plugins/usdVrmFileFormat -- python tools/baseline_freeze.py --check`
as the gate in every migration PR.

## 8. Migration history (compatibility link)

<a id="8-phase-status"></a>
The completed phase ladder, CI adoption history and old measurements are
[archived](../archive/workspace-migration/WORKSPACE.md#8-phase-status).
Current dependencies and artifacts are defined by §1–§5; remaining packaging
work is in the [backlog](../roadmap/backlog.md#package-portability).

## 9. External package contracts

<a id="9-destinations-under-the-motion-architecture"></a>

### 9.1 Destination of every identity

The historical identity/rename table is
[archived](../archive/workspace-migration/WORKSPACE.md#91-destination-of-every-identity).
Current consumed packages and their allowed edges are in §1 and §2.

### 9.2 Moving rules

These rules remain binding for future moves out of this workspace:

1. **One identity per move, and the edge flips in the same change.** Once an
   installed package is published, delete the in-tree copy and consume it
   through `find_package`. Never keep both copies, even temporarily.
2. **Behavior does not change across the move.** Rerun parity evidence against
   the consumed package before deleting the in-tree implementation.
3. **History moves with the code**, preserving blame in the destination.
4. **The reverse edge is refused.** Neither `usd-motion-plugins` nor
   `motion-connectors` may depend on an identity retained here.
5. **Same OpenUSD.** A consumed package uses the exact pin; a mismatch is a
   configure error.
6. **Move in dependency order.** Every linked dependency must have a published
   destination before its consumer moves.
7. **Move linked live-input groups atomically.** A move must not introduce
   duplicate implementations or an undeclared edge to `motion-connectors`.
   The original concrete group and its release provenance are
   [archived](../archive/workspace-migration/WORKSPACE.md#92-moving-rules).

### 9.3 Names

Published packages define their own public names. The old-to-new map is
[historical](../archive/workspace-migration/WORKSPACE.md#93-names).

### 9.4 Sequences

The completed migration sequences are
[historical](../archive/workspace-migration/WORKSPACE.md#94-sequences).
New identities take their contract from §1 and §2, not a new phase number.

### 9.5 The line through `vrmRetarget`

`vrmRig` owns VRM required bones, expressions, look-at and spring-bone values;
it may link `motionCore` and no other generic motion package. Retargeting is
`motionRetarget`'s. The original header-level cut and its findings are
[historical](../archive/workspace-migration/WORKSPACE.md#95-the-line-through-vrmretarget).
