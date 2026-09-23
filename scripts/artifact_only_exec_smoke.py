#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Evaluate the exec bundles from the *installed product* and nothing else.

The OpenExec plan owes one run it had never made: computation discovery from a
packaged plugin rather than a build tree (P0-4 step 7), which is also P0-7's
"works from packaged plugins" row. Every exec suite until now loaded a *built*
bundle through its source directory. This is the packaged run, and it is the
parity harness rather than a new one: the five `workspace_exec_parity_*` cases,
with the product's own tool producing the bake and the product's own bundles
evaluating it. The recorded input is the converted mocopi export committed in
`tests/motion/fixtures/` -- an input, not the artifact -- since the product's
BVH converter left with MIG-3.

    ost plugin package --workspace --product   -> the product dist
    ost plugin product verify  <dist>          -> archive + every member checksum
    ost plugin product install <dist> --prefix -> a fresh prefix outside the repo
    tests/parity/test_exec_parity.py --case ...
        --retarget <prefix>/tools/motion_retarget/bin/motion_retarget
        --parity   <build tree>/tests/parity/exec_parity
    <build tree>/plugins/execMotion/tests/execMotion_display_tests

Two executables come from the build tree, and they are the harness rather than
the artifact: OpenExec has no Python binding, and the product ships no program
that evaluates a computation. Both link only static workspace libraries and
OpenUSD, so what they load at run time is the runtime's and the product's --
which is not an assumption here but the check:

* the environment is the runtime `ost env` prints plus the activation the
  product declares, with every inherited plugin path dropped and every loader
  path inside this repository removed;
* `exec_parity` reports the plugins the registry loaded and every module the
  process mapped. ExecMotion and ExecVrm have to be among them, from the
  prefix; any module whose file name is one of the product's libraries has to
  be the prefix's copy; and nothing else may come from inside the repository
  except the runtime and the harness itself;
* then every copy of execMotion's `plugInfo.json` in the prefix is moved aside
  and a case is run again. It must refuse, with ExecMotion loaded from nowhere
  -- the proof that no location outside the product was answering.

And the two rows of P0-3 the parity cases do not reach, in the same
environment:

* **the tool's own process.** The product's `motion_retarget` bakes the real,
  textured `Seed-san.vrm` with a `.vrma` walk and writes `--load-report`: the
  importer and the VRMA reader have to load from the prefix, and nothing of the
  tool's may come from the repository;
* **embedded textures, from a Python host.** `artifact_texture_probe.py` opens
  that bake, resolves every `avatar.vrm[images/...]` texture to bytes through
  the product's resolver, and reports what its own process loaded. It adds no
  DLL directory, so on Windows it is also the measurement of whether a Python
  host needs `os.add_dll_directory` beside the activation's `PATH`
  (INSTALL.md).

Exit codes follow `clean_install_smoke.py`: 0 pass, 1 the smoke ran and an
assertion failed, 2 the harness itself is misconfigured.

Usage:
    python scripts/artifact_only_exec_smoke.py            # package, then run
    python scripts/artifact_only_exec_smoke.py --product dist/products/...
    python scripts/artifact_only_exec_smoke.py --case vrma_walk --keep
