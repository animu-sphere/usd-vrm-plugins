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

Both halves. The syntax model (`BvhDocument.h`), the parser (`BvhParser.h`), the
frozen diagnostic set (`Diagnostics.h`), and the extractor (`BvhExtract.h`) that
turns a `BvhDocument` into `motionSource` values — which is what took the
declared `motionBvh -> motionSource` edge
([WORKSPACE.md §2](../../docs/architecture/WORKSPACE.md)).

That edge is the library's only one, and it is not a route to OpenUSD by another
name: `motionSource` links `motionCore`, whose `Gf` value types nothing here
names, because three numbers on an `OFFSET` line are not a vector in any basis
this layer knows.

What the edge cost is the **binary** half of the boundary check, which is now
removed rather than narrowed. Whether a built artifact records an OpenUSD import
turns out to be the linker's answer and not this library's: MSVC pulls only the
archive members something references, GNU ld with `--as-needed` drops the
resulting unused entries, and Apple's ld64 records every library on the link
line regardless. One source tree, two answers, so the check was measuring a
toolchain. The rule that survives is about **source** — no OpenUSD name in any
file here, extractor included — and it is platform independent.
`tests/check_boundaries.py` carries the measurement.

What the extractor decides is how a channel set becomes a track, and nothing
else. Three of those answers are worth knowing before reading a converted clip:
position channels **state** a joint's local translation rather than adding to
its `OFFSET`; a component a joint did not animate falls back to the `OFFSET`
rather than to zero; and the Euler order is the *relative* order of the rotation
channels, whatever position channels sit between them. Each is argued in
`BvhExtract.h`.

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
the only ones the parser raises; six are semantics and belong to the layer where
a document meets a profile. `DiagnosticIsSyntax` states the split, and the
boundary check fails a parser source that names a semantic code.

The extractor is granted exactly one of the six, `VRM_BVH_INVALID_ROTATION_ORDER`,
by name in that check. The grant is narrow because the reason is: a joint
declaring two rotation channels, or the same axis twice, forms no Euler order
whoever wrote the file — no profile is involved, and the layer raising it holds
none.

A failure the set does not name is `VRM_BVH_PARSE_FAILED` with a precise
`detail` — a file declaring ten frames and carrying eight is the standing
example. Adding a code the moment a parser meets a new file is exactly the drift
freezing the set prevents.

## Build

```sh
cmake -S libs/motionBvh -B build/motion-bvh -DCMAKE_PREFIX_PATH="<motionSource-prefix>;<usd-install>"
cmake --build build/motion-bvh --config Release
ctest --test-dir build/motion-bvh -C Release --output-on-failure
cmake --install build/motion-bvh --prefix <prefix> --config Release
```

A standalone configure needs `motionSource` on the prefix path, and OpenUSD
behind it — `motionSource` reaches `motionCore`, which is where the `Gf` value
types come from. Nothing here names one; the prefix is for the edge, not for
this library's own code.

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
