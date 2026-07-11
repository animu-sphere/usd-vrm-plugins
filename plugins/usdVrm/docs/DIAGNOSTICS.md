# usdVrm diagnostic taxonomy

Every diagnostic the toolchain reports carries a stable code (`VRMxxx`) with a
fixed severity. This replaces the importer's old free-text warnings: the code is
the contract, the message is human-readable detail.

There are three sources of truth, kept in sync:

| Where | What it holds |
| --- | --- |
| `src/model/VrmDiagnostics.h` | the code strings the C++ importer prefixes onto each message (`[VRMxxx] …`) |
| `tools/vrm_diagnostics.py` | the canonical catalog: code → severity + source + title, and the parsing/roll-up helpers |
| this file | the human-readable reference (generated from the catalog) |

## Severity ladder

Most severe to least. Tools fail (non-zero exit) on any `ERROR` or `FATAL`.

| Severity | Meaning |
| --- | --- |
| `FATAL` | the stage is unusable / could not be read |
| `ERROR` | a contract violation — downstream tools will misbehave |
| `WARNING` | fidelity loss or an unmapped feature; the import still loads |
| `INFO` | a note about a deliberate, lossless-but-not-typed mapping |

## Sources

- **import** — emitted by the C++ importer (reader/authorer) at `Read()` time and
  surfaced on `/Asset.customData.vrm:warnings`, coded.
- **validate** — emitted by `tools/validate_vrm.py` over an already-imported
  stage. No C++ analogue.

## Code catalog

| Code | Severity | Source | Title |
| --- | --- | --- | --- |
| VRM001 | WARNING | import | No VRM extension; imported as plain glTF |
| VRM002 | ERROR | import | VRM extension JSON could not be parsed |
| VRM101 | WARNING | import | Embedded image format unsupported; texture skipped |
| VRM102 | WARNING | import | Data-URI image unsupported; texture skipped |
| VRM103 | WARNING | import | Texture uses a UV set other than TEXCOORD_0 |
| VRM110 | WARNING | import | Conflicting inverse bind matrices across skins |
| VRM120 | WARNING | import | Primitive is not a triangle list; skipped |
| VRM121 | WARNING | import | Primitive has no POSITION; skipped |
| VRM140 | WARNING | import | Humanoid bone could not be mapped to a joint |
| VRM150 | INFO | import | VRM 0.x materialValues expression preserved raw only |
| VRM160 | WARNING | import | CUBICSPLINE animation approximated as linear |
| VRM170 | WARNING | import | Node constraint has no valid source; skipped |
| VRM180 | WARNING | import | Morph targets present but no skeleton; blend shapes skipped |
| VRM181 | WARNING | import | Humanoid bones present but no skeleton imported |
| VRM200 | FATAL | validate | Stage has no default prim |
| VRM201 | ERROR | validate | Default prim is not /Asset |
| VRM202 | WARNING | validate | /Asset kind is not 'component' |
| VRM203 | ERROR | validate | Stage up-axis is not Y |
| VRM204 | WARNING | validate | Stage metersPerUnit is not 1.0 |
| VRM205 | ERROR | validate | SkelRoot/skeleton presence mismatch on /Asset |
| VRM210 | ERROR | validate | Skinned mesh has no skeleton binding |
| VRM211 | ERROR | validate | skel:skeleton target does not resolve to a Skeleton |
| VRM212 | ERROR | validate | Joint index out of range for the bound skeleton |
| VRM213 | ERROR | validate | Skinned mesh is missing joint indices/weights |
| VRM214 | ERROR | validate | Skeleton topology is not parent-before-child |
| VRM220 | INFO | validate | Mesh has no material binding |
| VRM221 | ERROR | validate | Material binding target does not exist |
| VRM222 | ERROR | validate | Texture asset does not resolve |
| VRM230 | ERROR | validate | Humanoid prim does not apply VrmHumanoidAPI |
| VRM231 | ERROR | validate | Humanoid vrm:skeleton relationship is missing/broken |
| VRM232 | ERROR | validate | Humanoid bone value is not a joint on the skeleton |
| VRM240 | ERROR | validate | Expression morph-target relationship is broken |
| VRM241 | ERROR | validate | Expression material-color target does not exist |
| VRM242 | ERROR | validate | Expression prim does not apply VrmExpressionAPI |
| VRM243 | ERROR | validate | Expression morph-target arrays are not parallel |
| VRM244 | ERROR | validate | Expression material-color arrays are not parallel |
| VRM245 | ERROR | validate | LookAt prim does not apply VrmLookAtAPI |
| VRM246 | ERROR | validate | LookAt skeleton relationship is missing/broken |
| VRM247 | ERROR | validate | LookAt eye joint value is not a skeleton joint |
| VRM250 | ERROR | validate | Spring-bone joint path is not on the skeleton |
| VRM251 | ERROR | validate | Spring-bone collider-group target does not exist |
| VRM252 | ERROR | validate | Spring-bone prim does not apply VrmSpringBoneAPI |
| VRM253 | ERROR | validate | Spring-bone parameter arrays are not parallel |
| VRM254 | ERROR | validate | Collider prim does not apply VrmColliderAPI |
| VRM255 | ERROR | validate | Collider shape token is invalid |
| VRM260 | INFO | validate | Lossless raw VRM extension block is absent |
| VRM262 | ERROR | validate | Constraint prim does not apply VrmConstraintAPI |
| VRM263 | ERROR | validate | Constraint type token is invalid |
| VRM264 | ERROR | validate | Constraint joint value is not a skeleton joint |
| VRM270 | WARNING | validate | Schema contract version is absent |
| VRM271 | ERROR | validate | Schema contract version is unsupported |

## Tools

```sh
# Validate an imported stage against the contract (exit 1 on error/fatal).
ost plugin run plugins/usdVrm -- \
    python plugins/usdVrm/tools/validate_vrm.py avatar.vrm --json

# Full compatibility report: import + validation diagnostics, asset inventory,
# feature compatibility (human-readable, or --json / --out report.json).
ost plugin run plugins/usdVrm -- \
    python plugins/usdVrm/tools/vrm_report.py avatar.vrm --json
```
