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
of v0.6.0, as of v0.7.0 both a second live adapter and a recorded-file path, and
as of v0.8.0 a third live adapter — the first tracker source — over two leaves
the first two adapters had each been writing privately.
Its own status:

| Component | Since | Status |
| --- | --- | --- |
| `usdVrmaFileFormat` | v0.3.0 | GLB/glTF animation read, humanoid rotation, hips translation, canonical `HumanoidSkeleton`, `UsdSkelAnimation`, time range, provenance (Motion Phase B) |
| `usdVrmaFileFormat` | v0.7.0 | Expression animation read: declared expressions become `/Animation/Expressions/<name>` prims carrying a time-sampled weight, unexpanded and unclamped (Motion Phase G, first half) |
| `motionCore` | v0.3.0 | Vendor-neutral pose/animation types (Motion Phase A contract) |
| `motionRuntime` | v0.4.0 | `PoseBuffer`, interpolation/extrapolation, resample, filter, blend |
| `motionRuntime` | v0.5.0 | `IMotionSource` / `ClipSource` / `LiveCaptureSource`, the `motion-capture-trace` format, `ReplaySender`, `CaptureRecorder` (Motion Phase D) |
| `vrmRetarget` | v0.4.0 | Humanoid map, rest-pose correction, pose retargeter, root-motion policy |
| `vrmRetarget` | unreleased | `ExpressionResolver`: a clip's named expression weight resolved onto one avatar's morph-target and material-colour binds, joined on `vrm:expressionName`, with the avatar's `overrideBlink` / `overrideLookAt` / `overrideMouth` arbitrating co-active expressions (Motion Phase G) |
| `motion_retarget` | v0.4.0 | CLI: retargets a clip onto an avatar and binds `skel:animationSource` (Motion Phase C) |
| `motion_capture` | v0.5.0 | CLI: replays a recorded capture session into a semantic humanoid clip the retarget tool consumes unchanged (Motion Phase D) |
| `vrmAdapterVmc` | v0.6.0 | VMC Protocol input: OSC and VMC decode, frame assembly, Unity `HumanBodyBones` → `motion::HumanBone` mapping, `LiveCaptureSource` bridge, UDP receiver |
| `vmc_record` | v0.6.0 | CLI: records a bounded live VMC session to a `vmc-packet-capture` file, or inspects one, with a decode report; `--export-trace` writes what the adapter delivered as a `motion-capture-trace`, which is the adapter's whole hand-off to the product's tools |
| `vrmAdapterMocopi` | v0.7.0 | Native UDP input for one capture product: bounded receiver, packet capture, decoder for an unpublished wire grammar, joint map and basis change, frame assembly with restart detection, `LiveCaptureSource` bridge, and `BodyPlacementPolicy` composing `RootMotion` from a hips-only translating rig |
| `mocopi_record` | v0.7.0 | CLI: records a bounded live mocopi session or inspects a capture; `--export-trace` (from `--inspect` only) writes the same `motion-capture-trace` the product's tools replay unchanged, and reports the hips travel a path carries |
| `motionSource` | v0.7.0 | Format-neutral source rig and animation in the source's own angle order and unit, provenance, the producer-profile contract, and one declared crossing into canonical humanoid motion |
| `motionBvh` | v0.7.0 | BVH syntax, extraction and a frozen diagnostic set — no producer semantics and no default profile |
| `motion_bvh_inspect` · `motion_bvh_convert` | v0.7.0 | CLIs: report what a BVH file contains, and convert one to the avatar-independent semantic clip under an explicitly named producer profile |
| `liveTransport` | v0.8.0 | The live half's shared leaf: bounded UDP receiver, opt-in datagram queue, packet-capture file format with a per-record peer, and the diagnostic vehicle every live adapter reports through — no protocol, no product name, no diagnostic code |
| `osc` | v0.8.0 | The OSC 1.0 wire format once instead of once per adapter: packets, bundles and their flattening, addresses, type tags, arguments, and a refusal naming the byte it refused at — no address semantics |
| `vrmAdapterVrchatOsc` | v0.8.0 | VRChat OSC tracker input: semantic decode of numbered trackers and a named `head`, tracking-space conversion to VRM 1.0's basis, and frame assembly with restart, timeout and partial-set policies. Unknown traffic is recoverable — the message is dropped and the datagram is not |
| `vrchat_osc_record` | v0.8.0 | CLI: records or inspects a VRChat OSC packet capture; `--export-trace --assign` writes the `motion-capture-trace` the product's tools replay unchanged |
| `motionTracking` | v0.8.0 | Which tracker is which body region, and the direct solve from assigned observations to a `HumanoidPose`. Generic and outside every adapter; a region vocabulary that is deliberately not a bone list, and an observed position the solve cannot consume is reported rather than dropped, because consuming one is IK |

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
live adapter shipped in v0.8.0: VRChat OSC Trackers input over a
protocol-neutral OSC decoder
([the OSC track](../roadmap/osc-and-vrchat-trackers.md)). It is the first input
here that carries **tracker observations** rather than humanoid bone transforms
— pre-IK data, where a tracker index is not a body role.

