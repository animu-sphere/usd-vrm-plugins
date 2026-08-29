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
  layer** (`execVrm`, Product P4), never run by this importer. `execVrm` does not
  exist yet; the release it lands in is not fixed — it follows the packaging,
  tracker, recorded-source and producer-contract tracks
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

**The `.vrma` motion layer ships as of v0.5.0**, the first live-input adapter as
of v0.6.0, and as of v0.7.0 both a second live adapter and a recorded-file path.
Its own status:

| Component | Since | Status |
| --- | --- | --- |
| `usdVrmaFileFormat` | v0.3.0 | GLB/glTF animation read, humanoid rotation, hips translation, canonical `HumanoidSkeleton`, `UsdSkelAnimation`, time range, provenance (Motion Phase B) |
| `usdVrmaFileFormat` | v0.7.0 | Expression animation read: declared expressions become `/Animation/Expressions/<name>` prims carrying a time-sampled weight, unexpanded and unclamped (Motion Phase G, first half) |
| `motionCore` | v0.3.0 | Vendor-neutral pose/animation types (Motion Phase A contract) |
| `motionRuntime` | v0.4.0 | `PoseBuffer`, interpolation/extrapolation, resample, filter, blend |
| `motionRuntime` | v0.5.0 | `IMotionSource` / `ClipSource` / `LiveCaptureSource`, the `motion-capture-trace` format, `ReplaySender`, `CaptureRecorder` (Motion Phase D) |
| `vrmRetarget` | v0.4.0 | Humanoid map, rest-pose correction, pose retargeter, root-motion policy |
| `motion_retarget` | v0.4.0 | CLI: retargets a clip onto an avatar and binds `skel:animationSource` (Motion Phase C) |
| `motion_capture` | v0.5.0 | CLI: replays a recorded capture session into a semantic humanoid clip the retarget tool consumes unchanged (Motion Phase D) |
| `vrmAdapterVmc` | v0.6.0 | VMC Protocol input: OSC and VMC decode, frame assembly, Unity `HumanBodyBones` → `motion::HumanBone` mapping, `LiveCaptureSource` bridge, UDP receiver |
| `vmc_record` | v0.6.0 | CLI: records a bounded live VMC session to a `vmc-packet-capture` file, or inspects one, with a decode report; `--export-trace` writes what the adapter delivered as a `motion-capture-trace`, which is the adapter's whole hand-off to the product's tools |
| `vrmAdapterMocopi` | v0.7.0 | Native UDP input for one capture product: bounded receiver, packet capture, decoder for an unpublished wire grammar, joint map and basis change, frame assembly with restart detection, `LiveCaptureSource` bridge, and `BodyPlacementPolicy` composing `RootMotion` from a hips-only translating rig |
| `mocopi_record` | v0.7.0 | CLI: records a bounded live mocopi session or inspects a capture; `--export-trace` (from `--inspect` only) writes the same `motion-capture-trace` the product's tools replay unchanged, and reports the hips travel a path carries |
| `motionSource` | v0.7.0 | Format-neutral source rig and animation in the source's own angle order and unit, provenance, the producer-profile contract, and one declared crossing into canonical humanoid motion |
| `motionBvh` | v0.7.0 | BVH syntax, extraction and a frozen diagnostic set — no producer semantics and no default profile |
| `motion_bvh_inspect` · `motion_bvh_convert` | v0.7.0 | CLIs: report what a BVH file contains, and convert one to the avatar-independent semantic clip under an explicitly named producer profile |

**One row of the table above has met real hardware, and the rest have not.** A
mocopi device drove `vrmAdapterMocopi` end to end on 2026-08-15 — five sessions,
the first decoding with zero diagnostics — and one of them reached a released
avatar through the unchanged product tools. Those sessions are committed as
measured manifests with **no bytes**, since a session is a real person's motion.
Everything else behind this table is still generated: the
[motion traces](../../libs/motionRuntime/tests/corpus/README.md) are closed-form
maths, and the VMC captures reproduce the protocol's shapes. That is deliberate —
a corpus recorded from a commercial SDK could not be redistributed and CI could
not run it — but it bounds what the table claims. **No VMC sender application or
relay has been recorded**, which is why the cross-source comparison covers two
paths of three ([report 01](../reports/motion/01-2026-08-15-mocopi-cross-source.md))
and why the VMC half of the root/hips decision is still open.

