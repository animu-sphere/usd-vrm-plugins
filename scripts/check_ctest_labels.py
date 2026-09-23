#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Check the root build's CTest registrations against the OpenExec plan's P0-2.

`ost test` reports one flat total, so a workspace member whose suite silently
stops registering reads as 100% passing. `usdVrmaFileFormat` contributed zero
CTest names on every lane for months that way, behind an option-name bug
(roadmap/current.md, "`ost` cannot tell us a workspace member ran no tests").
The same flat total is why the plan asked for labels: a layer that can be run
and reported on its own is a layer whose count can be read.

This reads the registrations back from CTest itself, in listing mode, and
fails on four things:

* **A member that registers nothing.** Every `tests/CMakeLists.txt` of a
  member the root build adds must register at least one test, which is the
  local half of the per-member attribution asked of `ost` upstream.
* **A label a directory should contribute and does not.** Each of the plan's
  seven labels names the directories it comes from, and each must supply at
  least one test carrying it.
* **A test missing a directory-wide label.** Where a label means "every test
  this directory registers", a new test without it is found here rather than
  by the next person filtering on it.
* **A `motion.*` label the plan does not name**, which is how a misspelling
  would otherwise become an eighth, empty label.

CTest's listing mode still rewrites `Testing/Temporary/LastTest.log`, and this
runs inside a CTest run of the same tree. So the listing is taken from a copy
of the tree's `CTestTestfile.cmake` files, never from the build directory.
"""

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


# The plan's labels (docs/roadmap/openexec-foundation.md, P0-2) and the
# directories each must come from, relative to the source root ("." is the
# root CMakeLists.txt itself).
# `motion.core` and `motion.runtime` have no source here since MIG-1..MIG-2:
# the core and the runtime are consumed packages, and their suites run in
# `usd-motion-plugins` against the code. A label with no directory left is not
# a label this workspace can carry, so it is gone rather than left to pass
# vacuously -- what replaced it as evidence is the consumer-side suites below,
# which exercise the same values through the packages.
LABEL_SOURCES = {
    "motion.retarget": ["libs/vrmRetarget/tests"],
    "motion.cli": [
        "tools/motionRetarget/tests",
        "tools/motionCapture/tests",
        "tools/motionBvh/tests",
    ],
    "motion.integration": [".", "tests/parity"],
    "motion.openexec": [
        "plugins/execMotion/tests",
        "plugins/execVrm/tests",
        "tests/parity",
    ],
    "motion.real-corpus": [
        ".",
        "libs/motionBvh/tests",
        "tools/motionBvh/tests",
        "tests/parity",
    ],
}

# Where a label means every test the directory registers.
EVERY_TEST = {
    "motion.retarget": ["libs/vrmRetarget/tests"],
    "motion.cli": LABEL_SOURCES["motion.cli"],
    "motion.openexec": ["plugins/execMotion/tests", "plugins/execVrm/tests"],
}

# The members' test directories, at the depths the root CMakeLists.txt
# discovers members at: libraries, bundles, tools, adapters and their tools.
MEMBER_TEST_GLOBS = (
    "libs/*/tests/CMakeLists.txt",
    "plugins/*/tests/CMakeLists.txt",
    "tools/*/tests/CMakeLists.txt",
    "adapters/*/*/tests/CMakeLists.txt",
    "adapters/*/*/tools/*/tests/CMakeLists.txt",
)

_SUBDIRS = re.compile(r'^\s*subdirs\("([^"]+)"\)\s*$', re.MULTILINE)


def snapshot_test_files(build_dir, dest):
    """Copy the tree's CTestTestfile.cmake files, following `subdirs()`."""
    pending = [Path(".")]
    copied = 0
    while pending:
        rel = pending.pop()
        source = build_dir / rel / "CTestTestfile.cmake"
        if not source.is_file():
            continue
        target = dest / rel / "CTestTestfile.cmake"
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
        copied += 1
        for sub in _SUBDIRS.findall(source.read_text(encoding="utf-8")):
            sub_path = Path(sub)
            if sub_path.is_absolute():
                try:
                    sub_path = sub_path.relative_to(build_dir)
                except ValueError:
                    raise SystemExit(
                        f"check_ctest_labels: {source} names a test directory "
                        f"outside the build tree: {sub}")
                pending.append(sub_path)
            else:
                pending.append(rel / sub_path)
    if copied == 0:
        raise SystemExit(
            f"check_ctest_labels: no CTestTestfile.cmake in {build_dir}")
    return copied


def list_tests(ctest, build_dir, config):
    """Return CTest's json-v1 listing of the tree, taken from a copy."""
    with tempfile.TemporaryDirectory(prefix="ctest-labels-") as tmp:
        snapshot_test_files(Path(build_dir).resolve(), Path(tmp))
        command = [ctest, "--show-only=json-v1", "--test-dir", tmp]
        if config:
            command += ["-C", config]
        result = subprocess.run(command, capture_output=True, text=True,
                                encoding="utf-8")
        if result.returncode != 0:
            raise SystemExit(
                f"check_ctest_labels: {' '.join(command)} exited "
                f"{result.returncode}\n{result.stderr}")
        return json.loads(result.stdout)


def registering_file(listing, test):
    """The CMakeLists.txt whose directory registered `test`.

    The backtrace's outermost frame is the directory's own list file, so a
    test added through a helper in an included module is still attributed to
    the directory that included it.
    """
    graph = listing["backtraceGraph"]
    node = test.get("backtrace")
    if node is None:
        return None
    while "parent" in graph["nodes"][node]:
        node = graph["nodes"][node]["parent"]
    return graph["files"][graph["nodes"][node]["file"]]


