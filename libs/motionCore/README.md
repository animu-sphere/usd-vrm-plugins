# motionCore

`motionCore` is the vendor-neutral humanoid motion value contract for the
motion layer. It owns the VRM 1.0 humanoid vocabulary plus
`motion::HumanoidPose`, `HumanoidAnimation`, independent `RootMotion`, and
`MotionConstraintSet`.

It deliberately has no GLB parser, VRM parser, USD stage authoring,
plugin registration, network protocol, or vendor SDK. The sole OpenUSD
dependency is the small `Gf` value-type library used for vectors and
quaternions. `usdVrmaFileFormat` reads clips into these values; a future
`vrmRetarget` library will map them onto a concrete avatar skeleton.

All coordinates are right-handed, Y-up, metres. `World`, `Character`,
`Skeleton`, and `JointLocal` identify the reference frame of a constraint;
the conversion and USD-stage authoring belong to consumers. Root motion is
never encoded by mutating a hips-local rotation in this API.

Comparing those values takes two operations rather than one, because the
callers ask different questions. `operator==` is exact — the comparison
OpenExec type registration requires and a trace round-trip is defined by — and
`NearlyEqual` with a `MotionTolerance` asks whether two samples describe the
same *motion*, which is what a parity check and a corpus fixture mean. They
differ in three stated places, and `Compare.h` carries the reasoning for each.

```sh
cmake -S libs/motionCore -B build/motion-core -DCMAKE_PREFIX_PATH=/path/to/openusd
cmake --build build/motion-core --config Release
ctest --test-dir build/motion-core -C Release --output-on-failure
cmake --install build/motion-core --prefix <prefix> --config Release
```

Consumers use the installed package contract:

```cmake
find_package(motionCore CONFIG REQUIRED)
target_link_libraries(consumer PRIVATE motionCore::motionCore)
```
