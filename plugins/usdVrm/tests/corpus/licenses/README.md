# License records

Each vendored corpus asset keeps its **authoritative** license record *co-located*
with the file, so the license travels with the asset in any package/redistribution:

| model | record |
| --- | --- |
| Seed-san (VirtualCast) | [`../spec-samples/vrm1/seed-san/LICENSE.md`](../spec-samples/vrm1/seed-san/LICENSE.md) |
| VRM1 Constraint Twist (pixiv) | [`../spec-samples/vrm1/constraint-twist/LICENSE.md`](../spec-samples/vrm1/constraint-twist/LICENSE.md) |

Fetched / opt-in models (VRoid, Alicia) are **not** vendored; their license terms
must be verified per model at fetch time (see [`../CORPUS.md`](../CORPUS.md) and the
`candidates` in [`../manifest.json`](../manifest.json)). Record the verified terms
alongside the fetched file, or here, when a model is promoted to vendored.
