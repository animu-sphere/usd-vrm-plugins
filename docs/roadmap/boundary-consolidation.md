# Boundary consolidation — fix the shape before the inputs multiply

**Status:** ⬜ not started · **Target:** after the OpenExec foundation ·
**Policy:** [design/INTEGRATION_SCOPE_POLICY.md](../design/INTEGRATION_SCOPE_POLICY.md) ·
**Added:** 2026-09-06

Nine identities, three input categories and twelve packages exist. Every
boundary between them was designed on its own terms, at the moment it was
needed, and each is defensible on its own. **Nothing states them as one set**,
so the tenth identity re-argues them and the fourth input category answers them
again.

This track does not add a format, an adapter, a node or a package. It writes the
agreements down, gives the mechanical ones a check, and settles the three
decisions the workspace has been carrying as open — after which a component can
leave the repository at all
([the split track](motion-foundation-split.md)).

## 1. Why it is second, behind OpenExec

The 2026-08-29 order put the producer contracts *in front of* OpenExec, on the
argument that a compute layer over moving contracts evaluates a boundary twice.
The 2026-09-06 re-order inverts that pair, and the argument that makes it
coherent is the one the packaging track already made once:

> A boundary nothing outside has consumed is a boundary nobody has measured.

`execMotion` and `execVrm` are specified as **thin wrappers** — every node is a
call into `motionRuntime` or `vrmRetarget` and never a second implementation
([motion policy §11](../design/MOTION_ARCHITECTURE_POLICY.md)). That makes the
OpenExec foundation the first consumer of those libraries that is not the tool
that grew up beside them, and a node that *cannot* be written as a wrapper is a
finding about the library API, produced by an implementation rather than
predicted by a review. This track is scheduled immediately afterwards so that it
acts on those findings.

**What that ordering costs, stated rather than hedged.** NPZ/AMASS no longer
precedes OpenExec, so no new source shape informs Motion Phase E's node set; the
parity input stays what v0.7.0 recorded. The producer contract (§2) is written
after `execVrm` exists rather than before, so if the exec layer needs a fifth
crossing it will be discovered as rework in this track and not avoided. Both are
accepted: three of the four crossings are already implemented, and a contract
written against three implementations plus one real consumer is a better
contract than one written against three implementations and a plan.

### Findings from the exec layer, as they land

Written here as they are produced, so this track starts with evidence rather
than with a re-reading. Each is a place an implementation reached for a library
call and did not find one; none is a defect in the library it names.

