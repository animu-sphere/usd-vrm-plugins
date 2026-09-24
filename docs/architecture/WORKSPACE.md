# Workspace contract

This document is the binding contract for splitting `usdVrm` into an
OpenStrata plugin workspace. It fixes bundle identities, dependency
directions, artifact naming, and the invariants every migration PR must
preserve. Structural changes that contradict this document require changing
this document first, in its own PR.

Status: contract adopted; Phase 0 baseline frozen; Phase 1 `vrmSchema` split,
Phase 2 `vrmContainer` extraction, Phase 3 `usdVrmPackageResolver` split, and
Phase 4 `usdVrm` → `usdVrmFileFormat` rename landed (see §8). `usdVrm` is no
longer a bundle id; it names the aggregate product only (§1).

How VRM and VRMA use motion is
[design/VRM_MOTION_POLICY.md](../design/VRM_MOTION_POLICY.md). Workspace Phase 5
emits the aggregate product archive, but its standalone packaging-closure P0
remains open.

**Every generic motion identity has left this repository** (MIG-1..MIG-4,
2026-09-19..24). The `usd-motion-plugins` design policy settles the
ecosystem's motion architecture: vendor- and avatar-format-neutral motion,
retargeting, recording and the `UsdSkelAnimation` bridge live in
`usd-motion-plugins`, device and protocol input lives in `motion-connectors`,
and this repository keeps VRM and VRMA. §1 and §2 describe the reduced tree,
which builds VRM and VRMA only and consumes the shared packages by digest.
§9 is the record of where each identity went and under which rules.

Between 2026-07-28 and 2026-08-31 this contract also named the input
adapters, `liveTransport`, `osc`, `motionTracking` and the recorded-file layer
(`motionSource`, `motionBvh`, the producer profiles). Each had a row in §1,
edges in §2 and a side of §5's product split. All of them left, and their
rules are now their destination's to state. The reasoning as this contract
held it is in the v0.9.0 copy of this document
(`git show v0.9.0:docs/architecture/WORKSPACE.md`), the last release that
built them.

## 1. Bundles and libraries

Shipped through Workspace Phase 7:

| Identity | Kind | Role |
| --- | --- | --- |
| `vrmSchema` | plugin bundle (`usd-schema`) | VRM schema APIs (`VrmHumanoidAPI`, `VrmExpressionAPI`, `VrmLookAtAPI`, `VrmSpringBoneAPI`, `VrmColliderAPI`, `VrmConstraintAPI`; the material schemas `VrmMaterialAPI`, `VrmMToonAPI`, `VrmTextureInfoAPI`), schema tokens, `schema.usda` + generated sources, schema contract version |
| `usdVrmFileFormat` | plugin bundle (`usd-fileformat`) | `.vrm` `SdfFileFormat`, VRM 0.x / 1.0 detection, glTF/VRM parsing, canonical model (private), USD authoring (geometry, materials, skeleton, animation, schema application), import diagnostics |
| `usdVrmPackageResolver` | plugin bundle (`usd-package-resolver`) | `avatar.vrm[images/...]` package path resolution, embedded resource byte access, malformed/truncated/out-of-range rejection |
| `vrmContainer` | plain CMake library (`libs/`) | GLB header/chunk parsing, buffer-view access, byte-range validation, immutable byte views — shared by file format and resolver |
| `vrmCore` | plain CMake library (deferred) | canonical model, only if a second consumer beyond the importer appears |
| `usdVrm` | aggregate product name | retired as a bundle id; names the aggregate package composed of the bundles above |

VRMA and the VRM side of motion (Workspace Phase 6–8; motion policy §2, §14):

