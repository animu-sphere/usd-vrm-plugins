#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Enforce execVrm's dependency boundary and the snapshot rule.

The snapshot rule (motion policy §11.4) is one rule for both exec bundles, so
its tables live in execMotion's check and are imported from there rather than
restated: two copies of a list of forbidden clocks are two lists that drift.
Reaching into execMotion's tree follows the one edge WORKSPACE.md §2 allows
between the bundles (execVrm -> execMotion, runtime only), never the reverse.

What is execVrm's own:

  links    motionCore, motionRuntime and vrmRetarget, and nothing of vrmSchema:
           the schema is a bundle edge exec resolves by type name, and linking
           it would add a runtime dependency on a library the bundle needs
           nothing from
  source   no GLB parser, no importer model, no reparse of the source .vrm or
           .vrma bytes -- `execVrm`'s only input contract is what is on the stage
  binary   neither vrmSchema nor vrmContainer nor any importer, and not
           execMotion either, whose computation is read by name
  schemas  the Vrm*API applied schemas, UsdSkelSkeleton and UsdSkelBindingAPI,
           and none that execMotion declares

Usage: check_boundaries.py <bundle-source-dir> <built-library> <link-libraries>
"""

from __future__ import annotations

import pathlib
import re
import subprocess
import sys


def _exec_motion_rules(source: pathlib.Path):
    tests = source.parent / "execMotion" / "tests"
    if not (tests / "check_boundaries.py").is_file():
        raise RuntimeError(
            f"execMotion's check is not at {tests}: the snapshot rule's tables "
            f"live there, and this check will not pass without them")
    sys.path.insert(0, str(tests))
    import check_boundaries as rules  # noqa: E402 -- execMotion's, by path
    return rules


def main() -> int:
    if len(sys.argv) != 4:
        print(__doc__, file=sys.stderr)
        return 2
    source = pathlib.Path(sys.argv[1]).resolve()
    library = pathlib.Path(sys.argv[2]).resolve()
    errors: list[str] = []

    try:
        rules = _exec_motion_rules(source)
    except RuntimeError as exc:
        print(exc, file=sys.stderr)
        return 1

    errors += rules.purity_source_errors(source)
    errors += rules.purity_import_errors(library)
    errors += rules.link_errors(
        "execVrm", sys.argv[3],
        {"motionCore::motionCore", "motionRuntime::motionRuntime",
         "vrmRetarget::vrmRetarget"})

    declared, schema_errors = rules.schema_errors("execVrm", source)
    errors += schema_errors
    # The partition, read from the other side as well: whatever execMotion's
    # plugInfo.json declares today, this one may not.
    motion_source = source.parent / "execMotion"
    try:
        motion_declared = rules.declared_schemas(
            motion_source / "plugin" / "resources" / "execMotion" / "plugInfo.json.in")
    except (OSError, ValueError) as exc:
        errors.append(f"could not read execMotion's plugInfo.json.in: {exc}")
        motion_declared = set()
    for schema in sorted(declared & motion_declared):
        errors.append(
            f"execVrm and execMotion both declare {schema}: one of them loses "
            f"every computation it registered there")

    # The stage is the only input contract (WORKSPACE.md §2, openexec plan §3):
    # no parser of the source bytes, no importer model, no schema class -- the
    # schema is reached by type name, so its generated header has no reader here.
    forbidden_neighbours = re.compile(
        r"\b(?:vrmContainer|cgltf\w*|usdVrmFileFormat|usdVrmaFileFormat|"
        r"UsdVrmFileFormat\w*|UsdVrmaFileFormat\w*|mocopi|vrchat|ardy|"
        r"liveTransport|motionTracking|vrmAdapter\w*)\b|\bosc::|\bosc/|"
        r"#\s*include\s*[<\"](?:vrmSchema|usdVrmFileFormat|usdVrmaFileFormat|"
        r"vrmContainer|execMotion)/|\bGlb\w*|\bgltf\w*",
        re.IGNORECASE)
    for path in rules.source_files(source):
        code = rules.code_only(path.read_text(encoding="utf-8"))
        if forbidden_neighbours.search(code):
            errors.append(f"forbidden dependency direction: {path}")

    try:
        dependencies = rules.binary_dependencies(library)
    except (OSError, RuntimeError, subprocess.CalledProcessError) as exc:
        errors.append(f"could not inspect execVrm dependencies: {exc}")
        dependencies = ""
    if re.search(r"vrmSchema|vrmContainer|UsdVrm|ExecMotion|liveTransport|"
                 r"motionTracking|vrmAdapter|(?:^|[\s/\\])(?:lib)?osc[._]",
                 dependencies, re.IGNORECASE | re.MULTILINE):
        errors.append(
            "execVrm binary imports vrmSchema, an importer, execMotion, or a "
            "live library")

    if errors:
        if hasattr(sys.stderr, "reconfigure"):
            sys.stderr.reconfigure(errors="replace")
        print("\n".join(errors), file=sys.stderr)
        return 1
    print("execVrm boundary check passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
