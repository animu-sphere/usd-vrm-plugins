# vrmAdapterVmc

The VMC Protocol input adapter: OSC-over-UDP datagrams from any sender
application, in; canonical humanoid motion, out.

```text
UDP datagram → OSC decode → VMC message decode → frame assembly
             → VRM bone mapping → HumanoidPose → LiveCaptureSource
```

**Status: a motion source, and still no socket.** The whole decode path exists —
the recorded-packet format and its corpus, the OSC layer, the VMC message layer,
the skeleton map, the frame assembler, and the bridge into `motionRuntime`. A
recorded capture replays into a `motion::LiveCaptureSource` that samples like any
clip, so everything except transport is verifiable in CI from committed bytes.
What is left is the receiver, the record tool, and the evidence only a real
sender can give. See
[the plan](../../../docs/roadmap/adapters-mocopi-vmc-ardy.md) §5 for the
implementation order and Milestone B for what remains.

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

`vrmAdapterVmc_vmcCorpus` runs both layers over all seven recorded captures —
568 decoded messages, twelve ignored, eight refused and nine arguments counted
but not read — and checks three things counts cannot:

- the neutral capture's rotations are all identity with its root at the origin,
  and its sender clock starts at 12.5 s where the receive clock starts at 0;
- the sender-restart capture's backwards clock decodes without complaint,
  because `VRM_VMC_TIMESTAMP_REGRESSION` needs a memory of the previous frame
  and this layer has none;
- the malformed-forms capture's bad bone costs **that bone** — the datagram
  carrying it still yields the twenty-two messages that arrived with it, which
  is the first rule above stated as a number.

## VMC's names and VMC's axes, into a humanoid

[`SkeletonMap.h`](include/vrmAdapterVmc/SkeletonMap.h) is the one conversion the
adapter exists to perform, and the first layer that knows a `motion::HumanBone`
exists. It converts and it does not decide: frame boundaries, missing bones and
sender restarts belong to the assembler, and resolving a target joint belongs to
`vrmRetarget` two layers on.

**The vocabulary is Unity's `HumanBodyBones`, not VRM 1.0's.** A sender is a
Unity application and writes PascalCase where VRM 1.0 writes lowerCamel. The two
have the same 55 bones — but for the thumb they disagree about more than case,
because VRM 1.0 renamed the chain one joint down:

```text
Unity / VRM 0.x            VRM 1.0 / motion::HumanBone
LeftThumbProximal      ->  leftThumbMetacarpal
LeftThumbIntermediate  ->  leftThumbProximal
LeftThumbDistal        ->  leftThumbDistal
```

A map that lowercased the first letter would land every thumb rotation one joint
out while every other bone in the hand arrived correctly, so the table is
written out rather than derived and the tests state the thumb twice. The
spelling is matched exactly: an unrecognised name is
`VRM_VMC_UNSUPPORTED_MESSAGE` — info, recoverable, that bone ignored and the
frame kept — for the same reason an unimplemented address is.

**The basis change is VRM 1.0's, not VRM 0.x's.** Unity is left-handed with the
avatar facing +Z; the canonical contract is glTF's right-handed, Y-up, metre
basis. VRM 1.0 defines that conversion as a reflection through X where VRM 0.x
used Z, and this adapter follows VRM 1.0 because the vocabulary it converts into
is VRM 1.0's:

```text
position   (x, y, z)     ->  (-x, y, z)
rotation   (x, y, z, w)  ->  (w, (x, -y, -z))
```

The two sign flips on the imaginary part are one from the axis and one from the
reversed sense of rotation, which is why a rotation about +X survives unchanged
and one about +Z comes out about −Z. Quaternions are normalised on the way
through — senders emit un-normalised ones and a retarget composing them would
skew a joint. A zero-length or non-finite one is refused as
`VRM_VMC_PACKET_MALFORMED` instead: it names no orientation, and the value that
would have to be invented to carry on is exactly the identity a reader could not
tell from a real sample.

Two things this layer deliberately does not answer. **What a VMC bone rotation
means relative to rest** — it is the sender's local rotation, which equals the
rotation away from rest only when the sender's humanoid rest is identity; a
sender where it is not needs `vrmRetarget`'s `SourceRestPose`, and manufacturing
one from the first frame seen would be a guess. And **where a bone's position
goes** — the canonical pose carries rotations plus one `RootMotion`, so a bone's
local offset is the sender's rig geometry rather than motion. It is converted
and handed on unread, because whether the hips offset composes with
`/VMC/Ext/Root/Pos` needs the frame the assembler owns.

`vrmAdapterVmc_skeletonMapCorpus` maps every transform in all seven captures —
493 bones and 24 roots, none unsupported and none refused, 232 of them reflected
off the X axis — and checks two claims counts cannot: a neutral pose is still a
neutral pose after the basis change, and the arm-raise capture's five rotations
about Unity's −Z come out about the canonical +Z at 0°, 15°, 30°, 45° and 60°.
That capture raises a *left* arm, which Unity puts at −X and glTF at +X, so the
sign flips with the basis and both describe the same arm going up — a conversion
that dropped the flip would lower it.

## Where a frame begins

[`FrameAssembler.h`](include/vrmAdapterVmc/FrameAssembler.h) is the first layer
that *decides* rather than converts, and the decision the protocol forces is
where a frame begins — VMC promises nothing about one datagram being one frame.
The corpus already holds two sender shapes that disagree about it:

