# SPDX-License-Identifier: Apache-2.0
"""Tests for the diagnostic taxonomy, stage validator, and compatibility report.

Three layers, matching tools/vrm_diagnostics.py, tools/validate_vrm.py and
tools/vrm_report.py:

* the taxonomy is exercised as pure logic (code parsing, severity resolution);
* the validator is run over the committed fixtures (which must pass) and over
  fixtures deliberately mutated in-session to a broken shape (which must be
  flagged with the right code);
* the report is checked for its sections and that import-time warnings arrive
  coded from the freshly built importer.

Same runtime + plugin env as test_usdvrm_plugin.py (see CMakeLists.txt).
"""
import os
import pathlib
import sys

from pxr import Plug, Sdf, Usd, UsdSkel, Vt

FIXTURES = pathlib.Path(__file__).parent / "fixtures"
TOOLS = pathlib.Path(__file__).parents[1] / "tools"
sys.path.insert(0, str(TOOLS))

import validate_vrm
import vrm_diagnostics as diag
import vrm_report
from vrm_diagnostics import Severity


def _open(name):
    stage = Usd.Stage.Open(str(FIXTURES / name))
    assert stage, f"failed to open {name}"
    return stage


def _codes(diagnostics):
    return {d.code for d in diagnostics}


def check_taxonomy():
    """Pure-logic: code parsing, severity lookup, and count roll-up."""
    code, body = diag.parse_coded_message("[VRM140] humanoid bone 'x'; skipped")
    assert code == "VRM140", code
    assert body == "humanoid bone 'x'; skipped", body

    # Un-coded legacy text degrades cleanly to a warning with no code.
    code, body = diag.parse_coded_message("plain legacy warning")
    assert code == "" and body == "plain legacy warning"

    assert diag.severity_of("VRM002") is Severity.ERROR
    assert diag.severity_of("VRM150") is Severity.INFO
    assert diag.severity_of("VRM200") is Severity.FATAL
    assert diag.severity_of("VRMZZZ") is Severity.WARNING  # unknown -> default

    d = diag.make_import_diagnostic("[VRM150] preserved raw only")
    assert d.code == "VRM150" and d.severity is Severity.INFO and d.source == "import"

    # Every C++ importer code must be catalogued (contract with VrmDiagnostics.h).
    for c in ("VRM001", "VRM101", "VRM110", "VRM140", "VRM160", "VRM180", "VRM181"):
        assert c in diag.CATALOG, c

    counts = diag.severity_counts([
        diag.make("VRM200", "x"), diag.make("VRM220", "y")])
    assert counts["FATAL"] == 1 and counts["INFO"] == 1, counts
    assert diag.worst_severity([diag.make("VRM220", "y"),
                                diag.make("VRM200", "x")]) is Severity.FATAL


def check_valid_fixtures():
    """Every committed fixture must import to a contract-valid stage."""
    for name in ("minimal.vrm", "vrm0_minimal.vrm", "multiskin_ibm.vrm",
                 "unordered_skel.vrm", "expressions.vrm", "springbone.vrm",
                 "lookat.vrm", "constraints.vrm", "textures.vrm", "animation.vrm",
                 "materials.vrm", "names.vrm"):
        stage = _open(name)
        diagnostics = validate_vrm.validate_stage(stage)
        errors = [d for d in diagnostics
                  if d.severity >= Severity.ERROR]
        assert not errors, f"{name}: unexpected errors: {[e.to_dict() for e in errors]}"


def check_out_of_range_joint_index():
    """A JOINTS_0 index past the skeleton's joint count is flagged VRM212."""
    stage = _open("minimal.vrm")
    body = stage.GetPrimAtPath("/Asset/geo/Body")
    primvar = UsdSkel.BindingAPI(body).GetJointIndicesPrimvar()
    indices = list(primvar.Get())
    indices[0] = 999  # far past the 2-joint skeleton
    primvar.GetAttr().Set(Vt.IntArray(indices))

    codes = _codes(validate_vrm.validate_stage(stage))
    assert "VRM212" in codes, codes


def check_broken_skeleton_target():
    """A skel:skeleton pointing at a non-Skeleton prim is flagged VRM211."""
    stage = _open("minimal.vrm")
    body = stage.GetPrimAtPath("/Asset/geo/Body")
    rel = UsdSkel.BindingAPI(body).GetSkeletonRel()
    rel.SetTargets([Sdf.Path("/Asset/geo/Accessory")])  # a Mesh, not a Skeleton

    codes = _codes(validate_vrm.validate_stage(stage))
    assert "VRM211" in codes, codes


