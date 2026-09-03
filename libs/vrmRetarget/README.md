# vrmRetarget

The offline retarget core: it takes a clip of semantic humanoid poses and
expands it into a specific rig's joint order, correcting for the two rigs'
differing rest poses and resolving where root motion lands. It resolves the
clip's *expressions* onto the same rig as well — a named weight becomes the
blend-shape weights and material colours that name means on this avatar — and
its *gaze*: the place a clip looks at becomes this rig's eye rotations, or the
four gaze expressions an expression-driven rig aims with instead.

`vrmRetarget` is a **plain static CMake library**, not a plugin bundle. It has
no `plugInfo.json`, no `openstrata.plugin.yaml`, and — the load-bearing
constraint — **no OpenExec dependency**: the retarget core is complete and
testable before any exec node exists (motion policy §10.1, §18.12), so
`execVrm`'s future `HumanoidRetarget` node is a thin wrapper over this, not a
reimplementation. See [WORKSPACE.md](../../docs/architecture/WORKSPACE.md) §1–2,
enforced by [`tests/check_boundaries.py`](tests/check_boundaries.py).

Workspace Phase: **6b** · Motion Phase: **C** (both land in v0.4.0).

## It never opens a stage

The target rig arrives as plain values — `TargetSkeleton`, a `HumanoidMap`, a
`SourceRestPose` — not as a `UsdSkelSkeleton`. Reading those off a stage is the
caller's job ([`tools/motionRetarget`](../../tools/motionRetarget/) does it for
the CLI). That keeps the core testable without USD composition and usable by a
live source that has no stage at all.

## What it provides

| Header | Contents |
| --- | --- |
| `vrmRetarget/TargetSkeleton.h` | `TargetJoint`, `TargetSkeleton` — joint tokens, parents derived from `a/b/c` joint paths, decomposed rest transforms |
| `vrmRetarget/HumanoidMap.h` | `HumanoidMap` — human bone → target joint index, plus missing-required-bone and duplicate-binding reporting |
| `vrmRetarget/RestPose.h` | `SourceRestPose`, `RestPoseCorrection`, `ComputeRestPoseCorrection` |
| `vrmRetarget/RootMotionPolicy.h` | `RootMotionMode` (`Ignore` / `Hips` / `RootJoint`), `RootMotionOptions`, `ResolveRootTranslation` |
| `vrmRetarget/PoseRetargeter.h` | `PoseRetargeter`, `RetargetedPose`, `RetargetedAnimation`, `RetargetDiagnostics` |
| `vrmRetarget/ExpressionResolver.h` | `MorphTargetBind`, `MaterialColorBind`, `ExpressionDefinition`, `ExpressionRig`, `ExpressionResolver`, `ResolvedExpressions`, `ExpressionDiagnostics` |
| `vrmRetarget/LookAtEvaluator.h` | `LookAtRangeMap`, `LookAtCurveKey`, `LookAtType`, `LookAtRig`, `ParseLookAtRangeMaps`, `LookAtHead`, `LookAtInput`, `LookAtEvaluator`, `ResolvedLookAt`, `LookAtDiagnostics` |

## Five decisions worth knowing

- **Joint names are never guessed.** A binding comes from the avatar's
  `vrm:humanBones:<bone>` or from an explicit map file. Name heuristics are
  exactly the silent mis-retarget this contract exists to prevent, so a bone
  the caller did not bind stays unmapped and is *reported*, not inferred.
- **Rest-pose correction preserves the world delta.** With source rest `S`,
  source parent rest `Sp`, target rest `T`, and target parent rest `Tp`, the
  bone's world rotation away from its own rest is what survives the change of
  rig; the closed form falls out of equating the two deltas, and a unit test
  checks the invariant directly rather than the formula.
- **Root motion carries a delta, not a height.** The hips translation is
  applied relative to each rig's own rest translation, so a clip authored on a
  1.0 m rig drives a 1.6 m one without the avatar snapping to the source's hip
  height. `preserveTargetHeight` drops the vertical component entirely.
- **An expression joins on its name, and a reported zero is not silence.** The
  key is `vrm:expressionName` — the name the source VRM spelled — because the
  two sides sanitize prim names with private tables and a Japanese or colliding
  name lands differently on each. A name the sample reported at 0 authors its
  targets at 0, because "off now" is a statement; a name the sample never
  reported contributes nothing at all, because an absent name is not a zero
  weight. The `[0, 1]` clamp the `.vrma` reader deliberately withheld is applied
  here, per the specification, and the clamped names are reported.
- **A gaze is a point until it meets a rig, and then it is two answers.** A
  clip names a target *point*, because a direction needs a head and where the
  head sits is a property of an avatar. `LookAtEvaluator` is the layer that has
  the avatar: it places the gaze origin at the head plus the rig's
  `offsetFromHeadBone`, measures the aim in the head's own space, and runs it
  through the four range maps VRM 0.x and VRM 1.0 state in two different
  shapes — one value here, because a consumer that branched on the source
  version would be carrying the importer's job. A `bone`-type rig answers with
  eye rotations, the eye on the side the gaze goes to taking the *outer* map and
  the other the *inner* one; an `expression`-type rig answers with
  `motion::ExpressionWeights` for `lookLeft`, `lookRight`, `lookUp` and
  `lookDown` — which is exactly what `ExpressionResolver` consumes, so a gaze
  reaches the avatar's binds through the path the face already uses rather than
  through a second one. All four names are reported every sample, zeros
  included, for the reason a reported zero is authored above.

Unmapped joints keep their rest transform, so a clip that drives only part of a
rig leaves the rest of it alone instead of collapsing it to identity. Resolving
expressions produces values and authors nothing: writing `blendShapeWeights`
onto a stage is the caller's job, and
[`motion_retarget`](../../tools/motionRetarget/README.md) is the caller that
does it — it reads the binds off the avatar, hands them here, and authors what
comes back onto the animation it already binds to the rig.

## Building

It builds as part of the workspace root `CMakeLists.txt`. Standalone:

```bash
cmake -S libs/vrmRetarget -B build/vrmRetarget \
      -DCMAKE_PREFIX_PATH="<usd-install>;<motionCore-install>;<motionRuntime-install>"
cmake --build build/vrmRetarget
ctest --test-dir build/vrmRetarget --output-on-failure
```
