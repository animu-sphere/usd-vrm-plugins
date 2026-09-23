#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Does every packaged bundle carry the shared libraries its own record names?

Each bundle in the aggregate product ships a `dependencies.json` that names the
plain libraries it links and the package directories their runtime was staged
into. This check holds the bytes to that record: for every workspace library
built SHARED, the library's file has to be in one of the directories the record
names, and that directory has to be on the bundle's own loader path and the
product's.

It exists because the record and the bytes disagreed and nothing noticed
(ost report 40). `ost plugin build <bundle>` rebuilds one shared
`workspace-prefix` from that bundle's closure alone, and packaging stages every
bundle's library runtime out of that one prefix, counting a library as staged
when the *directory* exists. So a release lane whose last bundle build was
`execVrm` packaged three `.vrm` bundles that record `vrmContainer` at
`runtime/libraries/lib` and carry no `vrmContainer` binary anywhere, the
packaging step said nothing, and the product could not open a `.vrm` file.

    ost plugin package --workspace --product    -> the product dist
    ost plugin product verify / install          -> a fresh prefix
    this script                                  -> record vs bytes, per bundle

The other direction is reported and not failed: a bundle carrying a shared
library it does not declare is the same defect seen from the bundle that built
last, and while it stands the release lane's build order is what puts the
right binary in every package (release.yml, "Build every bundle").

Exit codes follow `clean_install_smoke.py`: 0 pass, 1 a bundle's bytes do not
match its record, 2 the harness itself is misconfigured -- including a product
in which no shared library was checked at all, since a check over nothing
passes having said nothing.

Usage:
    python scripts/check_product_libraries.py                 # package, then check
    python scripts/check_product_libraries.py --product dist/products/...
    python scripts/check_product_libraries.py --prefix <installed product>
"""
from __future__ import annotations

import argparse
import json
import pathlib
import re
import shutil
import sys
import tempfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from artifact_smoke import (  # noqa: E402
    REPO_ROOT, Failures, fail_setup, package_product, run, workspace_target)


def workspace_members() -> list[pathlib.Path]:
    """The member directories `openstrata.toml` declares, in its order."""
    text = (REPO_ROOT / "openstrata.toml").read_text(encoding="utf-8")
    block = re.search(r"^members\s*=\s*\[(.*?)\]", text,
                      re.MULTILINE | re.DOTALL)
    if block is None:
        fail_setup("openstrata.toml declares no [workspace] members")
    return [REPO_ROOT / member
            for member in re.findall(r'"([^"]+)"', block.group(1))]


def shared_libraries() -> dict[str, str]:
    """Library id -> CMake target, for every member library built SHARED.

    Read off the member's own `add_library` rather than listed here, so a
    library that changes kind changes what is checked without an edit to this
    file. A static library contributes nothing at run time and has no file a
    consumer could miss.
    """
    shared = {}
    for member in workspace_members():
        descriptor = member / "openstrata.library.yaml"
        cmake = member / "CMakeLists.txt"
        if not descriptor.is_file() or not cmake.is_file():
            continue
        found = re.search(r"^library:\s*$.*?^\s+id:\s*(\S+)",
                          descriptor.read_text(encoding="utf-8"),
                          re.MULTILINE | re.DOTALL)
        if found is None:
            fail_setup(f"{descriptor} names no library id")
        target = re.search(r"add_library\(\s*(\S+)\s+SHARED\b",
                           cmake.read_text(encoding="utf-8"))
        if target is not None:
            shared[found.group(1)] = target.group(1)
    return shared


def binary_pattern(target: str, target_os: str) -> re.Pattern[str]:
    """The file name a shared library's runtime half has on one platform."""
    name = re.escape(target)
    if target_os == "windows":
        return re.compile(rf"^(lib)?{name}\.dll$", re.IGNORECASE)
    if target_os == "macos":
        return re.compile(rf"^lib{name}(\.[^/]*)?\.dylib$")
    return re.compile(rf"^lib{name}\.so(\.[0-9.]+)?$")


def looks_shared(path: pathlib.Path, target_os: str) -> bool:
    name = path.name
    if target_os == "windows":
        return name.lower().endswith(".dll")
    if target_os == "macos":
        return name.endswith(".dylib")
    return re.search(r"\.so(\.[0-9.]+)?$", name) is not None