def check_broken_humanoid_bone():
    """A humanoid bone token that isn't a real joint path is flagged VRM232."""
    stage = _open("minimal.vrm")
    humanoid = stage.GetPrimAtPath("/Asset/rig/Humanoid")
    humanoid.GetAttribute("vrm:humanBones:hips").Set("not/a/joint")

    codes = _codes(validate_vrm.validate_stage(stage))
    assert "VRM232" in codes, codes


def check_springbone_end_node_vs_broken_path():
    """A bare end-node name is legitimate; only a broken joint *path* is VRM250."""
    stage = _open("springbone.vrm")
    hair = stage.GetPrimAtPath("/Asset/rig/SecondaryMotion/SpringBones/Hair")
    attr = hair.GetAttribute("vrm:joints")
    joints = list(attr.Get())

    # A bare leaf name that isn't on the skeleton (the tail "end" node) must NOT
    # be flagged — it's the documented non-skin source-node fallback.
    attr.Set(Vt.TokenArray(joints + ["hair_tail_end"]))
    assert "VRM250" not in _codes(validate_vrm.validate_stage(stage)), \
        "bare end-node name should not be flagged"

    # A hierarchical path that doesn't resolve to a joint is genuine breakage.
    attr.Set(Vt.TokenArray(joints + ["bogus/missing/joint"]))
    assert "VRM250" in _codes(validate_vrm.validate_stage(stage)), \
        "broken joint path should be flagged VRM250"


def check_missing_default_prim():
    """No default prim is a fatal VRM200 and short-circuits the rest."""
    stage = _open("minimal.vrm")
    stage.ClearDefaultPrim()
    diagnostics = validate_vrm.validate_stage(stage)
    assert diagnostics and diagnostics[0].code == "VRM200"
    assert diagnostics[0].severity is Severity.FATAL


def check_report_sections_and_coded_import_warnings():
    """The report carries all sections; badext's import warning arrives coded."""
    report = vrm_report.build_report(str(FIXTURES / "badext.vrm"))
    assert report["summary"]["opened"] is True
    for key in ("summary", "diagnostics", "assetInventory", "compatibility"):
        assert key in report, key

    comp = report["compatibility"]
    assert comp["sourceFormat"] == "VRM", comp
    assert comp["rawExtensionPreserved"] is True, comp

    # The out-of-range humanoid bone is surfaced by the freshly built importer as
    # a coded VRM140 diagnostic (not free text) and classified as a warning.
    import_diags = [d for d in report["diagnostics"] if d["source"] == "import"]
    assert any(d["code"] == "VRM140" for d in import_diags), import_diags
    assert all(d.get("code", "").startswith("VRM") for d in import_diags), import_diags


def check_report_valid_avatar():
    """A clean fixture reports COMPATIBLE with a resolvable asset inventory."""
    # textures.vrm exercises the asset inventory (embedded texture must resolve).
    report = vrm_report.build_report(str(FIXTURES / "textures.vrm"))
    assert report["summary"]["valid"] is True, report["summary"]
    inv = report["assetInventory"]
    assert inv["resolved"] >= 1 and inv["missing"] == 0, inv
    assert report["compatibility"]["features"]["meshes"] >= 1, report["compatibility"]

    # minimal.vrm exercises the skeleton/humanoid feature flags.
    skinned = vrm_report.build_report(str(FIXTURES / "minimal.vrm"))
    assert skinned["summary"]["valid"] is True, skinned["summary"]
    feats = skinned["compatibility"]["features"]
    assert feats["skeleton"] is True and feats["humanoid"] is True, feats


def main() -> int:
    plugin_path = os.environ.get("PXR_PLUGINPATH_NAME")
    if plugin_path:
        Plug.Registry().RegisterPlugins(plugin_path.split(os.pathsep))
    assert Sdf.FileFormat.FindByExtension("vrm"), \
        "usdVrm SdfFileFormat is not registered"

    for check in (check_taxonomy, check_valid_fixtures,
                  check_out_of_range_joint_index, check_broken_skeleton_target,
                  check_broken_humanoid_bone,
                  check_springbone_end_node_vs_broken_path,
                  check_missing_default_prim,
                  check_report_sections_and_coded_import_warnings,
                  check_report_valid_avatar):
        check()
        print(f"  {check.__name__}: OK")
    print("usdVrm validate/report tests: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
