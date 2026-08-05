# BVH corpus

The fixtures the parser reads, in two halves that are never mixed.

`generated/` holds hand-written BVH files, one per shape the format can take,
committed and runnable with no hardware. `recorded/` holds real producer
exports. Each half has its own manifest, its own expectation table in
[`test_bvh_parser.cpp`](../test_bvh_parser.cpp), and its own kind of claim: a
generated fixture pins a shape of the format and its counts are a property of
what was written here, where a recorded file pins what one application actually
exports and its counts are a measurement of someone else's software that nobody
here may adjust.

**[`manifest.json`](manifest.json)** is the machine-readable source of truth:
what each file pins, what it must produce or be refused for, its measured joint,
channel and frame counts, its frame time, its size and its hash. The measured
fields are produced by
[`tools/check_corpus.py`](../../tools/check_corpus.py) and re-checked by
`motionBvh_corpusManifest`; the `pins` prose is hand-written. This file is the
reader's guide.

## Why the counts are measured twice

`motionBvh_corpus` runs the C++ parser over every fixture against a table of
expectations in [`test_bvh_parser.cpp`](../test_bvh_parser.cpp). That is one
implementation agreeing with itself: a parser that miscounted a channel and a
fixture written to match it would both be green.

`check_corpus.py` reads the same files with a scanner written from the format
rather than from `BvhParser.cpp`, and the manifest is checked against *that*.
Where the two disagree, one of them is wrong and the fixture is not evidence of
anything until that is settled.

## Why no *generated* fixture is named after a producer

Every file in `generated/` is named after the shape it pins —
`valid-channel-order-yxz`, `malformed-frame-width` — and never after an
application. A `mocopi-*.bvh` sitting among the format shapes is the first place
one producer's export quietly becomes the format's definition, and the resulting
assumptions — joint names, unit, axes, root policy — are invisible until a
second producer disagrees
([recorded-motion-sources.md §1](../../../../docs/roadmap/recorded-motion-sources.md)).

None of those files carries a real skeleton, a real unit, or a real basis
either. Joint names are placeholders and offsets are invented proportions,
because this layer refuses to interpret any of them.

In `recorded/` the rule is the opposite, and for the same reason: which
application wrote a real file is the load-bearing fact about it, so the name
carries the producer and the export variant. The naming rule was never "no
producer names anywhere" — it is that a producer's answers must be attributable
to that producer, which means naming them where they are real and refusing to
imply them where they are not.

## The recorded half

```text
recorded/
├─ manifest.json      what each file is, and what was measured from it
├─ redistributable/   real files cleared for publication
└─ fetched/           git-ignored; where a row with no bytes gets its file
```

A file that cannot be redistributed is a row in the same manifest with no bytes
beside it — hash, exporting application and version, original file name, capture
date, frame time, joint and channel counts, **the hierarchy**, the coordinate
and unit observations, expected diagnostics, redistribution status. That is
enough for a later reader to tell whether a claim still holds without the file
([roadmap §8](../../../../docs/roadmap/recorded-motion-sources.md)).

`hierarchy` — joint names with their parents, parent before child — is the one
of those that is load-bearing rather than descriptive, and it is measured rather
than written. It is what a producer profile is *checked against*: a profile
claims a root, a mapping and an implied chain, and
[`check_motion_profiles.py`](../../../../scripts/check_motion_profiles.py) holds
that claim against this field when the file is absent and against the file
itself when it is here. So a recording this repository may not carry still costs
nothing in coverage except one link — that the row is a faithful reading of
those bytes — and that link is closed by anyone who fetches:

```bash
python scripts/fetch_corpus.py --corpus motion \
    --accept-license bandai-namco-motiondataset
python libs/motionBvh/tools/check_corpus.py --check --recorded \
    --corpus libs/motionBvh/tests/corpus
```

The first downloads into `fetched/` against the row's pinned SHA-256; the second
re-derives every measured field with the same scanner that wrote it. Neither
runs in CI, and that is deliberate: a build lane that reached a third party's
server would make a PR's colour depend on someone else's uptime, and it would
automate an acknowledgement the corpus policy asks a person to make.

**Every `observations` entry is read out of a file that declares none of it.**
BVH states no unit, no up axis, no handedness, no rotation order and no humanoid
bone. So the manifest records that a 27-joint export puts its root at Y 95.9893
and restates every other joint's `OFFSET` in that joint's position channels
every frame — and it records those as evidence for a producer profile to settle,
not as anything this layer knows or may act on. The parser reads that file
exactly as it reads a two-joint fixture, and the day it does otherwise is the
day one application's export became the format's definition.

`motionBvh_recordedCorpus` runs the C++ parser over this half against its own
table, and `motionBvh_recordedManifest` re-measures it with the Python scanner.
The hash matters more here than in the generated half: a recorded file whose
hash moved is a different capture wearing the same name, and every observation
written about it stops being about the bytes on disk.

Two producers are described here now, which is the release condition
([roadmap §8](../../../../docs/roadmap/recorded-motion-sources.md)) and is worth
more than the count suggests. The second one disagrees with the first about what
a root joint is — it puts the locomotion on a reference node and the body's
orientation on its child — about what an `End Site` holds, and about whether a
rig's `OFFSET`s compose into a pose at all. Each of those looked like a property
of the format while one export was all anybody here had read.

This is the motion layer's first third-party fixture, not the repository's. The
importer has vendored real models since its
[spec samples](../../../../plugins/usdVrmFileFormat/tests/corpus/CORPUS.md),
under the same rule: committed only where redistribution is cleared, recorded in
a manifest either way.

## Line endings

`.gitattributes` pins `*.bvh` to LF. The parser accepts CRLF and proves it in
`TestWriterVariation`, which builds the CRLF text in memory: a committed fixture
whose bytes changed with the checkout would make the manifest's hashes
platform-dependent, and hashes that differ per OS pin nothing.
