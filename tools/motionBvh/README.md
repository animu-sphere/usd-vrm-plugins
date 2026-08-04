# motion_bvh_inspect

Reports what a BVH file contains, in the file's own words.

```console
$ motion_bvh_inspect capture.bvh
source:    capture.bvh
joints:    27
depth:     12 level(s)
channels:  162 per frame
frames:    853
frameTime: 0.02 s (~50 Hz)
```

That is the whole claim. It reports no unit, no up axis, no handedness, no
rotation order and no humanoid bone, because a BVH file states none of them —
they are facts about the application that *wrote* the file, and they live in a
declarative producer profile one layer up
([recorded-motion-sources.md §2](../../docs/roadmap/recorded-motion-sources.md)).

## Sections

The summary always prints. Each flag adds one block, in a fixed order.

| Flag | Block |
| --- | --- |
| `--hierarchy` | Joints in declaration order, indented by depth, with offsets, channels, `End Site` terminators and row columns |
| `--channel-map` | Which joint and channel each row column is |
| `--frame N` | One motion row, by joint, 0-based |
| `--ranges` | Per-column smallest and largest value across every frame |
| `--all` | The three above |

`--max-depth`, `--max-joints` and `--max-frames` forward the parser's limits.
They are refusals of the pathological case rather than limits anyone will meet
(`BvhParser.h`), and a caller with a genuine reason can raise any of them.

Exit status: **0** the file was read, **1** the file was refused, **2** the
command was wrong. A refused file prints one diagnostic line on stderr and no
report at all — the parser reads a file whole or not at all, so half a summary
would read as a fact about the file rather than as the point it gave up.

## What `--ranges` is for

It is the block that measures a producer. Whether a root translation is in the
tens or the hundredths is what separates one writer's unit from another's, and
whether a joint's position channels move at all is what separates translation
animation from a rest offset restated every frame. Both are measurements, and
both are what a profile has to be written from — which is why the tool that
takes those measurements comes before the profile schema that consumes them
(BVH-0 in the [plan](../../docs/roadmap/recorded-motion-sources.md)).

## What it does not do yet

[WORKSPACE.md §1](../../docs/architecture/WORKSPACE.md) describes this tool as
reporting what a file contains *and optionally* which profiles are candidates
for it, with reasons. There is no profile contract yet, so there is nothing to
be a candidate for. Reporting candidates arrives with the profiles; writing a
detector first would settle the profile schema on whichever file happened to be
inspected first, which is the failure the plan's ordering exists to prevent.

`motion_bvh_convert` — BVH plus an explicitly named profile to the
avatar-independent semantic clip — lands in this same directory, which is why
the CMake project here is named after the layer rather than after the one
executable in it.

## Build

```sh
cmake -S tools/motionBvh -B build/motion-bvh-tools \
      -DCMAKE_PREFIX_PATH="<prefix holding motionBvh and motionSource>;<usd-install>"
cmake --build build/motion-bvh-tools --config Release
ctest --test-dir build/motion-bvh-tools -C Release --output-on-failure
```

**OpenUSD is needed to configure, and by nothing this tool contains.**
`find_package(motionBvh)` resolves `motionSource` and, through it, `pxr` — the
declared `motionBvh -> motionSource` edge — so the prefix path needs an OpenUSD
install even though no source file here names a `Gf` type and no code path
reaches one. On macOS the linker also records those dylibs on the executable,
because ld64 keeps every library on the link line whether or not a symbol is
used; MSVC and GNU ld with `--as-needed` do not. All three are correct about
their own artifact, which is why `motion_bvh_inspect_boundaries` no longer
inspects one — it checks the link line and the source, both of which say the
same thing on every platform.

## Tests

`motion_bvh_inspect_report` drives the CLI over the library's committed corpus
and checks every number it prints against two things the tool never touches: the
corpus manifest, whose measured fields come from an independent Python scanner,
and a reading of the `.bvh` text done in the test itself. A test that asked the
parser what a file says and then checked the tool agreed would be one
implementation agreeing with itself.
