#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Compatibility report for a usdVrm import.

Merges everything the toolchain knows about one imported avatar into a single
report, in both a human-readable form and machine JSON:

* **summary**      — VRM version, overall verdict, diagnostic counts by severity.
* **diagnostics**  — import-time messages (coded, off the stage's
                     ``vrm:warnings``) plus the stage validator's findings, all
                     in the shared taxonomy.
* **assetInventory** — every USD asset attribute (textures et al.) and whether
                     it resolves.
* **compatibility** — which VRM feature areas are present on the stage.

Run inside an environment where the usdVrm plugin is discoverable, e.g.:

    ost plugin run plugins/usdVrm -- \
        python plugins/usdVrm/tools/vrm_report.py avatar.vrm --json
"""

from __future__ import annotations

import argparse
import json
import pathlib
import sys
from typing import Any

from pxr import Ar, Plug, Tf, Usd, UsdGeom, UsdSkel

import validate_vrm
import vrm_diagnostics as diag
from vrm_diagnostics import Diagnostic, Severity
from package_vrm import _iter_asset_attributes

REPORT_SCHEMA_VERSION = 1


def _import_diagnostics(dp: Usd.Prim) -> list[Diagnostic]:
    """Recover import-time diagnostics from the stage's vrm:warnings."""

    vrm = dp.GetCustomData().get("vrm", {}) if dp and dp.IsValid() else {}
    warnings = vrm.get("warnings") or []
    return [diag.make_import_diagnostic(str(w)) for w in warnings]


def _asset_inventory(stage: Usd.Stage) -> tuple[list[dict[str, Any]], int, int]:
    """Asset attributes with a resolvability status. Returns (entries, ok, missing)."""

    resolver = Ar.GetResolver()
    entries = _iter_asset_attributes(stage)
    resolved = 0
    missing = 0
    for entry in entries:
        attr = entry.pop("_attr", None)
        value = attr.Get() if attr else None
        is_ok = validate_vrm.asset_path_resolves(stage, value, resolver)
        entry["resolves"] = is_ok
        if is_ok:
            resolved += 1
        else:
            missing += 1
    return entries, resolved, missing


def _compatibility(stage: Usd.Stage, dp: Usd.Prim) -> dict[str, Any]:
    """A feature-presence summary: what this avatar actually carries."""

    vrm = dp.GetCustomData().get("vrm", {}) if dp and dp.IsValid() else {}
    meshes = [p for p in stage.Traverse() if p.IsA(UsdGeom.Mesh)]
    skeletons = [p for p in stage.Traverse() if p.IsA(UsdSkel.Skeleton)]
    blendshapes = [p for p in stage.Traverse() if p.IsA(UsdSkel.BlendShape)]
    animations = [p for p in stage.Traverse() if p.IsA(UsdSkel.Animation)]

    def _has(path: str) -> bool:
        p = stage.GetPrimAtPath(path)
        return bool(p and p.IsValid())

    springs = stage.GetPrimAtPath(validate_vrm.SPRINGBONES_PATH)
    expressions = stage.GetPrimAtPath(validate_vrm.EXPRESSIONS_PATH)
    return {
        "sourceFormat": vrm.get("sourceFormat", ""),
        "sourceVersion": vrm.get("sourceVersion", ""),
        "specVersion": vrm.get("specVersion", ""),
        "schemaContractVersion": vrm.get("schemaContractVersion"),
        "frontAxisNormalized": vrm.get("frontAxisNormalized"),
        "features": {
            "meshes": len(meshes),
            "skeleton": bool(skeletons),
            "blendShapes": len(blendshapes),
            "animations": len(animations),
            "humanoid": _has(validate_vrm.HUMANOID_PATH),
            "expressions": bool(expressions and expressions.IsValid()
                                and expressions.GetChildren()),
            "lookAt": _has("/Asset/rig/LookAt"),
            "springBones": bool(springs and springs.IsValid()
                                and springs.GetChildren()),
            "constraints": _has("/Asset/rig/Constraints"),
        },
        "rawExtensionPreserved": bool(vrm.get("rawExtension")),
        "metaPreserved": bool(vrm.get("meta")),
    }