"""
from __future__ import annotations

import argparse
import json
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from artifact_smoke import (  # noqa: E402
    REPO_ROOT, Failures, apply_product_activation, fail_setup, ost_json,
    package_product, run, workspace_target)

# The parity driver's cases, all five, in the order the root suite lists them.
CASES = ("recorded_fixture", "recorded_root_motion", "generated_thirty_fps",
         "recorded_real_avatar", "vrma_walk")

# The case the negative check re-runs: the cheapest one that reaches both
# bundles, since the retarget reads its pose across into execMotion.
NEGATIVE_CASE = "recorded_fixture"

# The two plugins this smoke is about, by the name their plugInfo.json gives.
EXEC_PLUGINS = ("ExecMotion", "ExecVrm")
# What the product's motion_retarget has to load to bake a .vrm with a .vrma,
# and what a Python host has to load to resolve that bake's embedded textures.
TOOL_PLUGINS = ("UsdVrmFileFormat", "UsdVrmaFileFormat")
PROBE_PLUGINS = ("UsdVrmFileFormat", "UsdVrmPackageResolver")

# Inputs, from this repository -- the same files the root suite hands the
# driver. Inputs are not the artifact; the tools and bundles that read them are.
REAL_AVATAR = ("plugins/usdVrmFileFormat/tests/corpus/spec-samples/vrm1/"
               "seed-san/Seed-san.vrm")
VRMA_WALK = "plugins/usdVrmaFileFormat/tests/fixtures/canonical_walk.vrma"
DESIGN_MAP = "tools/motionRetarget/tests/fixtures/design_avatar_humanoid_map.json"
DISPLAY_FIXTURE = "plugins/execMotion/tests/fixtures/displayed_clip.usda"


def suffix() -> str:
    return ".exe" if os.name == "nt" else ""


def norm(path: str | pathlib.Path) -> str:
    """A path in the one spelling two paths are compared in."""
    return os.path.normcase(os.path.realpath(str(path)))


def inside(path: str | pathlib.Path, root: str | pathlib.Path) -> bool:
    path, root = norm(path), norm(root)
    return path == root or path.startswith(root.rstrip(os.sep) + os.sep)


def find_build_dir(given: str | None) -> pathlib.Path:
    """The root build tree that holds the two harnesses.

    `ost build` names it after the target, so it is found by what it contains
    rather than by a name this script would have to keep in step with `ost`.
    """
    if given is not None:
        build = pathlib.Path(given).resolve()
    else:
        matches = sorted((REPO_ROOT / "build").glob(
            f"*/tests/parity/exec_parity{suffix()}"))
        if len(matches) != 1:
            fail_setup(f"expected exactly one root build tree holding "
                       f"exec_parity under {REPO_ROOT / 'build'}, found "
                       f"{len(matches)}; pass --build-dir")
        build = matches[0].parents[2]
    for harness in harness_paths(build):
        if not harness.is_file():
            fail_setup(f"the build tree has no {harness}; run `ost build`")
    return build


def harness_paths(build: pathlib.Path) -> tuple[pathlib.Path, pathlib.Path]:
    return (build / "tests" / "parity" / f"exec_parity{suffix()}",
            build / "plugins" / "execMotion" / "tests"
            / f"execMotion_display_tests{suffix()}")


def product_environment(ost: str, platform: str, profile: str,
                        prefix: pathlib.Path) -> tuple[dict, list[str]]:
    """The environment the product is run in, and the runtime's roots.

    Built from the caller's, because the host's own loader paths (the C
    runtime on Windows) are needed -- but nothing that could reach a build
    tree survives: inherited plugin paths are dropped outright, and every
    loader-path entry inside this repository is removed before the runtime's
    and the product's are prepended.
    """
    env = dict(os.environ)
    env.pop("USDVRM_MOTION_PROFILE_PATH", None)
    env.pop("PXR_PLUGINPATH_NAME", None)
    for name in ("PATH", "LD_LIBRARY_PATH", "DYLD_LIBRARY_PATH", "PYTHONPATH"):
        if name in env:
            env[name] = os.pathsep.join(
                entry for entry in env[name].split(os.pathsep)
                if entry and not inside(entry, REPO_ROOT))

    runtime_roots = []
    result = ost_json([ost, "env", platform, "--profile", profile, "--json"])
    for entry in result["data"]["env"]:
        name, value = entry["name"], entry["value"]
        if name == "CMAKE_PREFIX_PATH":
            runtime_roots.append(value)
        existing = env.get(name)
        env[name] = f"{value}{os.pathsep}{existing}" if existing else value
    if not runtime_roots:
        fail_setup("`ost env` reported no CMAKE_PREFIX_PATH, so the runtime's "
                   "root is unknown and a module from it cannot be told from "
                   "one out of a build tree")
    apply_product_activation(env, prefix)

    # The claim the rest of the run rests on, checked rather than assumed.
    for name in ("PXR_PLUGINPATH_NAME", "PATH", "LD_LIBRARY_PATH",
                 "DYLD_LIBRARY_PATH"):
        for entry in env.get(name, "").split(os.pathsep):
            if entry and inside(entry, REPO_ROOT) and not any(
                    inside(entry, root) for root in runtime_roots):
                fail_setup(f"{name} still reaches into the repository: "
                           f"{entry}")
    return env, runtime_roots


def product_library_names(prefix: pathlib.Path) -> set[str]:
    """The file names of every shared library the product carries."""
    names = set()
    for path in prefix.rglob("*"):
        name = path.name
        if path.is_file() and (name.lower().endswith(".dll")
                               or name.endswith(".dylib")
                               or ".so" in name):
            names.add(os.path.normcase(name))
    return names


def check_provenance(failures: Failures, case: str, report: dict,
                     prefix: pathlib.Path, harness: pathlib.Path,
                     runtime_roots: list[str], ours: set[str],
                     required: tuple[str, ...] = EXEC_PLUGINS) -> None:
    plugins = report.get("loaded_plugins")
    modules = report.get("loaded_modules")
    if not failures.check(
            isinstance(plugins, dict) and isinstance(modules, list)
            and modules,
            f"{case}: the harness report carries no loaded_plugins or "
            f"loaded_modules, so where it loaded from cannot be read"):
        return
    for name in required:
        path = plugins.get(name)
        if failures.check(path is not None,
                          f"{case}: {name} was not loaded"):
            failures.check(inside(path, prefix),
                           f"{case}: {name} was loaded from {path}, not "
                           f"from the installed product")
    for name, path in sorted(plugins.items()):
        failures.check(
            not inside(path, REPO_ROOT)
            or any(inside(path, root) for root in runtime_roots),
            f"{case}: plugin {name} was loaded from the repository: {path}")
    for module in modules:
        if os.path.normcase(os.path.basename(module)) in ours:
            failures.check(inside(module, prefix),
                           f"{case}: {os.path.basename(module)} was loaded "
                           f"from {module}, not from the installed product")
        elif inside(module, REPO_ROOT):
            failures.check(
                norm(module) == norm(harness)
                or any(inside(module, root) for root in runtime_roots),
                f"{case}: {module} was loaded from the repository")


def run_case(case: str, prefix: pathlib.Path, env: dict,
             parity: pathlib.Path, work: pathlib.Path
             ) -> tuple[subprocess.CompletedProcess, list[dict]]:
    tools = prefix / "tools"
    command = [
        sys.executable, str(REPO_ROOT / "tests" / "parity"
                            / "test_exec_parity.py"),
        "--case", case,
        "--parity", str(parity),
        "--retarget", str(tools / "motion_retarget" / "bin"
                          / f"motion_retarget{suffix()}"),
        "--fixtures", str(REPO_ROOT / "tests" / "motion" / "fixtures"),
        "--design-fixtures", str(REPO_ROOT / "docs" / "design" / "fixtures"
                                 / "motion"),
        "--design-map", str(REPO_ROOT / DESIGN_MAP),
        "--parity-fixtures", str(REPO_ROOT / "tests" / "parity" / "fixtures"),
        "--real-avatar", str(REPO_ROOT / REAL_AVATAR),
        "--vrma-walk", str(REPO_ROOT / VRMA_WALK),
        "--work", str(work),
    ]
    print(f"$ test_exec_parity.py --case {case}", flush=True)
    result = subprocess.run(command, env=env, text=True, encoding="utf-8",
                            errors="replace", capture_output=True)
    # Only the harness's own reports: `<name>.parity.json`, one per compare.
    reports = [json.loads(path.read_text(encoding="utf-8"))
               for path in sorted(work.glob("*.parity.json"))]
    return result, reports


def check_textured_bake(failures: Failures, prefix: pathlib.Path, env: dict,
                        runtime_roots: list[str], ours: set[str],
                        work: pathlib.Path) -> None:
    """The product's tool bakes a textured avatar, and a Python host resolves
    the bake's textures; both processes report what they loaded."""
    work.mkdir(parents=True, exist_ok=True)
    tool = (prefix / "tools" / "motion_retarget" / "bin"
            / f"motion_retarget{suffix()}")
    bake = work / "textured_walk.usda"
    tool_report = work / "motion_retarget.load.json"
    command = [str(tool), "--avatar", str(REPO_ROOT / REAL_AVATAR),
               "--animation", str(REPO_ROOT / VRMA_WALK),
               "--output", str(bake), "--load-report", str(tool_report),
               "--quiet"]
    print(f"$ motion_retarget --avatar {pathlib.Path(REAL_AVATAR).name} "
          f"--animation {pathlib.Path(VRMA_WALK).name} --load-report", flush=True)
    result = subprocess.run(command, env=env, text=True, encoding="utf-8",
                            errors="replace", capture_output=True)
    if not failures.check(result.returncode == 0 and bake.is_file(),
                          f"textured: the product's motion_retarget failed "
                          f"(exit {result.returncode}):\n"
                          f"{result.stdout}{result.stderr}"):
        return
    if failures.check(tool_report.is_file(),
                      "textured: motion_retarget wrote no --load-report"):
        check_provenance(failures, "textured tool", json.loads(
            tool_report.read_text(encoding="utf-8")), prefix, tool,
            runtime_roots, ours, TOOL_PLUGINS)

    probe_report = work / "texture_probe.load.json"
    print("$ artifact_texture_probe.py textured_walk.usda", flush=True)
    probed = subprocess.run(
        [sys.executable, str(REPO_ROOT / "scripts" / "artifact_texture_probe.py"),
         str(bake), str(probe_report)],
        env=env, text=True, encoding="utf-8", errors="replace",
        capture_output=True)
    if not failures.check(probed.returncode == 0 and probe_report.is_file(),
                          f"textured: the embedded textures did not resolve "
                          f"from the product in a Python host (exit "
                          f"{probed.returncode}):\n"
                          f"{probed.stdout}{probed.stderr}"):
        return
    report = json.loads(probe_report.read_text(encoding="utf-8"))
    check_provenance(failures, "textured python host", report, prefix,
                     pathlib.Path(sys.executable), runtime_roots, ours,
                     PROBE_PLUGINS)
    print(f"  textured: {len(report['textures'])} embedded texture(s) "
          f"resolved from the product; the tool and a Python host loaded "
          f"nothing from the repository")

    # The negative, as for execMotion: with the product's resolver
    # registration gone, nothing else may resolve the textures.
    infos = sorted(path for path in prefix.rglob("plugInfo.json")
                   if '"UsdVrmPackageResolver"' in path.read_text(encoding="utf-8"))
    if not failures.check(bool(infos), "the product holds no "
                          "usdVrmPackageResolver plugInfo.json, so the "
                          "texture negative cannot run"):
        return
    hidden = []
    try:
        for info in infos:
            moved = info.with_name("plugInfo.json.hidden")
            info.rename(moved)
            hidden.append((moved, info))
        unresolved = subprocess.run(
            [sys.executable,
             str(REPO_ROOT / "scripts" / "artifact_texture_probe.py"),
             str(bake), str(work / "texture_probe.negative.json")],
            env=env, text=True, encoding="utf-8", errors="replace",
            capture_output=True)
        if failures.check(
                unresolved.returncode != 0,
                f"with every usdVrmPackageResolver plugInfo.json in the "
                f"product moved aside ({len(infos)}), the textures still "
                f"resolved -- something outside the product answered:\n"
                f"{unresolved.stdout}{unresolved.stderr}"):
            print(f"  negative: textures unresolved with {len(infos)} "
                  f"resolver registration(s) moved aside")
    finally:
        for moved, info in hidden:
            moved.rename(info)


