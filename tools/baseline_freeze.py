#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Phase 0 baseline freeze: capture and verify the pre-split behavior record.

The workspace split (docs/architecture/WORKSPACE.md §7) requires the current
importer/resolver/schema behavior to be frozen as committed evidence before any
code moves. This tool owns that evidence, under tests/baseline/:

  usda/               flattened, normalized USDA snapshot per local fixture
  digests/            structural digest JSON per fixture (incl. corpus models):
                      prim topology, applied schemas, material/skeleton
                      bindings, animation samples, asset resolution, diagnostics
  schema_contract.json  schema types/properties/tokens (generatedSchema.usda)
  discovery.json      registered USD types + file-format probe
  diagnostics.json    VRMxxx code catalog + corpus/negative expected codes
  symbols/            exported native symbols across all bundle libraries

The frozen views are deliberately bundle-set invariant: discovery freezes the
*union* of registered types across every workspace bundle (no per-plugin
attribution) and symbols freeze the union of exported names across every
bundle library. A migration PR that moves a registration or a symbol between
bundles therefore keeps these artifacts byte-identical; per-bundle attribution
is owned by each bundle's own discovery tests (WORKSPACE.md §6 invariant 5).

Modes:
  --check   (default) regenerate everything in memory and compare against the
            committed baseline; any difference is a migration regression.
  --update  rewrite the committed baseline from current behavior. Only valid
            when behavior is *supposed* to change (never in a structural PR).

Run inside a runtime session in which every workspace bundle is discoverable
(add `--with plugins/<bundle>` for each additional bundle once the split
begins):

    ost plugin run plugins/usdVrm -- python tools/baseline_freeze.py --check
    ost plugin run plugins/usdVrm -- python tools/baseline_freeze.py --update

