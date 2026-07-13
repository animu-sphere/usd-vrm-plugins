#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Schema-only verification for the vrmSchema bundle.

Runs with no .vrm importer in the session (WORKSPACE.md §1: schema tests must
not require the file-format bundle). Asserts:

  1. discovery — the `vrmSchema` plugin is registered and declares exactly the
     six Vrm*API types (WORKSPACE.md §6 invariant 5: registration moves are
     proven by discovery tests in the same PR).
  2. schema registry — every API has an applied-API prim definition whose
     builtin properties match the committed generatedSchema.usda.
  3. apply — ApplyAPI() by schema identifier works on an in-memory stage and
     the builtins expose their authored/fallback values.
  4. fixture — tests/fixtures/basic.usda opens and every prim that applies a
     Vrm*API validates against its prim definition.

Env: the vrmSchema plugInfo must be discoverable (PXR_PLUGINPATH_NAME), either
via `ost plugin run plugins/vrmSchema -- python tests/test_vrmschema_plugin.py`
or the CTest wiring in tests/CMakeLists.txt.
"""
from __future__ import annotations

import pathlib
import sys

from pxr import Plug, Sdf, Usd

BUNDLE = pathlib.Path(__file__).resolve().parents[1]

SCHEMA_APIS = [
    "VrmColliderAPI",
    "VrmConstraintAPI",
    "VrmExpressionAPI",
    "VrmHumanoidAPI",
    "VrmLookAtAPI",
    "VrmSpringBoneAPI",
]

_failures: list[str] = []


def check(cond: bool, msg: str) -> None:
    tag = "ok" if cond else "FAIL"
    print(f"[{tag}] {msg}")
    if not cond:
        _failures.append(msg)


def test_discovery() -> None:
    plugin = Plug.Registry().GetPluginWithName("vrmSchema")
    check(plugin is not None, "plugin 'vrmSchema' is registered")
    if not plugin:
        return
    types = dict(plugin.metadata).get("Types", {})
    declared = sorted(types.keys())
    expected = sorted(f"UsdVrm{name[3:]}" for name in SCHEMA_APIS)
    check(declared == expected,
          f"vrmSchema declares exactly the six schema types (got {declared})")
    for type_name, meta in types.items():
        check(dict(meta).get("schemaKind") == "singleApplyAPI",
              f"{type_name} is a singleApplyAPI")


def test_registry_matches_generated_schema() -> None:
    gen = BUNDLE / "plugin" / "resources" / "vrmSchema" / "generatedSchema.usda"
    layer = Sdf.Layer.OpenAsAnonymous(str(gen))
    check(layer is not None, "generatedSchema.usda parses")
    registry = Usd.SchemaRegistry()
    for prim in layer.rootPrims:
        prim_def = registry.FindAppliedAPIPrimDefinition(prim.name)
        check(prim_def is not None,
              f"{prim.name} has an applied-API prim definition")
        if not prim_def:
            continue
        authored = {p.name for p in prim.attributes}
        authored |= {r.name for r in prim.relationships}
        builtin = set(prim_def.GetPropertyNames())
        missing = sorted(authored - builtin)
        check(not missing,
              f"{prim.name}: all generatedSchema properties are builtins"
              + (f" (missing {missing})" if missing else ""))


def test_apply() -> None:
    stage = Usd.Stage.CreateInMemory()
    prim = stage.DefinePrim("/Rig", "Scope")
    for name in SCHEMA_APIS:
        check(bool(prim.ApplyAPI(name)), f"ApplyAPI('{name}') succeeds")
    applied = prim.GetAppliedSchemas()
    check(sorted(applied) == sorted(SCHEMA_APIS),
          f"all six APIs applied (got {sorted(applied)})")
    attr = prim.GetAttribute("vrm:humanBones:hips")
    check(attr.IsValid() and not attr.HasAuthoredValue(),
          "VrmHumanoidAPI builtin is valid without an authored value")
    attr.Set("J_Bip_C_Hips")
    check(attr.Get() == "J_Bip_C_Hips", "builtin token round-trips a value")


def test_fixture() -> None:
    fixture = BUNDLE / "tests" / "fixtures" / "basic.usda"
    stage = Usd.Stage.Open(str(fixture))
    check(stage is not None, "tests/fixtures/basic.usda opens")
    if not stage:
        return
    seen: set[str] = set()
    for prim in stage.Traverse():
        for schema in prim.GetAppliedSchemas():
            if not schema.startswith("Vrm"):
                continue
            seen.add(schema)
            prim_def = Usd.SchemaRegistry().FindAppliedAPIPrimDefinition(schema)
            check(prim_def is not None, f"{prim.GetPath()}: {schema} resolves")
            builtin = set(prim_def.GetPropertyNames()) if prim_def else set()
            for prop in prim.GetAuthoredProperties():
                if prop.GetName().startswith("vrm:"):
                    check(prop.GetName() in builtin,
                          f"{prim.GetPath()}.{prop.GetName()} is a "
                          f"{schema} builtin")
    check(seen == set(SCHEMA_APIS),
          f"fixture exercises all six APIs (got {sorted(seen)})")


def main() -> int:
    for test in (test_discovery, test_registry_matches_generated_schema,
                 test_apply, test_fixture):
        print(f"--- {test.__name__}")
        test()
    if _failures:
        print(f"\n{len(_failures)} check(s) FAILED")
        return 1
    print("\nall checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
