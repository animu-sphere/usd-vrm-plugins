# Packaging hardening — the installed-package consumer lane

**Status:** **PKG-0 through PKG-5 done** — twelve of twelve packages consumed
from outside the workspace on all three OS, and the three platforms agree about
the closure (2026-08-30) · **Target:** v0.8.0 ·
**Contract:** [architecture/PACKAGE_CONTRACT.md](../architecture/PACKAGE_CONTRACT.md)

The workspace has finished splitting. `vrmSchema`, `usdVrmFileFormat`,
`usdVrmPackageResolver` and `vrmContainer` are separate; the motion layer is
five libraries and four CLIs; the live half is three adapters over two shared
leaves. Every one of those boundaries is checked *inside* the workspace — and
none of them is checked from outside it.

This track closes that, and it comes before the third adapter's decoder, before
NPZ/AMASS, and before OpenExec, for a reason that is measured rather than
argued: **the boundaries this repository spent five phases creating are only
real if someone outside can consume them, and nothing here has ever tried.**

## 1. The defect that dated this track

On 2026-08-29 the OSC-3 extraction gave `vrmAdapterVmc` and
`vrmAdapterVrchatOsc` a `PUBLIC osc::osc`. Neither installed package config
gained a `find_dependency(osc)`. Both installed packages therefore named an
imported target no consumer could resolve — CMake does not go looking for it,
even with `oscConfig.cmake` sitting in the same prefix — and **all 17 lanes were
green.**

They were green because they had to be. A composed workspace build defines
`osc::osc` as an alias in the same CMake project, and `ost library build`
composes `requires.libraries` the same way; neither opens a config file at any
point. The configuration that fails is the one no lane runs.

The fix that landed is per-adapter and narrow on purpose: each adapter's
`check_boundaries.py` now cross-checks its link line against its config
template, verified by injection in both directions. It catches the recurrence in
three directories. It does not catch it in `libs/`, it does not check a
transitive closure, and it compares a template against a link line rather than a
package against a consumer.

**The general fix is a consumer that is not us.**

## 2. The shape

```text
producer workspace
      │
      ├── build
      └── install / package
                │
          clean prefix          (outside the repository, nothing else in it)
                │
        external consumer       (its own CMake project, its own build tree)
                │
          find_package()
                │
           build / link
```

Two properties make this different from every check that exists today, and both
are easy to lose by accident:

- **The prefix is clean.** It holds the package under test and its required
  packages, and nothing else. A prefix that also holds the workspace's build
  tree proves nothing, because CMake will find targets there.
- **The consumer names no workspace target.** Its `CMakeLists.txt` contains
  `find_package(<name> CONFIG REQUIRED)` and `target_link_libraries(consumer
  PRIVATE <name>::<name>)`, and nothing that resolves any other way. A fixture
  added *inside* the workspace tree loses this the first time someone runs it
  from the root build.

## 3. What is covered, and the one correction the plan needs

Fourteen identities were named as the minimum set. **Three of them ship no CMake
package, and cannot**, so they take a different consumer contract rather than a
weaker version of this one:

| Identity | Consumer contract |
| --- | --- |
| `vrmSchema`, `vrmContainer`, `motionCore`, `motionRuntime`, `vrmRetarget`, `motionSource`, `motionBvh`, `liveTransport`, `osc`, `vrmAdapterVmc`, `vrmAdapterMocopi`, `vrmAdapterVrchatOsc` | `find_package` — the lane this track builds. Twelve packages. |
| `usdVrmFileFormat`, `usdVrmPackageResolver`, `usdVrmaFileFormat` | **Plugin load, not `find_package`.** They export no target and install no config by design: nothing links them, OpenUSD discovers them through `plugInfo.json`. Their consumer contract is "the plugin registers and a stage opens", which `scripts/clean_install_smoke.py` already exercises from packaged artifacts. |

