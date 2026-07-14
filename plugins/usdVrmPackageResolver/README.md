# usdVrmPackageResolver

The OpenUSD `ArPackageResolver` for `.vrm` containers, split out of the
`usdVrm` file-format bundle (workspace Phase 3,
[`docs/architecture/WORKSPACE.md`](../../docs/architecture/WORKSPACE.md) §1).

The `.vrm` importer authors embedded textures as package-relative paths:

```text
/path/avatar.vrm[images/<content-hash>.png]
```

This bundle resolves those paths and serves the embedded image bytes straight
out of the GLB container — no temp extraction, no stage authoring. `Hio`
resolves the innermost extension (png/jpg) and asks `Ar` for the bytes; the
resolver parses the container through `vrmContainer`'s checked byte views and
returns the matching image.

## Boundaries (WORKSPACE.md §2)

- Depends on `vrmContainer` (plain library, via `requires.libraries`) and the
  OpenUSD `Ar` surface only.
- Never links `usdVrmFileFormat` or `vrmSchema`; never authors `UsdStage` /
  `UsdPrim`. `tests/check_boundaries.py` enforces both on every built binary.
- Package-path semantics are frozen: entry names are content-addressed
  `images/<fnv1a64>.<ext>` produced by `vrmContainer::MakeEmbeddedResourcePath`.

## Build & test

```sh
ost plugin build plugins/usdVrmPackageResolver
ost plugin test  plugins/usdVrmPackageResolver --up-to 5
ost plugin run   plugins/usdVrmPackageResolver -- python tests/test_resolver.py
```

Both flows resolve `vrmContainer` as an installed CMake package (the ost flow
composes it into the workspace prefix automatically; the plain-CMake composed
build uses the in-tree target). Without this bundle in a session, embedded
`avatar.vrm[images/...]` texture paths do not resolve — stages still open, but
texture bytes are unavailable.
