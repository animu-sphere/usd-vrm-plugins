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

The motion layer (`.vrma` import, retargeting, the OpenExec runtime) was added
to this contract on 2026-07-18 from
[design/MOTION_ARCHITECTURE_POLICY.md](../design/MOTION_ARCHITECTURE_POLICY.md).
Its identities and edges are **reserved, not built** — nothing in Workspace
Phases 6–8 exists in the tree yet. Workspace Phase 5 now emits the aggregate
product archive, but its standalone packaging-closure P0 remains open.
Reserving the motion identities here first is what the header requires:
structure lands in this document before it lands in code.

## 1. Bundles and libraries

Shipped and next (Workspace Phase 0–5):

| Identity | Kind | Role |
| --- | --- | --- |
| `vrmSchema` | plugin bundle (`usd-schema`) | VRM schema APIs (`VrmHumanoidAPI`, `VrmExpressionAPI`, `VrmLookAtAPI`, `VrmSpringBoneAPI`, `VrmColliderAPI`, `VrmConstraintAPI`), schema tokens, `schema.usda` + generated sources, schema contract version |
| `usdVrmFileFormat` | plugin bundle (`usd-fileformat`) | `.vrm` `SdfFileFormat`, VRM 0.x / 1.0 detection, glTF/VRM parsing, canonical model (private), USD authoring (geometry, materials, skeleton, animation, schema application), import diagnostics |
| `usdVrmPackageResolver` | plugin bundle (`usd-package-resolver`) | `avatar.vrm[images/...]` package path resolution, embedded resource byte access, malformed/truncated/out-of-range rejection |
| `vrmContainer` | plain CMake library (`libs/`) | GLB header/chunk parsing, buffer-view access, byte-range validation, immutable byte views — shared by file format and resolver |
| `vrmCore` | plain CMake library (deferred) | canonical model, only if a second consumer beyond the importer appears |
| `usdVrm` | aggregate product name | retired as a bundle id; names the aggregate package composed of the bundles above |

Reserved for the motion layer (Workspace Phase 6–8; motion policy §2, §14):

| Identity | Kind | Role |
| --- | --- | --- |
| `usdVrmaFileFormat` | plugin bundle (`usd-fileformat`, reserved) | `.vrma` `SdfFileFormat`, glTF/GLB animation parsing, canonical semantic `HumanoidSkeleton`, `UsdSkelAnimation` + provenance. Avatar-independent: it never resolves, binds to, or retargets onto a target VRM. |
| `execMotion` | plugin bundle (reserved) | Vendor-neutral OpenExec motion nodes: clip sample, pose buffer, resample, filter, blend, apply-constraints, generate, record |
| `execVrm` | plugin bundle (reserved) | VRM semantics applied to a target rig: humanoid retarget, root-motion resolve, expression, look-at, avatar apply — driven by the schema contract only |
| `motionCore` | plain CMake library (reserved) | `motion::HumanoidPose`, `HumanoidAnimation`, `RootMotion`, `MotionConstraintSet`, source metadata. No USD stage authoring, no vendor SDK, no network. |
| `motionRuntime` | plain CMake library (reserved) | Timestamped pose buffer, interpolation/extrapolation, resample, filter, blend — the OpenExec-independent runtime |
| `vrmRetarget` | plain CMake library (reserved) | Humanoid map, rest pose, pose retargeter, root-motion policy, expression resolver. **Completed before OpenExec** (motion policy §18.12). |
| adapters | optional bundles (reserved, `adapters/`) | The only place product names are permitted: `adapters/liveCapture/mocopi/`, `adapters/generators/ardy/` |

Shared code is never a plugin bundle: `vrmContainer` has no plugin
registration, no `plugInfo.json`, and no OpenUSD types in its public API. The
same rule binds `motionCore`, `motionRuntime`, and `vrmRetarget` — and
`motionCore` additionally carries no OpenUSD *stage* dependency, only value
types (`GfVec3f`, `GfQuatf`).

Product names (`Mocopi`, `ARDY`, any SDK or research-model name) are forbidden
in every identity above except `adapters/`. They may otherwise appear only in
`tests/integration/`, `examples/`, and provider metadata strings — never as a
branch condition in core logic (motion policy §8.1, §9).

## 2. Dependency directions

Allowed:

