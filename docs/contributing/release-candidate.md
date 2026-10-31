# Release candidate preparation

This is a **preparation worksheet**, not a release record or evidence of a
published tag. Target version and priority belong to the
[roadmap index](../roadmap/README.md#status-at-a-glance).
The package version remains [VERSION](../../VERSION) until the lockstep bump.

## Bounded candidate scope

Theme: native animated USD export and the VRM secondary-motion foundation.
Use only the existing [Unreleased changes](../../CHANGELOG.md#unreleased):

- Native export usability/validation, external images, `--flatten`, dependency
  inventory, diagnostics and reproducibility.
- `--motion <clip.vrma>`: the existing bake packaged as native animated USD,
  including plugin-free reopening and missing-tool refusal.
- `vrmRig::SpringBoneSolver`, the corrected VRM 0.x collider basis and schema
  contract **2** (roots versus ordered chains).
- Regression, installed-consumer and release-closure checks already on main.

ExecIr, resident renderer integration, full MToon rendering and constrained
distribution profiles are outside this candidate's acceptance conditions.

## Compatibility and migration

Package version and schema contract are independent. Preserve contract **2**
when bumping the package. Consumers of old spring declarations must follow
[Migrating v1 spring declarations](../../plugins/vrmSchema/docs/SCHEMA_CONTRACT.md#migrating-v1-spring-declarations).
Reimport from `.vrm` where possible. A v1 VRM 0.x stage needs its spring root
values moved to `vrm:rootJoints`, with the old `vrm:joints` opinion removed;
VRM 1.0 chains keep `vrm:joints`. Preserve parallel parameters and collider
relationships. Changing the version marker alone is not migration. The
validator rejects a mismatched contract (`VRM271`) and ambiguous declarations
(`VRM256`); legacy consumers must be tested or updated before consuming v2.

## Evidence still required

Earlier [Step 2](../reports/2026-10-29-export-release-dry-run.md) and
[Step 3](../reports/2026-10-31-export-motion-release-dry-run.md) export reports
establish those measurements. They predate the newest closure gates and do
not establish candidate-wide release readiness.

Before tagging, freeze the scope and candidate SHA; synchronize VERSION,
workspace/member/tool descriptors, internal version ranges, CMake fallbacks,
CHANGELOG and a release record explicitly marked unpublished until cut.
Run the source, standalone and plain-CMake checks, then the
[latest closure checklist](release-closure.md) at the resulting main SHA.
Inspect `release-closure.json` from all three product cells, installed
consumers, artifact-only smokes and reproducibility. Skips and failures are
not passing evidence.

The release owner explicitly accepts or blocks on the
[known package limitations](../roadmap/backlog.md#package-portability).
After all gates pass, tag and inspect the draft, actual assets/checksums,
lookdev registry publication and digests. Human publication remains the final
step. Record the actual tag SHA, workflow run URL and asset inventory in the
release record, then clear completed tasks from current.
