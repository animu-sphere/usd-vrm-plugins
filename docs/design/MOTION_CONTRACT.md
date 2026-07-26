# Motion contract (v0.3.0, extended in v0.4.0 and v0.5.0)

This is the executable contract frozen by **Motion Phase A**. It gives
`motionCore` and `usdVrmaFileFormat` one vocabulary without making either a
retargeter or runtime. The implementation is deliberately limited to the first
VRMA vertical slice; later Motion Phases may extend this document but may not
silently reinterpret these fields.

v0.4.0 extends it with the **Motion Phase C retarget semantics** and v0.5.0
with the **Motion Phase D live-capture semantics**, both below. Nothing above
those sections changed: the v0.3.0 fields mean exactly what they meant, and each
later phase is a new consumer of them rather than a reinterpretation.

## Scope

v0.3.0 accepts a `.vrma` GLB containing `VRMC_vrm_animation` `specVersion`
`"1.0"`, reads the first glTF animation, and authors an avatar-independent
semantic skeleton under `/Animation`:

```text
/Animation                         Scope, default prim
  /HumanoidSkeleton                 UsdSkelSkeleton
  /BodyAnimation                    UsdSkelAnimation
```

The skeleton joint tokens are semantic paths (`hips`, `hips/spine`,
`hips/spine/chest`, …), ordered by the VRM humanoid taxonomy. A clip is never
bound to a VRM avatar here. Retargeting belongs to Motion Phase C.

The checked-in design triplet is under
[`fixtures/motion/`](fixtures/motion/):

- [`canonical_walk.usda`](fixtures/motion/canonical_walk.usda) — expected
  output of the minimal VRMA reader;
- [`avatar.usda`](fixtures/motion/avatar.usda) — an intentionally different
  target joint order;
- [`expected_retargeted.usda`](fixtures/motion/expected_retargeted.usda) — the
  Phase C result, included now to make the eventual hand-off unambiguous.

They are hand-authored contract examples, not generated golden files. The
importer's executable fixture and flattened golden live beside the plugin.

## `motionCore` value contract

`motionCore` is a plain static CMake library. Its public types are
`motion::HumanBone`, `HumanoidPose`, `HumanoidAnimation`, `RootMotion`, source
metadata, foot-contact samples, and `MotionConstraintSet`. It may use OpenUSD
`GfVec3f` and `GfQuatf` value types, but it must never include or expose USD
stage, Sdf, plug, file-format, network, vendor-SDK, or product-specific APIs.

`HumanoidPose` stores one quaternion per `HumanBone` plus an explicit presence
bit. An absent bone is not an identity sample. `RootMotion` remains a separate
object even when hips translation is carried in the authoring skeleton, so a
future retargeter can choose how to apply it without rewriting clip data.

`MotionConstraintSet` is declarative only: root waypoints, trajectories,
full-body keyframes, joint positions, and joint rotations. v0.3.0 records the
types; it neither solves nor evaluates constraints.

## Coordinates and time

- Input glTF/VRMA and authored USD use right-handed, Y-up, metre units.
- glTF node rotations are copied as unit `GfQuatf` values; no VRM-avatar front
  correction is applied to this avatar-independent clip.
- Only hips translation is accepted as body translation. Translation channels
  on other humanoid bones and every scale channel are ignored with a warning.
- The importer resamples the first animation's channel times to a shared pose
  timeline. The authored stage uses 30 time codes per second; clip seconds map
  to time code by `seconds * 30`.
- `LINEAR` and `STEP` samples are read. `CUBICSPLINE` currently uses its value
  vertices as a linear approximation and emits a warning; tangent semantics are
  not promised by v0.3.0.

## Provenance and exclusions

`/Animation` carries namespaced `customData` under `vrma`: source format,
VRMA spec version, raw extension payload, and root-motion source. In USDA this
is represented as `dictionary vrma`, because USD expands colon-separated custom
data keys into a namespace dictionary.

