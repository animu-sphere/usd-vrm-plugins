# usdVrm — OpenUSD `vrm` file-format plugin

Imports `.vrm` (VRM 0.x / VRM 1.0) avatars as a normalized OpenUSD stage. The
goal is not a throwaway "viewable USD" but a structure that keeps VRM semantics
trackable and reconnectable to downstream runtimes.

Scaffolded with `ost plugin new usd-fileformat usdVrm --extension vrm`, then
extended with a real importer.

## Layout

```
openstrata.plugin.yaml          bundle contract (identity, runtime range, provides, tests)
CMakeLists.txt                  builds libUsdVrmFileFormat.{dll,dylib,so} into lib/
cmake/Dependencies.cmake        fetches cgltf (pinned) at configure time
src/
  UsdVrmFileFormat.{h,cpp}      SdfFileFormat entry point (CanRead/Read/WriteToString)
  io/                           VrmDocumentReader interface + cgltf implementation
  model/VrmCanonicalDocument.h  parser-independent intermediate model (0.x/1.0 normalized)
  usd/UsdVrmAuthorer.{h,cpp}    canonical model -> USD scene description
  resolver/                     ArPackageResolver for .vrm-embedded texture assets
  schema/                       committed usdGenSchema fallback for Vrm*API schemas
  util/                         path sanitize/uniquify, glTF->USD transform conversion
schema/schema.usda              typed schema source (Vrm*API); ost 0.6+ regenerates it at build time
plugin/resources/usdVrm/plugInfo.json.in     target-suffix template for USD plugin registration
plugin/resources/usdVrm/plugInfo.json        generated/inspectable USD plugin registration
plugin/resources/usdVrm/generatedSchema.usda usdGenSchema schematics for the typed schema
tools/                          generate_fixtures.py, vrm_fixture_lib.py, inspect_vrm.py,
                                generate_schema.py, package_vrm.py
tests/                          python smoke tests + generated fixtures (minimal, vrm0_minimal, multiskin_ibm, names, materials, badext) + invalid.vrm
```

## Architecture

```
.vrm bytes ──▶ CgltfVrmDocumentReader ──▶ VrmCanonicalDocument ──▶ UsdVrmAuthorer ──▶ USDA ──▶ SdfLayer
   (GLB)        (cgltf + pxr/base/js)        (no glTF/USD types          (UsdGeom/UsdSkel/
                                              leak across this seam)       UsdShade)
```

* **Reader** owns all cgltf contact and VRM-extension JSON parsing (via USD's own
  `pxr/base/js`, so there is no external JSON dependency). The plan's nominal
  `VRM.h` dependency does not exist as a real library; its role — interpreting the
  VRM/VRMC_vrm extension blocks — is fulfilled here, which also keeps the
  third-party ABI surface to just cgltf (compiled into one TU, never exported).