```text
usdVrmFileFormat      -> vrmSchema
usdVrmFileFormat      -> vrmContainer
usdVrmPackageResolver -> vrmContainer

usdVrmaFileFormat     -> vrmContainer
usdVrmaFileFormat     -> motionCore
motionRuntime         -> motionCore
vrmRetarget           -> motionCore
vrmRetarget           -> motionRuntime
execMotion            -> motionCore, motionRuntime
execVrm               -> vrmSchema
execVrm               -> motionCore, motionRuntime, vrmRetarget
adapters/*            -> motionCore, motionRuntime
```

Forbidden (non-exhaustive; anything not allowed above is forbidden):

```text
vrmSchema             -> any other bundle or library
usdVrmPackageResolver -> usdVrmFileFormat, vrmSchema
usdVrmFileFormat      -> usdVrmPackageResolver (link-time; resolver is a
                         runtime bundle dependency only)
usdVrmFileFormat      -> usdVrmaFileFormat, motion generator, any motion library
execVrm               -> usdVrmFileFormat private API, importer canonical model
execVrm               -> GLB parser (vrmContainer, cgltf)

motionCore            -> any vendor SDK, any product-named code, any network
                         protocol, any OpenUSD stage authoring
motionRuntime         -> vrmSchema, any USD file-format bundle
vrmRetarget           -> network protocol, OpenExec
usdVrmaFileFormat     -> live receiver, generator, vrmRetarget, a target VRM
motionCore/motionRuntime/vrmRetarget -> adapters/*  (adapters depend on the
                         core; the core never depends on an adapter)
any cycle, including self-cycles
```

Three of these are the motion layer's load-bearing invariants, restated so a
reviewer can check them without opening the policy:

- **`vrmRetarget` does not depend on OpenExec.** The retarget core is finished
  and testable before any OpenExec node exists (motion policy §10.1, §18.12);
  `execMotion` / `execVrm` nodes are thin wrappers over it.
- **`usdVrmaFileFormat` is avatar-independent.** It authors a canonical semantic
  humanoid skeleton, never a target skeleton's joint order. Retarget is a
  separate, later step (motion policy §4.2, §4.3).
- **The dependency arrow points at the core, never at an adapter.** Mocopi and
  ARDY are leaves.

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

## 5. Artifact naming and versioning

Per-bundle artifacts plus one aggregate:

```text
vrmSchema-<version>-<target>.tar.zst
usdVrmFileFormat-<version>-<target>.tar.zst
usdVrmPackageResolver-<version>-<target>.tar.zst
usdVrmaFileFormat-<version>-<target>.tar.zst   (when it exists)
execMotion-<version>-<target>.tar.zst          (when it exists)
execVrm-<version>-<target>.tar.zst             (when it exists)
usd-vrm-plugins-<version>-<target>-plugin-product.tar.zst (aggregate)
```

Adapter bundles, if they are ever published, are named
`vrmAdapter<Name>-<version>-<target>.tar.zst` and are **never** part of the
aggregate: the aggregate stays free of product names (motion policy §8.1).

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
| 5 | workspace packaging (per-bundle + aggregate) | not started |
| 6 | motion library bootstrap (`motionCore`, `motionRuntime`, `vrmRetarget`) | not started |
| 7 | `usdVrmaFileFormat` bundle bootstrap | not started |
| 8 | `execMotion` + `execVrm` bundle bootstrap | not started |

> **Phase 6 was renumbered on 2026-07-18.** It previously read "`execVrm`
> (LookAt first)" — a single phase covering the whole runtime layer. The motion
> policy splits that into three plain libraries and two bundles, so the runtime
> bootstrap is now Workspace Phase 8 and the LookAt-first ordering is retired
> (the retarget core comes first). Documents citing "Workspace Phase 6 =
> `execVrm`" predate this.

Each of Phases 6–8 establishes a boundary only: manifest, CMake package export,
standalone build, discovery test, packaging. Behavior lands inside those
boundaries as **Motion Phase A–H** — the two sequences are not the same
milestone, exactly as Workspace Phase 6 and Product P4 were not.

Scaffolds for new bundles start from the ost template catalog
(`ost plugin new usd-schema --template usd-schema-cpp`,
`ost plugin new usd-package-resolver`) rather than hand-rolled skeletons.

> **Gate status.** This document called for `ost plugin test --workspace` to be
> a required PR-lane gate from Phase 1 on. That has not happened: the generated
> PR lane runs `ost plugin test <bundle>` per cell (nine cells: three bundles ×
> three OS) and no lane runs the workspace graph validation. The dependency
> directions in §2 are enforced today by the per-bundle binary link checks and
> by `vrmContainer`'s boundary check — not by the graph gate. Wiring it in is
> tracked in the [roadmap](../roadmap/current.md).
