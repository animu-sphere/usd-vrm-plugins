# Consumer fixtures

One CMake project per package, each one an *external consumer*: it calls
`find_package`, links the exported target, includes a public header, and knows
nothing else about this workspace. Twelve of them, which is every package this
workspace installs.

Nothing here is built by the workspace. No `add_subdirectory` reaches this
directory, no ctest registers it, and `scripts/check_package_consumer.py` copies
the whole tree **outside the repository** before configuring anything in it.
That is deliberate:
[PACKAGE_CONTRACT.md §5](../../docs/architecture/PACKAGE_CONTRACT.md) criterion 5
— *the consumer's CMakeLists names no target from this repository's source tree*
— is the one that makes the other five mean anything, and it is the one a
fixture inside a workspace loses by accident the first time someone runs it from
the root build.

## Why these exist

A composed workspace build defines every target as an alias in one CMake
project, and `ost library build` composes `requires.libraries` the same way.
Neither opens a `*Config.cmake` at any point. So the one configuration that can
fail is the one nothing ran — and on 2026-08-29 it shipped: two adapters carried
`osc::osc` on their installed interface link line with no `find_dependency(osc)`
in either config, and all 17 lanes were green. They were green because they had
to be.

These fixtures are the consumer that is not us.

## Running one

```sh
python scripts/check_package_consumer.py osc
```

Four of the twelve need no OpenUSD and run exactly like that: `osc`,
`vrmContainer`, `liveTransport` and `vrmAdapterVrchatOsc`. The other eight take
a `--extra-prefix`.

The driver installs the package and its required packages into a scratch prefix
holding nothing else, configures the fixture against that prefix alone, builds
it, runs it, and reports the six criteria. `--prefix-source ost-package` asks
the same question of an `ost`-produced prefix instead of a `cmake --install`
one. See [roadmap/packaging-hardening.md](../../docs/roadmap/packaging-hardening.md).

A package with edges needs the packages it does *not* produce to come from
somewhere, and they arrive the way they arrive for any other consumer:

```sh
python scripts/check_package_consumer.py vrmAdapterVmc \
    --extra-prefix ~/.ost/runtimes/openstrata-cy2026-<platform>-py313-usd
```

That prefix is the OpenUSD runtime, and the reliable way to spell it is to read
it rather than to remember it: `.strata/targets/<target>/toolchain.cmake` names
the same directory in its `CMAKE_PREFIX_PATH` line, which is where the run
recorded above took it from.

`--extra-prefix` is never the workspace build tree. It holds the packages this
workspace does not produce, and the driver keeps it out of the resolution of the
package under test: criterion 1 fails if the package answers from anywhere but
the scratch prefix.

## Making one fail on purpose

A fixture that has only ever printed a pass is indistinguishable from one that
is not checked, so `--mutate` breaks the *installed prefix* and requires the run
to go red — a pass against a broken package is reported as the fixture being
untrustworthy. For a package with more than one edge, `--dependency` names the
one to remove:

```sh
python scripts/check_package_consumer.py vrmAdapterVmc \
    --mutate no-dependency --dependency osc --extra-prefix ...
```

That is the shape the defect above actually had — one missing line, and it was
the last one. Stripping every `find_dependency` instead is caught by whichever
edge the closure walk reaches first, which is a real catch that proves nothing
about the fifth.

**Removing a line is not the same as breaking something, and only one of the
three outcomes blames the fixture.** A `find_dependency` is inert whenever
something else already resolves that package, so:

- an edge another package in the prefix also declares is **refused before
  anything is installed** — `motionCore` is required by `motionRuntime`, so
  removing it from `vrmAdapterVmc`'s config breaks nothing. The refusal names
  the packages that mask it and the edges only this config resolves;
- an edge whose *condition* does not hold on this host ends **inconclusive**
  (exit 2). `liveTransport`'s `find_dependency(Threads)` sits inside
  `if(NOT WIN32)`, so on Windows the removed line was never reached. The driver
  evaluates no such condition and says so up front, from the qualification in
  the contract's own cell;
- exit 1 — *this fixture cannot be trusted* — is reserved for a mutation that
  really did break the prefix and was met with a pass anyway. It is an
  accusation against the one file in the loop that was not changed, so it is
  never the answer to an inert edit.

Without `--dependency` the mutation strips **every** `find_dependency`, and that
form is refused up front when none of the lines is this config's to lose. On
Windows `liveTransport`'s only edge is the conditional one, so the blanket
mutation there could delete a byte, break nothing, and reach exit 1 — the same
false accusation the named form refuses, arriving through the form that had no
guard. The refusal names each inert edge and why it is inert, and it costs a
second rather than an install.

