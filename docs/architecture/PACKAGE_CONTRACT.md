# Package contract

What every distributable package in this workspace promises to a consumer that
has **only the installed prefix** — no source tree, no workspace, no `ost`.

[WORKSPACE.md](WORKSPACE.md) is the binding contract for *identity*: which
bundles and libraries exist, which edges they may have, and what their artifacts
are named. This document is the binding contract for *distribution*: for each of
those identities, the package name a consumer writes in `find_package`, the
target it links, the headers it may include, and the packages that must resolve
before either works. The split is deliberate — §5 of WORKSPACE.md answers "what
is this artifact called", and a reader kept arriving at it with the question
"what do I write to consume it", which it did not answer.

Everything here is a promise to a consumer **outside** the repository. A claim
in this document is met when a clean prefix and a fixture project that names no
workspace target can configure, build and link against it — see
[roadmap/packaging-hardening.md](../roadmap/packaging-hardening.md) for the lane
that will check every package, `scripts/check_package_consumer.py` for the
driver that checks one today, and §5 below for why prose is not enough.

## 1. Why this document exists

**A composed build resolves every target in-tree and never opens a config
file.** That is the whole of it, and it produced a real defect on 2026-08-29:
the OSC-3 extraction gave `vrmAdapterVmc` and `vrmAdapterVrchatOsc` a
`PUBLIC osc::osc`, and neither installed package config gained a
`find_dependency(osc)`. Both packages then named a target no consumer could
resolve — CMake does not search for an imported target even with that package's
own config sitting in the same prefix — and **all 17 CI lanes were green**,
because the workspace build and `ost library build` both had `osc::osc` already
defined as an alias in the same CMake project.

The fix was per-adapter: each `check_boundaries.py` now cross-checks its link
line against its config template, verified by injection in both directions. That
closes the class for the three adapters and nowhere else, and it checks a
*template against a link line* rather than a package against a consumer. The
generalisation is this table plus a lane that configures a real consumer, and
until that lane exists the "standalone installability" column below states what
has been measured rather than what is intended.

## 2. What a package contract states

Seven fields, and each of them is something a consumer can be wrong about:

| Field | Meaning |
| --- | --- |
| **Package name** | The argument to `find_package(<name> CONFIG REQUIRED)`. It is the identity from WORKSPACE.md §1, unmodified — this workspace publishes no package under a name that is not an identity. |
| **Exported target** | The namespaced imported target a consumer links. Always `<name>::<name>`; the in-tree alias and the exported name are kept identical so a consumer's `target_link_libraries` line is the same in and out of the workspace. |
| **Public headers** | The include root the package installs, always `include/<name>/`. A header outside that directory is private whatever its file permissions say. |
| **Required packages** | What the config must `find_dependency` before its targets file is included. This is exactly the set of `PUBLIC`/`INTERFACE` workspace edges, and the rule in §3 makes it derivable rather than remembered. |
| **Platform dependencies** | Non-workspace libraries that travel with the target's `INTERFACE_LINK_LIBRARIES`. These are not workspace edges (WORKSPACE.md §2 does not govern them) but a consumer's link line fails without them. |
| **In the aggregate product** | Whether the identity is a member of `usd-vrm-plugins-<version>-<target>-plugin-product.tar.zst`. The rule and its reasoning are WORKSPACE.md §5; this column only records the answer. |
| **Standalone installability** | Whether an external consumer has been shown to configure and link against the installed package alone. Three values: **measured**, **unmeasured**, **not applicable** (the identity ships no CMake package). |

## 3. The rules a config file follows

These are what make the table below derivable from the CMake sources rather than
maintained beside them, which is the only version of this document that can stay
true.

1. **Every `PUBLIC` or `INTERFACE` workspace target on a link line has a
   `find_dependency` in that package's config.** This is the rule OSC-3 broke.
   `PRIVATE` links on a static library are still interface-propagated by CMake
   and take the same rule; `PRIVATE` on a shared library does not.
2. **A dependency is resolved once, guarded by its target.** Every config wraps
   its `find_dependency` in `if(NOT TARGET <name>::<name>)`, because a consumer
   that already resolved the package must not resolve it twice. `pxr` takes the
   same guard for a stronger reason: `pxrConfig.cmake` unconditionally
   re-creates imported targets such as `TBB::tbb`, so a second inclusion is an
   error rather than a no-op.
3. **A config never reaches past its own declared edges.** `motionBvh` depends
   on `motionSource` and therefore does not `find_dependency(pxr)`, although
   OpenUSD's value types arrive through that chain — asserting an edge the
   descriptor does not declare would put WORKSPACE.md §2 and this file in
   disagreement, and §2 wins.