# A source file's own name, which a `__FILE__` in a registration macro embeds.
SOURCE_SUFFIXES = (".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp")


def loader_search_paths(path: pathlib.Path) -> list[str]:
    """The RPATH / RUNPATH (ELF) or LC_RPATH (Mach-O) entries a binary carries,
    read with the platform's own tool. Empty on Windows, which has none, and
    when the tool is missing."""
    if sys.platform.startswith("linux"):
        command, pattern = ["readelf", "-d", str(path)], r"\((?:RPATH|RUNPATH)\).*\[(.*)\]"
    elif sys.platform == "darwin":
        command, pattern = ["otool", "-l", str(path)], r"^\s*path (.*) \(offset \d+\)"
    else:
        return []
    try:
        result = subprocess.run(command, capture_output=True, text=True,
                                errors="replace")
    except OSError as error:
        print(f"  loader search paths: cannot run {command[0]}: {error}")
        return []
    output = result.stdout
    if result.returncode != 0:
        print(f"  loader search paths: {command[0]} exited "
              f"{result.returncode} on {path.name}: {result.stderr.strip()}")
    import re
    return [match.group(1) for line in output.splitlines()
            if (match := re.search(pattern, line))]


def check_source_path_leaks(failures: Failures, prefix: pathlib.Path) -> None:
    """No binary in the product names a build-tree path.

    Every executable and shared library is searched for this repository's
    root, in either slash. A match that is a source file is a `__FILE__` a
    macro expanded -- OpenUSD's `TF_REGISTRY_FUNCTION` and schema registration
    put one in each plugin library -- which is a name, not a dependency, and is
    counted and printed rather than refused (the v0.9.0 record's known
    limitation). A match inside the binary's own RPATH / RUNPATH / LC_RPATH is
    the build machine's loader search path, which packaging does not rewrite
    (measured in the v0.9.0 dry run on Linux and macOS): it is counted and
    printed, a known limitation with its fix upstream in packaging, and the
    provenance checks are what prove nothing loads from it. Anything else -- a
    build directory, a `.strata` stage, a PDB -- fails.
    """
    root = str(REPO_ROOT)
    needles = {root, root.replace("\\", "/")}
    if os.name == "nt":
        needles = {needle.lower() for needle in needles}
    tolerated: dict[str, int] = {}
    searched: dict[str, int] = {}
    refused = 0
    scanned = 0
    for path in sorted(prefix.rglob("*")):
        name = path.name.lower()
        binary = (name.endswith((".exe", ".dll", ".dylib"))
                  or ".so" in name
                  or (path.parent.name == "bin" and "." not in name))
        if not path.is_file() or not binary:
            continue
        scanned += 1
        text = path.read_bytes().decode("latin-1")
        haystack = text.lower() if os.name == "nt" else text
        rpaths = loader_search_paths(path)
        for needle in needles:
            start = haystack.find(needle)
            while start != -1:
                end = start
                while end < len(text) and text[end] not in "\x00\n\"":
                    end += 1
                found = text[start:end]
                relative = found[len(root):].lstrip("\\/").replace("\\", "/")
                first = relative.split("/", 1)[0]
                if (found.lower().endswith(SOURCE_SUFFIXES)
                        and first not in ("build", ".strata", "dist")):
                    key = str(path.relative_to(prefix))
                    tolerated[key] = tolerated.get(key, 0) + 1
                elif any(found in rpath or rpath in found for rpath in rpaths):
                    key = str(path.relative_to(prefix))
                    searched[key] = searched.get(key, 0) + 1
                else:
                    refused += 1
                    failures.check(False, f"{path.relative_to(prefix)} names a "
                                          f"build-tree path: {found} "
                                          f"(its loader search paths: {rpaths})")
                start = haystack.find(needle, end)
    failures.check(scanned > 0, "the source-path scan found no binary to read")
    print(f"  source paths: {scanned} binaries scanned, {refused} build-tree "
          f"path(s); "
          f"{sum(tolerated.values())} source-file name(s) from registration "
          f"macros in {len(tolerated)} librar(ies)")
    for key, count in sorted(tolerated.items()):
        print(f"    {count} in {key}")
    if searched:
        print(f"  loader search paths: {sum(searched.values())} build-machine "
              f"RPATH entr(ies) naming the checkout, in {len(searched)} "
              f"binar(ies) (known limitation; nothing loads from them)")
        for key, count in sorted(searched.items()):
            print(f"    {count} in {key}")


