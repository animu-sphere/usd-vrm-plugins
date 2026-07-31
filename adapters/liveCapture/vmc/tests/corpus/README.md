# VMC packet corpus — recorded datagrams

The fixtures the VMC decoder replays. Each `.vmcpackets` file is a recorded
session in the format documented on
[`PacketCapture.h`](../../include/vrmAdapterVmc/PacketCapture.h): line-oriented
text, one `d` record per datagram, hex bytes with an ASCII gutter, deterministic
to six decimals.

**[`manifest.json`](manifest.json)** is the machine-readable source of truth
(provenance, licence, datagram counts, the address patterns present, and the
phenomenon each capture pins). Its measured fields are derived from the captures
by `tools/generate_packets.py` and re-checked by `vrmAdapterVmc_packetGen`, so
they cannot drift out of agreement with the fixtures; its prose fields (`pins`,
`tags`) are hand-written. This file is the operator's guide.

## Why packets and not traces

A [`motion-capture-trace`](../../../../../libs/motionRuntime/include/motionRuntime/CaptureTrace.h)
records what an adapter *produced*. This records what it was *given*:

```text
packets in  ->  [ vmc-packet-capture ]  ->  decode  ->  map
            ->  [ motion-capture-trace ]  ->  the canonical pipeline
```

Recording traces here would mean the decoder's own tests are fed by the decoder,
and every packet-level failure — a truncated datagram, a duplicate, a restart
mid-frame — would be untestable, because a trace cannot represent one. Both
formats exist because the adapter has two ends.

## Why these are synthetic

A recording made from a commercial sender application carries that application's
avatar, and the VRM corpus is already licence-gated for exactly that reason
([CORPUS.md](../../../../../plugins/usdVrmFileFormat/tests/corpus/CORPUS.md)). A
fixture nobody outside the project may redistribute is a fixture CI cannot run —
and the point of recording packets at all is that the adapter is verifiable with
no hardware and no socket.

So every byte here is assembled by
[`tools/generate_packets.py`](../../tools/generate_packets.py) from the OSC and
VMC message shapes: Apache-2.0 like the rest of the repo, byte-stable across
machines and operating systems, and reviewable in a diff. The OSC encoder is
written out in that file rather than pulled from a library, deliberately — a
corpus generated with the same implementation the decoder might later use would
agree with the decoder by construction rather than by the protocol.

**They are not sender-compatibility evidence.** They reproduce the shapes the
protocol produces, not any particular application's quirks. Two real sender
applications and a capture device relayed through one are Milestone B's, recorded
with the record tool and added here as they are measured
([the plan](../../../../../docs/roadmap/adapters-mocopi-vmc-ardy.md) §10).

## The set

| Capture | Datagrams | Pins |
| --- | --- | --- |
| `neutral-standing-30hz.vmcpackets` | 6 | The happy path: one bundle per frame, a full torso rig, every rotation identity. |
| `arm-raise-30hz.vmcpackets` | 117 | The other sender shape: one message per datagram, frame closed by a trailing `/VMC/Ext/T`, no `UpperChest`. |
| `mixed-traffic-30hz.vmcpackets` | 13 | Blend shapes, HMD, controller, camera, option string — well-formed, unimplemented, and *not* malformed. |
| `malformed-packets.vmcpackets` | 10 | One datagram per packet-level refusal, plus the two unsupported-but-valid cases for contrast. |
| `sender-restart-30hz.vmcpackets` | 10 | A duplicate delivery, a backwards sender clock, a frame cut off after six bones, and a restart. |

Three properties are deliberate and easy to lose:

- **The two clocks do not share an origin.** `d` records carry the *receive*
  clock; `/VMC/Ext/T` carries the sender's, which starts at 12.5 s in the
  neutral-standing capture. An adapter that conflates them passes every fixture
  whose clocks both start at zero.
- **Receive times never go backwards, sender times do.** Arrival order is what a
  recorder observes and a receive clock does not run backwards; disagreement
  between the two clocks is what `VRM_VMC_TIMESTAMP_REGRESSION` is about, and it
  lives in the payload.
- **Bones arrive in Unity's `HumanBodyBones` order**, in which the legs precede
  the spine and `UpperChest` sorts *last* of all — it was added to that enum
  after the fingers. Nothing may infer hierarchy from arrival order, and a corpus
  emitted parent-first would let an adapter get away with assuming otherwise.

## Verify

Both checks run under `ctest`:

```sh
ctest -R vrmAdapterVmc_corpus     # every capture parses and round trips byte-identically
ctest -R vrmAdapterVmc_packetGen  # the committed captures still match the generator
```

`vrmAdapterVmc_corpus` is the load-bearing one. It re-emits each committed
capture through `WritePacketCapture` and compares bytes, so a fixture can never
drift from the writer without turning a test red — which is what lets everything
downstream compare a golden result rather than merely parse one. The two are not
redundant: the corpus test holds a fixture to the *writer's* canonical form, and
`packetGen` holds it to the *generator*, so a hand-edited capture that is still
canonical (a flipped payload byte, say) fails the second and not the first.

## Add a capture

1. Add a builder to `tools/generate_packets.py` and register it in `CAPTURES`.
   Keep it closed-form: no RNG, no wall clock, no external data, no bytes copied
   out of a real session with someone's avatar in them.
2. Add an entry to `manifest.json` with its `file` and — the part no tool can
   derive — *which phenomenon it pins*. A capture that duplicates an existing
   one's coverage is not worth its review cost.
3. Run `python adapters/liveCapture/vmc/tools/generate_packets.py`. It writes the
   capture and fills in every measured field of the manifest entry (datagrams,
   payload bytes, duration, address patterns, digest); `--check` then holds both
   to what it produced.
4. Run `ctest -R vrmAdapterVmc_` and check the new capture appears in the corpus
   test's output.
