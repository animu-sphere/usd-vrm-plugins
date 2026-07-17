# SPDX-License-Identifier: Apache-2.0
"""Real-world VRM compatibility corpus smoke/regression tests.

Drives the freshly built plugin over the **vendored** avatars declared in
`tests/corpus/manifest.json` (see tests/corpus/CORPUS.md for the selection
policy). This test is **manifest-driven**: adding or updating a vendored model is
a manifest edit, not a code change. For each model it asserts:

- import invariants that hold for any well-formed VRM (default prim, up axis,
  units, preserved raw block, one valid skeleton, typed humanoid),
- the model's `expected` structure floors (min meshes/materials/joints, rig
  scopes) — floors, not exact counts, so cosmetic re-exports don't churn tests,
- license preservation: the model's `expected.meta` (incl. permission flags)
  round-trips onto `/Asset.customData.vrm:meta`, matching the vendored LICENSE.md,
- the **diagnostic contract**: observed diagnostic *codes* are a subset of
  `expectedDiagnostics`, and the max observed severity is within
  `expectedMaxSeverity` (we test codes/severity, not message text).

Run by hand inside the runtime + plugin env:
    ost plugin run plugins/usdVrmFileFormat -- python tests/test_usdvrm_corpus.py
"""
import json
import os
import pathlib
import re
import sys

from pxr import Plug, Sdf, Usd, UsdGeom, UsdShade, UsdSkel

HERE = pathlib.Path(__file__).parent
CORPUS = HERE / "corpus"
MANIFEST = CORPUS / "manifest.json"
sys.path.insert(0, str(HERE.parent / "tools"))
import vrm_diagnostics as diag  # noqa: E402

SEVERITY_ORDER = ["NONE", "INFO", "WARNING", "ERROR", "FATAL"]
_CODE_RE = re.compile(r"\[(VRM\d+)\]")


def load_manifest() -> dict:
    with open(MANIFEST, encoding="utf-8") as fh:
        return json.load(fh)


def _load_meta(vrm):
    """`vrm:meta` is preserved as a JSON string; return it parsed."""
    meta = vrm.get("meta")
    assert meta, "VRM meta must be preserved on /Asset"
    if isinstance(meta, str):
        meta = json.loads(meta)
    assert isinstance(meta, dict), f"meta should parse to an object, got {type(meta)}"
    return meta


def _diagnostic_codes(warnings) -> list[str]:
    codes = []
    for w in warnings:
        m = _CODE_RE.search(str(w))
        if m:
            codes.append(m.group(1))
    return codes