def exec_motion_plug_infos(prefix: pathlib.Path) -> list[pathlib.Path]:
    """Every copy of execMotion's plugInfo.json in the product. There is more
    than one -- a dependent bundle carries its dependency bundles inside it
    (ost report 40) -- and the registry keeps whichever it reads first."""
    return sorted(path for path in prefix.rglob("plugInfo.json")
                  if path.parent.name == "execMotion"
                  and '"ExecMotion"' in path.read_text(encoding="utf-8"))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--product", default=None,
                        help="an existing product dist directory, manifest.json "
                             "or .tar.zst; default packages the workspace")
    parser.add_argument("--build-dir", default=None,
                        help="the root build tree holding the harnesses; "
                             "default finds the one under build/")
    parser.add_argument("--case", action="append", choices=CASES,
                        help="run only these parity cases (default: all five)")
    parser.add_argument("--keep", action="store_true",
                        help="keep the installed prefix and every report")
    parser.add_argument("--ost", default="ost", help="ost executable")
    args = parser.parse_args()

    platform, profile = workspace_target()
    build = find_build_dir(args.build_dir)
    parity, display = harness_paths(build)
    for required in (REAL_AVATAR, VRMA_WALK, DESIGN_MAP, DISPLAY_FIXTURE):
        if not (REPO_ROOT / required).is_file():
            fail_setup(f"no input at {REPO_ROOT / required}")

    if args.product is None:
        product = package_product(args.ost, platform, profile)
    else:
        product = pathlib.Path(args.product).resolve()
        if not product.exists():
            fail_setup(f"no product at {product}")
    run([args.ost, "plugin", "product", "verify", str(product)])

    scratch = pathlib.Path(tempfile.mkdtemp(prefix="vrm-exec-only-"))
    prefix = scratch / "prefix"
    if inside(prefix, REPO_ROOT):
        fail_setup(f"the install prefix is inside the repository: {prefix}")

    failures = Failures()
    try:
        run([args.ost, "plugin", "product", "install", "--prefix",
             str(prefix), str(product)])
        env, runtime_roots = product_environment(args.ost, platform, profile,
                                                 prefix)
        ours = product_library_names(prefix)
        check_source_path_leaks(failures, prefix)

        for case in args.case or CASES:
            result, reports = run_case(case, prefix, env, parity,
                                       scratch / "work" / case)
            if not failures.check(
                    result.returncode == 0,
                    f"{case}: the parity case failed from the product "
                    f"(exit {result.returncode}):\n"
                    f"{result.stdout}{result.stderr}"):
                continue
            if not failures.check(bool(reports),
                                  f"{case}: passed and left no harness "
                                  f"report to read provenance from"):
                continue
            for report in reports:
                check_provenance(failures, case, report, prefix, parity,
                                 runtime_roots, ours)
            print(f"  {case}: parity holds from the product "
                  f"({len(reports)} comparison(s))")

        # P0-3's rows: the tool's own process, and textures from a Python host.
        check_textured_bake(failures, prefix, env, runtime_roots, ours,
                            scratch / "work" / "textured")

        # P0-7's packaged row: the display suite, under the same environment.
        print(f"$ {display.name} {DISPLAY_FIXTURE}", flush=True)
        shown = subprocess.run([str(display), str(REPO_ROOT / DISPLAY_FIXTURE)],
                               env=env, text=True, encoding="utf-8",
                               errors="replace", capture_output=True)
        if failures.check(shown.returncode == 0,
                          f"the display suite failed from the product "
                          f"(exit {shown.returncode}):\n"
                          f"{shown.stdout}{shown.stderr}"):
            print("  display: passed from the product")

        # And the negative: with execMotion's registration gone from the
        # product, nothing may answer in its place.
        hidden = []
        infos = exec_motion_plug_infos(prefix)
        if failures.check(bool(infos),
                          "the product holds no execMotion plugInfo.json, so "
                          "the negative check cannot run"):
            try:
                for info in infos:
                    moved = info.with_name("plugInfo.json.hidden")
                    info.rename(moved)
                    hidden.append((moved, info))
                result, reports = run_case(NEGATIVE_CASE, prefix, env, parity,
                                           scratch / "work" / "negative")
                loaded = [report.get("loaded_plugins", {}).get("ExecMotion")
                          for report in reports]
                if failures.check(
                        result.returncode != 0 and bool(reports)
                        and not any(loaded)
                        and all(report.get("exec_refusals", 0) > 0
                                for report in reports),
                        f"with every execMotion plugInfo.json in the product "
                        f"moved aside ({len(infos)}), {NEGATIVE_CASE} exited "
                        f"{result.returncode} and loaded ExecMotion from "
                        f"{[path for path in loaded if path] or 'nowhere'}; "
                        f"it must refuse, with ExecMotion loaded from "
                        f"nowhere:\n{result.stdout}{result.stderr}"):
                    print(f"  negative: {NEGATIVE_CASE} refused with "
                          f"{len(infos)} execMotion registration(s) moved "
                          f"aside")
            finally:
                for moved, info in hidden:
                    moved.rename(info)
    finally:
        if args.keep:
            print(f"kept the installed prefix and reports at: {scratch}")
        else:
            shutil.rmtree(scratch, ignore_errors=True)

    rc = failures.report()
    print("artifact-only exec smoke: " + ("PASS" if rc == 0 else "FAIL"))
    return rc


if __name__ == "__main__":
    sys.exit(main())
