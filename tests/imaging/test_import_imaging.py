#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""A `.vrm` imported, read through Hydra, against what its source says
(VRM_IMAGING_POLICY.md §17.3, imaging track Step I5).

    .vrm -> usdVrmFileFormat -> Vrm*API -> vrmImaging -> Hydra data

The expectation comes from the source file, not from the stage:
`material_oracle.expected()` restates, from a VRM 1.0 GLB's JSON, what the
canonical semantics of each material must be -- the importer suite's
independent oracle. This writes that out per Material prim, as JSON, and hands
it with the `.vrm` to `vrmImaging_import_tests`, which reads every value
through UsdImaging's stage scene index.

The two committed MToon fixtures are both read against `mtoon_vrm1.vrm`'s
source. The oracle does not restate VRM 0.x: `mtoon_vrm1.vrm` is
`mtoon_vrm0.vrm` as UniVRM migrates it, and the importer suite holds the two
stages equal (`check_mtoon_vrm0_matches_vrm1`). So the 0.x file read through
Hydra has to give the 1.0 source's semantics too, which is what the migration
means to a renderer.

The stage is opened here only to name each source material's prim
(`vrm:sourceMaterialIndex`); no value is read from it.
"""

from __future__ import annotations

import argparse
import json
import pathlib
import subprocess
import sys

CORE = "inputs:vrm:material:"
MTOON = "inputs:vrm:mtoon:"

# The fixture pair's six materials, as the importer suite names them: at least
# these, so a fixture that lost one fails here rather than passing on fewer.
MATERIALS = {"Hair", "Face", "Veil", "Tear", "Lashes", "Plain"}


def plain(value):
    if isinstance(value, bytes):
        return value.hex()
    if isinstance(value, tuple):
        return [plain(v) for v in value]
    return value


def expectation(oracle, vrm: pathlib.Path) -> dict:
    """The source's canonical semantics per Material prim path."""
    from pxr import Usd, UsdShade

    glb = oracle.read_glb(str(vrm))
    assert glb is not None, f"{vrm.name} is not a GLB"
    gltf, binary = glb
    assert "VRMC_vrm" in gltf.get("extensions", {}), (
        f"{vrm.name} is not VRM 1.0, which the oracle restates")

    stage = Usd.Stage.Open(str(vrm))
    assert stage, f"{vrm.name} does not open: is usdVrmFileFormat registered?"
    paths = {}
    for prim in stage.Traverse():
        if prim.IsA(UsdShade.Material):
            index = prim.GetCustomData().get("vrm", {}).get("sourceMaterialIndex")
            assert index is not None, f"{prim.GetPath()}: no vrm:sourceMaterialIndex"
            paths[index] = str(prim.GetPath())
    assert sorted(paths) == list(range(len(gltf.get("materials", [])))), (
        f"{vrm.name}: material prims {sorted(paths)} for "
        f"{len(gltf.get('materials', []))} source materials")

    materials = {}
    for index, path in sorted(paths.items()):
        want = oracle.expected(gltf, binary, index)
        fields = {"material": {}}
        for name, value in want["attrs"].items():
            if name.startswith(CORE):
                fields["material"][name[len(CORE):]] = plain(value)
            elif name.startswith(MTOON):
                fields.setdefault("mtoon", {})[name[len(MTOON):]] = plain(value)
            else:
                raise AssertionError(f"{path}: the oracle maps {name}, which "
                                     f"is in no group this suite reads")
        assert ("mtoon" in fields) == ("VrmMToonAPI" in want["schemas"]), path
        groups = sorted(fields) + (["textureInfo"] if want["textures"] else [])
        textures = {role: {field: plain(value) for field, value in texture.items()}
                    for role, texture in want["textures"].items()}
        materials[path] = {"groups": groups, "fields": fields,
                           "textures": textures}
    return {"source": vrm.name, "materials": materials}


def material_paths(vrm: pathlib.Path) -> set[str]:
    from pxr import Usd, UsdShade

    stage = Usd.Stage.Open(str(vrm))
    assert stage, f"{vrm.name} does not open"
    return {str(prim.GetPath()) for prim in stage.Traverse()
            if prim.IsA(UsdShade.Material)}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--suite", required=True,
                        help="the vrmImaging_import_tests executable")
    parser.add_argument("--oracle-dir", required=True,
                        help="the directory holding material_oracle.py")
    parser.add_argument("--vrm1", required=True, type=pathlib.Path,
                        help="the VRM 1.0 fixture the oracle reads")
    parser.add_argument("--vrm0", required=True, type=pathlib.Path,
                        help="its VRM 0.x source, read against the same "
                             "expectation")
    parser.add_argument("--work", required=True, type=pathlib.Path)
    args = parser.parse_args()

    sys.path.insert(0, args.oracle_dir)
    import material_oracle  # noqa: E402

    want = expectation(material_oracle, args.vrm1)
    names = {path.rsplit("/", 1)[-1] for path in want["materials"]}
    assert MATERIALS <= names, f"the fixture's materials are {sorted(names)}"
    assert any("mtoon" in m["fields"] and len(m["textures"]) >= 5
               for m in want["materials"].values()), (
        "no MToon material with its textures: the fixture no longer exercises "
        "the MToon half")
    # The pair names its materials alike, or one expectation cannot serve both.
    assert material_paths(args.vrm0) == set(want["materials"]), (
        f"{args.vrm0.name}'s materials are not {args.vrm1.name}'s")

    args.work.mkdir(parents=True, exist_ok=True)
    expected = args.work / "import_imaging.expected.json"
    expected.write_text(json.dumps(want, indent=1, sort_keys=True) + "\n",
                        encoding="utf-8")

    failed = 0
    for vrm in (args.vrm1, args.vrm0):
        print(f"$ vrmImaging_import_tests {vrm.name} {expected.name}", flush=True)
        result = subprocess.run([args.suite, str(vrm), str(expected)],
                                capture_output=True, text=True,
                                encoding="utf-8", errors="replace")
        sys.stdout.write(result.stdout)
        sys.stderr.write(result.stderr)
        if result.returncode != 0:
            print(f"{vrm.name}: exit {result.returncode}", file=sys.stderr)
            failed += 1
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