| Finding | Where it surfaced | The ask |
| --- | --- | --- |
| Reading a `UsdSkelAnimation` into a canonical pose exists only in a **tool** (`tools/motionRetarget`'s `StageIo.cpp`), where a bundle cannot call it, so `execMotion` keeps a second seam over plain values | `motion.sampleAnimation` ([sampling report §5](../reports/openusd/26.08-openexec-sampling.md)) | a library home for clip → pose, or an explicit decision that the duplication stays |
| `motion::PoseFilter` has no **stateless one-step** entry point, so a pure computation composes one out of two `Apply` calls — a seed and a step, which also **drops the dropout history** the filter keeps in its state and not in its result (measured: a bone returning after a missing frame is 45.0° here against 23.8° streamed) | `motion.filterPose` ([filtering report §7, §8](../reports/openusd/26.08-openexec-filtering.md)) | `Step(prior, pose, options)` as a free function beside the streaming class, returning the **state** beside the result — the result alone is what a caller cannot carry |
| A driver has to know things no computation declares: a request is armed by its first `Compute`, a recurrence is stepped through `ComputeWithOverrides`, a snapshot is handed in the same way, and a **wrongly typed override is dropped** with a coding error while its key computes the ordinary value — so a driver that got the type wrong gets a plausible answer. An override also reaches across a relationship to a node on another prim, which is how a driver's pose enters a blend, and a pose stamped anywhere but the other sources' instant is refused there | `motion.filterPose` ([filtering report §4, §5](../reports/openusd/26.08-openexec-filtering.md)), `motion.interpolatePose` ([interpolation report §2, §4](../reports/openusd/26.08-openexec-interpolation.md)), `motion.blendPoses` ([blending report §4](../reports/openusd/26.08-openexec-blending.md)) | a written driver contract, first needed by P0-6's parity harness: a driver holds **one** previous answer per prim, because one override drives every node depending on it ([root-motion report §4](../reports/openusd/26.08-openexec-root-motion.md)), and **one** snapshot beside it — two keys, one call — treats a coding error around the call as a failed frame, stamps a pose it hands to a blend's source at the instant the others were sampled at, and calls `ChangeTime` before expecting a retarget, which refuses the default time code ([retarget report §5](../reports/openusd/26.08-openexec-retarget.md)) |
| The root **intake** rule (`RootMotionIntake`, and `DeriveVelocity` in particular) lives in `LiveCaptureSource::_Condition` — a private method of a capture *session* that owns a buffer, a filter, held-bone state and statistics. The composition idiom that worked for `PoseFilter` was tried and **produces a wrong answer**: two poses at the same instant are a reseed for `PoseFilter` and a refusal for `Push`, and an un-overridden node evaluates exactly that case | `motion.extractRootMotion` ([root-motion report §5](../reports/openusd/26.08-openexec-root-motion.md)) | `ConditionRootMotion(prior, pose, intake)` as a free function beside the session class, so the rule has one implementation again |
| The status-carrying sampler — `IMotionSource`'s answer, which the motion contract says a caller needs — exists only as a **method on a source object** that owns its animation, so a pure computation copies the whole history into a `ClipSource` per evaluation to ask one question; the free `motion::SampleAnimation` beneath it takes the history by reference and returns the pose **without the status**. Beside it: the time-order precondition that function's binary search relies on is stated and checked nowhere, and the bracket-and-hold search is written twice, in `PoseBuffer::Sample` and `SampleAnimation`. The first finding where the wrapper *works* and the finding is what it costs | `motion.interpolatePose` ([interpolation report §6](../reports/openusd/26.08-openexec-interpolation.md)) | `SampleClip(animation, t) -> PoseSampleResult` as a free function that `ClipSource::Sample` calls, with the ordering precondition written on it — so the answer is reachable without a copy and the search has one implementation |
| `motion::BlendPoses` answers where it should not. Over nothing weighted (or an empty list) it returns a default pose **stamped 0.0**, whatever instant its sources were sampled at. It carries a NaN weight through its running total into NaN rotations. It reuses `LerpPose`'s timestamp interpolation, which is right for two samples in time and wrong for sources combined at one instant (measured: a blend of clips at 1.0 s and 0.5 s stamped 0.625 s). And its fold of successive pairwise slerps depends on the order it is given, 4.247° between three sources and their reverse, which its header does not say. The node refuses the first three and relies on the fan-in's measured authored order for the fourth. The first finding that is about the library's *answer* rather than a missing or costly entry point | `motion.blendPoses` ([blending report §5, §6](../reports/openusd/26.08-openexec-blending.md)) | an N-way blend that can say *there is nothing to blend* (an optional result, or a result type with an absent state as `PoseSampleResult` has), with finite weights and one shared instant stated as preconditions and its order dependence stated in its header |
| Building a `vrmRetarget::TargetSkeleton` from a `UsdSkelSkeleton`'s rest transforms exists only in a **tool** (`StageIo.cpp`'s `ReadSkeletonRest` and `DecomposeRest`), and the half that is plain arithmetic — matrix to rest rotation and translation, scale and shear dropped — has no `vrmRetarget` entry point, so `execVrm` carries the decomposition line for line. The sampler's finding again, one library over | `vrm.computeTargetSkeleton` ([humanoid report §7](../reports/openusd/26.08-openexec-humanoid.md)) | a `TargetSkeleton` built from joint tokens and rest matrices, beside the class, that the tool and the bundle both call |
| Reading a clip's **rest pose** off its skeleton exists only in the **tool** (`StageIo.cpp`'s `ReadClip`): which joint fills which bone's slot, by joint leaf, and which bone is its parent, by the parent path's leaf. So `execVrm` carries that assignment line for line, over the rests `vrm.computeTargetSkeleton` has already decomposed. The sampler's finding a third time. Next to it, read off the API rather than measured: `PoseRetargeter` computes its own `RestPoseCorrection` in its constructor and accepts none, so a retarget node that wraps it would redo, per evaluation, the correction `vrm.computeRestPoseCorrection` caches per rig edit | `vrm.computeRestPoseCorrection` ([correction report §7](../reports/openusd/26.08-openexec-rest-correction.md)) | a `SourceRestPose` built from a semantic skeleton's tokens and decomposed rests, beside the struct (the recompute next to it is measured, and is the row below) |
| `vrmRetarget::PoseRetargeter` computes its `RestPoseCorrection` in its **constructor** and accepts none, so a node that wraps it constructs a retargeter per evaluation and recomputes, on every frame the clip moves, the correction `vrm.computeRestPoseCorrection` caches per rig edit. The node applies exactly the cached value (asserted bit for bit) and still cannot be handed it. Measured on a full 55-bone humanoid: **21.2 µs per evaluation, 17.7 µs of it the correction**, against 1.9 µs for the retarget alone. The first finding whose cost is repeated work rather than a copy | `vrm.humanoidRetarget` ([retarget report §2](../reports/openusd/26.08-openexec-retarget.md)) | a retargeter, or a free per-pose function beside it, that takes the correction as an input, so the node reads the cached value instead of recomputing it |
| `VrmHumanoidAPI`'s 55 `vrm:humanBones:*` attributes carry **no fallback**, and 26.08 hands an unauthored schema attribute to a callback as one element of Sdf's default for the type, with a `TF_WARN`. So every compute of a humanoid map warns once per unbound bone, and "an unbound bone reads as the empty token" is the executor's statement rather than the schema's. **Not only a schema's**: an attribute defined by no schema that the prim declares with no value, or blocks, arrives the same way, so a valueless `vrm:retarget:translationScale` is a scale of 0 | `vrm.computeHumanoidMap` ([humanoid report §4, §8](../reports/openusd/26.08-openexec-humanoid.md)), `vrm.humanoidRetarget` ([retarget report §4](../reports/openusd/26.08-openexec-retarget.md)) | a decision, with BND-0, on a `""` fallback for each bone attribute in `vrmSchema` — it changes what `UsdAttribute::Get` answers for an unauthored bone, which the offline reader already treats as unbound |
| `motion:timeCodesPerSecond`, `motion:filter:*` and `motion:root:intake` are authored by fixtures and by nothing else, and so are `execVrm`'s `vrm:retarget:sourceSkeleton` and its four `vrm:retarget:*` root-motion statements | the first three nodes, `vrm.computeRestPoseCorrection` and `vrm.humanoidRetarget` | BND-0 below decides whether a rate shim and two policies belong on a clip at all, and whether the stage or a producer says which clip drives which humanoid and where its root lands |