def check_prefix(failures: Failures, prefix: pathlib.Path) -> int:
    """Checks one installed product; returns how many (bundle, library)
    pairs were held to their record."""
    product_activation = json.loads(
        (prefix / "openstrata.activation.json").read_text(encoding="utf-8"))
    target_os = product_activation.get("target_os", "")
    if target_os not in ("windows", "linux", "macos"):
        fail_setup(f"the product states target_os {target_os!r}")
    product_paths = set(product_activation.get("library_paths", []))

    shared = shared_libraries()
    bundles = sorted(path for path in (prefix / "bundles").iterdir()
                     if (path / "dependencies.json").is_file())
    if not bundles:
        fail_setup(f"{prefix} holds no bundle with a dependencies.json")

    checked = 0
    for bundle in bundles:
        record = json.loads(
            (bundle / "dependencies.json").read_text(encoding="utf-8"))
        activation = json.loads(
            (bundle / "openstrata.activation.json").read_text(encoding="utf-8"))
        bundle_paths = set(activation.get("library_paths", []))
        declared = set()
        for library in record.get("libraries", []):
            identity = library["id"]
            if identity not in shared:
                continue
            declared.add(identity)
            checked += 1
            pattern = binary_pattern(shared[identity], target_os)
            directories = library.get("runtime_directories", [])
            holding = [directory for directory in directories
                       if (bundle / directory).is_dir()
                       and any(pattern.match(entry.name)
                               for entry in (bundle / directory).iterdir())]
            if not failures.check(
                    bool(holding),
                    f"{bundle.name}: records {identity} at "
                    f"{directories or 'no directory'} and carries no "
                    f"{identity} binary in any of them"):
                continue
            for directory in holding:
                failures.check(
                    directory in bundle_paths,
                    f"{bundle.name}: {identity} is in {directory}, which the "
                    f"bundle's activation does not put on the loader path")
                failures.check(
                    f"bundles/{bundle.name}/{directory}" in product_paths,
                    f"{bundle.name}: {identity} is in {directory}, which the "
                    f"product's activation does not put on the loader path")
            print(f"  {bundle.name}: {identity} in {', '.join(holding)}")

        # The other direction, reported rather than failed (see the module
        # docstring): a shared library of this workspace that the bundle
        # carries and does not declare.
        staged = bundle / "runtime" / "libraries"
        if staged.is_dir():
            for path in sorted(staged.rglob("*")):
                if not path.is_file() or not looks_shared(path, target_os):
                    continue
                owner = next((identity for identity, target in shared.items()
                              if binary_pattern(target, target_os)
                              .match(path.name)), None)
                if owner is not None and owner not in declared:
                    print(f"  note: {bundle.name} carries "
                          f"{path.relative_to(bundle).as_posix()}, a library "
                          f"it does not declare (ost report 40)")
    return checked


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group()
    source.add_argument("--product", default=None,
                        help="an existing product dist directory, manifest.json "
                             "or .tar.zst; default packages the workspace")
    source.add_argument("--prefix", default=None,
                        help="an already installed product prefix")
    parser.add_argument("--ost", default="ost", help="ost executable")
    args = parser.parse_args()

    scratch = None
    if args.prefix is not None:
        prefix = pathlib.Path(args.prefix).resolve()
        if not (prefix / "openstrata.activation.json").is_file():
            fail_setup(f"{prefix} is not an installed product")
    else:
        if args.product is None:
            platform, profile = workspace_target()
            product = package_product(args.ost, platform, profile)
        else:
            product = pathlib.Path(args.product).resolve()
            if not product.exists():
                fail_setup(f"no product at {product}")
        run([args.ost, "plugin", "product", "verify", str(product)])
        scratch = pathlib.Path(tempfile.mkdtemp(prefix="vrm-product-libs-"))
        prefix = scratch / "prefix"
        run([args.ost, "plugin", "product", "install", "--prefix",
             str(prefix), str(product)])

    failures = Failures()
    try:
        checked = check_prefix(failures, prefix)
    finally:
        if scratch is not None:
            shutil.rmtree(scratch, ignore_errors=True)
    if checked == 0:
        fail_setup("no bundle in the product declares a shared workspace "
                   "library, so nothing was checked")

    rc = failures.report()
    print(f"product shared libraries ({checked} checked): "
          + ("PASS" if rc == 0 else "FAIL"))
    return rc


if __name__ == "__main__":
    sys.exit(main())
