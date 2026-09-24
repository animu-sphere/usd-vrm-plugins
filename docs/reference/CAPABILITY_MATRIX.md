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
| PBR material | supported | supported | `UsdShadeMaterial` + a `/preview` `UsdShadeNodeGraph` holding the `UsdPreviewSurface` network | Yes |
| Textures (base/MR/normal/emissive/occlusion) | supported | supported | `UsdUVTexture` + wrap modes | Yes |
| `KHR_texture_transform` | supported | supported | `UsdTransform2d` | Yes |
| MToon shading | approximated | approximated | `UsdPreviewSurface` fallback + `vrm:mtoon:raw` | Partial |
| Skeletal animation (joint TRS) | supported | supported | `UsdSkelAnimation` | Yes |
| Morph-weight (blend-shape) animation | unsupported | unsupported | — | n/a |
| Front-direction normalization | repaired | supported | root transform + `customData` provenance | Yes |
| VRM meta / spec version | preserved | preserved | `customData.vrm:meta` / `:specVersion` | Yes |
| Raw VRM/VRMC extension block | preserved | preserved | `customData.vrm:rawExtension` | Yes |
| Compressed textures (e.g. KTX2) | unsupported | unsupported | skipped (`VRM101`/`VRM102`) | n/a |

¹ VRM 0.x has no node-constraint concept; any such data present is preserved raw.

## Import / evaluation / simulation boundary

The `usdVrmFileFormat` file-format plugin **authors data only**. It never evaluates or
simulates:

- **LookAt**, **node constraints**, and **spring bones** are authored as typed
  schema data on the stage. Their runtime evaluation/simulation is a **separate
  layer** (`execVrm`), never run by this importer. `execVrm` evaluates
  retargeting only — the target rig, the humanoid map, rest-pose correction,
  one sample's retarget and its diagnostics — and none of these three yet;
  they follow on [the `ExecIr` track](../roadmap/execir-track.md).
- MToon **shading** realization (beyond the PreviewSurface approximation) is
  Product P5.

This separation is deliberate: the importer stays a pure, deterministic
data-authoring step so downstream runtimes can be swapped without changing it.

## Motion on a VRM rig

The feature matrix above covers the `.vrm` importer. How VRM and VRMA use
motion is implemented by the components below; the contract is
[VRM_MOTION_POLICY.md](../design/VRM_MOTION_POLICY.md).

| Component | Implemented |
| --- | --- |
| `usdVrmaFileFormat` | A `VRMC_vrm_animation` 1.0 clip → an avatar-independent semantic clip: humanoid rotations and the hips translation from the first animation (`LINEAR`, `STEP`; `CUBICSPLINE` linearised with a warning), 30 time codes per second, `vrma` provenance; expressions as `/Animation/Expressions/<name>` weights, unclamped and unexpanded; look-at as `/Animation/LookAt`, a placed target point and the head-bone offset. Scale channels and translations other than the hips are ignored with a warning |
| `vrmRig` | VRM 1.0's required bones; `ExpressionResolver` (named weights onto one avatar's morph-target and material-colour binds, joined on `vrm:expressionName`, with `overrideBlink` / `overrideLookAt` / `overrideMouth` arbitration); `LookAtEvaluator` (a target point onto a `bone`- or `expression`-type rig's own range maps) |
| `motion_retarget` | Bakes a semantic clip onto a VRM rig as `UsdSkelAnimation` bound through `skel:animationSource`; authors expression `blendShapeWeights` and gaze; carries each joint's rest scale; frozen exit codes; `--load-report`, `--build-info`, `--version`. **Material colours are resolved and not written** — a colour slot is a material input |
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

## See also

- [roadmap/](../roadmap/) — incomplete work.
- [`SUPPORTED_CONFIGURATIONS.md`](SUPPORTED_CONFIGURATIONS.md) — platforms, OpenUSD, build.
- [`../plugins/vrmSchema/docs/SCHEMA_CONTRACT.md`](../../plugins/vrmSchema/docs/SCHEMA_CONTRACT.md) — schema contract v1.
- [`../plugins/usdVrmFileFormat/docs/DIAGNOSTICS.md`](../../plugins/usdVrmFileFormat/docs/DIAGNOSTICS.md) — diagnostic codes.
