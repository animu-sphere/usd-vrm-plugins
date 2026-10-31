# Release candidate preparation

This is a preparation worksheet, not publication evidence. Scope, priority and
target version belong to the [roadmap index](../roadmap/README.md#status-at-a-glance).
The last completed preparation is recorded in [v0.11.0](../releases/v0.11.0.md).
The package version stays at [VERSION](../../VERSION) until the next lockstep bump.

## Freeze the scope and compatibility boundary

Use only the selected milestone and existing Unreleased changes. Record
capabilities, non-goals, accepted limitations and migration in a candidate
record marked `status: proposed` with an explicit **Unpublished** banner.
Package version and schema contract are independent; schema changes require
their own migration instructions and consumer verification.

## Prepare and verify

Synchronize VERSION, workspace/member/tool descriptors, internal dependency
ranges, CMake fallbacks and the numbered CHANGELOG. Run the graph, source,
standalone, plain-CMake and installed-consumer checks. Run the latest
[release closure checklist](release-closure.md) at the resulting main SHA.
Inspect all three closure JSON files, their installed-consumer reports,
artifact-only positive/negative cases and packaging reproducibility.
Missing, skipped or failed required gates block tagging; applicable pyramid
skips and installed-data exclusions must be described by the evidence.

Explicitly accept or block each known packaging limitation. After the gates
pass, tag the verified SHA, inspect the draft, actual assets/checksums and
lookdev registry digests, then publish under the release owner's authorization.
Record the exact tag SHA, workflow URLs, publication and asset inventory before
removing the unpublished status. Remove completed work from current and select
the next milestone.
