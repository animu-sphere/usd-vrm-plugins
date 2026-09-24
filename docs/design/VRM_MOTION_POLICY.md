---
status: binding
owner: usd-vrm-plugins
---

# VRM motion policy

How VRM and VRMA use motion: what `.vrma` import authors, how VRMA and VRM
compose on a stage, and what a VRM rig adds when motion is applied to it —
the humanoid binding, expressions, look-at, the bake, and `execVrm`.

Generic motion is not defined here. This repository **consumes** it, and
links to the repository that owns each contract instead of restating it
([contributing/documentation.md](../contributing/documentation.md#cross-repository-contracts)):

| Subject | Owner |
| --- | --- |
| `MotionPose`, `MotionClip`, `MotionChannelSet`, `SourceMetadata`, the canonical basis, root motion and the hips, comparison, the recorded-trace format, `MotionStream` intake | `usd-motion-plugins` [MOTION_CONTRACT.md](https://github.com/animu-sphere/usd-motion-plugins/blob/main/docs/design/MOTION_CONTRACT.md) |
| Generic retargeting: rest-pose correction, root-motion modes, scale, partial skeletons, retarget diagnostics | `usd-motion-plugins` [RETARGETING_POLICY.md](https://github.com/animu-sphere/usd-motion-plugins/blob/main/docs/design/RETARGETING_POLICY.md) |
| How motion is represented and read in OpenUSD | `usd-motion-plugins` [USD_MAPPING.md](https://github.com/animu-sphere/usd-motion-plugins/blob/main/docs/design/USD_MAPPING.md) |
| The OpenExec driver contract, driver diagnostics, and what a stage states for a computation | `usd-motion-plugins` [EXEC_CONTRACT.md](https://github.com/animu-sphere/usd-motion-plugins/blob/main/docs/design/EXEC_CONTRACT.md) |
| Device and protocol input, source profiles, source coordinate conversion, tracker observations | `motion-connectors` [CONNECTOR_CONTRACT.md](https://github.com/animu-sphere/motion-connectors/blob/main/docs/design/CONNECTOR_CONTRACT.md) |

This document replaces the VRM-specific parts of the retired
[MOTION_ARCHITECTURE_POLICY.md](MOTION_ARCHITECTURE_POLICY.md) and
[MOTION_CONTRACT.md](MOTION_CONTRACT.md). The importer, the schema contract
and the import / evaluation boundary stay [DESIGN_POLICY.md](DESIGN_POLICY.md)'s;
what `vrmSchema` promises is
[SCHEMA_CONTRACT.md](../../plugins/vrmSchema/docs/SCHEMA_CONTRACT.md)'s.
Section numbers are stable.

---

## 1. VRM and VRMA are separate file-format plugins

`.vrm` and `.vrma` both sit on glTF/GLB, and they play different roles in USD.

| Input | Meaning | USD product | Bundle |
| --- | --- | --- | --- |
| `.vrm` | a character asset | mesh, material, skeleton, humanoid, expression, look-at, spring-bone data | `usdVrmFileFormat` |
| `.vrma` | reusable motion | humanoid animation, expression animation, look-at animation | `usdVrmaFileFormat` |
| runtime | application and evaluation | retarget, root motion, expression, look-at | `motion_retarget`, `execVrm`, or a runtime outside this repository |

`.vrma` reading is never added to `usdVrmFileFormat`. The two bundles have a
symmetric structure — a document reader, a canonical model, an authorer — and
neither links the other.

A file-format plugin never evaluates, simulates or retargets. That is the
importer's boundary ([DESIGN_POLICY.md §10](DESIGN_POLICY.md#10-runtime-semantics--openexec))
applied to motion.

## 2. Composition

**VRM and VRMA compose by reference, not by `subLayer`.** A subLayer stack puts
`/Asset` and `/Animation` on one stage and says nothing about which skeleton a
clip applies to, how humanoid semantics map to real joints, how the rest poses
differ, where root motion goes, or which expressions and look-at a clip
targets. Several clips sharing a prim path would also conflict by layer
strength.

```text
shot.usda
├─ /World/Character            references avatar.vrm</Asset>
├─ /World/Motions/Walk         references walk.vrma</Animation>
└─ /World/Bindings/CharacterWalk   target, source motion, retarget policy
```

**A third binding / assembly layer relates them**, and the VRMA original and
the animation retargeted for one character stay separate:

```text
avatar.vrm              the character, as authored
walk.vrma               the motion, as authored
character_walk.usda     the retarget result for one character
shot.usda               placement and binding
```

Who authors that binding layer, and in what vocabulary, is open
([roadmap/backlog.md](../roadmap/backlog.md#vrm-motion-open-questions)); the
generic half of the question is `usd-motion-plugins`' `Bindings` prim
(USD-O5 in its USD_MAPPING.md).

## 3. What `.vrma` import authors

`usdVrmaFileFormat` accepts a GLB with `VRMC_vrm_animation` `specVersion`
`"1.0"`, reads the first glTF animation, and authors an avatar-independent
semantic clip:

```text
/Animation                 Scope, default prim; customData `vrma`
├─ HumanoidSkeleton        UsdSkelSkeleton over semantic joint paths
├─ BodyAnimation           UsdSkelAnimation, bound to HumanoidSkeleton
├─ Expressions/<name>      one prim per expression the clip declares
└─ LookAt                  when the clip declares look-at
```

### 3.1 The skeleton is a canonical semantic skeleton

`HumanoidSkeleton.joints` are semantic paths (`hips`, `hips/spine`,
`hips/spine/chest`, …) in the VRM humanoid order, never the joint paths of any
target avatar. A clip is bound to no avatar here.

### 3.2 Coordinates, channels and time

- Input and output are right-handed, Y-up, metres. glTF node rotations are
  copied as unit quaternions; no avatar front correction is applied to an
  avatar-independent clip.
- Only the hips translation is read as body translation. Translation channels
  on other bones and every scale channel are ignored with a warning.
  `BodyAnimation` still authors a constant identity `scales`, because UsdSkel
  fetches translations, rotations and scales as a unit and `scales` has no
  schema fallback: without it a clip binds and resolves to the rest pose.
- Every channel is evaluated at the union of all channels' key times, and the
  stage uses 30 time codes per second (`seconds * 30`).
- `LINEAR` and `STEP` are read. `CUBICSPLINE` uses its value vertices as a
  linear approximation, with a warning.

`/Animation` carries `customData` under `vrma`: the source format, the VRMA
spec version, the raw extension payload and the root-motion source.

### 3.3 Expressions

Each declared expression becomes `/Animation/Expressions/<name>` carrying
`vrm:expressionName`, `vrm:expressionType` and `vrm:expressionWeight`. VRMA
animates an expression as the translation X of a node.

- **Nothing is expanded.** Which morph targets and material colours an
  expression drives is the avatar's property; a clip bound to no avatar cannot
  know it. Resolving is §5's.
- **Nothing is clamped.** A weight outside `[0, 1]` is carried verbatim with
  `VRMA109`: correcting it here would hide the authoring tool from whoever
  reads the clip. The clamp belongs to whoever applies the weight (§5).
- **An unreported weight is not a zero.** A channel → time samples; no channel
  but a node transform → one default value; neither → no
  `vrm:expressionWeight` at all.
- **`vrm:expressionName` is the join key, never the prim name.** Both the clip
  and the avatar (`VrmExpressionAPI`, where a VRM 0.x preset carries the VRM
  1.0 name it migrates to) author it, and each side sanitizes prim names with
  its own table, so names outside ASCII differ by prim path.

The attributes are namespaced rather than a typed schema. Applying a
`VrmAnimationExpressionAPI` later would change no path and no attribute name.

### 3.4 Look-at

`/Animation/LookAt` carries `vrm:lookAtOffsetFromHeadBone` (uniform: a
measurement of the source rig, constant in a clip) and `vrm:lookAtTarget`, a
**point**, never a direction: a direction needs a head, and a head is a rig's.

- **A target is placed where the file put it.** The ancestors' stated
  transforms are composed into it; an ancestor the clip itself animates is
  warned about (`VRMA114`) and not evaluated.
- **A clip says one of three things.** A channel → time samples; the file
  places the node and nothing animates it → one default; nothing places it →
  no `vrm:lookAtTarget`. A clip with no `lookAt` block gets no prim, which is
  a fourth statement.
- **An unusable node costs the target, not the declaration.** A missing node
  (`VRMA110`) or one a bone or expression already drives (`VRMA111`) still
  authors the prim and the offset.
- A missing `offsetFromHeadBone` is read as zero with `VRMA112`.

### 3.5 What the plugin does not do

`usdVrmaFileFormat` does not search for or bind to a target avatar, correct
proportions or rest poses, expand expressions into blend shapes, apply look-at
to eyes or neck, simulate spring bones, or blend poses.

## 4. Applying motion to a VRM rig

The retarget is `usd-motion-plugins`' `motionRetarget`, consumed as a package
([RETARGETING_POLICY.md](https://github.com/animu-sphere/usd-motion-plugins/blob/main/docs/design/RETARGETING_POLICY.md)).
What a VRM rig adds to it is `vrmRig`, and it adds three things.

### 4.1 The humanoid binding

A human bone drives a target joint only through an explicit binding: the
avatar's `vrm:humanBones:<bone>` tokens on `VrmHumanoidAPI`, or a
`humanBone -> joint token` map a caller supplies (`--humanoid-map`). Joint
names are never matched heuristically.

### 4.2 The required bones are VRM 1.0's

`motionRetarget` holds no required set; every caller here passes VRM 1.0's
(`vrmRig::GetRequiredBones`). A required bone the rig does not bind is
`MOTION_RETARGET_MISSING_REQUIRED_BONE` from `DiagnoseRig`, before any clip,
and the retarget proceeds without it. An optional bone — eyes, jaw, toes,
shoulders, `upperChest`, fingers — missing is not a missing bone.

### 4.3 Where the two callers part

`motion_retarget` and `execVrm` agree on every value (§7.3), and differ in one
place by design: a map with **two bones on one joint**, or a binding to an
index the rig lacks, is warned about and used by the tool, and **refused** by
`vrm.computeHumanoidMap`, which names every bone on the joint. A computation
cannot return a warning beside a value. No producer authors either case.

## 5. Expressions on a rig

`vrmRig`'s `ExpressionResolver` turns one sample's `MotionChannelSet` into
blend-shape weights and material colours for one avatar. `ExpressionRig` holds
what the avatar declared, keyed by `vrm:expressionName`; the caller reads the
binds off the stage and hands plain values in.

**A resolve is an arbitration, not a sum.** Two expressions that bind
*different* targets still fight when those targets displace the same vertices,
and nothing in the weights shows it
([issue #170](https://github.com/animu-sphere/usd-vrm-plugins/issues/170)).
VRM 1.0's `overrideBlink`, `overrideLookAt` and `overrideMouth`
(`VrmExpressionAPI` builtins) are the specification's mechanism for it, and
they are applied here, per sample, because an override is a statement one
expression makes about the others. Three rules are measured rather than
assumed:

1. **An expression that resolves to zero overrides nothing**, so the rate is
   read off the resolved weight, not the reported one. Each category takes the
   largest rate any expression asked for, so two overrides do not suppress
   twice.
2. **A binary expression is rounded again after a partial suppression**:
   `isBinary` says the rig has no half-shut eyelid to land on.
3. **The arbitration is a single pass.** An expression another override
   suppressed still overrides its own category; cascading would make the
   answer depend on the order categories are settled in.

A rig that aims its eyes with expressions gets its gaze arbitrated by the same
rule, since those weights are folded into the sample before the resolve.

## 6. Look-at on a rig

`vrmRig`'s `LookAtEvaluator` turns a clip's target point into eye rotations or
expression weights against one avatar's own look-at configuration — its type,
its eye joints and its range-map curves, authored by the importer under
`/Asset/rig/LookAt`. A gaze joins to nothing but a head, so it needs no key.
Carry what the producer said; resolve where the rig is.

## 7. The bake and `execVrm`

### 7.1 `motion_retarget`

The stage half of a retarget: it reads the avatar and the clip, bakes a
`UsdSkelAnimation` in the rig's joint order, and binds it with
`skel:animationSource` on an **override** of the referenced skeleton, so the
avatar keeps owning its rig. Expressions are authored as `blendShapes` /
`blendShapeWeights` on the same animation (the morph-target half only: a
material colour is a material input, not a skeletal one).

- **It states each joint's rest scale, constant over the clip.** UsdSkel takes
  an animated joint's local transform from the animation whole, so the
  `scales` a bake authors *are* the rig's scales (the scale rule is
  RETARGETING_POLICY §6.1's).
- **It raises the caller's codes**: `MOTION_RETARGET_NON_UNIT_SCALE` for a
  clip that animates scale, `MOTION_RETARGET_TIME_RANGE_DERIVED` for a one-pose
  clip placed at the stage's start, and
  `MOTION_RETARGET_OUTPUT_COLLIDES_WITH_INPUT` for an output naming an input.
  `execVrm` reads no clip `scales` and never raises the first.

**Exit codes** name the input at fault
([`ExitCode.h`](../../tools/motionRetarget/src/ExitCode.h)). A warning never
changes the code.

| Code | Meaning | What fixes it |
| --- | --- | --- |
| `0` | success, with or without diagnostics | — |
| `1` | invalid user input: an option, a path with no regular file behind it, the `--humanoid-map` file, a prim or joint an option names and the stage lacks, contradicting arguments, an `--output` naming an input | the arguments |
| `2` | a clip that opened and is not a semantic humanoid clip | a different clip, or a converter in front of this tool |
| `3` | a layer OpenUSD would not open: no file format claims it, or the format refused it; the line names the bundle a `.vrm` or `.vrma` needs | the environment or the file |
| `4` | an avatar the retarget cannot bake onto: no `defaultPrim`, no skeleton, a skeleton outside it, no joints, no humanoid mapping | the avatar, or `--humanoid-map` / `--skeleton` |
| `5` | creating, authoring or saving the output failed | the output location |
| `6` | reserved for OpenExec evaluation; never returned | — |

### 7.2 `execVrm`

OpenExec computations applying VRM semantics to a target rig. Each is a
**wrapper over one library call** — `motionRetarget` or `vrmRig` — and never a
second implementation; a node that cannot be written as one is a finding about
the library's API.

- It reads the schema contract from the stage (`VrmHumanoidAPI` **applied**),
  never the importer's private API or canonical model.
- It evaluates an immutable snapshot and performs no I/O; `execVrm_boundaries`
  checks it (the rule is `usd-motion-plugins`' design policy §21).
- `vrm.computeRigDiagnostics` is the rig's report and
  `vrm.computeRetargetDiagnostics` the rig's followed by one sample's, so
  merging the second over a clip's keys, in order, gives the clip's list.
- A stage evaluated by exec states which clip drives which rig
  (`vrm:retarget:sourceSkeleton`) and where its root lands (the four
  `vrm:retarget:*` statements); what a stage states for a computation is
  EXEC_CONTRACT §5's.

The computation list is the [bundle README](../../plugins/execVrm/README.md)'s.
Expression and look-at computations are the [ExecIr track](../roadmap/execir-track.md)'s.

### 7.3 Parity with the bake

`execVrm` and `motion_retarget` are two implementations of one retarget, and
the parity harness (`tests/parity/`) holds them equal. It compares the arrays a
bake authors, not the pose:

- **at the bake's own time samples**, paired by index with the clip's keys;
  the distance between the two instants is compared separately, as placement;
- **joint by joint in the rig's order**; joints and scales must be equal
  outright;
- **each rotation and translation classified, never widened**: exact, a
  quaternion's sign only, a rounding within `MotionTolerance`, or a divergence;
- **the diagnostics as whole lines, in order**, the exec side merged over the
  keys against the tool's lines whose code the library raises. A caller-raised
  code is reported beside the comparison, not in it.

Only a divergence, a refusal, a shape mismatch or differing diagnostics fails.
The harness drives exec through `tests/parity/ExecDriver`, which follows
EXEC_CONTRACT §3 and raises its §4 codes; it is test support until a second
caller needs a driver (EX-O1 there). The measured result is the
[parity report](../reports/openusd/26.08-openexec-parity.md).

The executable statement of §4 is the hand-authored design triplet under
[`tests/motion/fixtures/design_triplet/`](../../tests/motion/fixtures/design_triplet/):
`canonical_walk.usda` + `avatar.usda` must produce `expected_retargeted.usda`.

## 8. `ExecIr` is optional, never a prerequisite

`ExecIr` is OpenUSD's general-purpose invertible-rig machinery. `execVrm` may
use it rather than reimplement FK, controllers or switching, through an adapter
that is not load-bearing:

```text
UsdSkel / VRM semantics  ↕  ExecIr adapter  ↕  ExecIr representation
```

Forbidden: the importer authoring `ExecIr` prims as a requirement; `vrmRig`,
or any consumed motion library, depending on `ExecIr`; and `ExecIr` as the
canonical motion contract. The last has the lasting cost: `ExecIr` is per-prim
scalar avars in world space, the canonical contract quaternion arrays in
joint-local space, and upstream still documents it as experimental. The plan
is [roadmap/execir-track.md](../roadmap/execir-track.md).

## 9. Decisions

1. `.vrm` and `.vrma` are separate file-format plugins.
2. VRM and VRMA compose by reference, not `subLayer`.
3. A third binding / assembly layer relates them.
4. The VRMA original and a retargeted animation stay separate.
5. File-format plugins never evaluate, simulate or retarget.
6. A VRM rig's semantics — the humanoid binding, the required bones,
   expression arbitration, look-at — are applied by `vrmRig` over the consumed
   generic retarget, never by a second retarget.
7. `ExecIr` is an optional adapter, never the canonical motion contract and
   never a prerequisite.
8. An upstream display limitation bounds a display claim, not the motion
   pipeline: OpenUSD 26.08 cannot register a `UsdSkel` exec imaging adapter.
