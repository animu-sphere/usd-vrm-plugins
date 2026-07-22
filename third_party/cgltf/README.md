# cgltf

This directory vendors `cgltf.h` from [cgltf](https://github.com/jkuhlmann/cgltf)
release `v1.15`, commit `360db1a95480fe102ae9c69b27c5d101167ff5ba`.

It is intentionally a small, header-only dependency: `usdVrmaFileFormat`
compiles the implementation once in `src/io/cgltfImpl.cpp`. Keep the source and
its accompanying MIT [`LICENSE`](LICENSE) together when updating it.
