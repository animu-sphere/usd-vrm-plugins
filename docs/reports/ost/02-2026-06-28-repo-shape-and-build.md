# OST dogfooding report #2 — repo shape, dual-mode build, and a unified root cause

> **Update (ost 0.4.0):** §1's "no root CMake" ask is answered — `ost init --template
> plugin-workspace` now emits the dual-mode root we hand-rolled here. Re-checked
> (incl. a naming-friction note) in
> [report #5](05-2026-06-30-v0.4.0-schema-template.md). The body below is the original
> record.

- Date: 2026-06-28
- Continues [report #1](01-2026-06-28-bootstrap.md). Same host/runtime
  (Windows 11, MSVC 14.34, OpenUSD at `C:\dev\build\usd2605` = **v26.08**).
- Scope: the repository layout decision, making the repo build **without**
  OpenStrata too, cgltf via FetchContent, and a refinement that collapses two of
  report #1's P1 issues into one root cause. Plus forward notes for the planned
  multi-plugin-session / `requires.runtime_libs` work.

---

## 1. Repo shape: project + bundles, and the "where's the CMake?" gap

We kept the OpenStrata-native shape: a project manifest (`openstrata.toml`) at
the repo root and the plugin as a **bundle** under `plugins/usdVrm/` (its own
`openstrata.plugin.yaml`). Rationale: the repo is named `usd-vrm-plugins`
(plural) and the headline ask is dogfooding **multi-plugin sessions**
(`ost plugin run/view --with <bundle>`), for which "1 project → N bundles under
`plugins/`" is the natural unit. `ost plugin <cmd> plugins/usdVrm` correctly
inherits the project's `platform/profile`, so the nesting costs nothing
ergonomically.

**Friction worth fixing in `ost`:** `ost init --bare` + `ost plugin new` leaves
the repo root with **no top-level `CMakeLists.txt`**. That's surprising and
unfriendly to anyone who doesn't use `ost` — they open the repo and there's
nothing to `cmake -S .`. The bundle's own `CMakeLists.txt` exists, but it's two
directories down and not obviously the entry point.

What we did (and what I'd suggest `ost` scaffold for a project that contains
bundles): add a **dual-mode** root `CMakeLists.txt` + `CMakePresets.json` that
`add_subdirectory()` every bundle and resolve OpenUSD via plain
`find_package(pxr)` + `CMAKE_PREFIX_PATH`. The bundle `CMakeLists.txt` stays a
standalone project (so `ost plugin build` can still target it directly) **and**
is `add_subdirectory()`-able. Two requirements made this work cleanly:

- Guard the bundle's `find_package(pxr)` with `if(NOT pxr_FOUND)` so the root can
  resolve it once.
- Stage the built library with `CMAKE_CURRENT_SOURCE_DIR`, **not**
  `CMAKE_SOURCE_DIR` — the scaffold uses `${CMAKE_SOURCE_DIR}/lib`, which when
  the bundle is a subdirectory points at the *repo root* `lib/`, breaking the
  committed `plugInfo.json` `LibraryPath` (and `ost plugin test`). This is a
  latent scaffold bug the moment a bundle is consumed as a subproject.

**Suggestion:** `ost init --template usd-plugin` (or a new
`--template usd-plugins`) could emit this root `CMakeLists.txt`/presets pair and
make the bundle scaffold use `CMAKE_CURRENT_SOURCE_DIR`, so the repo is
buildable both ways from minute one.

## 2. One root cause behind report #1's two "P1 path" issues

Report #1 listed (separately) a relative `CMAKE_TOOLCHAIN_FILE` in
`ost plugin build` and a relative `PXR_PLUGINPATH_NAME` in `ost plugin test/run`.
Re-testing with an **absolute** bundle path shows they're the same bug:
`ost` echoes the bundle path through unchanged, so a relative CLI arg yields
relative derived paths everywhere.

- `ost plugin build plugins/usdVrm` → `-DCMAKE_TOOLCHAIN_FILE=plugins/usdVrm/...`
  (relative → CMake resolves against the build dir → "toolchain not found").
- `ost plugin build "$PWD/plugins/usdVrm"` → absolute `CMAKE_TOOLCHAIN_FILE`,
  **found**. (It then fails only because no `cl` is on PATH — see §3.)
- `ost plugin test "$PWD/plugins/usdVrm"` → absolute `PXR_PLUGINPATH_NAME` →
  discovery + L2-L4 pass.

**Fix:** canonicalize `<bundle>` to an absolute path once, at the top of every
`ost plugin` subcommand, before composing any CMake arg or session env var.
A single `fs::canonicalize` removes both failure modes. This is the
highest-leverage fix for the whole `ost plugin` surface and is a prerequisite for
multi-plugin sessions (every `--with <bundle>` arg needs the same treatment).

## 3. `ost plugin build` on Windows still needs the compiler environment

With the absolute path, `ost plugin build` gets as far as CMake then dies with
*"No CMAKE_CXX_COMPILER could be found"*: the generated toolchain uses the `host`
compiler policy + Ninja, but `ost` doesn't activate a VS environment, so `cl` /
`link` aren't on PATH. Options for `ost` on Windows:
- auto-discover VS via `vswhere` and inject the dev environment for `host`
  policy + Ninja, or
- document that `ost plugin build` must run from a VS Developer prompt (or wrap
  it in `ost devshell`).

**Workaround used here:** a tiny batch that runs `vcvars64.bat` then invokes the
exact CMake command `ost` would, with an absolute `-DCMAKE_TOOLCHAIN_FILE` and
`-DCMAKE_BUILD_TYPE=Release` (report #1 §P3 covers why Release is needed). With
that, the full plugin builds cleanly (7/7 TUs) and `ost plugin test "$PWD/..."`
is green (10 pass / 0 fail / 3 skip).

## 4. cgltf via FetchContent inside the ost build flow — fine

cgltf is now fetched at configure time (pinned tag, `cmake/Dependencies.cmake`),
not vendored. The relevant ost observation: configure-time network + FetchContent
works transparently under the ost-generated toolchain (cgltf's own
`CMakeLists.txt` is skipped via the `SOURCE_SUBDIR do-not-configure` trick so only
the header is taken). No `ost` change needed — but note that a future
"hermetic/offline" `ost` build mode would need an opt-out or a pre-fetch step;
we exposed `-DCGLTF_SOURCE_DIR=` for that case.

## 5. Forward notes for multi-plugin sessions & `requires.runtime_libs`

Concrete things this plugin surfaces for that work:

- **Absolutize per-`--with` bundle** (root + `lib/` + `plugInfo` root + any
  `requires.runtime_libs`). §2 will otherwise recur once per added bundle, with
  the same silent discovery failure.
- **`requires.runtime_libs` contrast case:** `usdVrm` deliberately **statically
  compiles cgltf into one TU and exports no third-party symbols**, so it needs
  **zero** extra runtime lib dirs. That's the opposite of LumaGraph's `zlib.dll`
  case. The session composer should treat an empty/absent `requires.runtime_libs`
  as the common case and not assume every plugin drags a sibling DLL. A good
  end-to-end test matrix for `--with` would pair this (no runtime libs) with a
  plugin that does declare them.
- **`plugInfo.json` `LibraryPath` is still the cross-platform soft spot**
  (report #1 §P1.3). We committed a Windows `.dll`, `../../../lib/`-relative path
  that works for both `ost plugin test` and plain CMake here. For multi-plugin +
  multi-OS sessions this really wants to be generated per-platform (suffix +
  lib-dir) — either by the scaffold via `configure_file`, or by `ost` staging the
  lib next to `plugInfo.json` at session-setup time.

## 6. Still-open from report #1 (unchanged)
- Version mis-detection persists: the 26.08 runtime is reported as `25.05.01` by
  `runtime pull`/`doctor`/`test`. Confirmed again via `pxrInternal_v0_26_8`
  symbols. The version gate is effectively not enforcing anything.
- `ost runtime show`/`validate` still want `cy2026 --profile usd`, not the full
  runtime id that `runtime list` prints.
