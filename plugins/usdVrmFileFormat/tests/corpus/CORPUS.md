# Real-world VRM compatibility corpus

Redistributable, real-world `.vrm` avatars used to prove `usdVrm` against assets
that synthetic fixtures don't exercise. This file is the **selection policy**;
[`manifest.json`](manifest.json) is the machine-readable provenance data and
[`README.md`](README.md) is the operator's guide (verify / fetch / run / add).
Every asset here is licensed independently of `usdVrm` (which is Apache-2.0) and
is used **read-only as a test fixture**.

## Directory layout

```
spec-samples/<ver>/<name>/   vendored vrm-c spec samples (license-clear), + LICENSE.md
vroid/{vrm0,vrm1}/           VRoid-produced models — fetched by pinned SHA-256, git-ignored
conformance/vrm1/<feature>/  single-feature spec models (per subsystem)
generated/{minimal,malformed,edge-cases}/  synthetic, repo-produced
licenses/                    index of the co-located per-asset LICENSE.md files
```

Vendored assets (committed) live only under `spec-samples/`; third-party fetched
models live under `vroid/` and are never committed.

## Selection policy

1. **License bar.** An asset is eligible only if redistribution is *explicitly*
   granted — for VRM 1.0 that means the model's own `VRMC_vrm.meta` has
   `allowRedistribution: true` (and, for shipping USD conversions, `modification:
   allowModificationRedistribution`); for VRM 0.x, the `VRM.meta` license flags or
   an accompanying standard license (CC0/CC-BY) must permit it. **We verify the
   embedded meta, not the README** — the "VRM Public License 1.0" label alone says
   nothing; its terms are settings-driven per model.
2. **Storage = hybrid.**
   - *Vendored* (committed here) only when the flags above clearly allow verbatim
     redistribution. Each vendored asset lives in its own folder with a
     `LICENSE.md` (author, source URL, SHA-256, permission flags, required
     credit).
   - *Fetch-at-test / opt-in* for assets whose original-file redistribution is
     unclear or not granted (e.g. the dwango "ニコニ立体ちゃん / Alicia" character
     rules permit derivative works but not clearly verbatim original
     redistribution). Those are pulled from their canonical URL by pinned SHA-256
     at test time, or kept as a local opt-in fixture — **never committed**.
3. **Derivatives inherit the source license.** A USD conversion of a corpus model
   is an *adapted work*; under VRM PL 1.0 its terms must be "same or more
   restrictive" than the source. Keep any committed derivative under the source's
   license (in its asset folder), not under the repo's Apache-2.0.
4. **Attribution is mandatory where the model requires it** (`creditNotation:
   required`). See each asset's `LICENSE.md`.

## Vendored assets

| asset | dir | VRM | author | license | redistribution | modification | credit |
|---|---|---|---|---|---|---|---|
| Seed-san | `spec-samples/vrm1/seed-san/` | 1.0 | VirtualCast, Inc. | VRM PL 1.0 | `allowRedistribution: true` | `allowModificationRedistribution` | **required** |
| VRM1 Constraint Twist | `spec-samples/vrm1/constraint-twist/` | 1.0 | pixiv Inc. (2022) | VRM PL 1.0 | `allowRedistribution: true` | `allowModificationRedistribution` | unnecessary |

Flags were read directly from each file's `VRMC_vrm.meta` (see the per-asset
`LICENSE.md` for the SHA-256 and the full flag set).

### Required credits
- **Seed-san model by VirtualCast, Inc.** — VRM Public License 1.0.

## Coverage vs the policy axes

| axis | covered by | status |
|---|---|---|
| VRM 1.0 | Seed-san, VRM1 Constraint Twist | ✅ |
| MToon | Seed-san, VRM1 Constraint Twist (`VRMC_materials_mtoon`, `KHR_materials_unlit`) | ✅ |
| SpringBone | Seed-san, VRM1 Constraint Twist (`VRMC_springBone`) | ✅ |
| Node constraints | Seed-san, VRM1 Constraint Twist (`VRMC_node_constraint`) | ✅ |
| Expressions / LookAt / PBR | Seed-san | ✅ |
| VRM 0.x | — | ⛳ candidate: AliciaSolid 0.51 (fetch/opt-in; original redistribution unclear) |
| VRoid-generated avatar | — | ⛳ candidate: VRoid official sample (license TBD) |
| Animation clips | — | ❌ gap |
| Texture compression (KTX2) | — | ❌ gap |
| multi-skin | — | ❌ gap |

## Candidates evaluated but not vendored

- **AliciaSolid_vrm-0.51** (ニコニ立体ちゃん, dwango). The dwango character rules
  (`3d.nicovideo.jp/alicia/rule.html`) permit creating and distributing derivative
  works for personal use, but do **not** clearly grant verbatim redistribution of
  the original file; it is a proprietary character license, not a standard OSS
  license. UniVRM ships it in its MIT test-models tree, but that is community
  practice, not an explicit grant. → **not vendored**; if used, fetch by pinned
  SHA-256 from the canonical source at test time, or keep local/opt-in. Value:
  VRM 0.x + non-ASCII (Japanese) bone/meta names.
- **VRoid official sample avatars** — separate license evaluation still needed.

## How to re-verify an asset's flags

The flags come straight from the GLB's JSON chunk. Any of:
- import with `usdVrm` and inspect `/Asset.customData.vrm:meta`, or
- read `extensions.VRMC_vrm.meta` (1.0) / `extensions.VRM.meta` (0.x) from the
  glTF JSON chunk directly.
