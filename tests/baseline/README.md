# Phase 0 baseline — frozen pre-split behavior

This directory is the committed behavior record required by
[WORKSPACE.md §7](../../docs/architecture/WORKSPACE.md) before any workspace
split begins. Every migration phase (vrmSchema split, vrmContainer extraction,
resolver split, rename, packaging) is gated against it: **a migration PR that
changes any file here is a regression by definition, regardless of tests
passing.**

All artifacts are generated and verified by
[`tools/baseline_freeze.py`](../../tools/baseline_freeze.py).

## Commands

```sh
# Verify current behavior against the committed baseline (the phase gate).
# ost 0.15+ resolves the manifest dependency closure into the session:
ost plugin run plugins/usdVrm -- python tools/baseline_freeze.py --check

# Rewrite the baseline (ONLY in a dedicated behavior-change PR, never in a
# structural/migration PR):
ost plugin run plugins/usdVrm -- python tools/baseline_freeze.py --update
```

The check also runs as the `usdvrm_baseline` CTest test in the plain-CMake
build (`ctest --test-dir build/plain -R usdvrm_baseline`).

## What is frozen

| Artifact | Freezes | Plan §9 items covered |
| --- | --- | --- |
| `usda/fixtures/*.usda` | flattened, normalized stage per local `.vrm` fixture (16 positive) | USDA snapshot, stage topology, material binding, skeleton topology, animation output |
| `usda/malformed/*.usda` | stage output of the 5 recoverable malformed fixtures (warning paths) | USDA snapshot, diagnostics-adjacent authoring |
| `digests/{fixtures,malformed,corpus}/*.json` | structural digest per input, incl. the vendored corpus models too large for text snapshots: prim topology + applied schemas, mesh/material/skeleton bindings, points/rest/bind hashes, time-sample tables, asset inventory with **resolved** flags, VRM custom data, warning codes; for read-fatal inputs the open failure + its codes | stage topology, embedded texture resolution, diagnostics code, animation output |
| `schema_contract.json` | schema types, properties, fallbacks, allowed tokens (from `generatedSchema.usda`, sha256-pinned) + registry visibility of the six `Vrm*API` schemas | schema contract |
| `discovery.json` | the **union** of registered USD types across every workspace bundle (file format, package resolver, schema APIs) + `SdfFileFormat.FindByExtension("vrm")` probe. Which bundle registers which type is deliberately not frozen — that attribution moves during the split and is owned by per-bundle discovery tests (WORKSPACE.md §6 invariant 5) | plugin discovery, public Python surface |
| `diagnostics.json` | full `VRMxxx` catalog (code → severity/source/title) + corpus expected-diagnostics table + negative-corpus contract | diagnostics code |
| `symbols/<platform>.txt` | the **union** of exported native symbols across every built bundle library under `plugins/*/lib` (a symbol moving between bundles stays invisible; one appearing or vanishing does not) | public C++ symbols |

Notes on the remaining plan §9 items:

- **Clean-install smoke / embedded texture resolution** — the observable
  result a clean install must reproduce is exactly `discovery.json` (registry
  state) plus the `resolved: true` asset entries in the digests. The packaged
  layout itself is exercised by `ost plugin test --from-package` and the
  release lane, not duplicated here.
- **Public Python symbols** — the bundle ships no Python module; its
  Python-visible surface is the registry state (`discovery.json`) plus the
  schema contract (`schema_contract.json`), both frozen.

## Regression criteria

1. `--check` exits non-zero → the PR changed frozen behavior. In a
   structural/migration PR this is a regression to fix, never a baseline to
   update.
2. Moving files (`git mv`, bundle splits) must not change baseline *content*.
   The generators locate inputs through the manifests, so path-only moves stay
   invisible by construction; if a move forces a generator edit, the artifacts
   it emits must still be byte-identical.
3. An intended behavior change regenerates the baseline with `--update` in its
   own non-structural PR, with the diff reviewed as the behavior change.
4. Baseline artifacts are machine-independent (absolute repo paths are
   rewritten to `${REPO}`, flatten doc headers stripped). Byte-exactness is
   asserted on the platform that generated them (Windows / cy2026 usd
   runtime); symbol lists are per-platform and the check skips platforms
   without a committed list instead of failing.

## Enforcement map

- Local / phase gate: `usdvrm_baseline` CTest + the `--check` command above.
  Every migration PR description must include the check result.
- CI (`ost plugin test`, all three PR lanes): L2 discovery, L2 schema
  registration, L4 stage open, and the L5 golden for `minimal.vrm` overlap
  with parts of this baseline. Note: L5 golden currently compares the
  **first** `tests.roundtrip` entry (verified empirically 2026-07-13), so the
  per-fixture snapshots here are intentionally *not* modeled as ost goldens;
  multi-fixture snapshots remain intentionally owned by this gate.
