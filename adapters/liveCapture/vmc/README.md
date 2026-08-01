# vrmAdapterVmc

The VMC Protocol input adapter: OSC-over-UDP datagrams from any sender
application, in; canonical humanoid motion, out.

```text
UDP datagram → OSC decode → VMC message decode → frame assembly
             → VRM bone mapping → HumanoidPose → LiveCaptureSource
```

**Status: VMC message decoding, no humanoid.** The build, the manifest, the
boundary check, the frozen diagnostic codes, the recorded-packet format and its
corpus, the OSC layer, and the VMC message layer exist. A datagram now becomes
bone poses, a root transform, blend-shape values and the sender's clock — but
nothing knows that "LeftUpperArm" is a `motion::HumanBone`, which way the
sender's axes point, or where a frame begins. See
[the plan](../../../docs/roadmap/adapters-mocopi-vmc-ardy.md) §5 for the
implementation order and Milestone A for what "done" means.

## What this is, structurally

A plain static CMake library with an `openstrata.library.yaml`, exactly like
`motionRuntime` and `vrmRetarget` — **not** a plugin bundle. It registers
nothing with OpenUSD and ships no `plugInfo.json`, because
[WORKSPACE.md §2](../../../docs/architecture/WORKSPACE.md) keeps it away from
`vrmSchema`, from every file-format bundle, and from OpenExec. It has exactly
two dependencies, and they are the two its manifest declares:

```text
vrmAdapterVmc -> motionCore, motionRuntime
```

`tests/check_boundaries.py` is what makes that a fact rather than an intention.
It fails on a plugin manifest anywhere in the tree, on a stage/registration/exec
API in `include/` or `src/`, on a mention of a sibling adapter or a plugin
bundle, on a `target_link_libraries` naming anything but the two permitted
libraries, and on a binary whose imports leave the OpenUSD value-type layer.

That last one inspects the **test executable**, not the adapter's own archive.
A static `.lib`/`.a` records no imports at all — `dumpbin /dependents` on one
prints a section summary and nothing else — so a check pointed at the library
would be a gate that cannot fail. Pointed at the linked executable it has real
teeth: run against `motion_retarget` it reports `usd_sdf`, `usd_usd`, and
`usd_usdSkel`, which is exactly the class of import this boundary exists to
refuse.

## What it is not allowed to do

Target-skeleton discovery, retargeting, rest-pose correction, its own
interpolation, its own smoothing, `UsdSkelAnimation` authoring, stage authoring,
or a dependency on a sibling adapter. Every one of those already exists once in
this repository; a second copy inside an adapter is a forked pipeline that stays
invisible until two inputs disagree about the same avatar.

The adapter maps a VMC bone name to a `motion::HumanBone`, and stops. It never
resolves a joint index in a target skeleton — that is `vrmRetarget`'s job, one
layer down the pipeline and behind a `VrmHumanoidAPI` mapping.

One permission is easy to misread in the other direction: this adapter's future
**CLI**, under `tools/`, *may* drive `vrmRetarget` and author a stage, exactly as
`motion_retarget` does. The library may not. That is why the boundary check
scans `include/` and `src/` only.

## Transport arrives last

The implementation order is deliberately backwards from the tempting one:
recorded-packet decoder → semantic mapping → frame assembly → live-source bridge
→ thin UDP receiver. Building the receiver first would make every subsequent
test require a live sender; building it last keeps the whole adapter verifiable
in CI from committed fixtures, with no hardware and no socket.

## Recorded input

`vmc-packet-capture` v1 — spec on
[`PacketCapture.h`](include/vrmAdapterVmc/PacketCapture.h) — is what makes that
order possible: the datagrams a session delivered, verbatim, with the instant
each arrived. Line-oriented text, so a fixture diffs; hex with an ASCII gutter,
so an address pattern is legible without a decoder ring:

```text
d 0.000000 20
  2f 56 4d 43 2f 45 78 74 2f 54 00 00 2c 66 00 00  |/VMC/Ext/T..,f..|
  3d cc cc cd                                      |=...|
```

It is not a `motion-capture-trace`, and the difference is the adapter's two
ends: a trace records what an adapter *produced*, a capture what it was *given*.
Only the second can represent a truncated datagram, a duplicate delivery, or a
restart mid-frame — which is to say, only the second can test a decoder.

The corpus lives in [`tests/corpus/`](tests/corpus/) and is generated, never
recorded off a commercial sender, because a fixture carrying someone's avatar is
one CI cannot redistribute. Two tests hold it: `vrmAdapterVmc_corpus` re-emits
every committed capture through the C++ writer and compares bytes, and
`vrmAdapterVmc_packetGen` re-runs the generator and compares against that. A
hand-edited fixture that is still canonical fails the second, not the first.

## OSC, and only OSC

[`OscPacket.h`](include/vrmAdapterVmc/OscPacket.h) decodes a datagram into
messages: addresses, type tags, arguments, bundles flattened into wire order. It
does not know that `/VMC/Ext/Bone/Pos` means anything, and that is what makes
both layers testable — OSC has its own malformed-input cases, and a decoder that
mixed the two could only ever be tested end to end.

Three rules are decisions rather than details:

- **A datagram decodes entirely or not at all.** A bundle whose third element is
  malformed yields no messages, not two. A half-decoded frame is worse than a
  refused one: the assembler cannot tell which half it got.
