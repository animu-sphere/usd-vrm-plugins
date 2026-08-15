# 01 — One mocopi session, observed two ways (2026-08-15)

The cross-source comparison of
[the adapter plan §9.6](../../roadmap/adapters-mocopi-vmc-ardy.md#96-cross-source-comparison),
which is v0.7.0's distinguishing check. One physical session was observed as
**mocopi UDP** and as **that application's own BVH export**, over the same
window, and the two were compared at the canonical layer.

**It is not a green test and was never going to be.** §9.6's stated outcome is a
list of what each path cannot carry, written down once, from evidence. That list
is [below](#4-what-each-path-cannot-carry). The agreement numbers are what make
the list trustworthy: two paths that disagreed about the motion could not be
asked what they drop.

No VMC relay was recorded, so the third arm of §9.6's diagram is still open.

## 0. What was recorded

| | |
| --- | --- |
| Date | 2026-08-15, one operator recording their own motion |
| Sender | mocopi app 2.7.2, six sensors, iPhone |
| Transport | IPv4 LAN, UDP 12351 |
| Sequence | 5 s still · head turn left then right · **left** arm raised and lowered · four steps forward · return |
| Recorded by | `mocopi_record` 0.6.0 at `43a5fe2`, measured at `3a9fe0a` |

The application was recording internally for the whole UDP window, so the BVH
export covers the session and some either side of it. **The bytes are not in this
repository and cannot be**: a session is a real person's motion and a skeleton
packet is a body measurement of that person. What survives is
[`recorded/manifest.json`](../../../adapters/liveCapture/mocopi/tests/corpus/recorded/manifest.json)
— hashes and every measured field — and this report.

Four further sessions were recorded the same evening and are measured in that
manifest. Three of them are used below, because a comparison needs a floor and a
scale.

## 1. The two chains

```text
                 ┌─ UDP ─→ mocopi_record ─→ capture ─→ --export-trace ─→ motion_capture ─┐
one recorded ────┤                                                                       ├─→ compared
session          └─ BVH ─→ (the application's own export) ─→ motion_bvh_convert ─────────┘
```

Both end at a `UsdSkelAnimation` carrying canonical humanoid bones, and the
comparison reads only that. Nothing in the analysis parses a packet or a BVH row
— which is the point of comparing at the canonical layer rather than at a
decoder's.

Both reach **22 canonical bones, the same 22**. Neither carries a bone the other
lacks. The BVH converter reports `27 read, 22 bound, 5 ignored`, `26 joint(s)
restating rest geometry`, and `composed: spine, chest, upperChest, head`; the
live path reaches the same set through `MocopiSkeletonMap` and the same path
rule.

| | UDP | BVH |
| --- | --- | --- |
| samples | 2190 | 2820 |
| declared rate | 59.9453 Hz (measured from the sender's own clock) | 59.9988 Hz (`frameTime 0.016667`) |
| span | 36.5166 s | 46.9843 s |
| diagnostics on decode | none | none |

## 2. Aligning them was the hard part, and it is a result

The first attempt aligned on "how far the left upper arm is from this path's own
first sample" and produced an rms of 61.674° — a misalignment, not a
disagreement. Two reasons, both worth keeping:

- **Neither recording's first frame is a pose the other shares.** The UDP
  capture's frame 0 is the operator's hand on the phone starting the recording:
  `rightLowerArm` sits ~65° away from its frame-0 value for the entire rest of
  the take. Any analysis that uses frame 0 as a reference measures that.
- **The two clocks are not the same clock.** Aligning in 300-frame windows shows
  the lag sliding by **3 frames over 1800, about 1667 ppm**. The rates above
  differ by ~890 ppm in the same direction.

The alignment that works is baseline-free: total rotation between consecutive
samples, summed over the shared bones, correlated between the paths. That gives
an integer lag per window, and a straight line through those lags removes the
slip.

**One application, one session, two outputs, two time bases.** That is a fact
about the producer and not about either of our paths, and it is the first thing
the comparison had to discover in order to be able to say anything else.

## 3. The rotations agree

With the drift-corrected alignment, sample for sample, over 2190 samples and 22
bones:

| | degrees |
| --- | --- |
| median of the per-bone medians | **0.084** |
| worst per-bone median | 0.129 |
| worst single sample | 16.967 |

**The residual is timing, not value**, and §9.6 requires that to be shown rather
than asserted — a difference outside tolerance must name what is responsible
instead of widening the tolerance. Splitting the 48136 bone-samples by how fast
that bone was turning:

| population | count | median difference | max |
| --- | --- | --- | --- |
| nearly still (≤ 0.05 °/frame) | 19759 | **0.0000** | 2.208 |
| turning (> 1 °/frame) | 1826 | 1.191 | 16.967 |

and the twenty worst samples in the entire comparison are **all in frames
1–13**, each implying the same lag of 3.7–4.4 frames — a single residual offset
at the head of the take, made visible by fast motion.

A quantity that agrees to 0.0000° whenever the body is still, and disagrees only
in proportion to how fast a bone is moving, is being sampled at slightly
different instants. It is not being decoded differently.

**The scale that makes 0.084° meaningful.** The `neutral-standing` session
measures this device against itself: standing as still as a person can for
17.58 s, the median per-frame change is **0.0183°** and the largest excursion
from the first pose is **3.68°**. So the two paths agree with each other far
more closely than the device agrees with itself over the same seconds.

## 4. What each path cannot carry

The deliverable. Each row is a measurement from this session block, not a
prediction.

| What | Kept by | Lost by | Measured |
| --- | --- | --- | --- |
| **The body's travel** | BVH | **UDP → canonical** | 4.81 m of hips path (0.69 m net) in this session. The BVH clip carries it as a hips translation with a 1.17 m maximum excursion; the UDP clip authors **no translations at all**. |
| **The device identity** | capture | **trace** | `mocopi-packet-capture` has a `device` header key; `motion-capture-trace` has three provenance keys and none of them is that. |
| **A second peer** | live report | **capture** | The `session-restart` session arrived from two source ports; the capture header names one, so the file replays as a one-peer session. The sibling format has this identically. |
| **Transport facts** | UDP | **BVH** | Arrival times, datagram loss, restarts, a source that went quiet. A file has none of these, and latency is a live-path measurement only — reporting one for a file would be inventing it. |
| **Tracking state** | *neither* | both | The measured grammar carries no per-joint confidence or state field, and the product's UX does not produce the state either (§5 below). |

**The travel row is the one that matters, and it is a policy rather than a
defect.** The device sends the hips joint's absolute position in every frame —
the only translating joint on this rig — and while
[§5.2](../../roadmap/adapters-mocopi-vmc-ardy.md) is open no layer on the live
path will call it root motion, so it reaches no `HumanoidPose` and no trace.
Retargeted onto an avatar, this session walks on the spot: the legs step, the
body turns, and nothing travels.

Until 2026-08-15 nothing in this repository could put a number on that. It can
now, because the export measures it on the way past and prints it:

```text
mocopi_record: the trace carries no root motion, so 4.81282 m of hips path
               (0.690738 m net) stays in the capture
```

Path **and** net, because this session makes them disagree: a walk out and back
displaces 0.69 m and travels 4.81 m, and the second number alone would describe
it as nearly stationary. The `walk-root-motion` session is starker still —
4.39 m of path against 0.30 m of net.

## 5. What the same session block settled elsewhere

- **Handedness, from labelled motion.** It was closed on 2026-08-12 by comparing
  rest offsets against this application's BVH export, which is an argument about
  a calibration. Now it is closed from movement, with the side stated before the
  take: in the dedicated session `leftUpperArm` moves 77.26° against
  `rightUpperArm`'s 15.06°, and in this one 108.75° against 29.77° — and the
  right arm's peak falls in a different part of the session. This is the
  labelled asymmetric evidence the five sessions of 2026-08-11 could not supply,
  because all four of their motions were bilateral.
- **What a restart costs, measured.** A real stop-and-start produced
  `VRM_MOCOPI_SOURCE_RESTARTED` from the clock branch, and the new session was
  dark for **233 frames = 3.8833 s** until the device repeated its rest table.
  The refusal count and the new session's first emitted timestamp agree exactly,
  which cross-checks the assembler's accounting against the device. The
  roadmap's "about 3.5 s" is a little optimistic and should read 3.9 s.
- **`VRM_MOCOPI_DEVICE_UNAVAILABLE` was raised by a device** for the first time,
  during that gap, rather than by a unit test.
- **Tracking loss was dropped as a take, and the reason is a finding.** Removing
  a sensor puts the product into re-tracking, so "a stream carrying a lost
  sensor" is a state this application's UX does not produce. That is a second
  reason `VRM_MOCOPI_TRACKING_LOST` stays frozen and unraised, beside having no
  field in the measured grammar to decode into.
- **The toes never move.** Both paths agree about `leftToes` and `rightToes` to
  exactly zero, and the standing session shows why: the rig declares them and
  the solver never rotates them. The two paths agreed about an absence.

## 6. What is still open

- **No VMC relay was recorded**, so §9.6's third path is unobserved. The
  comparison above is two of three.
- **The root/hips decision itself.** This report supplies the cost — 4.81 m in
  36 seconds — and does not make the choice, which is
  [§5.2](../../roadmap/adapters-mocopi-vmc-ardy.md)'s to record.
- **The corpus still cannot be surprised by the protocol.** Every committed
  capture was generated from the measured grammar. A `BVH Sender` capture would
  be the fixture that can refute it, and none exists yet.
- **Nothing here runs in CI**, by design: the hardware lane is opt-in and is
  never a required gate. What CI gained from this session is the manifest and
  this report, not a test.

## 7. Reproducing it

The bytes live outside the repository, on the operator's machine. Given them:

```powershell
# the live half
mocopi_record --inspect cross-source.mocopipackets --export-trace cross-source.trace
motion_capture --trace cross-source.trace --output cross-source-udp.usda

# the recorded half
motion_bvh_convert cross-source.bvh --profile mocopi-mobile-bvh-default-v1 `
    --profile-dir profiles/motion --output cross-source-bvh.usda
```

and then the comparison, which reads only those two clips. The analysis scripts
sit beside the captures rather than in the repository — they are one evening's
measurement of two files, not a component — and what they computed is in §2–§4
above with the populations each number came from.
