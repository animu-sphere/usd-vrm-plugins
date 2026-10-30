# Capability matrix

Per-feature support status for the `usdVrmFileFormat` importer, as of the current tree.
This table describes **what is implemented today**, not aspirations — it is kept
in sync with the code and the [roadmap](../roadmap/). Simulation/evaluation of
runtime features is explicitly out of the importer (see the
import/evaluation/simulation boundary below).

## Status vocabulary

Aligned with the design policy's §11 fidelity vocabulary:

| Status | Meaning |
| --- | --- |
| **supported** | Interpreted and authored as typed USD; validated. |
| **approximated** | A portable USD stand-in is authored; source fidelity is not fully reproduced. |
| **preserved** | Kept losslessly as raw data (`customData`), not typed-interpreted. |
| **unsupported** | Not authored; skipped (usually with a coded diagnostic). |
| **invalid** | Rejected / reported as a contract violation (see diagnostics). |
| **repaired** | Normalized/repaired from a non-canonical source form. |

## Feature matrix

| Feature | VRM 0.x | VRM 1.0 | USD output | Validated |
| --- | :--- | :--- | --- | :---: |
| Mesh (points/normals/UV/indices) | supported | supported | `UsdGeomMesh` | Yes |
| Non-skinned node transform | supported | supported | `xformOp:transform` | Yes |
| Skin / skinning | supported | supported | `UsdSkelBindingAPI` | Yes |
| Skeleton (unified across skins) | supported | supported | `UsdSkelSkeleton` (bind from IBM) | Yes |
| Humanoid mapping | supported | supported | `VrmHumanoidAPI` | Yes |
| Expression / BlendShape | supported | supported | `VrmExpressionAPI` + `UsdSkelBlendShape` | Yes |
| LookAt (data) | supported | supported | `VrmLookAtAPI` | Yes |
| SpringBone / SecondaryAnimation (data) | supported | supported | `VrmSpringBoneAPI` + `VrmColliderAPI` | Yes |
| Node constraint (data) | preserved¹ | supported | `VrmConstraintAPI` | Yes |
| PBR material | supported | supported | `UsdShadeMaterial` + typed `VrmMaterialAPI`, realized as a `/preview` `UsdShadeNodeGraph` (`UsdPreviewSurface`) and a `/mtlx` one (MaterialX `gltf_pbr`), both generated from the typed attributes | Yes |
| Textures (base/MR/normal/emissive/occlusion) | supported | supported | `VrmTextureInfoAPI`; `UsdUVTexture` / MaterialX `image` + wrap modes | Yes |
| `KHR_texture_transform` | supported | supported | `VrmTextureInfoAPI` `transform:*`; `UsdTransform2d` / MaterialX `place2d` | Yes |
| MToon shading | approximated | approximated | typed `VrmMToonAPI` / `VrmTextureInfoAPI` (0.x normalized into the 1.0 model) + `vrm:mtoon:raw`; both realizations generated from the typed schemas; `/mtlx` approximates MToon's toon shading, shade, MatCap and parametric rim under a headlight, `/preview` draws base colour, alpha and emission only; outline, GI equalization and UV animation are typed but not realized | Partial |
| Skeletal animation (joint TRS) | supported | supported | `UsdSkelAnimation` | Yes |
| Morph-weight (blend-shape) animation | unsupported | unsupported | — | n/a |
| Front-direction normalization | repaired | supported | root transform + `customData` provenance | Yes |
| VRM meta / spec version | preserved | preserved | `customData.vrm:meta` / `:specVersion` | Yes |
| Raw VRM/VRMC extension block | preserved | preserved | `customData.vrm:rawExtension` | Yes |
| Compressed textures (e.g. KTX2) | unsupported | unsupported | skipped (`VRM101`/`VRM102`) | n/a |

¹ VRM 0.x has no node-constraint concept; any such data present is preserved raw.

## Import / evaluation / simulation boundary

The runtime's [scoped headless host evidence (2026-10-29)](https://github.com/animu-sphere/usd-avatar-runtime/blob/main/docs/reference/CAPABILITY_MATRIX.md#vrm-fixed-tick-session-and-host-evidence-2026-10-29)
records clip-driven VRM 0.x/1.0 avatars against installed `vrmRig`, including
fixed ticks, owner parity, reset replay and retained unload. This is evidence
produced by `usd-avatar-runtime`; resident rendering, Hydra/direct parity and
scaled spring rigs remain outside that measurement.

