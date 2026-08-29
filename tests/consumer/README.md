# Consumer fixtures

One CMake project per package, each one an *external consumer*: it calls
`find_package`, links the exported target, includes a public header, and knows
nothing else about this workspace.

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

The driver installs the package and its required packages into a scratch prefix
holding nothing else, configures the fixture against that prefix alone, builds
it, runs it, and reports the six criteria. `--prefix-source ost-package` asks
the same question of an `ost`-produced prefix instead of a `cmake --install`
one. See [roadmap/packaging-hardening.md](../../docs/roadmap/packaging-hardening.md).

## Adding one

Copy `osc/` and change three things: the `project()` name, the `PACKAGE` and
`TARGET` arguments to `consumer_criteria`, and what `main.cpp` includes and
calls. Everything else is shared by
[`ConsumerCriteria.cmake`](ConsumerCriteria.cmake), and that sharing is the
point rather than a convenience: twelve fixtures each writing their own
`find_package` and their own `if(TARGET)` would be twelve chances for one of
them to check less than the others and still print a pass.

Three rules, all of them mechanically enforced by the driver's criterion-5 pass
— which reads [`ConsumerCriteria.cmake`](ConsumerCriteria.cmake) as well as the
fixture's own two files, because an identity added to the shared module would
otherwise leak into every fixture while all of them reported criterion 5 met —
and all of them verified by mutating a fixture until each was caught:

- **Name no workspace identity but your own.** A fixture that links a sibling
  package is testing the prefix's contents, not the package's contract.
- **Reach no path out of this directory.** `add_subdirectory`, `../../`, and
  `CMAKE_SOURCE_DIR` are all refused; each of them can find the source tree.
- **Include a public header and call something.** A fixture that only links
  proves the config file and not the header install, and it keeps proving it
  after the headers stop being installed.

## What belongs here, and what does not

These are packaging fixtures, not tests of the library. `main.cpp` asks the
smallest question the package can answer — for `osc`, whether an address comes
back — because anything larger makes a packaging failure look like a decoder
failure the first time it goes red. The decoder's own suite lives in
[`libs/osc/tests/`](../../libs/osc/tests/).

Three bundles have no fixture here and never will: `usdVrmFileFormat`,
`usdVrmPackageResolver` and `usdVrmaFileFormat` export no target and install no
config by design — OpenUSD discovers them through `plugInfo.json`, nothing links
them. Their consumer contract is "the plugin registers and a stage opens", which
[`scripts/clean_install_smoke.py`](../../scripts/clean_install_smoke.py) already
gates from packaged artifacts. The driver refuses them by name rather than
letting a `find_package` fail at something it was never meant to find.
