#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Drive the BVH path from the *installed product* and nothing else.

WORKSPACE.md §5 ships the motion profiles as product data —
`share/usd-vrm-plugins/profiles/motion/` — for one stated reason: *a converter
with no profile available refuses every file it is given, which would make an
artifact-only smoke test of the BVH path impossible to pass.* That sentence has
been a requirement without a test since v0.7.0. `ost` 0.22.3 supplied the
missing half of the mechanism (`[[workspace.install_data]]`, product manifest
`data_files: 3`), and report 36 §4 said in as many words that **the staging is
what was proven and not the run**. This is the run.

    ost plugin package --workspace --product   -> the product dist
    ost plugin product verify  <dist>          -> archive + every member checksum
    ost plugin product install <dist> --prefix -> a fresh prefix outside the repo
    <prefix>/tools/motion_bvh/bin/motion_bvh_convert <bvh> --profile <id>

What makes it an *artifact-only* run rather than another way of running the
tool:

* the prefix is created outside the repository and the script refuses one
  inside it, so nothing in this source tree can answer a lookup;
* `--profile-dir` is never passed and `USDVRM_MOTION_PROFILE_PATH` is removed
  from the environment, so the profile is found by the tool's own installed
  layout or it is not found at all;
* the only environment the run is given is the runtime `ost env` prints and the
  activation the product itself declares in `openstrata.activation.json` —
  which is what the install contract tells a consumer to use;
* the profile that answered is then *proved* to be the installed one: the file
  is moved aside, the same command is re-run, and it must refuse.

The last of those is the check worth keeping. A converter that found a profile
somewhere else on the host would pass every other assertion here, and "which
profile was used" is the one reproducible fact a conversion carries.

Exit codes follow `clean_install_smoke.py`: 0 pass, 1 the smoke ran and an
assertion failed, 2 the harness itself is misconfigured.

Usage:
    python scripts/artifact_only_bvh_smoke.py            # package, then run
    python scripts/artifact_only_bvh_smoke.py --product dist/products/...
    python scripts/artifact_only_bvh_smoke.py --keep
