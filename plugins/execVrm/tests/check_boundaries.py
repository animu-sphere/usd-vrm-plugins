#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Enforce execVrm's dependency boundary and the snapshot rule.

The snapshot rule (motion policy §11.4) is one rule for both exec bundles. Its
tables are in `exec_rules.py` beside this file: they were execMotion's check
until MIG-2 deleted that bundle here, and the published bundle ships no tests
to import them from.

What is execVrm's own:

  links    motionCore, motionSampling, motionRecording, motionRetarget and
           vrmRig, and nothing of vrmSchema:
           the schema is a bundle edge exec resolves by type name, and linking
           it would add a runtime dependency on a library the bundle needs
           nothing from
  source   no GLB parser, no importer model, no reparse of the source .vrm or
           .vrma bytes -- `execVrm`'s only input contract is what is on the stage
  binary   neither vrmSchema nor vrmContainer nor any importer, and not
           execMotion either, whose computation is read by name
  schemas  the Vrm*API applied schemas, UsdSkelSkeleton and UsdSkelBindingAPI,
           and none that execMotion declares -- read from the consumed
           bundle's installed plugInfo.json, which is what a session loads

Usage: check_boundaries.py <bundle-source-dir> <built-library> <link-libraries>
                           <execMotion-plugInfo.json>
"""

from __future__ import annotations

import pathlib
import re
import subprocess
import sys


sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import exec_rules as rules  # noqa: E402 -- beside this file


def main() -> int:
    if len(sys.argv) != 5:
        print(__doc__, file=sys.stderr)
        return 2
    source = pathlib.Path(sys.argv[1]).resolve()
    library = pathlib.Path(sys.argv[2]).resolve()
    motion_plug_info = pathlib.Path(sys.argv[4]).resolve()
    errors: list[str] = []

    errors += rules.purity_source_errors(source)
    errors += rules.purity_import_errors(library)
    errors += rules.link_errors(
        "execVrm", sys.argv[3],
        {"motionCore::motionCore", "motionSampling::motionSampling",
         "motionRecording::motionRecording", "motionRetarget::motionRetarget",
         "vrmRig::vrmRig"})

    declared, schema_errors = rules.schema_errors("execVrm", source)
    errors += schema_errors
    # The partition, read from the other side as well: whatever the consumed
    # execMotion's plugInfo.json declares, this one may not.
    try:
        motion_declared = rules.declared_schemas(motion_plug_info)
    except (OSError, ValueError) as exc:
        errors.append(f"could not read execMotion's {motion_plug_info}: {exc}")
        motion_declared = set()
    if not motion_declared:
        errors.append(
            f"execMotion's {motion_plug_info} declares no schema: the partition "
            f"was checked against nothing")
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
