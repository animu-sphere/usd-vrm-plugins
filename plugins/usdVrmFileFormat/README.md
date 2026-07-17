# usdVrmFileFormat — OpenUSD `vrm` file-format plugin

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
../../libs/vrmContainer/        GLB header/chunk + immutable byte-view library
src/
  UsdVrmFileFormat.{h,cpp}      SdfFileFormat entry point (CanRead/Read/WriteToString)
  io/                           VrmDocumentReader interface + cgltf implementation
  model/VrmCanonicalDocument.h  parser-independent intermediate model (0.x/1.0 normalized)
  usd/UsdVrmAuthorer.{h,cpp}    canonical model -> USD scene description
  resolver/                     ArPackageResolver for .vrm-embedded texture assets
  util/                         path sanitize/uniquify, glTF->USD transform conversion
plugin/resources/usdVrmFileFormat/plugInfo.json.in     target-suffix template for USD plugin registration
plugin/resources/usdVrmFileFormat/plugInfo.json        generated/inspectable USD plugin registration
tools/                          generate_fixtures.py, vrm_fixture_lib.py, inspect_vrm.py,
                                package_vrm.py
tests/                          python smoke tests + generated fixtures (minimal, vrm0_minimal, multiskin_ibm, names, materials, badext) + invalid.vrm
```

## Architecture

```
.vrm bytes ─▶ vrmContainer ─▶ CgltfVrmDocumentReader ─▶ VrmCanonicalDocument ─▶ UsdVrmAuthorer ─▶ USDA
               (GLB + byte     (cgltf + pxr/base/js)       (no glTF/USD types       (UsdGeom/
                ranges; no USD)                              across this seam)         UsdSkel)
```

* **vrmContainer** validates untrusted GLB headers/chunks and buffer-view ranges,
  exposes immutable non-owning byte views, and owns the stable content-addressed
  embedded-resource naming shared with the package resolver. Its public API has
  no OpenUSD types or plugin registration.
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
  typed, compiled **`VrmHumanoidAPI`** applied schema owned by the sibling
  [`vrmSchema` bundle](../vrmSchema/): standard VRM bones are schema builtins,
  non-standard / VRM-0.x-only bones fall back to custom attributes (still
  lossless).
* **Typed schemas across the rig:** every control prim carries a compiled
  applied schema — `VrmExpressionAPI` (Expressions), `VrmLookAtAPI` (LookAt),
  `VrmSpringBoneAPI` + `VrmColliderAPI` (SecondaryMotion), and `VrmConstraintAPI`
  (Constraints). All are owned, generated, and registered by the `vrmSchema`
  bundle (workspace Phase 1 split); this importer links its typed API via
  `find_package(vrmSchema)`. The public **schema contract v1**, versioning
  policy, raw-to-typed correspondence table, and validator rules are in
  [`../vrmSchema/docs/SCHEMA_CONTRACT.md`](../vrmSchema/docs/SCHEMA_CONTRACT.md).
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
source `.vrm`, e.g. `avatar.vrm[images/<hash>.png]`. The sibling
[`usdVrmPackageResolver`](../usdVrmPackageResolver/) bundle serves those bytes
directly to OpenUSD/Hio, so normal imports do not depend on an OS temp
extraction cache. It is a **runtime** dependency: this file-format plugin
authors the package paths but does not link the resolver (WORKSPACE.md §2), so a
session must also load `usdVrmPackageResolver` for embedded textures to resolve.
Image format is detected by sniffing the magic bytes (some VRM exporters drop
the glTF `mimeType`).

For portable handoff, run the package tool inside a usdVrm-enabled OpenUSD
environment. It opens the source stage, copies texture files into a package-local
`textures/` directory, rewrites `UsdUVTexture.inputs:file` to relative asset
paths, exports `asset.usda`, and writes `package_report.json` as the asset
inventory:

```sh
ost plugin run plugins/usdVrmFileFormat -- python plugins/usdVrmFileFormat/tools/package_vrm.py avatar.vrm out/avatar
```

## Build & verify

Requires `ost` 0.16+ (from the repo root):

```sh
ost plugin build plugins/usdVrmFileFormat        # build libUsdVrmFileFormat into lib/
ost plugin test  plugins/usdVrmFileFormat        # L0-L6 verification pyramid
python plugins/usdVrmFileFormat/tools/generate_fixtures.py      # regenerate the test fixtures
```

### Viewing a `.vrm` in usdview

Opening an avatar in usdview needs the importer's **runtime** siblings on the
session plugin path — the typed schemas it applies (`vrmSchema`) and, for
embedded textures, the package resolver (`usdVrmPackageResolver`). Compose them
with `--with`:

```sh
ost plugin view plugins/usdVrmFileFormat /path/to/Avatar.vrm \
    --with plugins/vrmSchema --with plugins/usdVrmPackageResolver
```

`ost plugin view` / `test-view` load only the bundles named with `--with` — unlike
`build` / `test` / `run`, they do **not** auto-compose the manifest's
`requires.bundles` closure, so a bare `ost plugin view plugins/usdVrmFileFormat <avatar>`
opens without `vrmSchema` and the importer's schema apply fails inside
`Sdf.Layer.FindOrOpen` (on a non-UTF-8 locale usdview then masks it with a
secondary `UnicodeDecodeError`). Note also there is no `--` before the fixture:
the syntax is `ost plugin view <bundle> <fixture>`, not `run`'s `-- <cmd>`.

### The typed schema

The typed `Vrm*API` classes moved to the sibling
[`plugins/vrmSchema`](../vrmSchema/) bundle in the workspace Phase 1 split
(see [`docs/architecture/WORKSPACE.md`](../../docs/architecture/WORKSPACE.md)).
This importer consumes them strictly as an installed package
(`find_package(vrmSchema CONFIG REQUIRED)` + `vrmSchema::vrmSchema`); ost 0.15
builds/installs the manifest dependency closure before configuring this bundle.
Schema regeneration lives with the schema:
`plugins/vrmSchema/tools/generate_schema.py`.
