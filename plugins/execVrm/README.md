# execVrm

OpenExec computations applying VRM semantics to a target rig, driven by the
schema contract only. Workspace Phase 8 / Motion Phase E; the plan is
[docs/roadmap/openexec-foundation.md](../../docs/roadmap/openexec-foundation.md)
§6, P0-5.

**The rig, and one sample of a clip on it.** It registers five value types --
four of `vrmRetarget`'s, and `execMotion`'s `motion::HumanoidPose`, which a
bundle that reads a type has to register itself -- and five computations:

| Computation | Provider | Result |
| --- | --- | --- |
| `vrm.computeTargetSkeleton` | a `UsdSkelSkeleton` prim | the `vrmRetarget::TargetSkeleton` its `joints` and `restTransforms` state: tokens verbatim, parents from the joint paths, each rest transform decomposed into a rotation and a translation with **scale and shear dropped** |
| `vrm.computeHumanoidMap` | a prim with `VrmHumanoidAPI` applied | the `vrmRetarget::HumanoidMap` its `vrm:humanBones:*` tokens state, resolved against the one skeleton `vrm:skeleton` reaches |
| `vrm.computeRestPoseCorrection` | a prim with `VrmHumanoidAPI` applied | the `vrmRetarget::RestPoseCorrection` from the rest pose of the skeleton `vrm:retarget:sourceSkeleton` reaches onto this humanoid's rig, through its map |
| `vrm.computeBoundPose` | a `UsdSkelSkeleton` prim | the `motion::HumanoidPose` `execMotion`'s `motion.sampleAnimation` answers on the animation its `skel:animationSource` binds, forwarded |
| `vrm.humanoidRetarget` | a prim with `VrmHumanoidAPI` applied | the `vrmRetarget::RetargetedPose`: one sample of the clip `vrm:retarget:sourceSkeleton` reaches, in this rig's joint order, under the humanoid's root-motion statements |

`vrm.computeJointLocalTransforms` is P0-5's last node and does not exist yet.
`vrm.computeBoundPose` is not one of the plan's five: it is the second hop from
a humanoid to its clip's animation, which an exec input cannot make.

## Two schemas, and one of them is another bundle's

26.08 lets exactly one plugin declare a schema in `Info.Exec.Schemas`, so the two
exec bundles partition them ([WORKSPACE.md](../../docs/architecture/WORKSPACE.md)
§2): `UsdSkelAnimation` is `execMotion`'s; `UsdSkelSkeleton` and the `Vrm*API`
applied schemas are this bundle's. It declares the two it registers on.

`VrmHumanoidAPI` being **applied** is what lets this bundle compute on the
importer's humanoid at all. That prim is a `UsdGeomScope`, whose typed schema
`execGeom` declares, and a computation registered on an API schema resolves on
any prim that has the schema applied. It does **not** resolve on a prim that
carries every `vrm:humanBones:*` attribute and not the schema — the offline tool
finds a humanoid by attribute and would read it; this bundle finds one by schema
([the humanoid report](../../docs/reports/openusd/26.08-openexec-humanoid.md) §2).

**The bundle links nothing of `vrmSchema` and still needs it.** exec names a
schema by its TfType, `UsdVrmHumanoidAPI`, and resolves that name when it reads
this plugin's Exec block — which needs vrmSchema's plugInfo registered in the
session, not its library linked. Without it the skeleton computes and the
humanoid map is "not found" after five coding errors of exec's own; that is what
`requires.bundles: vrmSchema` states, and `execVrm_humanoid_without_schema`
measures it (§6 of the report).