Expressions, look-at, multiple clips, interpolation fidelity beyond the rule
above, live capture, motion generation, binding/assembly, retargeting, IK, and
foot locking are intentionally outside v0.3.0. Retargeting landed in v0.4.0
(below); the rest remain Motion Phases D–H.

## Retarget semantics (Motion Phase C, v0.4.0)

`vrmRetarget` expands a semantic clip into a specific rig's joint order. It
takes plain values and never opens a stage; `tools/motionRetarget` is the stage
half.

**Binding.** A human bone drives a target joint only through an explicit
binding: the avatar's `vrm:humanBones:<bone>` attributes, or a
`humanBone -> joint token` map supplied by the caller. Joint names are never
matched heuristically. A bone the clip drives but the rig does not bind is
**reported**, not guessed, and a joint the clip does not drive keeps its rest
transform rather than collapsing to identity.

**Rest-pose correction.** What survives the change of rig is the bone's
world-space rotation *away from its own rest*. With source rest `S`, source
parent rest `Sp`, target rest `T`, and target parent rest `Tp`, equating the two
world deltas

```text
Tp * Qt * T^-1 * Tp^-1  ==  Sp * Qs * S^-1 * Sp^-1
```

gives `Qt = (Tp^-1 * Sp) * Qs * (S^-1 * Sp^-1 * Tp * T)`, composing the OpenUSD
way (`a * b` applies `b` first). Where both rest poses are identity — the
`usdVrmaFileFormat` case — the sample passes through untouched.

**Root motion.** `RootMotion` stays a separate object, so where it lands is a
retarget-time choice: `Ignore` (animate in place), `Hips` (the joint bound to
`HumanBone::Hips`, the default), or `RootJoint` (a named root, leaving the hips
at rest). What carries is the **delta** from each rig's own hips rest
translation, not the absolute value, so a clip authored on a 1.0 m rig drives a
1.6 m one without the avatar snapping to the source's hip height. A uniform
`translationScale` and a `preserveTargetHeight` flag (horizontal delta only)
are the two documented adjustments.

**Output.** The bake authors a `UsdSkelAnimation` in the target rig's joint
order and binds it with `skel:animationSource` on an *override* of the
referenced skeleton, so the avatar keeps owning its own rig. Scale is never
*animated*, matching the v0.3.0 exclusion — but a constant identity `scales`
array is authored, because UsdSkel resolves translations, rotations and scales
as a unit and `scales` carries no schema fallback. Omitting the attribute does
not mean "no scale animation"; it means the clip binds correctly and then
resolves no joint transforms at all.

The hand-authored triplet under [`fixtures/motion/`](fixtures/motion/) is the
executable statement of all of the above:
`canonical_walk.usda` + `avatar.usda` must produce `expected_retargeted.usda`.

## Live-capture semantics (Motion Phase D, v0.5.0)

Phase D adds the observation side of §6.1: `IMotionSource`, the generic
`LiveCaptureSource`, a recorded trace format, and the replay driver that makes
the two testable. It changes nothing above — a live pose is a `HumanoidPose`
and a recorded session is a `HumanoidAnimation`, so the Phase C retargeter
consumes a capture without knowing it was one.

**One interface, no source distinction.** `IMotionSource::Sample(t)` answers
"what is the pose at this evaluation time?" and returns a `PoseSampleResult`
carrying the pose, a status, and the lag. `ClipSource` (a finished animation)
and `LiveCaptureSource` (a stream) are interchangeable behind it. Provenance
travels as `MotionSourceMetadata` and is **never** a branch condition — the
Phase D code reads `kind`, `provider`, `protocol` and `sourceId` only to record
them (motion policy §9).

**The status is part of the answer.** `Sampled` means real bracketing data;
`Held` means the request fell outside the observed range and the boundary pose
was repeated; `Extrapolated` means the root carried forward along its last
velocity; `Unavailable` means the source had nothing. A caller that ignores the
status cannot distinguish a live source from a stopped one, because a stopped
source keeps answering `Held` forever.