## 2. BND-0 — the canonical producer contract ⬜

*Moved here from the recorded-source milestone on 2026-09-06.* It was paired
with NPZ/AMASS because NPZ would have been the fifth producer to answer it
privately; under the new order the boundary work is its own track and this is
where it belongs.

Four categories produce motion and each was designed alone: recorded sources
(`motionSource` + a profile), live pose sources (`vrmAdapterVmc`,
`vrmAdapterMocopi`), tracker sources (`vrmAdapterVrchatOsc`), and generated
sources (none yet). They agree in practice and nothing states the agreement.

**What is unified is the canonical value boundary, not an I/O API.** How a
producer gets its bytes is its own business — a socket, a file, a model — and
unifying *that* would put a transport shape into a library that must not have
one:

```text
Recorded:   SourceAnimation          -> motion::HumanoidAnimation
Live:       timestamp + HumanoidPose
Tracker:    timestamp + TrackerFrame
Generator:  request/context          -> HumanoidAnimation or a pose stream
```

- ⬜ State each crossing as a contract, written from the three that are
  implemented plus whatever `execMotion` turns out to need.
- ⬜ Say what a tracker source may **not** do once, rather than per adapter: the
  solve from observations to humanoid bones is generic and outside every adapter
  ([the OSC track](osc-and-vrchat-trackers.md) §5).
- ⬜ Fix `IMotionGenerator` as the fourth crossing before the first generator
  adapter is written, not derived from it.

Done when a fifth producer can be added by naming which crossing it takes.

## 3. BND-1 — one reference pipeline, proved once for every category ⬜

Every category reaches `UsdSkelAnimation` today, and each is proved by its own
tests along its own path. There is no single test that says *the same thing*
happens to all of them.

```text
source → canonical HumanoidAnimation → vrmRetarget → UsdSkelAnimation → validation
```

- ⬜ One integration test, three sources: a `.vrma` clip, a BVH export, and a
  recorded live trace — through the identical downstream call sequence, with the
  source-specific part confined to the first arrow.
- ⬜ The assertion is that the downstream half is **the same code**, not that
  three outputs are individually plausible. A source that needs a downstream
  branch has found a boundary defect, which is the point of running them
  together.

