# SPDX-License-Identifier: Apache-2.0
"""Negative corpus: pin the importer's diagnostic contract on malformed input.

Drives the freshly built plugin over the deliberately-broken avatars declared in
`tests/corpus/generated/negative-manifest.json` (authored by
`tools/generate_negative.py`). This test is **manifest-driven**: each fixture
names the single diagnostic it must provoke and the mechanism by which the
importer must surface it, and the test asserts exactly that — so a regression
that silences, miscodes, or crashes on a malformed input fails here.

Two mechanisms (see the manifest's `mechanisms` block):

- ``read-fatal``   — `Usd.Stage.Open` must fail and the raised error must carry
                     the code (an unreadable container never yields a stage).
- ``import-warning`` — the stage opens and the code is the *only* diagnostic on
                       `/Asset.customData.vrm:warnings`.

Run by hand inside the runtime + plugin env:
    ost plugin run plugins/usdVrm -- python tests/test_usdvrm_negative.py
"""
import json
import os
import pathlib
import re
import sys

from pxr import Plug, Sdf, Tf, Usd

HERE = pathlib.Path(__file__).parent
GENERATED = HERE / "corpus" / "generated"
MANIFEST = GENERATED / "negative-manifest.json"
sys.path.insert(0, str(HERE.parent / "tools"))
import vrm_diagnostics as diag  # noqa: E402

_CODE_RE = re.compile(r"\[(VRM\d+)\]")


def load_manifest() -> dict:
    with open(MANIFEST, encoding="utf-8") as fh:
        return json.load(fh)


def _codes(text: str) -> list[str]:
    return _CODE_RE.findall(text or "")


def _open(path: pathlib.Path):
    """Open a stage, returning (stage_or_None, raised_error_text)."""
    try:
        stage = Usd.Stage.Open(str(path))
    except Tf.ErrorException as exc:
        return None, str(exc)
    return stage, ""


def check_read_fatal(path: pathlib.Path, code: str) -> None:
    stage, err = _open(path)
    assert stage is None, \
        f"{path.name}: expected the container to be rejected, but a stage opened"
    assert code in _codes(err), \
        f"{path.name}: raised error did not carry [{code}]: {err!r}"


def check_import_warning(path: pathlib.Path, code: str) -> None:
    stage, err = _open(path)
    assert stage is not None, f"{path.name}: expected a stage, got a read error: {err!r}"
    dp = stage.GetDefaultPrim()
    assert dp and dp.IsValid(), f"{path.name}: no default prim"
    vrm = dp.GetCustomData().get("vrm", {})
    warnings = [str(w) for w in (vrm.get("warnings") or [])]
    observed = sorted({c for w in warnings for c in _codes(w)})
    assert observed == [code], \
        f"{path.name}: expected exactly [{code}] on vrm:warnings, got {observed} " \
        f"(warnings={warnings})"


def check_fixture(fx: dict) -> None:
    code = fx["code"]
    mechanism = fx["mechanism"]
    spec = diag.CATALOG.get(code)
    assert spec is not None, f"{fx['id']}: unknown diagnostic code {code}"
    # The declared mechanism and the catalog severity must agree: a container
    # that fails to open is FATAL; a warning that still loads is not.
    if mechanism == "read-fatal":
        assert spec.severity == diag.Severity.FATAL, \
            f"{fx['id']}: read-fatal fixture pins non-FATAL code {code}"
    elif mechanism == "import-warning":
        assert spec.severity < diag.Severity.FATAL, \
            f"{fx['id']}: import-warning fixture pins FATAL code {code}"
    else:
        raise AssertionError(f"{fx['id']}: unknown mechanism {mechanism!r}")

    path = GENERATED / fx["file"]
    assert path.exists(), f"missing negative fixture: {path} " \
        "(run tools/generate_negative.py)"

    if mechanism == "read-fatal":
        check_read_fatal(path, code)
    else:
        check_import_warning(path, code)
    print(f"  {fx['id']}: OK ({mechanism} -> {code})")


def main() -> int:
    plugin_path = os.environ.get("PXR_PLUGINPATH_NAME")
    if plugin_path:
        Plug.Registry().RegisterPlugins(plugin_path.split(os.pathsep))
    assert Sdf.FileFormat.FindByExtension("vrm"), \
        "usdVrm SdfFileFormat is not registered"

    manifest = load_manifest()
    fixtures = manifest["fixtures"]
    assert fixtures, "negative manifest declares no fixtures"
    for fx in fixtures:
        check_fixture(fx)
    print(f"usdVrm negative corpus: OK ({len(fixtures)} fixture(s))")
    return 0


if __name__ == "__main__":
    sys.exit(main())