The USDA/digest artifacts are machine-independent: absolute paths under the
file-format bundle are rewritten to ${BUNDLE}, the remaining absolute repo
paths to ${REPO}, and the flatten doc header is stripped. ${BUNDLE} also makes
them bundle-*path* invariant: fixtures and corpus models live inside the
bundle, so without it a bundle directory rename would rewrite every asset path
and be indistinguishable from a behavior regression. The bundle is located by
manifest kind, never by directory name. Symbol baselines are per-platform;
--check only compares the file for the current platform and skips (does not
fail) when the platform has no committed baseline.
"""

from __future__ import annotations

import argparse
import difflib
import hashlib
import json
import pathlib
import re
import shutil
import subprocess
import sys

REPO = pathlib.Path(__file__).resolve().parents[1]
PLUGINS = REPO / "plugins"
BASELINE = REPO / "tests" / "baseline"

_KIND_RE = re.compile(r"^\s*kind:\s*(\S+)\s*$", re.M)


def _bundle_by_kind(kind: str) -> pathlib.Path:
    """Bundle root of the one manifest declaring `kind`. Looked up rather than
    named so a bundle directory rename moves no baseline bytes."""
    hits = [m.parent for m in sorted(PLUGINS.glob("*/openstrata.plugin.yaml"))
            if (k := _KIND_RE.search(m.read_text("utf-8")))
            and k.group(1) == kind]
    if len(hits) != 1:
        raise RuntimeError(f"expected exactly one {kind} bundle under "
                           f"{PLUGINS}, found {[str(h) for h in hits]}")
    return hits[0]


BUNDLE = _bundle_by_kind("usd-fileformat")
FIXTURES = BUNDLE / "tests" / "fixtures"
CORPUS = BUNDLE / "tests" / "corpus"
TOOLS = BUNDLE / "tools"

BASELINE_SCHEMA_VERSION = 1

# Schema APIs owned by the future vrmSchema bundle (WORKSPACE.md §1).
SCHEMA_APIS = [
    "VrmColliderAPI",
    "VrmConstraintAPI",
    "VrmExpressionAPI",
    "VrmHumanoidAPI",
    "VrmLookAtAPI",
    "VrmSpringBoneAPI",
]

SECTIONS = ("usda", "digests", "schema", "discovery", "diagnostics", "symbols")

_CODE_RE = re.compile(r"VRM\d{3}")


# ---------------------------------------------------------------------------
# Normalization helpers
# ---------------------------------------------------------------------------

def _path_variants(root: pathlib.Path) -> list[str]:
    """Spellings of an absolute path that can leak into generated text."""
    fwd = root.as_posix()
    variants = [fwd, str(root), str(root).replace("\\", "\\\\")]
    # Windows drive letters show up in either case depending on the producer.
    if len(fwd) > 1 and fwd[1] == ":":
        variants += [v[0].swapcase() + v[1:] for v in list(variants)]
    # Longest first so escaped-backslash forms win over the plain form.
    return sorted(set(variants), key=len, reverse=True)


def _scrub(text: str) -> str:
    # Bundle root before repo root: every bundle spelling starts with a repo
    # spelling, so scrubbing ${REPO} first would strand the bundle directory
    # name in the artifact and re-bake it on the next rename.
    for v in _path_variants(BUNDLE):
        text = text.replace(v, "${BUNDLE}")
    for v in _path_variants(REPO):
        text = text.replace(v, "${REPO}")
    return text


def _normalize_usda(text: str) -> str:
    text = text.replace("\r\n", "\n")
    # Drop the stage-metadata doc block: it embeds the absolute source path
    # ("Generated from Composed Stage of root layer C:\...").
    text = re.sub(r'[ \t]*doc = """.*?"""\n', "", text, count=1, flags=re.S)
    return _scrub(text)


def _round(x: float) -> float:
    return float("%.9g" % float(x))


def _fmt6(x: float) -> str:
    return "%.6g" % float(x)


def _array_hash(values, per_item) -> str:
    h = hashlib.sha256()
    for v in values:
        h.update(per_item(v).encode("ascii"))
        h.update(b" ")
    return h.hexdigest()


def _hash_float_array(values) -> str:
    """Order-preserving digest of a float/vec/matrix array at 1e-6 precision."""
    def item(v):
        try:
            return " ".join(_fmt6(c) for c in v)
        except TypeError:
            return _fmt6(v)
    # Matrices iterate as rows of vecs; flatten one more level if needed.
    def item2(v):
        try:
            return " ".join(item(r) for r in v)
        except TypeError:
            return item(v)
    return _array_hash(values, item2)


def _sha256_text(text: str) -> str:
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


def _jsonable(value):
    """Normalize an authored USD value into deterministic JSON."""
    from pxr import Gf, Sdf, Vt  # noqa: F401  (Vt used via duck typing)

    if isinstance(value, Sdf.AssetPath):
        return {"asset": _scrub(value.path.replace("\\", "/")),
                "resolved": bool(value.resolvedPath)}
    if isinstance(value, float):
        return _round(value)
    if isinstance(value, (int, bool, str)) or value is None:
        return value
    if isinstance(value, (Gf.Vec2f, Gf.Vec3f, Gf.Vec4f, Gf.Vec2d, Gf.Vec3d,
                          Gf.Vec4d, Gf.Quatf, Gf.Quatd)):
        if hasattr(value, "real"):
            comps = [value.real, *value.imaginary]
        else:
            comps = list(value)
        return [_round(c) for c in comps]
    if isinstance(value, (Gf.Matrix4d, Gf.Matrix3d)):
        return {"matrixSha256": _hash_float_array([value])}
    # Arrays: keep small ones verbatim, hash big ones.
    try:
        n = len(value)
    except TypeError:
        return str(value)
    items = list(value)
    if n <= 16:
        return [_jsonable(v) for v in items]
    return {"len": n, "sha256": _hash_float_array(items)
            if items and not isinstance(items[0], str)
            else _array_hash(items, str)}


# ---------------------------------------------------------------------------
# Workspace bundle enumeration
# ---------------------------------------------------------------------------

_PLUG_INFO_RE = re.compile(r"^\s*plug_info:\s*(\S+)\s*$", re.M)


def _bundle_plug_infos() -> list[pathlib.Path]:
    """plugInfo.json path for every workspace bundle manifest that declares a
    USD registration, in bundle-name order. The manifests are the source of
    truth for the bundle set (WORKSPACE.md §2), so the frozen views stay
    correct as bundles are added by the split."""
    out = []
    for manifest in sorted(PLUGINS.glob("*/openstrata.plugin.yaml")):
        m = _PLUG_INFO_RE.search(manifest.read_text("utf-8"))
        if not m:
            continue
        out.append(manifest.parent / m.group(1))
    if not out:
        raise RuntimeError(f"no bundle manifests under {PLUGINS}")
    return out


def _registry_plugin_names() -> list[str]:
    """USD Plug registry names declared by the workspace bundles' plugInfo
    files (a registry name is independent of the bundle id)."""
    names = []
    for plug_info in _bundle_plug_infos():
        if not plug_info.exists():
            raise RuntimeError(f"manifest-declared plugInfo missing: {plug_info}")
        data = json.loads(plug_info.read_text("utf-8"))
        for entry in data.get("Plugins", []):
            names.append(entry["Name"])
    return names


def _generated_schema_path() -> pathlib.Path:
    """The single generatedSchema.usda shipped by the workspace (schema
    ownership moves between bundles during the split; content must not)."""
    found = sorted(PLUGINS.glob("*/plugin/resources/*/generatedSchema.usda"))
    if len(found) != 1:
        raise RuntimeError(
            "expected exactly one committed generatedSchema.usda under "
            f"plugins/*/plugin/resources/, found {len(found)}: "
            f"{[str(p) for p in found]}")
    return found[0]


# ---------------------------------------------------------------------------
# Fixture enumeration
# ---------------------------------------------------------------------------

def _negative_manifest() -> dict:
    return json.loads(
        (CORPUS / "generated" / "negative-manifest.json").read_text("utf-8"))


def _corpus_manifest() -> dict:
    return json.loads((CORPUS / "manifest.json").read_text("utf-8"))


def _fixture_inputs():
    """Yield (group, name, path) for every baseline-relevant .vrm input."""
    for f in sorted(FIXTURES.glob("*.vrm")):
        yield "fixtures", f.stem, f
    for fx in _negative_manifest()["fixtures"]:
        p = CORPUS / "generated" / fx["file"]
        yield "malformed", p.stem, p
    for model in _corpus_manifest()["models"]:
        if model.get("storage") != "vendored":
            continue  # fetch-gated models are not reproducible from checkout
        yield "corpus", model["id"], CORPUS / model["file"]


# ---------------------------------------------------------------------------
# Generators — each returns {relative_path: text_content}
# ---------------------------------------------------------------------------

def gen_usda() -> dict[str, str]:
    from pxr import Usd

    out = {}
    for group, name, path in _fixture_inputs():
        if group == "corpus":
            continue  # too large to freeze as text; digests cover them
        try:
            stage = Usd.Stage.Open(str(path))
        except Exception:
            stage = None
        if not stage:
            continue  # read-fatal negatives are frozen in digests/diagnostics
        layer = stage.Flatten()
        out[f"usda/{group}/{name}.usda"] = _normalize_usda(layer.ExportToString())
    return out


def _digest_stage(stage) -> dict:
    from pxr import Sdf, Usd, UsdGeom, UsdShade, UsdSkel

    d: dict = {}
    dp = stage.GetDefaultPrim()
    d["defaultPrim"] = dp.GetPath().pathString if dp else None
    d["upAxis"] = str(UsdGeom.GetStageUpAxis(stage))
    d["metersPerUnit"] = _round(UsdGeom.GetStageMetersPerUnit(stage))
    d["framesPerSecond"] = _round(stage.GetFramesPerSecond())
    if stage.HasAuthoredTimeCodeRange():
        d["timeCodeRange"] = [_round(stage.GetStartTimeCode()),
                              _round(stage.GetEndTimeCode())]

    vrm = (dp.GetCustomData().get("vrm", {}) if dp else {}) or {}
    vsum = {}
    for key in ("sourceFormat", "sourceVersion", "specVersion",
                "schemaContractVersion", "sourceFrontAxis",
                "frontAxisNormalized"):
        if key in vrm:
            vsum[key] = vrm[key]
    if isinstance(vrm.get("meta"), str):
        vsum["metaSha256"] = _sha256_text(vrm["meta"])
        try:
            vsum["metaName"] = json.loads(vrm["meta"]).get("name")
        except ValueError:
            pass
    if isinstance(vrm.get("rawExtension"), str):
        vsum["rawExtensionSha256"] = _sha256_text(vrm["rawExtension"])
    warnings = [str(w) for w in (vrm.get("warnings") or [])]
    vsum["warningCodes"] = sorted(
        {c for w in warnings for c in _CODE_RE.findall(w)})
    vsum["warningCount"] = len(warnings)
    d["vrm"] = vsum

    prims = []
    meshes = {}
    skeletons = {}
    materials = {}
    assets = []
    time_sampled = {}

    for prim in stage.Traverse():
        pth = prim.GetPath().pathString
        entry = {"path": pth, "type": prim.GetTypeName()}
        applied = list(prim.GetAppliedSchemas())
        if applied:
            entry["appliedSchemas"] = applied
        kind = Usd.ModelAPI(prim).GetKind()
        if kind:
            entry["kind"] = kind
        prims.append(entry)

        for attr in prim.GetAttributes():
            n = attr.GetNumTimeSamples()
            if n:
                samples = attr.GetTimeSamples()
                time_sampled[attr.GetPath().pathString] = {
                    "numTimeSamples": n,
                    "timeRange": [_round(samples[0]), _round(samples[-1])],
                }
            tn = attr.GetTypeName()
            if tn and tn.type and tn == Sdf.ValueTypeNames.Asset:
                v = attr.Get()
                if v is not None:
                    assets.append({
                        "attr": attr.GetPath().pathString,
                        "assetPath": _scrub(v.path.replace("\\", "/")),
                        "resolved": bool(v.resolvedPath),
                    })

        if prim.IsA(UsdGeom.Mesh):
            mesh = UsdGeom.Mesh(prim)
            binding = UsdSkel.BindingAPI(prim)
            points = mesh.GetPointsAttr().Get() or []
            counts = mesh.GetFaceVertexCountsAttr().Get() or []
            m = {
                "pointCount": len(points),
                "faceCount": len(counts),
                "pointsSha256": _hash_float_array(points),
                "material": [t.pathString for t in UsdShade.MaterialBindingAPI(
                    prim).GetDirectBindingRel().GetTargets()],
                "skeleton": [t.pathString
                             for t in binding.GetSkeletonRel().GetTargets()],
            }
            bs = binding.GetBlendShapesAttr().Get()
            if bs:
                m["blendShapes"] = list(bs)
            pv = UsdGeom.PrimvarsAPI(prim).GetPrimvars()
            if pv:
                m["primvars"] = sorted(p.GetName() for p in pv)
            meshes[pth] = m

        if prim.IsA(UsdSkel.Skeleton):
            skel = UsdSkel.Skeleton(prim)
            s = {"joints": list(skel.GetJointsAttr().Get() or [])}
            rest = skel.GetRestTransformsAttr().Get()
            bind = skel.GetBindTransformsAttr().Get()
            if rest:
                s["restTransformsSha256"] = _hash_float_array(rest)
            if bind:
                s["bindTransformsSha256"] = _hash_float_array(bind)
            skeletons[pth] = s

        if prim.IsA(UsdShade.Material):
            mat = {"shaders": {}}
            for child in prim.GetChildren():
                shader = UsdShade.Shader(child)
                if not shader:
                    continue
                inputs = {}
                for inp in shader.GetInputs():
                    src = inp.GetConnectedSource()
                    if src:
                        inputs[inp.GetBaseName()] = {
                            "connectedTo": src[0].GetPrim().GetPath().pathString
                            + "." + src[1]}
                    else:
                        v = inp.Get()
                        if v is not None:
                            inputs[inp.GetBaseName()] = _jsonable(v)
                mat["shaders"][child.GetPath().pathString] = {
                    "id": shader.GetIdAttr().Get(),
                    "inputs": inputs,
                }
            materials[pth] = mat

    d["prims"] = prims
    d["meshes"] = meshes
    d["skeletons"] = skeletons
    d["materials"] = materials
    d["assets"] = assets
    d["timeSampledAttrs"] = time_sampled
    return d


def gen_digests() -> dict[str, str]:
    from pxr import Usd

    out = {}
    for group, name, path in _fixture_inputs():
        digest: dict = {
            "baselineSchemaVersion": BASELINE_SCHEMA_VERSION,
            "source": _scrub(path.as_posix()),
        }
        try:
            stage = Usd.Stage.Open(str(path))
        except Exception as exc:  # read-fatal: freeze the failure + its codes
            stage = None
            digest["open"] = False
            digest["errorCodes"] = sorted(set(_CODE_RE.findall(str(exc))))
        if stage:
            digest["open"] = True
            digest.update(_digest_stage(stage))
        elif "errorCodes" not in digest:
            digest["open"] = False
            digest["errorCodes"] = []
        out[f"digests/{group}/{name}.json"] = _dump_json(digest)
    return out


def gen_schema() -> dict[str, str]:
    from pxr import Sdf, Usd

    gen_path = _generated_schema_path()
    layer = Sdf.Layer.OpenAsAnonymous(str(gen_path))
    schemas = {}
    for prim in layer.rootPrims:
        props = {}
        for attr in prim.attributes:
            spec = {
                "typeName": str(attr.typeName.type.typeName)
                if attr.typeName.type else str(attr.typeName),
                "variability": str(attr.variability),
            }
            if attr.default is not None:
                spec["default"] = _jsonable(attr.default)
            allowed = attr.allowedTokens
            if allowed:
                spec["allowedTokens"] = list(allowed)
            if attr.custom:
                spec["custom"] = True
            props[attr.name] = spec
        for rel in prim.relationships:
            props[rel.name] = {"kind": "relationship"}
        schemas[prim.name] = {
            "properties": props,
            "apiSchemaType": prim.customData.get("apiSchemaType"),
        }

    registry = Usd.SchemaRegistry()
    registered = {
        name: registry.FindAppliedAPIPrimDefinition(name) is not None
        for name in SCHEMA_APIS
    }

    contract = {
        "baselineSchemaVersion": BASELINE_SCHEMA_VERSION,
        "schemaContractVersion": 1,
        "generatedSchemaSha256": _sha256_text(
            gen_path.read_text("utf-8").replace("\r\n", "\n")),
        "schemas": schemas,
        "registryVisible": registered,
    }
    return {"schema_contract.json": _dump_json(contract)}


def gen_discovery() -> dict[str, str]:
    from pxr import Plug, Sdf

    def scrub_meta(v):
        if isinstance(v, dict):
            return {k: scrub_meta(v[k]) for k in sorted(v)}
        if isinstance(v, list):
            return [scrub_meta(i) for i in v]
        if isinstance(v, str):
            return _scrub(v.replace("\\", "/"))
        return v

    # Union of registered USD types across every workspace bundle. Which
    # bundle registers which type is deliberately NOT frozen (it changes as
    # registrations move during the split); each bundle's own discovery tests
    # own that attribution. What must never change is the set of types and
    # their registration metadata.
    registered_types: dict = {}
    for name in _registry_plugin_names():
        plugin = Plug.Registry().GetPluginWithName(name)
        if not plugin:
            raise RuntimeError(
                f"plugin '{name}' not discoverable — run under "
                "`ost plugin run plugins/usdVrm -- python ...` with every "
                "workspace bundle in the session (--with plugins/<bundle>)")
        for type_name, meta in dict(plugin.metadata).get("Types", {}).items():
            if type_name in registered_types:
                raise RuntimeError(
                    f"type '{type_name}' registered by more than one "
                    "workspace bundle")
            registered_types[type_name] = scrub_meta(dict(meta))

    fmt = Sdf.FileFormat.FindByExtension("vrm")
    disco = {
        "baselineSchemaVersion": BASELINE_SCHEMA_VERSION,
        "registeredTypes": registered_types,
        "fileFormat": {
            "found": fmt is not None,
            "formatId": fmt.formatId if fmt else None,
            "target": fmt.target if fmt else None,
            "extensions": list(fmt.GetFileExtensions()) if fmt else [],
        },
        "pythonSurface": {
            # The bundles ship no Python module of their own; the
            # Python-visible surface is exactly the registry state frozen
            # here plus the schema contract (schema_contract.json).
            "pythonModule": None,
            "schemaApis": SCHEMA_APIS,
        },
    }
    return {"discovery.json": _dump_json(disco)}


def gen_diagnostics() -> dict[str, str]:
    sys.path.insert(0, str(TOOLS))
    try:
        import vrm_diagnostics
    finally:
        sys.path.pop(0)

    catalog = {
        code: {"severity": spec.severity.label,
               "source": spec.source,
               "title": spec.title}
        for code, spec in sorted(vrm_diagnostics.CATALOG.items())
    }
    corpus = {
        m["id"]: {"expectedDiagnostics": m.get("expectedDiagnostics", []),
                  "expectedMaxSeverity": m.get("expectedMaxSeverity")}
        for m in _corpus_manifest()["models"]
        if m.get("storage") == "vendored"
    }
    negative = {
        fx["id"]: {"file": fx["file"], "mechanism": fx["mechanism"],
                   "code": fx["code"]}
        for fx in _negative_manifest()["fixtures"]
    }
    data = {
        "baselineSchemaVersion": BASELINE_SCHEMA_VERSION,
        "catalog": catalog,
        "corpusExpectations": corpus,
        "negativeContract": negative,
    }
    return {"diagnostics.json": _dump_json(data)}


# --- symbols ---------------------------------------------------------------

def _platform_tag() -> str:
    import platform
    machine = platform.machine().lower()
    machine = {"amd64": "x86_64", "arm64": "arm64", "aarch64": "arm64"}.get(
        machine, machine)
    osname = {"win32": "windows", "darwin": "macos"}.get(sys.platform, "linux")
    return f"{osname}-{machine}"


def _find_dumpbin() -> str | None:
    exe = shutil.which("dumpbin")
    if exe:
        return exe
    vswhere = pathlib.Path(r"C:\Program Files (x86)\Microsoft Visual Studio"
                           r"\Installer\vswhere.exe")
    if not vswhere.exists():
        return None
    try:
        found = [l.strip() for l in subprocess.run(
            [str(vswhere), "-latest", "-products", "*", "-find",
             r"VC\Tools\MSVC\**\bin\Hostx64\x64\dumpbin.exe"],
            capture_output=True, text=True, check=True).stdout.splitlines()
            if l.strip()]
    except subprocess.CalledProcessError:
        return None
    return found[-1] if found else None


def _exported_symbols(lib: pathlib.Path) -> list[str] | None:
    """Exported symbol names for the built plugin library, or None if the
    platform's dump tool is unavailable."""
    if sys.platform == "win32":
        dumpbin = _find_dumpbin()
        if not dumpbin:
            return None
        text = subprocess.run([dumpbin, "/nologo", "/exports", str(lib)],
                              capture_output=True, text=True, check=True).stdout
        syms = []
        in_table = False
        for line in text.splitlines():
            parts = line.split()
            if parts[:4] == ["ordinal", "hint", "RVA", "name"]:
                in_table = True
                continue
            if in_table:
                if not parts:
                    continue
                if parts[0] == "Summary":
                    break
                if len(parts) >= 4 and parts[0].isdigit():
                    syms.append(parts[3])
        return sorted(syms)
    nm = shutil.which("nm")
    if not nm:
        return None
    flags = ["-gU"] if sys.platform == "darwin" else ["-D", "--defined-only"]
    text = subprocess.run([nm, *flags, str(lib)],
                          capture_output=True, text=True, check=True).stdout
    return sorted({parts[-1] for parts in
                   (l.split() for l in text.splitlines()) if parts})


