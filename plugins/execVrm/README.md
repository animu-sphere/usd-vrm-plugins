# execVrm

OpenExec computations applying VRM semantics to a target rig, driven by the
schema contract only. Workspace Phase 8 / Motion Phase E; the plan is
[docs/roadmap/openexec-foundation.md](../../docs/roadmap/openexec-foundation.md)
§6, P0-5.

**This is the rig half, not the retarget.** It registers three value types and
three computations:

| Computation | Provider | Result |
| --- | --- | --- |
| `vrm.computeTargetSkeleton` | a `UsdSkelSkeleton` prim | the `vrmRetarget::TargetSkeleton` its `joints` and `restTransforms` state: tokens verbatim, parents from the joint paths, each rest transform decomposed into a rotation and a translation with **scale and shear dropped** |
| `vrm.computeHumanoidMap` | a prim with `VrmHumanoidAPI` applied | the `vrmRetarget::HumanoidMap` its `vrm:humanBones:*` tokens state, resolved against the one skeleton `vrm:skeleton` reaches |
| `vrm.computeRestPoseCorrection` | a prim with `VrmHumanoidAPI` applied | the `vrmRetarget::RestPoseCorrection` from the rest pose of the skeleton `vrm:retarget:sourceSkeleton` reaches onto this humanoid's rig, through its map |

`vrm.humanoidRetarget` and `vrm.computeJointLocalTransforms` are P0-5's other two
nodes and do not exist yet.

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

## Invalidation

No input of any node here moves with time, so none is reported to a time change.
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

## The wrapper, and where it is not one

`vrm.computeHumanoidMap` is `HumanoidMap::SetJointToken` over the bindings, and
`vrm.computeRestPoseCorrection` is `ComputeRestPoseCorrection` over the source
rest, the rig and the map. `execVrm_rig` asserts each by comparing the node's
value with the library's own, and `execVrm_correction` does it again through the
built bundle.

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
refuses in turn, and so does the correction that reads the map.

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
| `execVrm_rig` | the seam, with no stage: the attribute names, the rest decomposition with scale dropped, both skeleton refusals, the empty skeleton, an unordered skeleton carried faithfully, the map equal to the library's own, the skeleton counted, and the three map refusals; the clip's rest read off a semantic skeleton, both source refusals, and the correction equal to the library's, with each of its refusals |
| `execVrm_humanoid` | the built bundle over `humanoid_rig.usda`: the vocabulary against the schema's prim definition, both computations on a `Scope` through the applied schema, one executor warning per unbound bone, a blocked rest pose arriving as one fallback matrix, what a skeleton authoring nothing arrives as, invalidation across the relationship, every refusal, and a prim with the attributes and not the schema having no map |
| `execVrm_correction` | the built bundle over `corrected_rig.usda`: the correction equal to the library's over the rigs computed beside it, and landing the clip's rest on the rig's; invalidation from both relationships and none from time; every refusal; a source that refused; a dangling second source, pinned; and a one-joint source with no rest, answered as the fallback |
| `execVrm_humanoid_without_schema` | the same binary with no `vrmSchema` in the session: the skeleton computes and the humanoid map is not found |

All four carry the CTest label `motion.openexec`.
