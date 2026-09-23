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

The fix was per-adapter (and the adapters have since left, with their checks,
for `motion-connectors`): each `check_boundaries.py` cross-checks its link
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
| **Standalone installability** | Whether an external consumer has been shown to configure and link against the installed package alone. Three values: **measured**, **unmeasured**, **not applicable** (the identity ships no CMake package). A platform in parentheses — **measured (Windows)** — narrows the first: the run was made, and the row is one whose closure differs by platform, so one host's answer is knowingly half of it. Nothing else may qualify a cell; a measurement that was true about an older package is not a fourth value, it is `unmeasured` with a note. |

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
3. **A config never reaches past its own declared edges.** `motionBvh` depended
   on `motionSource` and therefore did not `find_dependency(pxr)`, although
   OpenUSD's value types arrived through that chain — asserting an edge the
   descriptor does not declare would put WORKSPACE.md §2 and this file in
   disagreement, and §2 wins. Both left with MIG-3; the rule did not.
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

### 3.1 A `requires` range between two releases

*(Added 2026-09-13, from the review of `vrm.computeRestPoseCorrection`.)*

The workspace carries one version (the repo-root `VERSION`), and between two
tags `main` keeps the last one. On 2026-09-13 `VERSION` is `0.8.0`, `v0.8.0` is
tagged, and the OpenExec bundles have landed since. Every descriptor's `requires`
range, `>=0.8,<0.9`, therefore **admits the tagged 0.8.0 package of each
library, although dependents in the tree now use library API that package does
not have**. This is measured with `git diff v0.8.0 -- libs/*/include`:

| Dependent | Needs, added after `v0.8.0` |
| --- | --- |
| `execMotion` | `motionRuntime`'s `operator==` on `PoseSampleResult` (the registry requires it) |
| `execVrm` | `vrmRetarget`'s `operator==` on `TargetJoint`, `TargetSkeleton`, `HumanoidMap`, `RestPoseCorrection` and `RetargetedPose` (the same), and the `JointLocalTransforms` type with its own |
| `motion_retarget` | `vrmRetarget`'s `ExpressionResolver` and `LookAtEvaluator` headers, and the `GetJointWorldTransform` added beside `PoseRetargeter` |

No lane builds any of these against the tagged 0.8.0 package, and one that did
would fail at compile time with a missing symbol, not produce a wrong answer. A
range is a statement about packages, though, and this one is false for the
tagged package until the next bump.

**The rule this imposes: a release that carries library API added since the
last tag is a minor release.** The lockstep bump moves every range with it
(`>=0.9,<0.10`), which excludes the tagged package and makes each range true
again. A patch release (`0.8.1`) would keep `>=0.8,<0.9`, admit the 0.8.0
packages, and ship this gap in the release's own descriptors. SemVer already
requires a minor release for added API; this subsection records the second
consequence, which applies here and not in general. The range cannot be made
true between tags without a pre-release version, and this workspace does not use
one.

*Applied at v0.9.0 (2026-09-17)*: a minor bump, every range moved to
`>=0.9,<0.10`. The table above gained one row's worth before the tag:
`vrmRetarget`'s `TargetJoint::restScale` and `DecomposeRestTransform`, which
`execVrm` and `motion_retarget` both call (the scale policy).

## 4. The packages

### 4.1 Plugin bundles

A plugin bundle is loaded by OpenUSD through its `plugInfo.json`; only one of
them is also a *library* another target links, and it is the only one that ships
a CMake package.

| Package | Exported target | Public headers | Required packages | Platform deps | In product | Standalone |
| --- | --- | --- | --- | --- | --- | --- |
| `vrmSchema` | `vrmSchema::vrmSchema` | `include/vrmSchema/` | `pxr` (guarded on `pxr_FOUND`) | — | yes | **measured** |
| `usdVrmFileFormat` | — | — | — | — | yes | not applicable |
| `usdVrmPackageResolver` | — | — | — | — | yes | not applicable |
| `usdVrmaFileFormat` | — | — | — | — | yes | not applicable |
| `execMotion` | — | — | — | — | yes | not applicable |
| `execVrm` | — | — | — | — | yes | not applicable |