def _build_info() -> dict[str, Any] | None:
    """The build-metadata stamp of the discovered usdVrm plugin, if it has one.

    CMake configures ``buildInfo.json`` next to ``plugInfo.json`` (commit /
    toolchain / OpenUSD / build type), so the report can state exactly which
    binary produced its findings. ``None`` when the discovered plugin predates
    the stamp or the file is unreadable.
    """

    plugin = Plug.Registry().GetPluginWithName("UsdVrmFileFormat")
    if not plugin:
        return None
    stamp = pathlib.Path(plugin.resourcePath) / "buildInfo.json"
    try:
        return json.loads(stamp.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None


def build_report(path: str) -> dict[str, Any]:
    """Open `path` and produce the full compatibility report dict."""

    try:
        stage = Usd.Stage.Open(str(path))
    except Tf.ErrorException:
        stage = None
    if not stage:
        diagnostics = [diag.make("VRM200", f"failed to open stage: {path}")]
        return {
            "schemaVersion": REPORT_SCHEMA_VERSION,
            "source": str(path),
            "summary": {"opened": False, "valid": False,
                        "counts": diag.severity_counts(diagnostics)},
            "diagnostics": [d.to_dict() for d in diagnostics],
            "assetInventory": {"assets": [], "resolved": 0, "missing": 0},
            "compatibility": {},
            "build": _build_info(),
        }

    dp = stage.GetDefaultPrim()
    diagnostics = _import_diagnostics(dp) + validate_vrm.validate_stage(stage)
    entries, resolved, missing = _asset_inventory(stage)
    counts = diag.severity_counts(diagnostics)
    valid = counts[Severity.ERROR.label] == 0 and counts[Severity.FATAL.label] == 0

    return {
        "schemaVersion": REPORT_SCHEMA_VERSION,
        "source": str(path),
        "summary": {
            "opened": True,
            "valid": valid,
            "counts": counts,
        },
        "diagnostics": [d.to_dict() for d in diagnostics],
        "assetInventory": {
            "assets": entries,
            "resolved": resolved,
            "missing": missing,
        },
        "compatibility": _compatibility(stage, dp),
        "build": _build_info(),
    }


def render_text(report: dict[str, Any]) -> str:
    s = report["summary"]
    c = s["counts"]
    lines = [
        f"VRM compatibility report: {report['source']}",
        f"  opened : {s['opened']}",
    ]
    comp = report.get("compatibility", {})
    if comp:
        lines.append(
            f"  source : {comp.get('sourceFormat', '')} "
            f"{comp.get('sourceVersion', '')} (spec {comp.get('specVersion', '')})")
        lines.append(
            f"  contract: usdVrm schema v{comp.get('schemaContractVersion', '?')}")
        feats = comp.get("features", {})
        present = [k for k, v in feats.items() if v]
        lines.append(f"  features: {', '.join(present) if present else '(none)'}")
    build = report.get("build")
    if build:
        lines.append(
            f"  build  : usdVrm {build.get('pluginVersion', '?')} "
            f"@ {str(build.get('gitCommit', '?'))[:12]} | "
            f"OpenUSD {build.get('openusdVersion', '?')} | "
            f"{build.get('compiler', '?')} {build.get('buildType', '')} "
            f"on {build.get('buildOs', '?')}")
    inv = report["assetInventory"]
    lines.append(
        f"  assets : {inv['resolved']} resolved, {inv['missing']} unresolved")
    if report["diagnostics"]:
        lines.append("  diagnostics:")
        for d in report["diagnostics"]:
            loc = f" @ {d['primPath']}" if d.get("primPath") else ""
            code = d.get("code") or "----"
            lines.append(f"    [{d['severity']:<7}] {code} {d['message']}{loc}")
    lines.append(
        f"  -> {'COMPATIBLE' if s['valid'] else 'INCOMPATIBLE'}: "
        f"{c['FATAL']} fatal, {c['ERROR']} error, "
        f"{c['WARNING']} warning, {c['INFO']} info")
    return "\n".join(lines)


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Produce a compatibility report for an imported VRM stage.")
    parser.add_argument("input", help="Stage to report on (.vrm/.usd/.usda)")
    parser.add_argument(
        "--json", action="store_true", help="Emit the machine-readable JSON report")
    parser.add_argument(
        "--out", help="Write the JSON report to this path in addition to stdout")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = _build_parser().parse_args(argv)
    report = build_report(args.input)
    if args.out:
        with open(args.out, "w", encoding="utf-8") as fh:
            json.dump(report, fh, indent=2, sort_keys=True)
            fh.write("\n")
    if args.json:
        print(json.dumps(report, indent=2, sort_keys=True))
    else:
        print(render_text(report))
    if not report["summary"]["opened"]:
        return 2
    return 0 if report["summary"]["valid"] else 1


if __name__ == "__main__":
    sys.exit(main())
