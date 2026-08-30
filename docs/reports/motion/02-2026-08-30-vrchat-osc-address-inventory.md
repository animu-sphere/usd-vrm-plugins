# 02 — What mocopi's `VRChat (OSC)` output actually sends (2026-08-30)

VRC-1 of [the OSC track](../../roadmap/osc-and-vrchat-trackers.md#vrc-1--real-mocopi-capture-and-address-inventory):
a real `VRChat (OSC)` session, recorded and inventoried **before** a decoder is
written. Its output is a measurement, and that measurement is the input to
VRC-2's design.

**The risk this milestone was written to test is real.** The plan's
[risk A](../../roadmap/osc-and-vrchat-trackers.md#14-risks) said mocopi's
`VRChat (OSC)` output may not be the tracker subset anyone expects. It is not:
this sender uses **three numbered trackers and a named `head`**, out of a surface
that admits eight, and a decoder keyed on an integer index would have silently
dropped the one address carrying the head.

Two findings below were not on anyone's list. This wire's only marker of a
restart is the sender's **source port**, which the capture format does not carry;
and this application **cannot record a BVH while it is sending OSC**, which makes
the four-observation comparison of
[§11](../../roadmap/osc-and-vrchat-trackers.md#11-the-fourth-observation-of-one-session)
unproducible on this product rather than merely unrecorded.

## 0. What was recorded

| | |
| --- | --- |
| Date | 2026-08-30, one operator recording their own motion |
| Sender | mocopi app 2.7.2, six sensors, iPhone, `VRChat (OSC)` transfer format |
| Transport | IPv4 LAN, phone to this host over Wi-Fi, UDP 9000 |
| Takes | probe · neutral-standing · head-turn · **left** arm-raise · walk with root motion · session-restart |
| Recorded by | `vrchat_osc_record` 0.7.0 at `24ecf63` |
| Measured by | `vrchat_osc_record --inspect`, same build; the cadence and ordering figures of §3 by a throwaway script over the same files |

Six captures, 44 918 datagrams, 2 380 792 payload bytes. **The bytes are not in
this repository and cannot be**: tracker positions are a real person's motion in
a real room. What survives is
[the session manifest](../../../adapters/liveCapture/vrchatOsc/tests/corpus/recorded/manifests/2026-08-30-mocopi-vrchat-osc.json)
— hashes and every `--inspect` reading — and this report.

No BVH export accompanies any take, and §5 explains why that is a finding rather
than an omission.

## 1. The inventory

`vrchat_osc_record --inspect` over the 45-second probe, which is the longest
capture and identical in shape to the other five:

```text
addresses: 8 (13918 message(s), 0 bundled datagram(s), 0 refused)
  /tracking/trackers/1/position ,fff  1749 message(s) in 1749 datagram(s)  0.029106-44.967750 s
  /tracking/trackers/1/rotation ,fff  1674 message(s) in 1674 datagram(s)  0.029100-44.967742 s
  /tracking/trackers/2/position ,fff  1750 message(s) in 1750 datagram(s)  0.029117-44.967755 s
  /tracking/trackers/2/rotation ,fff  1748 message(s) in 1748 datagram(s)  0.029112-44.967753 s
  /tracking/trackers/3/position ,fff  1750 message(s) in 1750 datagram(s)  0.029126-44.968121 s
  /tracking/trackers/3/rotation ,fff  1748 message(s) in 1748 datagram(s)  0.029121-44.968113 s
  /tracking/trackers/head/position ,fff  1749 message(s) in 1749 datagram(s)  0.029094-44.966536 s
  /tracking/trackers/head/rotation ,fff  1750 message(s) in 1750 datagram(s)  0.029063-45.034146 s
```

**Eight rows in every capture, and the same eight.** No take added an address —
not the head turn, not the arm raise, not the walk, not the restart. Datagram
lengths are 52 and 56 bytes and nothing else, which is the same statement read
off the envelope: the 56-byte rows are the two whose address string is four
bytes longer.

The eight are the whole surface this sender uses. What that settles:

- **Three numbered trackers, from a surface of eight.** A six-sensor mocopi
  configuration does not populate `4`–`8`.
- **`head` is a name, not a number.** It sits in the same path position as `1`,
  `2` and `3`, so a decoder that parses that segment as an integer drops the head
  and reports nothing wrong. This is the single most consequential row in the
  table and it is the one a decoder written from the specification would have got
  wrong first.
- **Every address is `,fff`** — three 32-bit floats, position and rotation alike.
  A rotation is therefore three numbers rather than four: this sender sends Euler
  angles, and what convention they are in is not a question an inventory can
  answer.
- **No bundles at all.** 0 bundled datagrams in 44 918: one datagram carries
  exactly one message. Every frame-boundary question is therefore VRC-4's and
  cannot be answered by an OSC bundle.
- **Nothing was refused.** The decoder in `libs/osc` read every datagram of a
  real third-party sender, which is the first time it has met one.

## 2. Ordering: eight datagrams in a fixed cycle

The inventory groups by address and type tag, so it cannot see order. Measured
separately, the send order is a fixed cycle:

```text
head/rotation → head/position → 1/rotation → 1/position
              → 2/rotation → 2/position → 3/rotation → 3/position
```

Consecutive datagrams advance exactly one step around that cycle in **99.7 %** of
the 44 912 consecutive pairs across the six captures. The cycle is not a
partition to be assumed — one lost datagram shifts the phase for the rest of the
capture, so it is measured as a step census rather than by cutting the stream
into windows of eight.

**Rotation precedes position for the same tracker**, and the head leads the
cycle. Neither is something a specification promised.

An intact cycle spans a **median 0.053 ms** — all eight datagrams inside 53
microseconds. A frame on this wire is a burst, not a spread, which is worth
having before VRC-4 decides what a frame boundary is.

## 3. Cadence: it aims at 60 Hz and delivers 39

| Take | Datagrams | Span | Frames | Delivered | Nominal |
| --- | --- | --- | --- | --- | --- |
| probe | 13918 | 45.005 s | 1750 | 38.88 Hz | 58.93 Hz |
| neutral-standing | 6200 | 20.053 s | 776 | 38.70 Hz | 58.07 Hz |
| head-turn | 6200 | 19.937 s | 776 | 38.92 Hz | 58.09 Hz |
| arm-raise-left | 6200 | 19.969 s | 776 | 38.86 Hz | 57.12 Hz |
| walk-root-motion | 6200 | 19.873 s | 776 | 39.05 Hz | 55.43 Hz |
| session-restart | 6200 | 23.577 s | 778 | 33.00 Hz | 58.81 Hz |

*Nominal* is the median interval between frames; *delivered* is frames over the
capture's span. **The gap between them is whole frames going missing**, and the
distribution says so rather than an average: intervals are bimodal at ~17 ms and
~33.5 ms — one frame period, or exactly two — with 51–60 % of them in the short
mode. About a third of the frames this sender emits never arrive.

The native wire of the same application measures 59.945–60.000 Hz and does not do
this ([report 01](01-2026-08-15-mocopi-cross-source.md)). Whether the loss is the
sender's, the Wi-Fi's, or this host's is **not attributable from the receiving
end alone**, and this report does not attribute it. One piece of evidence points
away from the receiver: losses take whole cycles as units, where a receive loop
falling behind a 53-microsecond burst would lose its *tail* — tracker 3 — and
that is not the shape of the loss.

`session-restart` delivers 33.00 Hz because 4.85 s of its span are the deliberate
gap; its nominal rate is unremarkable.

### The one address that is not lost at random

131 single-address drops occurred across the six captures. **126 of them — 96 % —
are `/tracking/trackers/1/rotation`**, in every take, at rates from 0.6 % to
4.3 % of that address's own frames. The remaining five are spread one each over
four other addresses.

A network does not prefer one address. This is a property of the sender or of
this address's place in the burst, it is stated here rather than explained, and
it is the concrete case behind `VRM_VRCHAT_OSC_TRACKER_PARTIAL` — a tracker
reporting half of itself is not hypothetical on this wire, it happens about once
a second.

## 4. A restart is a source port, and the file cannot carry it

The `session-restart` take stopped the application's streaming for about ten
seconds mid-recording and started it again. Three things were measured, and the
third was not expected.

- **The live session saw two peers**: `192.168.1.8:51662` and
  `192.168.1.8:50035`. The application takes a new ephemeral source port when
  streaming restarts. Across the six captures every take has a different source
  port, so this is the wire's restart marker.
- **The dark window is 4.8452 s**, from the last datagram of the old session to
  the first of the new. There is no in-band session identifier, no rest table and
  no handshake: the stream simply resumes.
- **`--inspect` on that same capture reports one peer.** The capture format
  carries a single `peer` in its header and no peer per datagram, which
  [`liveTransport/UdpReceiver.h`](../../../libs/liveTransport/include/liveTransport/UdpReceiver.h)
  states as a deliberate asymmetry — *"a capture names one peer in its header,
  where a live socket learns a possibly different one per datagram"*.

That choice was free when it was made and is not free here. **The only signal
this wire gives that a session restarted does not survive into a capture**, so
every fixture-driven test of restart behaviour is testing the silence and not the
identity change. The gap does survive, so a decoder can still see *something
happened*; what it cannot see from a file is the difference between one session
pausing and a second session beginning — which is precisely the distinction
`Reset` versus `Refuse` is made of.

This is a finding, not a fix. Widening the format touches `liveTransport` and
three adapters and every committed fixture in two corpora, and it is
[the OSC track](../../roadmap/osc-and-vrchat-trackers.md)'s to schedule.

## 5. What this sender cannot be asked

Two limits of the *product*, both measured this session, and together they close
a question the roadmap had left open as merely unrecorded.

- **The transfer format is exclusive.** The application sends the native mocopi
  wire or `VRChat (OSC)`, not both, so no single physical take can be observed on
  two live paths.
- **It records no BVH while sending OSC.** Observed on app 2.7.2 during this
  session. In native mode it does, which is exactly why
  [report 01](01-2026-08-15-mocopi-cross-source.md) could compare UDP against a
  BVH export of the *same* take.

Their consequence is a fact about the product, not a scheduling problem:

> **On this sender, the VRChat OSC path cannot share a physical take with any
> other observation** — not with the native wire, and not even with a file export
> that would let two takes be chained through a common recording.

[§11](../../roadmap/osc-and-vrchat-trackers.md#11-the-fourth-observation-of-one-session)
asks for one physical session observed four ways, and
[the corpus](../../../adapters/liveCapture/vrchatOsc/tests/corpus/recorded/README.md)
asks a recorded VRChat OSC session to be *the same physical take* as the native
UDP recording. Neither is producible with this product. Both are amended in the
same change as this report — a comparison of separate takes is a smaller claim,
and stating it is better than carrying a condition no session can meet.

## 6. What VRC-2 inherits

Each of these is a decision the decoder now makes from evidence rather than from
the specification:

1. **The index segment is not an integer.** `head` and `1` occupy the same path
   position. Whatever type the decoder gives a tracker identity has to hold both,
   and the choice is visible in the first line of the first test.
2. **A rotation is three floats.** Not a quaternion, and the Euler convention is
   VRC-3's to establish against a recorded rest pose — never from the
   documentation, on
   [the handedness precedent](../../roadmap/adapters-mocopi-vmc-ardy.md#96-cross-source-comparison).
3. **Position and rotation are separate messages**, so a tracker is partial
   between two datagrams by construction, and — per §3 — partial for a whole
   frame about once a second on one specific address.
4. **The generated corpus has a shape to imitate**: eight addresses, `,fff`,
   52- and 56-byte datagrams, one message per datagram, in the cycle of §2, at
   ~58 Hz with about a third of the frames dropped whole. Every one of those is
   now a measurement rather than a guess.
5. **The addresses `4`–`8` have never been observed here.** A fixture containing
   one is inventing traffic; whether the decoder accepts them anyway is a
   decision about VRChat's surface, not about this sender, and it should be made
   knowingly.

## 7. What this report does not say

It decodes nothing. No value in any datagram was interpreted: not a coordinate,
not a sign, not a unit, not a scale. The tracking space, the Euler convention,
what a tracker index means on a body, and whether body travel reaches this wire
at all are all downstream of a decoder that does not exist yet — the walk take
was recorded so that question has evidence waiting when VRC-2 lands, not because
this report answers it.

It also says nothing about calibration: the application was calibrated before
every take, so `VRM_VRCHAT_OSC_CALIBRATION_REQUIRED` has no recorded behaviour
behind it and stays unraised, on the same terms
`VRM_MOCOPI_TRACKING_LOST` stayed frozen after
[report 01](01-2026-08-15-mocopi-cross-source.md)'s session.