**That difference is why this release claims tracker *input* and not
tracker-driven motion.** The adapter stops at a `TrackerFrame`; the humanoid
solve is generic and lives in `motionTracking`, outside every adapter, and it is
**direct assignment rather than IK** — an observed position the solve cannot
consume is reported rather than dropped, because consuming one is the estimate
this layer declines to make. What that reaches, end to end, is a partition
rather than a count: on the fixture leg four joints move and fourteen hold their
rest pose *exactly*, with four of the fourteen sitting between a driven hip and
a driven foot, which is where a solve that had begun estimating would show
first.

The decoder was designed from an inventory rather than from the specification,
and that ordering is deliberate: VRChat's OSC surface is published, and a
specification says what a *receiver* must accept where what a sender sends is a
measurement. Six captures and 44 918 datagrams later, the inventory carried
**eight addresses — three numbered trackers and a named `head`** — and the named
one is what a decoder reading that path segment as an integer would have dropped
silently ([report 02](../reports/motion/02-2026-08-30-vrchat-osc-address-inventory.md)).

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

Not yet in that layer: motion generation, OpenExec evaluation, blending beyond
the primitive, IK, and foot locking. Look-at animation is — unreleased, since
2026-09-04 — **read, evaluated and authored**: a `.vrma` clip's target point is
carried on the pose and authored under `/Animation/LookAt`, `vrmRetarget`'s
`LookAtEvaluator` turns it into one particular rig's answer against that rig's
own `/Asset/rig/LookAt`, and `motion_retarget` authors the result — eye-joint
rotations for a `bone`-type rig, the four gaze expressions for an
`expression`-type one, which reach the stage through the same
`blendShapeWeights` the face does. Expression
animation is read from a `.vrma` clip, carried on the pose, *resolved* onto a
particular avatar's morph-target and material-colour binds by `vrmRetarget`'s
`ExpressionResolver`, and — unreleased, since 2026-09-01 — authored:
`motion_retarget` writes `blendShapes` and `blendShapeWeights` onto the
`SkelAnimation` it binds to the rig, so the morph-target half of a clip's face
reaches an avatar end to end. Since 2026-09-04 that resolve is an
**arbitration** rather than a sum: an avatar's per-expression `overrideBlink`,
`overrideLookAt` and `overrideMouth` — VRM 1.0's only answer to two co-active
expressions displacing the same vertices — are imported onto
`VrmExpressionAPI` and applied to the whole sample before the binds, so a rig
whose `happy` blocks the blink is baked with the blink its own file asked for.
**Material colours are resolved and not written**, because a colour slot is a
material input and that vocabulary belongs to the material layer. See
[MOTION_ARCHITECTURE_POLICY.md](../design/MOTION_ARCHITECTURE_POLICY.md) and the
Motion Phase ladder in the [backlog](../roadmap/backlog.md).

## See also

- [`ROADMAP.md`](../roadmap/) — phased status (Product P0–P6, Workspace Phase
  0–8, Motion Phase A–H).
- [`SUPPORTED_CONFIGURATIONS.md`](SUPPORTED_CONFIGURATIONS.md) — platforms, OpenUSD, build.
- [`../plugins/vrmSchema/docs/SCHEMA_CONTRACT.md`](../../plugins/vrmSchema/docs/SCHEMA_CONTRACT.md) — schema contract v1.
- [`../plugins/usdVrmFileFormat/docs/DIAGNOSTICS.md`](../../plugins/usdVrmFileFormat/docs/DIAGNOSTICS.md) — diagnostic codes.
