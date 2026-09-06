# execMotion

Vendor-neutral OpenExec computations over canonical motion. Workspace Phase 8 /
Motion Phase E; the plan is
[docs/roadmap/openexec-foundation.md](../../docs/roadmap/openexec-foundation.md)
§6, P0-4.

**This is the foundation, not the layer.** It registers one value type and two
computations:

| Computation | Provider | Result |
| --- | --- | --- |
| `motion.identityPose` | a `UsdSkelAnimation` prim | the identity `motion::HumanoidPose` over the canonical bones the clip's `joints` name |
| `motion.sampleAnimation` | a `UsdSkelAnimation` prim | the pose the clip states **at the frame the system is evaluating**, stamped in seconds |

`motion.identityPose` is the mechanism at its weakest possible value and it
stays: with no algorithm behind it, a wrong answer there can only be a wrong
mechanism, which is what makes every later failure attributable.

The remaining nodes — `motion.filterPose`, `motion.extractRootMotion`,
`motion.interpolatePose`, `motion.blendPoses` — come next, in that order, over
`motionRuntime`. `blendPoses` is last because it is the one that wants two
inputs, and 26.08's builtin `computeValue` forwards across exactly one
connection.

## A clip has to state the rate its frames are counted at

`motion::HumanoidPose::timestamp` is **seconds**; an OpenExec computation is
handed a **frame**; and the rate between them is stage metadata a computation
cannot reach — `Stage().Metadata<double>(timeCodesPerSecond)` is accepted, is
not refused even with `.Required()`, and still yields no value
([the mechanism report](../../docs/reports/openusd/26.08-openexec-mechanism.md)
§5). That left two places the conversion could happen, and the choice is this
bundle's load-bearing signature decision:

- **inside the graph**, from an authored attribute a computation can read; or
- **outside it**, applied by whoever holds the stage after the pose crosses back
  out.

It is the first, because the second breaks the next node rather than this one.
`motion.filterPose` wraps `motion::PoseFilter`, whose whole point is that its
cutoff is frame-rate independent — it derives each step's weight from the time
elapsed between poses. A pose stamped after it leaves the graph reaches that
filter as time zero, so the rate has to be *in* the graph.

So `motion.sampleAnimation` reads `motion:timeCodesPerSecond` off the clip, as a
`.Required()` input, and **a clip that states none is refused rather than
stamped**: the callback posts an error and returns an empty pose, because
`timestamp` is a plain double with no absent state — a pose carrying a guessed
second is indistinguishable downstream from one carrying a measured second, and
an empty pose is not.

The attribute duplicates the layer's own `timeCodesPerSecond` metadatum, and the
duplication is a **shim for an upstream gap, not a format**. Nothing in this
repository authors it yet; the fixtures do, and the producer half is named in
[the plan](../../docs/roadmap/openexec-foundation.md) §9. If exec ever delivers
stage metadata to a callback, this input goes away.

## Nothing here interpolates

An exec input arrives **already resolved at the evaluated time**, so a frame
between two keys is USD's answer and not this bundle's. That is why
`motion.sampleAnimation` is *not* a wrapper over `motion::SampleAnimation`,
which is handed a whole `HumanoidAnimation` and performs its own hold-at-the-
edges lookup. The two are compared at P0-6 parity rather than assumed equal; what
26.08 does between keys is measured in
[the sampling report](../../docs/reports/openusd/26.08-openexec-sampling.md).

## What this bundle may not do

- **No stage authoring.** A computation produces a value; USD animation is
  authored on bake, record or publish, never per evaluated frame (motion policy
  §12.1).
- **No I/O inside a computation.** No socket, device poll, file watch, wall
  clock, mutable global or private thread pool. Receiving belongs to an adapter
  and buffering to `motionRuntime`; a computation evaluates an immutable
  snapshot (motion policy §11.4).
- **No second algorithm.** Every computation is a thin wrapper over
  `motionCore` / `motionRuntime`. The decisions live in functions over plain
  values ([`src/ExecMotionPose.h`](src/ExecMotionPose.h)) and the registration TU
  only marshals inputs into them. Where a node has no library call to wrap —
  `motion.sampleAnimation` has none, because reading a clip into a canonical pose
  lives in `tools/motionRetarget`'s `StageIo.cpp` and not in a library — that is
  recorded as a finding for
  [boundary consolidation](../../docs/roadmap/boundary-consolidation.md), which
  is the track scheduled to act on exactly this.
- **No VRM, and no product name.** Humanoid retarget, expressions, look-at and
  the schema contract are `execVrm`'s; mocopi, VMC and the rest are adapters'
  ([WORKSPACE.md](../../docs/architecture/WORKSPACE.md) §1, §2).

## One schema, one plugin

26.08 keys its `Info.Exec.Schemas` metadata by schema type and allows **exactly
one** plugin to declare a given schema. A second declarer gets a coding error at
metadata read and its computations for that schema are never registered — which
surfaces later as "computation not found", not as a load failure.

So `UsdSkelAnimation` is this bundle's, and `execVrm` reaches an animation
through an input accessor rather than by registering on it. The measurement is
[docs/reports/openusd/26.08-openexec-mechanism.md](../../docs/reports/openusd/26.08-openexec-mechanism.md)
§2 and the rule is [WORKSPACE.md](../../docs/architecture/WORKSPACE.md) §2.

## Tests

| Test | What it holds |
| --- | --- |
| `execMotion_pose` | the seam, with no stage, no system and no request |
| `execMotion_mechanism` | discovery through `plugInfo.json`, request compile, compute, an unchanged recompute, an authored-value invalidation, a time change reporting nothing for a time-independent value key, an explicit invalidation, and the shape an unregistered computation presents as |
| `execMotion_sample` | the same request at four times — the default time code, and frames 0, 100 and 50 — a value key being reported to the time callback even over a clip that holds still (which is what makes `computeTime` a per-frame recompute), and a clip with no rate being refused rather than stamped |

All three carry the CTest label `motion.openexec`.
