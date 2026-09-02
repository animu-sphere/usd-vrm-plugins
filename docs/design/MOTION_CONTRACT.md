# Motion contract (v0.3.0, extended in v0.4.0 through v0.8.0)

This is the executable contract frozen by **Motion Phase A**. It gives
`motionCore` and `usdVrmaFileFormat` one vocabulary without making either a
retargeter or runtime. The implementation is deliberately limited to the first
VRMA vertical slice; later Motion Phases may extend this document but may not
silently reinterpret these fields.

v0.4.0 extends it with the **Motion Phase C retarget semantics**, v0.5.0 with
the **Motion Phase D live-capture semantics**, v0.6.0 with **comparison
semantics**, and v0.7.0 with **expression semantics**, **recorded-source
provenance** and the **root and hips record**, all below. v0.8.0 adds one
section that is a boundary rather than a field — **where a tracker observation
is not** — and adds no type here at all.

Nothing above those sections changed: the v0.3.0 fields mean exactly what
they meant, and each later phase is a new consumer of them rather than a
reinterpretation.

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
(below), expression *reading* in v0.7.0 — `/Animation/Expressions`, described
under "Expression semantics" — and look-at *reading* after v0.8.0
(`/Animation/LookAt`, under "Look-at semantics"), while the rest remain Motion
Phases D–H.

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
`motion-capture-trace` format (`CaptureTrace.h`, version 2) is line-oriented
text with fixed six-decimal precision, so a trace round-trips byte-identically
and a fixture can be *compared* rather than merely parsed. Version 2 added the
`e` expression line; version 1 still reads, because a recording that stops being
readable when the format moves on is a recording lost, and the writer emits only
the current version — so byte-identity is a property of traces this writer
produced. It deliberately does
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

## Comparison semantics (v0.6.0)

Three consumers asked for one comparison and wanted two different answers, so
`motionCore` states both and each is named for the question it answers.
[`Compare.h`](../../libs/motionCore/include/motionCore/Compare.h) carries the
reasoning; this is what the contract promises.

```text
a == b                is this the same recorded value?
NearlyEqual(a, b)     is this the same motion?
```

`operator==` / `operator!=` are exact, on `MotionSourceMetadata`, `RootMotion`,
`ContactState`, `HumanoidPose` and `HumanoidAnimation`. That is what
`ExecTypeRegistry::RegisterType` requires before a pose can cross an OpenExec
computation boundary at all, and it is the comparison a trace round-trip is
defined by. The declarative `MotionConstraintSet` types deliberately have none:
nothing compares them yet.

`NearlyEqual` takes a `MotionTolerance` and answers the question a parity check
and a corpus test are actually asking. The two differ in exactly three places,
and each is a decision rather than an implementation detail:

- **A quaternion and its negation are the same orientation and different
  values.** `NearlyEqual` measures the angle between two orientations
  (`AngleBetween`, always the short arc), so `q` and `-q` are zero apart; `==`
  compares components, so they are not. Downstream of an exec computation the
  strict answer is the conservative one — a flipped sign recomputes what depends
  on it, which is wasteful and never wrong.
- **Provenance is part of the value and not part of the motion.** `==` reads
  `MotionSourceMetadata`; `NearlyEqual` does not. This is the only place the two
  read different fields, and it is why comparing an offline result against an
  OpenExec one needs no switch to turn metadata off.
- **The tolerance is stated once.** Every default is derived from the
  recorded-trace format's six decimals, which a value that survived a round trip
  is already up to 5e-7 away from: `angle` 1e-4 rad, `distance` 1e-5 m,
  `velocity` 1e-4 (looser, because a velocity is a position delta divided by a
  frame interval), `confidence` 1e-6, `time` 1e-6 s. A test that picks its own
  epsilon is asserting a contract nobody reviewed.

Two rules hold for both. **A field the pose does not claim is not compared** —
an absent bone's rotation slot and an unset `RootMotion` field hold whatever the
producer left there, so only the claim itself (the presence bits) is compared,
and a pose that omits a bone never equals one that carries it. **A NaN equals
nothing, including itself**; that is a property of the sample, and a comparison
that hid it would make a non-finite transform arrive later and quieter.