**It links nothing of `execMotion` either, and needs that too**, for the
retarget. The pose a retarget reads is `motion.sampleAnimation`, which
`execMotion` registers on `UsdSkelAnimation`, read by name across a skeleton's
`skel:animationSource`. Without `execMotion` in the session the rig and the
correction compute, and exec drops the animation from the bound pose's fan-in
**without a word**. Unlike a missing schema bundle, nothing of exec's own is
posted. The bound pose's count of the relationship is what notices, and
`execVrm_retarget_without_exec_motion` measures it
([the retarget report](../../docs/reports/openusd/26.08-openexec-retarget.md) §3).
The bundle also registers `motion::HumanoidPose` itself: exec checks that a type
an input reads is registered when *this* bundle's computations are, and with the
registration removed every session that did not load `execMotion` first lost
every computation here to a fatal error.

## A value the stage does not give arrives as the fallback

An attribute a prim's **schema defines** has an exec input node whether or not
the stage gives it a value. When it has none — unauthored, or value-blocked —
26.08 posts a `TF_WARN` ("No value set for output") and hands the callback **one
element of Sdf's fallback for the type**: an empty token, an identity matrix. An
authored empty array arrives as zero elements. No builtin says whether a value
was authored, so an absent value is recognised here by what it arrives as
([the report](../../docs/reports/openusd/26.08-openexec-humanoid.md) §4):

| The stage states | What this bundle answers |
| --- | --- |
| a skeleton authoring no `joints` | **refused** — it arrives as one joint named `""`, and no UsdSkel skeleton has one |
| `restTransforms` that do not pair with `joints` — unauthored or blocked on a skeleton of two joints or more lands here, as one matrix against several joints | **refused** |
| `joints = []`, with `restTransforms = []` or with none at all | the empty skeleton — with no joint for a rest transform to belong to, none becomes a number |
| one joint and no `restTransforms` | a one-joint skeleton at **identity** rest — the fallback, which from here is an authored identity; the offline tool answers the same |
| a bone the humanoid does not bind, or binds to `""`, or value-blocks | unbound — all three are the empty token by the time they arrive |

**Not only a schema's attributes.** One the prim *declares* with no value, or
blocks, arrives the same way whatever defines it. That was measured on the
retarget's `vrm:retarget:*` statements, which no schema defines (see below).

The unbound bones cost one executor warning each per compute of the map — the
fixture's 49 of 55 on the first compute, none on a cached recompute. They are
not this bundle's warnings and it cannot silence them; a `""` fallback on each
bone attribute in `vrmSchema` would, and is a schema-contract question filed with
[boundary consolidation](../../docs/roadmap/boundary-consolidation.md).

## What a humanoid may state, and what is refused

| `VrmHumanoidAPI` states | What comes back |
| --- | --- |
| bindings to joints of the one skeleton `vrm:skeleton` reaches | the map; the bones VRM 1.0 requires and it lacks are the map's to report (`FindMissingRequiredBones`), not a refusal |
| a binding to a joint the skeleton does not contain | **no value**, and an error naming the bone and the token |
| two bones on one joint | **no value**, and an error naming both |
| `vrm:skeleton` reaching nothing, or two objects, or one that is not a skeleton or whose skeleton refused | **no value** |
| a bone outside the vocabulary, as a custom attribute | nothing — it is not an input |

Every refusal is somewhere the library *would* answer: `SetJointToken` leaves an
unknown joint's bone unmapped, and the retargeter lets the later of two bones on
one joint win. Both answers are maps no consumer could tell from one the
humanoid meant, which is the bundle's rule and `execMotion`'s: default where an
absent value selects the library's behaviour, refuse where an answer would be
indistinguishable from a measured one, never default a value the scene stated.
`motion_retarget` warns and bakes in both cases, and in three more; the five
are P0-6's first table
([the report](../../docs/reports/openusd/26.08-openexec-humanoid.md) §7).

**The skeleton is counted.** `vrm:skeleton` is a relationship, and 26.08 drops a
target that provides no `vrm.computeTargetSkeleton`, and one whose skeleton
refused, from the fan-in without a word
([the blending report](../../docs/reports/openusd/26.08-openexec-blending.md) §2).
So the node reads the relationship a second time for the builtin `computePath`
and refuses unless exactly one object is reached and exactly one skeleton comes
back. With that check disabled, a humanoid naming a skeleton and a `Scope` was
answered against the skeleton.

