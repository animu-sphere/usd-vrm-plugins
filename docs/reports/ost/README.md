# OST dogfooding reports

This repository is built end to end with [OpenStrata](https://github.com/animu-sphere/open-strata)
(`ost`), and these are the dated records of what that was actually like — every
`ost` version from the pre-0.3 builds through 0.18.0, on Windows, macOS arm64,
and Linux. They are upstream feedback first and our own status trail second.

**They are append-only historical evidence.** A report is never rewritten to
match what later turned out to be true. When a newer `ost` resolves an item, a
new report re-verifies it and the superseded report gets a one-line forward-note
at the top pointing forward. So a "Blocker" in report 12 may well be fixed —
follow its forward-note rather than trusting the body.

Open work a report discovers is tracked in the [roadmap](../../roadmap/);
shipped scope lives in the [delivery history](../delivery-history.md) and the
[changelog](../../../CHANGELOG.md).

## Reading order

The current `ost` ask list is always in the **newest** report. Report 25 carries
the live v0.19.0 asks — it withdraws one of report 24's as already delivered and
carries the rest.

Numbering is the series' own: reports 1–8 call themselves "report #N" in their
bodies, so the filenames preserve those numbers rather than renumbering history.
`06a` is the odd one out — a macOS host build report written between #6 and #7
that was never given a series number.

| # | Date | Report | `ost` | Focus |
| --- | --- | --- | --- | --- |
| 25 | 2026-07-18 | [`--from-package` already composed `--workspace`](25-2026-07-18-v0.18.0-from-package-workspace-correction.md) | 0.18.0 | Correction: an ask we re-filed twice had already shipped; we read the (stale) help text instead of running it. Registration-half P0 strengthened. **Live v0.19.0 asks** |
| 24 | 2026-07-18 | [First workspace release](24-2026-07-18-v0.18.0-first-workspace-release-v0.19.0-asks.md) | 0.18.0 | v0.2.0 shipped off `package --workspace`; staged `runtime_libs` have no activation contract outside `ost`. Ask (2) withdrawn by 25 |
| 23 | 2026-07-18 | [Workspace packaging half-lands](23-2026-07-18-v0.18.0-workspace-packaging-v0.19.0-asks.md) | 0.18.0 | `package --workspace` + `bundles` land, but a schema bundle's registration half is never staged, so the packaged product cannot open a file. Asks carried into 24 |
| 22 | 2026-07-17 | [The evidence gate](22-2026-07-17-v0.17.0-evidence-gate-v0.18.0-asks.md) | 0.17.0 | `ci generate` emits a gate no producer can satisfy; `import` silently drops evidence. Superseded by 23 |
| 21 | 2026-07-15 | [Phase 3 resolver split](21-2026-07-15-v0.17.0-phase3-resolver-split-dogfooding.md) | 0.16/0.17 | `requires.libraries` lands; `plugin view` does not compose `requires.bundles` |
| 20 | 2026-07-13 | [Phase 2 handoff + v0.16.0 criteria](20-2026-07-13-v0.15.0-vrmcontainer-v0.16.0-asks.md) | 0.15.0 | Plain-library boundary: acceptance criteria for `requires.libraries` |
| 19 | 2026-07-13 | [Phase 2 `vrmContainer` extraction](19-2026-07-13-v0.15.0-phase2-vrmcontainer-dogfooding.md) | 0.15.0 | Composition lands; plain libraries remain the seam |
| 18 | 2026-07-13 | [Phase 1 `vrmSchema` split](18-2026-07-13-v0.14.0-phase1-vrmschema-split-dogfooding.md) | 0.14.0 | First real consumer run of the workspace toolchain |
| 17 | 2026-07-13 | [v0.14.0 asks verified](17-2026-07-13-v0.14.0-verification-v0.15.0-asks.md) | 0.14.0 | 3 delivered / 3 carried; workspace tooling arrives |
| 16 | 2026-07-12 | [Release lane on reproducible packaging](16-2026-07-12-v0.13.0-release-lane-v0.14.0-asks.md) | 0.13.0 | Digest-reproducible packaging drives the release workflow |
| 15 | 2026-07-12 | [Clean-install smoke](15-2026-07-12-v0.12.0-clean-install-smoke-v0.13.0-asks.md) | 0.12.0 | Packaged-artifact consumer path |
| 14 | 2026-07-11 | [Linux glibc floor fixed](14-2026-07-11-v0.12.0-linux-glibc-fix-v0.13.0-asks.md) | 0.12.0 | Real glibc floor measured; v0.11.0 asks rechecked |
| 13 | 2026-07-10 | [v0.10.0 recheck](13-2026-07-10-v0.10.0-recheck-v0.11.0-asks.md) | 0.10.0 | OCI/publish asks rechecked; GHCR auth story |
| 12 | 2026-07-09 | [First OCI runtime publish](12-2026-07-09-v0.9.0-oci-publish-v1.0.0-rc1-asks.md) | 0.9.0 | Hosted-bootstrap gap closed; two v1.0.0-rc1 asks |
| 11 | 2026-07-05 | [Hosted CI lanes](11-2026-07-05-v0.8.0-ci-hosted-lanes.md) | 0.8.0 | Real digests, generated lanes, the hosted bootstrap gap |
| 10 | 2026-07-05 | [CI policy decision](10-2026-07-05-v0.7.0-ci-policy-decision.md) | 0.7.0 | Which lanes this repo commits to |
| 9 | 2026-07-05 | [CI build-matrix policy notes](09-2026-07-05-v0.7.0-ci-build-matrix-policy.md) | 0.7.0 | Hosted vs self-hosted runner distinction |
| 8 | 2026-07-04 | [CI/workspace pass](08-2026-07-04-v0.6.0-ci-workspace-v0.7.0-asks.md) | 0.6.0 | v0.7.0 asks |
| 7 | 2026-07-04 | [0.5.0 recheck](07-2026-07-04-v0.5.0-recheck-v0.6.0-asks.md) | 0.5.0 | v0.6.0 asks |
| 6a | 2026-06-30 | [macOS OpenUSD build](06a-2026-06-30-v0.4.0-macos-openusd-build.md) | 0.4.0 | Building the macOS arm64 runtime from source |
| 6 | 2026-06-30 | [Templatization impressions](06-2026-06-30-v0.4.0-templatization-impressions.md) | 0.4.0 | Building a compiled, co-located schema from the template |
| 5 | 2026-06-30 | [0.4.0 templates](05-2026-06-30-v0.4.0-schema-template.md) | 0.4.0 | `usd-schema` and `plugin-workspace` templates |
| 4 | 2026-06-29 | [`usd-schema` is a stub](04-2026-06-29-v0.3.0-usd-schema-template.md) | 0.3.0 | Does a schema even want its own bundle? |
| 3 | 2026-06-28 | [0.3.0 follow-up](03-2026-06-28-v0.3.0-followup.md) | 0.3.0 | First re-verification pass |
| 2 | 2026-06-28 | [Repo shape and dual-mode build](02-2026-06-28-repo-shape-and-build.md) | pre-0.3 | A unified root cause |
| 1 | 2026-06-28 | [Bootstrapping the plugin](01-2026-06-28-bootstrap.md) | pre-0.3 | `init`, `runtime pull --from-usd`, `plugin new\|doctor\|build\|test\|run` |

## What these cost, and what they bought

The series is the reason several `ost` features exist in the shape they do:
`requires.bundles` (reports 17–18), `requires.libraries` and the
`openstrata.library.yaml` descriptor (reports 19–21), the reproducible packaging
the release lane depends on (report 16), and the Linux glibc floor measurement
(report 14). The still-open asks are in report 25.
