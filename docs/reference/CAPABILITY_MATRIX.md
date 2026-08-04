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
  layer** (`execVrm`, Product P4), never run by this importer. `execVrm` does not
  exist yet; it is planned for v0.8.0
  ([the OpenExec plan](../roadmap/openexec-foundation.md), and the
  [roadmap status table](../roadmap/README.md#status-at-a-glance) for the
  version).
- MToon **shading** realization (beyond the PreviewSurface approximation) is
  Product P5.

This separation is deliberate: the importer stays a pure, deterministic
data-authoring step so downstream runtimes can be swapped without changing it.

## Not covered by this matrix

This table covers the `.vrm` importer only. Skeletal animation *embedded in a
`.vrm`* is listed above; a standalone reusable `.vrma` clip is a different thing
and is handled by a different bundle.

**The `.vrma` motion layer ships as of v0.5.0**, and the first live-input adapter
as of v0.6.0. Its own status:

| Component | Since | Status |
| --- | --- | --- |
| `usdVrmaFileFormat` | v0.3.0 | GLB/glTF animation read, humanoid rotation, hips translation, canonical `HumanoidSkeleton`, `UsdSkelAnimation`, time range, provenance (Motion Phase B) |
| `motionCore` | v0.3.0 | Vendor-neutral pose/animation types (Motion Phase A contract) |
| `motionRuntime` | v0.4.0 | `PoseBuffer`, interpolation/extrapolation, resample, filter, blend |
| `motionRuntime` | v0.5.0 | `IMotionSource` / `ClipSource` / `LiveCaptureSource`, the `motion-capture-trace` format, `ReplaySender`, `CaptureRecorder` (Motion Phase D) |
| `vrmRetarget` | v0.4.0 | Humanoid map, rest-pose correction, pose retargeter, root-motion policy |
| `motion_retarget` | v0.4.0 | CLI: retargets a clip onto an avatar and binds `skel:animationSource` (Motion Phase C) |
| `motion_capture` | v0.5.0 | CLI: replays a recorded capture session into a semantic humanoid clip the retarget tool consumes unchanged (Motion Phase D) |
| `vrmAdapterVmc` | v0.6.0 | VMC Protocol input: OSC and VMC decode, frame assembly, Unity `HumanBodyBones` → `motion::HumanBone` mapping, `LiveCaptureSource` bridge, UDP receiver |
| `vmc_record` | v0.6.0 | CLI: records a bounded live VMC session to a `vmc-packet-capture` file, or inspects one, with a decode report; `--export-trace` writes what the adapter delivered as a `motion-capture-trace`, which is the adapter's whole hand-off to the product's tools |

**Nothing in the table above has been validated against a real sender or a real
capture rig.** Every corpus behind it is generated: the
[motion traces](../../libs/motionRuntime/tests/corpus/README.md) are closed-form
maths, and the VMC captures reproduce the protocol's shapes. That is deliberate —
a corpus recorded from a commercial SDK could not be redistributed and CI could
not run it — but it bounds what the table claims. Recording real sessions
from a device and from several sender applications, with the redistributable ones
committed and the rest kept as measured manifests, is v0.7.0
([adapter plan](../roadmap/adapters-mocopi-vmc-ardy.md)).

The one exception is not in that table because it produces no motion: `motionBvh`
reads a **real** producer export, committed at
[`tests/corpus/recorded/`](../../libs/motionBvh/tests/corpus/recorded/) since
2026-08-04. It is a file rather than a session — a parser meeting real bytes,
which is a different claim from a runtime meeting a real device, and it is the
only one of the two this repository can currently make.

There is no native capture-device adapter yet. Protocol and SDK decode belong
under `adapters/` (motion policy §8.1) and `vrmAdapterMocopi` is the next one.

**No recorded motion file format other than `.vrma` becomes motion.** BVH — the
format most capture applications export — is v0.7.0, as a generic pipeline
(`motionBvh` + `motionSource`) whose producer semantics live in declarative
profiles rather than in the parser
([plan](../roadmap/recorded-motion-sources.md)). Its **syntax** half is
implemented: `motionBvh` reads a BVH document and `motion_bvh_inspect` reports
what one contains. Neither produces a pose — a joint's name, unit and axes stay
uninterpreted until a profile says what they mean, so nothing yet reaches
`motion::HumanoidAnimation` from a file. FBX is not planned; the layering exists
so that a second reader can be added without changing anything above it.

Not yet in that layer: expression and look-at animation, motion generation,
OpenExec evaluation, blending beyond the primitive, IK, and foot locking. See
[MOTION_ARCHITECTURE_POLICY.md](../design/MOTION_ARCHITECTURE_POLICY.md) and the
Motion Phase ladder in the [backlog](../roadmap/backlog.md).

## See also

- [`ROADMAP.md`](../roadmap/) — phased status (Product P0–P6, Workspace Phase
  0–8, Motion Phase A–H).
- [`SUPPORTED_CONFIGURATIONS.md`](SUPPORTED_CONFIGURATIONS.md) — platforms, OpenUSD, build.
- [`../plugins/vrmSchema/docs/SCHEMA_CONTRACT.md`](../../plugins/vrmSchema/docs/SCHEMA_CONTRACT.md) — schema contract v1.
- [`../plugins/usdVrmFileFormat/docs/DIAGNOSTICS.md`](../../plugins/usdVrmFileFormat/docs/DIAGNOSTICS.md) — diagnostic codes.
