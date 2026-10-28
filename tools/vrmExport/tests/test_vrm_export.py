#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""vrm_export end to end (docs/design/VRM_EXPORT_POLICY.md, export track Step 1).

Every input is exported to `.usda`, `.usdc` and `.usdz` with `--check`, and
every output is then read back **in a process with no VRM plugin on
PXR_PLUGINPATH_NAME** -- the claim the tool exists for, and the one its own
`--check` cannot make, since the tool's process has the plugins loaded
(policy §8). What is read back is compared with the source stage, read in
this process with the plugins loaded:

* the same prims, with the same types, authored metadata, authored
  attributes and values, and relationship targets;
* every asset-valued attribute pointing at `./textures/<fnv1a64>.<ext>`, whose
  bytes are the source's bytes (policy §6.2).

Then what each format promises on its own: a `.usda` names no path of this
machine and is byte-identical when exported twice (policy §7, §9); a
`.usdz` holds its root layer first, textures after, and no `.vrm`, every
entry stamped 2000-01-01 00:00, and is byte-identical when exported again
in another time zone (§9); the
temporary directory a `.usdz` is assembled in is gone afterwards (§6.3). And
the refusals of policy §5.3, from the command line, including the two that
name a missing plugin.

    python test_vrm_export.py --tool <vrm_export> \
        --plugin fileformat=<dir> --plugin resolver=<dir> --plugin schema=<dir> \
        <input.vrm> [<input.vrm> ...]
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import pathlib
import platform
import subprocess
import sys
import tempfile
import time
import zipfile

FORMATS = ("usda", "usdc", "usdz")

# A zone neither a developer's host nor a CI runner is likely to be in, with a
# DST rule.
OTHER_TIME_ZONE = "PST8PDT"

# A `.usdz` export's SHA-256 per CPU architecture (policy §9), measured
# 2026-10-26: Windows and Linux x86-64 agree; macOS arm64 writes the same
# texture entries and a different `defaultLayer.usdc` -- the values the
# importer computed, not the packaging. A digest changes whenever what the
# importer authors for that fixture does; a failure prints the new one.
# Contract v2's x86-64 pins were remeasured on Windows on 2026-10-27.
# The arm64 pins still describe v1 and must be remeasured on macOS before merge.
PACKAGE_SHA256 = {
    "x86_64": {
        "minimal.vrm": "1e0b2db7995e385e558b62f8fd46d58d6eb631b393cf4e9858194118d441d90f",
        "textures.vrm": "8aa3d1267d4697d2e7643f4e5f7c128247847776fe8890c9806bbad9ae215428",
    },
    "arm64": {
        "minimal.vrm": "47d1b3a414789ac7b57c1721b6a53b416f5d395d19002d8fecdd73f15df1ecd2",
        "textures.vrm": "89d41350bf1de5049bceca34713d1ec94b461fee7f74d549ef8f40d2b48f2ae9",
    },
}
ARCHITECTURE = {"amd64": "x86_64", "x86_64": "x86_64",
                "arm64": "arm64", "aarch64": "arm64"}.get(platform.machine().lower(), "")

# Seconds: a ZIP entry's time is MS-DOS time, in two-second steps.
ZIP_TIME_RESOLUTION = 2.1

# The importer's fixture builder, for the one input written at test time.
FIXTURE_LIB = (pathlib.Path(__file__).resolve().parents[3]
               / "plugins" / "usdVrmFileFormat" / "tools")