`execMotion` joined the product on 2026-09-06 with its bootstrap (Workspace
Phase 8). It is a bundle in exactly the sense the next paragraph describes —
OpenUSD finds it, and nothing links it — with one addition: what registers is a
computation rather than a type, so its consumer contract is *the computation
resolves on a prim of the schema its plugInfo declares*. A plugInfo that fails to
stage does not fail loudly there; it presents as a computation that does not
exist, which is what `execMotion_mechanism` exists to catch
([the mechanism report](../reports/openusd/26.08-openexec-mechanism.md) §1).

`execVrm` joined on 2026-09-13, under the same contract and with one more
condition on it: one of the schemas its plugInfo declares, `UsdVrmHumanoidAPI`,
is **another bundle's type**. exec resolves that name when it reads the Exec
block, so the consumer contract holds only in a session that also registers
`vrmSchema` — a runtime edge, `requires.bundles`, and not a link one: the bundle
links nothing of `vrmSchema`. Without it the computations on the typed
`UsdSkelSkeleton` still resolve and the ones on the humanoid are not found,
which `execVrm_humanoid_without_schema` measures
([the humanoid report](../reports/openusd/26.08-openexec-humanoid.md) §6).

A second runtime edge joined it the same day, to `execMotion`, and it fails
more quietly. `vrm.humanoidRetarget` reads its pose from
`motion.sampleAnimation`, which `execMotion` registers, and exec drops a target
that provides no computation from a fan-in without a word, where a missing
schema bundle at least posts a coding error. So in a session without
`execMotion` the rig computes, and the bound pose and the retarget are refused
by this bundle's own count and nothing else. `execVrm_retarget_without_exec_motion`
measures it
([the retarget report](../reports/openusd/26.08-openexec-retarget.md) §3).

The three file-format and resolver bundles export no target and install no
config **by design**: nothing links them, OpenUSD discovers them. Their consumer
contract is a different one — the plugin is found, its types register, and a
stage opens — and it is not expressible as `find_package`. That contract has its
own long-standing P0: a dependency bundle's USD registration half is not staged
by `ost`, so a lone `usdVrmFileFormat` package registers the `.vrm` format and
then fails to open a stage (WORKSPACE.md §5, Workspace Phase 5).

`vrmSchema` is the exception because `usdVrmFileFormat` links it — the
`find_package(vrmSchema CONFIG REQUIRED)` in that bundle's own CMakeLists is the
first consumer of this row, and the reason it read *measured* before this track
existed is that the standalone bundle build in CI is exactly that consumer.

**It is now measured from outside as well, from a `cmake --install` prefix.**
`tests/consumer/vrmSchema/` configures, builds, links and runs against a prefix
holding this package alone, with OpenUSD through `--extra-prefix`. The layout it
gets is a bundle's rather than a library's: the shared object lands at
`lib/libvrmSchema.dll` **beside** the import library instead of under `bin/`, so
a consumer of this package on Windows needs this prefix's `lib` on the loader
path where a consumer of `vrmContainer` needs its `bin`. Both are inside the
prefix, which is what the contract promises, and neither is where the other one
is.

**PKG-2's open question stays open for this row.** It asked whether a
`cmake --install` prefix and an extracted `ost` package are the same artifact,
and answered *yes for a plain library* — a bundle is the shape where they could
still differ, because it carries its dependencies' link halves where a library
stages only its own install rules. This run used the first prefix only. What it
establishes is that the `find_package` contract holds for a bundle's layout at
all; `--prefix-source ost-package` against this row is the measurement that
would close the question, and it has not been made.

The link closure is 22 entries — every OpenUSD library a typed schema is built
from — against `vrmContainer`'s empty one, which is the clearest statement in
this document of what `find_dependency(pxr)` is carrying.

### 4.2 Libraries

| Package | Exported target | Public headers | Required packages | Platform deps | In product | Standalone |
| --- | --- | --- | --- | --- | --- | --- |
| `vrmContainer` | `vrmContainer::vrmContainer` | `include/vrmContainer/` | — | — | yes | **measured** |
| `vrmRig` | `vrmRig::vrmRig` | `include/vrmRig/` | `pxr`, `motionCore` | — | yes | **measured** |