def gen_symbols() -> dict[str, str]:
    libs = sorted(PLUGINS.glob("*/lib/*.dll")) + sorted(PLUGINS.glob("*/lib/*.so")) \
        + sorted(PLUGINS.glob("*/lib/*.dylib"))
    if not libs:
        raise RuntimeError("no built plugin library under plugins/*/lib "
                           "— run `ost plugin build` on each bundle first")
    # Union across every bundle library: a symbol moving between bundles
    # during the split keeps this artifact byte-identical; a symbol appearing
    # or vanishing does not.
    merged: set[str] = set()
    for lib in libs:
        syms = _exported_symbols(lib)
        if syms is None:
            return {}  # dump tool unavailable: caller reports a skip
        merged.update(syms)
    header = (f"# Exported native symbols across workspace bundle libraries "
              f"({_platform_tag()}); frozen by tools/baseline_freeze.py\n")
    return {f"symbols/{_platform_tag()}.txt": header + "\n".join(sorted(merged)) + "\n"}


GENERATORS = {
    "usda": gen_usda,
    "digests": gen_digests,
    "schema": gen_schema,
    "discovery": gen_discovery,
    "diagnostics": gen_diagnostics,
    "symbols": gen_symbols,
}


def _dump_json(data) -> str:
    return json.dumps(data, indent=2, sort_keys=True, ensure_ascii=True) + "\n"


