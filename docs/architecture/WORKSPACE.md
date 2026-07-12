# Workspace contract

This document is the binding contract for splitting `usdVrm` into an
OpenStrata plugin workspace. It fixes bundle identities, dependency
directions, artifact naming, and the invariants every migration PR must
preserve. Structural changes that contradict this document require changing
this document first, in its own PR.

Status: contract adopted; migration not started (see §8).

## 1. Bundles and libraries

| Identity | Kind | Role |
| --- | --- | --- |
| `vrmSchema` | plugin bundle (`usd-schema`) | VRM schema APIs (`VrmHumanoidAPI`, `VrmExpressionAPI`, `VrmLookAtAPI`, `VrmSpringBoneAPI`, `VrmColliderAPI`, `VrmConstraintAPI`), schema tokens, `schema.usda` + generated sources, schema contract version |
| `usdVrmFileFormat` | plugin bundle (`usd-fileformat`) | `.vrm` `SdfFileFormat`, VRM 0.x / 1.0 detection, glTF/VRM parsing, canonical model (private), USD authoring (geometry, materials, skeleton, animation, schema application), import diagnostics |
| `usdVrmPackageResolver` | plugin bundle (`usd-package-resolver`) | `avatar.vrm[images/...]` package path resolution, embedded resource byte access, malformed/truncated/out-of-range rejection |
| `execVrm` | plugin bundle (deferred) | OpenExec evaluators (LookAt first), driven by the schema contract only |
| `vrmContainer` | plain CMake library (`libs/`) | GLB header/chunk parsing, buffer-view access, byte-range validation, immutable byte views — shared by file format and resolver |
| `vrmCore` | plain CMake library (deferred) | canonical model, only if a second consumer beyond the importer appears |
| `usdVrm` | aggregate product name | retired as a bundle id; names the aggregate package composed of the bundles above |

Shared code is never a plugin bundle: `vrmContainer` has no plugin
registration, no `plugInfo.json`, and no OpenUSD types in its public API.

## 2. Dependency directions

Allowed:

```text
usdVrmFileFormat      -> vrmSchema
usdVrmFileFormat      -> vrmContainer
usdVrmPackageResolver -> vrmContainer
execVrm               -> vrmSchema
```

Forbidden (non-exhaustive; anything not allowed above is forbidden):

```text
vrmSchema             -> any other bundle or library
usdVrmPackageResolver -> usdVrmFileFormat, vrmSchema
usdVrmFileFormat      -> usdVrmPackageResolver (link-time; resolver is a
                         runtime bundle dependency only)
execVrm               -> usdVrmFileFormat private API, importer canonical model
any cycle, including self-cycles
```

Enforcement: `ost plugin test --workspace` (ost >= 0.14.0) validates the
bundle graph declared via `requires.bundles` before running any bundle's
verification, with stable `WORKSPACE_*` issue codes (dependency missing,
version mismatch, contract mismatch, direction forbidden, cycle) and exit 5
on violation. Bundle manifests are the source of truth for these edges.

Known enforcement gap: ost 0.14.0 does not validate plain-library
dependencies (`requires.libraries` is ignored), so `vrmContainer`'s edges are
enforced by repo CI instead: a structural check that `vrmContainer` contains
no plugin registration and links no `pxr` plugin targets, and link checks
(`dumpbin`/`nm`) that the resolver does not import file-format symbols.

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
execVrm-<version>-<target>.tar.zst          (when it exists)
usdVrmPlugins-<version>-<target>.tar.zst    (aggregate)
```

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

## 8. Phase status

| Phase | Deliverable | Status |
| --- | --- | --- |
| 0 | baseline snapshots + regression criteria | not started |
| 1 | `vrmSchema` bundle split | not started |
| 2 | `vrmContainer` extraction | not started |
| 3 | `usdVrmPackageResolver` bundle split | not started |
| 4 | `usdVrmFileFormat` purification/rename | not started |
| 5 | workspace packaging (per-bundle + aggregate) | not started |
| 6 | `execVrm` (LookAt first) | not started |

Scaffolds for new bundles start from the ost template catalog
(`ost plugin new usd-schema --template usd-schema-cpp`,
`ost plugin new usd-package-resolver`) rather than hand-rolled skeletons, and
`ost plugin test --workspace` becomes a required PR-lane gate from Phase 1 on.
