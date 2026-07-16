# Capability matrix

Per-feature support status for the `usdVrmFileFormat` importer, as of the current tree.
This table describes **what is implemented today**, not aspirations — it is kept
in sync with the code and the [roadmap](ROADMAP.md). Simulation/evaluation of
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
| PBR material | supported | supported | `UsdShadeMaterial` / `UsdPreviewSurface` | Yes |
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
  layer** (`execVrm`, roadmap P4), never run by this importer.
- MToon **shading** realization (beyond the PreviewSurface approximation) is
  roadmap P5.

This separation is deliberate: the importer stays a pure, deterministic
data-authoring step so downstream runtimes can be swapped without changing it.

## See also

- [`ROADMAP.md`](ROADMAP.md) — phased status (P0–P6).
- [`SUPPORTED_CONFIGURATIONS.md`](SUPPORTED_CONFIGURATIONS.md) — platforms, OpenUSD, build.
- [`../plugins/vrmSchema/docs/SCHEMA_CONTRACT.md`](../plugins/vrmSchema/docs/SCHEMA_CONTRACT.md) — schema contract v1.
- [`../plugins/usdVrmFileFormat/docs/DIAGNOSTICS.md`](../plugins/usdVrmFileFormat/docs/DIAGNOSTICS.md) — diagnostic codes.