The second row is not a gap this track leaves open — it is a contract that is
already gated, by a different mechanism, and merging the two would replace a
working check with a `find_package` that has nothing to find.
[PACKAGE_CONTRACT.md §4.1](../architecture/PACKAGE_CONTRACT.md) records both
shapes. `usdVrmaFileFormat` was not in the original fourteen-package list and
belongs in this row for the same reason as the two beside it; `vrmSchema` is the
one plugin bundle in the *first* row, because `usdVrmFileFormat` links it and so
it does ship a CMake package.

So: **twelve `find_package` consumers**, plus the plugin-load gate that exists.

## 4. Milestones

### PKG-0 — the contract document ✅

[PACKAGE_CONTRACT.md](../architecture/PACKAGE_CONTRACT.md) states, per package,
the name a consumer writes, the target it links, the header root, the required
packages, the platform dependencies, aggregate membership, and whether
standalone installability has been *measured* or only *reviewed*. Written
2026-08-29, from the CMake sources rather than from intent — which is what makes
the `unmeasured` column honest instead of aspirational.

### PKG-1 — one consumer fixture, one package ✅

The smallest thing that can fail correctly. `tests/consumer/` holds one CMake
project per package under test:

```cmake
cmake_minimum_required(VERSION 3.24)
project(vrmAdapterVmcConsumer LANGUAGES CXX)

find_package(vrmAdapterVmc CONFIG REQUIRED)

add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE vrmAdapterVmc::vrmAdapterVmc)
```

`main.cpp` includes one public header and calls one function from it, because a
fixture that only links proves the config file and not the header install.

Start with `osc`: it has an empty edge set, so it is the one package where a
failure can only be the config file itself. Then `vrmAdapterVmc`, which is the
package the §1 defect was actually in — and the fixture must be shown to **fail
against the pre-fix config** before it is trusted, in the way every negative
verification here is trusted or not.

**Done 2026-08-29 for `osc`.** `tests/consumer/osc/` configures, builds, links
and runs against a prefix holding the package's seven files and nothing else,
and `osc`'s row in PACKAGE_CONTRACT.md §4.2 now reads *measured*. Three things
came out of it that the plan above did not predict.

*The criteria are shared rather than copied.* `tests/consumer/ConsumerCriteria.cmake`
holds the `find_package`, the target and archive resolution, and the transitive
closure walk, and each fixture passes it a package name. Twelve fixtures each
writing their own would be twelve chances for one to check less than the others
and still print a pass — and a check that was never run is the failure mode this
whole track exists for, not a check that ran and said no. The module names no
workspace target, so the only identity that appears in a fixture is the one the
fixture is for, which is what makes criterion 5 mechanical.

*The negative verification is four mutations of the prefix, not of the source.*
`--mutate` deletes the config (criterion 1 fails), removes its targets include
(2), strips its `find_dependency` lines (3), or deletes its header root (4), and
requires the run to go red — a pass against a broken package is reported as the
fixture being untrustworthy rather than as a success. Nothing stashes or reverts
a tracked file, so the silent-no-op trap that class of verification usually
carries does not apply. `no-dependency` **refuses to run against `osc`** and
says why: an empty required-package set has nothing to strip, and a mutation
that matched nothing and reported a catch is the same lie in a smaller package.
Criterion 5 was verified the same way, by three edits to the fixture — a sibling
identity on the link line, an `add_subdirectory` into the source tree, and a
`main.cpp` that stopped including a public header — each caught, with the files
restored byte-exact afterwards.

Only criteria **1–4** count as a catch, because those are the four the prefix
can break. Criterion 5 is settled before the mutation is applied, so counting it
would let an already-broken fixture record a verification whose effect was never
observed; a mutation run that starts from one is refused as a setup error
instead. The criterion each mutation is *aimed* at is recorded, but a catch by
an earlier one is reported as a measurement rather than as a failure — stripping
a `find_dependency` may be refused by the exported targets file at
`find_package` time, and a gate insisting on criterion 3 would have called that
correct catch wrong.