# Read in whichever process runs it: this one for a source `.vrm`, a
# plugin-free child for an export. Printed as JSON.
SUMMARY = r'''
import hashlib, json, re, sys
from pxr import Ar, Sdf, Usd


def fnv1a64(data):
    h = 1469598103934665603
    for b in data:
        h ^= b
        h = (h * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return "%016x" % h


def plain(value):
    # A list op's repr is its address; its items are what it says.
    if hasattr(value, "prependedItems"):
        return ("listop", value.isExplicit, list(value.explicitItems),
                list(value.prependedItems), list(value.appendedItems),
                list(value.deletedItems))
    return value


# A zero's sign, dropped: `-0.0` and `-0` as whole numbers in a repr.
NEGATIVE_ZERO = re.compile(r"(?<![\w.])-(0(?:\.0*)?)(?![\w.])")


def digest(value, zero_sign=True):
    text = repr(plain(value))
    if not zero_sign:
        text = NEGATIVE_ZERO.sub(r"\1", text)
    return hashlib.sha1(text.encode("utf-8")).hexdigest()


def asset_bytes(value):
    resolved = value.resolvedPath or Ar.GetResolver().Resolve(value.path)
    if not resolved:
        return None
    asset = Ar.GetResolver().OpenAsset(Ar.ResolvedPath(str(resolved)))
    if not asset:
        return None
    return bytes(asset.Read(asset.GetSize(), 0))


stage = Usd.Stage.Open(sys.argv[1])
root = stage.GetRootLayer()
summary = {
    "defaultPrim": root.defaultPrim,
    "upAxis": str(stage.GetMetadata("upAxis")),
    "metersPerUnit": stage.GetMetadata("metersPerUnit"),
    "prims": {},
    "assets": {},
}
for prim in Usd.PrimRange(stage.GetPseudoRoot(), Usd.PrimAllPrimsPredicate):
    if prim.IsPseudoRoot():
        continue
    attrs = {}
    blind = {}
    for attr in prim.GetAuthoredAttributes():
        type_name = attr.GetTypeName()
        if type_name in (Sdf.ValueTypeNames.Asset, Sdf.ValueTypeNames.AssetArray):
            value = attr.Get()
            values = list(value or []) if type_name == Sdf.ValueTypeNames.AssetArray else [value]
            entries = []
            for value in values:
                data = asset_bytes(value) if value and value.path else None
                entries.append({
                    "authored": value.path if value else "",
                    "fnv": fnv1a64(data) if data is not None else None,
                })
            summary["assets"][attr.GetPath().pathString] = entries
            attrs[attr.GetName()] = str(type_name)
            continue
        samples = attr.GetTimeSamples()
        values = [attr.Get(t) for t in samples]
        attrs[attr.GetName()] = [str(type_name), digest(attr.Get()), len(samples),
                                 digest(values)]
        blind[attr.GetName()] = [digest(attr.Get(), False), digest(values, False)]
    summary["prims"][prim.GetPath().pathString] = {
        "type": str(prim.GetTypeName()),
        "metadata": digest(sorted((k, repr(plain(v))) for k, v in prim.GetAllAuthoredMetadata().items())),
        "attrs": attrs,
        "attrsZeroSignBlind": blind,
        "rels": {rel.GetName(): [str(t) for t in rel.GetTargets()]
                 for rel in prim.GetAuthoredRelationships()},
    }
print(json.dumps(summary, sort_keys=True))
'''


class Failures:
    def __init__(self) -> None:
        self.messages: list[str] = []

    def check(self, condition: bool, message: str) -> bool:
        if not condition:
            self.messages.append(message)
            print(f"FAIL: {message}")
        return condition


def plugin_env(plugins: dict[str, str], names: tuple[str, ...]) -> dict[str, str]:
    """This process's environment with exactly `names` registered."""
    env = dict(os.environ)
    env.pop("PXR_PLUGINPATH_NAME", None)
    if names:
        env["PXR_PLUGINPATH_NAME"] = os.pathsep.join(plugins[n] for n in names)
    return env


def run(tool: str, args: list[str], env: dict[str, str]) -> subprocess.CompletedProcess:
    return subprocess.run([tool, *args], env=env, capture_output=True, text=True,
                          encoding="utf-8", errors="replace")


def summarize(path: pathlib.Path, env: dict[str, str] | None) -> dict:
    """The stage's summary, read in a child process with `env`."""
    result = subprocess.run([sys.executable, "-c", SUMMARY, str(path)], env=env,
                            capture_output=True, text=True, encoding="utf-8",
                            errors="replace")
    if result.returncode != 0:
        raise RuntimeError(f"cannot summarize {path}:\n{result.stderr}")
    return json.loads(result.stdout)


