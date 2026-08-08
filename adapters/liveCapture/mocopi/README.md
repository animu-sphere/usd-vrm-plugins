# vrmAdapterMocopi

The native live input adapter for one capture product: that product's own UDP
packets from the device's application, in; canonical humanoid motion, out. No
third-party sender application anywhere in the path.

```text
UDP datagram → packet decode → joint mapping → coordinate conversion
             → frame assembly → HumanoidPose → LiveCaptureSource
```

**Status: scaffold.** What exists is the library's identity and its two edges,
the frozen diagnostic set, and the recorded-packet format that makes every layer
above it testable without a socket. What does not exist is any decoder — no
packet syntax, no joint map, no basis change, no receiver — and the reason is in
[the next section](#the-format-is-not-documented-and-that-shapes-the-order).
See [the plan](../../../docs/roadmap/adapters-mocopi-vmc-ardy.md) §6 and
Milestone D for the implementation order.

## The format is not documented, and that shapes the order

The transport is stated publicly and the payload is not. What the vendor
documents is that the application sends motion data over **UDP** to a PC on
**port 12351** by default, that the address must be **IPv4** (`localhost` and
IPv6 are unsupported), and that the data is **not encrypted** because the
product assumes a local network.

There is no prose specification of the packet structure anywhere in that
documentation. The authoritative artifacts are the vendor's own **Apache-2.0**
sources — a serialization library and a set of receiver plugins — and the
`BVH Sender` application, which transmits a BVH file over the same UDP format.

Two consequences, and they are the whole reason this adapter's first commit
carries no decoder:

- **A decoder written from a remembered format is a guess wearing the shape of
  progress.** That is the failure mode the recorded-motion plan was built around
  ([BVH-0](../../../docs/roadmap/recorded-motion-sources.md#9-milestones)), and
  it applies harder here: a BVH file that is misread produces a visibly wrong
  figure, where a packet field read at the wrong offset produces plausible
  numbers.
- **`BVH Sender` is the evidence path, and it needs no device.** It encodes a
  BVH file into this product's UDP form, so pointing it at a `.bvh` this
  repository *wrote* — the generated format-shape fixtures under
  [`libs/motionBvh/tests/corpus/generated/`](../../../libs/motionBvh/tests/corpus/generated/)
  — yields a capture whose encoding is the vendor's and whose content is ours.
  That is a fixture this project may commit and CI may run, which is not true of
  a session recorded off a phone with somebody's motion in it.

So the corpus arrives before the decoder, the capture format below arrives
before the corpus, and this commit is the format.

## What this is, structurally

A plain static CMake library with an `openstrata.library.yaml`, exactly like
`motionRuntime` and `vrmRetarget` — **not** a plugin bundle. It registers
nothing with OpenUSD and ships no `plugInfo.json`, because
[WORKSPACE.md §2](../../../docs/architecture/WORKSPACE.md) keeps it away from
`vrmSchema`, from every file-format bundle, and from OpenExec. It has exactly
two dependencies, and they are the two its manifest declares:

```text
vrmAdapterMocopi -> motionCore, motionRuntime
```

`tests/check_boundaries.py` is what makes that a fact rather than an intention.
It fails on a plugin manifest anywhere in the tree, on a stage/registration/exec
API in `include/` or `src/`, on a mention of the sibling adapter or a plugin
bundle, on a `target_link_libraries` naming anything but the two permitted
libraries, and on a binary whose imports leave the OpenUSD value-type layer.

It is also the *only* enforcement: `ost` 0.21.0 does not discover a library
under `adapters/`, so the workspace graph gate validates none of these edges and
still reports "valid"
([report 34](../../../docs/reports/ost/34-2026-07-29-v0.21.0-adapter-library-discovery-gap.md)).

## The sibling adapter is not a dependency, and never becomes one

The other live adapter decodes a generic protocol that this same product can be
relayed through, which makes reaching across it the *convenient* mistake rather
than an implausible one. Adapter plan §2.1 forbids it, and the reason is not
tidiness: a native decoder that borrowed a relay's decoder would inherit the
relay's assumptions about framing, clocks and bone names, and the entire point
of building this path is to measure what those assumptions cost
([§6](../../../docs/roadmap/adapters-mocopi-vmc-ardy.md)). The two boundary
scripts refuse each other's names, so the pair is symmetric.

The two paths do meet once, deliberately: the **same physical session** observed
live over UDP and exported to a file can be compared at the canonical layer,
which is the release's distinguishing check and belongs to nothing lower than a
test that holds both.

## What it is not allowed to do

Target-skeleton discovery, retargeting, rest-pose correction, its own
interpolation, its own smoothing, `UsdSkelAnimation` authoring, stage authoring,
or a dependency on a sibling adapter. Every one of those already exists once in
this repository; a second copy inside an adapter is a forked pipeline that stays
invisible until two inputs disagree about the same avatar.

Two more are specific to a *native* adapter, because this is exactly where the
temptation appears. It does not grow buffering, interpolation or filtering — the
bridge into the runtime is a bridge, and one that acquires those has become a
second motion runtime. And it does not widen the canonical contract:
product-specific metadata is isolated as provenance, never as a new value type,
so its output meets the **same** contract as the sibling's rather than a
superset of it.

## Transport arrives last

The implementation order is deliberately backwards from the tempting one:
recorded-packet decoder → semantic mapping → frame assembly → live-source bridge
→ thin UDP receiver. Building the receiver first would make every subsequent
test require a live device; building it last keeps the whole adapter verifiable
in CI from committed fixtures, with no hardware and no socket.

## Recorded input

`mocopi-packet-capture` v1 — spec on
[`PacketCapture.h`](include/vrmAdapterMocopi/PacketCapture.h) — is what makes
that order possible: the datagrams a session delivered, verbatim, with the
instant each arrived. Line-oriented text, so a fixture diffs; hex with an ASCII
gutter, so a binary protocol's field tags are legible without a decoder ring:

```text
!mocopi-packet-capture 1
sender example.synthetic
device example.synthetic
sourceId neutral-standing-01
listen 0.0.0.0:12351

d 0.000000 16
  6e 6f 74 2d 61 2d 70 61 63 6b 65 74 00 01 80 ff  |not-a-packet....|
```

It is not a `motion-capture-trace`, and the difference is the adapter's two
ends: a trace records what an adapter *produced*, a capture what it was *given*.
Only the second can represent a truncated datagram, a duplicate delivery, or a
restart mid-frame — which is to say, only the second can test a decoder.

**It is a second format rather than the sibling's, and that is a decision.** The
header file argues it in full; the short version is that reaching the sibling's
header is a forbidden edge, `motionRuntime` is the wrong home for a transport
artifact, and two magic lines mean a capture of one protocol handed to the other
protocol's decoder fails at line 1 with a clear message instead of at the first
field with a malformed-packet diagnostic blamed on a source that never sent it.
This repeats a shape rather than sharing code, which is the same answer the
semantic clip writer already gave here; what would change it is a *third*
recorder, at which point the shape is a library and its home is a boundary
question argued in its own change.

`device` is the one header key the sibling format does not carry. The native
path's claim is that it keeps device and sensor state a relay drops, and a
capture that cannot say which device produced it cannot support that claim
later. Like `sender`, it is provenance only — nothing in the decode path may
branch on either.

## Diagnostics

Nine codes, frozen in `include/vrmAdapterMocopi/Diagnostics.h` — and frozen on
2026-08-03, two days before this directory existed, so that the set describes
the protocol's failure modes rather than whichever bug was chased first:

```text
VRM_MOCOPI_SOCKET_BIND_FAILED   VRM_MOCOPI_TRACKING_LOST
VRM_MOCOPI_DEVICE_UNAVAILABLE   VRM_MOCOPI_TIMESTAMP_INVALID
VRM_MOCOPI_UNSUPPORTED_JOINT    VRM_MOCOPI_SOURCE_RESTARTED
VRM_MOCOPI_PACKET_MALFORMED     VRM_MOCOPI_FRAME_INCOMPLETE
VRM_MOCOPI_NON_FINITE_TRANSFORM
```

`VRM_MOTION_*` is the canonical layer's namespace and `VRM_VMC_*` is the other
adapter's; neither is this one's. Exactly one code is non-recoverable — a
receiver that never bound has nothing to recover into. Two that look fatal and
are not: a device that is **not there yet** is the ordinary state of a receiver
bound before the operator started the application, and **tracking loss** is the
device reporting on itself accurately, which is a phenomenon a session recovers
from rather than a defect.

## Building and testing

Composed with the rest of the workspace:

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=<usd-install>
cmake --build build --config Release
ctest --test-dir build -R vrmAdapterMocopi
```

Or through the runtime `ost` resolves for the workspace:

```sh
ost build && ost test
```

Standalone — this directory is its own CMake project, resolving `motionCore` and
`motionRuntime` as installed packages rather than in-tree targets:

```sh
cmake -S adapters/liveCapture/mocopi -B build/mocopi \
      -DCMAKE_PREFIX_PATH="<usd-install>;<workspace-prefix>"
cmake --build build/mocopi
```

`ost plugin build` is not the standalone route here: it takes a *bundle*
directory and refuses anything without an `openstrata.plugin.yaml`, which an
adapter does not have and must not grow.