"""
from __future__ import annotations

import argparse
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile
from typing import NoReturn

REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]

# The tool member and the executable inside it, as `openstrata.product.json`
# names them. Spelled out rather than discovered: a script that scanned the
# prefix for something convert-shaped would pass on the day the product stopped
# carrying this one.
TOOL_MEMBER = "motion_bvh"
TOOL_EXECUTABLE = "motion_bvh_convert"

# Where the product's data must land. This is the contract WORKSPACE.md §5
# states, `openstrata.toml`'s `[[workspace.install_data]]` maps to, and
# `ProfileLocator.cpp` derives from the executable's own path — three places
# that have to agree, which is why the destination is written here as a literal
# and compared rather than read out of the manifest.
PROFILE_DESTINATION = ("share", "usd-vrm-plugins", "profiles", "motion")

# The committed export and the profile written from it, named for the reason
# `test_motion_bvh_convert.py` names them: a smoke that scanned a directory
# would pass on the day the file it is about stopped being there.
DEFAULT_BVH = ("libs/motionBvh/tests/corpus/recorded/redistributable/"
               "mocopi-mobile-arm-raise-turn.bvh")
DEFAULT_PROFILE_ID = "mocopi-mobile-bvh-default-v1"


class Failures:
    def __init__(self) -> None:
        self.messages: list[str] = []

    def check(self, condition: bool, message: str) -> bool:
        if not condition:
            self.messages.append(message)
        return condition

    def report(self) -> int:
        if not self.messages:
            return 0
        for message in self.messages:
            print(f"FAIL: {message}", file=sys.stderr)
        return 1


def run(cmd: list[str], **kw) -> subprocess.CompletedProcess:
    print(f"$ {' '.join(cmd)}", flush=True)
    return subprocess.run(cmd, check=True, text=True, **kw)


def ost_json(cmd: list[str]) -> dict:
    proc = run(cmd, capture_output=True)
    return json.loads(proc.stdout)


def fail_setup(msg: str) -> NoReturn:
    """Exit 2 — the harness is misconfigured, which is not the same answer as
    the product failing its own smoke."""
    print(f"SETUP: {msg}", file=sys.stderr)
    raise SystemExit(2)


def workspace_target() -> tuple[str, str]:
    """The platform and profile `openstrata.toml` requires.

    Read with a regex rather than a TOML parser so the script runs under any
    Python this repository's lanes have, and read from the manifest rather than
    passed in so the smoke cannot be pointed at a runtime the product was not
    built against.
    """
    text = (REPO_ROOT / "openstrata.toml").read_text(encoding="utf-8")
    requires = re.search(r"^\[requires\]\s*$(.*?)(?=^\[|\Z)", text,
                         re.MULTILINE | re.DOTALL)
    if requires is None:
        fail_setup("openstrata.toml has no [requires] table")
    body = requires.group(1)
    values = {}
    for key in ("platform", "profile"):
        found = re.search(rf'^\s*{key}\s*=\s*"([^"]+)"', body, re.MULTILINE)
        if found is None:
            fail_setup(f"openstrata.toml [requires] has no {key}")
        values[key] = found.group(1)
    return values["platform"], values["profile"]


def package_product(ost: str, platform: str, profile: str) -> pathlib.Path:
    """Package the workspace's aggregate product and return its dist directory.

    The tree must already be built — this is the packaging half only, exactly as
    `release.yml` runs it after `ost build` and the per-bundle builds.
    """
    result = ost_json([ost, "plugin", "package", "--workspace", "--product",
                       "--target", platform, "--profile", profile, "--json"])
    product = result["data"].get("product")
    if not product:
        fail_setup("`ost plugin package --workspace --product` reported no "
                   "product")
    archive = pathlib.Path(product["archive"])
    print(f"packaged product {product['name']} {product.get('version', '')} "
          f"-> {product['archive_digest']}")
    return archive.parent


def apply_runtime_env(env: dict, ost: str, platform: str,
                      profile: str) -> None:
    """The runtime the product was built against, as `ost env` reports it.

    Every entry is a prepend: `ost env` emits one row per path element, and a
    row that replaced the caller's value would take `PATH` away from the
    subprocess on Windows, where the C runtime lives on it.
    """
    result = ost_json([ost, "env", platform, "--profile", profile, "--json"])
    for entry in result["data"]["env"]:
        name, value = entry["name"], entry["value"]
        existing = env.get(name)
        env[name] = f"{value}{os.pathsep}{existing}" if existing else value


def apply_product_activation(env: dict, prefix: pathlib.Path) -> None:
    """The product's own activation, from the file the install contract names.

    Read rather than reconstructed: the file says which variable each list of
    paths belongs to, and on a POSIX host the loader variable is not `PATH`.
    """
    activation_path = prefix / "openstrata.activation.json"
    if not activation_path.is_file():
        fail_setup(f"the install wrote no activation file at {activation_path}")
    activation = json.loads(activation_path.read_text(encoding="utf-8"))
    variables = activation.get("environment", {})
    for key, paths_key in (("loader", "library_paths"),
                           ("plugin", "plugin_paths"),
                           ("python", "python_paths")):
        name = variables.get(key)
        if not name:
            continue
        values = [str(prefix / relative)
                  for relative in activation.get(paths_key, [])]
        if not values:
            continue
        joined = os.pathsep.join(values)
        existing = env.get(name)
        env[name] = f"{joined}{os.pathsep}{existing}" if existing else joined


def check_profiles_installed(failures: Failures,
                             prefix: pathlib.Path) -> pathlib.Path:
    """Every shipped profile reaches the prefix, byte for byte.

    Byte for byte rather than merely present, because the failure this replaces
    was a *copy* that stopped being the file `scripts/check_motion_profiles.py`
    validates (report 35 §4). A check that only counted files would have passed
    against it.
    """
    installed = prefix.joinpath(*PROFILE_DESTINATION)
    source = REPO_ROOT / "profiles" / "motion"
    if not failures.check(installed.is_dir(),
                          f"the product installed no {'/'.join(PROFILE_DESTINATION)}"):
        return installed
    for authored in sorted(source.glob("*.yaml")):
        shipped = installed / authored.name
        if not failures.check(shipped.is_file(),
                              f"{authored.name} did not reach the prefix"):
            continue
        failures.check(shipped.read_bytes() == authored.read_bytes(),
                       f"{authored.name} in the prefix is not the file "
                       f"profiles/motion/ holds")
    return installed


def run_converter(tool: pathlib.Path, env: dict, bvh: pathlib.Path,
                  profile_id: str,
                  output: pathlib.Path) -> subprocess.CompletedProcess:
    """The one command this whole script exists to run.

    No `--profile-dir`. The caller has already removed
    `USDVRM_MOTION_PROFILE_PATH` from `env`, so the two directories left in the
    search order are both derived from this executable's own location — and the
    executable is inside the prefix, which is outside the repository.
    """
    cmd = [str(tool), str(bvh), "--profile", profile_id,
           "--output", str(output)]
    print(f"$ {' '.join(cmd)}", flush=True)
    return subprocess.run(cmd, env=env, text=True, capture_output=True)


def check_the_run(failures: Failures, tool: pathlib.Path, env: dict,
                  bvh: pathlib.Path, profile_id: str,
                  installed_profiles: pathlib.Path,
                  work: pathlib.Path) -> None:
    output = work / "artifact-only-clip.usda"
    result = run_converter(tool, env, bvh, profile_id, output)
    if not failures.check(
            result.returncode == 0,
            f"the installed converter exited {result.returncode} with no "
            f"--profile-dir. This is the artifact-only condition failing:\n"
            f"{result.stdout}{result.stderr}"):
        return

    if not failures.check(output.is_file(),
                          f"the converter reported success and wrote no "
                          f"{output.name}"):
        return
    clip = output.read_text(encoding="utf-8")
    failures.check(profile_id in clip,
                   f"the clip does not record profileId {profile_id!r}")
    failures.check("SkelAnimation" in clip,
                   "the clip carries no SkelAnimation")
    print(result.stdout, end="")

    # And the negative: the file that answered was the installed one.
    #
    # Without this the smoke would pass on a host that happens to have a
    # profile of the same id somewhere the search order reaches, which is the
    # one way "it found its profile" can be true and mean nothing.
    shipped = installed_profiles / f"{profile_id}.yaml"
    hidden = installed_profiles / f"{profile_id}.yaml.hidden"
    if not failures.check(shipped.is_file(),
                          f"{shipped} is missing, so the negative check cannot "
                          f"run"):
        return
    shipped.rename(hidden)
    try:
        result = run_converter(tool, env, bvh, profile_id,
                               work / "must-not-exist.usda")
        failures.check(
            result.returncode == 2,
            f"with the installed profile moved aside the converter exited "
            f"{result.returncode}, so the profile it read the first time was "
            f"not the one this product ships")
    finally:
        hidden.rename(shipped)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--product", default=None,
                        help="an existing product dist directory, manifest.json "
                             "or .tar.zst; default packages the workspace")
    parser.add_argument("--bvh", default=DEFAULT_BVH,
                        help="the recorded export to convert")
    parser.add_argument("--profile-id", default=DEFAULT_PROFILE_ID,
                        help="the profile id the product must supply")
    parser.add_argument("--keep", action="store_true",
                        help="keep the installed prefix for inspection")
    parser.add_argument("--ost", default="ost", help="ost executable")
    args = parser.parse_args()

    platform, profile = workspace_target()
    bvh = (REPO_ROOT / args.bvh).resolve()
    if not bvh.is_file():
        fail_setup(f"no BVH file at {bvh}")

    ost = args.ost
    if args.product is None:
        product = package_product(ost, platform, profile)
    else:
        product = pathlib.Path(args.product).resolve()
        if not product.exists():
            fail_setup(f"no product at {product}")

    # Verify before install, in that order and both of them, because the
    # install contract in `openstrata.product.json` names both and a smoke that
    # skipped the first would be testing a different contract than the one a
    # consumer is told to follow.
    run([ost, "plugin", "product", "verify", str(product)])

    scratch = pathlib.Path(tempfile.mkdtemp(prefix="vrm-artifact-only-"))
    prefix = scratch / "prefix"
    if REPO_ROOT in prefix.resolve().parents or prefix.resolve() == REPO_ROOT:
        fail_setup(f"the install prefix is inside the repository: {prefix}")

    failures = Failures()
    try:
        run([ost, "plugin", "product", "install", "--prefix", str(prefix),
             str(product)])

        installed_profiles = check_profiles_installed(failures, prefix)

        suffix = ".exe" if os.name == "nt" else ""
        tool = prefix / "tools" / TOOL_MEMBER / "bin" / (TOOL_EXECUTABLE + suffix)
        if not failures.check(
                tool.is_file(),
                f"the product installed no {TOOL_EXECUTABLE} at {tool}"):
            return failures.report()

        env = dict(os.environ)
        # The whole point of the run. Its presence would make the smoke pass
        # against a product that ships no profile at all.
        env.pop("USDVRM_MOTION_PROFILE_PATH", None)
        apply_runtime_env(env, ost, platform, profile)
        apply_product_activation(env, prefix)

        check_the_run(failures, tool, env, bvh, args.profile_id,
                      installed_profiles, scratch)
    finally:
        if args.keep:
            print(f"kept the installed prefix at: {prefix}")
        else:
            shutil.rmtree(scratch, ignore_errors=True)

    rc = failures.report()
    print("artifact-only BVH smoke: " + ("PASS" if rc == 0 else "FAIL"))
    return rc


if __name__ == "__main__":
    sys.exit(main())
