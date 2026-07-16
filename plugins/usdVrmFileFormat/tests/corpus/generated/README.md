# Generated models

Synthetic models produced by the repo (minimal / malformed / edge-cases) — no
external distribution dependency, all license-clean.

## `malformed/` — the negative corpus

Deliberately broken `.vrm` files that pin the importer's **diagnostic contract**:
each one provokes exactly one `VRMxxx` code. They are authored by
[`../../../tools/generate_negative.py`](../../../tools/generate_negative.py) and
declared in [`negative-manifest.json`](negative-manifest.json), which
[`../../test_usdvrm_negative.py`](../../test_usdvrm_negative.py) drives over the
freshly built importer.

Two mechanisms:

- **read-fatal** — the container is unreadable, so `Usd.Stage.Open` fails and the
  importer surfaces the code in the raised error (`VRM003`).
- **import-warning** — the stage opens and the code is the *only* diagnostic on
  `/Asset.customData.vrm:warnings`.

| fixture | mechanism | code |
|---|---|---|
| `malformed_glb_json.vrm` | read-fatal | `VRM003` |
| `dangling_texture_index.vrm` | read-fatal | `VRM003` |
| `nonfinite_transform.vrm` | read-fatal | `VRM003` |
| `nonobject_vrm_extension.vrm` | read-fatal | `VRM003` |
| `out_of_range_joints.vrm` | import-warning | `VRM111` |
| `unmapped_humanoid_node.vrm` | import-warning | `VRM140` |
| `duplicate_humanoid_bone.vrm` | import-warning | `VRM141` |
| `oob_expression_morph.vrm` | import-warning | `VRM151` |
| `missing_spring_collider_group.vrm` | import-warning | `VRM190` |

Regenerate the binaries (deterministic) and run the contract test:

```sh
python plugins/usdVrm/tools/generate_negative.py
ost plugin run plugins/usdVrm -- python plugins/usdVrm/tests/test_usdvrm_negative.py
```

Adding a case is a manifest edit: add a `build_*` to `generate_negative.py`, a
`fixtures[]` entry to `negative-manifest.json`, and (if it needs a new code) a
`VRMxxx` to `src/model/VrmDiagnostics.h` + `tools/vrm_diagnostics.py` +
`docs/DIAGNOSTICS.md`. The ERROR-level `VRM002` (VRM-extension JSON unparseable)
has no fixture: cgltf validates the container first and rejects a non-object
extension as `VRM003`, so `VRM002` is not reachable through a crafted file.

## `minimal/`, `edge-cases/`

Reserved for corpus-scale positive generated assets as they are added. The
existing positive synthetic fixtures live in [`../../fixtures/`](../../fixtures/).
