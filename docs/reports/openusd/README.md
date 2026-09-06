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

## Reading an audit

Each one states its **method** near the top — what was read, what was run, and
what was neither. That section is load-bearing: a claim traced to a header is
not the same kind of claim as one traced to a passing test, and a plan that
leans on the first should say so.

The consequences an audit draws for this project are collected in its own "what
this changes in the plan" section, and mirrored into the affected task in
[roadmap/](../../roadmap/). The roadmap is where the work is tracked; the audit
is where the evidence stays.
