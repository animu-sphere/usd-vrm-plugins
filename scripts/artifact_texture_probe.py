#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Resolve a bake's embedded textures from a Python host, and say what loaded.

Run by `artifact_only_exec_smoke.py` in the installed product's environment,
never on its own. The smoke's product `motion_retarget` bakes a real `.vrm`
avatar; this opens that bake -- which references the avatar, so the importer
runs again in *this* process -- resolves every embedded texture through
OpenUSD's resolver and reads its bytes, then reports the plugins the registry
loaded and every module the process mapped.

Two things only a Python host measures, and both are open rows of the OpenExec
plan's P0-3:

* **embedded texture resolution from the artifact**: `avatar.vrm[images/...]`
  answered by the product's `usdVrmPackageResolver`, not by a build tree;
* **Windows DLL discovery in a Python host**: Python 3.8+ loads extension
  modules without `PATH`, and INSTALL.md could not say whether a plugin that
  OpenUSD loads *for* Python needs `os.add_dll_directory` too. This probe adds
  no DLL directory, so if it runs, `PATH` from the product's activation is
  enough.

Usage: artifact_texture_probe.py <bake.usda> <report.json>
Exit 0 with a report; 1 when a texture does not resolve or none exists.
"""
from __future__ import annotations

import json
import os
import pathlib
import sys

from pxr import Ar, Plug, Sdf, Usd


def loaded_modules() -> list[str]:
    """Every module this process mapped, by the path the loader resolved --
    the same enumeration `motion_retarget --load-report` and `exec_parity`
    report."""
    paths: set[str] = set()
    if os.name == "nt":
        import ctypes
        from ctypes import wintypes

        kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        psapi = ctypes.WinDLL("psapi", use_last_error=True)
        # Declared, not defaulted: a module handle is pointer-sized, and
        # ctypes' default int would truncate it on a 64-bit process.
        kernel32.GetCurrentProcess.restype = wintypes.HANDLE
        psapi.EnumProcessModules.argtypes = [
            wintypes.HANDLE, ctypes.POINTER(wintypes.HMODULE), wintypes.DWORD,
            ctypes.POINTER(wintypes.DWORD)]
        psapi.EnumProcessModules.restype = wintypes.BOOL
        psapi.GetModuleFileNameExW.argtypes = [
            wintypes.HANDLE, wintypes.HMODULE, wintypes.LPWSTR, wintypes.DWORD]
        psapi.GetModuleFileNameExW.restype = wintypes.DWORD
        process = kernel32.GetCurrentProcess()
        count = wintypes.DWORD()
        modules = (wintypes.HMODULE * 4096)()
        if psapi.EnumProcessModules(process, modules, ctypes.sizeof(modules),
                                    ctypes.byref(count)):
            buffer = ctypes.create_unicode_buffer(32768)
            for i in range(count.value // ctypes.sizeof(wintypes.HMODULE)):
                if psapi.GetModuleFileNameExW(process, modules[i], buffer,
                                              len(buffer)):
                    paths.add(buffer.value)
    elif sys.platform == "darwin":
        import ctypes

        dyld = ctypes.CDLL(None)
        dyld._dyld_image_count.restype = ctypes.c_uint32
        dyld._dyld_get_image_name.restype = ctypes.c_char_p
        dyld._dyld_get_image_name.argtypes = [ctypes.c_uint32]
        for i in range(dyld._dyld_image_count()):
            name = dyld._dyld_get_image_name(i)
            if name:
                paths.add(os.fsdecode(name))
    else:
        with open("/proc/self/maps", encoding="utf-8", errors="replace") as maps:
            for line in maps:
                slash = line.find("/")
                if slash != -1:
                    paths.add(line[slash:].rstrip("\n"))
    return sorted(paths)


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2
    bake, report_path = sys.argv[1], pathlib.Path(sys.argv[2])

    stage = Usd.Stage.Open(bake)
    textures = []
    failures = []
    if not stage:
        failures.append(f"the bake did not open: {bake}")
    else:
        context = stage.GetPathResolverContext()
        resolver = Ar.GetResolver()
        for prim in stage.Traverse():
            for attribute in prim.GetAttributes():
                if attribute.GetTypeName() != Sdf.ValueTypeNames.Asset:
                    continue
                value = attribute.Get()
                path = str(value.path) if value else ""
                # The importer authors an embedded texture as
                # <avatar>.vrm[images/<name>]: a package-relative path.
                if ".vrm[" not in path:
                    continue
                # Through the bake's reference the authored path may be
                # relative to the avatar's layer, which the attribute has
                # already anchored and resolved; resolve it here only when it
                # did not.
                resolved = value.resolvedPath
                if not resolved:
                    with Ar.ResolverContextBinder(context):
                        resolved = resolver.Resolve(path)
                asset = (resolver.OpenAsset(Ar.ResolvedPath(str(resolved)))
                         if resolved else None)
                size = asset.GetSize() if asset else 0
                if size <= 0:
                    failures.append(f"did not resolve to bytes: {path}")
                    continue
                textures.append({"path": path, "bytes": size})

    report = {
        "textures": textures,
        "failures": failures,
        "loaded_plugins": {plugin.name: plugin.path
                           for plugin in Plug.Registry().GetAllPlugins()
                           if plugin.isLoaded},
        "loaded_modules": loaded_modules(),
    }
    report_path.write_text(json.dumps(report, indent=2), encoding="utf-8")
    for failure in failures:
        print(f"FAIL: {failure}", file=sys.stderr)
    if not textures:
        print("FAIL: the bake carries no embedded texture", file=sys.stderr)
    print(f"resolved {len(textures)} embedded texture(s), "
          f"{sum(t['bytes'] for t in textures)} bytes")
    return 0 if textures and not failures else 1


if __name__ == "__main__":
    sys.exit(main())