* **Canonical model** absorbs VRM 0.x vs 1.0 structural differences.
* **Authorer** consumes only the canonical model and emits USDA on a worker
  thread (USD's reload path runs `Read` under an outer `SdfChangeBlock`).

## Key design decisions

* **Coordinate system:** glTF and USD are both right-handed, Y-up, metric, so
  geometry needs no axis flip. Only conversions: glTF column-major matrices →
  USD row-vector `GfMatrix4d` (a transpose, done for free by reading the array
  in order), and UV `V := 1 - V`. All of it lives in `util/TransformUtil`.
* **Front direction:** VRM 0.x avatars face **-Z**; VRM 1.0 standardized on
  **+Z**. To keep every imported avatar consistent (and matching the current
  standard), VRM 0.x is rotated 180° about the up axis at the **root** (`/Asset`)
  — a single SkelRoot-level transform, so skinning, animation, blend shapes and
  lookAt all keep working untouched. The source orientation is recorded in
  `customData` (`vrm:sourceFrontAxis`, `vrm:frontAxisNormalized`).
* **One unified skeleton:** VRM avatars are commonly split across several glTF
  skins referencing a shared joint hierarchy. The reader takes the **union** of
  all skin joints into a single `UsdSkelSkeleton` and remaps each mesh's
  `JOINTS_0` into that order. (Importing only the first skin yields a partial
  skeleton — one test avatar dropped from 128 joints / 51 humanoid bones to
  23 / 1.) Bind transforms come from each skin's **inverse bind matrices**
  (`bind = inverse(IBM)`), not the joint node world transforms.
* **Mesh placement:** skinned mesh verts live in skeleton space (the glTF node
  transform is ignored, per spec; `geomBindTransform` carries the bind
  placement). A **non-skinned** mesh instead gets its glTF node world transform
  authored as an `xformOp:transform`, so node-placed accessories don't collapse
  to the origin.
* **`/Asset` is a `SkelRoot`** when a skeleton exists (UsdSkel needs a SkelRoot
  ancestor enclosing the skinned meshes and skeleton), otherwise a plain
  `Xform`. It is always `kind=component` and the stage default prim — a
  deliberate, documented deviation from the plan's "always Xform".
* **`rig/Humanoid` is control semantics, not a bone copy:** it carries one
  resolvable `vrm:skeleton` relationship to the Skeleton prim plus a
  `vrm:humanBones:<bone>` **token attribute** per bone holding the joint path
  (joints are `Skeleton.joints` tokens, not prims, so a relationship can't target
  them directly). Never a duplicated joint hierarchy. These are formalized by a
  typed, compiled **`VrmHumanoidAPI`** applied schema (see
  `schema/schema.usda`), co-located in this same plugin: standard VRM bones are
  schema builtins, non-standard / VRM-0.x-only bones fall back to custom attributes
  (still lossless).
* **Typed schemas across the rig:** every control prim carries a compiled
  applied schema — `VrmExpressionAPI` (Expressions), `VrmLookAtAPI` (LookAt),
  `VrmSpringBoneAPI` + `VrmColliderAPI` (SecondaryMotion), and `VrmConstraintAPI`
  (Constraints). All are generated from `schema/schema.usda` and registered by the one
  plugin `plugInfo`. The public **schema contract v1**, versioning policy, raw-to-typed
  correspondence table, and validator rules are in `docs/SCHEMA_CONTRACT.md`.
* **Lossless preservation:** VRM `meta`/`specVersion`, the full raw
  VRM/VRMC_vrm extension block (`vrm:rawExtension`), and provenance
  (`sourceNodeIndex`, …) are kept in `customData` under a `vrm` namespace.

## Status

The importer build-out is complete; the forward work is doc/impl sync, a `v0.1.0`
release, Windows runtime CI, and the OpenExec runtime layer. See the project
[design & development policy](../../docs/DESIGN_POLICY.md) and the per-phase
[roadmap](../../docs/ROADMAP.md) (**P0–P6**) for live status.

Implemented: GLB read, version detection, meshes
(points/normals/UV/indices), `UsdPreviewSurface` materials with **textures**
(base color, metallic-roughness, normal, emissive, occlusion; wrap modes,
`KHR_texture_transform`), **MToon metadata** (`vrm:shaderModel` + `vrm:mtoon:raw`),
unified skeleton (bind from inverse bind matrices, topologically ordered) +
skinning binding, humanoid mapping, **blend shapes (`UsdSkelBlendShape`) and VRM
expressions** (`/Asset/rig/Expressions`, morph-target bindings), **glTF skeletal
animation** (`UsdSkelAnimation` joint TRS, bound + stage time range),
**LookAt** (`/Asset/rig/LookAt`: type + eye joint tokens + preserved curves),
**SpringBone** (`/Asset/rig/SecondaryMotion`: spring chains + collider groups,
data only — no simulation), VRM meta + raw-extension preservation, graceful
warnings on unsupported data.

VRM constraints, LookAt, and SpringBone are authored as typed schema **data** only —
their **evaluation/simulation** is a separate runtime layer (`execVrm`, roadmap P4),
never run by this file-format plugin. Not yet: morph-weight (blend-shape) animation
(only joint TRS clips are authored today), full MToon **shading** (roadmap P5; only
approximated via `UsdPreviewSurface` + `vrm:mtoon:raw` metadata today), and KTX2/WebP
image decode.

### Textures

Embedded PNG/JPEG images are referenced as package-relative assets inside the
source `.vrm`, e.g. `avatar.vrm[images/<hash>.png]`. The co-located
`UsdVrmPackageResolver` serves those bytes directly to OpenUSD/Hio, so normal
imports do not depend on an OS temp extraction cache. Image format is detected by
sniffing the magic bytes (some VRM exporters drop the glTF `mimeType`).

For portable handoff, run the package tool inside a usdVrm-enabled OpenUSD
environment. It opens the source stage, copies texture files into a package-local
`textures/` directory, rewrites `UsdUVTexture.inputs:file` to relative asset
paths, exports `asset.usda`, and writes `package_report.json` as the asset
inventory:

```sh
ost plugin run plugins/usdVrm -- python plugins/usdVrm/tools/package_vrm.py avatar.vrm out/avatar
```

## Build & verify

Requires `ost` 0.6+ (from the repo root):

```sh
ost plugin build plugins/usdVrm        # build libUsdVrmFileFormat into lib/
ost plugin test  plugins/usdVrm        # L0-L6 verification pyramid
python plugins/usdVrm/tools/generate_fixtures.py      # regenerate the test fixtures
```

### Regenerating the typed schema

The typed `Vrm*API` classes are generated from `schema/schema.usda` by OpenUSD's
**`usdGenSchema`**, which is a **Python** tool. With `ost` 0.6+, `ost plugin
build` regenerates the schema sources inside `.strata/` for the resolved runtime
and compiles them into `libUsdVrmFileFormat`.

The generated C++ (`src/schema/`) and `generatedSchema.usda` are still
**committed** as the plain-CMake fallback and as the source of the runtime
registration resources. Re-run the fallback generator when `schema/schema.usda`
changes and you need to refresh those committed files:

```sh
# Needs Python + an OpenUSD install (usdGenSchema lives there).
python plugins/usdVrm/tools/generate_schema.py --usd-root /path/to/openusd-install
```

The script runs `usdGenSchema`, copies the C++ into `src/schema/` and
`generatedSchema.usda` into the plugin resources, and merges the schema `Types`
into `plugInfo.json` / `plugInfo.json.in` beside the `SdfFileFormat` entry. It sets
`PXR_AR_DEFAULT_SEARCH_PATH` (so the `@usd/schema.usda@` sublayer that defines
`APISchemaBase` resolves) and passes `-t` the install's codegen templates, so it
works even when the interpreter's importable `pxr` is a different OpenUSD build.
