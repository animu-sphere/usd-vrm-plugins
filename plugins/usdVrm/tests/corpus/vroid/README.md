# VRoid-produced avatars

VRoid Studio-generated `.vrm` models (real-world topology: hair, outfits, many
materials/textures). Grouped by VRM version under `vrm0/` and `vrm1/`.

These are **not vendored** by default: VRoid model licenses vary and must be
verified per model (embedded `VRMC_vrm.meta` / `VRM.meta` flags + the
distribution page). Declared in `../manifest.json` under `models` (once vendored
and license-verified) or `candidates` (acquisition intent). Use
`scripts/fetch_corpus.py` to pull fetch-storage models by pinned SHA-256.
