---
status: accepted
owner: usd-vrm-plugins
---

# usd-vrm-plugins — native USD export policy (`vrm_export`)

> The canonical policy for turning an imported `.vrm` into a native USD
> artifact — `.usda`, `.usdc` or `.usdz` — that opens with no VRM plugin
> installed. It is a companion to [DESIGN_POLICY.md](DESIGN_POLICY.md), which
> stays canonical for the importer and Product P0–P6, and it answers to
> [INTEGRATION_SCOPE_POLICY.md](INTEGRATION_SCOPE_POLICY.md) for whether an
> identity may exist at all.
>
> Section numbers are stable so other documents can cite them ("export policy
> §6.2"). Later revisions may add subsections; a numbered section never
> changes meaning.
>
> **This introduces no new phase sequence.** The steps in §11 are the internal
> order of one track, not a sequence beside Product P0–P6 and Workspace
> Phase 0–8 ([roadmap](../roadmap/README.md#sequences)). The release a step
> lands in is fixed by the
> [status table](../roadmap/README.md#status-at-a-glance), and the open steps
> are the [export track](../roadmap/export-track.md).
>
> **Step 1 implemented 2026-09-26.** Four things measured while building it
> changed or sharpened a rule here: a `.usdz`'s root layer is
> `defaultLayer.usdc`, not the output's name (§6.3); a localized name is
> vrmContainer's hash, which is not quite FNV-1a (§6.2); crate outputs drop
> the sign of some zeros (§9); and packaging an unlocalized layer copies the
> whole `.vrm` into the package (§2).

---

## 1. Purpose

The importer makes a `.vrm` readable as a stage, and only where the plugins
are installed: `usdVrmFileFormat` to read the file, `vrmSchema` to recognize
what it authored, and `usdVrmPackageResolver` to serve the textures it points
at inside the `.vrm`. None of that travels with a stage. A consumer without
the plugins — a DCC tool, a viewer, a web pipeline, a CI job holding a fixed
artifact — gets nothing.

`vrm_export` is the explicit step that takes an imported stage and writes a
**native USD artifact** of it:

- `.vrm` → `.usda` / `.usdc`, with the textures beside it
- `.vrm` → `.usdz`, self-contained
- fixed artifacts for regression tests and corpus comparisons
- later, a VRM avatar and a `.vrma` clip assembled into one artifact (§11,
  Step 3)

Its goal is an output that opens under a plain OpenUSD install: no VRM
plugin, no source `.vrm`, no machine-dependent path.

## 2. What was measured before this was written

A prototype of the whole Step 1 pipeline, in Python against the pinned
OpenUSD 26.08 runtime, on 2026-09-25, over the vendored VRM 1.0 sample
`Seed-san.vrm`:

- The imported stage uses one layer — the `.vrm` itself — plus the session
  layer; `defaultPrim` is `Asset` and there are no sublayers. Exporting the
  root layer's content is therefore the whole stage.
- The importer authors every embedded texture as an **absolute**
  package-relative path, `C:/…/Seed-san.vrm[images/<fnv1a64>.png]`. All 14 were
  read through `Ar` (the package resolver serving the bytes) and written out.
- A localized path must be **anchored**: `./textures/<name>.png`. Written as
  `textures/<name>.png` it is a search path to USD, and
  `UsdUtilsCreateNewUsdzPackage` does not find it relative to the layer — it
  warns `Failed to resolve reference` for each one and files them under
  `0/<name>.png` in the archive.
- With anchored paths, `UsdUtilsCreateNewUsdzPackage` wrote 15 entries — the
  root layer first, then `textures/…` — and the five `usdUtilsValidators`
  (`PackageEncapsulation`, `MissingReference`, `RootPackage`, `UsdzPackage`,
  `FileExtension`) reported nothing. `UsdUtilsComputeAllDependencies` found
  one layer, 14 assets, no unresolved path.
- Opened **with no VRM plugin registered**, the `.usdz` composed 608 prims and
  every texture resolved inside the package. The VRM API schemas stay
  authored (`apiSchemas`) but are not recognized — `GetAppliedSchemas()` is
  empty — so the VRM data is present as attributes, and is typed again
  wherever `vrmSchema` is installed.
- `SdfZipFileWriter` stamps each archive entry with the **local-time
  modification time of the file it was added from**, so two exports of the
  same `.vrm` are not byte-identical (§9).

Measured while Step 1 was built (2026-09-26), against the tool itself:

- Packaged **without** localization, the layer's
  `C:/…/textures.vrm[images/…]` paths make `UsdUtilsCreateNewUsdzPackage`
  copy the **whole source `.vrm`** into the package, as `0/textures.vrm` —
  the outcome §6.4 exists to prevent, and the reason localization comes
  first. `--check` refuses that output.
- `SdfZipFileWriter` writes an entry name's UTF-8 bytes **without the ZIP
  UTF-8 flag** (general-purpose bit 11). OpenUSD reads such a name back
  correctly; any other ZIP reader decodes it as CP437. Hence an ASCII root
  layer name (§6.3).
- OpenUSD's crate writer drops the sign of zero in integer-valued vectors and
  float arrays — `Gf.Vec2f(-0.0, -0.0)` reads back as `(0, 0)` — while a
  scalar float keeps its `-0.0`. The importer authors such zeros (MaterialX
  `place2d` offsets, blend-shape offsets), so a `.usdc` or `.usdz` export is
  numerically, not bit-for-bit, equal to its source (§9).
- vrmContainer's `HashBytes` — the name the importer gives an embedded
  image — uses the offset basis `1469598103934665603`, which is FNV-1a 64's
  published `14695981039346656037` with its last digit missing (§6.2).

## 3. Responsibilities

Four responsibilities, and none is merged into another:

| Identity | Responsibility |
| --- | --- |
| `usdVrmFileFormat` | VRM → USD representation: parse VRM 0.x / 1.0, canonicalize, author the stage and its VRM schema data. |
| `usdVrmPackageResolver` | Resources embedded in a `.vrm` → asset resolution. |
| `vrm_export` | USD representation → portable native USD artifact: materialize, localize dependencies, package, validate. |

`vrm_export` is also the user-facing CLI; its export operation is kept apart
from its command line inside the tool (§4).

### 3.1 The file format gains no write capability

`.vrm`'s `SdfFileFormat` stays read-only. A file format answers *how is this
source read as a layer*; producing a deployment artifact is a different
operation with a different lifetime, and it belongs to a tool.

### 3.2 No file-format argument produces a package

`avatar.vrm:SDF_ARGS:package=usdz` is not adopted. A file-format argument
changes how a layer is read. Writing a `.usdz` changes the filesystem, and
it is an export operation, invoked as one.

### 3.3 "Export" means VRM → native USD here

Product P6 (DESIGN_POLICY §17) researches exporting **to** `.vrm` — writing a
VRM back from USD. `vrm_export` goes the other way and writes USD. A USD → VRM
writer, if P6 ever produces one, is not a mode of this tool unless P6 decides
so and records it here.

## 4. Placement and dependency boundary

**One tool identity, `vrm_export`, in `tools/vrmExport/`.** The export
operation is its own target inside the tool, separate from the command line,
with its own unit tests; `main.cpp` parses arguments and maps results to exit
codes (§5.3) and does nothing else.

**It is not a library identity.** A library consumed only by its own CLI
meets none of the three tests for a new identity
([scope policy §3](INTEGRATION_SCOPE_POLICY.md#3-the-test-for-a-new-identity)):
it registers nothing with OpenUSD, it opens no new dependency direction, and
nobody would install it on its own. It moves to `libs/` when a second consumer
exists — Step 3's assembly, a Python binding, another tool — exactly as
`vrmContainer` did. Decided 2026-09-25.

The dependency boundary:

```text
vrm_export -> OpenUSD (usd, sdf, ar, tf, usdUtils, usdValidation)
           ~> usdVrmFileFormat, usdVrmPackageResolver, vrmSchema   (run time only,
                                                                    through the
                                                                    plugin registry)
```

- It never links `usdVrmFileFormat`, `vrmContainer`, `vrmSchema` or any
  importer internal, and never re-implements the importer. It reaches a
  `.vrm` the way any OpenUSD client does: `UsdStage::Open`.
- It reads no `vrm:` attribute by name. What the importer authored is carried
  through as data; a change to the importer's output is not a change here.
- It links no consumed motion package until Step 3 needs one.

`vrm_export` is a member of the aggregate product from Step 1. There is no
choice to make: `ost` refuses a discovered tool that is neither in
`release_members` nor excluded, and `release.yml` counts every `tools/`
descriptor against the packaged set, so an excluded tool fails the release
lane ([WORKSPACE.md](../architecture/WORKSPACE.md) §5).

## 5. The command line

### 5.1 Synopsis

```text
vrm_export <input.vrm> -o <output.{usda,usdc,usdz}> [--check] [--overwrite] [--verbose]
vrm_export --help | --version
```

The output format is the output path's extension: `.usda` text, `.usdc`
crate, `.usdz` package. There is no `--format`. A second positional argument
in place of `-o` is not accepted, so one spelling exists.

### 5.2 Options

| Option | Step | Meaning |
| --- | --- | --- |
| `-o`, `--output <path>` | 1 | The output. Required. |
| `--check` | 1 | Validate the written output (§8) and fail with exit code 6 if it does not hold. |
| `--overwrite` | 1 | Replace an existing output file. Without it an existing output is refused (exit 2). |
| `--verbose` | 1 | Report each localized dependency and each check. |
| `--help`, `--version` | 1 | |
| `--flatten` | 2 | Opt-in flattening (§6.1). |
| `--motion <clip.vrma>` | 3 | Assemble a clip onto the avatar. |
| `--keep-source` | 4 | Embed the source `.vrm` (§6.4). |
| `--keep-temp` | later | Keep the temporary directory of a failed `.usdz` export. |

`--overwrite` is pulled forward from the source plan's second milestone:
refusing by default is the safe behaviour, and a Step 1 that overwrote
silently would make Step 2's `--overwrite` a breaking change.

### 5.3 Exit codes

| Code | Meaning |
| --- | --- |
| 0 | Success. |
| 1 | Unexpected failure. |
| 2 | Invalid arguments: an unknown option, a missing `-o`, an unsupported extension, an input that is not a `.vrm`, an existing output without `--overwrite`. |
| 3 | The input could not be opened as a stage — including no `.vrm` file format registered. |
| 4 | Export failed: a dependency could not be read, or a layer could not be written. |
| 5 | `.usdz` packaging failed. |
| 6 | `--check` found the output invalid. |

Diagnostics go to stderr, one line each, prefixed `vrm_export:`. Where the
cause is a missing plugin — no `.vrm` file format, an embedded texture that
no resolver serves — the message names the bundle.

## 6. Output shape

### 6.1 Materialization

The stage is opened with `UsdStage::Open`, and its **root layer's content**
is written — transferred to a new layer and exported, never the source layer
edited in place. The session layer is not part of the output.

- **No flattening by default.** Composition semantics are kept, and a
  package is not a flatten: they are two operations. Step 1's input is a
  `.vrm`, whose layer has no composition arcs (§2), so the distinction first
  matters at Step 3; `--flatten` arrives with Step 2 as an explicit opt-in.
- **No re-layout.** The prim hierarchy the importer authored — `/Asset`,
  `geo`, `mtl`, `skel`, `rig` — is written as authored.
- **Step 1 accepts `.vrm` input only.** An arbitrary USD input is a
  non-goal (§10); the input check is on the extension.

### 6.2 Dependency localization

Every asset path in the layer is resolved through `Ar` and its bytes are
copied into the output:

- A localized file is named `textures/<hash>.<ext>` — a 64-bit hash of its
  bytes as sixteen lowercase hex digits, and the source's lowercase
  extension. The hash is vrmContainer's `HashBytes`: FNV-1a's algorithm with
  an offset basis one digit short of the published one (§2). It is kept so an
  embedded image has the same name here as the importer gave it inside the
  `.vrm`, where the resolver's package paths are frozen on it; for an
  external image it replaces a name that could collide with another
  avatar's.
- It is referenced as `./textures/<name>` — anchored (§2).
- Identical bytes are written once.
- A path that does not resolve, or whose bytes cannot be read, fails the
  export (exit 4). An output that silently lost a texture is the failure this
  tool exists to prevent.

For `.usda` and `.usdc`, `textures/` is written **next to the output file**,
so the loose export and the package's contents have one shape. Because every
name is a content hash, several exports can share one directory: an existing
file of the same name holds the same bytes. Decided 2026-09-25.

### 6.3 `.usdz` packaging

`.usdz` is written by OpenUSD's own packaging, `UsdUtilsCreateNewUsdzPackage`,
never by a ZIP writer of this repository:

1. The localized layer is exported as `defaultLayer.usdc`, with its
   `textures/`, into a temporary directory. The name is fixed and ASCII: the
   ZIP writer OpenUSD packages with sets no UTF-8 flag, so a non-ASCII output
   name would reach other ZIP readers as CP437 (§2), and a fixed name keeps a
   package's contents independent of its file name.
2. `UsdUtilsCreateNewUsdzPackage` packages it into the output path. The root
   layer is the first entry, as the USDZ specification requires.
3. The temporary directory is removed, whether packaging succeeded or not.

The root layer is always crate, and every entry name is ASCII. Entry names
beyond the ones above are OpenUSD's to choose, and nothing here adds a
convention of its own.

### 6.4 The source `.vrm` is not included

```text
.vrm   the source representation
.usdc  the materialized USD representation
.usdz  the deployment representation
```

The package carries the materialized representation and nothing that needs
the VRM plugins to read. Optional source embedding (`--keep-source`) is
Step 4's, if a consumer asks for it.

## 7. Absolute paths

No output contains a path of the machine that produced it: not the source
`.vrm`'s directory, not the temporary directory of §6.3. The importer authors
only one kind of absolute path (§2), and localization replaces every one;
the integration tests assert the absence directly, on the bytes of a `.usda`
export.

## 8. Validation

`--check` reopens the written output and requires:

- the root layer opens, and the stage has a `defaultPrim`;
- every dependency resolves (`UsdUtilsComputeAllDependencies` reports no
  unresolved path);
- no asset path is absolute or points into a `.vrm`;
- for `.usdz`: every dependency is inside the package, and the
  `usdUtilsValidators` — `PackageEncapsulationValidator`,
  `MissingReferenceValidator`, `RootPackageValidator`,
  `UsdzPackageValidator`, `FileExtensionValidator` — report no error.

What `--check` cannot prove is the one claim this tool is for: that the
output opens **without the VRM plugins**. Its own process has them loaded.
That claim is made by the integration tests, which reopen every output in a
separate process with no VRM plugin on `PXR_PLUGINPATH_NAME`.

## 9. Determinism

- `.usda` and `.usdc`: repeated exports of one `.vrm` are expected to be
  byte-identical, and Step 1's tests assert it for `.usda`.
- **Equality with the source.** A `.usda` holds every authored value
  bit-for-bit. A `.usdc` or `.usdz` holds every value numerically: the crate
  writer drops the sign of zero in integer-valued vectors and float arrays
  (§2). Step 1's tests compare accordingly.
- `.usdz`: **not byte-identical in Step 1.** Each entry carries the
  modification time of the temporary file it was packaged from, encoded in
  local time (§2). Pinning those times before packaging makes repeated
  exports on one host identical; identity across hosts also needs the time
  zone out of the encoding, which is OpenUSD's to change. Which of the two
  levels is guaranteed is Step 2's decision (§12 q2).
- Localized names are content hashes and the archive order is OpenUSD's, so
  neither depends on traversal order or on the machine.

## 10. Non-goals

Not in Step 1, and each is either a later step or out of scope:

- VRMA assembly, animation baking (Step 3)
- source embedding (Step 4)
- arbitrary USD input
- a generic packaging framework, in this repository or in OpenStrata
- flattening by default
- texture transcoding, mesh compression or any optimization
- renderer-specific conversion

The generic *dynamic asset → native USD → USDZ* helper is a candidate for
OpenStrata only when a second plugin workspace needs the same workflow. No
VRM-specific logic goes there.

## 11. Steps

The source plan named these P0–P3; they are **Steps 1–4 of this track**, and
are not Product P0–P3. Status is the
[export track](../roadmap/export-track.md)'s.

1. **Native export.** `.usda`, `.usdc`, `.usdz`; localization; `--check`,
   `--overwrite`; the exit codes; tests over the fixtures and the corpus.
2. **Validation and usability.** Determinism, `--flatten`, a package
   dependency report, richer diagnostics, and the product's `vrm_export`
   run from the release artifact rather than the build tree.
3. **Motion assembly.** `--motion <clip.vrma>`: an avatar and a clip composed
   into one artifact, with the `.vrm` and `.vrma` file formats kept
   independent and `vrm_export` doing the composition.
4. **Distribution pipeline.** Package profiles, optional source preservation,
   package metadata, and the generic helper extraction of §10 if justified.

## 12. Open questions

- **q1 — `package_vrm.py`.** The importer's Python tool
  ([`tools/package_vrm.py`](../../plugins/usdVrmFileFormat/tools/package_vrm.py))
  already writes a `.usda` with a `textures/` directory and a JSON inventory,
  and `vrm_report.py` imports its asset enumeration. Once Step 1 ships it is a
  second exporter. Retire it, keep it as a development tool, or make its
  report Step 2's dependency report?
- **q2 — `.usdz` byte reproducibility.** Per host (pinned modification
  times), across hosts (needs OpenUSD), or stage-semantic equality only (§9)?
- **q3 — who authors Step 3's binding layer.** The same question the backlog
  holds for any avatar + clip composition
  ([VRM motion open questions](../roadmap/backlog.md#vrm-motion-open-questions));
  Step 3 is its first consumer outside the parity harness.