*The driver reads the contract rather than a copy of it.* Package name, exported
target, header root and required packages all come out of PACKAGE_CONTRACT.md
§4, so a row that is wrong about a package fails this check instead of quietly
not being used, and there is no second table to drift. Parsing it also confirmed
the §3 correction from the outside: twelve rows carry a `find_package` contract,
and the six that do not are refused by name with the reason — reserved, or
plugin-load-not-`find_package`. *Which* of the two is decided by two cells
rather than one: `vrmAdapterArdy` says `reserved` in **Exported target**, while
`execMotion` and `execVrm` say it in **In the aggregate product** and carry a
plain dash beside a plugin bundle's. Reading only the target cell sent both of
those away with the plugin-load reason, which is untrue of either.

*The required-package cell is one level deep, and the installer needs the
closure.* §3 rule 3 forbids a config from reaching past its own declared edges,
so `motionBvh`'s row names `motionSource` and never `motionCore` — and
installing the row literally leaves `motionSource`'s own configure with nothing
to resolve. The driver walks the rows depth-first instead. The rest of the table
happens to be listed in topological order, which is why only `motionBvh`
exposed it.

**Done 2026-08-29 for `vrmAdapterVmc`, and this is the half the milestone was
written for.** `tests/consumer/vrmAdapterVmc/` configures, builds, links and
runs against a prefix holding `motionCore`, `motionRuntime`, `liveTransport`,
`osc` and the adapter, with OpenUSD arriving through `--extra-prefix` the way it
arrives for anyone else. It passed on the first run, which is worth stating
plainly: the package was correct, and it was correct because the OSC-3 fix had
already landed. What this adds is that the correctness is now measured rather
than reviewed, and the measurement has been shown to fail.

*The pre-fix config was reproduced in its own shape, not approximated.* The
existing `no-dependency` mutation strips every `find_dependency` in the config,
and against a package with five edges it is caught by the first the closure walk
reaches — `motionCore` — which is a real catch that proves nothing about the
fifth. The §1 defect was one missing line and it was the last one, so the driver
grew `--dependency`: `--mutate no-dependency --dependency osc` removes exactly
the block the OSC-3 fix added, leaves the other four, and criterion 3 refuses it
by name. All four original mutations were run too, and each was answered first
by the criterion it was aimed at.

*The narrowed mutation has three outcomes, and only one of them blames the
fixture.* Removing a `find_dependency` is inert whenever something else resolves
that package, and a run that then met every criterion would have exited 1 with
*this fixture cannot be trusted* — an accusation against the one file in the
loop that was not changed. Two shapes of that were found by review and are now
refused rather than reported: an edge another package in the prefix also
declares is refused **before anything is installed**, from the contract table
(`motionCore` is required by `motionRuntime`), and an edge whose condition does
not hold on this host ends **inconclusive** (`liveTransport`'s
`find_dependency(Threads)` is inside `if(NOT WIN32)`; the driver evaluates no
such condition and says so from the qualification in the cell). Both were run:
three refusals in under a second each, and the inconclusive one against a
scratch `liveTransport` fixture that was removed afterwards — PKG-3 is where
that package acquires a real one.

*Criterion 5 was verified against this fixture rather than inherited from the
last one.* A `motionCore::motionCore` added to the fixture's link line is caught
by name — **with criteria 1–4 still met**, which is the whole argument for
checking the fixture statically: a leaking consumer builds and runs and reports
a pass. The file was restored byte-exact afterwards, and the mutation was to the
fixture rather than to a tracked source file of the package.

*This fixture is also the first whose sources name other identities at all.* Its
comments name four sibling packages, in a criterion whose rule is that a fixture
must not name them — and it reads met, because the check strips comments before
it looks. `osc`'s fixture had nothing to say about a sibling, so that half of
the check had never been exercised by anything but its own unit of proof.

