# motion_bvh_inspect · motion_bvh_convert

Two commands, one directory, and the split between them is the layer boundary:
`motion_bvh_inspect` reports what a file **says**, and `motion_bvh_convert`
reads it the way a named producer **meant** it. That is why the CMake project
here is named after the layer rather than after either executable, and why they
do not share a link line — the first names no OpenUSD at all, and the second
authors a stage.

## motion_bvh_inspect

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

## motion_bvh_convert

BVH plus an explicitly named profile, to the avatar-independent semantic clip
`motion_retarget` already consumes.

```console
$ motion_bvh_convert capture.bvh --profile <id> --output canonical.usda
source:   capture.bvh
profile:  <id> (<producer>)
joints:   27 read, 22 bound, 5 ignored
frames:   853 at 50 Hz (17.04 s)
dropped:  0 joint(s) whose translation varied
restated: 26 joint(s) restating rest geometry
composed: spine, chest, upperChest, head
output:   canonical.usda
```

It stops at the clip on purpose. Baking onto an avatar is the next command and
a separate one:

```console
$ motion_retarget --motion canonical.usda --avatar avatar.vrm --output result.usda
```

That split is what makes one recording reusable across avatars, separates a
parsing failure from a retarget failure, and keeps this tool free of VRM schema
details — the source-rest-to-target-rest correction belongs to `vrmRetarget`,
which v0.4.0 already shipped, and a converter that applied it would be a second
one ([§4, §5](../../docs/roadmap/recorded-motion-sources.md)).

### There is no default profile

A BVH file states no producer, so this tool never guesses one. `--profile` is
required, and without it the run stops with `VRM_BVH_PROFILE_REQUIRED`. The
failure that forbids is specific: a near-miss profile produces motion that is
*subtly misassembled* rather than absent, which is worse than a refusal because
it looks like a result.

`--profile` takes either an id or a path. An id is looked up as `<id>.yaml`,
first hit wins:

1. every `--profile-dir`, in the order given
2. `USDVRM_MOTION_PROFILE_PATH`, a list in the platform's PATH separator
3. `<exe>/../share/usd-vrm-plugins/profiles/motion` — an install prefix
4. `<exe>/../../../profiles/motion` — this repository

The third is why a packaged artifact works with no flags at all: the profiles
ship beside the tools, and a converter with none available refuses every file it
is given. A request that *is* a path is opened as given. The difference matters
in one more place: when you name an id, the file's own `id` must match it, so a
profile renamed on disk cannot make a conversion record an id it never read.

### Report and exit status

Sections are fixed, so two runs over one file are byte-identical. `dropped` and
`restated` are deliberately two lines rather than one: a rig restating its rest
geometry every frame lost nothing, and a rig whose elbow actually translates
lost motion, and one word for both would hide the second inside the first.
`composed` names the bones that absorbed a chain of joints no profile maps —
not a warning, but the thing a cross-source comparison needs in order to tell a
composition residual apart from a real disagreement.

Exit status: **0** the clip was written, **1** the conversion produced no clip,
**2** the command or something it named was wrong. A profile that will not load
is a 2 even though it is nobody's typo — the `.bvh` is fine, and sending whoever
ran it to look at their capture would be the wrong place.

## Build

```sh
cmake -S tools/motionBvh -B build/motion-bvh-tools \
      -DCMAKE_PREFIX_PATH="<prefix holding motionBvh and motionSource>;<usd-install>"
cmake --build build/motion-bvh-tools --config Release
ctest --test-dir build/motion-bvh-tools -C Release --output-on-failure
```

OpenUSD is needed on the prefix path, for two different reasons that are worth
keeping apart. **`motion_bvh_convert` links it**, because authoring a clip is
what it is for. **`motion_bvh_inspect` does not, and still needs it to
configure**: `find_package(motionBvh)` resolves `motionSource` and, through it,
`pxr` — the declared `motionBvh -> motionSource` edge — even though no source
file behind that executable names a `Gf` type and no code path reaches one. On
macOS the linker also records those dylibs on the inspect executable, because
ld64 keeps every library on the link line whether or not a symbol is referenced;
MSVC and GNU ld with `--as-needed` do not. All three are correct about their own
artifact, which is why the boundary check does not inspect one — it checks the
link line and the source, both of which say the same thing on every platform.

The two executables have two different boundaries and the check is run twice,
once per target. `motion_bvh_convert` is invoked with `--crossing`: it may
author a stage and name the humanoid vocabulary, and it still may not name
`vrmRetarget` or `vrmSchema`, because the target avatar is the one thing this
layer never binds to. The set of files each rule applies to is read out of that
target's `add_executable` call, so a source moved between the two executables
cannot keep the permissions of the one it left, and a source compiled by neither
is an error rather than a file exempt from both.

## Tests

`motion_bvh_inspect_report` drives the CLI over the library's committed corpus
and checks every number it prints against two things the tool never touches: the
corpus manifest, whose measured fields come from an independent Python scanner,
and a reading of the `.bvh` text done in the test itself. A test that asked the
parser what a file says and then checked the tool agreed would be one
implementation agreeing with itself.

`motion_bvh_convert_clip` does the same for the converter, and takes the same
care: it reads the `.bvh` and the profile itself, composes the Euler angles and
walks the joint paths in its own code, and compares the clip on disk against
that. The rest translations are checked against the sums of the `OFFSET` lines
along each bound chain, the hips track against the root's position columns, and
the rotations against an independent intrinsic composition — so a converter that
took a mapped joint's rotation verbatim instead of composing the path fails on
every bone with an unmapped segment above it. Two premises are asserted rather
than assumed, because a later profile edit could leave every comparison
silently checking the wrong numbers: that the profile states the canonical
basis, and that its unit is centimetres.

The end-to-end test is not here — it drives two tools, so it is registered at
the workspace root as `workspace_bvh_end_to_end`
([tests/motion/](../../tests/motion/)).