The one exception is not in that table because it produces no motion: `motionBvh`
reads a **real** producer export, committed at
[`tests/corpus/recorded/`](../../libs/motionBvh/tests/corpus/recorded/) since
2026-08-04. It is a file rather than a session — a parser meeting real bytes,
which is a different claim from a runtime meeting a real device, and it is the
only one of the two this repository can currently make.

Protocol and SDK decode belong under `adapters/` (motion policy §8.1).
`vrmAdapterMocopi` — the first native capture-device adapter — shipped in v0.7.0
and has been driven by a device; what that release did **not** close is further
operator evidence, not code ([current.md](../roadmap/current.md)). A **third**
live adapter is in progress behind it: VRChat OSC Trackers input over a
protocol-neutral OSC decoder
([the OSC track](../roadmap/osc-and-vrchat-trackers.md), and the
[roadmap status table](../roadmap/README.md#status-at-a-glance) for the version).
It is the first input here that carries **tracker observations** rather than
humanoid bone transforms — pre-IK data, where a tracker index is not a body role
— so nothing in it maps onto a row of this table until a solve boundary exists.

`vrmAdapterVrchatOsc` exists as of 2026-08-25 and **decodes nothing**: it is a
scaffold, a frozen diagnostic set, and `vrchat_osc_record`, which turns a live
sender into a packet capture and reports the datagram envelope. That distinction
is the one this table is for. A recorder is not a capability of the pipeline —
no row moves — and the reason the decoder is not here yet is deliberate rather
than pending: VRChat's OSC surface is published, and a specification says what a
*receiver* must accept where what a sender sends is a measurement, so the
inventory is taken from real datagrams before a decoder is designed from it.

**One recorded motion file format other than `.vrma` becomes motion, as of
v0.7.0.** BVH — the format most capture applications export — is a generic
pipeline (`motionBvh` + `motionSource`) whose producer semantics live in
declarative profiles rather than in the parser
([plan](../roadmap/recorded-motion-sources.md)). A joint's name, unit and axes
stay uninterpreted until a profile says what they mean, so
`motion_bvh_convert` **refuses every file until one is named** and there is no
default profile anywhere. Two producers' profiles ship in
[`profiles/motion/`](../../profiles/motion/); a profile the repository does not
ship works the same way, by path. FBX is not planned; the layering exists so
that a second reader can be added without changing anything above it.

Not yet in that layer: look-at animation, motion generation, OpenExec
evaluation, blending beyond the primitive, IK, and foot locking. Expression
animation is read from a `.vrma` clip and carried on the pose, but it is not
*resolved*: turning a named weight into the morph targets of a particular
avatar is `ExpressionResolve`, still ahead. See
[MOTION_ARCHITECTURE_POLICY.md](../design/MOTION_ARCHITECTURE_POLICY.md) and the
Motion Phase ladder in the [backlog](../roadmap/backlog.md).

## See also

- [`ROADMAP.md`](../roadmap/) — phased status (Product P0–P6, Workspace Phase
  0–8, Motion Phase A–H).
- [`SUPPORTED_CONFIGURATIONS.md`](SUPPORTED_CONFIGURATIONS.md) — platforms, OpenUSD, build.
- [`../plugins/vrmSchema/docs/SCHEMA_CONTRACT.md`](../../plugins/vrmSchema/docs/SCHEMA_CONTRACT.md) — schema contract v1.
- [`../plugins/usdVrmFileFormat/docs/DIAGNOSTICS.md`](../../plugins/usdVrmFileFormat/docs/DIAGNOSTICS.md) — diagnostic codes.