def relative_dir(path, source_dir):
    """`path`'s directory relative to the source root, spelled with `/`.

    No `normcase`: on Windows `relpath` already matches the prefix without
    regard to case, and the remainder has to keep the case the tables spell.
    """
    rel = os.path.relpath(os.path.normpath(os.path.dirname(path)),
                          os.path.normpath(str(source_dir)))
    return rel.replace(os.sep, "/")


def collect(listing, source_dir):
    """The listing as (name, registering directory, labels) triples."""
    tests = []
    for test in listing.get("tests", []):
        labels = set()
        for prop in test.get("properties", []):
            if prop.get("name") == "LABELS":
                labels.update(prop.get("value", []))
        where = registering_file(listing, test)
        tests.append((test["name"],
                      relative_dir(where, source_dir) if where else None,
                      labels))
    return tests


def member_test_dirs(source_dir):
    """Every member test directory the source tree holds."""
    found = set()
    for pattern in MEMBER_TEST_GLOBS:
        for path in Path(source_dir).glob(pattern):
            found.add(relative_dir(str(path), source_dir))
    return sorted(found)


def check(tests, members):
    """Every finding, as one line each; an empty list is a pass."""
    findings = []
    by_dir = {}
    for name, where, labels in tests:
        by_dir.setdefault(where, []).append((name, labels))
        for label in sorted(labels):
            if label.startswith("motion.") and label not in LABEL_SOURCES:
                findings.append(
                    f"{name}: '{label}' is not one of the plan's labels")

    for member in members:
        if member not in by_dir:
            findings.append(
                f"{member}/CMakeLists.txt registers no test in this build")

    for label, dirs in LABEL_SOURCES.items():
        for where in dirs:
            if not any(label in labels for _, labels in by_dir.get(where, [])):
                findings.append(
                    f"{label}: {where} contributes no test carrying it")

    for label, dirs in EVERY_TEST.items():
        for where in dirs:
            for name, labels in by_dir.get(where, []):
                if label not in labels:
                    findings.append(
                        f"{label}: {name} ({where}) does not carry it")
    return findings


def _passing_tree():
    """One test per label source, and one per member: the smallest pass."""
    tests = []
    for label, dirs in LABEL_SOURCES.items():
        for where in dirs:
            tests.append((f"{label}@{where}", where, {label}))
    labels_by_dir = {}
    for name, where, labels in tests:
        labels_by_dir.setdefault(where, set()).update(labels)
    # A directory-wide label has to be on every test of its directory, so the
    # fixture gives each test there all of the directory's labels.
    tests = [(name, where, set(labels_by_dir[where]))
             for name, where, _ in tests]
    members = sorted({where for _, where, _ in tests
                      if where.endswith("/tests") and
                      not where.startswith("tests/")})
    return tests, members


def selftest():
    """Hold each rule to a case that must fail on exactly its own line."""
    tests, members = _passing_tree()
    failures = []

    def expect(case, tests_, members_, wanted):
        got = check(tests_, members_)
        if got != wanted:
            failures.append(f"{case}: expected {wanted}, got {got}")

    expect("the passing tree", tests, members, [])

    # A member whose suite registers nothing: the usdVrmaFileFormat shape.
    expect("a silent member", tests,
           members + ["plugins/usdVrmaFileFormat/tests"],
           ["plugins/usdVrmaFileFormat/tests/CMakeLists.txt registers no "
            "test in this build"])

    # A directory that stopped contributing a label, with its tests still
    # there and carrying another one.
    stripped = [(n, w, (l - {"motion.integration"}) if w == "." else l)
                for n, w, l in tests]
    expect("a label a directory no longer contributes", stripped, members,
           ["motion.integration: . contributes no test carrying it"])

    # A new test in a directory-wide label's directory, registered without it.
    added = tests + [("execMotion_new", "plugins/execMotion/tests", set())]
    expect("a test without its directory's label", added, members,
           ["motion.openexec: execMotion_new (plugins/execMotion/tests) "
            "does not carry it"])

    # A misspelled label: reported as unknown, and the directory it was meant
    # for reported as contributing none.
    misspelled = [(n, w, {"motion.realcorpus"} if n.startswith(
        "motion.real-corpus@libs/motionBvh") else l) for n, w, l in tests]
    expect("a misspelled label", misspelled, members,
           ["motion.real-corpus@libs/motionBvh/tests: 'motion.realcorpus' is "
            "not one of the plan's labels",
            "motion.real-corpus: libs/motionBvh/tests contributes no test "
            "carrying it"])

    # A label outside motion.* is not this check's business.
    other = tests + [("osc_boundaries", "libs/osc/tests", {"boundaries"})]
    expect("a label outside the plan's namespace", other,
           members + ["libs/osc/tests"], [])

    if failures:
        print("check_ctest_labels selftest FAILED:")
        for failure in failures:
            print(f"  {failure}")
        return 1
    print("check_ctest_labels selftest: 6 cases passed")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--ctest", default="ctest")
    parser.add_argument("--build-dir")
    parser.add_argument("--source-dir")
    parser.add_argument("--config", default="")
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()

    if args.selftest:
        return selftest()
    if not args.build_dir or not args.source_dir:
        parser.error("--build-dir and --source-dir are required")

    listing = list_tests(args.ctest, args.build_dir, args.config)
    tests = collect(listing, args.source_dir)
    members = member_test_dirs(args.source_dir)
    findings = check(tests, members)
    if findings:
        print(f"check_ctest_labels: {len(findings)} finding(s) over "
              f"{len(tests)} test(s):")
        for finding in findings:
            print(f"  {finding}")
        return 1

    print(f"check_ctest_labels: {len(tests)} test(s), "
          f"{len(members)} member suite(s), each registering at least one")
    for label in LABEL_SOURCES:
        count = sum(1 for _, _, labels in tests if label in labels)
        print(f"  {label}: {count}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
