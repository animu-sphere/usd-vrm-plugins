#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Create a portable USD package from a VRM import.

The usdVrm importer may author texture paths that resolve through Ar, including
package-relative paths inside the source .vrm. This tool is the portable handoff
step: it opens the VRM through usdVrm, copies resolved texture bytes into a
package directory, rewrites their USD asset paths to package-relative paths,
exports a USDA layer, and writes a JSON inventory report.

Run inside an environment where the usdVrm plugin is discoverable, for example:

    ost plugin run plugins/usdVrm -- python plugins/usdVrm/tools/package_vrm.py \
        avatar.vrm packaged/avatar
"""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import re
import sys
from typing import Any

from pxr import Ar, Sdf, Usd


REPORT_SCHEMA_VERSION = 1


def _package_member_path(value: str, label: str) -> str:
    windows_path = pathlib.PureWindowsPath(value)
    normalized = pathlib.PurePosixPath(value.replace("\\", "/"))
    if windows_path.drive or windows_path.root or normalized.is_absolute():
        raise ValueError(f"{label} must be relative to the package directory")
    parts = [part for part in normalized.parts if part not in ("", ".")]
    if not parts or ".." in parts:
        raise ValueError(f"{label} must stay inside the package directory")
    return pathlib.PurePosixPath(*parts).as_posix()


def _sanitize_stem(stem: str) -> str:
    cleaned = re.sub(r"[^A-Za-z0-9._-]+", "_", stem).strip("._-")
    return cleaned or "asset"


def _asset_path_parts(value: Sdf.AssetPath) -> tuple[str, str]:
    authored = getattr(value, "path", "") or ""
    resolved = getattr(value, "resolvedPath", "") or ""
    return authored, resolved


def _is_usd_asset_attr(attr: Usd.Attribute) -> bool:
    return attr.GetTypeName() == Sdf.ValueTypeNames.Asset


def _shader_id(prim: Usd.Prim) -> str:
    attr = prim.GetAttribute("info:id")
    if not attr:
        return ""
    value = attr.Get()
    return str(value) if value else ""


def _asset_kind(prim: Usd.Prim, attr: Usd.Attribute) -> str:
    if attr.GetName() == "inputs:file" and _shader_id(prim) == "UsdUVTexture":
        return "texture"
    return "asset"


def _iter_asset_attributes(stage: Usd.Stage) -> list[dict[str, Any]]:
    assets: list[dict[str, Any]] = []
    for prim in Usd.PrimRange(stage.GetPseudoRoot()):
        for attr in prim.GetAttributes():
            if not _is_usd_asset_attr(attr):
                continue
            value = attr.Get()
            if not isinstance(value, Sdf.AssetPath):
                continue
            authored, resolved = _asset_path_parts(value)
            assets.append(
                {
                    "primPath": prim.GetPath().pathString,
                    "attribute": attr.GetName(),
                    "attributePath": attr.GetPath().pathString,
                    "kind": _asset_kind(prim, attr),
                    "authoredPath": authored,
                    "resolvedPath": resolved,
                    "_attr": attr,
                }
            )
    return assets


def _source_path_for(
    entry: dict[str, Any], source_dir: pathlib.Path
) -> pathlib.Path | None:
    for key in ("resolvedPath", "authoredPath"):
        raw = entry.get(key) or ""
        if not raw or "://" in raw:
            continue
        path = pathlib.Path(raw)
        if not path.is_absolute():
            path = source_dir / path
        if path.exists() and path.is_file():
            return path
    return None


def _asset_path_candidates(entry: dict[str, Any]) -> list[str]:
    candidates: list[str] = []
    for key in ("resolvedPath", "authoredPath"):
        raw = entry.get(key) or ""
        if raw and raw not in candidates:
            candidates.append(raw)
    return candidates


def _read_source_bytes(
    entry: dict[str, Any], source_dir: pathlib.Path
) -> tuple[bytes, str] | None:
    local_path = _source_path_for(entry, source_dir)
    if local_path is not None:
        return local_path.read_bytes(), str(local_path)

    resolver = Ar.GetResolver()
    for raw in _asset_path_candidates(entry):
        resolved = resolver.Resolve(raw)
        if not resolved:
            resolved = Ar.ResolvedPath(raw)
        asset = resolver.OpenAsset(resolvedPath=resolved)
        if not asset:
            continue
        with asset:
            return asset.Read(asset.GetSize(), 0), str(resolved)
    return None


def _hash_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _asset_extension(entry: dict[str, Any], fallback: str = ".bin") -> str:
    resolver = Ar.GetResolver()
    for raw in _asset_path_candidates(entry):
        ext = resolver.GetExtension(raw)
        if ext:
            return f".{ext.lower()}"
    return fallback


def _asset_stem(entry: dict[str, Any]) -> str:
    raw = (entry.get("authoredPath") or entry.get("resolvedPath") or "asset").replace(
        "\\", "/"
    )
    if "[" in raw and raw.endswith("]"):
        raw = raw.rsplit("[", 1)[1][:-1]
    return pathlib.PurePosixPath(raw).stem


def _package_asset_bytes(
    data: bytes,
    stem_hint: str,
    suffix: str,
    asset_dir: pathlib.Path,
    asset_dir_name: str,
    digest_to_name: dict[str, str],
) -> tuple[str, str]:
    digest = _hash_bytes(data)
    if digest in digest_to_name:
        return digest, digest_to_name[digest]

    stem = _sanitize_stem(stem_hint)
    name = f"{stem}-{digest[:16]}{suffix}"
    target = asset_dir / name
    asset_dir.mkdir(parents=True, exist_ok=True)
    target.write_bytes(data)

    portable_path = pathlib.PurePosixPath(asset_dir_name, name).as_posix()
    digest_to_name[digest] = portable_path
    return digest, portable_path


def package_stage(
    input_path: str | pathlib.Path,
    output_dir: str | pathlib.Path,
    *,
    stage_name: str = "asset.usda",
    textures_dir: str = "textures",
    report_name: str = "package_report.json",
    force: bool = False,
) -> dict[str, Any]:
    """Package a VRM/USD stage and return the JSON-serializable report."""

    src = pathlib.Path(input_path)
    out_dir = pathlib.Path(output_dir)
    stage_name = _package_member_path(stage_name, "stage_name")
    textures_dir = _package_member_path(textures_dir, "textures_dir")
    report_name = _package_member_path(report_name, "report_name")
    if out_dir.exists() and any(out_dir.iterdir()) and not force:
        raise FileExistsError(
            f"output directory is not empty: {out_dir} "
            "(pass --force to overwrite files in place)"
        )
    out_dir.mkdir(parents=True, exist_ok=True)

    stage = Usd.Stage.Open(str(src))
    if not stage:
        raise RuntimeError(f"failed to open stage: {src}")

    source_layer_dir = src.resolve().parent
    asset_dir = out_dir / textures_dir
    digest_to_name: dict[str, str] = {}
    inventory = _iter_asset_attributes(stage)

    packaged = 0
    missing = 0
    skipped = 0
    for entry in inventory:
        attr = entry.pop("_attr")
        if entry["kind"] != "texture":
            entry["status"] = "skipped"
            entry["reason"] = "not a texture asset"
            skipped += 1
            continue

        source_asset = _read_source_bytes(entry, source_layer_dir)
        if source_asset is None:
            entry["status"] = "missing"
            entry["reason"] = "asset path did not resolve to readable bytes"
            missing += 1
            continue

        source_bytes, source_description = source_asset
        digest, portable_path = _package_asset_bytes(
            source_bytes,
            _asset_stem(entry),
            _asset_extension(entry),
            asset_dir,
            textures_dir,
            digest_to_name,
        )
        attr.Set(Sdf.AssetPath(portable_path))
        entry["status"] = "packaged"
        entry["sourceFile"] = source_description
        entry["sha256"] = digest
        entry["portablePath"] = portable_path
        packaged += 1

    stage_path = out_dir / stage_name
    stage_path.parent.mkdir(parents=True, exist_ok=True)
    if stage_path.exists() and not force:
        raise FileExistsError(f"stage already exists: {stage_path}")
    if not stage.GetRootLayer().Export(str(stage_path)):
        raise RuntimeError(f"failed to export package stage: {stage_path}")

    report = {
        "schemaVersion": REPORT_SCHEMA_VERSION,
        "source": str(src),
        "packageRoot": str(out_dir),
        "stage": stage_name,
        "pathPolicy": {
            "textureAssetPaths": "relative-to-package-root",
            "textureDirectory": textures_dir,
        },
        "summary": {
            "assetAttributes": len(inventory),
            "texturesPackaged": packaged,
            "missingAssets": missing,
            "skippedAssets": skipped,
        },
        "assets": inventory,
    }

    report_path = out_dir / report_name
    report_path.parent.mkdir(parents=True, exist_ok=True)
    if report_path.exists() and not force:
        raise FileExistsError(f"report already exists: {report_path}")
    report_path.write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    return report


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Package an imported VRM stage with portable texture asset paths."
    )
    parser.add_argument("input", help="Input .vrm/.usd stage to open through OpenUSD")
    parser.add_argument("output_dir", help="Directory to write the package into")
    parser.add_argument(
        "--stage-name",
        default="asset.usda",
        help="Output USDA filename relative to output_dir (default: asset.usda)",
    )
    parser.add_argument(
        "--textures-dir",
        default="textures",
        help="Texture directory inside the package (default: textures)",
    )
    parser.add_argument(
        "--report-name",
        default="package_report.json",
        help="Inventory report filename relative to output_dir",
    )
    parser.add_argument(
        "--force",
        action="store_true",
        help="Allow overwriting files inside an existing package directory",
    )
    return parser


def main(argv: list[str] | None = None) -> int:
    args = _build_parser().parse_args(argv)
    try:
        report = package_stage(
            args.input,
            args.output_dir,
            stage_name=args.stage_name,
            textures_dir=args.textures_dir,
            report_name=args.report_name,
            force=args.force,
        )
    except Exception as exc:
        print(f"package_vrm.py: {exc}", file=sys.stderr)
        return 1

    print(json.dumps(report["summary"], indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())
