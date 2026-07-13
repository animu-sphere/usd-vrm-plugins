# vrmContainer

`vrmContainer` is the workspace's plain shared CMake library for untrusted GLB
container bytes. It validates the GLB 2 header and chunk table, exposes
immutable non-owning JSON/BIN and buffer views, and preserves the
content-addressed embedded-resource naming used by the VRM importer and package
resolver.

The public API has no OpenUSD or plugin-registration types. Views never own
their backing bytes; callers must keep the input storage alive.

```sh
cmake -S libs/vrmContainer -B build/vrm-container
cmake --build build/vrm-container --config Release
ctest --test-dir build/vrm-container -C Release --output-on-failure
cmake --install build/vrm-container --prefix <prefix> --config Release
```

Consumers use the installed package contract:

```cmake
find_package(vrmContainer CONFIG REQUIRED)
target_link_libraries(consumer PRIVATE vrmContainer::vrmContainer)
```