`NearlyEqual` also reports *what* differed, in a fixed order — timestamp, root,
bones in humanoid enum order, confidence, contacts, expressions by name — so a
failing corpus test names the bone and the amount rather than only the
disagreement. An expression *name* is an identifier rather than a measurement,
so both comparisons read it exactly; only the weight takes a tolerance.

## What the contract still owes its next two consumers

Recorded 2026-07-29, when the input-adapter and OpenExec directions were
re-planned. Neither is a change to anything above; both are additions the
shipped types do not yet carry, and each is wanted by more than one caller —
which is the argument for adding them once, deliberately, rather than at the
first call site that needs one.

- ✅ **Deterministic comparison.** Landed 2026-08-02 as the comparison semantics
  above, ahead of the bone mapping that is its first caller. The three consumers
  it was owed to are `ExecTypeRegistry::RegisterType`
  ([OpenExec plan](../roadmap/openexec-foundation.md) P0-4), the
  offline/OpenExec parity comparison (P0-6), and the adapter corpus tests
  ([adapters plan](../roadmap/adapters-mocopi-vmc-ardy.md) §9).
- ⬜ **Tracking state.** `validRotations` says a bone is absent and `confidence`
  says a bone is uncertain. Neither distinguishes *tracking lost* — a source
  that is connected and no longer solving — from *zero pose* or from a bone the
  session never observed. The three mean different things downstream, and only a
  live adapter produces the middle one.
- ✅ **An expression sample.** Landed 2026-08-03 as `ExpressionWeights` on
  `HumanoidPose`, together with format 2 of the recorded trace. See below for
  the one place it departs from what was written here.

A fourth is adjacent and already satisfied, worth naming so it is not
re-litigated: **serialization** exists as the `motion-capture-trace` format
above, and a trace round-trips byte-identically. Anything added to the value
types is added to that format in the same change, or a replay stops reproducing
the session it recorded.

## Expression semantics (v0.7.0)

`HumanoidPose::expressions` is a set of named weights, sorted by name and
holding each name once. The ordering is an invariant rather than a convention:
two producers that reported the same weights in a different order describe the
same motion, so they have to be the same *value*, and a trace written from
either has to round-trip to the same bytes.

**A name is carried verbatim, and the vocabulary is open.** VRM 1.0 defines
preset names and then lets an author add their own; a VMC sender's blend-shape
names are its model's, not a specification's. Resolving one producer's `Joy`
onto a particular rig's `happy` needs the rig, which this layer does not have,
so it is a consumer step (`ExpressionResolve`, Motion Phase G) rather than a
table here that would guess for every producer at once.

**An unreported name is not a zero weight**, exactly as a bone outside
`validRotations` is not an identity rotation. `Find` answers with a pointer so
the two cannot be confused, and `LerpPose` holds a weight only one endpoint
reported rather than fading it toward zero — fading would invent a channel
closing that no producer described.

**Both producers exist as of 2026-08-23.** `vrmAdapterVmc` carries
`/VMC/Ext/Blend/Val`, and `usdVrmaFileFormat` reads the `expressions` channel of
a `.vrma` clip — VRMA points an expression at a node and animates its
translation X as the weight. The clip reader is where the open-vocabulary rule
first costs something: VRMA's specification says a weight outside `[0, 1]` is
clamped, and the reader carries `1.5` as `1.5` with a warning instead. The rule
that decided it is the one above — a value type that quietly corrects a producer
hides that producer from the operator — and the clamp belongs to whoever applies
the weight to a rig, which is the same layer that will resolve the name.

### The one departure: on the pose, not beside it

The item above asked for *a timestamped set of weights alongside `HumanoidPose`*
— a parallel track. It landed as a field on the pose instead, and the reason is
that both producers put expressions on the pose's instants already: a VMC
datagram carries bones and blend values under one `/VMC/Ext/T`, and the `.vrma`
reader already evaluates every channel at the union of their key times. A
parallel track would have required a second buffer, a second intake policy and a
second resampler in `motionRuntime` to carry data arriving at the same instants
as the poses — machinery with one producer and nothing to distinguish it.

What this costs is stated rather than hidden: a producer whose expression
channel genuinely runs on its own clock has to be resampled onto the pose
timeline by whoever reads it. No producer in the tree does, and the day one
does, this is the paragraph to revisit.

## Look-at semantics (after v0.8.0)