### PKG-2 — the driver ✅

A script that, for a named package: installs the workspace to a scratch prefix,
configures the fixture against that prefix alone, builds it, and reports which
of the six criteria in
[PACKAGE_CONTRACT.md §5](../architecture/PACKAGE_CONTRACT.md) it met. It runs on
a workstation with no CI involved, because a lane that cannot be reproduced by
hand is a lane nobody can debug.

Open question this milestone answers rather than assumes: whether the prefix
comes from `cmake --install` or from an extracted `ost` package. They are not
the same artifact — `ost` stages a dependency's link half under
`runtime/libraries/{lib,bin}` — and a consumer contract that holds for one and
not the other is a finding, not a configuration error.

**Done 2026-08-29: `scripts/check_package_consumer.py`, and for a plain library
the two prefixes are the same artifact.** `--prefix-source` takes both, and both
were run by hand against `osc`. The two prefixes hold the **same seven files
under the same names**, and `oscConfig.cmake` and `oscTargets-release.cmake` are
byte-identical between them; the archive itself is not, because `ost` built it
with the runtime's toolchain (`msvc143`) and the workstation's `cmake --install`
used the local one (MSVC 19.51). That difference is the one a consumer contract
should be insensitive to, and it was: the same fixture linked either archive.

The staging difference the question anticipated is real but belongs to a
*bundle*, whose package carries its dependencies' link halves. A plain library's
`ost` package stages only its own install rules — so this answer is about a
*shape* rather than about eleven packages. The eight library rows and the three
adapters all have that shape and `vrmSchema` does not, which makes it the one
row in §3's first group where the two prefixes could still diverge; and only
`osc` has actually been run either way.

It also runs the criterion-5 pass before it builds anything, which is the cheap
half and the half that guards the other five — and that pass reads the shared
`ConsumerCriteria.cmake` as well as the fixture's own two files. The module is
copied beside every fixture and included by all of them, so an identity or a
`CMAKE_SOURCE_DIR` added there would leak into twelve fixtures at once while all
twelve went on reporting criterion 5 met, which is this criterion's own failure
mode arriving through the one file the fixtures do not own.

Two more properties are asserted rather than assumed, because each of them
otherwise turns a harness fact into a contract claim. **Criterion 1 must resolve
from the scratch prefix**: CMake searches the host too, so a stale install
elsewhere satisfies 1–4 and prints a pass without the prefix being opened —
worse, it makes `--mutate no-config` blame the fixture. And **the built consumer
runs with the prefix's own `bin` and `lib` on the loader path**, which the two
`SHARED` rows need; without it `vrmContainer` and `vrmSchema` would exit
`0xC0000135` on Windows and be reported as *the package links but does not
work*.

One defect the milestone found in itself, worth recording because it is a
this-machine class rather than a this-script one: the driver captures the
consumer's configure output, and a captured toolchain speaks the host's
language. MSBuild on a Japanese Windows emits cp932, and one stray byte raised
`UnicodeDecodeError` inside subprocess's reader thread, surfacing several frames
later as `stdout is None`. **Decoding with `errors="replace"` moved that crash
rather than removing it** — U+FFFD is not encodable in cp932 either, so echoing
what had just been decoded raised `UnicodeEncodeError` on the way back *out*, on
the same input and past every `try` in the file. Both halves have to be lossy: a
criterion the driver cannot render must read as unmet, never as a traceback.

### PKG-3 — all twelve ✅

Widen to every row in §3. Expect this to find more than one instance of the §1
class: no config file in this repository has ever been opened by a consumer, and
the one that was reviewed most recently was the one that was wrong.

Each failure is fixed in the config, never in the fixture. A fixture edited to
make a package pass is the workspace check wearing a disguise.

