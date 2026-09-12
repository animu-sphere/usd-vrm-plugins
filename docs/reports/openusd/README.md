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

## Reading an audit

Each one states its **method** near the top — what was read, what was run, and
what was neither. That section is load-bearing: a claim traced to a header is
not the same kind of claim as one traced to a passing test, and a plan that
leans on the first should say so.

The consequences an audit draws for this project are collected in its own "what
this changes in the plan" section, and mirrored into the affected task in
[roadmap/](../../roadmap/). The roadmap is where the work is tracked; the audit
is where the evidence stays.
