# OpenUSD audits

Findings from reading a specific OpenUSD release, written when this project has
to build on a part of OpenUSD that is new, unstable, or thinly documented.

Unlike the [`ost` series](../ost/), these are not upstream feedback and are not
numbered: each one is named for the release and the subsystem it audits, and it
is superseded by the next audit of the same subsystem rather than by a
forward-note. Two audits of the *same* release are not a supersession when their
methods differ: one read and one run answer different kinds of question, and the
second says which of the first's claims it corrects. They are still **history**
— an audit records what a release looked like on the day it was read, and is
not rewritten when the release changes.

| Audit | Release | Subsystem |
| --- | --- | --- |
| [26.08-openexec-migration.md](26.08-openexec-migration.md) | 26.08 | OpenExec: `exec`, `execUsd`, `execIr`, `vdf`, `usdExecImaging` — registration, callbacks, value types, connection dataflow, requests, cache/invalidation, and the Hydra path. **Read, not run.** |
| [26.08-openexec-mechanism.md](26.08-openexec-mechanism.md) | 26.08 | OpenExec registration and evaluation, **run**: what a plugin's `Info.Exec.Schemas` block may claim, what an applied API schema reaches, and what `InvalidateAll()` takes with it. Corrects three statements in the migration audit and does not replace it. |
| [26.08-openexec-sampling.md](26.08-openexec-sampling.md) | 26.08 | OpenExec inputs and time, **run**: that `.Required()` does not refuse a missing attribute, that time dependence follows the inputs, what the default time code resolves a keyed clip to, and who interpolates between two keys. Extends the mechanism report's timestamp finding and replaces neither audit. |
| [26.08-openexec-filtering.md](26.08-openexec-filtering.md) | 26.08 | OpenExec chaining and overrides, **run**: that a computation reads another computation and inherits its time dependence, that a request is armed by its first compute, and how `ComputeWithOverrides` supplies a value the graph cannot derive — and what a filter's state loses by having to travel as a pose. Extends the sampling report across a node boundary and replaces no audit. |
| [26.08-openexec-root-motion.md](26.08-openexec-root-motion.md) | 26.08 | OpenExec value types, refusals and drivers, **run**: that a bundle registers more than one value type and a computation may answer in a type other than the one it reads, that time dependence follows the link rather than the type, that an **undeclared** input is silent, that one override drives every node depending on it, and that a refusal must set **no value** (`SetEmptyOutput`) to stay distinguishable from an answer. Corrects one sentence of the sampling report and replaces no audit. |
| [26.08-openexec-interpolation.md](26.08-openexec-interpolation.md) | 26.08 | OpenExec snapshots and drivers, **run**: that a driver's history enters as an override on a key whose type is a whole `HumanoidAnimation`, that two overrides of two keys in one call each reach only their own dependents, that a **wrongly typed or empty** override is dropped with a coding error and the key computes its ordinary value, and that a result type with an absent state of its own (`PoseSampleResult`'s `Unavailable`) answers where every earlier type had to refuse. Answers one open item of the root-motion report and replaces no audit. |
| [26.08-openexec-blending.md](26.08-openexec-blending.md) | 26.08 | OpenExec fan-in, **read, then run**: that a relationship's targets arrive in **authored order** (at first compile, after an edit and in a fresh system); that a target not providing the computation and a source that **refused** both vanish from the fan-in without a word; that time, an authored value and an edit of the relationship all invalidate across it; and that an override on one prim reaches a dependent on another. Corrects one sentence of the OpenExec plan and replaces no audit. |
| [26.08-openexec-humanoid.md](26.08-openexec-humanoid.md) | 26.08 | OpenExec on this workspace's own applied schema, **read, then run**: that an attribute a schema **defines** and the stage gives no value reaches a callback as **one element of the type's fallback**, beside a `TF_WARN`, not as nothing; that a computation on `VrmHumanoidAPI` resolves on a prim `execGeom` types, and not on one carrying the attributes without the schema; that the schema bundle is a **runtime** requirement exec resolves by type name; and that invalidation crosses a relationship between two schemas and follows the dependency rather than the value. Narrows one finding of the sampling report, finds one sentence of `execMotion`'s documentation false for a one-joint clip, and replaces no audit. |
| [26.08-openexec-rest-correction.md](26.08-openexec-rest-correction.md) | 26.08 | OpenExec across two rigs on one stage, **run**: that one computation serves two prims through two relationships, one of them defined by no schema; that a relationship target naming no prim arrives **exactly as an unauthored relationship**, which is why an absent input reached through a relationship is refused rather than defaulted; and that invalidation follows the dependency, not the value, on a third node. Repeats two of the humanoid report's findings on a second relationship and replaces no audit. |
| [26.08-openexec-retarget.md](26.08-openexec-retarget.md) | 26.08 | OpenExec across two bundles, **run**: that a value one bundle computes reaches a computation of the other unchanged and that exec **says nothing** when the other bundle is missing, so only a relationship count notices; that a bundle reading a value type must register it itself or lose every computation in a session that did not load the other first; that the fallback for a valueless attribute follows its **existence, not its schema**; and what a wrapper that recomputes a cached correction costs. Narrows one sentence of the humanoid report and replaces no audit. |
| [26.08-openexec-joint-transforms.md](26.08-openexec-joint-transforms.md) | 26.08 | OpenExec and UsdSkel at the bake boundary, **run**: that the retarget's value, in the shape a `UsdSkelAnimation` states it, is exactly what UsdSkel resolves once authored, and that without `scales`, or with arrays one joint short, UsdSkel resolves the **rest pose** without a word; what the identity-scale rule does to a rig whose rest is scaled; that an **unregistered result type** takes every computation of the bundle with it; and that `motion_retarget` places a sample at a time code rebuilt from seconds, off the frame at 30 fps. Replaces no audit. |

## Reading an audit

Each one states its **method** near the top — what was read, what was run, and
what was neither. That section is load-bearing: a claim traced to a header is
not the same kind of claim as one traced to a passing test, and a plan that
leans on the first should say so.

The consequences an audit draws for this project are collected in its own "what
this changes in the plan" section, and mirrored into the affected task in
[roadmap/](../../roadmap/). The roadmap is where the work is tracked; the audit
is where the evidence stays.