def check_model(model: dict) -> None:
    rel = model["file"]
    exp = model.get("expected", {})
    path = CORPUS / rel
    assert path.exists(), f"missing corpus asset: {path}"

    stage = Usd.Stage.Open(str(path))
    assert stage, f"failed to open {rel}"

    # Import invariants that hold for any well-formed VRM 1.0 avatar.
    dp = stage.GetDefaultPrim()
    assert dp and dp.GetPath().pathString == "/Asset", dp
    assert dp.GetTypeName() == "SkelRoot", dp.GetTypeName()
    assert UsdGeom.GetStageUpAxis(stage) == UsdGeom.Tokens.y
    assert abs(UsdGeom.GetStageMetersPerUnit(stage) - 1.0) < 1e-9

    vrm = dp.GetCustomData().get("vrm", {})
    assert vrm.get("sourceFormat") == "VRM", vrm.get("sourceFormat")
    assert vrm.get("schemaContractVersion") == 1, vrm
    assert vrm.get("specVersion") == model["vrmVersion"], \
        f"{rel}: specVersion {vrm.get('specVersion')} != manifest {model['vrmVersion']}"
    assert vrm.get("rawExtension"), "raw VRM block should be preserved as fallback"

    # License preservation: the model's meta (incl. permission flags) must be
    # readable from the imported stage, matching the vendored LICENSE.md.
    meta = _load_meta(vrm)
    for key, want in exp.get("meta", {}).items():
        got = meta.get(key)
        assert got == want, f"{rel}: meta[{key}] = {got!r}, expected {want!r}"

    # Structure floors: one unified skeleton, skinned geometry, materials.
    skels = [p for p in stage.Traverse() if p.IsA(UsdSkel.Skeleton)]
    assert len(skels) == 1, f"{rel}: expected exactly one skeleton, got {len(skels)}"
    joints = UsdSkel.Skeleton(skels[0]).GetJointsAttr().Get() or []
    assert len(joints) >= exp.get("minJoints", 0), f"{rel}: {len(joints)} joints"
    valid, reason = UsdSkel.Topology(list(joints)).Validate()
    assert valid, f"{rel}: invalid skeleton topology: {reason}"

    meshes = [p for p in stage.Traverse() if p.IsA(UsdGeom.Mesh)]
    mats = [p for p in stage.Traverse() if p.IsA(UsdShade.Material)]
    assert len(meshes) >= exp.get("minMeshes", 0), f"{rel}: {len(meshes)} meshes"
    assert len(mats) >= exp.get("minMaterials", 0), f"{rel}: {len(mats)} materials"

    # Typed humanoid control prim is applied.
    humanoid = stage.GetPrimAtPath("/Asset/rig/Humanoid")
    assert humanoid.IsValid(), f"{rel}: missing /Asset/rig/Humanoid"
    assert "VrmHumanoidAPI" in humanoid.GetAppliedSchemas(), humanoid.GetAppliedSchemas()

    # The rig scopes the model declares are authored.
    for scope in exp.get("rigScopes", []):
        assert stage.GetPrimAtPath(f"/Asset/rig/{scope}").IsValid(), \
            f"{rel}: missing /Asset/rig/{scope}"

    # Diagnostic contract: codes must be a subset of the declared set, and the
    # max severity must not exceed the declared ceiling. (Preserved as a
    # Vt.StringArray, so normalize before inspecting.)
    warnings = list(vrm.get("warnings") or [])
    assert all(isinstance(w, str) for w in warnings), \
        f"{rel}: warnings must be strings, got {warnings!r}"
    codes = _diagnostic_codes(warnings)
    expected_codes = set(model.get("expectedDiagnostics", []))
    unexpected = [c for c in codes if c not in expected_codes]
    assert not unexpected, \
        f"{rel}: unexpected diagnostics {unexpected} (expected {sorted(expected_codes)})"
    ceiling = model.get("expectedMaxSeverity", "NONE")
    max_obs = "NONE"
    for c in codes:
        sev = diag.severity_of(c).label
        if SEVERITY_ORDER.index(sev) > SEVERITY_ORDER.index(max_obs):
            max_obs = sev
    assert SEVERITY_ORDER.index(max_obs) <= SEVERITY_ORDER.index(ceiling), \
        f"{rel}: max severity {max_obs} exceeds declared ceiling {ceiling}"

    print(f"  {model['id']}: OK "
          f"({len(meshes)} meshes, {len(mats)} materials, {len(joints)} joints, "
          f"diagnostics={sorted(set(codes)) or 'none'} <= {ceiling})")


def main() -> int:
    plugin_path = os.environ.get("PXR_PLUGINPATH_NAME")
    if plugin_path:
        Plug.Registry().RegisterPlugins(plugin_path.split(os.pathsep))
    assert Sdf.FileFormat.FindByExtension("vrm"), \
        "usdVrm SdfFileFormat is not registered"

    manifest = load_manifest()
    vendored = [m for m in manifest["models"] if m.get("storage") == "vendored"]
    assert vendored, "manifest declares no vendored corpus models"
    for model in vendored:
        check_model(model)
    print(f"usdVrm corpus smoke tests: OK ({len(vendored)} vendored model(s))")
    return 0


if __name__ == "__main__":
    sys.exit(main())