# ---------------------------------------------------------------------------
# Update / check drivers
# ---------------------------------------------------------------------------

def _generate(sections) -> tuple[dict[str, str], list[str]]:
    artifacts: dict[str, str] = {}
    skips: list[str] = []
    for section in sections:
        produced = GENERATORS[section]()
        if section == "symbols" and not produced:
            skips.append(f"symbols: no symbol dump tool for {_platform_tag()}")
        artifacts.update(produced)
    return artifacts, skips


def do_update(sections) -> int:
    artifacts, skips = _generate(sections)
    # Regenerated sections fully own their subtrees: drop stale files so a
    # removed fixture cannot leave a zombie baseline behind. Symbol baselines
    # for *other* platforms are preserved.
    for section, subdir in (("usda", "usda"), ("digests", "digests")):
        if section in sections and (BASELINE / subdir).exists():
            shutil.rmtree(BASELINE / subdir)
    for rel, text in sorted(artifacts.items()):
        dest = BASELINE / rel
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_text(text, encoding="utf-8", newline="\n")
        print(f"  wrote {rel}")
    for s in skips:
        print(f"  SKIP  {s}")
    print(f"baseline updated: {len(artifacts)} artifact(s) under "
          f"{BASELINE.relative_to(REPO)}")
    return 0


