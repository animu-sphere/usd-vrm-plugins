#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Run pinned upstream solvers outside the source tree; commit data, never sources.

Requires Node/npm, and a recent Roslyn csc + .NET Framework on Windows for
UniVRM (or an equivalent compiler/runtime). Ordinary tests need neither.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import urllib.request

HERE = Path(__file__).resolve().parent
SOURCES = ("SceneInfo", "SphereCollider", "SpringBoneJointInit", "SpringBoneJointState", "SpringBoneSettings", "SpringBoneSystem")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--work-dir", type=Path, required=True)
    parser.add_argument("--csc", required=True, help="Roslyn csc executable")
    args = parser.parse_args()
    work = args.work_dir.resolve()
    if work == HERE or HERE in work.parents:
        parser.error("reference sources must stay outside the committed fixture directory")
    work.mkdir(parents=True, exist_ok=True)
    npm = shutil.which("npm.cmd") or shutil.which("npm")
    subprocess.run([npm, "install", "--prefix", str(work), "--ignore-scripts", "--no-audit", "--no-fund", "--save-exact", "three@0.170.0", "@pixiv/three-vrm-springbone@3.4.5"], check=True)
    subprocess.run(["node", str(HERE / "generate_three.cjs"), str(work), str(HERE / "three-vrm.csv")], check=True)
    hashes = {}
    for name in SOURCES:
        url = f"https://raw.githubusercontent.com/vrm-c/UniVRM/v0.128.1/Assets/VRM/Runtime/SpringBone/Logic/{name}.cs"
        data = urllib.request.urlopen(url).read()
        hashes[name + ".cs"] = hashlib.sha256(data).hexdigest()
        (work / (name + ".cs")).write_bytes(data)
    exe = work / "GenerateUniVRM.exe"
    subprocess.run([args.csc, "/nologo", "/langversion:latest", "/r:System.Web.Extensions.dll", f"/out:{exe}", str(HERE / "UnityMathShim.cs"), str(HERE / "GenerateUniVRM.cs"), *[str(work / (name + ".cs")) for name in SOURCES]], check=True)
    subprocess.run([str(exe), str(HERE / "cases.json"), str(HERE / "univrm.csv")], check=True)
    provenance = {"three-vrm": "3.4.5", "three": "0.170.0", "UniVRM": "v0.128.1", "UniVRM_source_sha256": hashes}
    (HERE / "provenance.json").write_text(json.dumps(provenance, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