**Done 2026-08-30, and no config file failed.** All twelve configure, build,
link and run from a prefix holding their own transitive closure and nothing
else. That is the outcome the paragraph above did not predict, and it is worth
stating plainly rather than quietly: the prediction was wrong, and the reason it
was wrong is that the §1 defect had already been fixed in both packages it
shipped in, and the eleven other configs were written to §3's rules by people
following them. What this milestone adds is that the compliance is now measured
instead of reviewed — for the first time, by something that is not this
workspace.

What it *did* find is in the harness rather than in a package, three times, and
each one would have made a future run lie.

*The blanket `find_dependency` mutation could accuse a fixture of an edit that
changed nothing.* The narrowed form learned that lesson a day earlier and grew
two refusals; the form with no name kept none of them. `liveTransport`'s only
edge is conditional, so on Windows that mutation deletes a line inside an
`if(NOT WIN32)` which is never reached — and the run then met every criterion
and exited 1 with *this fixture cannot be trusted*.

The fix separates two facts the first attempt at it collapsed into one refusal.
**Masking is a property of the prefix**, readable from the contract and true on
every host, so a package whose every edge is also declared by a package beside
it is refused before anything is installed. **A condition is a question about
the host**, and this driver evaluates none — so a conditional edge is a reason
not to blame the fixture, and not evidence that the mutation is inert. Those
runs are made, and a pass ends **inconclusive**. Refusing them instead threw
away a real catch: `vrmSchema`'s `find_dependency(pxr)` is inside
`if(NOT pxr_FOUND)`, which every clean consumer reaches, and its blanket
mutation is caught by criterion 4.

*Criterion 5 was too coarse for a package whose API hands back a lower layer's
type.* `ExtractBvhSource` takes a `motionSource::SourceSkeleton*`, so a
`motionBvh` fixture that calls it writes a sibling's namespace — and was
reported as naming one. The check now asks per file: a CMake file is where an
identity becomes an edge, so any other package named there is still the
violation; a `.cpp` creates no edge, so what is refused there is an `#include`
of a sibling's header root. Refusing the spelling instead would have left the
row with the most interesting closure in the table measured by the weakest
fixture in it.

*Criterion 3 cannot see a missing `find_dependency(pxr)`.* Removing it from
`motionCore`'s config was answered by the build rather than by the closure walk,
because OpenUSD's imported targets are unnamespaced — `arch`, `gf`, `tf` — and a
walk that refuses an undefined `::`-qualified entry has nothing to refuse in a
bare name. So for the eight rows whose external edge is OpenUSD, the *header a
fixture includes* is the only thing between a missing edge and a passing run,
and each of those fixtures includes the one that carries the edge into its
translation unit. That is a rule PKG-4's lane inherits rather than a note about
these eight files.

Two shapes the earlier milestones had not measured, both of which behaved:

- **`vrmContainer` is the only `SHARED` package**, and a shared package's
  contract is not finished at the link. Its prefix ships `bin/vrmContainer.dll`
  beside `lib/vrmContainer.lib`; `vrmSchema`, the one *bundle* with a package,
  puts `libvrmSchema.dll` in `lib/` instead. Both are inside the prefix, which
  is what the contract promises, and neither is where the other one is — which
  is why the driver puts the prefix's own `bin` and `lib` on the loader path and
  nothing else. **PKG-2's open question is not closed by this**: every run here
  used a `cmake --install` prefix, so what `vrmSchema` shows is that the
  contract holds for a bundle's layout, not that an extracted `ost` package
  produces the same one. `--prefix-source ost-package` against that row is
  still unrun.