One relationship statement it cannot see: a target path with **no prim behind
it**, beside a real skeleton. That path provides neither computation, so it is
missing from both reads, and the map is answered against the skeleton with no
error. A blend catches the same drop by counting weights; a humanoid states
nothing else to count against, and exec offers no read of a relationship's
authored targets. `execVrm_humanoid` pins it.

**Fifty-five inputs, declared in a loop.** `VrmHumanoidAPI` spells a binding as
one attribute per bone, and `Inputs()` appends on every call, so the
registration iterates motionCore's vocabulary rather than spelling it. The names
are `vrm:humanBones:` + `motion::HumanBoneName`, and `execVrm_humanoid` compares
them, both ways, with the properties the schema's own prim definition lists.

## The clip's rest, and the correction onto this rig

`vrm.computeRestPoseCorrection` is `vrmRetarget::ComputeRestPoseCorrection`
over three inputs: the humanoid's own map, the target rig across `vrm:skeleton`,
and the rig a clip was authored against, across **`vrm:retarget:sourceSkeleton`**.
That relationship is defined by no schema. It is a convention of this bundle,
like `motion:timeCodesPerSecond` is `execMotion`'s, and nothing authors it yet.
It names a *skeleton*, not an animation: an animation states no rest, and the
skeleton reaches both halves of a clip, since its `skel:animationSource` names
the animation. Both rigs are read through `vrm.computeTargetSkeleton`, so one
implementation decomposes them.

**The source is read by name and the target never is.** A semantic clip's joint
leaves are the vocabulary's bone names. That is the motion contract's statement,
and `motion.sampleAnimation` reads an animation's joints the same way. Each joint
whose leaf is a bone fills that bone's rest, and its parent is the bone named by
the leaf of its parent path. This is how `motion_retarget`'s `ReadClip` reads it.
A joint that is not a bone fills no slot and its rest is dropped, both here and
in the tool, because `SourceRestPose` has one slot per bone.

| `vrm:retarget:sourceSkeleton` reaches | What comes back |
| --- | --- |
| one semantic skeleton | the correction; unmapped bones identity |
| nothing — unauthored, **or a path naming no prim** | **no value**: both arrive as a count of zero, so an identity rest is not assumed for either |
| two objects, or one that is not a skeleton or whose skeleton refused | **no value** |
| a skeleton where no joint leaf is a bone, such as the avatar's own | **no value** |
| a skeleton naming one bone at two joints | **no value**, with every joint named; the offline tool keeps the later one silently |
| a path naming no prim *beside* a real source | the correction, answered against the real one — the path is invisible to both reads, as for `vrm:skeleton`; pinned |

If the map refuses, the correction refuses too, and its error says the map's
refusal came first.

## One sample of the clip, on this rig

`vrm.humanoidRetarget` is `vrmRetarget::PoseRetargeter` over the rig, the map,
the clip's rest and the root-motion options, asked for **one** pose: the clip's
own sample at the evaluated frame. It is reached from the humanoid through
`vrm:retarget:sourceSkeleton` to the clip's skeleton, then through its
`skel:animationSource` to `motion.sampleAnimation`. That is `motion_retarget`'s
call per sample, with the same four arguments, and it is the pose P0-6 compares.
One relationship on the humanoid names the clip, so the rest the correction
reads and the pose the retarget reads cannot come from two clips. A filtered or
blended pose reaches it only as a driver's override of `motion.sampleAnimation`
or of `vrm.computeBoundPose`, which the suite measures.

Where the clip's root lands is stated on the humanoid, in `motion_retarget`'s
own words:

