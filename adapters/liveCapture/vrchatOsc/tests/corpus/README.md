# VRChat OSC corpus

Recorded datagrams, in the `vrchat-osc-packet-capture` format
([`PacketCapture.h`](../../include/vrmAdapterVrchatOsc/PacketCapture.h)), with
the extension `.vrchatoscpackets`.

**It is empty, and being empty is this milestone's position rather than an
omission.** VRC-0 ships the recorder and no decoder, so neither half of this
corpus can be authored yet without guessing:

- `generated/` fixes the protocol's *shapes* with no hardware, and a shape is
  something a decoder defines. Writing fixtures from the published specification
  before a real datagram has been measured is the failure
  [§6](../../../../../docs/roadmap/osc-and-vrchat-trackers.md#6-the-adapter-capture-precedes-decoder)
  exists to prevent — a specification says what a receiver must accept, not what
  a sender sends. It arrives with VRC-2.
- `recorded/` is the evidence, and it arrives from a device session (VRC-1).
  **That session happened on 2026-08-30** and left
  [a manifest](recorded/manifests/2026-08-30-mocopi-vrchat-osc.json) and no
  bytes, on the redistribution rule below: a tracker stream is a real person's
  motion in a real room. So this directory stays empty of captures while the
  measurement it exists for is done, which is the arrangement the split was
  designed to allow rather than a gap.

Until then the two CTest names that read this directory do not exist: they are
registered by globbing for the extension rather than by testing for the
directory, so they appear on the commit that adds the first capture and fail
from that moment on if it drifts. The siblings guard on the directory instead,
because theirs were created with fixtures already in them.

## The split, and what may be committed where

The policy is [adapter plan
§9.2](../../../../../docs/roadmap/adapters-mocopi-vmc-ardy.md#92-corpus)'s,
unchanged: generated and recorded never mix, and a session that cannot be
redistributed leaves a manifest and no bytes.

```text
generated/               protocol shapes, committed, CI-runnable, no hardware
recorded/
├─ redistributable/      real sessions cleared for publication
└─ manifests/            everything else, as measured facts
```

Both CTest names read the tree recursively, so a capture is checked wherever in
it the capture lives.

## What a committed capture must carry

`sender` and `sourceId`, or the corpus check refuses it. Provenance is the
manifest's job to *describe* and the fixture's job to *carry*: a capture naming
neither cannot be traced back to what produced it, and the operator who can still
supply them is the one who just ran the session.

`device` is not required and is asked for anyway. A VRChat OSC stream is
*relayed* — the sender is an application re-expressing some other device's
tracking — so a capture that names the application and not the device behind it
cannot answer the question
[§11](../../../../../docs/roadmap/osc-and-vrchat-trackers.md#11-the-fourth-observation-of-one-session)
exists to ask, which is whether one physical session observed four ways agrees
with itself.

## A VRChat client is never a test dependency

Every replay test here completes with nothing installed. The evidence is the
generated corpus and a recorded session; the client is not part of either, and
the hardware lane that produces the second is never a required PR gate.