`HumanoidPose::lookAtTarget` is **a point, optional, and never a direction.**

**A point, because a direction needs a head.** VRMA points look-at at a node and
the character watches where that node *is*; turning that into a gaze means
knowing where the eyes are, which is a property of a rig. So this layer carries
the place the producer named, and `LookAtEvaluate` (Motion Phase G) turns it into
eye rotations or expression weights against one avatar's own look-at
configuration — the type, the eye joints and the range-map curves the importer
already authors under `/Asset/rig/LookAt`. It is the same division the
expression weights are under: carry what the producer said, resolve where the
rig is.

**Optional, because the origin is a place.** An unreported target is not a gaze
at `(0, 0, 0)`, exactly as an unreported expression name is not a weight of zero
and a bone outside `validRotations` is not an identity rotation. A sentinel
would have made those two the same value at the one point a producer can
legitimately look at, so the presence is carried separately and both comparisons
read it before they read the point.

**On the pose, for the reason expressions are.** A `.vrma` clip's look-at
channel keys into the same union of key times the body and the face are
evaluated at, so it arrives at the instants the poses already exist at. The
departure recorded above for expressions applies unchanged, including its cost:
a producer whose gaze genuinely runs on its own clock has to be resampled onto
the pose timeline by whoever reads it.

**The offset from the head bone travels beside the clip, not on it.** VRMA
states `offsetFromHeadBone`, which is a measurement of the rig the clip was
authored on: it cannot vary within a clip, so paying for it per sample would buy
nothing, and it is authored once as `vrm:lookAtOffsetFromHeadBone`. A file that
states none is read as zero — a gaze starting at the head bone itself — with a
warning, because that is a claim about the source rig rather than a neutral
default.

**A target is placed where the file put it.** A look-at node may be parented,
and a position read in its own space would be a gaze at the wrong place, so the
reader composes the ancestors' *stated* transforms into the target. An ancestor
that the clip itself animates is warned about and not composed: evaluating it
would be a scene evaluation at every instant rather than a clip read, and the
warning is what keeps the difference visible.

**A clip says one of three things**, and the authored stage keeps them apart the
way the expression half does. A channel drives the node — time samples. The file
places the node and nothing animates it — one target for the whole clip,
authored as a default, because a run of identical time samples would claim the
file keyed something it did not. Nothing places it — no `vrm:lookAtTarget` at
all. A clip that declares no `lookAt` block gets no `/Animation/LookAt` prim,
which is a fourth thing again: the file never raised the subject.

*Placed* is the word rather than *states a translation*, and the difference is a
whole state. A target node with no transform of its own under a positioned
parent is placed exactly as deliberately as one that states its own — the
position is in the parent — so what separates "the file gave a position" from
"the file gave none" is whether anything in the chain wrote one, never whether
the composed number happens to be the origin.

**An unusable node costs the target, not the declaration.** A `lookAt` block
naming a node that does not exist, or one a bone or an expression already
drives, still said something: it raised the subject, and it measured its rig's
offset. So the prim and the offset are authored and the target is not, which is
the state a clip declaring an unplaced node is already in — and a stage that
dropped the whole block would be indistinguishable from a clip that never
mentioned look-at, which is a different statement.

**The recorded-trace format carries it** as a `lookat x y z` line, at most one
per frame, from format version 3. No live producer emits one today; the format
carries it anyway, because a pose field a recorder silently dropped would make a
replay differ from the session it claims to reproduce.

## Recorded-source provenance (v0.7.0)

The recorded-file path carries its own provenance type, `SourceProvenance`, and
this section settles what it is **relative to** `MotionSourceMetadata` — a
question the plan asked to have answered before a converter set its first field
rather than after ([recorded-motion-sources.md §10](../roadmap/recorded-motion-sources.md)).

**It is a neighbour, not the same type and not a superset**, and the derivation
runs one way: `motionSource::CanonicalMetadata(provenance)` produces the
canonical value, and nothing produces a `SourceProvenance` from a canonical one.
Two independent arguments give the same answer, and either alone would have been
enough:

- *They travel differently.* `MotionSourceMetadata` rides on every pose and every
  canonical animation, is compared by `operator==`, and is written into the
  recorded-trace format — so a field added to it is a field every sample carries
  and every trace has to round-trip. A file's producer version and the profile it
  was read under cannot vary within a clip, so paying that per-sample cost for
  them would buy nothing.
