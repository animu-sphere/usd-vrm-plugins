# execMotion

Vendor-neutral OpenExec computations over canonical motion. Workspace Phase 8 /
Motion Phase E; the plan is
[docs/roadmap/openexec-foundation.md](../../docs/roadmap/openexec-foundation.md)
§6, P0-4.

**This is the foundation, not the layer.** It registers four value types and
seven computations:

| Computation | Provider | Result |
| --- | --- | --- |
| `motion.identityPose` | a `UsdSkelAnimation` prim | the identity `motion::HumanoidPose` over the canonical bones the clip's `joints` name |
| `motion.sampleAnimation` | a `UsdSkelAnimation` prim | the pose the clip states **at the frame the system is evaluating**, stamped in seconds |
| `motion.priorPose` | a `UsdSkelAnimation` prim | the sampled pose, forwarded — the value key a driver **overrides** with the previous frame's answer |
| `motion.filterPose` | a `UsdSkelAnimation` prim | one `motion::PoseFilter` step from `motion.priorPose` toward the sampled pose |
| `motion.extractRootMotion` | a `UsdSkelAnimation` prim | the `motion::RootMotion` the sampled pose states, under the clip's intake policy |
| `motion.poseHistory` | a `UsdSkelAnimation` prim | the sampled pose as a one-sample `motion::HumanoidAnimation` — the value key a driver **overrides** with its buffer's snapshot |
| `motion.interpolatePose` | a `UsdSkelAnimation` prim | the `motion::PoseSampleResult` `motion::ClipSource` answers over `motion.poseHistory`, **at the instant the system is evaluating** |

`motion.identityPose` is the mechanism at its weakest possible value and it
stays: with no algorithm behind it, a wrong answer there can only be a wrong
mechanism, which is what makes every later failure attributable.

The remaining node — `motion.blendPoses` — comes next, over `motionRuntime`. It
is last because it is the one that wants two poses from two places, and 26.08's
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
stamped**: the callback posts an error and sets **no value at all**, because
`timestamp` is a plain double with no absent state — a pose carrying a guessed
second is indistinguishable downstream from one carrying a measured second.

