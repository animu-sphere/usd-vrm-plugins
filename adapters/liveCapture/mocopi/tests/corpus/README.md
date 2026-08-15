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

## Sides, and why the fixtures still do not lean on them

Handedness is **resolved**: the basis is right-handed and **+X is the body's
left**, so in `bnid` order 11–14 is the left arm, 15–18 the right, 19–22 the left
leg and 23–26 the right. It was settled on 2026-08-12 by comparing this rig
against the one
[`mocopi-mobile-bvh-default-v1`](../../../../../profiles/motion/mocopi-mobile-bvh-default-v1.yaml)
measured from the same application's BVH export — all 27 rest offsets agree sign
for sign to 4.4e-7 m — and the adapter
[README](../../README.md#the-decoder-stops-where-the-measurement-does) carries
the argument.

The fixtures below still describe arm and leg bones **by bone id**, and the
`_xPos`/`_xNeg` vocabulary is gone rather than replaced with left/right. That is
not leftover caution. The decoder these captures test names no bone and performs
no basis change; a fixture that asserted "the left arm rotated" would be
asserting something this layer does not compute, and would go green or red for
reasons belonging to the joint map two layers up.

**That component now exists, and it reads this same directory** —
`vrmAdapterMocopi_skeletonMapCorpus`, a third pass over the captures asking what
each becomes once its joints carry canonical bones. So the sides are finally
asserted, on the layer that computes them, and `arms-lowered-60hz` is where: the
left forearm's rest direction is rotated by the sample and compared against where
a lowered arm has to be.

One thing that pass measured is worth having here, because it is a property of
the *rig* rather than of the test. **A bilaterally symmetric rig cannot be asked
which arm moved.** Swap left and right in the map's table and every "both arms
went down" assertion still passes, because the rest direction and the rotation
are swapped together — verified by doing it. What catches the swap is asking
which arm is *where*: the left arm's rest offsets run along +X. Sides are a fact
about the basis, and only the basis can be asked about them.

## The set

| Capture | Datagrams | Pins |
| --- | --- | --- |
| `neutral-standing-60hz.mocopipackets` | 6 | The happy path, and both packet kinds in one file: a skeleton then five 60 Hz frames, every rotation identity. Also the measured invariant that **only the root translates** — every non-root frame translation equals its rest offset bit for bit. |
| `arms-lowered-60hz.mocopipackets` | 4 | A non-identity rotation on the two upper arms (bones 12 and 16 — the left and the right, now that handedness is settled), in opposite directions: ~85° carried in the **third** imaginary component, which is what the measured standing sessions showed against a T-pose rest. A decoder that reordered the quaternion cannot pass this and the baseline at once. The assertion is still on the component and the antisymmetry, not on a side. |
| `frame-loss-60hz.mocopipackets` | 8 | What the transport does and the decoder refuses none of: an `fnum` gap of 3 whose `time` delta is exactly 3/60 s — the measured Wi-Fi loss shape, where the sender's clock never skipped — then a duplicate delivery, then a restart. Its restart goes backwards by only 0.1 s, which is inside any jitter threshold worth having, so it is also the capture that proves the counter and not the clock is what catches one. |
| `session-restart-60hz.mocopipackets` | 9 | A restart the session **recovers** from, which is the half `frame-loss-60hz` cannot show: that file ends on the restart's own frame, so its new session never declares a rig and never emits one. Here the rig is dropped, two frames are refused for want of one, a second skeleton packet re-declares it, and the session resumes — the device's real shape, since the rest table is repeated about every 3.5 s. It is the only capture that can tell `MocopiLiveSource`'s two restart policies apart, and its old session is recorded twenty seconds into its stream so that the new one's clock is behind *all* of it rather than overtaking it within two frames. |
| `malformed-container.mocopipackets` | 5 | One datagram per **container**-level refusal: empty, a header that does not fit, a payload longer than the datagram, a walk that ends between chunks, and a datagram of the sibling protocol. |
| `malformed-packets.mocopipackets` | 13 | One datagram per **packet**-level refusal, and the row adds up: the wrong magic, an unmeasured version, a missing `head`, both payload kinds, neither, a duplicated field (1+1+1+1+1+1), three bad field widths — `fnum`, `tran`, `tmcd` — three unusable clocks, and a **skeleton** with one unusable rest transform. |
| `refused-bones-60hz.mocopipackets` | 2 | The bone-not-frame rule: three unusable bone records decode to **24 usable bones and three diagnostics**, not to a refused datagram — followed by the same frame with nothing wrong with it. Its mirror image is the last datagram of `malformed-packets`: the same defect in a *skeleton* refuses the packet whole, because a rest table with a hole in it has bones whose `pbid` names an id that is not there. |
| `incomplete-frame-60hz.mocopipackets` | 3 | `refused-bones-60hz`'s three damaged records with a **skeleton packet in front** and a clean frame behind. It exists because "incomplete" is only meaningful against a rig: the decoder and the skeleton map treat this capture and its sibling *identically* — both corpus passes assert so — and the only difference between the two files is the declared rig. That is what turns three refused records into `VRM_MOCOPI_FRAME_INCOMPLETE` on an **emitted** frame one layer up, where the rig-less file gives two frames refused for having none. |
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

Six CTest names guard the result, and they check different things. Five of them
are readings of this same directory at successive layers, which is the point:
one set of bytes, asked a different question by each component that consumes it,
so a fixture is never merely parsed by the layer it was written for.

- `vrmAdapterMocopi_corpus` — every capture parses and re-emits byte-identically
  through the **C++ writer**, so a fixture cannot drift from the format.
- `vrmAdapterMocopi_motionPacketCorpus` — every capture decodes to what its
  `sourceId` claims it pins, or is refused **per datagram** with the code it
  exists to be refused with. A capture with no assertion registered against its
  `sourceId` fails rather than passing quietly.

  It does **not** read `manifest.json`: the assertions are hand-written C++ named
  after the phenomenon. So the chain is manifest → bytes (`_packetGen`) and bytes
  → decoder (this), with nothing asserting a `pins` string against behaviour. A
  `pins` field is a claim addressed to a reviewer, and treating it as an
  assertion would be reading a description as a specification.
- `vrmAdapterMocopi_skeletonMapCorpus` — what each capture becomes once its
  joints carry canonical bones: which arm is which, which bones a refused record
  costs, and which rigs get no map at all. Registered per `sourceId` like the
  pass above, including for the two `malformed-*` captures, whose assertion is
  that nothing reaches this layer from them — a claim that can stop being true.
- `vrmAdapterMocopi_frameAssemblerCorpus` — the sequence questions, which every
  layer below answers one datagram at a time and so cannot: a gap, a duplicate
  delivery, a restart, and whether a frame short of the declared rig is emitted
  or refused.
- `vrmAdapterMocopi_liveSourceCorpus` — the first reading whose subject is a
  **pose a consumer sampled** rather than something a layer produced. It makes
  the one cross-layer claim: every frame the assembler emitted was admitted by
  the intake, because the assembler emits strictly advancing frames within a
  session and that is exactly the ordering `LiveCaptureSource::Push` requires.
  It replays each capture through a single reused buffer, so "a datagram need not
  outlive the call" is checked by the poses matching rather than by an assertion
  about pointers, and it is the only place the two restart policies are run over
  the same bytes.
- `vrmAdapterMocopi_packetGen` — the committed bytes still match the
  **generator** that authored them. This matters more here than for the sibling
  adapter: regenerating is the only way this corpus can be extended, because the
  measurement it was written from is not committed.