- *They are answerable by different layers.* Everything in `SourceProvenance` is
  known before any motion is: a reader supplies the format and the file's
  identity, a profile supplies the producer label, and a caller supplies which
  profile it named. `MotionSourceMetadata` describes motion that by then already
  exists.

The mapping, and it is deliberately narrowing:

| `SourceProvenance` | `MotionSourceMetadata` |
| --- | --- |
| — | `kind` = `Clip`, always |
| `producer` | `provider` |
| `format` | `protocol` |
| `sourceId` | `sourceId` |
| `producerVersion` | *dropped* |
| `profileId` | *dropped* |

`kind` is `Clip` because a recorded file **is** a clip by the time any of this
sees it, whatever the sensors that produced it were doing. `LiveCapture` says
values arrived over time from a running source, which is the property
`motionRuntime`'s intake acts on, and a file has none of it.

`protocol` carries the format because the field answers *how did these values
arrive*, and for a recording that is the format it was read from. Leaving it
empty would make a converted clip indistinguishable from one with no stated
origin; putting the format in `provider` would overwrite the more specific fact.

The two dropped fields survive **beside** the motion, in the semantic clip's
authored metadata, the way the `.vrma` importer already records its own source's
file facts. `motionSource_provenance` pins the narrowing as a fact rather than a
sentence: two provenances differing only in producer version and profile id
convert to the same canonical metadata, so a later change that quietly widened
the mapping — packing a profile id into `sourceId`, say — fails there.

**A profile id is never a branch condition**, in this layer or any other. It is
recorded so a conversion is reproducible and auditable; code that read it to
decide behavior would be the producer-conditional core the whole profile design
exists to prevent ([WORKSPACE.md §1](../architecture/WORKSPACE.md)).

## The canonical basis, stated (v0.7.0)

Canonical motion is **right-handed, +Y up, +Z forward, metres**. Three of those
four were already written down at the top of this document; the forward axis was
not, and the recorded-file converter is the first code in this repository that
has to *state* it rather than inherit it.

It is a recording of an existing fact and not a new decision, which is worth
saying because it could easily be read as one. Two independent things in the
tree already answer it. The VMC adapter converts a left-handed, +Y-up,
+Z-forward sender into canonical by flipping X alone and leaving Z untouched
(`vrmAdapterVmc/SkeletonMap.cpp`), so the sender's forward is canonical's. And
the avatars this motion is retargeted onto face +Z by their own specification,
which is what the whole pipeline is aimed at. Anything else would have been
noticed already — as a character walking backwards, which is exactly the class
of failure that looks like a result.

What forced the statement is that a producer profile declares a `forwardAxis`
and a `handedness`, and a converter cannot map "+Z forward" onto canonical
without knowing which way canonical faces. `motionSource::CanonicalBasis` is
that mapping made a value: a signed permutation of the three components whose
**determinant is the handedness question** — +1 where the change of basis is a
rotation, -1 where a left-handed source has to be mirrored to reach a
right-handed canonical space. A position is `M v`; a rotation is
`(w, det(M) · M v)`, which is the same conjugation written for quaternions.

The determinant also settles the angle convention, and this is the part a second
implementation would most likely get wrong twice. Euler angles are composed into
quaternions by the **right-hand rule always**, out of the raw numbers, with no
reference to the source's handedness — a left-handed source's positive angle
then comes out negated in canonical space, which is what a left-hand-rule
rotation *is* once mirrored. Applying handedness in both places produces a body
that is correct in every axis-aligned test pose and wrong the moment anything
turns, so `motionSource_conversion` checks the rotation half *physically*: a
direction is rotated and the answer compared against where that direction has to
end up, because a component-against-component test agrees with a mirrored
implementation as readily as with a correct one.

A named Euler order composes **intrinsically**: `ZXY` is `qZ * qX * qY`, so the
last-declared angle is applied first and each successive one turns about axes
the previous have already moved. Nothing in the source model states this —
`SourceEulerOrder` says only which angle is stored where — so the converter is
where it is decided, and it is written down because the opposite reading also
produces plausible motion: mirrored about the order, agreeing exactly wherever
one axis moves at a time.