A refusal setting no value, rather than a default-constructed one, is
[the bundle's one refusal shape](#how-a-computation-refuses) and is not specific
to the rate.

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

## What a clip may state about its root

`motion.extractRootMotion` answers with a `motion::RootMotion` — the bundle's
**second** registered value type, and the first result here that is not a pose.
One attribute decides it:

| `motion:root:intake` | What comes back |
| --- | --- |
| absent | `motion::LiveCaptureConfig`'s own default, which is `deriveVelocity` |
| `passthrough` | the root the pose carries, unchanged |
| `ignore` | a **cleared** `motion::RootMotion` — every presence flag false |
| `deriveVelocity` | `passthrough`, plus a linear velocity where the pose has a position, reported none, and there is a prior position and time between the two |
| anything else | **no value at all**, and a posted error naming the computation ([how a computation refuses](#how-a-computation-refuses)) |

`ignore` **clears rather than zeroes**, and that is what the presence flags are
for: a cleared root says *this clip does not place the body*, and a zero position
with `hasPosition` set says *the body is at the origin*. Only the first leaves a
rig its own placement.

That is also why the last row sets **no value** rather than a cleared one: a
cleared `motion::RootMotion` is `ignore`'s answer bit for bit, so a refusal
producing one would be a deliberate `ignore` as far as any consumer could tell.
This node is the reason the bundle states its refusal shape
[once, below](#how-a-computation-refuses).

The last row is the other half of the rule above, and the two together are one
rule rather than two moods. An **absent** attribute is a clip that said nothing,
and it gets the library's behaviour. A token that spells no policy is a clip that
*stated* something this layer cannot honour, and defaulting there would hand a
misspelled `ignore` the root motion it asked not to have. So: default where an
absent value selects the library's documented behaviour, refuse where it would
produce a number no consumer can tell from a measured one, and never default a
value the clip stated.

The node reads `motion.sampleAnimation` rather than `motion.filterPose`, and that
is the **library's** ordering rather than a preference: `LiveCaptureSource`
conditions the root of a frame as it arrived and smooths afterwards, so a node
differentiating a filtered position would answer a different question from the
one `motionRuntime` answers — and P0-6 parity would have to explain the
difference instead of measuring it.

Its prior pose is the same `motion.priorPose` the filter takes, so **one override
drives both**: a driver holds one previous answer per prim and substitutes it
once per frame, whatever the request happens to contain
([the root-motion report](../../docs/reports/openusd/26.08-openexec-root-motion.md) §4).

### Where the rule it applies lives

`motion::RootMotionIntake` is the library's enum and the rule it selects is the
library's — written down in the
[motion contract](../../docs/design/MOTION_CONTRACT.md) and implemented in
`LiveCaptureSource::_Condition`, which is **private**, on a class that is a
capture *session*: a buffer, a filter, held-bone state and statistics.

The composition idiom `motion.filterPose` uses was tried here and **produces a
wrong answer**. Two poses at the same instant are a reseed for
`motion::PoseFilter::Apply` and a *refusal* for `LiveCaptureSource::Push` — so a
composed answer would be the previous frame's root, and an un-overridden node
evaluates exactly that case. The three lines are therefore in the seam, matched
condition for condition and asserted against their definition rather than against
the library, and the ask is
[boundary consolidation](../../docs/roadmap/boundary-consolidation.md)'s:
`ConditionRootMotion(prior, pose, intake)` as a free function, so the rule has
one implementation again.

## A history is handed in, and sampled whole

`motion.interpolatePose` answers `IMotionSource`'s one question — *what is the
pose at this evaluation time?* — of a **snapshot**: a timestamped history of
poses, the "immutable snapshot" motion policy §11.4 puts between a live source's
buffer and every computation. A computation never reaches for a buffer, so the
buffer's samples reach the graph the one way a value the scene does not state
can: as an override, on `motion.poseHistory`.

That makes two keys here a driver fills, and they are different kinds of thing:

| Key | What a driver puts there | What it is |
| --- | --- | --- |
| `motion.priorPose` | the previous frame's answer | the graph's own output, **fed back** — the state a recurrence needs |
| `motion.poseHistory` | its buffer's samples, as a `motion::HumanoidAnimation` | the source's input, **handed in** |

So a driver holds **one previous answer and one snapshot per prim**, and may
hand both over in one `ComputeWithOverrides` — each reaches only the nodes that
depend on it
([the interpolation report](../../docs/reports/openusd/26.08-openexec-interpolation.md) §2).

Un-overridden, the history is the clip's own pose as a history of one, sampled
at its own instant, and the node **is** `motion.sampleAnimation` at every frame
on the timeline — the same pass-through the filter has, special-cased in
neither.

| History | What comes back |
| --- | --- |
| two samples bracketing the instant | `Sampled` — `motion::LerpPose` between them, stamped at the instant |
| samples ending before the instant, or starting after it | `Held` — the nearer boundary sample, **stamped at the instant**, with the lag that says how far off it is |
| empty | `Unavailable`, carrying no pose — an **answer** |
| a timestamp that is not finite, or timestamps decreasing somewhere | **no value at all**, and a posted error naming the computation |
| any, while the system is at the **default time code** | **no value at all**, and a posted error — there is no instant to sample at |

**The answer is the library's `motion::PoseSampleResult` whole, and not a bare
pose.** `ClipSource` stamps a hold at the requested instant exactly as it stamps
a sample, so a pose alone cannot say whether the source reached that instant —
and a source that has stopped delivering keeps answering `Held` forever. The
status is part of the answer ([motion contract](../../docs/design/MOTION_CONTRACT.md),
live-capture semantics), and a wrapper does not get to drop a field of the thing
it wraps. Registering the type needed an exact `operator==` on it, which
`motionRuntime` now carries.

**An empty history is an answer, not a refusal**, and that is the bundle's
refusal rule applied rather than bent: this is the first result type here with an
absent state of its own, so `Unavailable` cannot be mistaken for a measurement
and there is nothing for a refusal to protect. A history out of time order, or
carrying a timestamp that is not finite, is the one refusal about a history — the
library's binary search would bracket the instant with samples that do not
surround it, and every comparison with a NaN is false, so an ordering check alone
would let one through. Repeated timestamps are not refused: at or past the end of
the history the **last** of a repeated pair holds, elsewhere the first of a pair
or an interpolation between neighbours answers, and every answer is a sample
somebody measured.

**The instant is `motion.sampleAnimation`'s timestamp**, the seconds that node
already converted the frame into — not the rate a second time. So the conversion
has one home, and a clip with no rate is refused here by propagation, **whether
or not a history was supplied**: with no rate there is no instant to sample
anything at.

**And at the default time code there is no instant either**, which the stamp
cannot say — the sampler stamps 0.0 there, harmlessly for a clip. A history lives
entirely on a timeline, and sampling a buffer at a guessed 0.0 would answer a
believable `Held`. So the node reads `computeTime` for that one fact and refuses
there, overridden or not. That is also where every request is armed, so a driver
names an instant with `ChangeTime` before it expects a history sampled
([the interpolation report](../../docs/reports/openusd/26.08-openexec-interpolation.md) §5).

**A wrongly typed override answers plausibly.** 26.08 drops an override whose
type is not the key's — an empty `VtValue` included — posts a coding error
naming the key, and computes the key's *ordinary* value, so this node answers the
clip's pose as though nobody had overridden anything
([the interpolation report](../../docs/reports/openusd/26.08-openexec-interpolation.md) §4).
The bundle cannot refuse it, because the substitution is rejected before any
callback runs. A driver treats a coding error around `ComputeWithOverrides` as a
failed frame.

### What the wrapper costs

This node is one library call and nothing else: `motion::ClipSource`,
constructed over the history, asked `Sample` once. But `ClipSource` **owns** the
animation it serves, so every evaluation copies the history into it, and the
free function beneath it — `motion::SampleAnimation`, which would take the
history by reference — returns the pose without the status. The ask for
[boundary consolidation](../../docs/roadmap/boundary-consolidation.md) is a free
`SampleClip(animation, t) -> PoseSampleResult` that `ClipSource::Sample` calls,
with the time-order precondition `SampleAnimation` relies on — and nothing states
— written on it.

## How a computation refuses

**By setting no value at all** — `VdfContext::SetEmptyOutput`, after posting a
`TF_RUNTIME_ERROR` that names the computation. Never by returning a
default-constructed result.

It is one rule, and it is the same one the rate's refusal was written for: *an
answer nobody can tell from a refusal is worse than no answer.* A
default-constructed result fails that test for every type this bundle produces:

| Type | A default-constructed value is also… |
| --- | --- |
| `motion::HumanoidPose` | what a clip whose `joints` name no canonical bone legitimately samples to |
| `motion::RootMotion` | `motion:root:intake = "ignore"`'s own answer, **bit for bit** |
| `motion::HumanoidAnimation` | an empty history — which `motion.interpolatePose` answers, as `Unavailable` |
| `motion::PoseSampleResult` | `Unavailable`: the answer for a history that holds nothing, which is not the same statement as "this history cannot be sampled" |

So a refusal spelled that way would hand a misspelled `passthrough` the exact
behaviour of a deliberate `ignore`, for any consumer not inspecting `TfError`s —
the mirror image of the mistake the intake table above refuses to make. An empty
value is the one shape no computation here ever produces as an *answer*, which
is what makes it the only shape a refusal can take and stay distinguishable.

It costs the value-returning callback form: a callback that may refuse takes
`const VdfContext&`, returns `void`, and calls `SetOutput` on every path that has
an answer. `motion.identityPose` keeps the returning form because it has no
refusal to express, so both forms are live in one bundle.

**A refusal propagates.** A node handed no value refuses in turn, so a clip with
no rate reaches a caller as a refusal at `motion.sampleAnimation`, at
`motion.filterPose` *and* at `motion.interpolatePose`, rather than as a filtered
or interpolated version of a pose nobody sampled. `motion.priorPose` and
`motion.poseHistory` forward the absence for the same reason.

## Nothing here interpolates a clip

An exec input arrives **already resolved at the evaluated time**, so a frame
between two keys of a clip is USD's answer and not this bundle's. That is why
`motion.sampleAnimation` is *not* a wrapper over `motion::SampleAnimation`,
which is handed a whole `HumanoidAnimation` and performs its own hold-at-the-
edges lookup. The two are compared at P0-6 parity rather than assumed equal; what
26.08 does between keys is measured in
[the sampling report](../../docs/reports/openusd/26.08-openexec-sampling.md).

What `motion.interpolatePose` interpolates is a **history a driver hands in**,
which the graph cannot see any other way — and through it the library's sampler
does run inside exec: a driver that supplies a clip's own key poses as the
history evaluates `motion::SampleAnimation`'s rule beside USD's, at the same
instant, in one request.

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
  of two `Apply` calls, `motion.extractRootMotion`'s rule is private to a
  capture session whose composition gives a wrong answer, and
  `motion.interpolatePose`'s status-carrying answer exists only on a source
  object that costs a copy of the history per evaluation — that is recorded as a
  finding for
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
| `execMotion_sample` | the same request at four times — the default time code, and frames 0, 100 and 50 — a value key being reported to the time callback even over a clip that holds still (which is what makes `computeTime` a per-frame recompute), and a clip with no rate being refused rather than stamped, with no value and with the refusal reaching the node downstream |
| `execMotion_filter` | one computation reading another, a value key inherited by a node that declares no `computeTime`, an override reaching every dependent of the key it names and no sibling, and a clip's policy landing on `motion::PoseFilter`'s own weight rather than on this bundle's |
| `execMotion_root` | the bundle's second registered value type coming back beside the first out of one request, a dependent whose result type differs from its input's, a velocity that exists nowhere in the clip, one override driving both recurrences in a single call, and an intake token that names no policy being refused **with no value** where an absent one is defaulted and a deliberate `ignore` answers with a cleared root |
| `execMotion_interpolate` | the third and fourth registered value types, a driver's history overriding a key whose type is not a pose and being sampled bracketed, held and empty — `Unavailable` as an answer, a decreasing history as the one refusal — two overrides of two keys in one call each reaching only their own dependents, a wrongly typed and an empty override being **dropped** by exec in favour of the key's ordinary value, a history refused at the default time code even when one was supplied, and a clip with no rate refused whether or not a history was supplied |

All six carry the CTest label `motion.openexec`.