def do_check(sections) -> int:
    artifacts, skips = _generate(sections)
    failures = []
    for rel, expected_new in sorted(artifacts.items()):
        committed = BASELINE / rel
        if not committed.exists():
            failures.append((rel, "missing from committed baseline "
                             "(run --update on the baseline PR only)"))
            continue
        old = committed.read_text("utf-8").replace("\r\n", "\n")
        if old != expected_new:
            diff = "\n".join(difflib.unified_diff(
                old.splitlines(), expected_new.splitlines(),
                fromfile=f"committed/{rel}", tofile=f"current/{rel}",
                lineterm="", n=2))
            failures.append((rel, diff))
    # Baseline files whose generator ran but no longer produces them.
    for section, subdir in (("usda", "usda"), ("digests", "digests")):
        if section not in sections:
            continue
        for f in sorted((BASELINE / subdir).rglob("*")):
            if f.is_file():
                rel = f.relative_to(BASELINE).as_posix()
                if rel not in artifacts:
                    failures.append((rel, "committed baseline artifact is no "
                                     "longer produced (fixture removed?)"))

    for s in skips:
        print(f"[SKIP] {s}")
    if failures:
        print(f"\nBASELINE REGRESSION: {len(failures)} artifact(s) differ "
              "from the committed Phase 0 baseline.\n")
        for rel, detail in failures:
            print(f"[DIFF] {rel}")
            lines = detail.splitlines()
            for line in lines[:40]:
                print(f"    {line}")
            if len(lines) > 40:
                print(f"    ... ({len(lines) - 40} more diff lines)")
        print("\nA migration PR must not change baseline artifacts "
              "(docs/architecture/WORKSPACE.md §7). If this change is an "
              "intended behavior change, land it in its own non-structural "
              "PR and regenerate with --update.")
        return 1
    checked = len(artifacts)
    print(f"baseline OK: {checked} artifact(s) match the committed baseline")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    mode = ap.add_mutually_exclusive_group()
    mode.add_argument("--check", action="store_true",
                      help="verify current behavior against the committed "
                           "baseline (default)")
    mode.add_argument("--update", action="store_true",
                      help="rewrite the committed baseline from current "
                           "behavior")
    ap.add_argument("--only", default=",".join(SECTIONS),
                    help="comma-separated sections to process "
                         f"(default: all of {','.join(SECTIONS)})")
    args = ap.parse_args()

    sections = [s.strip() for s in args.only.split(",") if s.strip()]
    unknown = [s for s in sections if s not in SECTIONS]
    if unknown:
        ap.error(f"unknown section(s): {', '.join(unknown)}")

    try:
        import pxr  # noqa: F401
    except ImportError:
        print("error: OpenUSD Python bindings not importable. Run inside the "
              "plugin session:\n  ost plugin run plugins/usdVrm -- python "
              "tools/baseline_freeze.py", file=sys.stderr)
        return 2

    return do_update(sections) if args.update else do_check(sections)


if __name__ == "__main__":
    sys.exit(main())
