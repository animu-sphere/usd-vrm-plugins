#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Configure a real external consumer against one installed package.

The check every lane in this repository is structurally unable to make. A
composed workspace build defines every target as an alias in one CMake project,
and `ost library build` composes `requires.libraries` the same way; neither ever
opens a `*Config.cmake`. So the configuration that fails is the one nothing
runs, and on 2026-08-29 it shipped: two adapters named `osc::osc` on their
installed interface link line with no `find_dependency(osc)` in either config,
with all 17 lanes green (PACKAGE_CONTRACT.md 1).

What this does, for one named package:

    1. install it -- and its required workspace packages -- into a scratch
       prefix that holds nothing else;
    2. copy tests/consumer/ **outside the repository**, so the fixture cannot
       resolve anything through the source tree it came from;
    3. configure the fixture with that prefix as its only workspace prefix;
    4. build it, and run it;
    5. report which of PACKAGE_CONTRACT.md section 5's six criteria were met.

Criteria 1-3 are reported by the fixture at configure time (see
tests/consumer/ConsumerCriteria.cmake, whose message spellings this script
parses). 4 is the build. 5 is a property of the fixture's own sources and is
checked here, statically, because a fixture is the one thing in this loop that
can be edited to make a package pass. 6 needs three platforms and belongs to
PKG-4's lane -- this run records the closure it measured so that lane has
something to compare.

    python scripts/check_package_consumer.py osc
    python scripts/check_package_consumer.py osc --prefix-source ost-package
    python scripts/check_package_consumer.py osc --mutate no-targets-include
    python scripts/check_package_consumer.py osc --json report.json

