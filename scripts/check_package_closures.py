#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Criterion 6: do Windows, macOS and Linux agree about the package closure?

PACKAGE_CONTRACT.md section 5 has six acceptance criteria, and
`scripts/check_package_consumer.py` answers five of them. The sixth cannot be
answered by any one host, so that driver records the closure it measured and
says so:

    6. n/a  one platform (Windows) cannot answer whether three agree;
            PKG-4's lane compares the closure recorded here

This is where that comparison happens. It reads the JSON reports the driver
wrote on each of the three platforms and decides whether the closures agree, or
whether a difference is one the contract documents.

    python scripts/check_package_closures.py reports-windows reports-macos
        reports-linux --json criterion-6.json

**Not everything in a closure is this workspace's to promise.** An entry is one
of three things, and each takes a different rule (PACKAGE_CONTRACT.md 5.1):

*A workspace target* -- `motionCore::motionCore`. This workspace produces it, a
config file in this repository is what brings it in, and that file is the same
file on every platform. It must be there on all three or on none, and a
difference is a defect with no qualifier available.

*A declared platform dependency* -- `ws2_32`, `Threads::Threads`. The contract's
`Platform deps` cell names it and says where it applies, so the rule is the
cell's: present exactly on the platforms named, absent on the others. That cell
is the one documented platform difference in the document, and checking it in
both directions is how issue #113's raw-library half closes -- on a POSIX host
`vrmAdapterMocopi` must carry the threading library and no socket one, which is
a measurement no Windows run can make (packaging-hardening.md PKG-5).

*Everything else* -- `arch`, `gf`, `Dbghelp.lib`, `TBB::tbb`. These arrive from
a required package this workspace does not produce, and the three runtimes are
three different builds of OpenUSD: comparing their link lines entry for entry
would report a difference between two upstream builds as a defect in a config
file here. So they are attributed rather than compared, and the attribution is
what gets checked -- **a package whose contract closure reaches no external
required package must carry none of them at all**, which is a strong statement
about exactly the four rows where it can be strong (`osc`, `vrmContainer`,
`liveTransport`, `vrmAdapterVrchatOsc`).

Two rules apply to every entry whatever its class, because each is a package
exporting its build rather than its interface: an absolute path, and a workspace
identity spelled as anything but its exported target.