## Adding one

Copy `osc/` and change three things: the `project()` name, the `PACKAGE` and
`TARGET` arguments to `consumer_criteria`, and what `main.cpp` includes and
calls. Everything else is shared by
[`ConsumerCriteria.cmake`](ConsumerCriteria.cmake), and that sharing is the
point rather than a convenience: twelve fixtures each writing their own
`find_package` and their own `if(TARGET)` would be twelve chances for one of
them to check less than the others and still print a pass.

The third of those three is the one that takes thought, and PKG-3 measured why.
**Include the header that carries the package's edges**, not the smallest one:
for every package whose external edge is OpenUSD, the include is the *only*
thing between a missing `find_dependency(pxr)` and a passing run, because
OpenUSD's imported targets are unnamespaced and the closure walk has nothing to
refuse in a bare `gf`. **Call something whose archive member carries the edges
the headers do not show**: `vrmRetarget` meets the runtime layer only at the
link, and `vrmAdapterVrchatOsc` meets the decoder only there, so a fixture that
constructed a value and stopped would compile, link, and never ask.

Three rules, all of them mechanically enforced by the driver's criterion-5 pass
— which reads [`ConsumerCriteria.cmake`](ConsumerCriteria.cmake) as well as the
fixture's own two files, because an identity added to the shared module would
otherwise leak into every fixture while all of them reported criterion 5 met —
and all of them verified by mutating a fixture until each was caught:

- **Name no workspace identity but your own, in CMake.** A fixture that
  `find_package`s or links a sibling is testing the prefix's contents, not the
  package's contract. This applies to `CMakeLists.txt` and to the shared module,
  which are the only two files that can create an edge.
- **Include no sibling's header root, in C++.** `main.cpp` creates no edge, so
  the rule there is about includes rather than names. A *name* is often
  unavoidable and always fine: `motionBvh` hands back a
  `motionSource::SourceSkeleton`, so every consumer of it writes that namespace,
  and the type arrives through `motionBvh`'s own public header — which is what
  its `find_dependency` exists for. Reaching `<motionSource/…>` directly is the
  violation, because that is the fixture depending on what else the prefix holds.
- **Reach no path out of this directory.** `add_subdirectory`, `../../`, and
  `CMAKE_SOURCE_DIR` are all refused in every file; each of them can find the
  source tree.
- **Include a public header and call something.** A fixture that only links
  proves the config file and not the header install, and it keeps proving it
  after the headers stop being installed.

## What belongs here, and what does not

These are packaging fixtures, not tests of the library. `main.cpp` asks the
smallest question the package can answer — for `osc`, whether an address comes
back; for `vrmAdapterVmc`, whether a bone name goes in and the same name comes
out; for `vrmContainer`, whether a hand-assembled container parses; for
`vrmSchema`, whether a generated class knows its own attribute names — because
anything larger makes a packaging failure look like a decoder failure the first
time it goes red. Those suites live with their code, in
[`libs/osc/tests/`](../../libs/osc/tests/) and
[`adapters/liveCapture/vmc/tests/`](../../adapters/liveCapture/vmc/tests/).

*Which* header a fixture includes is a packaging decision, though. A package
with edges is best asked through the header that carries them: `SkeletonMap.h`
pulls a canonical humanoid header and two OpenUSD value-type headers into the
consumer's translation unit, so a config that forgot a required package fails at
the first `#include` rather than at link time.

So is *which call* it makes, for a static package whose platform link line is
carried by one archive member. `liveTransport`'s fixture calls into
`UdpReceiver` — the one call there that needs no socket — because a fixture that
called only the diagnostic vehicle would never pull the member with the socket
symbols in, and would link a package whose `ws2_32` had gone missing while
reporting criterion 4 met. It binds nothing and names no port: a packaging
fixture that took one would go red on a host where something else already held
it, which is a fact about the machine and not about the package.

Three bundles have no fixture here and never will: `usdVrmFileFormat`,
`usdVrmPackageResolver` and `usdVrmaFileFormat` export no target and install no
config by design — OpenUSD discovers them through `plugInfo.json`, nothing links
them. Their consumer contract is "the plugin registers and a stage opens", which
[`scripts/clean_install_smoke.py`](../../scripts/clean_install_smoke.py) already
gates from packaged artifacts. The driver refuses them by name rather than
letting a `find_package` fail at something it was never meant to find.
