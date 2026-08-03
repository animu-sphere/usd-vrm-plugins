# BVH corpus — format shapes

The fixtures the parser reads. `generated/` holds hand-written BVH files, one
per shape the format can take, committed and runnable with no hardware.

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

## Why no fixture is named after a producer

Every file here is named after the shape it pins — `valid-channel-order-yxz`,
`malformed-frame-width` — and never after an application. A `mocopi-*.bvh` or a
`rokoko-*.bvh` in the *syntax* layer's corpus is the first place one producer's
export quietly becomes the format's definition, and the resulting assumptions —
joint names, unit, axes, root policy — are invisible until a second producer
disagrees ([recorded-motion-sources.md §1](../../../../docs/roadmap/recorded-motion-sources.md)).

None of these files carries a real skeleton, a real unit, or a real basis
either. Joint names are placeholders and offsets are invented proportions,
because this layer refuses to interpret any of them.

## Where the real files go

Producer exports land with the profiles that describe them, under
`tests/corpus/recorded/`, split the way the roadmap
[§8](../../../../docs/roadmap/recorded-motion-sources.md) describes:

```text
recorded/
├─ redistributable/   real files cleared for publication
└─ manifests/         everything else, as measured facts and no bytes
```

A file that cannot be redistributed leaves a manifest — hash, exporting tool and
version, producer identity and version, profile id, frame time, joint and
channel counts, coordinate convention, unit, root policy, the bones it is
expected to map, expected diagnostics, validation date, redistribution status —
and no bytes. That is enough for a later reader to tell whether a claim still
holds without the file.

## Line endings

`.gitattributes` pins `*.bvh` to LF. The parser accepts CRLF and proves it in
`TestWriterVariation`, which builds the CRLF text in memory: a committed fixture
whose bytes changed with the checkout would make the manifest's hashes
platform-dependent, and hashes that differ per OS pin nothing.
