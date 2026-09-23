# vrmRig

What a VRM rig adds to a retarget, and none of it retargets. The retarget
itself — expanding semantic humanoid poses into one rig's joint order,
correcting for the two rigs' rest poses, resolving where root motion lands — is
`usd-motion-plugins`' [`motionRetarget`](https://github.com/animu-sphere/usd-motion-plugins/tree/main/libs/motionRetarget),
consumed here as a published package. This library is the three things that
only a VRM avatar knows:

- **the bones a VRM 1.0 avatar must bind**, which every caller here hands the
  retarget, because `motionRetarget` holds no required set of its own;
- **the clip's expressions**, resolved onto the rig — a named weight becomes the
  blend-shape weights and material colours that name means on this avatar;
- **its gaze** — the place a clip looks at becomes this rig's eye rotations, or
  the four gaze expressions an expression-driven rig aims with instead.

It was the VRM half of `vrmRetarget` until 2026-09-23, when the generic half
left for `usd-motion-plugins` along the line
[WORKSPACE.md §9.5](../../docs/architecture/WORKSPACE.md#95-the-line-through-vrmretarget)
draws; the name went with the half that retargets.

`vrmRig` is a **plain static CMake library**, not a plugin bundle. It has no
`plugInfo.json`, no `openstrata.plugin.yaml`, and — the load-bearing constraint
— **no OpenExec dependency** (motion policy §10.1, §18.12). Its one edge beyond
OpenUSD's value libraries is `motionCore`: the VRM half includes nothing from
the generic half, and [`tests/check_boundaries.py`](tests/check_boundaries.py)
forbids every other `usd-motion-plugins` package so that the line stays drawn.
See [WORKSPACE.md](../../docs/architecture/WORKSPACE.md) §1–2.

## It never opens a stage

The rig arrives as plain values — an `ExpressionRig`, a `LookAtRig` — not as
prims. Reading those off a stage is the caller's job
([`tools/motionRetarget`](../../tools/motionRetarget/) does it for the CLI).
That keeps the library testable without USD composition and usable by a live
source that has no stage at all.

## What it provides

| Header | Contents |
| --- | --- |
| `vrmRig/RequiredBones.h` | `GetRequiredBones` — VRM 1.0's seventeen required humanoid bones, hips first, which a caller passes as `motionRetarget`'s `RetargetOptions::requiredBones` |
| `vrmRig/ExpressionResolver.h` | `MorphTargetBind`, `MaterialColorBind`, `ExpressionDefinition`, `ExpressionRig`, `ExpressionResolver`, `ResolvedExpressions`, `ExpressionDiagnostics` |
| `vrmRig/LookAtEvaluator.h` | `LookAtRangeMap`, `LookAtCurveKey`, `LookAtType`, `LookAtRig`, `ParseLookAtRangeMaps`, `LookAtHead`, `LookAtInput`, `LookAtEvaluator`, `ResolvedLookAt`, `LookAtDiagnostics` |

## Four decisions worth knowing

- **The required set is the caller's statement, and a VRM caller always makes
  it.** The joint vocabulary carries no required-bone rule, so `motionRetarget`
  requires nothing but the hips under `Hips` root motion. A caller that forgets
  this set gets no error, only a rig that lacks a VRM 1.0 bone reported as
  whole — which is why `execVrm` and `motion_retarget` both pass it and their
  suites fail when either stops.
- **An expression joins on its name, and a reported zero is not silence.** The
  key is `vrm:expressionName` — the name the source VRM spelled — because the
  two sides sanitize prim names with private tables and a Japanese or colliding
  name lands differently on each. A name the sample reported at 0 authors its
  targets at 0, because "off now" is a statement; a name the sample never
  reported contributes nothing at all, because an absent name is not a zero
  weight. The `[0, 1]` clamp the `.vrma` reader deliberately withheld is applied
  here, per the specification, and the clamped names are reported.
- **Co-active expressions are arbitrated, not summed.** Two expressions that
  bind *different* morph targets still fight when those targets displace the
  same vertices, and nothing in the weights shows it. So a sample is resolved as
  a whole: every reported name resolves to a weight, then the avatar's own
  `overrideBlink` / `overrideLookAt` / `overrideMouth` settle the blink, look-at
  and mouth categories between those weights — the largest rate any expression
  asked for, since two overrides do not suppress twice — and only the survivors
  reach the binds. An expression the sample resolves to zero overrides nothing,
  which is why the rate is read off the resolved weight rather than the reported
  one; and a binary expression is rounded again after a partial suppression,
  because `isBinary` says the rig has no half-shut eyelid to land on. It is one
  pass, so an expression *another* override suppressed still overrides its own
  category — cascading would make the answer depend on the order the categories
  are settled in, and two expressions overriding each other's categories would
  have none at all. A suppression is named with the expression that caused it,
  and is deliberately not a defect: the avatar asked for it.
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
  `motion::MotionChannelSet` for `lookLeft`, `lookRight`, `lookUp` and
  `lookDown` — which is exactly what `ExpressionResolver` consumes, so a gaze
  reaches the avatar's binds through the path the face already uses rather than
  through a second one. All four names are reported every sample, zeros
  included, for the reason a reported zero is authored above.

Resolving expressions produces values and authors nothing: writing
`blendShapeWeights` onto a stage is the caller's job, and
[`motion_retarget`](../../tools/motionRetarget/README.md) is the caller that
does it — it reads the binds off the avatar, hands them here, and authors what
comes back onto the animation it already binds to the rig.

## Building

It builds as part of the workspace root `CMakeLists.txt`. Standalone:

```bash
cmake -S libs/vrmRig -B build/vrmRig       -DCMAKE_PREFIX_PATH="<usd-install>;<motionCore-install>"
cmake --build build/vrmRig
ctest --test-dir build/vrmRig --output-on-failure
```
