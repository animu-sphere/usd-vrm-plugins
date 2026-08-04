# motionSource

`motionSource` is the **format-neutral** middle of the recorded-file path. It
holds the rig a recording describes (`SourceSkeleton`), what that recording
animated (`SourceAnimation`), and where the file came from
(`SourceProvenance`) — in the source's own joint names, basis, unit, and angle
convention, with no file format anywhere in it.

A reader knows a file format and no semantics; this layer knows semantics and no
file format; and a declarative producer profile supplies what neither can know
on its own ([recorded-motion-sources.md §2](../../docs/roadmap/recorded-motion-sources.md)).
The arrow `motionBvh -> motionSource` never reverses — the day a
format-shaped field lands here is the day a second reader cannot be added
without changing every signature above it, which is the entire reason this layer
exists before a second reader does
([WORKSPACE.md §2](../../docs/architecture/WORKSPACE.md)).

## What is here today

The value model and its invariants:

| Header | Holds |
| --- | --- |
| `SourceSkeleton.h` | `SourceJoint`, `SourceSkeleton`, the name/hierarchy queries a profile matches against, `ValidateSourceSkeleton` |
| `SourceAnimation.h` | `SourceJointTrack`, `SourceAnimation`, the Euler order and angle unit a source declares, `ValidateSourceAnimation` |
| `SourceProvenance.h` | what file this was, under which profile, from which writer |
| `CanonicalMetadata.h` | the one crossing into `motionCore` |

Still to come, in this order: the profile contract, the producer profiles, and
the converter from a skeleton, an animation and a profile to
`motion::HumanoidAnimation` ([§12](../../docs/roadmap/recorded-motion-sources.md)).

## The four decisions the model is shaped by

Each is argued where it is implemented, because each would have been invisible
in a diff:

| Decision | Where |
| --- | --- |
| Values stay in the source's convention, so nothing here is a `GfVec3f` — a vector implies a basis this layer does not know | `SourceSkeleton.h` |
| A skeleton is a value on its own, because a profile is matched against a rig before a frame is read | `SourceSkeleton.h` |
| A rotation is stored the way the source expressed it — angles with their order, or quaternions, never converted between the two here | `SourceAnimation.h` |
| An unanimated component is an empty vector, never a run of identity values | `SourceAnimation.h` |

## Provenance is a neighbour of `MotionSourceMetadata`, not the same type

`SourceProvenance` describes the **file**; `motion::MotionSourceMetadata`
describes the **motion**, rides on every pose, and is compared and serialised as
part of it. The derivation is one-way and narrowing, and
[`CanonicalMetadata.h`](include/motionSource/CanonicalMetadata.h) states what it
drops and where those facts survive instead. Settling this before the converter
set its first field was a
[contract item](../../docs/roadmap/recorded-motion-sources.md) rather than an
implementation detail; the answer is in
[MOTION_CONTRACT.md](../../docs/design/MOTION_CONTRACT.md).

## No producer, no format

There is no producer name and no format name in this library, no default
profile, and no `if (producer == ...)` — the same rule the reader below it
follows, one layer up where it is easier to break. `motionSource_boundaries`
checks it on every build, together with the claim that `CanonicalMetadata` is
the only file here that names a canonical type at all.

## Build

```sh
cmake -S libs/motionSource -B build/motion-source \
    -DCMAKE_PREFIX_PATH=<openusd-prefix>;<motionCore-prefix>
cmake --build build/motion-source --config Release
ctest --test-dir build/motion-source -C Release --output-on-failure
cmake --install build/motion-source --prefix <prefix> --config Release
```

Consumers use the installed package contract:

```cmake
find_package(motionSource CONFIG REQUIRED)
target_link_libraries(consumer PRIVATE motionSource::motionSource)
```
