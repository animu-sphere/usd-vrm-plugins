# execMotion

Vendor-neutral OpenExec computations over canonical motion. Workspace Phase 8 /
Motion Phase E; the plan is
[docs/roadmap/openexec-foundation.md](../../docs/roadmap/openexec-foundation.md)
§6, P0-4.

**This is the foundation, not the layer.** It registers one value type and four
computations:

| Computation | Provider | Result |
| --- | --- | --- |
| `motion.identityPose` | a `UsdSkelAnimation` prim | the identity `motion::HumanoidPose` over the canonical bones the clip's `joints` name |
| `motion.sampleAnimation` | a `UsdSkelAnimation` prim | the pose the clip states **at the frame the system is evaluating**, stamped in seconds |
| `motion.priorPose` | a `UsdSkelAnimation` prim | the sampled pose, forwarded — the value key a driver **overrides** with the previous frame's answer |
| `motion.filterPose` | a `UsdSkelAnimation` prim | one `motion::PoseFilter` step from `motion.priorPose` toward the sampled pose |

`motion.identityPose` is the mechanism at its weakest possible value and it
stays: with no algorithm behind it, a wrong answer there can only be a wrong
mechanism, which is what makes every later failure attributable.

The remaining nodes — `motion.extractRootMotion`, `motion.interpolatePose`,
`motion.blendPoses` — come next, in that order, over `motionRuntime`.
`blendPoses` is last because it is the one that wants two inputs, and 26.08's
builtin `computeValue` forwards across exactly one connection.

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

## A filter is a recurrence, and the graph has no history

`motion::PoseFilter` derives each step's weight from the seconds elapsed since
the pose before it. A computation is handed exactly one time and no way to reach
another, so the state that filter normally keeps has nowhere in the graph to
live: a static in the callback is the mutable state exec's cache-safety contract
and the motion policy both forbid, and an authored "previous pose" attribute
would put a derived value into the scene.

So it is **passed in**. `motion.priorPose` is a computation whose ordinary value
is the clip's own pose at the evaluated frame, and whose purpose is to be
replaced:

```cpp
// the driver's loop -- one evaluation, one substituted value
ExecUsdValueOverrideVector overrides;
overrides.push_back({ExecUsdValueKey(clip, TfToken("motion.priorPose")),
                     VtValue(previousAnswer)});
ExecUsdCacheView view = system.ComputeWithOverrides(request,
                                                    std::move(overrides));
previousAnswer = view.Get(0).UncheckedGet<motion::HumanoidPose>();
```

**Exec does the step and the driver owns the sequence.** For a live source that
is where the state already is — `motionRuntime`'s pose buffer holds it — and for
a clip it is the loop above. Exec never sees two frames at once.

Two things a driver has to know, because no computation declares them
([the filtering report](../../docs/reports/openusd/26.08-openexec-filtering.md)
§4, §5): a request is armed by its **first** `Compute`, so a `ChangeTime` before
one reaches no callback at all; and an override applies to a single
`ComputeWithOverrides` and does not survive it.

Un-overridden, `motion.filterPose` **is** `motion.sampleAnimation`: the filter
smooths the clip against itself, the elapsed time is zero, and `PoseFilter`
reseeds and returns its argument. Nothing here special-cases that.

### What the round trip costs

A pose is not the whole of a filter's state. `motion::PoseFilter` retains a
state strictly richer than what it returns — a bone a pose does not report keeps
its stored rotation *in the state* and stays out of the *result*, so a brief
dropout does not restart that bone's history — and only a result can travel back
in as the next prior pose. So a bone returning after a missing frame is passed
through here, where the streaming filter would slerp it from what it kept: **45°
against 23.8°** in the case `execMotion_pose` pins, in both directions.

That costs nothing for a clip, whose `joints` are `uniform` so no bone ever drops
out, and it is real for a live source — which is what this node is aimed at.
Reproducing the carry-forward rule in this bundle would be the second algorithm
the wrapper rule forbids, so the difference is recorded instead, and the ask on
`motionRuntime` is sharpened by it: a one-step entry point has to hand back the
**state** as well as the result. P0-6 parity compares the two.

## What a clip may state about smoothing

| Attribute | Type | Absent means |
| --- | --- | --- |
| `motion:filter:cutoffHz` | `float` | `PoseFilter::Options`' own 6 Hz. Non-positive disables smoothing, which is the library's documented pass-through. |
| `motion:filter:rootPosition` | `bool` | the library's own `true` |
| `motion:filter:rootOrientation` | `bool` | the library's own `true`, and inert over a clip: a `UsdSkelAnimation` states no root orientation, so the pose carries none. It is here because the wrapper carries the whole of `Options`, and a live source does report one. |

An absent field is **left at the library's default and not at one this bundle
picked**, which is the opposite of what the rate above gets, and the difference
is what each absent value costs. A missing rate produces a *second* no consumer
can tell from a measured one. A missing cutoff selects the behaviour every other
caller of `motion::PoseFilter` already gets.

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
  lives in `tools/motionRetarget`'s `StageIo.cpp` and not in a library — or where
  it has one in the wrong shape — `motion::PoseFilter` is a streaming class with
  no one-step entry point, so `motion.filterPose` composes a seed and a step out
  of two `Apply` calls — that is recorded as a finding for
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
| `execMotion_pose` | the seam, with no stage, no system and no request — including what the round trip through a pose costs, measured against `motion::PoseFilter` driven as the streaming operator it is |
| `execMotion_mechanism` | discovery through `plugInfo.json`, request compile, compute, an unchanged recompute, an authored-value invalidation, a time change reporting nothing for a time-independent value key, an explicit invalidation, and the shape an unregistered computation presents as |
| `execMotion_sample` | the same request at four times — the default time code, and frames 0, 100 and 50 — a value key being reported to the time callback even over a clip that holds still (which is what makes `computeTime` a per-frame recompute), and a clip with no rate being refused rather than stamped |
| `execMotion_filter` | one computation reading another, a value key inherited by a node that declares no `computeTime`, an override reaching every dependent of the key it names and no sibling, and a clip's policy landing on `motion::PoseFilter`'s own weight rather than on this bundle's |

All four carry the CTest label `motion.openexec`.