This is the test a fourth source is added *to*. NPZ/AMASS ships when it can join
it without changing it.

## 4. BND-2 — settle the adapter distribution decision ⬜

Open since v0.7.0 and now blocking a release claim rather than a feature.
`ost library package` produces an adapter artifact, and no lane publishes one;
`release.yml` stages the aggregate's members and nothing else. So "the adapters
are optional artifacts" is a design statement with no artifact behind it.

Decide, and record it in
[PACKAGE_CONTRACT.md](../architecture/PACKAGE_CONTRACT.md):

- ⬜ Does a GitHub release carry the adapter artifacts, or are they a separate
  download?
- ⬜ Do they take the product's version, or their own? *(Recommended: one
  version, separate artifact membership — a version that diverges buys a
  compatibility matrix nobody asked for.)*
- ⬜ How is a vendor SDK dependency declared, rather than discovered at build
  time?
- ⬜ Does hardware-requiring validation become a capability lane, or stay the
  opt-in local run it is today?

## 5. BND-3 — artifact closure as the release gate ⬜

[Scope policy §8](../design/INTEGRATION_SCOPE_POLICY.md) lists what a release is
expected to demonstrate from the package rather than from the tree. Most of it
exists as separate lanes and separate prose; none of it is a single gate a
release either passes or does not.

- ⬜ One checklist, generated or checked from the manifests, covering: workspace
  graph, standalone bundle build, aggregate package, installed-consumer
  configure, artifact-only smoke, installed data and profile resolution, and
  per-adapter closure.
- ⬜ It runs where a release runs. `release.yml` is hand-authored and no
  `pull_request` event executes it, which is why its first execution found a jq
  expression that had never once run
  ([ost report 39](../reports/ost/39-2026-09-01-v0.22.8-release-lane-first-execution.md)).

## 6. BND-4 — make the invariants checkable ⬜

[Scope policy §11](../design/INTEGRATION_SCOPE_POLICY.md) is a review checklist.
Some of it is already mechanical — the per-target boundary checks, the graph
gate, `check_docs.py`, the link-line/config cross-check — and the rest is review
convention, which is the state every one of those was in before it caught
something.

- ⬜ Inventory the checklist against what is enforced, and record which
  invariants are convention on purpose (some genuinely are: "a node is a
  wrapper" is not a grep).
- ⬜ Close the cheap gaps rather than all of them. A product name outside
  `adapters/` and a non-declarative profile are greppable; a source-specific
  type in `motionCore` is close to it.
- ⬜ **Per-member test attribution stays an upstream ask.** `ost test` reports
  one flat total, so `usdVrmaFileFormat` contributing zero CTest targets read as
  100% passing for months. The bug was ours and is fixed; nothing would report
  the next one
  ([report 38](../reports/ost/38-2026-08-30-v0.22.8-workspace-cell-verbs-and-orphaned-lanes.md) §4).

## 7. BND-5 — finish separating the workspace contract from its history ⬜

Carried from Product P0. The 2026-08-29 plan proposed splitting
[WORKSPACE.md](../architecture/WORKSPACE.md) three ways; **only the package half
was taken** — §5 now defers to
[PACKAGE_CONTRACT.md](../architecture/PACKAGE_CONTRACT.md). The dependency-rules
split is still open because it changes a document five others cite by section
number, so it is worth doing once with the citations updated in the same PR.

This track is where it lands: a split done alongside the split of a *repository*
is one migration of the citations instead of two.

## 8. What this track is not

- **Not a rewrite.** Every boundary named here already exists in code. If a
  consolidation would move a boundary, that is a WORKSPACE.md change in its own
  PR and this track cites it.
- **Not new abstraction.** `vrmCore` is not created here, and neither is a
  unified producer *interface* — §2 fixes a value boundary, and an I/O interface
  over four transports is the thing it exists to prevent.
- **Not the repository split.** Splitting is a release and ownership decision
  that this track makes *possible* by fixing what a leaving component would have
  to carry ([the split track](motion-foundation-split.md)).

## 9. Done when

- A fifth producer is added by naming a crossing, not by designing one.
- One integration test covers three source categories through one downstream
  path, and a fourth source joins it without changing it.
- The adapter distribution question has an answer in PACKAGE_CONTRACT.md and an
  artifact behind it.
- A release either passes the closure checklist or is not cut.
- Every invariant is enforced or is recorded as deliberately conventional.
