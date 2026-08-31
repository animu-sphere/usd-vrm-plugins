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
| `vrmSchema` | `vrmSchema::vrmSchema` | `include/vrmSchema/` | `pxr` (guarded on `pxr_FOUND`) | — | yes | **measured** |
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
| `motionCore` | `motionCore::motionCore` | `include/motionCore/` | `pxr` | — | yes | **measured** |
| `motionRuntime` | `motionRuntime::motionRuntime` | `include/motionRuntime/` | `pxr`, `motionCore` | — | yes | **measured** |
| `vrmRetarget` | `vrmRetarget::vrmRetarget` | `include/vrmRetarget/` | `pxr`, `motionCore`, `motionRuntime` | — | yes | **measured** |
| `motionSource` | `motionSource::motionSource` | `include/motionSource/` | `pxr`, `motionCore` | — | yes | **measured** |
| `motionBvh` | `motionBvh::motionBvh` | `include/motionBvh/` | `motionSource` | — | yes | **measured** |
| `liveTransport` | `liveTransport::liveTransport` | `include/liveTransport/` | `Threads` (non-Windows) | `ws2_32` (Windows), `Threads::Threads` (elsewhere) | **no** | **measured** |
| `osc` | `osc::osc` | `include/osc/` | — | — | **no** | **measured** |
| `motionTracking` | `motionTracking::motionTracking` | `include/motionTracking/` | `pxr`, `motionCore` | — | not yet | **measured** |

`vrmContainer` is the only `SHARED` library here; every other row is `STATIC`
and defines a `<NAME>_STATIC` compile definition `PUBLIC`, which a consumer
inherits from the imported target and must not set by hand.

**`motionTracking`'s row is the first whose *product* cell says neither yes nor
no.** It is on the product side of [WORKSPACE.md §5](WORKSPACE.md)'s split — it
names no product, opens nothing, and a product tool can link it, which is the
question `osc` fails — and nothing links it **yet**, in or out of the product:
§2's permission is `adapters/*/tools/* -> motionTracking` and no CLI has taken
it, so this fixture is currently its only reader. A `no` there would read as the exclusion the
two shared leaves carry, and a `yes` would claim a member that does not exist, so
the cell says what is true.

**Its required-package cell changed on 2026-08-31, and it is the only cell that
did.** Until VRC-5 the row was `osc`'s measurement repeated — three empty
columns, no workspace edge, no `find_dependency` at all. The solve took the one
edge [WORKSPACE.md §2](WORKSPACE.md) grants this library, so the config now
carries `motionCore` and, with it, the `pxr` guard every row that names a Gf
type carries. **The platform column stayed empty**, which is the half worth
saying out loud: what this library gained is a value type, not an ability, and
nothing here opens or waits on anything. Re-measured the same day from a
`cmake --install` prefix — configure, build, link and **run**, against a prefix
holding `motionCore` and nothing else of this workspace's.

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

**`liveTransport`'s closure is exactly the one entry the table predicts, and a
Windows run is the weaker half of its measurement.** `tests/consumer/liveTransport/`
records a closure of `ws2_32` and nothing else — no workspace package, no
threading target — which is the Windows side of the one documented platform
difference in this document. The POSIX side is worth more, because there the
same fixture verifies the *absence* of the socket link as well as the presence
of the threading one, and no host can check both ([the track](../roadmap/packaging-hardening.md)
PKG-5). The `Standalone` cell therefore says **measured (Windows)** rather than
**measured**: an unqualified word there would claim a platform agreement that
only PKG-4's lane can produce.

*Which* call a fixture makes is a packaging decision for this row in a way it is
not for the others. This is a static library, so the archive member carrying the
platform's socket calls is linked only when the consumer needs it — a fixture
that called the diagnostic vehicle alone would link a package whose platform
link line was missing entirely and report criterion 4 met. The fixture therefore
calls into `UdpReceiver`, and it calls the one thing there that needs no socket:
`Receive` on a receiver that was never opened returns `Closed`. Nothing binds
and no port is named, because a packaging fixture that took a port would go red
on a host where something else already held it, which is a fact about the
machine rather than about the package.