- **`vrmAdapterMocopi`'s standing platform gap is half closed.** Its closure
  carries `ws2_32` twice over, once from its transport edge and once from
  OpenUSD's `arch`, which is the imported-target half of
  [#113](https://github.com/animu-sphere/usd-vrm-plugins/issues/113) measured
  rather than predicted. The raw-library half is what PKG-5 keeps.

The arithmetic: ten packages measured green here, on top of PKG-1's two, and
**forty-eight mutations of the installed prefix** across them — forty-one
caught, five refused before anything was installed because masking makes them
inert on any host, and two inconclusive because `liveTransport`'s one edge is
conditional and unreached here. The blanket mutation that exposed the driver
defect above was reproduced before the fix and re-run after it. Six more edits
went to the *fixtures* rather than the prefix, and criterion 5 caught each
statically, before a build.

Every package with more than one edge was also mutated by *name*, because
stripping every `find_dependency` is caught by whichever edge the closure walk
reaches first and says nothing about the last one — which is the shape the §1
defect actually had, and `vrmAdapterVrchatOsc` is where that shape was
reproduced for the second of the two packages that shipped it.

### PKG-4 — the CI lane ✅

One cell, on every pull request, on all three OS. It must be a *separate* build
from the workspace cells: the point is a prefix that contains no build tree.

The acceptance criteria are
[PACKAGE_CONTRACT.md §5](../architecture/PACKAGE_CONTRACT.md) 1–6, and criterion
6 — that the three platforms agree about the package closure — is the one only a
lane can check. `liveTransport` is where a difference is expected and permitted:
`ws2_32` on Windows, `Threads::Threads` elsewhere. A difference anywhere else is
a defect until documented.

**Green on all three OS, 2026-08-30**, and criterion 6 with them:
[`.github/workflows/package-consumer.yml`](../../.github/workflows/package-consumer.yml),
three jobs — read the pins, consume on each of the three OS, compare the three
closures. Twelve packages × three platforms, and **every workspace target in
every closure is present on all three or on none**. The one difference this
contract permits is present in both directions: `Threads::Threads` on macOS and
Linux, `ws2_32` on Windows, for `liveTransport` and the three adapters that
inherit it. `Standalone` in PACKAGE_CONTRACT.md §4 is now unqualified in every
row.

It took two red runs to get there, and both were the lane doing its job (see
below). The synthetic three-platform inputs the comparison was verified against
turned out to have the right *shape* and the wrong counts: OpenUSD's own
platform link line is 3 entries on Windows (`Dbghelp.lib`, `Shlwapi.lib`,
`Ws2_32.lib`), 2 on Linux (`dl`, `m`) and 3 on macOS (those two plus
`-framework Foundation`) — 10 external entries against 9 and 9. The rules fired
where they were aimed; the prediction of what they would see was approximate,
which is the limit that was stated in advance.

*It is hand-authored because the schema has two cell kinds and neither is this
one.* `kind: consumer` is refused — `unknown variant `consumer`, expected
`bundle` or `workspace`` — so `ost ci generate` cannot render it, which is the
condition `ost ci matrix` exists for: *emit the resolved cells so a workflow
`ost ci generate` cannot express can consume the same pins instead of copying
them*. The other hand-authored workflow here is `release.yml`, which copies an
X11 step, an `ost` pin and three runtime digests and was missed when CI was
re-pinned to 26.08, failing a tag build while every PR lane stayed green. So
this one copies nothing: `scripts/ci_pins.py` reads the runners, the digests,
the host packages and the Python version out of `openstrata.ci.yaml` through
`ost ci matrix`, and the three OS come from the three `verify: test` workspace
cells rather than from a list in the YAML. **`--expect 3` is a check rather than
a formality** — criterion 6 asks whether three platforms agree, and a lane that
quietly asked it of two would answer a different question and print a pass.

*The one pin that cannot come from `ost` is which `ost`*, so
`ci_pins.py bootstrap-version` reads it from the contract file with a regex —
and is then checked by the tool it installed: `lane-matrix` re-reads the same
value through `ost ci matrix` and refuses to emit anything if the two disagree.

*Criterion 6 needed a contract before it needed a script.* Read strictly it is
unimplementable and read loosely it is vacuous, because the three runtimes are
three separate builds of OpenUSD: an entry-for-entry comparison reports an
upstream difference as a defect here, and a comparison that tolerates any
difference catches nothing.
[PACKAGE_CONTRACT.md §5.1](../architecture/PACKAGE_CONTRACT.md) now states the
partition — a workspace target agrees or it is a defect; a declared platform
dependency is present exactly where its cell says and **absent elsewhere**;
everything else is attributed to the external package that brought it, and the
attribution is what gets checked. `scripts/check_package_closures.py` implements
that and reads §4's tables through the driver's own parser, so there is no
second table to drift.

*The comparison is verified by ten cases, each of which was made to happen.* A
check that has only ever printed a pass is indistinguishable from one that is
not run — the rule this whole track exists for — and this one has no prefix to
mutate, so its inputs are the reports. Nine of the ten are the checks firing:
`ws2_32` surviving on a POSIX host and a POSIX host with no threading link
(**both halves of PKG-5**, and the first is the one Windows structurally cannot
see); a workspace target present on one platform and one missing from one; a
foreign entry in a package with no external edge; an absolute path; a workspace
identity spelled as a raw archive. The tenth is the answer that is *not* a
verdict: two platforms end in a setup refusal rather than a pass, because a
question about three is not answered by two.

**The first run found a defect, and it is in the runtime rather than in a
package** ([report 37](../reports/ost/37-2026-08-30-v0.22.6-runtime-python-paths-from-the-producer.md)).
macOS ran all twelve green. Windows and Linux each configured four and stopped:
a pulled runtime's CMake package carries the *producing* machine's Python paths,
in `pxrConfig.cmake`'s guarded variables and again in sixteen imported targets'
`INTERFACE_INCLUDE_DIRECTORIES`, and the second of those no `-D` can override.
The four that passed everywhere are exactly the four whose closure never reaches
`pxr`; the eight that failed are exactly the eight that do.

That is this track's premise arriving from a direction it did not predict. PKG-4
was written to catch a *package* that could not be consumed from outside, and
the first thing it caught was a *runtime* — for the same reason, that no lane
had ever configured against one without `ost build` in front of it. **And `ost
build` is not immune**: a whole-workspace configure against a runtime whose baked
include directory was made foreign fails identically, which was measured rather
than assumed. The lane carries both workarounds in its setup, where they touch no
package under test, and the ask is upstream (§5 of this document: an upstream fix
is recorded as a report, not absorbed).

**Those ten cases run against synthetic reports, and that is a stated limit
rather than an oversight.** The Windows halves are real — twelve packages, this
workstation, criteria 1–5 met — and the macOS and Linux halves are the
substitution this lane *expects*: `ws2_32` becomes
`$<LINK_ONLY:Threads::Threads>`, and OpenUSD's Windows platform names give way
to POSIX ones. That is a prediction, and predicting a measurement is exactly
what PKG-3 got wrong when it expected to find more instances of the §1 defect
and found none. What the synthetic inputs establish is that each rule fires on
the shape it is aimed at; what only the lane can establish is what the shape
actually is.

### PKG-5 — close the standing platform gap ✅

`vrmAdapterMocopi`'s standalone build has been unverified since the receiver
added `ws2_32` and an installed-config edit without re-running the check
([#113](https://github.com/animu-sphere/usd-vrm-plugins/issues/113)). The
imported-target half of that stopped being a prediction on 2026-08-29 and is now
a boundary check; **the raw-library half is what remains**, and a POSIX run is
worth more than a Windows one because there it verifies the *absence* of a
threading link. PKG-4's lane closes this by construction, which is why the entry
moves here from the carry-over list rather than being tracked twice.

**Closed 2026-08-30 by measurement.** On `macos-15` and `ubuntu-24.04`
`vrmAdapterMocopi`'s consumer links `Threads::Threads` and **no** `ws2_32`,
which is the absence a Windows run cannot see and the half of the issue that had
been open since the receiver grew a platform link. The check reads the
requirement from the package's own `Platform deps` cell rather than from a rule
written for it, and both directions were made to fail before it was believed.

## 5. What this track is not

- **Not a packaging feature.** It adds no artifact, no member, and no `ost`
  verb. If a fix requires one, that is an upstream ask and it is recorded as a
  report, not absorbed here.
- **Not the aggregate product's gate.** `scripts/clean_install_smoke.py` extracts
  the product outside the repository and drives textured avatars end to end;
  that is the *plugin-load* contract and it stays where it is. This track is the
  `find_package` contract beside it.
- **Not a rewrite of any config file.** The rules in
  [PACKAGE_CONTRACT.md §3](../architecture/PACKAGE_CONTRACT.md) are what the
  existing configs already follow, one broken instance aside. This track checks
  them; it does not restyle them.
- **Not a second dependency-direction check.** WORKSPACE.md §2's directions are
  gated by `ost plugin test --workspace --graph-only` on every PR. A consumer
  lane would notice a violation only by accident, and must not be argued for as
  though it covered one.

## 6. Contract changes this plan requires

- **WORKSPACE.md §5** keeps artifact naming and aggregate membership and defers
  the per-package consumer contract to
  [PACKAGE_CONTRACT.md](../architecture/PACKAGE_CONTRACT.md). No claim in §5
  changes; one paragraph now points elsewhere for what it never stated.
- **PACKAGE_CONTRACT.md's `Standalone` column** moves from `unmeasured` to
  `measured` package by package, as PKG-3 measures them. That column is the
  track's progress bar, and it is the only place the progress is recorded — a
  second list would drift.
- **PACKAGE_CONTRACT.md gains §5.1**, stating what criterion 6 compares: which
  closure entries are this workspace's to answer for and which arrive from a
  package it does not produce. That was implicit while criterion 6 was
  unanswered, and it stops being implicit the moment a lane has to decide. ✅
  **Done 2026-08-30**, with PKG-4.
- **`scripts/check_docs.py`** gains a check that every identity with a
  `*Config.cmake.in` has a row in PACKAGE_CONTRACT.md, and that every row names
  a package that exists. That is the same class of check as the bundle-inventory
  one it already runs. ✅ **Done 2026-08-30**, with five ways to fail it, each
  one made to fail before the check was believed: a config template with no
  row, a row claiming its package exports no target beside that package's own
  config, a row naming an identity no manifest declares, a row promising a
  target that nothing installs, and a row marked **reserved** beside its own
  config. The last is there because reserved rows are exempt from the existence
  half by design (PACKAGE_CONTRACT.md §6) — without a case of its own it would
  fall through both halves, which is the drift the check is for.

## 7. PR splitting

1. PACKAGE_CONTRACT.md (PKG-0) — document only, no code.
2. The `osc` fixture and the driver (PKG-1, PKG-2) — one package, run by hand,
   with the negative verification against the broken config recorded.
3. `vrmAdapterVmc`'s fixture — the package the defect was in.
4. The remaining ten, in one PR per group of siblings, so a failure is
   attributable to a group rather than to a batch. ✅ — landed as four groups:
   the two shapes no fixture had (`vrmContainer`, `liveTransport`), the motion
   layer's five, the two remaining adapters, and the one bundle with a package.
5. The CI cell (PKG-4), which carries PKG-5 with it. Three files that are not
   the workflow: `scripts/ci_pins.py` so the lane copies no pin,
   `scripts/run_package_consumer_lane.py` so it can be reproduced by hand, and
   `scripts/check_package_closures.py` for the criterion no host can answer.
6. `check_docs.py`'s row check. ✅ — landed before PKG-4 rather than after it,
   because the document it checks is five days old and the drift it catches has
   had no time to happen yet. That is the moment to add such a check.

The document comes first because a fixture written before the contract settles
on whatever the first package happened to need — the same rule the OSC track
used for `libs/osc`, and for the same reason.
