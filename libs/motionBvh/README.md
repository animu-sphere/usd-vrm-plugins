# motionBvh

`motionBvh` reads BVH **syntax**: `HIERARCHY`, `ROOT` / `JOINT`, `OFFSET`,
`CHANNELS`, `End Site`, `MOTION`, the frame count, the frame time, and the
channel values in **declaration order**. That is the whole of its job.

It deliberately does not know which joint is a `HumanBone`, what unit an offset
is in, which way is up, whether the file is left- or right-handed, what a root
translation means, or how to read a rest pose. Those are facts about the
application that *wrote* the file rather than about the format, so they live in
a declarative producer profile one layer up, and the conversion that uses them
belongs to `motionSource`
([recorded-motion-sources.md §2](../../docs/roadmap/recorded-motion-sources.md)).

**There is no producer name anywhere in this library, and there is no default
profile.** Bake one producer's answers into the parser and the second producer
is not a new profile — it is a rewrite. `motionBvh_boundaries` checks the first
half of that claim on every build.

## What is here today

The syntax half: the model (`BvhDocument.h`), the parser (`BvhParser.h`), and
the frozen diagnostic set (`Diagnostics.h`). The extractor that turns a
`BvhDocument` into `motionSource` values arrives with `motionSource` itself, and
brings the declared `motionBvh -> motionSource` edge with it
([WORKSPACE.md §2](../../docs/architecture/WORKSPACE.md)). Until then this
library links nothing at all — not even OpenUSD's value types, because three
numbers on an `OFFSET` line are not a vector in any basis this layer knows.

## The four rules the parser is shaped by

Each one is a decision, and each is argued where it is implemented:

| Rule | Where |
| --- | --- |
| A file parses entirely or not at all — the first refusal ends it and the document is untouched | `BvhParser.h` |
| A frame is a **line**, so a short row is reported on the row that is short | `BvhParser.h` |
| Keywords are case-insensitive; joint names are verbatim and are never touched | `BvhParser.h` |
| Channel declaration order is retained and never normalised — it *is* the file's Euler order | `BvhDocument.h` |

## Diagnostics

Eleven `VRM_BVH_*` codes, frozen before the parser was written
([§6](../../docs/roadmap/recorded-motion-sources.md)). Five are syntax and are
the only ones this library raises; six are semantics and belong to the layer
where a document meets a profile. `DiagnosticIsSyntax` states the split, and the
boundary check fails a parser source that names a semantic code.

A failure the set does not name is `VRM_BVH_PARSE_FAILED` with a precise
`detail` — a file declaring ten frames and carrying eight is the standing
example. Adding a code the moment a parser meets a new file is exactly the drift
freezing the set prevents.

## Build

```sh
cmake -S libs/motionBvh -B build/motion-bvh
cmake --build build/motion-bvh --config Release
ctest --test-dir build/motion-bvh -C Release --output-on-failure
cmake --install build/motion-bvh --prefix <prefix> --config Release
```

No `CMAKE_PREFIX_PATH` is needed, which is itself the point: this library has no
OpenUSD dependency to resolve.

Consumers use the installed package contract:

```cmake
find_package(motionBvh CONFIG REQUIRED)
target_link_libraries(consumer PRIVATE motionBvh::motionBvh)
```

## Corpus

`tests/corpus/generated/` holds format shapes — hand-written, committed, and
runnable with no hardware. They are named after the shape they pin
(`valid-nested-joints.bvh`, `malformed-frame-width.bvh`), never after a
producer: a fixture called `mocopi-*.bvh` among the format shapes would be the
first place a producer's export became the format's definition.

`tests/corpus/recorded/` holds real producer exports, and there is one:
`mocopi-mobile-arm-raise-turn.bvh`, a 17-second session off a phone — 27 joints,
162 channels, 853 rows at 50 Hz. It is a different kind of evidence and it is
kept apart from the shapes rather than added to them, with its own manifest, its
own expectation table, and the redistribution split
[§8](../../docs/roadmap/recorded-motion-sources.md) describes.

The parser reads it exactly as it reads a two-joint fixture. What that file
*means* — that its unit is centimetres, that +Y is up, that only its root
translates, that its seven `torso_*` joints are one spine — is measured, written
down in
[`recorded/manifest.json`](tests/corpus/recorded/manifest.json), and acted on
nowhere in this library. Those are the facts a producer profile will be written
from, and the profile is where they become decisions.