**The core owns no transport and no clock.** `LiveCaptureSource::Push` takes an
already-decoded pose, stamped in the capture system's own clock. Protocol
decode and coordinate conversion are the adapter's job (motion policy §8.2), and
nothing in `motionRuntime` reads a wall clock. Both properties are what make a
recorded session replayable: the same frames pushed in the same order produce
identical buffers, statistics, and retargeted output on every run and every OS.
`SetClockOffset` / `AlignClock` map the capture clock onto the consumer's; a
deliberate playback delay is the same operation with an earlier alignment.

**Intake decisions are explicit and counted.** Four policies, each reported in
`LiveCaptureStats` rather than applied silently:

- *Confidence.* Bones reporting below `confidenceFloor` are treated as missing.
  A frame carrying **no** confidence array is never gated — an adapter that
  cannot measure confidence must not lose its bones for saying so.
- *Missing bones.* `HoldLast` carries the last observed rotation forward, so a
  dropout freezes a limb; `LeaveUnbound` reports it absent so the target rig's
  rest pose can take over. This is the same invariant `PoseBuffer` keeps for a
  missing sample: **held, never faded toward identity**.
- *Root motion.* `Passthrough` records what arrived; `Ignore` drops the root
  entirely; `DeriveVelocity` (the default) fills in a linear velocity from
  consecutive frames when the source reports none, which is what lets
  extrapolation hide a late frame.
- *Ordering.* Timestamps must strictly increase. A frame behind the head by more
  than `staleFrameSeconds` is counted **stale** (an ordinary straggler); a
  closer one is counted **out-of-order**, which means the source's clock is not
  monotonic and the caller has a fault to investigate. Both are refused.

**A recorded trace stores capture order, not arrival order.** The
`motion-capture-trace` format (`CaptureTrace.h`, version 1) is line-oriented
text with fixed six-decimal precision, so a trace round-trips byte-identically
and a fixture can be *compared* rather than merely parsed. It deliberately does
not record delivery order: when a frame arrived is a property of the transport,
and it is reproduced by the replay schedule instead. `ReplaySender` pushes
frames as a caller-driven clock advances; `CaptureRecorder` accumulates the
evaluated result back into a `HumanoidAnimation`, carrying the per-tick status
counts with it so a clip baked from a laggy session holds the evidence of that
lag.

**A captured clip is an ordinary semantic clip.** `motion_capture` authors the
same avatar-independent shape `usdVrmaFileFormat` produces — a `UsdSkelSkeleton`
over humanoid joint paths, a `UsdSkelAnimation` bound to it, a constant identity
`scales` array for the reason given above — so `motion_retarget` bakes a live
session with no changes. Two details are specific to capture:

- **Rest transforms are identity, except the hips.** A capture stream reports
  rotations relative to the humanoid rest, never the rest itself, so identity
  rests make the rest-pose correction a no-op — the honest reading, not a
  simplification. The hips rest translation is seeded with the session's first
  observed root position, so root motion still arrives downstream as a *delta*
  from where the capture started rather than as an absolute height, exactly as
  the Phase C root-motion rule requires.
- **A bone the session never observed is absent, not authored at rest.** A joint
  that is present and unmoving means something different downstream from a joint
  that was never captured.

**One humanoid taxonomy.** The semantic hierarchy (`HumanBoneParent`,
`NearestPresentAncestor`, `HumanBoneJointPath`) moved into `motionCore` in
v0.5.0. The `.vrma` reader previously carried a private copy; two tables that
can disagree would produce two skeletons that look alike and do not compose.

Live capture's corpus is synthetic by necessity, not convenience — see
[`libs/motionRuntime/tests/corpus/README.md`](../../libs/motionRuntime/tests/corpus/README.md).
Validation against a real capture rig needs an adapter and remains open.