| Identity | Kind | Role |
| --- | --- | --- |
| `usdVrmaFileFormat` | plugin bundle (`usd-fileformat`, v0.3.0) | `.vrma` `SdfFileFormat`, glTF/GLB animation parsing, canonical semantic `HumanoidSkeleton`, `UsdSkelAnimation` + provenance. Avatar-independent: it never resolves, binds to, or retargets onto a target VRM. |
| `execVrm` | plugin bundle (`usd-exec`, bootstrapped 2026-09-13) | VRM semantics applied to a target rig: humanoid retarget, root-motion resolve, expression, look-at, avatar apply — driven by the schema contract only. **The boundary, `vrm.computeTargetSkeleton` and `vrm.computeBoundPose` on `UsdSkelSkeleton`, `vrm.computeBindingPose` on the applied `UsdSkelBindingAPI`, and `vrm.computeHumanoidMap`, `vrm.computeRestPoseCorrection`, `vrm.humanoidRetarget` and `vrm.computeJointLocalTransforms` on the applied `VrmHumanoidAPI` exist (2026-09-13) — the OpenExec plan's five P0-5 nodes, and two it needed; expression, look-at and avatar apply do not exist yet.** It declares `UsdSkelSkeleton`, `UsdSkelBindingAPI` and `UsdVrmHumanoidAPI` and links nothing of `vrmSchema` or `execMotion`, and needs both in the session: exec resolves the second schema by type name, and the retarget's pose is `execMotion`'s `motion.sampleAnimation`, read by name (§2). `execMotion` is `usd-motion-plugins`' published bundle, pinned by digest in `requires.bundles`. |
| `vrmRig` | plain static CMake library (`libs/vrmRig/`; `vrmRetarget` until 2026-09-23) | What a VRM rig adds to a retarget, and none of it retargets: VRM 1.0's required-bone set, which every caller hands the retarget, and — Motion Phase G — the two consumer resolves: `ExpressionResolver` (a named weight onto one rig's binds) and `LookAtEvaluator` (a target point onto one rig's eyes or its gaze expressions). Links `motionCore` and nothing else of `usd-motion-plugins` (§9.5). |
| `motion_retarget` | CLI executable (`tools/motionRetarget`, v0.4.0) | Reads the target rig off a stage, and the semantic clip through `motionUsd` plus the clip's `vrm:` tracks, drives `motionRetarget` and `vrmRig` over plain values, authors the retargeted `UsdSkelAnimation` and its `skel:animationSource` binding. Not a bundle — it registers nothing with OpenUSD. |

**What this workspace consumes is not listed here.** An identity in these
tables is one this workspace builds. The shared motion packages are
`usd-motion-plugins`': `motionCore`, `motionSampling`, `motionRetarget`,
`motionUsd` and the `execMotion` bundle. Each member that uses one names it in
its own descriptor, pinned by digest per target, and §2 says which member may
use which.

Every other identity these tables once held has left, with MIG-1..MIG-4:

- the motion libraries (`motionCore`, `motionRuntime`, the generic half of
  `vrmRetarget`) and the `execMotion` bundle;
- the recorded-file layer (`motionSource`, `motionBvh`, the BVH tools, the
  producer profiles) and the capture CLI (`motion_capture`);
- the live transport, the OSC decoder, the tracker layer and the four input
  adapters with their recorders.

§9.1 records where each one went and under which name.
`scripts/check_cmake_boundaries.py` fails if one comes back, under either
name (MIG-5).

`adapters/` no longer exists. An input adapter is `motion-connectors`', and
this workspace has none. The `ExecIr` adapter named in
[the OpenExec plan §3](../archive/motion-split/openexec-foundation.md) is a different
thing: an internal insulation layer inside `execVrm` that confines a
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
Measured on `ost` 0.23.6 (2026-09-24), after MIG-4 and MIG-2 had emptied
`adapters/` and taken `execMotion`: `5 bundle(s), 3 bundle edge(s), 2
libraries, 13 library edge(s), 1 tool(s), valid`. The library edges include
the digest-pinned consumed packages. The count was `10 libraries, 16 library
edge(s)` on 0.22.3, when the ten were the seven under `libs/` and the three
adapters.

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
      version: ">=0.2,<0.3"
      contract: 1
```

- `execVrm` reads the schema contract from the stage only — never importer
  internals.

## 4. Workspace root responsibilities

The root owns composition, not implementation:

- bundle discovery and workspace-wide configuration
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
reuse the one definition. Until 2026-09-24 the root `find_package`'d all five
motion packages up front, which made every configure require every package —
`motionRecording` included, which nothing here includes. That package is
reached only as an installed prefix on `CMAKE_PREFIX_PATH`: never
`add_subdirectory` of its source, never `FetchContent`. `ost` puts the
digest-pinned artifact there; a plain-CMake build puts a `cmake --install`
there, and `.github/workflows/plain-cmake.yml` proves that path with no `ost`
at all.

## 5. Artifact naming and versioning

> **What a consumer writes to use one of these is
> [PACKAGE_CONTRACT.md](PACKAGE_CONTRACT.md), not this section.** This section
> names artifacts and decides aggregate membership; it has never stated the
> package name, exported target, header root or required packages that a project
> outside this repository needs, and readers kept arriving here for them. That
> split was made on 2026-08-29, after two installed packages named an imported
> target no consumer could resolve while every lane was green — a defect neither
> document would have caught, because nothing in this workspace had ever opened a
> package config file. Nothing in this section changed with the split.

Per-bundle artifacts plus one aggregate:

```text
vrmSchema-<version>-<target>.tar.zst
usdVrmFileFormat-<version>-<target>.tar.zst
usdVrmPackageResolver-<version>-<target>.tar.zst
usdVrmaFileFormat-<version>-<target>.tar.zst
execVrm-<version>-<target>.tar.zst
usd-vrm-plugins-<version>-<target>-plugin-product.tar.zst (aggregate)
```

`execMotion` left this list with MIG-2 (2026-09-24). It is
`usd-motion-plugins`' artifact, and `ost` embeds the verified bundle in
`execVrm`'s package under `runtime/bundles/execMotion/`, so the product still
carries it ([PACKAGE_CONTRACT.md §4.1](PACKAGE_CONTRACT.md)).

**The product is declared, not discovered.** `openstrata.toml`'s
`release_members` names the aggregate: the five bundles and `motion_retarget`.
Packaging fails with `AGGREGATE_MEMBERSHIP_MISMATCH` when the discovered
bundle and tool ids minus `release_exclude` are not exactly that list.
`release_exclude` has been empty since MIG-4 took the three adapter CLIs it
named. `release.yml`'s staging step keeps its own count beside `ost`'s check,
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

Every identity those two clauses excluded — the four adapters,
`liveTransport`, `osc` — is `motion-connectors`' now. Every non-VRM identity
they admitted — `motionSource`, `motionBvh`, their tools, and the profiles
the product installed through `[[workspace.install_data]]` — is
`usd-motion-plugins`'. How the product carried and excluded each of them, and
what `ost` measured at every step, is in the v0.9.0 copy of this section.

Initial release rules: bundle identities and artifacts are separate; the git
tag is shared; all bundle versions stay synchronized with the repository
version; no independent release cadence until there is real demand.
Debug-symbol sidecars keep the ost `plugin package` convention
(`*-debug.tar.zst`).

## 6. Migration invariants

Every migration PR must preserve all of these:

1. Authored stage semantics do not change: all fixture stages produce
   baseline-identical output (Phase 0 snapshots are the reference).
2. One plugin boundary moves per PR; structural moves and feature changes
   never share a PR.
3. Each split PR adds (and CI runs) the standalone build of the bundle it
   creates, resolving siblings as installed packages.
4. Manifest and CMake package export are updated in the same PR as the code
   move; the manifest stays the source of truth for bundle metadata.
5. Plugin registration moves are proven by discovery tests in the same PR
   (no silently dropped or duplicated `plugInfo.json` registrations).
6. Package-path semantics (`avatar.vrm[images/...]`) do not change in the
   resolver split.
7. The rename PR (`usdVrm` → `usdVrmFileFormat`) adds no functionality.

## 7. Stage baseline policy (Phase 0)

Before any code moves, the current behavior is frozen as committed baseline
evidence, and every subsequent phase gate compares against it:

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

## 8. Phase status

| Phase | Deliverable | Status |
| --- | --- | --- |
| 0 | baseline snapshots + regression criteria | done (`tests/baseline/`) |
| 1 | `vrmSchema` bundle split | done (`plugins/vrmSchema`) |
| 2 | `vrmContainer` extraction | done (`libs/vrmContainer`) |
| 3 | `usdVrmPackageResolver` bundle split | done (`plugins/usdVrmPackageResolver`) |
| 4 | `usdVrmFileFormat` purification/rename | done (`plugins/usdVrmFileFormat`) |
| 5 | workspace packaging (per-bundle + aggregate) | aggregate product done; standalone registration P0 remains open upstream |
| 6a | `motionCore` bootstrap | done (`libs/motionCore`); left with MIG-1, 2026-09-21 (§9.1) |
| 6b | `motionRuntime` + `vrmRetarget` bootstrap | done (`libs/motionRuntime`, `libs/vrmRetarget`); left with MIG-2, 2026-09-21 and 2026-09-23, what stayed of `vrmRetarget` as `vrmRig` (§9.1) |
| 7 | `usdVrmaFileFormat` bundle bootstrap | done (`plugins/usdVrmaFileFormat`) |
| 8 | `execMotion` + `execVrm` bundle bootstrap | done: `execMotion` (`plugins/execMotion`, 2026-09-06; left with MIG-2, 2026-09-24) and `execVrm` (`plugins/execVrm`, 2026-09-13) |

> **Phase 6 was renumbered on 2026-07-18.** It previously read "`execVrm`
> (LookAt first)" — a single phase covering the whole runtime layer. The motion
> policy splits that into three plain libraries and two bundles, so the runtime
> bootstrap is now Workspace Phase 8 and the LookAt-first ordering is retired
> (the retarget core comes first). Documents citing "Workspace Phase 6 =
> `execVrm`" predate this.

Each of Phases 6–8 establishes a boundary only: manifest, CMake package export,
standalone build, discovery test, packaging. Motion Phases A+B fill the shipped
6a/7 boundaries; later behavior belongs to Motion Phases C–H. The two
sequences are not the same milestone, exactly as Workspace Phase 6 and Product
P4 are not.

> **The ladder ends at 8 and does not grow with every new library.** It tracks
> the *migration* out of the single `usdVrm` bundle — §6's invariants are written
> for moving existing code — and that migration is finished but for Phase 5's
> packaging P0 and Phase 8's bootstrap. Greenfield libraries take their identity
> and edges from §1 and §2 and no phase number: `adapters/liveCapture/vmc`
> shipped that way in v0.6.0, and `motionSource`, `motionBvh` and the BVH tools
> arrive the same way. Renumbering the ladder for each of them would make
> "Workspace Phase 0–8" — a string five documents repeat — mean something
> different every release, for no gain in what anyone can check.

Scaffolds for new bundles start from the ost template catalog
(`ost plugin new usd-schema --template usd-schema-cpp`,
`ost plugin new usd-package-resolver`) rather than hand-rolled skeletons.

> **Gate status — closed (ost 0.21.0).** This document called for the §2
> dependency directions to be enforced by a required PR gate from Phase 1 on.
> They now are: the `workspace-graph-pr` cell in `openstrata.ci.yaml` runs
> `ost plugin test --workspace --graph-only`, which validates the graph and
> exits on that result alone — no build, no runtime, milliseconds. Three
> `verify: test` workspace cells then build the root tree and run its CTest
> suite on all three OS, which is what the libraries and CLIs never had.
>
> v0.5.0 had tried to do this by hand and could not finish; the history is
> below, and the adoption is
> [report 33](../reports/ost/33-2026-07-28-v0.21.0-workspace-ci-adoption.md).
>
> The gate is a real one, not a formality: pointing `motionRuntime` at
> `motionCore >=0.9,<1.0` fails it with
> `WORKSPACE_LIBRARY_DEPENDENCY_VERSION_MISMATCH` and a non-zero exit.
>
> **What the gate actually covers (measured on ost 0.20.0, Workspace Phase 6b).**
> `ost plugin test --workspace` reports `4 bundle(s), 1 bundle edge(s), 4
> libraries, 7 library edge(s)` — so it *does* discover plain libraries from
> their `openstrata.library.yaml` and validate their `requires.libraries`
> edges, not only the plugin bundles. It caught a real
> `WORKSPACE_LIBRARY_DEPENDENCY_VERSION_MISMATCH` while Phase 6b was landing
> (the new libraries declared `motionCore >=0.4,<0.5` before `VERSION` moved off
> `0.3.0`), which is precisely the class of break this gate exists for. That
> makes the missing CI wiring more costly than it looked, not less. What the
> gate does **not** do is compile or unit-test a plain library: `--workspace`
> tests bundles. Under ost 0.20.0 `ost ci generate` also emitted one job per
> *bundle* cell, so `motionRuntime`, `vrmRetarget`, and the CLIs got no
> generated cell at all — recorded as an ask in
> [report 28](../reports/ost/28-2026-07-26-v0.20.0-motion-layer-ci-gap.md) and
> answered by 0.21.0's `kind: workspace` cell, which builds and tests them
> through the root tree instead.
>
> **v0.5.0 tried to cover that by hand and did not finish.** `motion-ci.yml`
> built the whole workspace with plain CMake from the repo root — the only
> configuration in which `libs/` and `tools/` targets exist — but was blocked at
> configure time: `pxrConfig.cmake` resolved Python development components to
> the paths of the Python the runtime was *built* against, which exist on no
> hosted runner. It shipped disabled and was deleted when the contract grew the
> cell it had been standing in for. That attempt is why the ask was accepted:
> a repo should not have to hand-roll a lane to test a library it declares
> through `openstrata.library.yaml`, and when it tries, it runs into a second
> problem the contract cannot express.

## 9. Destinations under the motion architecture

Added 2026-09-17. The `usd-motion-plugins` design policy ("the motion-plugins
policy" below) fixes one dependency direction for the ecosystem —
`usd-vrm-plugins`, `usd-mmd-plugins` and `motion-connectors` depend on
`usd-motion-plugins`, `usd-avatar-runtime` on all of them, and never the
reverse — and says generic motion code in this repository migrates there
(its §19.1, §37). This section is the structural half of that decision: where
each identity goes. The plan and its order are
[the migration track](../archive/motion-split/motion-foundation-split.md); why the
scope changed is
[the scope policy](../design/INTEGRATION_SCOPE_POLICY.md).

It supersedes the split preconditions this repository set itself on
2026-09-06 — two non-VRM consumers, independent versioning, a diverged
cadence — which asked *whether* a component leaves. That is now decided
outside this repository, and `usd-mmd-plugins` is a planned second non-VRM
consumer in any case.

**Every move is done, and this section is a record from here** (MIG-5,
2026-09-24). Every identity §9.1 gives another destination arrived there with
its history, and this repository consumes it as an installed package or no
longer uses it. The table below says where each went. The rules in §9.2 are
kept because they bind a future move as much as the finished ones, and rule 1
is checked mechanically (§2).

### 9.1 Destination of every identity

| Identity | Destination | What arrives there | What stays here |
| --- | --- | --- | --- |
| `motionCore` | `usd-motion-plugins` (`motionCore`, renamed types, §9.3) — **done 2026-09-21**, consumed here | the joint vocabulary, the pose and clip values | nothing |
| `motionRuntime` | `usd-motion-plugins` (`motionSampling` and `motionRecording`) — **done 2026-09-21** | sampling, filtering, blending, root motion; the capture trace and the recorder | nothing; `motionSampling` is consumed here, and `motionRecording` by nothing here since 2026-09-24 |
| `vrmRetarget` | split, along the line §9.5 draws — **done 2026-09-23** | the generic pose retargeter, the skeleton and the joint map, rest-pose handling, root-motion policy and the body retarget's diagnostics → `motionRetarget`, consumed here | VRM 1.0's required-bone set, `ExpressionResolver`, `LookAtEvaluator` — VRM semantics (motion-plugins policy §38), as `vrmRig`; building a map from `VrmHumanoidAPI` is already `execVrm`'s and `motion_retarget`'s |
| `motionSource`, `motionBvh`, `motion_bvh_inspect`, `motion_bvh_convert`, `profiles/motion/` | `usd-motion-plugins` (BVH, its §26–§27) — **done 2026-09-23**, deleted here | the format-neutral source layer, the BVH reader and tools, the declarative producer profiles | nothing |
| `motionFbx`, `usdBvhFileFormat` (deferred) | `usd-motion-plugins` | reserved there, if ever created | nothing |
| `motion_capture` | `usd-motion-plugins` (`motion_record`) — **done 2026-09-23**, deleted here | trace → avatar-independent clip | nothing |
| `motion_retarget` | split — **done 2026-09-23** | the generic half of the stage **reading** (`StageIo`, §9.5) → `motionUsd`, arrived 2026-09-20, consumed 2026-09-23 | a VRM retarget CLI over the shared libraries, and the bake: `WriteRetargetedAnimation` authors onto a VRM avatar |
| `execMotion` | `usd-motion-plugins` (`plugins/execMotion`, optional, its §21) — **done 2026-09-24**, consumed here by digest | the vendor-neutral OpenExec nodes | nothing |
| `execVrm` | stays | — | VRM semantics as OpenExec nodes, over the shared core |
| `liveTransport`, `osc` | `motion-connectors` (`motionConnectorTransport`, `motionConnectorOsc`) — **done 2026-09-21** | UDP receiver, capture file, OSC 1.0 wire format | nothing |
| `vrmAdapterVmc`, `vrmAdapterMocopi`, `vrmAdapterVrchatOsc`, their record tools | `motion-connectors` (`motionConnectorVmc`, `motionConnectorMocopi`, `motionConnectorVrchatOsc`) — **done 2026-09-21** | protocol and device decode, frame assembly, recording | nothing |
| `motionTracking` | `motion-connectors` (`motionConnectorTracking`) — **done 2026-09-21** | tracker regions, assignment, the tracker solve | nothing |
| `vrmAdapterArdy` (reserved) | `motion-connectors`, behind the motion-plugins generator interface | — | nothing |
| `usdVrmaFileFormat` | stays | — | `.vrma` reading and its stage (motion-plugins policy §26), consuming the shared `motionCore` |
| `vrmSchema`, `usdVrmFileFormat`, `usdVrmPackageResolver`, `vrmContainer`, `vrmCore` | stay | — | the product |

No row whose destination is another repository has code here any more. A
new generic motion feature is proposed in `usd-motion-plugins`, and a new
device or protocol input in `motion-connectors`. Until 2026-09-24 such a row
was **frozen here**: it took fixes and the work v0.9.0 still owed, but no new
generic capability.

The destination names are lower-camel identities, each also its CMake package
name and its exported target's (`motionCore::motionCore`), exactly as
[PACKAGE_CONTRACT.md](PACKAGE_CONTRACT.md) states them here. Both destinations
settled that on 2026-09-19 (their WS-O1). So `motionCore`, `motionSource` and
`motionBvh` keep their identity when they move, and `usd-motion-plugins`
v0.1.0 publishes a `motionCore` package whose types are renamed (§9.3).
Nothing can resolve both, because rule 1 below never leaves two copies, and the
version names which one a consumer has.

### 9.2 Moving rules

The §6 invariants were written for moving code *within* this workspace. Moving
it *out* adds these:

1. **One identity per move, and the edge flips in the same change.** When an
   identity arrives in its destination and is published as an installed
   package, the change here deletes the in-tree copy and consumes the package
   through `find_package` — never both copies at once, and never a copy kept
   "until later" (motion-plugins policy §37: duplicating a generic algorithm
   permanently is not acceptable).
2. **Behaviour does not change across the move.** The retarget, BVH and
   OpenExec parity evidence this repository holds is re-run against the
   consumed package before the in-tree copy is deleted; a difference blocks
   the move.
3. **History moves with the code** (`git filter-repo` or an equivalent), so
   a file's blame survives in its destination.
4. **The reverse edge is refused.** Nothing in `usd-motion-plugins` or
   `motion-connectors` may depend on an identity that stays here; a need for
   one means the boundary is in the wrong place, and is raised as a finding.
5. **Same OpenUSD.** A consumed package is built against this repository's
   exact OpenUSD pin; a mismatch is a configure error.
6. **Adapters before the core they depend on do not move.** An identity moves
   only after everything it links has a published destination, so the order
   is the dependency order: `motionCore`, `motionRuntime`, the generic half of
   `vrmRetarget` and `motionSource`/`motionBvh` to `usd-motion-plugins`; then
   `liveTransport`, `osc`, `motionTracking` and the adapters to
   `motion-connectors`.
7. **Every live input moves at once.** `vrmAdapterMocopi` and
   `vrmAdapterVrchatOsc` link `liveTransport` and `osc`. Moving the shared
   leaves first would leave either two copies for a release (rule 1) or an
   edge from here to `motion-connectors` that no contract declares. So
   `liveTransport`, `osc`, `motionTracking`, the three `vrmAdapter*` libraries
   and their record tools arrive together in `motion-connectors` v0.1.0, and
   this repository drops all of them in one change (`motion-connectors`
   WS-O7, decided 2026-09-19).

### 9.3 Names

The shared core's public names are the motion-plugins policy's, and the types
are renamed when they arrive, not before — renaming in place would change
every consumer here twice.

| Here | In `usd-motion-plugins` |
| --- | --- |
| namespace `motion` | `openstrata::motion` |
| `motion::HumanoidPose` | `MotionPose` |
| `motion::HumanoidAnimation` | `MotionClip` |
| `motion::HumanBone` | `HumanJoint` |
| `motion::RootMotion` | `RootMotion` |
| `vrmRetarget::TargetSkeleton` | `SkeletonDescriptor` |
| `vrmRetarget::HumanoidMap` | `RetargetMap` |
| `motion::ExpressionWeights` on the pose | `MotionChannelSet` |
| the generic half of `vrmRetarget` (namespace, `VRMRETARGET_*` macros, include root) | `motionRetarget` |
| `VRM_RETARGET_*` diagnostic codes | the destination's code style (its DIAG-O1) |

Where a published contract there chooses differently, the published contract
wins and this table is corrected. Until the last of the moving headers left,
`tests/boundary/motion-vocabulary.json` listed every VRM-vocabulary name they
still spelled and which of these rows or which §9.5 finding disposed of it,
and `workspace_motion_vocabulary` failed on a name it did not list. Both were
retired on 2026-09-23 with the generic half of `vrmRetarget`, the last headers
they scanned.

### 9.4 Sequences

The migration's phases are the motion-plugins policy's §37 Phase A–F. In this
repository they are always written **Migration Phase A–F**, never "Phase A",
because Motion Phase A–H already exists here and the two are unrelated. The
Workspace ladder (§8) does not grow for them: it tracked the move out of
`usdVrm`, and a move out of the repository is not a step on it.

### 9.5 The line through `vrmRetarget`

Drawn on 2026-09-19, as MIG-0's second item
([the migration track §2](../archive/motion-split/motion-foundation-split.md#2-mig-0--preparation-)).
It is drawn here, in a change of its own, because it splits an identity.

The split is by header, and one function in one header is the only thing cut in
two. The VRM half includes nothing from the generic half, and the generic half
includes nothing from the VRM half. Both are measured from the sources, not
inferred from the names:

| Header | Declares | Goes to | Why |
| --- | --- | --- | --- |
| `TargetSkeleton.h` | `TargetJoint`, `TargetSkeleton`, `DecomposeRestTransform` | `motionRetarget` as `SkeletonDescriptor` | a rig as plain values: joint tokens, parents, rest transforms |
| `HumanoidMap.h` | `HumanoidMap`, without `GetRequiredBones` | `motionRetarget` as `RetargetMap` | it maps `motion::HumanJoint` to a joint index, and it never reads a VRM binding. Every caller builds one from `VrmHumanoidAPI` (`execVrm`'s `ExecVrmRig`, `motion_retarget`'s `StageIo`), and that reading stays with the caller |
| `HumanoidMap.h` | `GetRequiredBones`, and the check that reads it | **cut**: the set stays here, the check moves with a caller-supplied set | finding 1 below |
| `RestPose.h` | `SourceRestPose`, `RestPoseCorrection`, `ComputeRestPoseCorrection` | `motionRetarget` | the rest-pose path rule, stated for any two rigs |
| `RootMotionPolicy.h` | `RootMotionMode`, `RootMotionOptions`, `ResolveRootTranslation` | `motionRetarget` | the root-motion policy. `Hips` is the default because the motion contract records body translation on the hips ([`usd-motion-plugins` MOTION_CONTRACT.md §5.3](https://github.com/animu-sphere/usd-motion-plugins/blob/main/docs/design/MOTION_CONTRACT.md#53-root-motion-and-the-hips)), which is a rule for every producer and not a `.vrma` convention |
| `PoseRetargeter.h` | `PoseRetargeter`, `RetargetedPose`, `RetargetedAnimation`, `JointLocalTransforms`, `GetJointWorldTransform`, `DiagnoseRig`, `RetargetOptions` | `motionRetarget` | the retarget itself |
| `Diagnostics.h` | the eight frozen `RetargetDiagnosticCode`s and their record | `motionRetarget`, codes restyled (§9.3) | the body retarget's codes: five raised by the library, three by a stage-holding caller that is itself `motionUsd` or a CLI there |
| `ExpressionResolver.h` | `ExpressionResolver` and its diagnostics | **stays** | resolves producer expression names onto one avatar's VRM expressions and binds |
| `LookAtEvaluator.h` | `LookAtEvaluator`, `ParseLookAtRangeMaps` | **stays** | reads a VRM rig's look-at type and range maps, in the 0.x and 1.0 shapes |

What stays in `libs/vrmRetarget/` is then two resolvers and a bone set, and
none of it retargets. Whether it keeps the name is decided in MIG-2, when it
happens. It is not decided here, because renaming a library that is still
whole would change every consumer twice.

**It did not keep it.** The cut was made on 2026-09-23, and what stayed is
`vrmRig` (`libs/vrmRig/`), the name decided on 2026-09-20. The line is
kept mechanically after the cut: `vrmRig_boundaries` forbids every
`usd-motion-plugins` package but `motionCore`, so the VRM half still includes
nothing from the generic half.

`motion_retarget`'s `StageIo` splits along the same seam, and the move
(2026-09-20,
[usd-motion-plugins #13](https://github.com/animu-sphere/usd-motion-plugins/pull/13))
drew it more narrowly than this section first did. What went is the
**reading**: a skeleton, a clip's joints, its time codes and its samples.
What stays is everything VRM — `ReadAvatar`'s humanoid binding, expressions
and look-at, the clip's `vrm:` attributes — and **the writing too**.
`WriteRetargetedAnimation` is a bake onto a VRM avatar, not an
avatar-independent clip, so `motionUsd`'s authoring half came from
`motion_capture`'s clip writer instead, on 2026-09-19. `ReadClip` here
becomes a call to `ReadMotionStage` plus the `vrm:` reading, in the consuming
change.

**Findings.** These are the concepts a rename cannot carry. Each is fixed on
arrival, in its own change after the move (the destination's WORKSPACE.md §3
rule 4):

1. **The required-bone set is VRM 1.0's, and the generic retargeter reads
   it.** `HumanoidMap::GetRequiredBones` states which bones "a VRM 1.0 avatar
   must define", and `DiagnoseRig` raises `MissingRequiredBone` from it for
   every rig. The destination's motion contract says the joint vocabulary
   carries no required-bone rule, and that whether a target needs a joint is
   the retarget's question. So `motionRetarget` takes the required set from its
   caller. This repository supplies VRM 1.0's, and `usd-mmd-plugins` supplies
   its own or none.
2. **The look-at target is a pose field, not a channel.**
   `MotionPose::lookAtTarget` is a point, and the destination carries gaze
   as a channel in `MotionChannelSet`, whose value type is still open (its
   MC-O4, a first non-scalar channel). This one is not a VRM concept. It is a
   shape the destination has not decided yet, and it is recorded here so that
   MIG-1 does not decide it by accident.
3. **Expression weights are the channel set under a VRM name.** They are
   already carried verbatim and sorted by name, which is the destination's
   channel rule. The difference is the namespace (`vrm:happy`), and the
   producers choose it, not this library.