Spring declarations in contract v2 use `vrm:rootJoints` for VRM 0.x subtree
roots and `vrm:joints` for VRM 1.0 ordered chains. The fixture and corpus smoke
tests assert this distinction; the validator checks exclusive authorship and
parameter-array lengths. `vrmRig::SpringBoneSolver` evaluates the declarations
over plain rigid values; its adapter and live-avatar evidence remain in the
runtime's scope. VRM 0.x collider offsets are normalized from the source's
left-handed coordinates into glTF joint-local coordinates by negating Z once,
before any model front-direction bake. The smoke regression covers positive,
negative and omitted Z, a rotated attachment, raw-source retention and unchanged
VRM 1.0 values. The unit and reference-comparison suites pass on hosted
Windows, Linux and macOS arm64, as does the installed consumer which configures,
resets and steps the solver ([source CI](https://github.com/animu-sphere/usd-vrm-plugins/actions/runs/40624536250),
[package consumer](https://github.com/animu-sphere/usd-vrm-plugins/actions/runs/40624536217),
`14efdec`, 2026-10-28).

The `usdVrmFileFormat` file-format plugin **authors data only**. It never evaluates or
simulates:

- **LookAt**, **node constraints**, and **spring bones** are authored as typed
  schema data on the stage. Their runtime evaluation/simulation is a **separate
  layer**, never run by this importer: `execVrm` for look-at and constraints,
  and `vrmRig`, stepped by a runtime outside this repository, for spring bones
  ([spring-bone track](../roadmap/springbone-track.md)). `execVrm` evaluates
  retargeting only — the target rig, the humanoid map, rest-pose correction,
  one sample's retarget and its diagnostics — and none of these three yet;
  they follow on [the `ExecIr` track](../roadmap/execir-track.md).
- MToon **shading** is approximated in `/mtlx` under a headlight (Product P5
  Step 6, [material track](../roadmap/material-track.md)); a skinned avatar
  needs `USDSKELIMAGING_ENABLE_NORMAL_COMPUTATIONS=1` in Storm or the shading
  shows every triangle. The full MToon renderer — scene lights, outline, UV
  animation — is `hydra-toon`'s, another repository.

This separation is deliberate: the importer stays a pure, deterministic
data-authoring step so downstream runtimes can be swapped without changing it.

## Motion on a VRM rig

The feature matrix above covers the `.vrm` importer. How VRM and VRMA use
motion is implemented by the components below; the contract is
[VRM_MOTION_POLICY.md](../design/VRM_MOTION_POLICY.md).

| Component | Implemented |
| --- | --- |
| `usdVrmaFileFormat` | A `VRMC_vrm_animation` 1.0 clip → an avatar-independent semantic clip: humanoid rotations and the hips translation from the first animation (`LINEAR`, `STEP`; `CUBICSPLINE` linearised with a warning), 30 time codes per second, `vrma` provenance; expressions as `/Animation/Expressions/<name>` weights, unclamped and unexpanded; look-at as `/Animation/LookAt`, a placed target point and the head-bone offset. Scale channels and translations other than the hips are ignored with a warning |
| `vrmRig` | VRM 1.0's required bones; `ExpressionResolver` (named weights onto one avatar's morph-target and material-colour binds, joined on `vrm:expressionName`, with `overrideBlink` / `overrideLookAt` / `overrideMouth` arbitration); `LookAtEvaluator` (a target point onto a `bone`- or `expression`-type rig's own range maps); `SpringBoneSolver` (VRM 1.0 chains, VRM 0.x root subtrees and virtual tails, sphere/capsule colliders, center-space history and reset, returning joint-local rotations; rigid metre inputs, fixed rest translations; [API conventions](../../libs/vrmRig/README.md#spring-bones)) |
| `motion_retarget` | Bakes a semantic clip onto a VRM rig as `UsdSkelAnimation` bound through `skel:animationSource`; authors expression `blendShapeWeights` and gaze; carries each joint's rest scale; frozen exit codes; `--load-report`, `--build-info`, `--version`. authors expression **material colours as time samples on the Material's canonical inputs** (the schema contract's slot table), which `/mtlx` and `/preview` read |
| `execVrm` | OpenExec computations over the applied `VrmHumanoidAPI`: target skeleton, humanoid map, rest-pose correction, one sample's retarget, joint-local transforms, and the retarget's diagnostics as values — equal to `motion_retarget`'s bake bit for bit on the parity cases |

Not implemented here: expression and look-at as OpenExec computations, an
`ExecIr` rig, and realtime skinned display through the exec scene index
([the `ExecIr` track](../roadmap/execir-track.md)).

**Consumed, and not stated here.** The generic motion this product links —
values, sampling, filtering, recording, retargeting, BVH, the OpenUSD motion
mapping and `execMotion` — has its status in
[`usd-motion-plugins`' capability matrix](https://github.com/animu-sphere/usd-motion-plugins/blob/main/docs/reference/CAPABILITY_MATRIX.md);
live and device input in
[`motion-connectors`' capability matrix](https://github.com/animu-sphere/motion-connectors/blob/main/docs/reference/CAPABILITY_MATRIX.md).
What this product shipped of either before the split is in its
[release records](../releases/).

## Native USD export

`vrm_export` writes `.usda`, `.usdc` and `.usdz` from imported VRM 0.x/1.0
assets, localizes embedded and external textures, optionally flattens and
validates the output, and emits a portable JSON dependency inventory with
`--dependency-report` ([report contract](../design/VRM_EXPORT_POLICY.md#81-dependency-report)).
The end-to-end suite reopens every output with no VRM plugin registered and
compares the inventory with the actual localized files. CLI usage is in the
[tool README](../../tools/vrmExport/README.md); open work is in the
[export track](../roadmap/export-track.md).
Failure diagnostics identify unreadable dependencies' authored and anchored
paths and referring USD attributes, give destination recovery checks, and
list all unresolved dependencies found by validation. Exit codes remain the
[export policy's failure categories](../design/VRM_EXPORT_POLICY.md#53-exit-codes).

The installed product's exporter is verified on Windows, Linux and macOS
arm64: two textured VRM 0.x/1.0 inputs export to `.usdz`, reopen with no VRM
plugin registered, and refuse export when the product's importer registration
is removed ([2026-10-29 release dry run](../reports/2026-10-29-export-release-dry-run.md)).

## See also

- [roadmap/](../roadmap/) — incomplete work.
- [`SUPPORTED_CONFIGURATIONS.md`](SUPPORTED_CONFIGURATIONS.md) — platforms, OpenUSD, build.
- [`../plugins/vrmSchema/docs/SCHEMA_CONTRACT.md`](../../plugins/vrmSchema/docs/SCHEMA_CONTRACT.md) — schema contract v2.
- [`../plugins/usdVrmFileFormat/docs/DIAGNOSTICS.md`](../../plugins/usdVrmFileFormat/docs/DIAGNOSTICS.md) — diagnostic codes.
