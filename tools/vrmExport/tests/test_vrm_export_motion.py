#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Motion assembly equals the existing bake, even without sources or plugins."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import zipfile

READ = r'''
import hashlib, json, re, sys
from pxr import Ar, Plug, Sdf, Usd, UsdGeom, UsdSkel
stage = Usd.Stage.Open(sys.argv[1])
assert stage and stage.GetDefaultPrim()
tracks, assets, poses = {}, {}, {}
cache = UsdSkel.Cache()
for prim in stage.Traverse():
    if prim.IsA(UsdSkel.Root):
        cache.Populate(UsdSkel.Root(prim), Usd.PrimDefaultPredicate)
times = set()
def value(x):
    return re.sub(r'(?<![\w.])-(0(?:\.0*)?)(?![\w.])', r'\1', repr(x))
for prim in stage.Traverse():
    for attr in prim.GetAuthoredAttributes():
        samples = attr.GetTimeSamples()
        if samples:
            times.update(samples)
            tracks[str(attr.GetPath())] = [[t, value(attr.Get(t))] for t in samples]
        if attr.GetTypeName() == Sdf.ValueTypeNames.Asset:
            asset = attr.Get()
            if asset and asset.path:
                resolved = asset.resolvedPath or Ar.GetResolver().Resolve(asset.path)
                data = Ar.GetResolver().OpenAsset(Ar.ResolvedPath(str(resolved)))
                assert data, (attr.GetPath(), asset.path)
                assets[str(attr.GetPath())] = hashlib.sha256(bytes(data.Read(data.GetSize(), 0))).hexdigest()
for prim in stage.Traverse():
    if prim.IsA(UsdSkel.Skeleton):
        query = cache.GetSkelQuery(UsdSkel.Skeleton(prim))
        assert query and query.GetAnimQuery(), str(prim.GetPath())
        poses[str(prim.GetPath())] = [
            [t, [round(float(x), 6) for matrix in query.ComputeJointLocalTransforms(t)
                 for row in matrix for x in row]] for t in sorted(times)]
print(json.dumps(dict(
    defaultPrim=str(stage.GetDefaultPrim().GetPath()),
    metrics=[stage.GetStartTimeCode(), stage.GetEndTimeCode(), stage.GetTimeCodesPerSecond(),
             UsdGeom.GetStageMetersPerUnit(stage), str(UsdGeom.GetStageUpAxis(stage))],
    tracks=tracks, assets=assets, poses=poses,
    plugins=[p.name for p in Plug.Registry().GetAllPlugins()
             if p.name.lower().startswith(('usdvrm', 'vrm', 'execvrm'))])))
'''


def run(args, env, code=0):
    result = subprocess.run([str(x) for x in args], env=env, capture_output=True,
                            text=True, encoding="utf-8", errors="replace")
    assert result.returncode == code, (args, result.returncode, result.stdout, result.stderr)
    return result


