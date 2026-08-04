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

The value model and its invariants, and the profile contract over them:

| Header | Holds |
| --- | --- |
| `SourceSkeleton.h` | `SourceJoint`, `SourceSkeleton`, the name/hierarchy queries a profile matches against, `ValidateSourceSkeleton` |
| `SourceAnimation.h` | `SourceJointTrack`, `SourceAnimation`, the Euler order and angle unit a source declares, `ValidateSourceAnimation` |
| `SourceProvenance.h` | what file this was, under which profile, from which writer |
| `SourceProfile.h` | what one producer's export means: the stated vocabulary, `SourceProfile`, `ValidateSourceProfile`, `MatchSourceProfile` and its typed refusals |
| `SourceProfileFile.h` | a profile as a file: the keys, the small language they are written in, and the line a file that gets one wrong is told about |
| `CanonicalMetadata.h` | provenance's crossing into `motionCore` |
| `CanonicalConversion.h` | the converter: the change of basis, the angle composition, the path rule, the rest pose, the root policies, and the four ways a conversion refuses |

The profiles themselves are **data**, in [`profiles/motion/`](../../profiles/motion/)
rather than here: a product name may appear in one precisely because no code in
this library has a name for it.

Still to come: the second producer's profile
([§12](../../docs/roadmap/recorded-motion-sources.md)).

## The decisions the model is shaped by

Each is argued where it is implemented, because each would have been invisible
in a diff:

| Decision | Where |
| --- | --- |
| Values stay in the source's convention, so nothing here is a `GfVec3f` — a vector implies a basis this layer does not know | `SourceSkeleton.h` |
| A skeleton is a value on its own, because a profile is matched against a rig before a frame is read | `SourceSkeleton.h` |
| A rotation is stored the way the source expressed it — angles with their order, or quaternions, never converted between the two here | `SourceAnimation.h` |
| An unanimated component is an empty vector, never a run of identity values | `SourceAnimation.h` |
| Every convention has an `Unspecified` and validation refuses it, so a profile nobody finished is a refusal rather than a silent set of answers | `SourceProfile.h` |
| A joint map is a hierarchy embedding, not a name lookup — the near-miss profile is the one where every name matched and the body is assembled wrong | `SourceProfile.h` |
| A match returns facts and never a score; a confidence is the detector's arithmetic over them — except the one count that arithmetic gets wrong, which the match carries | `SourceProfile.h` |
| A name table is an array sized by its enum and asserted in enumerator order, so a vocabulary that grows without its spelling is a compile error | `SourceProfile.cpp` |
| A profile file is a stated subset and an unknown key is refused, because a `requred:` quietly dropped unbinds a joint the profile called mandatory | `SourceProfileFile.h` |
| A profile that is read is already valid, so no caller re-proves that there is no default profile | `SourceProfileFile.h` |
| The change of basis is one signed permutation and handedness is its determinant, so a left-handed source is mirrored once rather than corrected twice | `CanonicalConversion.h` |
| A bound bone's local rotation is the composition of the path above it, so a joint no profile maps is not a rotation thrown away | `CanonicalConversion.h` |
| The rest pose is built by that same walk, because a second traversal is a second traversal that can disagree with the first | `CanonicalConversion.h` |
| A quaternion track is refused with a reason, because no reader writes one and converting it would test a path against a value this repository invented | `CanonicalConversion.h` |

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

## A refusal is typed here and a diagnostic code in the caller

The semantic half of a reader's frozen diagnostic set is raised where a document
meets a profile — which is this library, the one forbidden to know that reader
exists. So `MatchSourceProfile` returns a `SourceProfileRefusal` naming the
*event* in terms no format supplies, and a caller holding both a reader and a
profile maps it onto that reader's codes. Structural invariants of the values
this library owns stay plain text, which is a different thing and stated as one
([`SourceProfile.h`](include/motionSource/SourceProfile.h),
[recorded-motion-sources.md §10](../../docs/roadmap/recorded-motion-sources.md)).

## No producer, no format

There is no producer name and no format name in this library, no default
profile, and no `if (producer == ...)` — the same rule the reader below it
follows, one layer up where it is easier to break. A profile's *data* carries a
product name and its *code* never does, which is the line
[WORKSPACE.md §1](../../docs/architecture/WORKSPACE.md) draws and the test of
whether it has been crossed: ship every profile and this library is
byte-identical. `motionSource_boundaries` checks it on every build, together
with the claim that exactly four files name a canonical type at all —
`CanonicalMetadata`, `SourceProfile`, `SourceProfileFile` and
`CanonicalConversion` — and that only those four name a `Gf` value type. Stage,
`Sdf` and plugin APIs are forbidden in every file including the converter's:
authoring a clip belongs to a caller, and a library that opened a stage would
have stopped being one.

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
