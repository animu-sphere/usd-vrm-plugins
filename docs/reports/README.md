# Reports

Evidence from real runs, plus the granular delivery log.

Unlike the sibling repos, **this directory is published**, not local-only: the
`ost` dogfooding series is a deliberate artifact of this project — see
[ost/](ost/). `usd-vrm-plugins` is OpenStrata's first external adopter, and the
record of adopting it in public is worth as much as the plugins.

| Document | Contents |
| --- | --- |
| [ost/](ost/) | The `ost` dogfooding series (22 reports, every version from pre-0.3 to 0.17.0). Append-only; the newest report carries the live upstream ask list. |
| [delivery-history.md](delivery-history.md) | The granular pre-`v0.1.0` delivery log: what shipped, retained from the original roadmap. |

## What belongs where

These notes capture *how* something was validated, on a specific machine, at a
specific time. They are working history, not a current-state contract:

- Current structure and contracts belong in [architecture/](../architecture/)
  and [reference/](../reference/).
- Design rationale belongs in [design/](../design/).
- Incomplete work belongs in the [roadmap](../roadmap/).
- Shipped scope belongs in the [changelog](../../CHANGELOG.md), with per-version
  detail in [releases/](../releases/).
- Completed pre-release roadmap detail belongs in
  [delivery-history.md](delivery-history.md).

When a report disagrees with a current-state document, the current-state
document wins and the report is history.