- **Every OSC 1.0 and 1.1 type tag is understood**, including the fourteen VMC
  never sends. Skipping an argument requires knowing its size, so a decoder that
  handled only `i`, `f` and `s` would have to refuse a valid message the moment
  a sender attached a `d` — blaming the sender for the decoder's gap.
- **The only refusal is `VRM_VMC_PACKET_MALFORMED`.** This layer cannot tell an
  unimplemented address from any other one; `/foo/bar` and `/VMC/Ext/Midi/Note`
  both decode cleanly here. `VRM_VMC_UNSUPPORTED_MESSAGE` belongs one layer up,
  where addresses have meanings.

Diagnostics carry the offending address as their subject and a byte offset in
their detail — inside a bundle, an offset into the *datagram*, so it can be
found in a committed capture rather than bisected.

## VMC, and not yet a humanoid

[`VmcMessage.h`](include/vrmAdapterVmc/VmcMessage.h) is the layer where an
address means something. Seven messages decode; everything else is reported and
skipped:

```text
kind          | address              | known form | fields
--------------+----------------------+------------+---------------------
Availability  | /VMC/Ext/OK          | ,i[ii]     | availability
Time          | /VMC/Ext/T           | ,f         | seconds
Model         | /VMC/Ext/VRM         | ,ss        | name (path), title
RootTransform | /VMC/Ext/Root/Pos    | ,sfffffff  | name, transform
BoneTransform | /VMC/Ext/Bone/Pos    | ,sfffffff  | name, transform
BlendValue    | /VMC/Ext/Blend/Val   | ,sf        | name, value
BlendApply    | /VMC/Ext/Blend/Apply | ,          | —
```

A bone name stays plain text and a quaternion stays in the sender's
`(x, y, z, w)` order. Nothing is converted, normalised, or resolved against a
rig — handedness, up axis, units, and the map from "LeftUpperArm" to a
`motion::HumanBone` belong to the next layer, which is the one that knows what
the numbers are for.

Four more decisions, each written down where it is enforced:

- **A message is refused, never a packet.** The opposite of the OSC layer's rule,
  for the opposite reason: the framing is already established here, so one
  malformed `/VMC/Ext/Bone/Pos` costs that bone and not the twenty-one that came
  with it. A frame missing one bone is the assembler's ordinary business.
- **An unimplemented address is not a defect.** Every sender emits a headset
  transform, a camera, a MIDI note. `VRM_VMC_UNSUPPORTED_MESSAGE` is info and
  recoverable, `DecodeVmcPacket` still returns true, and the mixed-traffic
  capture is in the corpus to hold the two codes apart — ten of its ninety-three
  messages take that path.
- **A known address with the wrong arguments is malformed.** OSC puts an `f` and
  a `d` in the same field, so a decoder reading values without checking tags
  would accept `,sddddddd` as a bone pose and pin nothing about the wire format.
  The refusal quotes both tag strings.
- **Arguments past the known form are counted, never interpreted.** Longer forms
  are in the wild: a third string on `/VMC/Ext/VRM`, further status integers on
  `/VMC/Ext/OK`, more floats after `/VMC/Ext/Root/Pos`'s quaternion. Refusing
  those blames a sender for being newer; decoding them would invent a meaning for
  bytes no fixture here pins — which is why they are described by shape and not
  by what they are believed to mean. Each moves into the table above when a
  capture of it exists.

`vrmAdapterVmc_vmcCorpus` runs both layers over the recorded bytes — 122 / 117 /
83 / 173 decoded messages and ten ignored — and checks two things counts cannot:
the neutral capture's rotations are all identity with its root at the origin, and
the sender-restart capture's backwards clock decodes without complaint, because
`VRM_VMC_TIMESTAMP_REGRESSION` needs a memory of the previous frame and this
layer has none.

## Diagnostics

Eight codes, frozen in `include/vrmAdapterVmc/Diagnostics.h` before the first
decoder exists so that the set describes the protocol rather than whichever bug
was chased last:

```text
VRM_VMC_PACKET_MALFORMED        VRM_VMC_UNSUPPORTED_MESSAGE
VRM_VMC_TIMESTAMP_REGRESSION    VRM_VMC_DUPLICATE_BONE
VRM_VMC_INCOMPLETE_FRAME        VRM_VMC_SOURCE_RESTARTED
VRM_VMC_SOCKET_BIND_FAILED      VRM_VMC_STALE_JOINT
```

`VRM_MOTION_*` is the canonical layer's namespace, not this one's: a reader can
tell a decode failure from a motion-contract violation without knowing which
adapter produced it. Exactly one code is non-recoverable — a receiver that never
bound has nothing to recover into.

## Building and testing

Composed with the rest of the workspace:

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=<usd-install>
cmake --build build --config Release
ctest --test-dir build -R vrmAdapterVmc
```

Or through the runtime `ost` resolves for the workspace:

```sh
ost build && ost test
```

Standalone — this directory is its own CMake project, resolving `motionCore` and
`motionRuntime` as installed packages rather than in-tree targets:

```sh
cmake -S adapters/liveCapture/vmc -B build/vmc \
      -DCMAKE_PREFIX_PATH="<usd-install>;<workspace-prefix>"
cmake --build build/vmc
```

`ost plugin build` is not the standalone route here: it takes a *bundle*
directory and refuses anything without an `openstrata.plugin.yaml`, which an
adapter does not have and must not grow.
