# Current — release closure

Milestone: release closure (target and priority: [roadmap index](README.md#status-at-a-glance)).
Owner: `usd-vrm-plugins`. The candidate consists of existing implementation;
new features wait until after this milestone. Package version stays unchanged
until release preparation; schema contract remains **2**.

## Goal

Prove the candidate from installed artifacts with the latest gates, then
publish only the measured scope. Earlier export measurements do not establish
that the newer release-closure gates passed.

## Work (open only)

| ID | Status | Owner | Next action / done when | Evidence destination |
| --- | --- | --- | --- | --- |
| R01 | active | usd-vrm-plugins | Freeze the candidate scope; prepare the lockstep package-version/range/fallback updates, numbered CHANGELOG, release record, schema migration and accepted limitations. No publication claim before publishing. | [Candidate preparation](../contributing/release-candidate.md); final release record. |
| R02 | active | usd-vrm-plugins | Run graph, workspace regressions, standalone bundles, plain CMake and installed consumers; run the latest release workflow on the resulting main SHA. All three product cells must have passing closure JSON, including all required gates. | Dated report with SHA/run URLs, closure and consumer artifacts. |
| R03 | active | usd-vrm-plugins | Review artifact-only import/VRMA/exec/imaging/export proof and negative tests at that SHA; explicitly accept or block on each packaging limitation. | Candidate report and release record; [closure requirements](../contributing/release-closure.md). |
| R04 | blocked | usd-vrm-plugins | After R01–R03 pass: tag, inspect draft/assets/checksums and registry digests, then human publication. Record actual tag SHA, run URL and asset inventory; remove completed rows here and select the next milestone. | Published release and immutable release record. |

## External dependencies

Hosted Windows x86_64, Linux x86_64 and macOS arm64 evidence is required.
The known standalone registration and packaging limitations are linked from
the [backlog](backlog.md#package-portability); they do not silently become
release exceptions. R03 records the release owner's decision on each.

Historical release preparation and the disposition of carried items are in
the [documentation audit](../reports/2026-10-31-documentation-ownership-audit.md).