## Recorded-source rest pose and the path rule (v0.7.0)

A producer profile maps a rig onto the canonical humanoid, which has fewer
joints than any real rig. **A bound bone's local rotation is the composition of
the source local rotations from just below its nearest bound ancestor down to
itself, root-first.** The joints in between are not rotations to drop — a joint
between two mapped ones is *on the path* between them — and a converter taking a
mapped joint's local rotation verbatim would lose every rotation above it and
place the arms and head wrong. That reads as a subtly misassembled body rather
than as a failure, which is why the rule is stated here rather than left to be
found as a bug in a retarget nobody can localise.

It belongs to the converter and not to a profile: a profile that could state it
would be stating an algorithm, and profiles are declarative
([recorded-motion-sources.md §3](../roadmap/recorded-motion-sources.md)). What a
profile's choice of *which* joints to map changes is how a chain's bend is
distributed, never the chain's total orientation — the composition preserves
that whatever the profile chose.

The same walk builds the clip's rest pose, from whichever rotations the profile
says are the rest (`restPose: rest-offsets` | `stated-rest-rotations` |
`first-frame`) and from the rest translations along the path. One composition
with two callers, deliberately: a rest pose derived by a second traversal is a
second traversal that can disagree with the first, and the disagreement would
appear as a constant per-bone offset that looks like a bad capture.

**A rest taken from the first frame is taken from the first frame entirely**
(v0.7.0, with the second producer). `first-frame` originally took its rotations
from frame 0 and its translations from the rest offsets — one rest assembled out
of two poses. That is invisible while a producer's offsets *are* its rest, and a
producer whose offsets are its rest does not choose `first-frame`; the setting
exists for the export whose offsets are not a pose at all, which is exactly the
export the mixture is wrong for. So under `first-frame` a joint's rest
translation is its **first sampled translation** where it has one, and its rest
offset where it has none. The second producer's export makes the difference
concrete: its `Hips` `OFFSET` is where the capture volume put the performer —
427 cm of Z away in one file and near zero in another from the same rig — so the
old reading put a capture artefact into a rest pose and `vrmRetarget` would have
subtracted it from every frame.

What the setting *means* is worth restating with it, because the name invites a
claim the data does not make. `first-frame` does not assert that the writer's
first frame is a neutral pose. It says the source states no rest, and the
profile elects frame 0 as the one to measure motion away from — so the target
avatar stands in its own rest at frame 0 and moves as the performer moved
thereafter. That is coherent and it is a limitation: two clips from one dataset
are each anchored to their own first frame, and their absolute postures are not
comparable to each other. A source that genuinely states a rest says so with
`rest-offsets` or `stated-rest-rotations`, and neither is affected by any of
this.

**`first-frame` is not good enough to bake onto an avatar, and `t-pose` is what
a producer with a real rig gets instead** (v0.7.0, from the first bake onto a
real VRM). The limitation above is worse in practice than it reads: mapping the
source's frame 0 onto the target's rest means the avatar never leaves its own
rest pose's neighbourhood. A T-posed avatar driven by a walk whose arms hang at
its sides walks with its arms held straight out, and the leg that happened to be
bent in frame 0 carries that bend as a permanent offset while the straight one
looks correct. Nothing is wrong with any individual rotation; the reference is
wrong, and every frame inherits it.

`restPose: t-pose` says the rig's neutral **is** the canonical T-pose — arms
along the lateral axis, legs down, spine up, toes forward. It is the value for a
producer that retargeted its capture onto a published character rig, which is
common enough to be worth a word: the rest exists, the format simply has nowhere
to write it. The rest is then built from the canonical T-pose directions and the
rig's **own bone lengths**, which its rest offsets already carry, so nothing
about the character the producer used enters this repository.

**The offsets pin direction and not roll, and that gap is filled from the first
frame.** A bone direction leaves the rotation *about* that bone free, and a
rig's bind carries a roll per joint that BVH never states. Choosing one — the
shortest rotation, say — puts every joint in exactly the right place and every
frame in the wrong one, which reads as a limb twisted about its own axis rather
than as a failure. So each joint starts at its orientation in frame 0 and is
turned by the least that brings its bone onto the T-pose direction: the aim
comes from the canonical pose and the roll comes from the file.