**The motion layer's five packages are measured, and the chain matters more
than the count.** `motionCore`, `motionRuntime`, `vrmRetarget`, `motionSource`
and `motionBvh` each configured, built, linked and ran from a prefix holding
their own transitive closure and nothing else, with OpenUSD arriving through the
driver's `--extra-prefix` the way it arrives for anyone else. Three things came
out of it.

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
compute.* `motionBvh` declares `motionSource` and nothing further (§3 rule 3),
and installing that cell literally leaves `motionSource`'s own configure with
nothing to resolve. It is the one row where the difference shows, because the
rest of the table happens to be listed in topological order — and it is now the
one row measured through a three-package closure the consumer never names.

*Two of the five hold their C++ namespace in common, and the package name is not
it.* `motionCore` and `motionRuntime` are both `namespace motion`: the identity
is the artifact's name and the namespace is the layer. A consumer finds that out
from the header, which is one more reason criterion 4 requires including one
rather than only linking.

### 4.3 Adapters

An adapter is a plain library under `adapters/`, never in the aggregate product
(WORKSPACE.md §5), and its artifact carries its CLI with it.

| Package | Exported target | Public headers | Required packages | Platform deps | In product | Standalone |
| --- | --- | --- | --- | --- | --- | --- |
| `vrmAdapterVmc` | `vrmAdapterVmc::vrmAdapterVmc` | `include/vrmAdapterVmc/` | `pxr`, `motionCore`, `motionRuntime`, `liveTransport`, `osc` | inherited from `liveTransport` | no | **measured** |
| `vrmAdapterMocopi` | `vrmAdapterMocopi::vrmAdapterMocopi` | `include/vrmAdapterMocopi/` | `pxr`, `motionCore`, `motionRuntime`, `liveTransport` | inherited from `liveTransport` | no | **measured** — including the raw-library half of [#113](https://github.com/animu-sphere/usd-vrm-plugins/issues/113), on both POSIX platforms |
| `vrmAdapterVrchatOsc` | `vrmAdapterVrchatOsc::vrmAdapterVrchatOsc` | `include/vrmAdapterVrchatOsc/` | `pxr`, `motionCore`, `liveTransport`, `osc` | inherited from `liveTransport` | no | **measured** |
| `vrmAdapterArdy` | reserved | reserved | reserved | — | no | not applicable |

`vrmAdapterVmc` is the second row to say **measured**, on 2026-08-29, and it is
the first one measured with edges: five packages must resolve before its target
links, and the consumer names none of them (`tests/consumer/vrmAdapterVmc/`).
Three things that run said, none of which `osc` could have.

**The closure a consumer inherits is fifteen entries, and four of them are this
package's.** On Windows: `motionCore::motionCore`, `motionRuntime::motionRuntime`,
`liveTransport::liveTransport`, `osc::osc`, OpenUSD's `arch`, `gf`, `tf`,
`boost`, `python`, `TBB::tbb` and `Python3::Python`, and the platform names
`Dbghelp.lib`, `Shlwapi.lib`, `Ws2_32.lib` and `ws2_32`. The last two are the
same library spelled by two different providers — OpenUSD's `arch` writes it
capitalised, `liveTransport` in lower case — and that pair is the clearest
statement of what criterion 6 is for: on a POSIX host the second becomes
`$<LINK_ONLY:Threads::Threads>` (§3 rule 4) and the first disappears with the
rest of the Windows set, so this is a difference a lane must expect rather than
a defect it should report. Nothing here reaches `vrmContainer`, `vrmSchema` or a
sibling adapter, which is WORKSPACE.md §2 observed from outside the workspace
for the first time.

**Criterion 4 exercises three include roots, not one.** The header the fixture
includes carries `motionCore/Humanoid.h` and two OpenUSD `Gf` headers into the
consumer's translation unit, so a config that resolved this package's target and
left a required package unresolved fails at the first `#include` rather than at
link time. That is a property of *this* package's public headers rather than a
rule, and it is why the fixture includes that header and not a self-contained
one.

**The §1 defect was reproduced in its own shape and caught.** Removing every
`find_dependency` from the installed config is caught by whichever edge the
closure walk reaches first — `motionCore`, here — which says nothing about the
fifth. `--mutate no-dependency --dependency osc` removes exactly the block the
OSC-3 fix added and leaves the other four, and criterion 3 refuses it by name:
*`osc::osc` is on the link closure of `vrmAdapterVmc::vrmAdapterVmc` and no
package has defined it*. That is the failure every one of the 17 lanes was
structurally unable to produce.

**`vrmAdapterVrchatOsc` gained `pxr` and `motionCore` on 2026-08-30, and the
paragraph this replaces predicted it.** That paragraph said the two rows were
absent because the library held no canonical value, that they would "arrive with
the code that produces one", and that this table was where a reviewer should
notice. VRC-3 is that code — the sender's axes into the canonical basis, which
is a `GfVec3f` and a `GfQuatf` — so the row grew by two and the closure grew to
reach OpenUSD's value-type layer.

`motionRuntime` is still absent, and that absence is the same kind of statement
the other two used to be: it is what an adapter takes when it produces a
**pose**, and a tracker observation is pre-IK. It arrives with VRC-5's solve or
it does not arrive at all.

**Nothing here was caught by a check, and that is worth one sentence.**
`scripts/check_docs.py` refuses a `*Config.cmake.in` with no row and a row
naming no package; it does not compare a row's *required packages* against the
`find_dependency` calls in the config it points at. This row was updated by
hand, in the change that made it wrong, and the driver below is what proved the
update rather than the review — since `check_package_consumer.py` reads this
table to build the prefix, a row that under-states its edges installs too little
and the consumer fails to configure. That is a good failure mode and not a
substitute for the check.

**The cell says *measured* because it was measured again**, on 2026-08-30 with
the new edge in place: `python scripts/run_package_consumer_lane.py --package
vrmAdapterVrchatOsc` on a Windows workstation, criteria 1–5 met, criterion 6
left to PKG-4's lane. The closure it records is **fourteen** entries, where the
pre-VRC-3 one reached no OpenUSD at all — `Dbghelp.lib`, `Python3::Python`,
`Shlwapi.lib`, `TBB::tbb`, `Ws2_32.lib`, `arch`, `boost`, `gf`,
`liveTransport::liveTransport`,
`motionCore::motionCore`, `osc::osc`, `python`, `tf`, `ws2_32` — which is the
same shape `vrmAdapterVmc`'s carries, double `ws2_32` spelling included: once
from this package's transport edge and once, capitalised, from OpenUSD's `arch`.
A measurement that was true about an older package is not a fourth value for
this cell (§3), so re-running it was the requirement rather than the courtesy.

**All three adapters are measured, and all three cells say *(Windows)*.** Every
adapter inherits its platform dependency from `liveTransport`, which is the one
row in this document whose closure differs by platform — so a Windows run of any
of them is knowingly half of the measurement, and PKG-4's lane is the other
half. `vrmAdapterVmc`'s cell was written before that qualifier existed and is
corrected here; nothing about its measurement changed.

**`vrmAdapterVrchatOsc` is the second half of the §1 defect, and it was
reproduced in its own shape too.** `--mutate no-dependency --dependency osc`
removes exactly the block the OSC-3 fix added to *this* config and leaves the
transport edge in place, and criterion 3 refuses it by name. The same defect,
in the same shape, in the second of the two packages that shipped it — and the
fixture that catches it needs both of this package's edges, because they are
answered at different stages: the transport leaf arrives through the public
header and the decoder only at the link, where `InventoryAddresses` pulls it in.
A fixture that built a capture and stopped would have measured one of the two.

**`vrmAdapterMocopi`'s cell says *measured* rather than *stale*, and the
difference is exactly one half of [#113](https://github.com/animu-sphere/usd-vrm-plugins/issues/113).**
Its closure is fourteen entries and carries `ws2_32` twice over: once from this
package's own transport edge and once, capitalised, from OpenUSD's `arch` — the
same double spelling `vrmAdapterVmc` records, which is what a platform
difference looks like when two providers name one library. That is the
imported-target half of the issue, now measured rather than predicted. The
raw-library half is not, and cannot be here: on Windows the socket library is
*present*, and what the issue is about is whether a POSIX host links the
threading library and no socket one ([the track](../roadmap/packaging-hardening.md)
PKG-5).

## 5. What "standalone" is worth without a lane

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

The distinction is not a loosening for its own sake: `motionBvh` hands back
`motionSource::SourceSkeleton`, so *every* consumer of it writes that namespace
whether or not it has ever heard of the package — the type arrives through
`motionBvh`'s own public header, which is exactly what its `find_dependency` is
for. Refusing the spelling would have left the row with the most interesting
closure in this document measured by the weakest fixture in it.

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