**The negative verification.** A green consumer proves nothing until the same
consumer has been shown to go red for the right reason. `--mutate` breaks the
*installed* prefix -- never the source tree, so no `git stash` is involved and
none of that trap's silent no-ops apply -- and then requires the run to fail:
exit 0 means the mutation was caught, and exit 1 means the fixture passed
against a package it should not have. Every mutation asserts that it actually
changed a byte, because a mutation that matched nothing and reported a catch is
the same lie in a smaller package.
"""
from __future__ import annotations

import argparse
import json
import os
import pathlib
import platform
import re
import shutil
import subprocess
import sys
import tempfile
from typing import NoReturn

REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
CONTRACT = REPO_ROOT / "docs/architecture/PACKAGE_CONTRACT.md"
FIXTURE_ROOT = REPO_ROOT / "tests/consumer"

# Packages a consumer resolves that this workspace does not produce. They are
# not installed into the scratch prefix -- they arrive through --extra-prefix,
# the same way they would arrive for anyone else.
EXTERNAL_PACKAGES = {"pxr", "Threads"}


def fail_setup(msg: str) -> NoReturn:
    """Exit 2 -- the harness is misconfigured, which is not the same answer as
    a package that failed its contract (exit 1)."""
    print(f"SETUP: {msg}")
    raise SystemExit(2)


def run(cmd: list, **kw) -> subprocess.CompletedProcess:
    print(f"$ {' '.join(str(c) for c in cmd)}", flush=True)
    # `errors="replace"` rather than the default, because a captured toolchain
    # speaks the host's language: MSBuild on a Japanese Windows emits cp932 and
    # a stray byte raises UnicodeDecodeError inside subprocess's reader thread,
    # where it surfaces as `stdout is None` several frames away from the cause.
    # A criterion this driver could not parse must read as unmet, not as a
    # traceback.
    return subprocess.run([str(c) for c in cmd], text=True,
                          errors="replace", **kw)


# --------------------------------------------------------------------------
# The contract is the input, not a copy of it
# --------------------------------------------------------------------------
#
# Every fact this driver needs about a package -- its exported target, its
# header root, what must resolve before it does -- is already stated per package
# in PACKAGE_CONTRACT.md section 4. Reading it there rather than keeping a table
# here is what stops the two from drifting, and it means a contract row that is
# wrong about a package fails this check instead of quietly not being used. That
# is the direction the track wants: the document is the claim, and this is what
# holds it to one.

ROW = re.compile(r"^\|(?P<cells>.+)\|\s*$")


def contract_rows() -> dict:
    if not CONTRACT.exists():
        fail_setup(f"{CONTRACT} is missing")
    rows: dict = {}
    for line in CONTRACT.read_text(encoding="utf-8").splitlines():
        m = ROW.match(line.strip())
        if not m:
            continue
        cells = [c.strip() for c in m.group("cells").split("|")]
        # A package row's name cell is always backticked, which is what
        # separates it from the table header above it and the separator between
        # them. Matching on shape rather than on the literal word `Package`
        # keeps a future column rename out of this loop.
        if len(cells) != 7 or not cells[0].startswith("`"):
            continue
        name = cells[0].strip("`")
        if not re.fullmatch(r"[A-Za-z][A-Za-z0-9]*", name):
            continue
        rows[name] = {
            "package": name,
            "target": cells[1].strip("`"),
            "headers": cells[2].strip("`"),
            "requires": cells[3],
            "platform": cells[4],
            "product": cells[5],
            "standalone": cells[6],
        }
    if not rows:
        fail_setup("no package rows parsed out of PACKAGE_CONTRACT.md section 4")
    return rows


def required_packages(row: dict) -> list:
    """The `Required packages` cell, as names. Parenthetical qualifications --
    `pxr (guarded on pxr_FOUND)`, `Threads (non-Windows)` -- are conditions on
    how a config resolves it, not part of the name."""
    cell = re.sub(r"\([^)]*\)", " ", row["requires"])
    return [n.strip("`") for n in re.findall(r"`[^`]+`", cell)]


def workspace_closure(package: str, rows: dict) -> list:
    """Every workspace package that must be installed before `package` can be,
    in an order that installs each after its own dependencies.

    A depth-first post-order rather than the table's own order. `motionBvh`
    requires `motionSource`, which requires `motionCore`, and `motionCore` is
    nowhere in `motionBvh`'s row -- PACKAGE_CONTRACT.md section 3 rule 3 forbids
    a config from reaching past its own declared edges, so the transitive set is
    something a consumer's installer has to compute rather than read. Taking the
    row's list literally worked only for the packages the table happens to list
    in topological order, and `motionBvh` is the one it does not."""
    order: list = []
    seen: set = set()

    def visit(name: str, stack: tuple) -> None:
        if name in stack:
            fail_setup("PACKAGE_CONTRACT.md section 4 describes a dependency "
                       "cycle: " + " -> ".join(stack + (name,)))
        if name in seen:
            return
        if name not in rows:
            fail_setup(f"`{name}` is required by `{stack[-1]}` and has no row "
                       f"in PACKAGE_CONTRACT.md section 4")
        for dep in required_packages(rows[name]):
            if dep not in EXTERNAL_PACKAGES:
                visit(dep, stack + (name,))
        seen.add(name)
        if name != package:
            order.append(name)

    visit(package, ())
    return order


def source_dir(package: str) -> pathlib.Path:
    """Where the package's own standalone CMake project lives. Discovered from
    the descriptors rather than from a path table, on the same rule
    check_docs.py discovers identities by."""
    for pattern, key in (("libs/*/openstrata.library.yaml", "id"),
                         ("adapters/*/*/openstrata.library.yaml", "id"),
                         ("plugins/*/openstrata.plugin.yaml", "name")):
        for descriptor in sorted(REPO_ROOT.glob(pattern)):
            text = descriptor.read_text(encoding="utf-8")
            m = re.search(rf"^\s*{key}:\s*(.+?)\s*$", text, re.M)
            if m and m.group(1).strip().strip("'\"") == package:
                return descriptor.parent
    fail_setup(f"no descriptor in the workspace declares `{package}`")


# --------------------------------------------------------------------------
# Criterion 5 -- the one a fixture can lose by being edited
# --------------------------------------------------------------------------

def strip_comments(path: pathlib.Path) -> str:
    """The file's code, without what a reader wrote about it. A comment naming
    a sibling identity is prose; a link line naming one is a leak, and only the
    second is what this criterion is about."""
    text = path.read_text(encoding="utf-8")
    if path.suffix in (".cpp", ".h"):
        text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
        text = re.sub(r"//[^\n]*", "", text)
        return text
    return "\n".join(line for line in text.splitlines()
                     if not line.lstrip().startswith("#"))


def check_fixture(package: str, row: dict, fixture: pathlib.Path) -> list:
    """Static properties of the sources the fixture is built from. Returns the
    reasons it is not trustworthy, empty when it is.

    PACKAGE_CONTRACT.md section 5: *criterion 5 is the one that makes the others
    mean anything*. A fixture that names a workspace target, points at a source
    path, or quietly stopped including a public header still builds -- and a
    passing build is exactly what it would report.

    **The shared module is scanned too, and that is not optional.**
    ConsumerCriteria.cmake is copied beside every fixture and included by all of
    them, so a workspace identity or a `CMAKE_SOURCE_DIR` added there leaks into
    twelve fixtures at once while all twelve go on reporting criterion 5 met --
    which is the failure this criterion exists to make impossible, arriving
    through the one file the fixtures do not own."""
    reasons = []
    cmakelists = fixture / "CMakeLists.txt"
    main = fixture / "main.cpp"
    shared = fixture.parent / "ConsumerCriteria.cmake"
    for path in (cmakelists, main, shared):
        if not path.exists():
            fail_setup(f"fixture {fixture} cannot be checked: {path} is missing")

    identities = set(contract_rows()) - {package}
    for path in (cmakelists, main, shared):
        body = strip_comments(path)
        for other in sorted(identities):
            if re.search(rf"\b{re.escape(other)}\b", body):
                reasons.append(f"{path.name} names the workspace identity "
                               f"`{other}`, which is not the package under test")
        for pattern, what in ((r"add_subdirectory", "add_subdirectory()"),
                              (r"\.\./\.\./", "a path out of the fixture tree"),
                              (r"CMAKE_SOURCE_DIR", "CMAKE_SOURCE_DIR")):
            if re.search(pattern, body):
                reasons.append(f"{path.name} uses {what}, which can reach the "
                               f"workspace source tree")

    header_root = row["headers"].rstrip("/").split("/")[-1]
    includes = re.findall(r'#\s*include\s*[<"]([^>"]+)[>"]',
                          main.read_text(encoding="utf-8"))
    if not any(i.startswith(f"{header_root}/") for i in includes):
        reasons.append(f"main.cpp includes no header under `{header_root}/`, "
                       f"so it would build against a package that installed "
                       f"none")
    return reasons


# --------------------------------------------------------------------------
# The prefix
# --------------------------------------------------------------------------

def cmake_install(package: str, prefix: pathlib.Path, extra_prefixes: list,
                  generator, work: pathlib.Path) -> None:
    src = source_dir(package)
    build = work / f"build-{package}"
    configure = ["cmake", "-S", src, "-B", build,
                 f"-DCMAKE_INSTALL_PREFIX={prefix}",
                 "-DCMAKE_BUILD_TYPE=Release",
                 f"-D{package.upper()}_BUILD_TESTS=OFF",
                 "-DUSDVRM_BUILD_TESTS=OFF"]
    if generator:
        configure += ["-G", generator]
    search = [str(prefix)] + list(extra_prefixes)
    configure.append("-DCMAKE_PREFIX_PATH=" + ";".join(search))
    if run(configure).returncode:
        fail_setup(f"configuring {package} for install failed")
    if run(["cmake", "--build", build, "--config", "Release"]).returncode:
        fail_setup(f"building {package} for install failed")
    if run(["cmake", "--install", build, "--config", "Release"]).returncode:
        fail_setup(f"installing {package} failed")


def ost_package_install(package: str, prefix: pathlib.Path,
                        work: pathlib.Path) -> None:
    """The other prefix a consumer can get: an extracted `ost` artifact.

    PKG-2 exists partly to answer whether these are the same artifact. For a
    plain library they are -- `ost library build` runs the same install rules
    into an isolated prefix -- but the question had to be measured rather than
    assumed, because `ost` stages a *bundle*'s dependencies under
    `runtime/libraries/{lib,bin}` and a contract that held for one shape and not
    the other would be a finding."""
    src = source_dir(package)
    ost = shutil.which("ost")
    if not ost:
        fail_setup("--prefix-source ost-package needs `ost` on PATH")
    if run([ost, "library", "build", src]).returncode:
        fail_setup(f"ost library build {src} failed")
    staged = sorted(src.glob(".strata/targets/*/library-prefix"))
    if not staged:
        fail_setup(f"ost library build left no library-prefix under {src}")
    if len(staged) > 1:
        print(f"note: {len(staged)} target prefixes staged; using {staged[-1]}")
    shutil.copytree(staged[-1], prefix, dirs_exist_ok=True)


# --------------------------------------------------------------------------
# The mutations -- how the fixture earns being believed
# --------------------------------------------------------------------------
#
# Each one breaks the installed prefix in a way exactly one criterion is
# supposed to notice, and each asserts it changed something. A mutation that
# silently matched nothing would report a catch it never made, which is the
# same class of false green as a `git stash push` that was a no-op because the
# fix was already committed.

def locate_config(package: str, prefix: pathlib.Path) -> pathlib.Path:
    """The installed config file, found rather than assumed.

    `lib/cmake/<pkg>/` is where `${CMAKE_INSTALL_LIBDIR}` lands on this
    workstation and on Windows, and it is *not* where it lands on a `lib64`
    distribution -- where hardcoding it turned a mutation that cannot apply
    into a `FileNotFoundError`, which is the one outcome a negative
    verification must never produce: an exception is not a refusal, and a
    disabled check that raises looks like a broken script rather than a
    missing measurement."""
    found = sorted(prefix.rglob(f"{package}Config.cmake"))
    if not found:
        fail_setup(f"{prefix} holds no {package}Config.cmake, so `{package}` "
                   f"installed no CMake package to mutate")
    if len(found) > 1:
        fail_setup(f"{prefix} holds {len(found)} copies of "
                   f"{package}Config.cmake: {', '.join(str(f) for f in found)}")
    return found[0]


def mutate(name: str, package: str, row: dict, prefix: pathlib.Path) -> str:
    config = locate_config(package, prefix)

    if name == "no-config":
        if not config.exists():
            fail_setup(f"{config} is not there to remove")
        config.unlink()
        return f"deleted {config.relative_to(prefix)} - criterion 1 must fail"

    if name == "no-targets-include":
        text = config.read_text(encoding="utf-8")
        stripped = re.sub(r"^include\(.*Targets\.cmake.*\)\s*$", "",
                          text, flags=re.M)
        if stripped == text:
            fail_setup(f"{config} has no targets include to remove")
        config.write_text(stripped, encoding="utf-8")
        return (f"removed the targets include from "
                f"{config.relative_to(prefix)} - criterion 2 must fail")

    if name == "no-dependency":
        text = config.read_text(encoding="utf-8")
        stripped = re.sub(r"^\s*find_dependency\(.*\)\s*$", "",
                          text, flags=re.M)
        if stripped == text:
            fail_setup(
                f"{config} contains no find_dependency, so this mutation "
                f"cannot break it. That is the measurement rather than an "
                f"error in it: `{package}`'s required-package set is empty "
                f"(PACKAGE_CONTRACT.md section 4), so use this mutation on a "
                f"package that has edges")
        config.write_text(stripped, encoding="utf-8")
        return (f"removed every find_dependency from "
                f"{config.relative_to(prefix)} - criterion 3 must fail")

    if name == "no-headers":
        # The header root is a contract value, not a guess: PACKAGE_CONTRACT.md
        # section 2 says it is always `include/<name>/`, so the row is what
        # names the directory to delete.
        include = prefix / row["headers"].rstrip("/")
        if not include.is_dir():
            fail_setup(f"{include} is not there to remove")
        shutil.rmtree(include)
        return (f"deleted {row['headers']} - criterion 4 must fail, and "
                f"criteria 1-3 must still pass")

    fail_setup(f"unknown mutation `{name}`")


# Which criterion each mutation is aimed at. This is documentation with an
# assertion behind it rather than a gate: a mutation is *caught* when any of
# criteria 1-4 fails, because those are the four the prefix can break, and the
# run says so when the criterion that failed is not the one predicted. That
# distinction is a measurement PKG-3 will want -- stripping a `find_dependency`
# may be refused by the exported targets file at `find_package` time, which is
# criterion 1 catching a criterion 3 defect, and a gate insisting on 3 would
# have called a correct catch a failure.
MUTATION_TARGET = {
    "no-config": "1",
    "no-targets-include": "2",
    "no-dependency": "3",
    "no-headers": "4",
}
MUTATIONS = tuple(MUTATION_TARGET)


# --------------------------------------------------------------------------
# The run
# --------------------------------------------------------------------------

MET = re.compile(r"consumer: criterion (\d) MET (.*)")
NOT_MET = re.compile(r"consumer: criterion (\d) NOT MET (.*)")
CLOSURE = re.compile(r"consumer: closure (.+?)\s*$", re.M)
PLATFORM = re.compile(r"consumer: platform (\S+) *(\S*)")
PACKAGE_DIR = re.compile(r"consumer: package-dir (.+?)\s*$", re.M)


def make_output_lossy() -> None:
    """Never let printing a toolchain's own words end this run.

    Decoding the capture with `errors="replace"` moved the cp932 problem rather
    than removing it: U+FFFD is not encodable in cp932 either, so echoing what
    was just decoded raised `UnicodeEncodeError` on the way *out* -- on the
    same Japanese-Windows input, several frames from the cause, and past every
    `try` in this file. Both halves have to be lossy for the invariant in this
    module's docstring to hold: a criterion this driver cannot render must read
    as unmet, never as a traceback."""
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            try:
                stream.reconfigure(errors="replace")
            except (ValueError, OSError):
                # A stream that will not be reconfigured is one this process
                # does not own. Nothing to do, and nothing worth failing over.
                pass


def run_environment(prefix: pathlib.Path, extra_prefixes: list) -> dict:
    """The environment the built consumer is run in.

    A consumer of a SHARED package -- `vrmContainer` and `vrmSchema` are the two
    -- links an import library at build time and needs the DLL itself at load
    time. Without this, those two would exit 0xC0000135 on Windows and this
    driver would report *the package links but does not work*, which is a
    harness property filed as a contract failure. The prefix's own `bin` and
    `lib` are added and nothing else: a run that needed a directory outside the
    prefix would be telling us something true about the package."""
    env = dict(os.environ)
    roots = [prefix] + [pathlib.Path(p) for p in extra_prefixes]
    entries = [str(root / sub) for root in roots for sub in ("bin", "lib")
               if (root / sub).is_dir()]
    if not entries:
        return env
    key = "PATH" if sys.platform == "win32" else (
        "DYLD_LIBRARY_PATH" if sys.platform == "darwin" else "LD_LIBRARY_PATH")
    env[key] = os.pathsep.join(entries + ([env[key]] if env.get(key) else []))
    return env


def main() -> int:
    make_output_lossy()
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("package", help="the package under test, e.g. osc")
    ap.add_argument("--prefix-source", choices=("cmake-install", "ost-package"),
                    default="cmake-install",
                    help="how the install prefix is produced "
                         "(default: cmake-install)")
    ap.add_argument("--extra-prefix", action="append", default=[],
                    metavar="DIR",
                    help="a prefix holding non-workspace packages (OpenUSD). "
                         "Repeatable. Never the workspace build tree.")
    ap.add_argument("--generator", help="CMake generator for both the install "
                                        "and the consumer (default: CMake's)")
    ap.add_argument("--mutate", choices=MUTATIONS,
                    help="break the installed prefix and require the consumer "
                         "to fail - the negative verification")
    ap.add_argument("--json", metavar="PATH", help="write the report here")
    ap.add_argument("--keep", action="store_true",
                    help="keep the scratch prefix and build trees")
    args = ap.parse_args()

    rows = contract_rows()
    if args.package not in rows:
        fail_setup(f"`{args.package}` has no row in PACKAGE_CONTRACT.md "
                   f"section 4")
    row = rows[args.package]
    # Two different reasons a row carries no `find_package` contract, and they
    # are told apart by two different cells. A reserved identity says so in
    # `Exported target` (`vrmAdapterArdy`) *or* in `In the aggregate product`
    # (`execMotion`, `execVrm`, whose target cell is a plain dash like a plugin
    # bundle's) -- so testing the target cell alone sent both of those away with
    # the plugin-load reason, which is not true of either and is not a reason a
    # reader could act on.
    if "reserved" in (row["target"], row["product"]):
        fail_setup(
            f"`{args.package}` is a reserved identity: it has a row in "
            f"PACKAGE_CONTRACT.md and no package behind it yet. It arrives here "
            f"when it acquires one (PACKAGE_CONTRACT.md section 6)")
    if row["target"] in ("-", "—"):
        fail_setup(
            f"`{args.package}` exports no target and installs no CMake config "
            f"by design (PACKAGE_CONTRACT.md 4.1). Its consumer contract is "
            f"plugin load, which scripts/clean_install_smoke.py gates")

    fixture_src = FIXTURE_ROOT / args.package
    if not fixture_src.is_dir():
        fail_setup(f"no consumer fixture at "
                   f"{fixture_src.relative_to(REPO_ROOT)} - "
                   f"PKG-1/PKG-3 add one per package")

    work = pathlib.Path(tempfile.mkdtemp(prefix=f"consumer-{args.package}-"))
    # Outside the repository is a property this check depends on, so it is
    # asserted rather than arranged and hoped for.
    if REPO_ROOT in work.parents or work == REPO_ROOT:
        fail_setup(f"scratch directory {work} is inside the repository")

    report: dict = {
        "package": args.package,
        "prefix_source": args.prefix_source,
        "mutation": args.mutate,
        "host": {"system": platform.system(), "machine": platform.machine()},
        "criteria": {},
        "closure": [],
    }
    prefix = work / "prefix"
    try:
        # --- criterion 5, before anything is built --------------------------
        reasons = check_fixture(args.package, row, fixture_src)
        report["criteria"]["5"] = {
            "met": not reasons,
            "detail": "; ".join(reasons) if reasons else
                      "the fixture names no workspace target, reaches no "
                      "source path, and includes a public header",
        }

        # --- the prefix -----------------------------------------------------
        deps = workspace_closure(args.package, rows)
        for dep in deps:
            print(f"--- installing required package {dep} ---")
            cmake_install(dep, prefix, args.extra_prefix, args.generator, work)
        print(f"--- installing {args.package} ---")
        if args.prefix_source == "cmake-install":
            cmake_install(args.package, prefix, args.extra_prefix,
                          args.generator, work)
        else:
            if deps:
                fail_setup("--prefix-source ost-package composes a library's "
                           "own dependencies itself; mixing it with a "
                           "cmake-installed dependency would not be either "
                           "artifact")
            ost_package_install(args.package, prefix, work)

        if args.mutate:
            # Criterion 5 is a property of the fixture and is settled above,
            # before the prefix is touched. A mutation run that started from an
            # already-broken fixture would find criterion 5 failed and report
            # the mutation as caught, recording a negative verification whose
            # effect was never observed -- so it is a setup error, not a catch.
            if not report["criteria"]["5"]["met"]:
                fail_setup(
                    f"the fixture already fails criterion 5 "
                    f"({report['criteria']['5']['detail']}), so a mutation run "
                    f"could only confirm that. Fix the fixture first")
            print(f"--- mutation: "
                  f"{mutate(args.mutate, args.package, row, prefix)} ---")

        # --- the consumer, outside the repository ---------------------------
        consumer_root = work / "consumer"
        shutil.copytree(FIXTURE_ROOT, consumer_root)
        fixture = consumer_root / args.package
        build = work / "consumer-build"
        configure = ["cmake", "-S", fixture, "-B", build,
                     "-DCMAKE_BUILD_TYPE=Release",
                     "-DCMAKE_PREFIX_PATH=" +
                     ";".join([str(prefix)] + list(args.extra_prefix))]
        if args.generator:
            configure += ["-G", args.generator]
        configured = run(configure, capture_output=True)
        print(configured.stdout, end="")
        print(configured.stderr, end="", file=sys.stderr)
        text = configured.stdout + configured.stderr

        for m in MET.finditer(text):
            report["criteria"][m.group(1)] = {"met": True,
                                              "detail": m.group(2).strip()}
        for m in NOT_MET.finditer(text):
            report["criteria"][m.group(1)] = {"met": False,
                                              "detail": m.group(2).strip()}
        report["closure"] = sorted({m.group(1) for m in CLOSURE.finditer(text)})
        host = PLATFORM.search(text)
        if host:
            report["host"]["cmake_system"] = host.group(1)
            report["host"]["cmake_processor"] = host.group(2)

        # Criterion 1 is *this* prefix answering, or it is not criterion 1.
        # CMake searches the host as well, so a stale install in /usr/local or
        # under Program Files satisfies 1-4 and prints PASS without the scratch
        # prefix ever being opened -- and turns `--mutate no-config` into a
        # report that the fixture is untrustworthy, which is the opposite of
        # what happened.
        found = PACKAGE_DIR.search(text)
        if found:
            package_dir = pathlib.Path(found.group(1))
            report["package_dir"] = str(package_dir)
            inside = prefix.resolve() in package_dir.resolve().parents
            if not inside:
                report["criteria"]["1"] = {
                    "met": False,
                    "detail": f"{args.package} resolved from {package_dir}, "
                              f"which is outside the scratch prefix "
                              f"{prefix} - this run measured some other "
                              f"install of the package",
                }

        for n in ("1", "2", "3"):
            if n not in report["criteria"]:
                # The configure died before that criterion printed anything.
                # Unreported is not met, and saying so is the difference
                # between this check and one that only looks for the word NOT.
                report["criteria"][n] = {
                    "met": False,
                    "detail": "the consumer's configure did not reach this "
                              "criterion" if configured.returncode else
                              "the configure succeeded without reporting this "
                              "criterion - ConsumerCriteria.cmake and this "
                              "driver disagree",
                }

        # --- criterion 4 ----------------------------------------------------
        built = configured.returncode == 0 and run(
            ["cmake", "--build", build, "--config", "Release"]).returncode == 0
        report["criteria"]["4"] = {
            "met": bool(built),
            "detail": "the consumer compiled and linked" if built else
                      "the consumer did not build",
        }
        if built:
            exe = next((p for p in sorted(build.rglob("consumer*"))
                        if p.is_file() and p.suffix in ("", ".exe")), None)
            ran = exe is not None and run(
                [exe],
                env=run_environment(prefix, args.extra_prefix)).returncode == 0
            report["ran"] = bool(ran)
            if not ran:
                report["criteria"]["4"] = {
                    "met": False,
                    "detail": "the consumer built but did not run - the "
                              "package links but does not work",
                }

        # --- criterion 6 ----------------------------------------------------
        report["criteria"]["6"] = {
            "met": None,
            "detail": f"one platform ({platform.system()}) cannot answer "
                      f"whether three agree; PKG-4's lane compares the closure "
                      f"recorded here",
        }
    finally:
        if args.keep:
            print(f"kept: {work}")
        else:
            shutil.rmtree(work, ignore_errors=True)

    # --- the answer ---------------------------------------------------------
    print()
    print(f"PACKAGE_CONTRACT.md section 5 - {args.package} "
          f"(prefix from {args.prefix_source})")
    for n in sorted(report["criteria"]):
        state = report["criteria"][n]["met"]
        mark = {True: "met    ", False: "NOT MET", None: "n/a    "}[state]
        print(f"  {n}. {mark}  {report['criteria'][n]['detail']}")
    if report["closure"]:
        print(f"  link closure: {', '.join(report['closure'])}")
    else:
        print("  link closure: empty")

    if args.json:
        pathlib.Path(args.json).write_text(
            json.dumps(report, indent=2, sort_keys=True) + "\n",
            encoding="utf-8")
        print(f"  report: {args.json}")

    failed = [n for n, c in report["criteria"].items() if c["met"] is False]

    if args.mutate:
        # Inverted on purpose: with the prefix broken, a pass is the failure.
        #
        # Only criteria 1-4 count as a catch. Those are the four the *prefix*
        # can break, and they are the only ones whose failure is evidence that
        # this mutation did something -- criterion 5 is settled before the
        # mutation is applied and criterion 6 is never answered here, so
        # counting either would record a verification whose effect was never
        # observed.
        caught = sorted(n for n in failed if n in ("1", "2", "3", "4"))
        if caught:
            expected = MUTATION_TARGET[args.mutate]
            print(f"\nPASS (negative): the mutation `{args.mutate}` was caught "
                  f"by criterion {', '.join(caught)}")
            if expected not in caught:
                # Not a failure -- a measurement. A defect can legitimately be
                # refused earlier than the criterion it belongs to.
                print(f"  note: criterion {expected} was predicted and "
                      f"criterion {caught[0]} answered first")
            return 0
        print(f"\nFAIL (negative): the mutation `{args.mutate}` broke the "
              f"installed package and the consumer met every criterion the "
              f"prefix can break - this fixture cannot be trusted to report a "
              f"real failure")
        return 1

    if failed:
        print(f"\nFAIL: {args.package} does not meet criterion "
              f"{', '.join(sorted(failed))}. Fix the config, never the fixture "
              f"(roadmap/packaging-hardening.md PKG-3)")
        return 1
    print(f"\nPASS: {args.package} meets every criterion this host can check")
    return 0


if __name__ == "__main__":
    sys.exit(main())