```text
bundled     | T(12.500) root Hips … UpperChest | T(12.533) root Hips …
unbundled   | root Hips … RightToes T(20.000)  | root Hips … T(20.033)
```

In one the clock *opens* the frame and in the other it *closes* it, so either
convention read as a rule produces one frame per two on the other sender — off
by half a frame, with every rotation in it still individually correct. Two rules
cover both: **a second clock ends the frame**, and **a repeat ends it unless it
arrived in the same datagram**, where the same repetition is
`VRM_VMC_DUPLICATE_BONE` instead. That exception is the only place a datagram
boundary is load-bearing anywhere in the adapter, and it is why the assembler
consumes packets rather than a flattened message stream.

A backwards clock means three things, told apart by one comparison against the
last accepted frame: **equal or slightly earlier** is
`VRM_VMC_TIMESTAMP_REGRESSION` and the frame is refused, which is what stops a
duplicated datagram from becoming a duplicated pose; **earlier by more than the
restart threshold** is `VRM_VMC_SOURCE_RESTARTED`; anything later is accepted. A
restart is reported and not repaired — offsetting the stream to keep timestamps
rising would manufacture continuity out of a discontinuity.

The assembler **holds nothing forward**: a bone the session has observed and this
frame did not carry is reported missing and the frame is still emitted, because
`MissingBonePolicy` is the intake's answer. A bone missing past the staleness
horizon is additionally `VRM_VMC_STALE_JOINT`, raised once per crossing rather
than per frame. Both are measured against the rig the session has actually
observed — a sender that solves no fingers is complete, not incomplete forty
times a second.

`vrmAdapterVmc_frameAssemblerCorpus` assembles all seven captures and makes the
claim this layer exists for: **both sender shapes yield five frames at the same
30 Hz cadence**, with the unbundled one's arm rising 15° per frame in the order
it was sent.

## Into the runtime

[`LiveSource.h`](include/vrmAdapterVmc/LiveSource.h) is the last layer that is
still this adapter's, and the thinnest. It hands assembled frames to
`motion::LiveCaptureSource` and answers `IMotionSource` by forwarding, so a
consumer holds one object and samples poses off it like any clip:

```cpp
VmcLiveSource source;
source.PushDatagram(bytes, size, receiveTime, &diagnostics);
if (source.ConsumeSessionRestart()) {
    source.GetIntake().AlignClock(now);
}
motion::PoseSampleResult pose = source.Sample(now);
```

Buffering, interpolation, smoothing, confidence gating, missing-bone resolution
and root-motion intake all exist exactly once, in `motionRuntime`, and this class
contributes none of them: the assembler reports a gap, `MissingBonePolicy`
decides what a gap means, and the tests run the same input under both policies to
show the answer changing with the runtime's configuration rather than with the
adapter.

**The one decision it takes is what a sender restart costs.** The assembler emits
the new session's clock verbatim, which for the intake is a frame arriving behind
the newest it holds — refused, forever. So the policy is explicit: `Reset` drops
the intake's history and admits the new session (the default, because the
alternative is a stream that dies the first time an operator restarts their
sender application), and `Refuse` hands the frame on and lets the session visibly
stop. The third option — offsetting the new timestamps to splice the two
sessions — is not offered anywhere in this adapter. A restart also invalidates
the clock offset, which only the consumer can re-align, so it is latched and
handed back rather than repaired.

**What a pose cannot carry stays readable.** The hips offset, the `missing` and
`stale` sets, and the session flag reach a `HumanoidPose` nowhere at all, so
`GetFramesFromLastPush()` is a window on exactly what was just delivered — valid
until the next push. Whether the hips offset is body translation or rig geometry
is a question only a real sender's session answers, and a recording tool
gathering that evidence should not have to drive the assembler separately.

Two smaller properties are worth knowing. **Provenance applies from when the
sender sent it**: `/VMC/Ext/VRM` may arrive mid-session, and poses buffered
before it keep the bare `vmc` provenance rather than retroactively learning a
title the session did not know yet. And **the datagram's lifetime stops here** —
every string view a decoded packet holds has become a value before the push
returns, so a receiver may hand this API the buffer it is about to overwrite,
which is the hazard [`VmcMessage.h`](include/vrmAdapterVmc/VmcMessage.h)
describes and no overload can refuse.

**Nothing here is thread-safe, and neither is what it wraps.** `motionRuntime`
contains no mutex or atomic: `PoseBuffer` holds a deque and
`LiveCaptureSource::Sample` writes its statistics as it answers, so a push and a
sample may not run concurrently and neither may two samples. Motion policy §11.4
puts a network thread on one side of a *thread-safe* pose buffer, which does not
exist yet, and that is the first question the UDP receiver has to answer — a
queue hand-off it owns, or synchronisation inside `motionRuntime`. This class
takes no private lock meanwhile, because a mutex here would leave every
`GetIntake()` caller racing on the same buffer and look like a fix.

`vrmAdapterVmc_liveSourceCorpus` replays all seven captures from bytes and makes
the cross-layer claim: **every frame the assembler emitted was admitted by the
intake**, because the assembler emits strictly advancing frames within a session
and that is exactly the ordering `LiveCaptureSource::Push` requires. The
sender-restart capture is then replayed twice more to show that what a restart
costs is a policy and not an accident — six frames under `Reset`, four under
`Refuse`, on the same bytes.

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