def compare(source: dict, exported: dict, label: str, failures: Failures,
            zero_sign: bool) -> None:
    """`zero_sign` False compares values with -0 equal to 0.

    OpenUSD's crate writer keeps a scalar float's -0.0 and turns -0 into 0 in
    an integer-valued vector or array (measured 2026-09-26, policy §9), so a
    `.usdc` and a `.usdz` can only be held to numeric equality. A `.usda` is
    held to the sign too.
    """
    for key in ("defaultPrim", "upAxis", "metersPerUnit"):
        failures.check(source[key] == exported[key],
                       f"{label}: {key} {exported[key]!r} != source {source[key]!r}")
    missing = sorted(set(source["prims"]) - set(exported["prims"]))
    extra = sorted(set(exported["prims"]) - set(source["prims"]))
    failures.check(not missing, f"{label}: {len(missing)} prim(s) lost, e.g. {missing[:3]}")
    failures.check(not extra, f"{label}: {len(extra)} prim(s) added, e.g. {extra[:3]}")
    values = "attrs" if zero_sign else "attrsZeroSignBlind"
    keys = ("type", "metadata", values, "rels")

    def view(summary: dict, path: str) -> dict:
        return {key: summary["prims"][path][key] for key in keys}

    differing = [path for path in source["prims"]
                 if path in exported["prims"]
                 and view(source, path) != view(exported, path)]
    if not failures.check(not differing,
                          f"{label}: {len(differing)} prim(s) differ, e.g. {differing[:3]}"):
        path = differing[0]
        for key in keys:
            a, b = source["prims"][path][key], exported["prims"][path][key]
            if a != b:
                if isinstance(a, dict):
                    diff = sorted(k for k in set(a) | set(b) if a.get(k) != b.get(k))
                    print(f"  {path} {key} differ in {diff[:5]}")
                else:
                    print(f"  {path} {key}: {a!r} != {b!r}")

    failures.check(set(source["assets"]) == set(exported["assets"]),
                   f"{label}: the asset-valued attributes differ")
    for attr, entries in source["assets"].items():
        out = exported["assets"].get(attr, [])
        if not failures.check(len(entries) == len(out), f"{label}: {attr} changed length"):
            continue
        for src, dst in zip(entries, out):
            if not src["authored"]:
                continue
            failures.check(src["fnv"] is not None,
                           f"{label}: the source's {attr} does not resolve ({src['authored']})")
            failures.check(dst["fnv"] is not None,
                           f"{label}: {attr} does not resolve without the VRM plugins "
                           f"({dst['authored']})")
            failures.check(dst["authored"].startswith(f"./textures/{src['fnv']}"),
                           f"{label}: {attr} is {dst['authored']!r}, not "
                           f"./textures/{src['fnv']}.*")
            failures.check(src["fnv"] == dst["fnv"],
                           f"{label}: {attr} carries different bytes")


def check_no_local_path(output: pathlib.Path, source_path: pathlib.Path, label: str,
                        failures: Failures) -> None:
    """A `.usda` names no path of this machine (policy §7)."""
    text = output.read_bytes().decode("utf-8", errors="replace")
    source_dir = str(source_path.resolve().parent)
    # A `.usda` string escapes a backslash, so a Windows path is written with
    # doubled ones.
    for form in {source_dir, source_dir.replace("\\", "/"), source_dir.replace("\\", "\\\\")}:
        failures.check(form not in text,
                       f"{label}: the output names the source directory {form}")
    failures.check(".vrm[" not in text, f"{label}: the output still points into a .vrm")
    failures.check(tempfile.gettempdir() not in text and "vrm_export_" not in text,
                   f"{label}: the output names a temporary directory")


