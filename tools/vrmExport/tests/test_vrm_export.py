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
`.usdz` holds its root layer first, textures after, and no `.vrm`; the
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
import subprocess
import sys
import tempfile
import zipfile

FORMATS = ("usda", "usdc", "usdz")

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


def test_exports(tool: str, inputs: list[pathlib.Path], plugins: dict[str, str],
                 work: pathlib.Path, failures: Failures) -> None:
    full = plugin_env(plugins, ("fileformat", "resolver", "schema"))
    bare = plugin_env(plugins, ())
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
                    names = package.namelist()
                failures.check(names and names[0] == "defaultLayer.usdc",
                               f"{label}: the first entry is {names[:1]}, not the root layer")
                failures.check(all(n.startswith("textures/") for n in names[1:]),
                               f"{label}: entries outside textures/: {names[1:4]}")
                failures.check(not any(n.lower().endswith(".vrm") for n in names),
                               f"{label}: the package holds the source .vrm")
            else:
                textures = out_dir / "textures"
                count = len(list(textures.iterdir())) if textures.is_dir() else 0
                assets = {e["fnv"] for entries in source["assets"].values()
                          for e in entries if e["fnv"]}
                failures.check(count == len(assets),
                               f"{label}: textures/ holds {count} file(s) for "
                               f"{len(assets)} distinct image(s)")

            if fmt == "usda":
                text = output.read_bytes().decode("utf-8", errors="replace")
                source_dir = str(source_path.resolve().parent)
                for form in {source_dir, source_dir.replace("\\", "/")}:
                    failures.check(form not in text,
                                   f"{label}: the output names the source directory {form}")
                failures.check(".vrm[" not in text,
                               f"{label}: the output still points into a .vrm")
                failures.check(tempfile.gettempdir() not in text
                               and "vrm_export_" not in text,
                               f"{label}: the output names a temporary directory")

                # Determinism (policy §9): a second export, elsewhere, is the
                # same bytes.
                again = work / f"{source_path.stem}-usda-again" / output.name
                result = run(tool, [str(source_path), "-o", str(again)], full)
                failures.check(result.returncode == 0 and again.read_bytes() ==
                               output.read_bytes(),
                               f"{label}: a second export is not byte-identical")


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
    expect([str(vrm), "-o", str(cases_dir / "a.usda"), "--flatten"], 2, "an unknown option")
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
