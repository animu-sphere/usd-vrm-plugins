#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Run the vrmImaging suites against the *installed product* and nothing else.

Imaging track Step I5: `ost`'s L2 constructs the packaged adapters, which says
they load; it does not say what they contribute. Every `vrmImaging` suite until
now read the build tree's `plugInfo.json`. This is the same suites, with the
product's in its place:

    ost plugin package --workspace --product   -> the product dist
    ost plugin product verify  <dist>          -> archive + every member checksum
    ost plugin product install <dist> --prefix -> a fresh prefix outside the repo
    <build tree>/plugins/vrmImaging/tests/vrmImaging_*_tests ...

The executables come from the build tree, and they are the harness rather than
the artifact: each links OpenUSD and no plugin, so what it reads through Hydra
is what the registry loaded. The environment is the one
`artifact_only_exec_smoke.py` runs in -- the runtime `ost env` prints plus the
product's activation, with every inherited plugin path dropped and every loader
path inside this repository removed -- and:

* every hand-authored suite (`discovery`, `mtoon`, `material`,
  `texture_info`) runs over its committed fixture, and `mtoon` again in a
  session with the product's `vrmImaging` and no `vrmSchema`;
* `import` runs over the committed MToon pair through its driver, with the
  product's importer and package resolver (policy §17.3);
* `expression_bake` runs over the product's own `motion_retarget` bake of
  `expressions_mtoon.vrm` (Step I4), so the tool and the imaging bundle are
  both the product's;
* `discovery` reports where `VrmImaging` loaded from, and it has to be the
  prefix;
* then every copy of `vrmImaging`'s `plugInfo.json` in the prefix is moved
  aside and `discovery` is run again. It must fail -- the proof that no
  location outside the product was answering.

Exit codes follow `clean_install_smoke.py`: 0 pass, 1 the smoke ran and an
assertion failed, 2 the harness itself is misconfigured.

Usage:
    python scripts/artifact_only_imaging_smoke.py            # package, then run
    python scripts/artifact_only_imaging_smoke.py --product dist/products/...
"""
from __future__ import annotations

import argparse
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from artifact_smoke import (  # noqa: E402
    REPO_ROOT, Failures, fail_setup, inside, package_product,
    product_environment, run, suffix, workspace_target)

SUITES_DIR = pathlib.Path("plugins") / "vrmImaging" / "tests"
FIXTURES = REPO_ROOT / SUITES_DIR / "fixtures"
IMPORTER_TESTS = REPO_ROOT / "plugins" / "usdVrmFileFormat" / "tests"

# The hand-authored suites and the fixture each reads, as the bundle's own
# tests/CMakeLists.txt registers them.
HAND_AUTHORED = (
    ("discovery", None),
    ("mtoon", "mtoon_materials.usda"),
    ("material", "material_core.usda"),
    ("texture_info", "texture_info.usda"),
)

# The Step I4 bake, as the root CMakeLists.txt makes it for the MToon fixture.
BAKE_AVATAR = IMPORTER_TESTS / "fixtures" / "expressions_mtoon.vrm"
BAKE_CLIP = (REPO_ROOT / "tools" / "motionRetarget" / "tests" / "fixtures"
             / "expressive_clip.usda")


def find_suites(given: str | None) -> pathlib.Path:
    """The build directory holding the vrmImaging suites.

    Found by what it contains, as `artifact_only_exec_smoke.py` finds its
    harness, rather than by a directory name kept in step with `ost`.
    """
    name = f"vrmImaging_discovery_tests{suffix()}"
    if given is not None:
        suites = pathlib.Path(given).resolve() / SUITES_DIR
    else:
        matches = sorted((REPO_ROOT / "build").glob(f"*/{SUITES_DIR.as_posix()}/{name}"))
        if len(matches) != 1:
            fail_setup(f"expected exactly one root build tree holding {name} "
                       f"under {REPO_ROOT / 'build'}, found {len(matches)}; "
                       f"pass --build-dir")
        suites = matches[0].parent
    for suite in [s for s, _ in HAND_AUTHORED] + ["import", "expression_bake"]:
        if not (suites / f"vrmImaging_{suite}_tests{suffix()}").is_file():
            fail_setup(f"the build tree has no vrmImaging_{suite}_tests in "
                       f"{suites}; run `ost build`")
    return suites


def suite(suites: pathlib.Path, name: str) -> str:
    return str(suites / f"vrmImaging_{name}_tests{suffix()}")


def run_suite(failures: Failures, what: str, command: list[str],
              env: dict) -> subprocess.CompletedProcess:
    print(f"$ {what}", flush=True)
    result = subprocess.run(command, env=env, text=True, encoding="utf-8",
                            errors="replace", capture_output=True)
    if failures.check(result.returncode == 0,
                      f"{what}: failed against the product (exit "
                      f"{result.returncode}):\n{result.stdout}{result.stderr}"):
        passed = [line for line in result.stdout.splitlines()
                  if line.endswith(": passed")]
        print(f"  {passed[-1] if passed else 'passed'}")
    return result


def imaging_plug_infos(prefix: pathlib.Path) -> list[pathlib.Path]:
    """Every copy of vrmImaging's plugInfo.json in the product."""
    return sorted(path for path in prefix.rglob("plugInfo.json")
                  if '"VrmImaging"' in path.read_text(encoding="utf-8"))