def test_exports(tool: str, inputs: list[pathlib.Path], plugins: dict[str, str],
                 work: pathlib.Path, failures: Failures) -> None:
    full = plugin_env(plugins, ("fileformat", "resolver", "schema"))
    bare = plugin_env(plugins, ())
    packages: list[tuple[pathlib.Path, pathlib.Path]] = []
    for source_path in inputs:
        # Read with the plugins, in a child like every export is, so both
        # sides of each comparison come through the same code.
        source = summarize(source_path, full)
        for fmt in FORMATS:
            label = f"{source_path.name} -> .{fmt}"
            out_dir = work / f"{source_path.stem}-{fmt}"
            output = out_dir / f"{source_path.stem}.{fmt}"
            result = run(tool, [str(source_path), "-o", str(output), "--check"], full)
            if not failures.check(result.returncode == 0,
                                  f"{label}: exit {result.returncode}: {result.stderr.strip()}"):
                continue
            compare(source, summarize(output, bare), label, failures,
                    zero_sign=(fmt == "usda"))

            if fmt == "usdz":
                with zipfile.ZipFile(output) as package:
                    infos = package.infolist()
                names = [i.filename for i in infos]
                failures.check(names and names[0] == "defaultLayer.usdc",
                               f"{label}: the first entry is {names[:1]}, not the root layer")
                failures.check(all(n.startswith("textures/") for n in names[1:]),
                               f"{label}: entries outside textures/: {names[1:4]}")
                failures.check(not any(n.lower().endswith(".vrm") for n in names),
                               f"{label}: the package holds the source .vrm")
                failures.check(all(i.date_time == (2000, 1, 1, 0, 0, 0) for i in infos),
                               f"{label}: entries stamped "
                               f"{sorted({i.date_time for i in infos})[:2]}, not 2000-01-01")
                expected = PACKAGE_SHA256.get(ARCHITECTURE, {}).get(source_path.name)
                if expected:
                    digest = hashlib.sha256(output.read_bytes()).hexdigest()
                    # Which entry differs, when one does: name, size, CRC-32.
                    entries = [(i.filename, i.file_size, f"{i.CRC:08x}") for i in infos]
                    failures.check(digest == expected,
                                   f"{label}: SHA-256 {digest}, expected {expected}; "
                                   f"entries {entries}")
                packages.append((source_path, output))
            else:
                textures = out_dir / "textures"
                count = len(list(textures.iterdir())) if textures.is_dir() else 0
                assets = {e["fnv"] for entries in source["assets"].values()
                          for e in entries if e["fnv"]}
                failures.check(count == len(assets),
                               f"{label}: textures/ holds {count} file(s) for "
                               f"{len(assets)} distinct image(s)")

            if fmt == "usda":
                check_no_local_path(output, source_path, label, failures)

                # Determinism (policy §9): a second export, elsewhere, is the
                # same bytes.
                again = work / f"{source_path.stem}-usda-again" / output.name
                result = run(tool, [str(source_path), "-o", str(again)], full)
                failures.check(result.returncode == 0 and again.read_bytes() ==
                               output.read_bytes(),
                               f"{label}: a second export is not byte-identical")

        # --flatten (policy §6.1): the composed stage in one layer. A `.vrm`
        # authors no composition arc, so it is the same stage; and like any
        # output it names no path of this machine, which the source-file
        # comment UsdStage::Flatten writes by default would.
        label = f"{source_path.name} -> --flatten .usda"
        flat = work / f"{source_path.stem}-flatten" / f"{source_path.stem}.usda"
        result = run(tool, [str(source_path), "-o", str(flat), "--flatten", "--check"], full)
        if failures.check(result.returncode == 0,
                          f"{label}: exit {result.returncode}: {result.stderr.strip()}"):
            compare(source, summarize(flat, bare), label, failures, zero_sign=True)
            check_no_local_path(flat, source_path, label, failures)

    # Determinism of a package (policy §9, q2): each `.usdz` exported again,
    # elsewhere, is the same bytes. An entry's time has a two-second
    # resolution, so the second pass starts two seconds after the first ended
    # -- otherwise an unpinned time can match by landing in the same tick. It
    # runs in another time zone too, which the MSVC runtime OpenUSD's
    # localtime() runs in on Windows does not read from TZ (measured
    # 2026-10-26); Linux and macOS do.
    time.sleep(ZIP_TIME_RESOLUTION)
    for source_path, output in packages:
        label = f"{source_path.name} -> .usdz"
        again = work / f"{source_path.stem}-usdz-again" / output.name
        result = run(tool, [str(source_path), "-o", str(again)],
                     dict(full, TZ=OTHER_TIME_ZONE))
        failures.check(result.returncode == 0 and again.read_bytes() == output.read_bytes(),
                       f"{label}: a second export, in TZ={OTHER_TIME_ZONE}, is not "
                       f"byte-identical")


