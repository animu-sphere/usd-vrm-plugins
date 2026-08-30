#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Run every `find_package` consumer on this host, and record what it measured.

PKG-4's lane, as one command. It is a separate entry point from the driver
rather than a loop in the workflow YAML for the reason the track states about
lanes generally: a lane that cannot be reproduced by hand is a lane nobody can
debug. This is the thing CI runs, and it is the thing a maintainer runs when CI
disagrees with them.

    python scripts/run_package_consumer_lane.py --reports reports-linux

**Which packages it runs is read, not listed.** The set is every row in
PACKAGE_CONTRACT.md section 4 that carries a `find_package` contract, taken
through the driver's own parser -- so a thirteenth package arrives in this lane
by acquiring a contract row, and cannot arrive in the contract without arriving
here. A list in this file would be the one place the twelve could quietly become
eleven.

**The OpenUSD prefix is resolved, not remembered.** `ost env <platform>
--profile <profile> --json` names the runtime this host materialized, which is
where a consumer of these packages gets OpenUSD from; `--extra-prefix` is how it
reaches the driver, exactly as it does for anyone else. It is never the
workspace build tree, and there is not one here: this lane builds no workspace.

Exit 0 when every package met every criterion this host can check, 1 when one
did not, 2 when the run could not be set up. Criterion 6 is not answered here at
all -- one host cannot -- and `scripts/check_package_closures.py` is what reads
the reports this writes from all three.
"""
from __future__ import annotations

import argparse
import json
import pathlib
import platform
import re
import shutil
import subprocess
import sys
import sysconfig

REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
DRIVER = REPO_ROOT / "scripts/check_package_consumer.py"

sys.path.insert(0, str(REPO_ROOT / "scripts"))
import check_package_consumer as driver  # noqa: E402


def fail_setup(msg: str) -> int:
    print(f"SETUP: {msg}")
    return 2


def runtime_prefix(runtime_platform: str, profile: str) -> str:
    """Where OpenUSD is on this host, from `ost` rather than from a path this
    file remembers. The README tells a workstation to read the same directory
    out of `.strata/targets/<target>/toolchain.cmake`; that file is written by a
    workspace build, and this lane deliberately does not run one."""
    ost = shutil.which("ost")
    if not ost:
        return ""
    result = subprocess.run(
        [ost, "env", runtime_platform, "--profile", profile, "--json"],
        text=True, capture_output=True, errors="replace")
    if result.returncode:
        return ""
    try:
        data = json.loads(result.stdout)["data"]
    except (ValueError, KeyError):
        return ""
    return data["prefix"] if data.get("pulled") else ""


def python_development(root: str) -> tuple:
    """The `Python3_EXECUTABLE`, `Python3_LIBRARY` and `Python3_INCLUDE_DIR`
    this host can offer `pxrConfig.cmake`, or an empty tuple.

    Derived from the interpreter running this script rather than searched for,
    because a lane already pins that to the runtime's Python version and
    `sysconfig` is that interpreter describing its own installation. A named
    root is honoured for a hand run, and then the same layout is assumed
    beneath it.

    Every path is checked to exist before it is offered. A variable naming a
    file that is not there is worse than not setting it: `pxrConfig.cmake`
    guards its own values with `if(NOT DEFINED ...)`, so a wrong value here
    replaces a possibly-right one with a definitely-wrong one, and the failure
    then reads as a defect in the package rather than in this derivation."""
    prefix = pathlib.Path(root or sysconfig.get_config_var("prefix") or "")
    if not prefix.is_dir():
        return ()
    tag = sysconfig.get_config_var("py_version_short") or ""
    if sys.platform == "win32":
        nodot = sysconfig.get_config_var("py_version_nodot") or tag.replace(".", "")
        library = prefix / "libs" / f"python{nodot}.lib"
        include = prefix / "Include"
    else:
        libdir = pathlib.Path(sysconfig.get_config_var("LIBDIR") or (prefix / "lib"))
        names = [sysconfig.get_config_var(v) for v in ("LDLIBRARY", "INSTSONAME")]
        library = next((libdir / n for n in names if n and (libdir / n).exists()),
                       libdir / f"libpython{tag}.so")
        include = pathlib.Path(sysconfig.get_paths()["include"])
    executable = pathlib.Path(sys.executable) if not root else next(
        (p for p in (prefix / "python.exe", prefix / "bin" / f"python{tag}",
                     prefix / "bin" / "python3") if p.exists()),
        pathlib.Path(sys.executable))
    if not (executable.exists() and library.exists() and include.is_dir()):
        return ()
    return (str(executable), str(library), str(include))


BAKED_INCLUDE = re.compile(r"set\(Python3_INCLUDE_DIR \[\[(.+?)\]\]\)")


def repair_runtime_python_include(prefix: str, include_dir: str) -> tuple:
    """Point a pulled runtime's imported targets at a Python include directory
    that exists on this host, and say what was changed.

    `pxrTargets.cmake` writes the producing machine's Python include directory
    into the `INTERFACE_INCLUDE_DIRECTORIES` of sixteen imported targets. That
    is not a variable and no `-D` reaches it: CMake refuses at generate time
    with *Imported target "tf" includes non-existent path*, on every host that
    is not the producer. Measured on 2026-08-30 against all three published
    26.08 leaves, and reproduced against a whole-workspace configure -- so this
    is not something `ost build` is immune to, only something no lane had yet
    asked of a host that did not build the runtime
    (docs/reports/ost/37-2026-08-30-v0.22.6-runtime-python-paths-from-the-producer.md).

    **The path is read, not guessed.** `pxrConfig.cmake` names the producer's
    own `Python3_INCLUDE_DIR` in a literal, so the substitution is exactly that
    string and nothing that resembles it. If it exists on this host there is
    nothing to repair, and the macOS leaf is that case.

    This edits the *materialized* runtime, never the artifact it came from --
    `ost runtime pull --force` restores it byte for byte. It is repair of an
    external prefix, which is a different act from editing a package under test:
    nothing in this workspace is touched, and a package that then fails, failed.
    """
    root = pathlib.Path(prefix)
    config, targets = root / "pxrConfig.cmake", root / "cmake" / "pxrTargets.cmake"
    if not (config.exists() and targets.exists()):
        return ("", 0, "not-a-pxr-prefix")
    m = BAKED_INCLUDE.search(config.read_text(encoding="utf-8"))
    if not m:
        return ("", 0, "no-baked-include")
    baked = m.group(1)
    if pathlib.Path(baked).is_dir():
        return (baked, 0, "exists")

    # Two spellings of one directory: `pxrConfig.cmake` keeps the separators the
    # producing platform used and `pxrTargets.cmake` normalizes them, so a
    # substitution of one form alone silently repairs nothing on Windows.
    #
    # And what is written back must be CMake's spelling, not this platform's:
    # a Windows path with backslashes lands inside a quoted argument where
    # `\U` and `\P` are escapes, and the file stops parsing -- measured, by
    # writing one and reading `Syntax error in cmake code` back out.
    replacement = include_dir.replace("\\", "/")
    text = original = targets.read_text(encoding="utf-8")
    changed = 0
    for spelling in {baked, baked.replace("\\", "/")}:
        changed += text.count(spelling)
        text = text.replace(spelling, replacement)
    if not changed:
        # The path is absent from this host *and* absent from the file that was
        # supposed to name it. Nothing was repaired and nothing is known to be
        # broken; saying "no repair needed" here would be a claim about the
        # second half that this function never checked.
        return (baked, 0, "absent")
    targets.write_text(text, encoding="utf-8")
    return (baked, changed, "repaired")


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--reports", default="package-consumer-reports",
                    metavar="DIR", help="where the JSON reports are written")
    ap.add_argument("--extra-prefix", action="append", default=[],
                    metavar="DIR",
                    help="a prefix holding non-workspace packages. When "
                         "omitted, `ost env` names the materialized runtime")
    ap.add_argument("--platform", default="cy2026",
                    help="the runtime platform to resolve OpenUSD from")
    ap.add_argument("--profile", default="usd")
    ap.add_argument("--generator", help="CMake generator, passed through")
    ap.add_argument("--python-root", default="", metavar="DIR",
                    help="the Python 3 installation whose Development artifacts "
                         "`pxrConfig.cmake` is given. Defaults to the "
                         "interpreter running this script, which is the one a "
                         "lane already pins to the runtime's version")
    ap.add_argument("--no-repair-runtime", action="store_true",
                    help="leave a pulled runtime's baked Python include "
                         "directory alone. The run then fails on any host that "
                         "did not build the runtime, which is what the "
                         "unrepaired artifact does (report 37)")
    ap.add_argument("--package", action="append", default=[], metavar="NAME",
                    help="run only these packages (default: every row with a "
                         "find_package contract)")
    args = ap.parse_args()

    rows = driver.contract_rows()
    packages = [name for name, row in rows.items()
                if "reserved" not in (row["target"], row["product"])
                and row["target"] not in ("-", "—")]
    if args.package:
        unknown = [p for p in args.package if p not in packages]
        if unknown:
            return fail_setup(
                f"{', '.join(unknown)} carries no find_package contract in "
                f"PACKAGE_CONTRACT.md section 4")
        packages = [p for p in packages if p in args.package]

    extra = list(args.extra_prefix)
    if not extra:
        prefix = runtime_prefix(args.platform, args.profile)
        if prefix:
            extra = [prefix]
            print(f"OpenUSD from `ost env {args.platform} "
                  f"--profile {args.profile}`: {prefix}")
        else:
            # Four of the twelve need no OpenUSD, so this is not fatal -- but
            # it is the difference between a lane that measured twelve packages
            # and one that measured four while printing nothing about it.
            print("note: no materialized runtime and no --extra-prefix; only "
                  "the packages that need no OpenUSD can pass")

    python = python_development(args.python_root)
    if python:
        print("Python 3 Development artifacts, for `pxrConfig.cmake`'s own "
              "find_dependency:")
        for flag, value in zip(("executable", "library", "include"), python):
            print(f"  {flag}: {value}")
    else:
        # Not fatal, because the runtime's own baked paths may resolve on this
        # host -- they do on the machine that produced it, and they did on the
        # macOS runner. Saying so is the difference between a host that did not
        # need this and one where the derivation quietly found nothing.
        print("note: no Python 3 Development artifacts derived; "
              "`pxrConfig.cmake`'s own baked paths are what will answer")

    if python and not args.no_repair_runtime:
        for prefix in extra:
            baked, changed, note = repair_runtime_python_include(prefix,
                                                                 python[2])
            if note == "repaired":
                print(f"repaired {prefix}: {changed} imported-target include "
                      f"entries named `{baked}`, which is not a directory on "
                      f"this host (report 37)")
            elif note == "exists":
                print(f"no repair needed: the runtime's baked `{baked}` is a "
                      f"directory on this host")
            elif note == "absent":
                print(f"nothing repaired: `{baked}` is not on this host and "
                      f"appears in no imported target either")

    reports = pathlib.Path(args.reports)
    reports.mkdir(parents=True, exist_ok=True)

    outcomes: dict = {}
    for package in packages:
        print(f"\n{'=' * 70}\n=== {package}\n{'=' * 70}", flush=True)
        command = [sys.executable, str(DRIVER), package,
                   "--json", str(reports / f"{package}.json")]
        for prefix in extra:
            command += ["--extra-prefix", prefix]
        if python:
            command += ["--python-executable", python[0],
                        "--python-library", python[1],
                        "--python-include-dir", python[2]]
        if args.generator:
            command += ["--generator", args.generator]
        outcomes[package] = subprocess.run(command).returncode

    print(f"\n{'=' * 70}")
    print(f"PACKAGE_CONTRACT.md section 5, criteria 1-5 on "
          f"{platform.system()} -- {len(packages)} packages")
    for package in packages:
        code = outcomes[package]
        print(f"  {'pass' if code == 0 else 'FAIL'}  {package}"
              + ("" if code == 0 else f"  (exit {code})"))
    # The driver's two failing answers are different facts, and a summary that
    # collapses them points an operator at the wrong file. Exit 1 is a package
    # that did not meet its contract, and PKG-3's rule applies: fix the config,
    # never the fixture. Exit 2 is the run not having been set up -- which is
    # what this lane's own first run met, on eight packages at once, and telling
    # that operator to go and edit a config file would have named the one thing
    # that was not wrong.
    contract = [p for p, code in outcomes.items() if code == 1]
    setup = [p for p, code in outcomes.items() if code not in (0, 1)]
    if contract:
        print(f"\nFAIL: {', '.join(contract)} did not meet the contract. Fix "
              f"the config, never the fixture "
              f"(roadmap/packaging-hardening.md PKG-3).")
    if setup:
        print(f"\nSETUP: {', '.join(setup)} could not be run at all, so this "
              f"host measured nothing about them. That is a fact about the "
              f"host or the prefixes it was given rather than about the "
              f"package, and criterion 6 refuses a comparison that is missing "
              f"them rather than comparing what is left.")
    if contract or setup:
        return 1
    print(f"\nPASS: every package met every criterion {platform.system()} can "
          f"check. Criterion 6 needs three platforms and is answered by "
          f"scripts/check_package_closures.py.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