def without_schema(env: dict, prefix: pathlib.Path,
                   info: pathlib.Path) -> dict:
    """`env` with the product's plugin paths replaced by vrmImaging's alone."""
    changed = dict(env)
    kept = [entry for entry in env.get("PXR_PLUGINPATH_NAME", "").split(os.pathsep)
            if entry and not inside(entry, prefix)]
    changed["PXR_PLUGINPATH_NAME"] = os.pathsep.join(kept + [str(info.parent)])
    return changed


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--product", default=None,
                        help="an existing product dist directory, manifest.json "
                             "or .tar.zst; default packages the workspace")
    parser.add_argument("--build-dir", default=None,
                        help="the root build tree holding the suites; default "
                             "finds the one under build/")
    parser.add_argument("--keep", action="store_true",
                        help="keep the installed prefix and the bake")
    parser.add_argument("--ost", default="ost", help="ost executable")
    args = parser.parse_args()

    platform, profile = workspace_target()
    suites = find_suites(args.build_dir)
    for required in (BAKE_AVATAR, BAKE_CLIP,
                     IMPORTER_TESTS / "material_oracle.py"):
        if not required.is_file():
            fail_setup(f"no input at {required}")

    if args.product is None:
        product = package_product(args.ost, platform, profile)
    else:
        product = pathlib.Path(args.product).resolve()
        if not product.exists():
            fail_setup(f"no product at {product}")
    run([args.ost, "plugin", "product", "verify", str(product)])

    scratch = pathlib.Path(tempfile.mkdtemp(prefix="vrm-imaging-only-"))
    prefix = scratch / "prefix"
    if inside(prefix, REPO_ROOT):
        fail_setup(f"the install prefix is inside the repository: {prefix}")

    failures = Failures()
    try:
        run([args.ost, "plugin", "product", "install", "--prefix",
             str(prefix), str(product)])
        env, _ = product_environment(args.ost, platform, profile, prefix)
        infos = imaging_plug_infos(prefix)
        if not infos:
            fail_setup("the product holds no vrmImaging plugInfo.json")

        # The hand-authored suites, and where the plugin loaded from.
        for name, fixture in HAND_AUTHORED:
            command = [suite(suites, name)]
            if fixture:
                command.append(str(FIXTURES / fixture))
            result = run_suite(failures, f"vrmImaging_{name}", command, env)
            if name == "discovery" and result.returncode == 0:
                loaded = [line[len("loaded: "):] for line in
                          result.stdout.splitlines() if line.startswith("loaded: ")]
                if failures.check(len(loaded) == 1,
                                  "vrmImaging_discovery reported no load path"):
                    failures.check(inside(loaded[0], prefix),
                                   f"VrmImaging was loaded from {loaded[0]}, "
                                   f"not from the installed product")
                    print(f"  VrmImaging loaded from the product: {loaded[0]}")
        run_suite(failures, "vrmImaging_mtoon --without-schema",
                  [suite(suites, "mtoon"), str(FIXTURES / "mtoon_materials.usda"),
                   "--without-schema"],
                  without_schema(env, prefix, infos[0]))

        # §17.3: the committed pair through the product's importer.
        run_suite(failures, "test_import_imaging.py (mtoon_vrm1, mtoon_vrm0)", [
            sys.executable,
            str(REPO_ROOT / "tests" / "imaging" / "test_import_imaging.py"),
            "--suite", suite(suites, "import"),
            "--oracle-dir", str(IMPORTER_TESTS),
            "--vrm1", str(IMPORTER_TESTS / "fixtures" / "mtoon_vrm1.vrm"),
            "--vrm0", str(IMPORTER_TESTS / "fixtures" / "mtoon_vrm0.vrm"),
            "--work", str(scratch / "import")], env)

        # Step I4: the product's tool bakes, the product's bundle reads.
        bake = scratch / "expression_bake_mtoon.usda"
        tool = (prefix / "tools" / "motion_retarget" / "bin"
                / f"motion_retarget{suffix()}")
        baked = run_suite(failures, "motion_retarget expressions_mtoon.vrm", [
            str(tool), "--avatar", str(BAKE_AVATAR), "--animation",
            str(BAKE_CLIP), "--output", str(bake), "--quiet"], env)
        if baked.returncode == 0:
            run_suite(failures, "vrmImaging_expression_bake",
                      [suite(suites, "expression_bake"), str(bake)], env)

        # And the negative: with the product's registration gone, nothing may
        # answer in its place.
        hidden = []
        try:
            for info in infos:
                moved = info.with_name("plugInfo.json.hidden")
                info.rename(moved)
                hidden.append((moved, info))
            print("$ vrmImaging_discovery (registration moved aside)", flush=True)
            result = subprocess.run([suite(suites, "discovery")], env=env,
                                    text=True, encoding="utf-8",
                                    errors="replace", capture_output=True)
            if failures.check(
                    result.returncode != 0,
                    f"with every vrmImaging plugInfo.json in the product "
                    f"moved aside ({len(infos)}), vrmImaging_discovery still "
                    f"passed -- something outside the product answered:\n"
                    f"{result.stdout}{result.stderr}"):
                print(f"  negative: discovery failed with {len(infos)} "
                      f"vrmImaging registration(s) moved aside")
        finally:
            for moved, info in hidden:
                moved.rename(info)
    finally:
        if args.keep:
            print(f"kept the installed prefix and the bake at: {scratch}")
        else:
            shutil.rmtree(scratch, ignore_errors=True)

    rc = failures.report()
    print("artifact-only imaging smoke: " + ("PASS" if rc == 0 else "FAIL"))
    return rc


if __name__ == "__main__":
    sys.exit(main())