def test_command_line(tool: str, vrm: pathlib.Path, textured: pathlib.Path,
                      plugins: dict[str, str], work: pathlib.Path,
                      failures: Failures) -> None:
    full = plugin_env(plugins, ("fileformat", "resolver", "schema"))
    cases_dir = work / "cli"
    cases_dir.mkdir()

    def expect(args: list[str], code: int, label: str, env=full, needle: str = "") -> None:
        result = run(tool, args, env)
        failures.check(result.returncode == code,
                       f"{label}: exit {result.returncode}, expected {code}: "
                       f"{result.stderr.strip()[:300]}")
        if needle:
            failures.check(needle in result.stdout + result.stderr,
                           f"{label}: the output does not mention {needle!r}")

    expect(["--help"], 0, "--help", needle="usage: vrm_export")
    expect(["--version"], 0, "--version", needle="vrm_export ")
    expect([], 2, "no arguments", needle="no input")
    expect([str(vrm), str(cases_dir / "a.usdz")], 2, "a positional output", needle="-o")
    expect([str(vrm), "-o", str(cases_dir / "a.obj")], 2, "an unknown extension")
    expect([str(vrm), "-o", str(cases_dir / "a.usda"), "--motion"], 2, "an unknown option")
    expect([str(cases_dir / "missing.vrm"), "-o", str(cases_dir / "a.usda")], 3,
           "a missing input")

    not_vrm = cases_dir / "input.usda"
    not_vrm.write_text("#usda 1.0\n", encoding="utf-8")
    expect([str(not_vrm), "-o", str(cases_dir / "b.usda")], 2, "a non-.vrm input")

    existing = cases_dir / "existing.usdc"
    expect([str(vrm), "-o", str(existing)], 0, "a first export")
    expect([str(vrm), "-o", str(existing)], 2, "an existing output", needle="--overwrite")
    expect([str(vrm), "-o", str(existing), "--overwrite"], 0, "--overwrite")

    nested = cases_dir / "a" / "b" / "nested.usda"
    expect([str(vrm), "-o", str(nested), "--check"], 0, "a directory that does not exist")
    failures.check(nested.is_file(), "the nested output was not written")

    expect([str(textured), "-o", str(cases_dir / "verbose.usdz"), "--check", "--verbose"], 0,
           "--verbose", needle="vrm_export: localized ")

    # The two refusals that name a missing plugin (policy §5.3).
    expect([str(vrm), "-o", str(cases_dir / "c.usda")], 3, "no VRM plugin",
           env=plugin_env(plugins, ()), needle="usdVrmFileFormat")
    expect([str(textured), "-o", str(cases_dir / "d.usdz")], 4, "no package resolver",
           env=plugin_env(plugins, ("fileformat", "schema")), needle="usdVrmPackageResolver")
    failures.check(not (cases_dir / "d.usdz").exists(),
                   "a failed export left its output behind")

    # The temporary directory a package is assembled in is removed (§6.3).
    scratch_tmp = cases_dir / "tmp"
    scratch_tmp.mkdir()
    env = dict(full, TMP=str(scratch_tmp), TEMP=str(scratch_tmp), TMPDIR=str(scratch_tmp))
    expect([str(textured), "-o", str(cases_dir / "e.usdz")], 0, "a package", env=env)
    left = list(scratch_tmp.iterdir())
    failures.check(not left, f"the temporary directory was left behind: {left[:3]}")


def write_external_image_vrm(directory: pathlib.Path) -> pathlib.Path:
    """A `.vrm` whose images are one embedded and two external files.

    An external image is authored as a plain file path beside the `.vrm`
    rather than a path into it, which is the other half of localization
    (policy §6.2). `maps/red.png` holds the embedded image's bytes, so the
    export writes two files for three images. Written here rather than
    committed among the importer's fixtures, which `baseline_freeze.py`
    freezes the import of.
    """
    sys.path.insert(0, str(FIXTURE_LIB))
    from vrm_fixture_lib import (ARRAY_BUFFER, ELEMENT_ARRAY_BUFFER, FLOAT, TRI_INDICES,
                                 TRI_POSITIONS, U16, GlbBuilder, solid_png, vrm1_extension)

    red, blue = solid_png(200, 40, 40), solid_png(40, 40, 200)
    maps = directory / "maps"
    maps.mkdir(parents=True)
    (maps / "red.png").write_bytes(red)
    (maps / "blue.png").write_bytes(blue)

    b = GlbBuilder()
    pos = b.add(FLOAT, "VEC3", TRI_POSITIONS, ARRAY_BUFFER, minmax=True)
    uv = b.add(FLOAT, "VEC2", [(0.0, 0.0), (1.0, 0.0), (0.5, 1.0)], ARRAY_BUFFER)
    idx = b.add(U16, "SCALAR", TRI_INDICES, ELEMENT_ARRAY_BUFFER)
    gltf = {
        "asset": {"version": "2.0", "generator": "vrm_export tests"},
        "scene": 0, "scenes": [{"nodes": [0]}],
        "nodes": [{"name": "Body", "mesh": 0}],
        "meshes": [{"name": "Body", "primitives": [{
            "attributes": {"POSITION": pos, "TEXCOORD_0": uv},
            "indices": idx, "material": 0}]}],
        "images": [
            {"name": "embedded", "bufferView": b.add_bytes(red), "mimeType": "image/png"},
            {"name": "external", "uri": "maps/blue.png"},
            {"name": "external_same_bytes", "uri": "maps/red.png"},
        ],
        "textures": [{"source": 0}, {"source": 1}, {"source": 2}],
        "materials": [{
            "name": "Skin",
            "pbrMetallicRoughness": {"baseColorTexture": {"index": 0}},
            "emissiveTexture": {"index": 1},
            "emissiveFactor": [1.0, 1.0, 1.0],
            "normalTexture": {"index": 2},
        }],
        "extensionsUsed": ["VRMC_vrm"],
        "extensions": {"VRMC_vrm": vrm1_extension({})},
    }
    path = directory / "external_images.vrm"
    path.write_bytes(b.build(gltf))
    return path


