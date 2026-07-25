# Motion contract (v0.3.0, extended in v0.4.0)

This is the executable contract frozen by **Motion Phase A**. It gives
`motionCore` and `usdVrmaFileFormat` one vocabulary without making either a
retargeter or runtime. The implementation is deliberately limited to the first
VRMA vertical slice; later Motion Phases may extend this document but may not
silently reinterpret these fields.

v0.4.0 extends it with the **Motion Phase C retarget semantics** below. Nothing
above that section changed: the v0.3.0 fields mean exactly what they meant, and
the retargeter is a new consumer of them rather than a reinterpretation.

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
referenced skeleton, so the avatar keeps owning its own rig. Scale channels are
not authored, matching the v0.3.0 exclusion.

The hand-authored triplet under [`fixtures/motion/`](fixtures/motion/) is the
executable statement of all of the above:
`canonical_walk.usda` + `avatar.usda` must produce `expected_retargeted.usda`.