Exit 0 the three agree, 1 they do not, 2 the reports cannot answer the question
-- a platform missing, a package missing, or a report whose own criteria 1-5
were not met. The third is a setup answer rather than a verdict, on the same
rule the driver uses: a check that did not run is not a check that said no.
"""
from __future__ import annotations

import argparse
import json
import pathlib
import re
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

# The contract is the input, not a copy of it -- and the parser for it lives in
# the driver that writes the reports this script reads. A second parser here
# would be a second reading of the same table, which is the drift this whole
# track exists to stop.
import check_package_consumer as driver  # noqa: E402

# `platform.system()`, which is what the driver records. Darwin is macOS; the
# contract spells it the way a reader does, and this script says both.
SYSTEMS = ("Windows", "Darwin", "Linux")
DISPLAY = {"Windows": "Windows", "Darwin": "macOS", "Linux": "Linux"}

# How a `Platform deps` cell qualifies a token, as the set of systems it names.
# `ws2_32` (Windows), `Threads::Threads` (elsewhere) -- and an unqualified token
# applies everywhere.
QUALIFICATIONS = {
    "windows": {"Windows"},
    "elsewhere": {"Darwin", "Linux"},
    "non-windows": {"Darwin", "Linux"},
    "macos": {"Darwin"},
    "linux": {"Linux"},
}

GENEX = re.compile(r"^\$<LINK_ONLY:(.+)>$")
ABSOLUTE = re.compile(r"^(?:[A-Za-z]:[\\/]|[\\/])")


def fail_setup(msg: str) -> int:
    print(f"SETUP: {msg}")
    return 2


def unwrap(entry: str) -> str:
    """`$<LINK_ONLY:Threads::Threads>` is how CMake records a link that does not
    propagate usage requirements, and it is the spelling the POSIX side of the
    one documented platform difference actually arrives in. Comparing the
    wrapper rather than what it wraps would report `Threads::Threads` missing on
    every platform that has it."""
    m = GENEX.match(entry.strip())
    return m.group(1).strip() if m else entry.strip()


def platform_dependencies(package: str, rows: dict) -> dict:
    """The `Platform deps` cell as {token: the systems it applies to}.

    ``inherited from `liveTransport``` is resolved rather than skipped: three
    adapter rows say it, and it is the cell that carries issue #113's
    raw-library half into every one of them."""
    cell = rows[package]["platform"]
    if cell in ("-", "—", ""):
        return {}
    inherited = re.match(r"inherited from `([^`]+)`", cell)
    if inherited:
        source = inherited.group(1)
        if source not in rows:
            raise KeyError(source)
        return platform_dependencies(source, rows)
    deps: dict = {}
    for m in re.finditer(r"`([^`]+)`\s*(?:\(([^)]*)\))?", cell):
        token, qualification = m.group(1), (m.group(2) or "").strip().lower()
        if not qualification:
            deps[token] = set(SYSTEMS)
        elif qualification in QUALIFICATIONS:
            deps[token] = set(QUALIFICATIONS[qualification])
        else:
            # Unreadable is not the same as absent. A cell this check cannot
            # parse stops the run, because treating the token as unqualified
            # would assert it on three platforms out of a sentence nobody read.
            deps[token] = None
    return deps


def load(paths: list) -> tuple:
    """Every report under the given files and directories, keyed by package and
    system. A duplicate that disagrees with itself is a setup error rather than
    a difference between platforms."""
    reports: dict = {}
    problems: list = []
    files: list = []
    for raw in paths:
        path = pathlib.Path(raw)
        if path.is_dir():
            files += sorted(path.rglob("*.json"))
        elif path.is_file():
            files.append(path)
        else:
            problems.append(f"{path} is neither a file nor a directory")
    for path in files:
        try:
            report = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, ValueError) as exc:
            problems.append(f"{path} does not parse: {exc}")
            continue
        if not isinstance(report, dict) or "package" not in report:
            # The lane's artifacts hold the driver's reports and whatever else
            # a future step writes beside them; a file that is not one is
            # skipped rather than guessed at.
            continue
        if report.get("mutation"):
            problems.append(
                f"{path} is a mutation run of `{report['package']}` "
                f"(`--mutate {report['mutation']}`), whose closure was measured "
                f"against a prefix broken on purpose")
            continue
        system = report.get("host", {}).get("system", "")
        if system not in SYSTEMS:
            problems.append(f"{path} records host system `{system}`, which is "
                            f"none of {', '.join(SYSTEMS)}")
            continue
        key = (report["package"], system)
        if key in reports and reports[key]["closure"] != report["closure"]:
            problems.append(
                f"two reports for `{report['package']}` on {DISPLAY[system]} "
                f"disagree about the closure, so neither can be compared")
            continue
        reports[key] = report
    return reports, problems


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("reports", nargs="+",
                    help="JSON reports from check_package_consumer.py, or "
                         "directories holding them")
    ap.add_argument("--json", metavar="PATH", help="write the comparison here")
    args = ap.parse_args()

    rows = driver.contract_rows()
    # The packages criterion 6 is about: the twelve with a `find_package`
    # contract. A row that exports no target carries a plugin-load contract
    # instead (PACKAGE_CONTRACT.md 4.1) and has no closure to compare.
    packages = [name for name, row in rows.items()
                if "reserved" not in (row["target"], row["product"])
                and row["target"] not in ("-", "—")]
    if not packages:
        return fail_setup("PACKAGE_CONTRACT.md section 4 names no package with "
                          "a find_package contract")
    targets = {rows[p]["target"] for p in packages}
    identities = set(rows)

    reports, problems = load(args.reports)
    for package in packages:
        for system in SYSTEMS:
            report = reports.get((package, system))
            if report is None:
                problems.append(
                    f"no report for `{package}` on {DISPLAY[system]}, so this "
                    f"run cannot say whether three platforms agree about it")
                continue
            unmet = sorted(
                n for n in ("1", "2", "3", "4", "5")
                if report["criteria"].get(n, {}).get("met") is not True)
            if unmet:
                problems.append(
                    f"`{package}` on {DISPLAY[system]} did not meet criterion "
                    f"{', '.join(unmet)}, so its closure is not a measurement "
                    f"of a package that met its contract")
    if problems:
        for problem in problems:
            print(f"  - {problem}")
        return fail_setup(f"{len(problems)} report(s) cannot answer criterion 6")

    result: dict = {"systems": list(SYSTEMS), "packages": {}}
    failures: list = []

    for package in packages:
        closures = {s: [unwrap(e) for e in reports[(package, s)]["closure"]]
                    for s in SYSTEMS}
        try:
            platform_deps = platform_dependencies(package, rows)
        except KeyError as exc:
            return fail_setup(
                f"`{package}`'s Platform deps cell inherits from {exc}, which "
                f"has no row in PACKAGE_CONTRACT.md section 4")
        for token, systems in platform_deps.items():
            if systems is None:
                return fail_setup(
                    f"`{package}`'s Platform deps cell qualifies `{token}` "
                    f"with a phrase this check cannot read; the ones it knows "
                    f"are {', '.join(sorted(QUALIFICATIONS))}")

        external_reachable = any(
            dep in driver.EXTERNAL_PACKAGES
            for name in [package] + driver.workspace_closure(package, rows)
            for dep in driver.required_packages(rows[name]))

        classified: dict = {}
        for system in SYSTEMS:
            workspace, declared, external = set(), set(), set()
            for entry in closures[system]:
                if entry in targets:
                    workspace.add(entry)
                    continue
                if entry in platform_deps:
                    declared.add(entry)
                    continue
                external.add(entry)
                # A path or a raw identity is how a package exports its build
                # tree instead of its interface, and neither is excused by
                # having arrived from somewhere else in the closure.
                if ABSOLUTE.match(entry):
                    failures.append(
                        f"`{package}` on {DISPLAY[system]} carries the absolute "
                        f"path `{entry}` on its link closure; an installed "
                        f"package names targets and libraries, not paths on the "
                        f"machine that built it")
                elif entry in identities or re.sub(
                        r"^lib|\.(a|lib|so|dylib)$", "", entry) in identities:
                    failures.append(
                        f"`{package}` on {DISPLAY[system]} names the workspace "
                        f"identity `{entry}` as something other than an "
                        f"exported target")
            classified[system] = {
                "workspace": sorted(workspace),
                "platform": sorted(declared),
                "external": sorted(external),
            }
            if external and not external_reachable:
                failures.append(
                    f"`{package}` on {DISPLAY[system]} carries "
                    f"{', '.join(sorted(external))} on its link closure, and "
                    f"nothing in its contract closure requires a package this "
                    f"workspace does not produce -- so no declared edge "
                    f"explains it")

        # The workspace half, compared with no qualifier available to it.
        if len({frozenset(classified[s]["workspace"]) for s in SYSTEMS}) != 1:
            for system in SYSTEMS:
                elsewhere = [set(classified[s]["workspace"])
                             for s in SYSTEMS if s != system]
                only = sorted(set(classified[system]["workspace"])
                              - set.union(*elsewhere))
                missing = sorted(set.intersection(*elsewhere)
                                 - set(classified[system]["workspace"]))
                if only:
                    failures.append(
                        f"`{package}` carries {', '.join(only)} on "
                        f"{DISPLAY[system]} alone; a workspace package arrives "
                        f"through a config file, which is the same file on "
                        f"every platform")
                if missing:
                    failures.append(
                        f"`{package}` carries {', '.join(missing)} everywhere "
                        f"but {DISPLAY[system]}")

        # The declared platform dependencies, against the cell that declares
        # them. This is the one difference the contract permits, which is why it
        # is also the one checked in both directions: PKG-5 is the absence half.
        for token, systems in platform_deps.items():
            for system in SYSTEMS:
                present = token in classified[system]["platform"]
                if system in systems and not present:
                    failures.append(
                        f"`{package}` does not carry `{token}` on "
                        f"{DISPLAY[system]}, where PACKAGE_CONTRACT.md's "
                        f"Platform deps cell says it applies")
                if system not in systems and present:
                    failures.append(
                        f"`{package}` carries `{token}` on {DISPLAY[system]}, "
                        f"where PACKAGE_CONTRACT.md's Platform deps cell says "
                        f"it does not")

        result["packages"][package] = {
            "external_reachable": external_reachable,
            "platform_deps": {t: sorted(s) for t, s in platform_deps.items()},
            "by_system": classified,
        }

    # --- the answer ---------------------------------------------------------
    print("PACKAGE_CONTRACT.md section 5 criterion 6 - "
          + ", ".join(DISPLAY[s] for s in SYSTEMS))
    print()
    for package in packages:
        entry = result["packages"][package]
        first = entry["by_system"][SYSTEMS[0]]
        external = {s: set(entry["by_system"][s]["external"]) for s in SYSTEMS}
        differing = sorted(set().union(*external.values())
                           - set.intersection(*external.values()))
        print(f"  {package}")
        print(f"    workspace: {', '.join(first['workspace']) or 'none'}")
        for token, systems in sorted(entry["platform_deps"].items()):
            print(f"    platform:  {token} on "
                  f"{', '.join(DISPLAY[s] for s in SYSTEMS if s in systems)}")
        if entry["external_reachable"]:
            counts = ", ".join(f"{DISPLAY[s]} {len(external[s])}"
                               for s in SYSTEMS)
            print(f"    external:  {counts}"
                  + (f"; differing: {', '.join(differing)}" if differing
                     else "; identical"))
        else:
            print("    external:  none, and none is permitted")

    if args.json:
        pathlib.Path(args.json).write_text(
            json.dumps(result, indent=2, sort_keys=True) + "\n",
            encoding="utf-8")
        print(f"\n  report: {args.json}")

    if failures:
        print(f"\nFAIL: criterion 6 is not met, by {len(failures)} difference"
              f"{'' if len(failures) == 1 else 's'}:")
        for failure in failures:
            print(f"  - {failure}")
        print("\nA difference is a defect until PACKAGE_CONTRACT.md documents "
              "it (roadmap/packaging-hardening.md PKG-4). Fix the config file "
              "or the contract cell, never this check.")
        return 1

    print(f"\nPASS: {len(packages)} packages, three platforms, criterion 6 met")
    return 0


if __name__ == "__main__":
    sys.exit(main())
