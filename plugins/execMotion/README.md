# execMotion

Vendor-neutral OpenExec computations over canonical motion. Workspace Phase 8 /
Motion Phase E; the plan is
[docs/roadmap/openexec-foundation.md](../../docs/roadmap/openexec-foundation.md)
§6, P0-4.

**This is the bootstrap, not the layer.** It registers one value type and one
computation, and the computation is the identity:

| Computation | Provider | Result |
| --- | --- | --- |
| `motion.identityPose` | a `UsdSkelAnimation` prim | the identity `motion::HumanoidPose` over the canonical bones the clip's `joints` name |

The pose carries **no timestamp**, and that is measured rather than skipped. A
computation is handed a *frame* and `motion::HumanoidPose::timestamp` is
*seconds*; the rate between them is stage metadata a computation cannot reach
(`Stage().Metadata<double>(timeCodesPerSecond)` is accepted, is not refused with
`.Required()`, and still yields no value). A guessed rate would put a wrong
second into canonical motion that nothing downstream could tell from a measured
one, so this computation declares no time input at all — the identity pose is the
same pose at every frame. `motion.sampleAnimation` will have to be *given* a rate
([the mechanism report](../../docs/reports/openusd/26.08-openexec-mechanism.md)
§5).

The real nodes — `motion.sampleAnimation`, `motion.filterPose`,
`motion.extractRootMotion`, `motion.interpolatePose`, `motion.blendPoses` — come
next, in that order, over `motionRuntime`. Mechanism before behaviour is
deliberate: with no algorithm in the bundle, a wrong answer can only be a wrong
mechanism.

## What this bundle may not do

- **No stage authoring.** A computation produces a value; USD animation is
  authored on bake, record or publish, never per evaluated frame (motion policy
  §12.1).
- **No I/O inside a computation.** No socket, device poll, file watch, wall
  clock, mutable global or private thread pool. Receiving belongs to an adapter
  and buffering to `motionRuntime`; a computation evaluates an immutable
  snapshot (motion policy §11.4).
- **No second algorithm.** Every computation is a thin wrapper over
  `motionCore` / `motionRuntime`. The decisions live in a function over plain
  values ([`src/ExecMotionIdentity.h`](src/ExecMotionIdentity.h)) and the
  registration TU only marshals inputs into it.
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
| `execMotion_identity` | the seam, with no stage, no system and no request |
| `execMotion_mechanism` | discovery through `plugInfo.json`, request compile, compute, an unchanged recompute, an authored-value invalidation, a time change reporting nothing for a time-independent value key, an explicit invalidation, and the shape an unregistered computation presents as |

Both carry the CTest label `motion.openexec`.