| Attribute | The flag it is | Absent (the prim has no such attribute) | Refused |
| --- | --- | --- | --- |
| `token vrm:retarget:rootMotion` | `--root-motion` | `hips` | anything but `hips`, `root` and `ignore`, the empty token included |
| `token vrm:retarget:rootJoint` | `--root-joint` | not read unless `root` | under `root`: none, or a token that is not a full joint path of the rig |
| `float vrm:retarget:translationScale` | `--translation-scale` | 1 | a value that is not finite |
| `bool vrm:retarget:preserveTargetHeight` | `--preserve-target-height` | false | nothing |

**A declared, valueless `translationScale` is 0, and is pinned.** It arrives as
the fallback, like a schema attribute (above), and 0 is a scale an author may
state, since the tool accepts `--translation-scale 0`. So it cannot be refused.

**At the default time code the retarget refuses.** The sampler answers a keyed
clip there with an empty pose stamped 0.0, which is harmless as a pose.
Retargeted, it would be the rig's whole rest at 0 seconds. So a driver calls
`ChangeTime` before expecting a retarget, beside "compute once to arm a
request", which it already has to do. The check comes after every statement, so
a request armed on a wrong stage still reports the statement.

## Invalidation

No input of the rig nodes moves with time, so none of them is reported to a time
change.
A rig that were would be recomputed, with everything downstream of it, on every
frame a clip plays. A binding edit reports the map and not the skeleton. A
rest-transform edit reports both, although the map's value does not change:
**invalidation follows the dependency, not the value**. A retargeted
`vrm:skeleton` reaches the map with no request rebuilt.

The correction follows both relationships. A clip rest edit reports it and not
the map. A clip rest *translation* reports it too, although a correction is
rotations only and does not change. A retargeted `vrm:retarget:sourceSkeleton`
reaches it with no request rebuilt
([the correction report](../../docs/reports/openusd/26.08-openexec-rest-correction.md)).

The pose is the one thing here that moves with time, and its dependence crosses
two links and a bundle: a frame change reports the sampler, the bound pose and
the retarget, and not the rig, the map or the correction. A key of the clip, a
rebinding of its skeleton's `skel:animationSource` and a retargeted source
relationship each reach the retarget with no request rebuilt. A root-motion
statement reaches the retarget and nothing else
([the retarget report](../../docs/reports/openusd/26.08-openexec-retarget.md) §6).

## The wrapper, and where it is not one

`vrm.computeHumanoidMap` is `HumanoidMap::SetJointToken` over the bindings,
`vrm.computeRestPoseCorrection` is `ComputeRestPoseCorrection` over the source
rest, the rig and the map, and `vrm.humanoidRetarget` is `PoseRetargeter` over
those and the options. `execVrm_rig` asserts each by comparing the node's value
with the library's own, and `execVrm_correction` and `execVrm_retarget` do it
again through the built bundles.

**The retarget is a wrapper that recomputes a cached value.** `PoseRetargeter`
computes its correction in its constructor and accepts none. So the node
constructs one per evaluation, on every frame the clip moves, and computes
again the correction `vrm.computeRestPoseCorrection` holds; the suite asserts it
is the same value, bit for bit. On a full 55-bone humanoid that is 17.7 µs of
the node's 21.2 µs. The ask is a retargeter that takes the correction as an
input.

Reading a clip's rest off its skeleton — which joint fills which bone, and which
bone is its parent — exists only in `tools/motionRetarget`'s `ReadClip`, so that
assignment is in the seam too; the decomposition under it is
`vrm.computeTargetSkeleton`'s. The ask is the same as for the skeleton: a
`SourceRestPose` built from a semantic skeleton's tokens and rests, beside the
struct.

