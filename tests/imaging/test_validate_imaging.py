#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""VRM300 against real sessions: `validate_vrm.py --check-imaging` run with
vrmImaging registered, without it, with UsdImaging's external plugins switched
off, and without vrmSchema.

The rule is `usdVrmFileFormat`'s suite's (`check_imaging_rules`), measured
there with the adapters handed in. What only a composed build can show is the
other half: that the validator's discovery sees what UsdImaging would
construct from `plugInfo.json` in each of those sessions. So each case is a
fresh process with its own `PXR_PLUGINPATH_NAME`, because a plugin registry
never forgets a plugin once registered.

The environment this starts from registers all three bundles; each case takes
out what it names.
"""

from __future__ import annotations

import argparse
import json
import os
import pathlib
import subprocess
import sys
import tempfile


def run(validator: str, stage: str, env: dict[str, str], *flags: str) -> dict:
    result = subprocess.run(
        [sys.executable, validator, stage, "--json", *flags],
        env=env, capture_output=True, text=True, encoding="utf-8")
    if result.returncode not in (0, 1):
        raise SystemExit(f"validate_vrm.py exited {result.returncode} on "
                         f"{stage}:\n{result.stdout}\n{result.stderr}")
    return json.loads(result.stdout)


def without(env: dict[str, str], bundle: str) -> dict[str, str]:
    """`env` with every plugin path entry of `bundle` taken out."""
    entries = env.get("PXR_PLUGINPATH_NAME", "").split(os.pathsep)
    kept = [e for e in entries if e and bundle.lower() not in e.lower()]
    if len(kept) == len([e for e in entries if e]):
        raise SystemExit(f"the session registers no {bundle}, so a case "
                         f"without it would prove nothing")
    changed = dict(env)
    changed["PXR_PLUGINPATH_NAME"] = os.pathsep.join(kept)
    return changed


def vrm300(report: dict) -> str | None:
    found = [d for d in report["diagnostics"] if d.get("code") == "VRM300"]
    assert len(found) <= 1, found
    return found[0]["message"] if found else None


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--validator", required=True)
    parser.add_argument("--vrm", required=True,
                        help="an MToon .vrm the importer applies all three "
                             "canonical schemas to")
    args = parser.parse_args()

    session = dict(os.environ)
    session.pop("USDIMAGING_ENABLE_PLUGINS", None)
    failures = []

    def check(condition: bool, what: str) -> None:
        print(f"  {'ok  ' if condition else 'FAIL'} {what}")
        if not condition:
            failures.append(what)

    # The discovery on its own, first thing in a fresh process: nothing has
    # opened a stage yet, which is when a TfType named only by a plugInfo.json
    # is still unknown to Tf.Type.FindByName.
    tools = str(pathlib.Path(args.validator).parent)
    listed = subprocess.run(
        [sys.executable, "-c",
         "import sys; sys.path.insert(0, sys.argv[1]); import validate_vrm; "
         "print(' '.join(sorted(validate_vrm.imaging_adapter_schemas())))",
         tools], env=session, capture_output=True, text=True, check=True
    ).stdout.split()
    check({"VrmMaterialAPI", "VrmMToonAPI", "VrmTextureInfoAPI"} <= set(listed),
          f"imaging_adapter_schemas() in a fresh process lists the three "
          f"adapters (got {listed})")

    # vrmImaging in the session: every family has its adapter.
    message = vrm300(run(args.validator, args.vrm, session, "--check-imaging"))
    check(message is None, f"with vrmImaging: no VRM300 (got {message!r})")

    # Without it: raised, naming the bundle.
    no_imaging = without(session, "vrmImaging")
    message = vrm300(run(args.validator, args.vrm, no_imaging, "--check-imaging"))
    check(bool(message) and "vrmImaging is not in the session" in message,
          f"without vrmImaging: VRM300 names it (got {message!r})")
    # And only when asked.
    message = vrm300(run(args.validator, args.vrm, no_imaging))
    check(message is None, f"without --check-imaging: no VRM300 (got {message!r})")

    # UsdImaging's switch for external plugins drops the adapters silently
    # (imaging policy §27); the validator says so instead.
    switched_off = dict(session, USDIMAGING_ENABLE_PLUGINS="0")
    message = vrm300(run(args.validator, args.vrm, switched_off, "--check-imaging"))
    check(bool(message) and "USDIMAGING_ENABLE_PLUGINS is off" in message,
          f"USDIMAGING_ENABLE_PLUGINS=0: VRM300 names it (got {message!r})")

    # Without vrmSchema the importer cannot run, so the stage is the same
    # avatar flattened to `.usda` in the full session, as a deployment with no
    # VRM plugin would receive it: its schemas are authored and unregistered,
    # and UsdImaging never asks an adapter about an unregistered schema.
    with tempfile.TemporaryDirectory() as scratch:
        flat = str(pathlib.Path(scratch) / "flattened.usda")
        subprocess.run(
            [sys.executable, "-c",
             "import sys; from pxr import Usd; "
             "Usd.Stage.Open(sys.argv[1]).Flatten().Export(sys.argv[2])",
             args.vrm, flat], env=session, check=True)
        no_schema = without(session, "vrmSchema")
        message = vrm300(run(args.validator, flat, no_schema, "--check-imaging"))
        check(bool(message) and "not a registered schema" in message,
              f"without vrmSchema: VRM300 names it (got {message!r})")
        message = vrm300(run(args.validator, flat, session, "--check-imaging"))
        check(message is None, f"the same stage with vrmSchema: no VRM300 "
                               f"(got {message!r})")

    if failures:
        print(f"workspace_validate_imaging: {len(failures)} failed")
        return 1
    print("workspace_validate_imaging: passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