**Two more left with MIG-3**, on 2026-09-23: `motionSource` and `motionBvh` are
`usd-motion-plugins`'
[`motionSource`](https://github.com/animu-sphere/usd-motion-plugins/blob/main/libs/motionSource) and
[`motionBvh`](https://github.com/animu-sphere/usd-motion-plugins/blob/main/libs/motionBvh), with the
BVH tools and the producer profiles. Nothing here consumes them — no member
reads a BVH file any more — so no descriptor names them, and their rows and
consumer fixtures went with them rather than becoming consumed rows.

**Two more rows left with MIG-1 and MIG-2**, on 2026-09-21: `motionCore` and
`motionRuntime` are `usd-motion-plugins`'
[`motionCore`](https://github.com/animu-sphere/usd-motion-plugins/blob/main/libs/motionCore),
[`motionSampling`](https://github.com/animu-sphere/usd-motion-plugins/blob/main/libs/motionSampling) and
[`motionRecording`](https://github.com/animu-sphere/usd-motion-plugins/blob/main/libs/motionRecording) now — one library became two
there — and this workspace **consumes** them: five members name them in
`requires.libraries` with a digest per target, and `ost` materializes the
published artifacts before the build. Their contracts are that repository's to
state; what this table still owes them is the consumer's half, which is §5's
criterion 5 read from the other side — every remaining row's closure now
resolves packages this workspace did not build.

**And the retarget, with MIG-2**, on 2026-09-23: the generic half of
`vrmRetarget` is `usd-motion-plugins`'
[`motionRetarget`](https://github.com/animu-sphere/usd-motion-plugins/blob/main/libs/motionRetarget),
consumed by `execVrm` and `motion_retarget` the same way. What stayed is the
`vrmRig` row: the expression resolve, the look-at and VRM 1.0's required
bones. Its closure lost `motionSampling` and `motionRecording` with the code
that used them, and it does not gain `motionRetarget` — the VRM half includes
nothing from the generic half ([WORKSPACE.md §9.5](WORKSPACE.md#95-the-line-through-vrmretarget)).

The rows that remain are what this workspace still *installs*. A consumed
package has no row here and never will: this document is about what a consumer
of **this** repository resolves.

`vrmContainer` is the only `SHARED` library here; every other row is `STATIC`
and defines a `<NAME>_STATIC` compile definition `PUBLIC`, which a consumer
inherits from the imported target and must not set by hand.

**Three rows left this table with MIG-4**, on 2026-09-21: `liveTransport`,
`osc` and `motionTracking` are `motion-connectors`' `motionConnectorTransport`,
`motionConnectorOsc` and `motionConnectorTracking` now
([WORKSPACE.md §9.1](WORKSPACE.md)). Their measurements went with them and are
not repeated here — what this document loses with them is stated rather than
quietly dropped:

- **the only platform difference it carried.** `liveTransport` was the one row
  whose closure differed by platform (`ws2_32` on Windows, `Threads::Threads`
  elsewhere), which is why four cells read *measured (Windows)* until PKG-4's
  lane answered criterion 6. No row here differs by platform any more, so the
  qualifier has no subject left in this workspace.
- **the only empty-edge row.** `osc` was measured first *because* it had no
  `find_dependency` at all, so a failure could only have been its own config
  file. The argument in §5 about the order fixtures were taken in still holds
  as history; the shape it started from is no longer here.
- **the row whose product cell said neither yes nor no.** `motionTracking` was
  on the product side of [WORKSPACE.md §5](WORKSPACE.md)'s split with nothing
  linking it. Its reader exists now, and it is
  [`vrchat_osc_record`](https://github.com/animu-sphere/motion-connectors/blob/main/tools/vrchatOscRecord) in the
  repository that holds both.

**`vrmContainer` already read *measured*, and now it is measured in the second
of the two senses that word carries here.** Three bundles call
`find_package(vrmContainer CONFIG REQUIRED)` in their standalone builds, and CI
runs them — that is a real consumer, and it is what the column meant for this
row. What it is not is a consumer *outside* this workspace: those three are
members of it, built from its tree, and criterion 5 is the one property they
cannot have. `tests/consumer/vrmContainer/` closes that on 2026-08-29, and it is
the first fixture for a `SHARED` package — which is a shape the eight static
rows cannot check, because every criterion up to the link is answered by an
import library and the shared object itself is not opened until the consumer
runs. A package that installed a config, a header root and a `.lib` and forgot
the `.dll` meets criteria 1–4 and exits `0xC0000135` on the line after them.
This one did not: the prefix ships `bin/vrmContainer.dll` beside
`lib/vrmContainer.lib`, criterion 2 names both, and the consumer ran with the
prefix's own `bin` and `lib` on the loader path and nothing else.

**The motion layer's five packages were measured, and the chain matters more
than the count.** `motionCore`, `motionRuntime`, `vrmRetarget`, `motionSource`
and `motionBvh` each configured, built, linked and ran from a prefix holding
their own transitive closure and nothing else, with OpenUSD arriving through the
driver's `--extra-prefix` the way it arrives for anyone else. Two of the five
are consumed packages now and their fixtures went with them, and `vrmRetarget`
is `vrmRig`, whose fixture was rewritten for what stayed; the three findings
below are what that measurement established, and the first two are why the
consumer side still holds.

*Criterion 3 is blind to an external package, and only criterion 4 catches one.*
Removing `find_dependency(pxr)` from `motionCore`'s installed config was
answered by the **build**, not by the closure walk: OpenUSD's imported targets
are unnamespaced — `arch`, `gf`, `tf` — and a walk that refuses an undefined
`::`-qualified entry has nothing to refuse in a bare name, because a raw library
name is the linker's to resolve rather than CMake's. So for the five rows whose
only external edge is `pxr`, the fixture's `#include` of a header that carries a
`Gf` type into the consumer's translation unit is not a nicety: it is the only
thing standing between a missing `find_dependency(pxr)` and a passing run. A
fixture that included a self-contained header would meet all five criteria a
host can check against a package no clean consumer could build.

*A config may not reach past its own edges, so the closure is the installer's to
compute.* `motionBvh` declared `motionSource` and nothing further (§3 rule 3),
and installing that cell literally left `motionSource`'s own configure with
nothing to resolve. It was the one row where the difference showed, because the
rest of the table happened to be listed in topological order, and the one row
measured through a three-package closure the consumer never names. It left with
MIG-3; the driver still computes the closure.

*Two of the five hold their C++ namespace in common, and the package name is not
it.* `motionCore` and `motionRuntime` are both `namespace motion`: the identity
is the artifact's name and the namespace is the layer. A consumer finds that out
from the header, which is one more reason criterion 4 requires including one
rather than only linking.

### 4.3 Adapters — retired 2026-09-21

There is no adapter in this workspace. `vrmAdapterVmc`, `vrmAdapterMocopi` and
`vrmAdapterVrchatOsc` left together with MIG-4 (the moving rule that sends them
as a set, [WORKSPACE.md §9.2](WORKSPACE.md) rule 7) and are
[`motionConnectorVmc`](https://github.com/animu-sphere/motion-connectors/blob/main/libs/motionConnectorVmc),
[`motionConnectorMocopi`](https://github.com/animu-sphere/motion-connectors/blob/main/libs/motionConnectorMocopi) and
[`motionConnectorVrchatOsc`](https://github.com/animu-sphere/motion-connectors/blob/main/libs/motionConnectorVrchatOsc)
now, each with its recorder under that repository's root `tools/`. The reserved
`vrmAdapterArdy` row went with them: the generation adapter is created there.

Their rows said **measured**, and what those measurements established is worth
keeping in one sentence each, because §5's argument below rests on them: five
packages had to resolve before `vrmAdapterVmc`'s target existed, which is where
§3's rule that a config declares its whole `PUBLIC` interface first had
something behind it; `vrmAdapterVrchatOsc` reproduced §1's defect in its second
shape, with the two edges answered at different stages — the transport leaf
through the public header and the decoder only at the link; and
`vrmAdapterMocopi` carried `ws2_32` twice over, once from its own transport edge
and once, capitalised, from OpenUSD's `arch`, which is what a platform
difference looks like when two providers name one library.

An adapter's contract is `motion-connectors`' to state now. What this document
keeps is the shape of the rule that governed them — a package outside the
aggregate product, carrying its CLI in the same artifact — because
[WORKSPACE.md §5](WORKSPACE.md)'s split is still this repository's, and the next
identity that carries a producer's name will meet it again.

## 5. What "standalone" is worth without a lane

**Read this section as dated.** Six of the rows it counts — the three shared
leaves and the three adapters — left this workspace with MIG-4 on 2026-09-21
and are `motion-connectors`' now (§4.2, §4.3). The counts, the platform
qualifier and the adapter-CLI shape below are the record of what was measured
here while they were here; the packages and their fixtures travelled with the
code, and re-stating their numbers from this side would be a claim this
repository can no longer make.

**Measured** in the tables above is a statement about evidence, and as of
2026-08-30 every row that carries a `find_package` contract has it: all twelve
have been configured, built, linked and *run* from a clean prefix by a project
that names no workspace target, and no config file failed. There is no
`unmeasured` cell left in §4.

The order they were taken in is the argument for believing them. `osc` came
first because it has no edge, so a failure there could only be its own config
file; `vrmAdapterVmc` second because it has five, which is where §3's rule that a
config declares its whole `PUBLIC` interface first has something behind it.
After those came the *shapes* neither of them has — the only `SHARED` package,
whose contract is not finished at the link; the only package whose closure
differs by platform; the one whose declared edges are not its closure; the one
whose edges are not all visible from its headers; and the one bundle that ships
a CMake package. A twelfth fixture that looked like the first would have
measured one shape twelve times.

**Criterion 6 was answered on 2026-08-30, and every cell above is now
unqualified.** PKG-4's lane ran all twelve packages on `windows-2022`,
`macos-15` and `ubuntu-24.04`, and the three platforms agree: every workspace
target in every closure is present on all three or on none, and the one
difference this document permits is present in both directions —
`Threads::Threads` on macOS and Linux, `ws2_32` on Windows, for `liveTransport`
and each of the three adapters that inherit it. That closes the raw-library half
of [#113](https://github.com/animu-sphere/usd-vrm-plugins/issues/113): on both
POSIX platforms `vrmAdapterMocopi` carries the threading library and **no**
socket one, which is the absence no Windows run can see.

The four rows that read **measured (Windows)** until then were `liveTransport`
and those three adapters. Nothing about their packages changed; what changed is
that a second and third host looked.

That a document is not the deliverable is the whole reason the lane exists.
Seventeen green lanes did not catch a package naming an unresolvable target, and
they could not have: no lane opens a config file. So the acceptance criteria
below belong to a CI lane, and the roadmap track that built it is
[packaging-hardening.md](../roadmap/packaging-hardening.md). That lane is
[`.github/workflows/package-consumer.yml`](../../.github/workflows/package-consumer.yml),
and §5.1 states what it compares — because *which* differences between three
platforms are this workspace's to answer for is a contract question, and a check
that decided it on its own would be a second contract.

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

**A CLI is not a package, and the driver switches every one of them off.** An
adapter's source directory builds two things — the library whose row is in §4,
and the adapter's CLI, which [WORKSPACE.md](WORKSPACE.md) §2 grants edges the
library is refused. VRC-6 made that concrete: `vrchat_osc_record` links
`motionTracking` and `motionRuntime`, and `vrmAdapterVrchatOsc` requires
neither and must not. Since this check installs each package from its source
tree into a prefix holding **exactly its declared closure** — the emptiness
that lets a mutated config file be attributed to the config file — building the
CLI there would need two packages no row names, which is either a lie in a row
or a prefix the check can no longer reason about. So each adapter carries a
`<PACKAGE>_BUILD_TOOL` option and the driver passes `OFF` for every source tree
with a `tools/` directory. What that leaves unmeasured is stated rather than
hidden: a standalone configure of an adapter *with* its CLI is the shape its
artifact is built from, and `ost library build` is what exercises it.

**It is a rule about edges, and the two files a fixture is built from can create
different ones.** A `CMakeLists.txt` is where an identity becomes a link edge —
`find_package` and `target_link_libraries` are the two ways to consume a
sibling — so naming any package but the one under test is the violation there,
in the fixture's own file and in the shared module it includes. A `.cpp` cannot
create an edge at all. What it can do is `#include` a sibling's header root,
which makes the fixture depend on whatever else the prefix happens to hold
rather than on this package's contract, so that is what is refused there.

The distinction is not a loosening for its own sake: `motionBvh` handed back
`motionSource::SourceSkeleton`, so *every* consumer of it wrote that namespace
whether or not it had ever heard of the package — the type arrived through
`motionBvh`'s own public header, which is exactly what its `find_dependency` is
for. Refusing the spelling would have left the row with the most interesting
closure in this document measured by the weakest fixture in it. The row left
with MIG-3; the rule is kept for the next package that hands back a lower
layer's type.

**Each of the six is verified by having been seen to fail.** A criterion that
has only ever printed *met* is indistinguishable from one that is not checked,
so 1–4 were each broken in the installed prefix — the config deleted, its
targets include removed, its `find_dependency` lines stripped, its header root
deleted — and 5 by edits to the fixture itself, with every mutation asserting it
changed a byte before the run it justifies. For a package with more than one
edge the `find_dependency` mutation also takes the *name* of the edge to remove,
because stripping all of them is caught by the first one resolved and a package
whose fifth edge was missing would pass a mutation aimed at its first. Stripping
all of them is *refused* only when every edge is one another package in the
prefix also declares, which is a fact about the prefix and true on any host.
When the edges carry **conditions** instead, the run is made and a pass ends
inconclusive rather than blaming the fixture: `liveTransport`'s one edge is
inside `if(NOT WIN32)` and is not reached on Windows, while `vrmSchema`'s is
inside `if(NOT pxr_FOUND)` and is reached by every clean consumer — the contract
cell says a condition exists and not which way it falls, so refusing both would
have thrown away a real catch, and `vrmSchema`'s is caught by criterion 4. The mutations are
`--mutate` in that driver, and they break the *prefix* rather than the source
tree, so nothing in this loop depends on a `git stash` that might be a silent
no-op.

### 5.1 What criterion 6 compares, and what it does not

Criterion 6 says *the three platforms agree about the package closure, or a
documented platform difference says why not*. Left there, it is unimplementable
in the strict reading and vacuous in the loose one: the three runtimes are three
separate builds of OpenUSD, so an entry-for-entry comparison would report a
difference between two upstream builds as a defect in a config file here — and
a comparison that shrugged at any difference would have nothing to catch.

So a closure entry is one of three things, and each takes its own rule. This is
the contract half of `scripts/check_package_closures.py`, which is what the lane
runs; the script reads the tables in §4 rather than keeping its own copy of them,
on the same rule the driver follows.

| Class of entry | Example | The rule |
| --- | --- | --- |
| A **workspace target** | `motionCore::motionCore` | Present on all three platforms or on none. A workspace package arrives through a config file in this repository, and that is the same file everywhere, so a difference here has no qualifier available to it. |
| A **declared platform dependency** | `ws2_32`, `Threads::Threads` | The `Platform deps` cell names it and says where it applies. Present exactly on the platforms named, and **absent on the others** — the second half is the check, not a formality. |
| **Everything else** | `arch`, `gf`, `Dbghelp.lib`, `TBB::tbb` | Attributed, not compared. It arrives from a required package this workspace does not produce, and what a `pxr` build puts on its own link line is not a promise made here. |

The third row would be a hole if the attribution were assumed, so it is checked:
**a package whose contract closure reaches no external required package must
carry none of these at all.** That is a strong statement about exactly the rows
where it can be strong — `osc`, `vrmContainer` and `liveTransport` have no `pxr`
anywhere in their closure, so a foreign entry in one of them is a defect with
nothing to blame it on. For the rows that do reach OpenUSD, the differences are
recorded in the lane's output rather than failed, and the reason is stated where
a reviewer reads it.

**That set was four rows until 2026-08-30 and is now three.**
`vrmAdapterVrchatOsc` left it by taking the `motionCore` edge in VRC-3, which is
the transition this section describes rather than a hole in it: the strong
statement is available to a package for exactly as long as its contract closure
stays inside this workspace, and an adapter that produces a canonical value has
left that condition on purpose. The nine rows that reach OpenUSD are the nine
this document's other cells already say do.

Two rules hold for every entry whatever its class, because each is a package
exporting its build rather than its interface: **no absolute path**, and **no
workspace identity spelled as anything but its exported target** — a bare
`motionCore` or a `libmotionCore.a` on a link closure is a package that resolved
a sibling by file rather than by contract.

The absence half of the second row is where
[#113](https://github.com/animu-sphere/usd-vrm-plugins/issues/113) closes. On
Windows the socket library is present and `vrmAdapterMocopi`'s Windows run
proves the imported-target half; what the issue is actually about is a POSIX
host linking the threading library and *no* socket one, and that is a
measurement no Windows host can make. It is checked here rather than remembered
([the track](../roadmap/packaging-hardening.md) PKG-5).

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