**A root the profile has silenced cannot aim anything.** `t-pose` turns each
joint to face the way a T-posed humanoid's bone does, and
`RootRotationPolicy::None` says the rig's root joint states no body rotation, so
on a rig whose root *is* its hips the two meet: the bone below an identity root
goes wherever the rig's offsets send it and the body is not stood up. That is
the profile being obeyed rather than a gap — a root whose rotation says nothing
about the body cannot be the joint that orients it — and it does not arise for
the shape the pair is normally written for, a reference node *above* the hips,
which carries no bone and was never going to aim one.

That makes `t-pose` a statement about a rig **plus one reading of one frame**,
which is weaker than the name suggests and is stated here rather than discovered
later. What it costs is bounded in a way `first-frame` is not: the part frame 0
is wrong about is exactly the part being replaced, and a clip whose first frame
holds a limb rolled unusually about its own axis inherits that roll. What
recovers the last of it is a rest a profile could state outright, which is a
vocabulary this does not add.

`motionSource::CanonicalRestPose` carries a local rotation and a local
translation per bone plus a presence bitset, and **no parent array**: the
semantic parent of a bone within a rig carrying `present` is
`motion::NearestPresentAncestor`, and a second copy of the humanoid taxonomy is
the defect `HumanBoneParent` was moved into `motionCore` to avoid. It is
deliberately not a field of `motion::HumanoidAnimation` — that type is compared
by `operator==` and round-tripped through the recorded-trace format, so a field
added to it is one every live-capture consumer inherits. It is
`vrmRetarget::SourceRestPose`-shaped and never meets it in code: a semantic clip
stands between them, and `motion_retarget` reads the rest back off the stage.
Should a third shape of the same value appear, that is the point at which it
belongs in `motionCore` rather than beside each caller.

**Root translation and root rotation are the profile's two questions.**
`AbsolutePosition` and `RestRelative` differ only in where their zero is, and
both become the same canonical thing — an absolute position in the clip's own
space, which is what `vrmRetarget` subtracts each rig's own hips rest from.
`RootTranslationPolicy::None` drops the channel. `RootRotationPolicy::None`
drops the root's rotation *everywhere*, including out of the path composition:
a scene node's rotation reaching the first bound bone below it is body motion
invented from a transform that says nothing about a body.

`None` there means the rig's **root joint** and only it, which is deliberately
not the same scope as the sentence below about paths — the value is a statement
about one node, and `BodyOrientation` is a statement about a composition. The
shape neither spells is a *turning* scene node above a hips that also turns; no
export has it, and the day one does, that is a new value rather than a
reinterpretation of these two.

**Both questions are asked of a path, not of a joint** (v0.7.0, with the second
producer). Where the body is and which way it faces is one fact about a rig, and
a rig is free to spread it over more than one joint: the second producer's
export puts the locomotion on a reference node that never rotates and the body's
orientation on that node's only child. So the two policies describe the
composition of the source path **from the rig's root down to the joint bound to
`hips`** — the same walk, ending at the joint the canonical humanoid roots at.

Three things follow, and each is why this is a contract line rather than an
implementation detail. The path always exists: `ValidateSourceProfile` refuses a
profile that does not bind `hips`, and refuses one that binds them optionally,
so there is no valid profile for which "down to the hips" names nothing. A rig
whose root *is* its hips has a path of length one and is unchanged by this
sentence — the first producer's export reads identically before and after, which
is what makes the rule safe to state after one producer had already shipped. And
a profile still names the rig's **root** in `root.joint`, because that field is
how a profile is matched against a skeleton and matching is a question about
shape; the joint the policies act on is derived from the mapping instead, since
a profile that could name it separately could also name one off the path and
mean nothing by it.

The failure this replaces is worth recording, because it was quiet in one
direction and loud in the other. Reading the rig's root alone gave a body that
never turned; naming the child instead was not expressible at all. Meanwhile the
child's translation went to `ConversionReport::droppedTranslationJoints`, so a
conversion of that export would have *said* it was throwing away the walk.

**A track expressed as quaternions is refused**, with the reason, until a reader
writes one. Nothing in the tree does, so converting it would mean implementing a
path no fixture exercises end to end and testing it against a value this
repository invented. The alternative recorded in
[§10](../roadmap/recorded-motion-sources.md) — a synthetic fixture, said in the
corpus to be synthetic — stays open and is what a reader producing quaternions
would arrive with.