`vrm.computeTargetSkeleton` has half a library call: `TargetSkeleton` and
`ResolveParentsFromTokens` are `vrmRetarget`'s, and turning a rest matrix into
the rotation and translation a `TargetJoint` carries lives in
`tools/motionRetarget`'s `StageIo.cpp`, where a bundle cannot call it. The seam
([`src/ExecVrmRig.cpp`](src/ExecVrmRig.cpp)) matches the tool's decomposition
line for line, and the ask for
[boundary consolidation](../../docs/roadmap/boundary-consolidation.md) is a
`TargetSkeleton` built from tokens and rest matrices, beside the class.

## How a computation refuses

`execMotion`'s way: a `TF_RUNTIME_ERROR` naming the computation, and **no value
at all** (`VdfContext::SetEmptyOutput`). An empty `TargetSkeleton`, an empty
`HumanoidMap` and an all-identity `RestPoseCorrection` are all answers — a
skeleton authoring `joints = []`, a humanoid binding nothing, two rigs with the
same rest — so none of them can stand for a refusal. A refusal propagates: a
skeleton that refused leaves the map a fan-in with nothing in it, the map
refuses in turn, and so does the correction that reads the map. It propagates
across the bundle boundary too: a clip whose sampler refused leaves the bound
pose nothing, and the retarget refuses after it.

## What this bundle may not do

- **No importer private API and no reparse** of the source `.vrm` / `.vrma`
  bytes: the only input contract is what is on the stage
  ([WORKSPACE.md](../../docs/architecture/WORKSPACE.md) §2).
- **No joint-name heuristics on the target.** A binding is a full joint path,
  resolved exactly; the fixtures' rig joints are named `J_Bip_C_Hips` and the
  like so a leaf lookup would find nothing. The *source* skeleton is read by
  leaf because a semantic clip's joint names are the bone vocabulary by
  contract, and one that does not follow it is refused, not guessed at.
- **No declaring `UsdSkelAnimation`**, which is `execMotion`'s; an animation is
  reached through an input.
- **No stage authoring, no I/O, no second algorithm** — as for `execMotion`.

## Tests

| Test | What it holds |
| --- | --- |
| `execVrm_rig` | the seam, with no stage: the attribute names, the rest decomposition with scale dropped, both skeleton refusals, the empty skeleton, an unordered skeleton carried faithfully, the map equal to the library's own, the skeleton counted, and the three map refusals; the clip's rest read off a semantic skeleton, both source refusals, and the correction equal to the library's, with each of its refusals; the bound pose forwarded and counted; the root-motion statements as the tool's flags, and their refusals; the retarget equal to `PoseRetargeter`'s, applying the cached correction, and its refusals in order |
| `execVrm_humanoid` | the built bundle over `humanoid_rig.usda`: the vocabulary against the schema's prim definition, both computations on a `Scope` through the applied schema, one executor warning per unbound bone, a blocked rest pose arriving as one fallback matrix, what a skeleton authoring nothing arrives as, invalidation across the relationship, every refusal, and a prim with the attributes and not the schema having no map |
| `execVrm_correction` | the built bundle over `corrected_rig.usda`: the correction equal to the library's over the rigs computed beside it, and landing the clip's rest on the rig's; invalidation from both relationships and none from time; every refusal; a source that refused; a dangling second source, pinned; and a one-joint source with no rest, answered as the fallback |
| `execVrm_retarget` | the built bundle and `execMotion` over `retargeted_rig.usda`: the retarget equal to `PoseRetargeter` over the sampler's pose and applying the cached correction bit for bit, the default time code refused, each root-motion statement against `ResolveRootTranslation`, invalidation from the frame, a key, the binding, the source and a statement, a driver's pose through either bundle's key, every refusal, and a valueless statement as the fallback |
| `execVrm_humanoid_without_schema` | the same binary as `execVrm_humanoid` with no `vrmSchema` in the session: the skeleton computes and the humanoid map is not found |
| `execVrm_retarget_without_exec_motion` | the same binary as `execVrm_retarget` with no `execMotion` in the session: the correction computes, and the bound pose and the retarget are refused by this bundle's count alone |

All six carry the CTest label `motion.openexec`.
