# Contributing

Thanks for helping improve `usd-vrm-plugins`. Small fixes, documentation
updates, tests, and questions are all welcome.

## Before you start

- Search existing issues and pull requests first.
- For a larger change, open an issue or discussion before doing substantial
  work. This helps avoid solving the wrong problem.
- Do not include private avatars, captures, credentials, or other material you
  are not allowed to redistribute.

## Build and test

The main development path uses OpenStrata:

```sh
ost plugin test --workspace
```

Plain CMake is also supported when an OpenUSD installation is available:

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/openusd-install
cmake --build build --config Release
ctest --test-dir build -C Release
```

The supported toolchain is documented in
[SUPPORTED_CONFIGURATIONS.md](docs/reference/SUPPORTED_CONFIGURATIONS.md).

For a documentation-only change, the full build is usually unnecessary. Check
links and wording locally, and mention that the build was not run in the pull
request.

## Formatting

Native C and C++ files use the repository's `.clang-format` configuration. Run
clang-format on files you changed, for example:

```sh
clang-format -i path/to/file.cpp path/to/file.h
```

Please do not format vendored code under `third_party/`.

## Pull requests

Keep a pull request focused and explain:

1. what changed and why;
2. how it was tested; and
3. any compatibility, documentation, or follow-up work that matters.

A small pull request is easier to review than a broad cleanup. Reviewers may
ask for tests or documentation when a change affects a public contract.

By contributing, you agree that your work is provided under the repository's
[Apache-2.0 license](LICENSE).

Please also follow the [Code of Conduct](CODE_OF_CONDUCT.md).