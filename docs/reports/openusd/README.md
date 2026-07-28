# OpenUSD audits

Findings from reading a specific OpenUSD release, written when this project has
to build on a part of OpenUSD that is new, unstable, or thinly documented.

Unlike the [`ost` series](../ost/), these are not upstream feedback and are not
numbered: each one is named for the release and the subsystem it audits, and it
is superseded by the next audit of the same subsystem rather than by a
forward-note. They are still **history** — an audit records what a release
looked like on the day it was read, and is not rewritten when the release
changes.

| Audit | Release | Subsystem |
| --- | --- | --- |
| [26.08-openexec-migration.md](26.08-openexec-migration.md) | 26.08 | OpenExec: `exec`, `execUsd`, `execIr`, `vdf`, `usdExecImaging` — registration, callbacks, value types, connection dataflow, requests, cache/invalidation, and the Hydra path. |

## Reading an audit

Each one states its **method** near the top — what was read, what was run, and
what was neither. That section is load-bearing: a claim traced to a header is
not the same kind of claim as one traced to a passing test, and a plan that
leans on the first should say so.

The consequences an audit draws for this project are collected in its own "what
this changes in the plan" section, and mirrored into the affected task in
[roadmap/](../../roadmap/). The roadmap is where the work is tracked; the audit
is where the evidence stays.
