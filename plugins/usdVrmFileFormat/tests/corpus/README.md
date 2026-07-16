# VRM test corpus

Real-world and reference `.vrm` assets used to prove `usdVrm` against inputs that
synthetic fixtures don't exercise. **[`manifest.json`](manifest.json)** is the
machine-readable source of truth (provenance, license, SHA-256, roles, feature
tags, expected diagnostics); **[`CORPUS.md`](CORPUS.md)** holds the selection
policy prose. This file is the operator's guide.

## Layout

```
tests/corpus/
  manifest.json              # provenance + expectations (the data)
  CORPUS.md                  # selection policy (the rules)
  spec-samples/vrm1/         # vendored vrm-c/vrm-specification samples (VRM 1.0)
    seed-san/                #   Seed-san.vrm + LICENSE.md
    constraint-twist/        #   VRM1_Constraint_Twist_Sample.vrm + LICENSE.md
  vroid/{vrm0,vrm1}/         # VRoid-produced models — fetched, never committed
  conformance/vrm1/<feature>/# single-feature spec models (humanoid, expression,
                             #   look-at, mtoon, spring-bone, constraint)
  generated/{minimal,malformed,edge-cases}/  # synthetic, repo-produced
  licenses/                  # index of the co-located per-asset LICENSE.md files
```

**Storage policy.** Only license-verified, redistribution-clear assets are
*vendored* (committed), and they live under `spec-samples/`. VRoid and other
third-party models are *fetched* by pinned SHA-256 into `vroid/` and are
**git-ignored** (see `.gitignore`) — never committed. See CORPUS.md for the
license bar.

## Models

Read the current set from the manifest:

```sh
python -c "import json;m=json.load(open('plugins/usdVrm/tests/corpus/manifest.json',encoding='utf-8'));\
print('\n'.join(f\"{x['id']:28} {x['vrmVersion']:4} {x.get('storage','?'):9} {x['file']}\" for x in m['models']))"
```

Today: `seed-san-vrm1` (VirtualCast) and `vrm1-constraint-twist` (pixiv), both
VRM 1.0 spec samples, vendored. VRoid + Alicia are declared as `candidates`
(fetch/opt-in) pending per-model license verification.

## Verify vendored assets (SHA-256)

```sh
python scripts/verify_corpus.py
```

Fails if any vendored file is missing, its SHA-256 drifts from the manifest pin,
or its `licenseFile` is absent.

## Fetch opt-in models (VRoid, Alicia)

```sh
python scripts/fetch_corpus.py --list                 # show status, fetch nothing
python scripts/fetch_corpus.py                         # fetch pinned, license-clear
python scripts/fetch_corpus.py --accept-license all    # incl. license-gated (after you verify terms)
```

The script is conservative by policy: it only auto-downloads an entry that has
**both** a direct `downloadUrl` and a `sha256` in the manifest, verifies the hash
after download, and refuses license-gated models (`redistributionAllowed` not
`true`) unless you pass `--accept-license <id>`. Entries without pins print
manual-acquisition instructions. Fetched files land under `vroid/` (git-ignored).

## Run the corpus tests

Inside the runtime + plugin session (drives the freshly built importer):

```sh
ost plugin run plugins/usdVrm -- python plugins/usdVrm/tests/test_usdvrm_corpus.py
```

The test is **manifest-driven**: it iterates the vendored models and asserts the
import invariants, `expected` structure floors, license-meta round-trip, and the
diagnostic-code contract. Adding a vendored model is a manifest edit.

## Run the negative corpus tests

The `generated/malformed/` tree holds deliberately-broken avatars that pin the
importer's diagnostic contract (one `VRMxxx` code each); see
[`generated/README.md`](generated/README.md).

```sh
python plugins/usdVrm/tools/generate_negative.py          # (re)author the fixtures
ost plugin run plugins/usdVrm -- \
    python plugins/usdVrm/tests/test_usdvrm_negative.py   # assert the pinned codes
```

## Add a model

1. Verify the license: read the model's embedded `VRMC_vrm.meta` (1.0) /
   `VRM.meta` (0.x) flags — **not** the README label. Vendor only if
   redistribution is explicitly granted (see CORPUS.md §Selection policy).
2. Place it: vendored → `spec-samples/<ver>/<name>/` with a `LICENSE.md`;
   fetch/opt-in → declare under `candidates` with a `targetPath` in `vroid/`.
3. Record provenance in `manifest.json`: `sha256` (`python scripts/verify_corpus.py`
   prints the actual hash on mismatch), `source`, `sourceUrl`, `downloadedAt`,
   `license`, `licenseFile`, `roles`, `featureTags`, `expectedDiagnostics`,
   `expectedMaxSeverity`, and the `expected` structure block (measure it — don't
   guess; import the model and read the counts + warnings).
4. Add the vendored path to `openstrata.plugin.yaml` `tests.smoke` if it should
   run in the pyramid.

## Git LFS

Vendored `.vrm` are committed as plain binaries today (`*.vrm binary` in
`.gitattributes`; each is ~10 MB). If the vendored set grows, switch to Git LFS
(`*.vrm filter=lfs diff=lfs merge=lfs -text`) — but do **not** retroactively LFS
already-committed files without an intentional history migration, and never LFS a
model whose redistribution terms are unclear (fetch it instead).
