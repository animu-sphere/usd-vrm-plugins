# vrm_export

Writes an imported `.vrm` as native USD — `.usda`, `.usdc` or `.usdz` — that
opens with **no VRM plugin installed**.

```bash
vrm_export avatar.vrm -o avatar.usdz --check
vrm_export avatar.vrm -o avatar.usda
```

The output format is the output path's extension. The policy — what the tool
is for, what it may depend on, and why each rule below exists — is
[docs/design/VRM_EXPORT_POLICY.md](../../docs/design/VRM_EXPORT_POLICY.md);
what is left to build is the
[export track](../../docs/roadmap/export-track.md).

## What it needs at run time

It opens the `.vrm` with `UsdStage::Open`, like any OpenUSD client, so the
session needs the three VRM bundles registered on `PXR_PLUGINPATH_NAME`:
`usdVrmFileFormat` to read it, `vrmSchema` for the schemas it applies, and
`usdVrmPackageResolver` to serve the textures embedded in it. Without the
first the tool exits 3; without the resolver, 4. Each message names the
bundle.

The **output** needs none of them.

## What it writes

- The imported stage's root layer, as the importer authored it — not
  flattened unless `--flatten` asks, not re-laid-out. The session layer is
  not part of it.
- Every texture, copied to `textures/<hash>.<ext>` and referenced as
  `./textures/<hash>.<ext>`. `<hash>` is the importer's own name for an
  embedded image, so the same image has the same name everywhere. For a
  `.usda` / `.usdc` the directory is written next to the output; several
  exports can share it.
- For `.usdz`: a package made by OpenUSD's `UsdUtilsCreateNewUsdzPackage`,
  holding `defaultLayer.usdc` first and `textures/` after. The source `.vrm`
  is never included.

No output names a path of the machine that wrote it.

## Options

| Option | Meaning |
| --- | --- |
| `-o`, `--output <path>` | The output. Required. |
| `--check` | Reopen the output and validate it: a `defaultPrim`, every dependency resolved, no absolute path or path into a `.vrm`, and for `.usdz` OpenUSD's five `usdUtilsValidators`. |
| `--overwrite` | Replace an existing output. Without it, one is refused. |
| `--flatten` | Write the composed stage flattened into one layer. For a `.vrm`, whose layer has no composition arcs, the stage is the same either way. |
| `--dependency-report` | On success, print a JSON inventory to stdout: sorted distinct localized paths, their byte counts, the root layer and validation status ([policy §8.1](../../docs/design/VRM_EXPORT_POLICY.md#81-dependency-report)). Does not imply `--check`. |
| `--verbose` | Report each localized texture and each check. |
| `--help`, `--version` | |

`--check` runs in the tool's own process, which has the VRM plugins loaded,
so it cannot show that the output opens without them. The end-to-end test
does, in a separate process (below).

To save the portable dependency inventory, including successful validation:

```sh
vrm_export avatar.vrm -o avatar.usdz --check --dependency-report > dependencies.json
```

Diagnostics and `--verbose` output stay on stderr. The inventory contains no
machine-local paths, counts duplicate images once, and excludes the root
layer from its byte total. A failed export emits no inventory.

## Exit codes

| Code | Meaning |
| --- | --- |
| 0 | Success. |
| 1 | Unexpected failure. |
| 2 | Invalid arguments — including an input that is not a `.vrm`, and an existing output without `--overwrite`. |
| 3 | The input could not be opened — including no `.vrm` file format registered. |
| 4 | Export failed — a texture could not be read or a layer written. |
| 5 | `.usdz` packaging failed. |
| 6 | `--check` found the output invalid. |

## What a `.usdc` / `.usdz` does not keep

OpenUSD's crate writer stores an integer-valued vector or float array without
the sign of a zero: an importer-authored `(-0, -0)` reads back as `(0, 0)`.
The values are equal and nothing renders differently; a `.usda` keeps the
sign.

## Layout

The export operation is its own target (`vrm_export_operation`, in
`src/export/`), apart from the command line in `src/main.cpp`. It is not a
library identity: the CLI is its only consumer, and it moves to `libs/` when a
second one exists (policy §4).

## Tests

- `vrm_export_operation` — the operation's unit tests: format detection, the
  command line, every refusal that comes before a stage is opened, and
  `--check` on outputs written by hand. Runs with no VRM plugin registered.
- `vrm_export_end_to_end` — 16 fixtures and corpus avatars to all three
  formats with `--check`; each output reopened **in a process with no VRM
  plugin** and compared with the source stage prim by prim, attribute by
  attribute, and texture byte by texture byte. Also: a `.usda` names no local
  path and is byte-identical when exported twice; a `.usdz`'s entries, every
  one stamped 2000-01-01, and the package byte-identical when exported again
  in another time zone (policy §9); the
  temporary directory removed; and every exit code from the command line.
  One more input is written at run time: a `.vrm` with one embedded and two
  external images, one of them the embedded image's bytes — two files
  localized for three images, and exit 4 once an external image is gone.
  Every input is also exported with `--flatten`, and held to the same stage
  and the same absence of local paths.
  Each format's dependency report is compared with its actual files (or ZIP
  entries), including byte counts, deduplication and repeatability. CLI cases
  hold the unchecked status, a shared textures directory, a Unicode output
  name, and no report on failure.
  Registered only in the composed root build, which has the three bundles.
- `workspace_unicode_paths` (root) — both formats from a directory no ANSI
  code page can spell.
- `scripts/artifact_only_export_smoke.py` (release lane) — the *installed
  product's* `vrm_export` exports `Seed-san.vrm` and a VRM 0.x fixture to
  `.usdz`. Each package is read back with no VRM plugin registered, and the
  export must refuse once the product's importer registration is moved
  aside.

```sh
ost build
ost test
```
