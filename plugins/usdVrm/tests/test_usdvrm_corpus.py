# SPDX-License-Identifier: Apache-2.0
"""Real-world VRM compatibility corpus smoke tests.

Drives the freshly built plugin over the redistributable avatars committed under
`tests/corpus/` (see tests/corpus/CORPUS.md for provenance + licenses). Unlike the
synthetic-fixture smoke tests, these assert only import invariants that must hold
for *any* well-formed VRM, plus that the importer preserves each model's VRM
`meta` (including its license flags) onto `/Asset.customData.vrm:meta`.

Run by hand inside the runtime + plugin env:
    ost plugin run plugins/usdVrm -- python tests/test_usdvrm_corpus.py
"""
import json
import os
import pathlib
import sys

from pxr import Plug, Sdf, Usd, UsdGeom, UsdShade, UsdSkel

CORPUS = pathlib.Path(__file__).parent / "corpus"

# Expected VRM meta license fields, read from each model's VRMC_vrm.meta and
# recorded in tests/corpus/<asset>/LICENSE.md. The importer must round-trip these
# onto the stage so downstream tooling can read the license from USD alone.
AVATARS = {
    "seed-san/Seed-san.vrm": {
        "author": "VirtualCast, Inc.",
        "meta": {
            "name": "Seed-san",
            "authors": ["VirtualCast, Inc."],
            "avatarPermission": "everyone",
            "commercialUsage": "corporation",
            "creditNotation": "required",
            "allowRedistribution": True,
            "modification": "allowModificationRedistribution",
        },
        "min_meshes": 5,
        "min_materials": 1,
        "min_joints": 50,
        "rig": ["Humanoid", "Expressions", "LookAt", "SecondaryMotion", "Constraints"],
    },
    "vrm1-constraint-twist/VRM1_Constraint_Twist_Sample.vrm": {
        "author": "pixiv Inc.",
        "meta": {
            "name": "VRM1_Constraint_Twist_Sample",
            "authors": ["pixiv Inc."],
            "avatarPermission": "everyone",
            "commercialUsage": "corporation",
            "creditNotation": "unnecessary",
            "allowRedistribution": True,
            "modification": "allowModificationRedistribution",
        },
        "min_meshes": 5,
        "min_materials": 1,
        "min_joints": 50,
        "rig": ["Humanoid", "LookAt", "Constraints"],
    },
}


def _load_meta(vrm):
    """`vrm:meta` is preserved as a JSON string; return it parsed."""
    meta = vrm.get("meta")
    assert meta, "VRM meta must be preserved on /Asset"
    if isinstance(meta, str):
        meta = json.loads(meta)
    assert isinstance(meta, dict), f"meta should parse to an object, got {type(meta)}"
    return meta


def check_avatar(rel, spec):
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
    assert vrm.get("specVersion") == "1.0", vrm.get("specVersion")
    assert vrm.get("rawExtension"), "raw VRM block should be preserved as fallback"

    # License preservation: the model's meta (incl. permission flags) must be
    # readable from the imported stage, matching the vendored LICENSE.md.
    meta = _load_meta(vrm)
    for key, want in spec["meta"].items():
        got = meta.get(key)
        assert got == want, f"{rel}: meta[{key}] = {got!r}, expected {want!r}"

    # Structure: one unified skeleton, skinned geometry, materials.
    skels = [p for p in stage.Traverse() if p.IsA(UsdSkel.Skeleton)]
    assert len(skels) == 1, f"{rel}: expected exactly one skeleton, got {len(skels)}"
    joints = UsdSkel.Skeleton(skels[0]).GetJointsAttr().Get() or []
    assert len(joints) >= spec["min_joints"], f"{rel}: {len(joints)} joints"
    valid, reason = UsdSkel.Topology(list(joints)).Validate()
    assert valid, f"{rel}: invalid skeleton topology: {reason}"

    meshes = [p for p in stage.Traverse() if p.IsA(UsdGeom.Mesh)]
    mats = [p for p in stage.Traverse() if p.IsA(UsdShade.Material)]
    assert len(meshes) >= spec["min_meshes"], f"{rel}: {len(meshes)} meshes"
    assert len(mats) >= spec["min_materials"], f"{rel}: {len(mats)} materials"

    # Typed humanoid control prim is applied.
    humanoid = stage.GetPrimAtPath("/Asset/rig/Humanoid")
    assert humanoid.IsValid(), f"{rel}: missing /Asset/rig/Humanoid"
    assert "VrmHumanoidAPI" in humanoid.GetAppliedSchemas(), humanoid.GetAppliedSchemas()

    # The rig scopes the model's features imply are authored.
    for scope in spec["rig"]:
        assert stage.GetPrimAtPath(f"/Asset/rig/{scope}").IsValid(), \
            f"{rel}: missing /Asset/rig/{scope}"

    # Real avatars carry quirks; the importer must degrade gracefully, surfacing
    # any issues as a string array on /Asset rather than crashing. (Preserved as
    # a Vt.StringArray, so normalize before inspecting.)
    warnings = list(vrm.get("warnings") or [])
    assert all(isinstance(w, str) for w in warnings), \
        f"{rel}: warnings must be strings, got {warnings!r}"

    print(f"  {rel}: OK "
          f"({len(meshes)} meshes, {len(mats)} materials, {len(joints)} joints"
          f"{', %d warning(s)' % len(warnings) if warnings else ''})")


def main() -> int:
    plugin_path = os.environ.get("PXR_PLUGINPATH_NAME")
    if plugin_path:
        Plug.Registry().RegisterPlugins(plugin_path.split(os.pathsep))
    assert Sdf.FileFormat.FindByExtension("vrm"), \
        "usdVrm SdfFileFormat is not registered"

    for rel, spec in AVATARS.items():
        check_avatar(rel, spec)
    print("usdVrm corpus smoke tests: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