4. **A platform dependency belongs to the library that uses it, not to its
   consumers.** `liveTransport` links `ws2_32` (Windows) or `Threads::Threads`
   (elsewhere) `PUBLIC` and its config resolves `Threads` itself; the three
   adapters that link it declare neither. `ws2_32` needs no `find_dependency`
   because it is a raw library name rather than an imported target — which is
   also why it is the half of [#113](https://github.com/animu-sphere/usd-vrm-plugins/issues/113)
   that a Windows run cannot check.
5. **Two libraries with no edge between them bring nothing for each other.**
   WORKSPACE.md §2 forbids an edge between `liveTransport` and `osc` in both
   directions, so a consumer of both resolves both. `vrmAdapterVmc`'s config
   does exactly that, in two separate guarded blocks.

## 4. The packages

### 4.1 Plugin bundles

A plugin bundle is loaded by OpenUSD through its `plugInfo.json`; only one of
them is also a *library* another target links, and it is the only one that ships
a CMake package.

| Package | Exported target | Public headers | Required packages | Platform deps | In product | Standalone |
| --- | --- | --- | --- | --- | --- | --- |
| `vrmSchema` | `vrmSchema::vrmSchema` | `include/vrmSchema/` | `pxr` (guarded on `pxr_FOUND`) | — | yes | measured |
| `usdVrmFileFormat` | — | — | — | — | yes | not applicable |
| `usdVrmPackageResolver` | — | — | — | — | yes | not applicable |
| `usdVrmaFileFormat` | — | — | — | — | yes | not applicable |
| `execMotion` | — | — | — | — | reserved | not applicable |
| `execVrm` | — | — | — | — | reserved | not applicable |

The three file-format and resolver bundles export no target and install no
config **by design**: nothing links them, OpenUSD discovers them. Their consumer
contract is a different one — the plugin is found, its types register, and a
stage opens — and it is not expressible as `find_package`. That contract has its
own long-standing P0: a dependency bundle's USD registration half is not staged
by `ost`, so a lone `usdVrmFileFormat` package registers the `.vrm` format and
then fails to open a stage (WORKSPACE.md §5, Workspace Phase 5).

`vrmSchema` is the exception because `usdVrmFileFormat` links it — the
`find_package(vrmSchema CONFIG REQUIRED)` in that bundle's own CMakeLists is the
first consumer of this row, and the reason it is *measured* is that the
standalone bundle build in CI is exactly that consumer.

### 4.2 Libraries

| Package | Exported target | Public headers | Required packages | Platform deps | In product | Standalone |
| --- | --- | --- | --- | --- | --- | --- |
| `vrmContainer` | `vrmContainer::vrmContainer` | `include/vrmContainer/` | — | — | yes | measured |
| `motionCore` | `motionCore::motionCore` | `include/motionCore/` | `pxr` | — | yes | unmeasured |
| `motionRuntime` | `motionRuntime::motionRuntime` | `include/motionRuntime/` | `pxr`, `motionCore` | — | yes | unmeasured |
| `vrmRetarget` | `vrmRetarget::vrmRetarget` | `include/vrmRetarget/` | `pxr`, `motionCore`, `motionRuntime` | — | yes | unmeasured |
| `motionSource` | `motionSource::motionSource` | `include/motionSource/` | `pxr`, `motionCore` | — | yes | unmeasured |
| `motionBvh` | `motionBvh::motionBvh` | `include/motionBvh/` | `motionSource` | — | yes | unmeasured |
| `liveTransport` | `liveTransport::liveTransport` | `include/liveTransport/` | `Threads` (non-Windows) | `ws2_32` (Windows), `Threads::Threads` (elsewhere) | **no** | unmeasured |
| `osc` | `osc::osc` | `include/osc/` | — | — | **no** | **measured** |

`vrmContainer` is the only `SHARED` library here; every other row is `STATIC`
and defines a `<NAME>_STATIC` compile definition `PUBLIC`, which a consumer
inherits from the imported target and must not set by hand.

`osc`'s row is empty in three columns and that is the measurement rather than an
oversight: one source file, two headers, no workspace edge and no platform
library, which is why its archive is seven files where the transport leaf's is
nine (WORKSPACE.md §5).

It is also the first row in this document to say **measured**, on 2026-08-29,
and it says so because an external consumer configured, built, linked and *ran*
against it from a prefix holding those seven files and nothing else
(`tests/consumer/osc/`, driven by `scripts/check_package_consumer.py`). Its
empty edge set is what made it the right one to measure first: with no
`find_dependency` to resolve, a failure could only have been the config file
itself, so the run says something about the fixture as well as about the
package. What it does *not* say is anything about criterion 6 — one host cannot
answer whether three agree, and PKG-4's lane is where that column stops being
about a workstation.

### 4.3 Adapters

An adapter is a plain library under `adapters/`, never in the aggregate product
(WORKSPACE.md §5), and its artifact carries its CLI with it.

| Package | Exported target | Public headers | Required packages | Platform deps | In product | Standalone |
| --- | --- | --- | --- | --- | --- | --- |
| `vrmAdapterVmc` | `vrmAdapterVmc::vrmAdapterVmc` | `include/vrmAdapterVmc/` | `pxr`, `motionCore`, `motionRuntime`, `liveTransport`, `osc` | inherited from `liveTransport` | no | unmeasured |
| `vrmAdapterMocopi` | `vrmAdapterMocopi::vrmAdapterMocopi` | `include/vrmAdapterMocopi/` | `pxr`, `motionCore`, `motionRuntime`, `liveTransport` | inherited from `liveTransport` | no | **stale** — measured before the receiver added a platform link ([#113](https://github.com/animu-sphere/usd-vrm-plugins/issues/113)) |
| `vrmAdapterVrchatOsc` | `vrmAdapterVrchatOsc::vrmAdapterVrchatOsc` | `include/vrmAdapterVrchatOsc/` | `liveTransport`, `osc` | inherited from `liveTransport` | no | unmeasured |
| `vrmAdapterArdy` | reserved | reserved | reserved | — | no | not applicable |

**`vrmAdapterVrchatOsc` requires no `pxr`, `motionCore` or `motionRuntime`, and
that is its current shape rather than an omission.** It ships no semantic
decoder, so nothing in the library holds a canonical value; those three rows
arrive with the code that produces one, and this table is where a reviewer
should notice they are missing when it does.

## 5. What "standalone" is worth without a lane

**Unmeasured** in the tables above is a statement about evidence, not a
prediction of failure. Each of those configs is written to the rules in §3 and
was reviewed against them; as of 2026-08-29 one of them — `osc` — has also been
configured from a clean prefix by a project that names no workspace target, and
the rest have not.

That distinction is the whole reason this document is not the deliverable.
Seventeen green lanes did not catch a package naming an unresolvable target, and
they could not have: no lane opens a config file. So the acceptance criteria
below belong to a CI lane, and the roadmap track that builds it is
[packaging-hardening.md](../roadmap/packaging-hardening.md).

A package meets this contract when, from an install prefix containing it and its
required packages **and nothing else**:

1. `find_package(<name> CONFIG REQUIRED)` succeeds;
2. every imported target the config's targets file defines resolves;
3. every `PUBLIC`/`INTERFACE` dependency resolves, transitively;
4. a consumer that includes one public header and links `<name>::<name>` builds
   and links;
5. the consumer's CMakeLists names no target from this repository's source tree;
6. Windows, macOS and Linux agree about the package closure, or a documented
   platform difference says why not.

Criterion 5 is the one that makes the others mean anything, and it is the one a
fixture inside the workspace loses by accident. `tests/consumer/` therefore
holds fixtures that nothing in the workspace builds, and
`scripts/check_package_consumer.py` copies the tree outside the repository
before configuring it, so the accident is not available to be made. That driver
reports 1–5 for one named package on one host; 6 is the criterion that needs
three, and PKG-4's lane is where the closure this one records gets compared.

**Each of the six is verified by having been seen to fail.** A criterion that
has only ever printed *met* is indistinguishable from one that is not checked,
so 1–4 were each broken in the installed prefix — the config deleted, its
targets include removed, its `find_dependency` lines stripped, its header root
deleted — and 5 by three edits to the fixture itself, with every mutation
asserting it changed a byte before the run it justifies. The mutations are
`--mutate` in that driver, and they break the *prefix* rather than the source
tree, so nothing in this loop depends on a `git stash` that might be a silent
no-op.

## 6. Changing this document

A change to a package's name, exported target, header root, or required
packages is a **contract change** and follows WORKSPACE.md §6's rule: it lands
in this document in the same PR as the CMake change, never after it. Adding a
`PUBLIC` workspace link without adding the matching `find_dependency` and the
row here is the defect §1 describes, and the per-adapter boundary checks are
what catch it today.

A new identity arrives in WORKSPACE.md §1 first — that document decides whether
something exists and what it may depend on. It arrives here when it acquires an
installed package, which for a reserved identity is later or never.