def test_external_images(tool: str, plugins: dict[str, str], work: pathlib.Path,
                         failures: Failures) -> pathlib.Path:
    """The external-image `.vrm`: every format, then the image taken away.

    Returns the input, for test_exports to hold to everything it holds the
    committed fixtures to.
    """
    full = plugin_env(plugins, ("fileformat", "resolver", "schema"))
    vrm = write_external_image_vrm(work / "external")

    # Both external files are read, and the duplicate of the embedded image
    # is written once.
    output = work / "external-usda" / "external_images.usda"
    result = run(tool, [str(vrm), "-o", str(output), "--verbose"], full)
    if failures.check(result.returncode == 0,
                      f"external images: exit {result.returncode}: {result.stderr.strip()}"):
        localized = [line for line in result.stderr.splitlines()
                     if line.startswith("vrm_export: localized ")]
        failures.check(len(localized) == 3,
                       f"external images: {len(localized)} localized line(s), expected 3")
        written = sorted(p.name for p in (output.parent / "textures").iterdir())
        failures.check(len(written) == 2,
                       f"external images: textures/ holds {written}, expected two files")

    # An external image that is gone fails the export, and names the path
    # (policy §6.2), rather than writing an output that lost a texture.
    missing = work / "external-missing"
    missing.mkdir()
    missing_vrm = missing / vrm.name
    missing_vrm.write_bytes(vrm.read_bytes())
    (missing / "maps").mkdir()
    (missing / "maps" / "red.png").write_bytes((vrm.parent / "maps" / "red.png").read_bytes())
    gone = work / "external-missing-out" / "gone.usdz"
    result = run(tool, [str(missing_vrm), "-o", str(gone)], full)
    failures.check(result.returncode == 4,
                   f"a missing external image: exit {result.returncode}, expected 4: "
                   f"{result.stderr.strip()[:300]}")
    failures.check("blue.png" in result.stderr,
                   f"a missing external image: the message does not name it: "
                   f"{result.stderr.strip()[:300]}")
    failures.check(not gone.exists(), "a missing external image left an output behind")
    return vrm


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--tool", required=True)
    parser.add_argument("--plugin", action="append", default=[],
                        help="name=directory, for fileformat, resolver and schema")
    parser.add_argument("--textured", required=True,
                        help="an input with embedded textures, for the command-line cases")
    parser.add_argument("inputs", nargs="+")
    args = parser.parse_args()

    plugins = dict(item.split("=", 1) for item in args.plugin)
    for name in ("fileformat", "resolver", "schema"):
        if name not in plugins:
            parser.error(f"--plugin {name}=<dir> is required")
    inputs = [pathlib.Path(p) for p in args.inputs]

    failures = Failures()
    with tempfile.TemporaryDirectory(prefix="vrm_export_test_") as scratch:
        work = pathlib.Path(scratch)
        inputs.append(test_external_images(args.tool, plugins, work, failures))
        test_exports(args.tool, inputs, plugins, work, failures)
        test_command_line(args.tool, inputs[0], pathlib.Path(args.textured), plugins, work,
                          failures)

    if failures.messages:
        print(f"{len(failures.messages)} failure(s)")
        return 1
    print(f"OK: {len(inputs)} input(s) x {len(FORMATS)} formats, and the command line")
    return 0


if __name__ == "__main__":
    sys.exit(main())
