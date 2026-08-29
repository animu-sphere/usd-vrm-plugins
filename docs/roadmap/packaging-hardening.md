# Packaging hardening — the installed-package consumer lane

**Status:** in progress — PKG-0, PKG-1 and PKG-2 done; one of twelve packages
measured · **Target:** v0.8.0 ·
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

### PKG-3 — all twelve ⬜

Widen to every row in §3. Expect this to find more than one instance of the §1
class: no config file in this repository has ever been opened by a consumer, and
the one that was reviewed most recently was the one that was wrong.

Each failure is fixed in the config, never in the fixture. A fixture edited to
make a package pass is the workspace check wearing a disguise.

### PKG-4 — the CI lane ⬜

One cell, on every pull request, on all three OS. It must be a *separate* build
from the workspace cells: the point is a prefix that contains no build tree.

The acceptance criteria are
[PACKAGE_CONTRACT.md §5](../architecture/PACKAGE_CONTRACT.md) 1–6, and criterion
6 — that the three platforms agree about the package closure — is the one only a
lane can check. `liveTransport` is where a difference is expected and permitted:
`ws2_32` on Windows, `Threads::Threads` elsewhere. A difference anywhere else is
a defect until documented.

### PKG-5 — close the standing platform gap ⬜

`vrmAdapterMocopi`'s standalone build has been unverified since the receiver
added `ws2_32` and an installed-config edit without re-running the check
([#113](https://github.com/animu-sphere/usd-vrm-plugins/issues/113)). The
imported-target half of that stopped being a prediction on 2026-08-29 and is now
a boundary check; **the raw-library half is what remains**, and a POSIX run is
worth more than a Windows one because there it verifies the *absence* of a
threading link. PKG-4's lane closes this by construction, which is why the entry
moves here from the carry-over list rather than being tracked twice.

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
- **`scripts/check_docs.py`** gains a check that every identity with a
  `*Config.cmake.in` has a row in PACKAGE_CONTRACT.md, and that every row names
  a package that exists. That is the same class of check as the bundle-inventory
  one it already runs.

## 7. PR splitting

1. PACKAGE_CONTRACT.md (PKG-0) — document only, no code.
2. The `osc` fixture and the driver (PKG-1, PKG-2) — one package, run by hand,
   with the negative verification against the broken config recorded.
3. `vrmAdapterVmc`'s fixture — the package the defect was in.
4. The remaining ten, in one PR per group of siblings, so a failure is
   attributable to a group rather than to a batch.
5. The CI cell (PKG-4).
6. `check_docs.py`'s row check.

The document comes first because a fixture written before the contract settles
on whatever the first package happened to need — the same rule the OSC track
used for `libs/osc`, and for the same reason.