## Root and hips (v0.7.0)

The record [§5.2](../roadmap/adapters-mocopi-vmc-ardy.md#52-frame-assembly-is-a-stated-policy-not-an-emergent-one)
left open twice and v0.7.0 owed: **what a sender means by hips offset and root**,
what each path does with it today, and what the parts still open cost. It is
written as a record first and a policy second, in that order, because two of the
three paths had the evidence to decide and one did not.

### What was observed

| Path | Channels that could be body translation | What the producer says | Evidence |
| --- | --- | --- | --- |
| **mocopi, native UDP** | the hips joint's translation, and nothing else | absolute, metres, every frame | 207,064 measured bone-frames in which **every non-root translation equalled its rest offset bit for bit**, over five device sessions |
| **mocopi, BVH export** | the root joint's translation, which is that rig's hips | `absolute-position` and `body-orientation`, declared in [`mocopi-mobile-bvh-default-v1`](../../profiles/motion/mocopi-mobile-bvh-default-v1.yaml), centimetres | one 17-second export, measured 2026-08-04 |
| **VMC Protocol** | `/VMC/Ext/Root/Pos` **and** the hips local position, both reachable | nothing: the protocol defines both and composes neither | generated corpus only — **no real sender has been recorded** |

Two of those rows are the same application, and their rest offsets agree sign
for sign to a worst component difference of **4.4e-7 m** once the
centimetre/metre factor is removed. So they are not two producers agreeing; they
are one producer observed twice, which is weaker evidence than it looks and is
exactly why the third row is not decided by analogy with them.

### What is canonical today

**A hips translation that is the rig's only translating joint is body
translation, and it is `RootMotion::worldPosition` — absolute, in the source's
own space.** The rig's rotation at that joint is the body's orientation and is
`RootMotion::worldOrientation`, while remaining the `HumanBone::Hips` local
rotation: a rig that roots at its hips has a root path of one joint, so the
composition down that path *is* that joint, and the duplication is what makes
two observations of one session comparable field for field.

That was already what the recorded path authored — `motionSource`'s converter
composes down the root path and both `RootTranslationPolicy` values land on the
same canonical thing. As of **2026-08-23** the native path authors it too, under
`vrmAdapterMocopi::BodyPlacementPolicy::HipsOnly`, which is the assembler's
default and the only one of §5.2's four words this protocol can express: there
is no root channel here, so `RootOnly` and `RootPlusHipsOffset` are absent rather
than unchosen.

Nothing downstream changed to accept it. `motion_capture` seeds the hips rest
translation with the session's first observed root position, so what reaches an
avatar is a **delta** exactly as the Phase C rule requires; `vrmRetarget` reads
`hasPosition` and `worldPosition` and applies the delta under whichever
`RootMotionMode` the caller named. `worldOrientation` is authored by both paths
and consumed by neither retarget mode — it is carried because a trace that
dropped it could not be compared against one that kept it.

**What it cost to leave open, now measured rather than described.** A 36-second
device session walks 4.81 m of hips path (0.69 m net; a walk out and back makes
those disagree). Until the record was written that reached the recorded path and
*nothing at all* reached the live one, so a session retargeted from UDP walked on
the spot — legs stepping, body turning, nothing travelling
([report 01 §4](../reports/motion/01-2026-08-15-mocopi-cross-source.md)). The
same measurement is still printed by `mocopi_record --export-trace`, with its
verb changed from what is being dropped to what is being kept, so an export from
either side of this record reports one quantity.

### What stays open, and what that costs

**The VMC half.** `/VMC/Ext/Root/Pos` and the hips local position are both
reachable in `vrmAdapterVmc`, both converted, and deliberately not composed —
`VmcFrameAssembler::hipsOffset` is reachable and unread. No policy is chosen,
because choosing one needs a measurement nobody here can supply: what a real
sender puts in each field, and whether senders differ. Inventing a capture for it
would be guessing at a sender's behaviour, which
[§2](../roadmap/adapters-mocopi-vmc-ardy.md#2-what-an-adapter-is-allowed-to-be)
forbids for the reason this record exists.

The cost is stated rather than hedged: **a VMC session retargets in place**, the
same way a native one did until this record, and the four words are still the
four available when a sender is recorded — `RootOnly`, `HipsOnly`,
`RootPlusHipsOffset`, or a per-sender profile. The last is the one the recorded
track already built and the one this repository would reach for first, since a
sender's convention is a fact about the sender.

**Why the native answer does not settle it.** The mocopi rig has one translating
joint, so its ambiguity does not arise; VMC's has two channels that a sender may
use in at least three ways. Applying `HipsOnly` there by analogy would be
composing a value from a guess about a product, which is what §2 refuses and what
a per-sender profile exists to do properly.

**What would close it.** One recorded session from each of two VMC senders, with
both channels' movement reported separately — which `vmc_record` already does,
because it reports them as movement rather than as meaning. That is an operator's
work and not a code milestone.

## Tracker observations, and where they are not (v0.8.0)

A **tracker source** observes numbered devices rather than bones: a position and
an orientation in the receiving application's tracking space, under an identity
that is an index into whatever the wearer strapped on
([the OSC track](../roadmap/osc-and-vrchat-trackers.md) §5). It is pre-IK, and it
is a fourth producer category beside the recorded source, the live pose source
and the generated source.

**It gets no type in `motionCore`, and that is a decision rather than a
deferral.** The question was left open in [the track](../roadmap/osc-and-vrchat-trackers.md)
§10 with two candidate answers — a generic `TrackingSource` or tracker-sample
type here, or nothing here at all — and it is settled by asking what would read
one. Every consumer of this header takes a pose: `vrmRetarget` maps bones onto a
target rig, the `motion-capture-trace` format serialises poses, the comparison
semantics compare them, and the OpenExec nodes evaluate them. A tracker sample
reaching this file would be a value with no reader in the aggregate product and
three standing obligations anyway — equality, comparison, and a place in the
trace format, which the rule above requires of *anything* added to the value
types. So the boundary this contract draws stays where it was: **`motionCore`
begins at the canonical pose**, and what a device observed before one existed is
somebody else's type.

**Whose:** `motionTracking`, which already holds two of the three decisions
[§5.1](../roadmap/osc-and-vrchat-trackers.md#51-assignment-is-a-third-thing-and-it-belongs-to-neither-end)
separates — the region vocabulary and the operator's assignment — and now holds
the third. That library takes an edge to this one and never the reverse
([WORKSPACE.md §2](../architecture/WORKSPACE.md)), which is what lets a solve
produce a `HumanoidPose` while nothing in `motionCore` learns that a tracker
exists. It is also the reason no adapter is involved: a solve inside an adapter
would be the second motion pipeline the whole layer exists to prevent, and a
VRChat-shaped observation type here would be the first vendor value in a
vendor-neutral library.

**What a tracker-driven pose is, as a value.** An ordinary `HumanoidPose`, and
sparse by construction: a rig of three to eleven trackers observes a handful of
places on a body, so `validRotations` carries what the solve authored and
nothing else, exactly as a clip that omits a bone does. Nothing new is added to
the pose for this producer — no tracker identity, no per-bone provenance — and a
consumer that cannot tell a tracker-driven pose from a clip-driven one is
reading the value correctly. `MotionSourceMetadata` is where a producer says
what it was.

**Root and hips are the existing rule, not a second one.** A hips tracker is a
body translation observed at one place, which is the case the root/hips record
above already answers: it is `RootMotion::worldPosition`, the same rotation is
`RootMotion::worldOrientation`, and it remains the `HumanBone::Hips` local
rotation. A tracker path that invented a second convention would make two
observations of one session incomparable field for field, which is the cost that
record was written to stop paying.

**What this section deliberately does not decide** is what a solve *does* — which
regions it can place, what it does with an observed position it cannot use, and
what it refuses. That is behaviour with a fixture per outcome, and it belongs to
VRC-5 rather than here, on the same rule the transport ring's magic and timeout
followed: a contract that pre-empted an implementation it could not see would be
deciding the wrong half.

The **tracking state** item this contract still owes its consumers is unchanged
by any of this. `validRotations` says absent and `confidence` says uncertain;
neither says *the source is connected and has stopped solving*, and a tracker
rig — where a device can go dark mid-session while the stream continues — is the
third producer to want the distinction rather than the first to answer it.