def read(path, env, bare=False):
    data = json.loads(run([sys.executable, "-c", READ, path], env).stdout)
    plugins = data.pop("plugins")
    if bare:
        assert not plugins, plugins
    return data


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tool", type=Path, required=True)
    parser.add_argument("--retarget", type=Path, required=True)
    parser.add_argument("--repo", type=Path, required=True)
    parser.add_argument("--plugin", action="append", required=True)
    args = parser.parse_args()
    env = dict(os.environ, PXR_PLUGINPATH_NAME=os.pathsep.join(args.plugin))
    bare = dict(env)
    bare.pop("PXR_PLUGINPATH_NAME", None)
    vrm = args.repo / "plugins/usdVrmFileFormat/tests"
    vrma = args.repo / "plugins/usdVrmaFileFormat/tests/fixtures"
    cases = [
        (vrm / "corpus/spec-samples/vrm1/seed-san/Seed-san.vrm", "canonical_walk"),
        (vrm / "corpus/spec-samples/vrm1/seed-san/Seed-san.vrm", "expressive_face"),
        (vrm / "fixtures/expressions_mtoon.vrm", "expressive_face"),
        (vrm / "fixtures/lookat.vrm", "gazing_head"),
        (vrm / "fixtures/vrm0_frontbake.vrm", "canonical_walk"),
    ]
    with tempfile.TemporaryDirectory(prefix="vrm motion 日本語 & ") as temp:
        scratch = Path(temp)
        temporary = scratch / "temporary"
        temporary.mkdir()
        env.update(TMP=str(temporary), TEMP=str(temporary), TMPDIR=str(temporary))
        # Simulate the installed-product tool layout. No PATH discovery is
        # needed, and spaces, Unicode and shell metacharacters reach argv intact.
        suffix = args.tool.suffix
        tool = scratch / "tools/vrm_export/bin" / args.tool.name
        retarget = scratch / "tools/motion_retarget/bin" / args.retarget.name
        for source, dest in [(args.tool, tool), (args.retarget, retarget)]:
            dest.parent.mkdir(parents=True)
            shutil.copy2(source, dest)
        for index, (avatar, clip) in enumerate(cases):
            work = scratch / str(index)
            sources = work / "sources"
            sources.mkdir(parents=True)
            source = sources / "avatar.vrm"
            motion = sources / "clip.vrma"
            shutil.copy2(avatar, source)
            shutil.copy2(vrma / (clip + ".vrma"), motion)
            baseline = work / "baseline.usda"
            run([retarget, "--avatar", source, "--animation", motion,
                 "--output", baseline, "--quiet"], env)
            expected = read(baseline, env)
            assert expected["tracks"] and expected["poses"]
            if clip == "expressive_face" and avatar.name == "Seed-san.vrm":
                assert any(x.endswith(".blendShapeWeights") for x in expected["tracks"])
            if avatar.name == "expressions_mtoon.vrm":
                assert any(".inputs:vrm:" in x for x in expected["tracks"])
            outputs = []
            for flatten in (False, True):
                for fmt in ("usda", "usdc", "usdz"):
                    out = work / ("flat" if flatten else "composed") / ("avatar." + fmt)
                    command = [tool, source, "--motion", motion, "-o", out,
                               "--check", "--dependency-report"]
                    if flatten:
                        command.append("--flatten")
                    report = json.loads(run(command, env).stdout)
                    outputs.append(out)
                    assert read(out, bare, True) == expected, (avatar, clip, fmt, flatten)
                    dependencies = report["dependencies"]
                    assert any(x["path"].endswith(".usdc") for x in dependencies) == (not flatten)
                    if index == 0 and not flatten and fmt == "usda":
                        native = out.parent / next(x["path"] for x in dependencies
                                                   if x["path"].endswith(".usdc"))
                        preserved = native.read_bytes()
                        collision = native.with_name(native.name.upper()) if os.name == "nt" else native
                        run([tool, source, "--motion", motion, "-o", collision,
                             "--overwrite"], env, 2)
                        assert native.read_bytes() == preserved
                    total = 0
                    if fmt == "usdz":
                        with zipfile.ZipFile(out) as package:
                            assert package.namelist()[0] == "defaultLayer.usdc"
                            assert all(not x.endswith((".vrm", ".vrma")) for x in package.namelist())
                            assert all(x.date_time == (2000, 1, 1, 0, 0, 0) for x in package.infolist())
                            for dependency in dependencies:
                                size = len(package.read(dependency["path"].removeprefix("./")))
                                assert size == dependency["sizeBytes"]
                                total += size
                    else:
                        for dependency in dependencies:
                            size = (out.parent / dependency["path"]).stat().st_size
                            assert size == dependency["sizeBytes"]
                            total += size
                    assert report["totalDependencyBytes"] == total
                    if fmt == "usda":
                        text = out.read_text(encoding="utf-8")
                        assert str(scratch).replace("\\", "/") not in text
                        assert ".vrm@" not in text and ".vrma@" not in text
                    original = out.read_bytes()
                    second = json.loads(run(command + ["--overwrite"], env).stdout)
                    assert out.read_bytes() == original and report == second
                    assert not list(temporary.iterdir()), "export left temporary files"
            sources.rename(work / "hidden-sources")
            for out in outputs:
                assert read(out, bare, True) == expected
            print(f"PASS {avatar.name} + {clip}: six portable animated outputs", flush=True)

        source, motion = cases[0][0], vrma / "canonical_walk.vrma"
        out = scratch / "failure.usda"
        for bad_motion, code in [("bad.usda", 2), (scratch / "missing.vrma", 3)]:
            result = run([tool, source, "--motion", bad_motion, "-o", out,
                          "--dependency-report"], env, code)
            assert not out.exists() and not result.stdout
        broken = scratch / "broken.vrma"
        broken.write_bytes(b"not a GLB")
        result = run([tool, source, "--motion", broken, "-o", out,
                      "--dependency-report"], env, 3)
        assert not out.exists() and not result.stdout
        no_vrma = dict(env, PXR_PLUGINPATH_NAME=os.pathsep.join(
            x for x in args.plugin if "usdVrmaFileFormat" not in x))
        result = run([tool, source, "--motion", motion, "-o", out], no_vrma, 3)
        assert "usdVrmaFileFormat" in result.stderr and not out.exists()
        retarget.rename(retarget.with_suffix(".hidden"))
        no_tool = dict(env, PATH=os.pathsep.join(
            x for x in env["PATH"].split(os.pathsep) if "motionRetarget" not in x))
        result = run([tool, source, "--motion", motion, "-o", out], no_tool, 4)
        assert "motion_retarget" in result.stderr and not out.exists()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
