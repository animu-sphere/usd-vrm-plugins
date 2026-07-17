#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Clean-install smoke assertions for a *packaged* usdVrmFileFormat bundle.

This runs inside a runtime session whose plugin path points **only** at an
extracted `ost plugin package` artifact — never the build tree or the source
bundle. It verifies the plan's clean-install contract (near-term plan §7.2):

  1. discovery      — USD resolves `.vrm`, served from the extracted package
  2. open           — a textured fixture and a real corpus avatar open
  3. validate       — the stage passes the standalone validator (no ERROR/FATAL)
  4. texture resolve — embedded textures resolve straight from the `.vrm`
                       container (package resolver), with no temp extraction

Env:
  VRM_PKG_ROOT   (required) extracted package root (holds plugin/, lib/, tests/)
  VRM_REPO_TOOLS (optional) path to plugins/usdVrmFileFormat/tools for the validator;
                 when unset, step 3 falls back to inline structural checks.

Exit code is non-zero on the first failed assertion.
"""
from __future__ import annotations

import os
import sys

from pxr import Ar, Plug, Sdf, Usd


def _norm(p: str | None) -> str:
    return (p or "").replace("\\", "/")


def _pkg_roots(pkg_root: str) -> tuple[str, ...]:
    """Acceptable lowercase prefixes for "inside the package".

    The extract dir and the paths USD reports may differ by symlink resolution
    (macOS: /var/folders/... vs /private/var/folders/...), so accept the root
    both as given and fully resolved.
    """

    real = _norm(os.path.realpath(pkg_root)).rstrip("/")
    return tuple({pkg_root.lower(), real.lower()})


def _fail(msg: str) -> None:
    print(f"FAIL: {msg}")
    sys.exit(1)


def _step(n: int, msg: str) -> None:
    print(f"[{n}/4] {msg}")


def check_discovery(pkg_root: str) -> None:
    _step(1, "discovery")
    ff = Sdf.FileFormat.FindByExtension("vrm")
    if ff is None:
        _fail("no file format registered for '.vrm'")
    print(f"      .vrm -> file format '{ff.formatId}'")

    vrm_plugins = [
        p for p in Plug.Registry().GetAllPlugins()
        if "vrmfileformat" in _norm(p.path).lower()
    ]
    if not vrm_plugins:
        _fail("usdVrmFileFormat plugin absent from the USD plugin registry")
    roots = _pkg_roots(pkg_root)
    served = [
        p for p in vrm_plugins
        if _norm(os.path.realpath(p.path)).lower().startswith(roots)
    ]
    if not served:
        leaked = ", ".join(_norm(p.path) for p in vrm_plugins)
        _fail(f"vrm plugin not served from the extracted package (build-tree/source leak?): {leaked}")
    print(f"      served from package: {_norm(served[0].path)}")


def open_stage(path: str) -> Usd.Stage:
    stage = Usd.Stage.Open(path)
    if not stage:
        _fail(f"failed to open {path}")
    return stage


def check_open(pkg_root: str) -> list[str]:
    _step(2, "open")
    opened: list[str] = []
    candidates = [
        f"{pkg_root}/tests/fixtures/textures.vrm",
        f"{pkg_root}/tests/corpus/spec-samples/vrm1/seed-san/Seed-san.vrm",
    ]
    for path in candidates:
        if not os.path.exists(path):
            print(f"      skip (absent): {os.path.basename(path)}")
            continue
        stage = open_stage(path)
        n = len(list(stage.Traverse()))
        if n == 0:
            _fail(f"{os.path.basename(path)} opened but has no prims")
        print(f"      opened {os.path.basename(path)} ({n} prims)")
        opened.append(path)
    if not opened:
        _fail("no sample avatar could be opened from the package")
    return opened


def check_validate(paths: list[str], repo_tools: str | None) -> None:
    _step(3, "validate")
    if repo_tools and os.path.isdir(repo_tools):
        sys.path.insert(0, repo_tools)
        try:
            import validate_vrm  # type: ignore
        except Exception as exc:  # pragma: no cover - import guard
            _fail(f"could not import validate_vrm from {repo_tools}: {exc}")
        for path in paths:
            diagnostics, opened = validate_vrm.validate_path(path)
            result = validate_vrm.build_result(path, diagnostics)
            counts = result["counts"]
            status = "VALID" if result["valid"] else "INVALID"
            print(f"      {os.path.basename(path)}: {status} "
                  f"({counts['FATAL']}F/{counts['ERROR']}E/"
                  f"{counts['WARNING']}W/{counts['INFO']}I)")
            if not result["valid"]:
                _fail(f"validator reported ERROR/FATAL for {path}")
        return
    # Fallback: inline structural contract when the validator tool is absent.
    print("      validate_vrm tool unavailable; inline structural checks")
    for path in paths:
        stage = open_stage(path)
        dp = stage.GetDefaultPrim()
        if not dp:
            _fail(f"{os.path.basename(path)} has no default prim")
        meshes = [p for p in stage.Traverse() if p.GetTypeName() == "Mesh"]
        materials = [p for p in stage.Traverse() if p.GetTypeName() == "Material"]
        if not meshes:
            _fail(f"{os.path.basename(path)} authored no Mesh")
        if not materials:
            _fail(f"{os.path.basename(path)} authored no Material")
        print(f"      {os.path.basename(path)}: default={dp.GetName()} "
              f"meshes={len(meshes)} materials={len(materials)}")


def check_texture_resolution(pkg_root: str) -> None:
    _step(4, "texture resolve")
    fx = f"{pkg_root}/tests/fixtures/textures.vrm"
    if not os.path.exists(fx):
        _fail("textures.vrm missing from package; cannot test texture resolution")
    stage = open_stage(fx)
    ctx = stage.GetPathResolverContext()
    resolver = Ar.GetResolver()
    n_tex = 0
    for prim in stage.Traverse():
        for attr in prim.GetAttributes():
            if attr.GetTypeName() != Sdf.ValueTypeNames.Asset:
                continue
            value = attr.Get()
            if not value:
                continue
            path = str(value.path)
            # Importer authors embedded textures as <avatar>.vrm[images/<hash>.<ext>].
            if ".vrm[" not in path:
                continue
            if not path.lower().startswith(_pkg_roots(pkg_root)):
                _fail(f"texture path escapes the package (temp-dir dependency?): {path}")
            with Ar.ResolverContextBinder(ctx):
                resolved = resolver.Resolve(path)
            if not resolved:
                _fail(f"packaged texture did not resolve: {path}")
            asset = resolver.OpenAsset(resolved)
            if not asset or asset.GetSize() <= 0:
                _fail(f"packaged texture resolved but is empty: {path}")
            n_tex += 1
            print(f"      resolved {asset.GetSize()}B from container: {path.split('/')[-1]}")
    if n_tex == 0:
        _fail("no packaged textures resolved from textures.vrm")


def main() -> int:
    pkg_root = _norm(os.environ.get("VRM_PKG_ROOT")).rstrip("/")
    if not pkg_root or not os.path.isdir(pkg_root):
        _fail("VRM_PKG_ROOT is unset or not a directory")
    repo_tools = os.environ.get("VRM_REPO_TOOLS")
    print(f"clean-install smoke against package: {pkg_root}")

    check_discovery(pkg_root)
    opened = check_open(pkg_root)
    check_validate(opened, repo_tools)
    check_texture_resolution(pkg_root)

    print("CLEAN_INSTALL_SMOKE_OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
