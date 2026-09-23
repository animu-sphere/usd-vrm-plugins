#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""What every artifact-only smoke shares: package, install, activate, report.

`artifact_only_exec_smoke.py` and `check_product_libraries.py` drive the
installed product and nothing else, and they need the same few steps to get
there -- the workspace's target read from `openstrata.toml`, the product
packaged, the runtime `ost env` reports and the activation the product declares
applied, and a failure collector whose exit code says whether the smoke or the
harness failed. This module is those steps and nothing else.

It was `artifact_only_bvh_smoke.py` until MIG-3. That smoke drove the product's
`motion_bvh_convert` over its installed profiles, and both left with the BVH
tools for `usd-motion-plugins`, so the run it made has no subject in this
product; the helpers it grew for its siblings stayed, under a name that says
what they are.

Exit codes follow `clean_install_smoke.py`: 0 pass, 1 the smoke ran and an
assertion failed, 2 the harness itself is misconfigured.
"""
from __future__ import annotations

import json
import os
import pathlib
import re
import subprocess
import sys
from typing import NoReturn

REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]


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
