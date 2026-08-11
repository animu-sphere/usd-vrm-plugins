# mocopi packet corpus — datagrams the decoder replays

The fixtures the mocopi packet decoder runs against. Each `.mocopipackets` file
is a capture in the format documented on
[`PacketCapture.h`](../../include/vrmAdapterMocopi/PacketCapture.h):
line-oriented text, one `d` record per datagram, hex bytes with an ASCII gutter,
deterministic to six decimals.

**[`manifest.json`](manifest.json)** is the machine-readable source of truth
(provenance, licence, datagram counts, the packet kinds and chunk tags present,
bone counts, digests, and the phenomenon each capture pins). Its measured fields
are derived from the captures by
[`tools/generate_packets.py`](../../tools/generate_packets.py) and re-checked by
`vrmAdapterMocopi_packetGen`, so they cannot drift out of agreement with the
fixtures; its prose fields (`pins`, `tags`) are hand-written. This file is the
operator's guide.

## What these are, and the one thing they are not

The grammar these bytes encode is **measured**: 8000 datagrams across five real
device sessions and four distinct motions, walked until the arithmetic closed at
every level of every datagram. The claims and the population each was measured on
are written out on
[`MotionPacket.h`](../../include/vrmAdapterMocopi/MotionPacket.h).

The **content** is invented. The rest pose here is clean round-number
proportions, because a real skeleton packet is a body measurement of a real
person and is not a thing to commit to a public repository — and the sessions the
grammar came from hold somebody's motion, so they are evidence that stays off
disk here too.

Which leads to the sentence worth being blunt about:

> **These fixtures pin the decoder, not the protocol.** The grammar that wrote
> them is the grammar they are decoded with, so no capture in this directory can
> be surprised by the wire format. A wrong measurement would make the generator
> and the decoder agree, and both be wrong.

That is not a defect in the corpus — it is what a regression test is for — but it
does mean the corpus is not the evidence. Two things close the gap, in this
order:

1. **The vendor's `BVH Sender`**, pointed at a `.bvh` this repository wrote
   ([`libs/motionBvh/tests/corpus/generated/`](../../../../../libs/motionBvh/tests/corpus/generated/)).
   Its *encoding* is the vendor's own and its *content* is ours, so it is a
   capture that can genuinely refute the grammar and that this project may still
   commit and public CI may still run. When an operator makes one it belongs in
   this directory, and `test_motion_packet.cpp` grows the case it deserves.
2. **The cross-source comparison** of
   [the plan](../../../../../docs/roadmap/adapters-mocopi-vmc-ardy.md) §9.6 — the
   same physical session observed natively and relayed through VMC — which is
   the release's distinguishing check and cannot be faked by either corpus.

This is the same split the BVH track already runs on
([`generated/` versus `recorded/`](../../../../../libs/motionBvh/tests/corpus/)),
with a wrinkle the plan names: a `BVH Sender` capture is neither of those two
things, because its bytes are the vendor's and its motion never met a sensor.

## Why packets and not traces

A [`motion-capture-trace`](../../../../../libs/motionRuntime/include/motionRuntime/CaptureTrace.h)
records what an adapter *produced*. This records what it was *given*:

```text
packets in  ->  [ mocopi-packet-capture ]  ->  decode  ->  map
            ->  [ motion-capture-trace ]   ->  the canonical pipeline
```

Recording traces here would mean the decoder's own tests are fed by the decoder,
and every packet-level failure — a truncated datagram, a duplicate delivery, a
restart mid-frame — would be untestable, because a trace cannot represent one.
Both formats exist because the adapter has two ends.

## No side is named anywhere in here

Handedness is unresolved, and deliberately so: a mirrored basis satisfies every
measurement taken so far, because a mirrored skeleton is geometrically
identical. So the arm and leg bones are described by their side of the **+X
axis** and by their **bone id**, never as left or right. Settling it needs an
asymmetric motion whose side is *labelled*, which is an operator's session and
not a commit — and a fixture that guessed would be the hardest place to notice
the guess later.

## The set

| Capture | Datagrams | Pins |
| --- | --- | --- |
| `neutral-standing-60hz.mocopipackets` | 6 | The happy path, and both packet kinds in one file: a skeleton then five 60 Hz frames, every rotation identity. Also the measured invariant that **only the root translates** — every non-root frame translation equals its rest offset bit for bit. |
| `arms-lowered-60hz.mocopipackets` | 4 | A non-identity rotation on the two upper arms, in opposite directions: ~85° carried in the **third** imaginary component, which is what the measured standing sessions showed against a T-pose rest. A decoder that reordered the quaternion cannot pass this and the baseline at once. |
| `frame-loss-60hz.mocopipackets` | 8 | What the transport does and the decoder refuses none of: an `fnum` gap of 3 whose `time` delta is exactly 3/60 s — the measured Wi-Fi loss shape, where the sender's clock never skipped — then a duplicate delivery, then a restart. |
| `malformed-container.mocopipackets` | 5 | One datagram per **container**-level refusal: empty, a header that does not fit, a payload longer than the datagram, a walk that ends between chunks, and a datagram of the sibling protocol. |
| `malformed-packets.mocopipackets` | 12 | One datagram per **packet**-level refusal: the wrong magic, an unmeasured version, a missing `head`, both payload kinds, neither, a duplicated field, two bad field widths, and three unusable clocks. |
| `refused-bones-60hz.mocopipackets` | 2 | The bone-not-frame rule: three unusable bone records decode to **24 usable bones and three diagnostics**, not to a refused datagram — followed by the same frame with nothing wrong with it. |
| `extended-form.mocopipackets` | 4 | Everything carried forward unread: unknown chunks beside the payload, inside `fram`, and inside all 27 bone records (reported **once**, not 27 times); an 11-bone rig that is a different rig and not a malformed packet; and the only non-zero `tmcd` anywhere. |

The two `malformed-*` captures are a pair, not a duplicate.
`malformed-container` dies before a field means anything, so every one of its
datagrams is refused by the chunk walk; `malformed-packets` walks as a container
every time and is refused one layer up. Only the second can carry a refusal that
names a *field*, because the first has no field left to name — and the empty
datagram sits in the first while being refused by the second layer, which is
itself the boundary being drawn.

## Regenerating

```sh
python adapters/liveCapture/mocopi/tools/generate_packets.py
```

Three CTest names guard the result, and they check different things:

- `vrmAdapterMocopi_corpus` — every capture parses and re-emits byte-identically
  through the **C++ writer**, so a fixture cannot drift from the format.
- `vrmAdapterMocopi_motionPacketCorpus` — every capture decodes to what the
  manifest says it pins, or is refused with the code it exists to be refused
  with. A capture with no assertion registered against its `sourceId` fails
  rather than passing quietly.
- `vrmAdapterMocopi_packetGen` — the committed bytes still match the
  **generator** that authored them. This matters more here than for the sibling
  adapter: regenerating is the only way this corpus can be extended, because the
  measurement it was written from is not committed.
