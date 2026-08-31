# 04 — Three paths, two performances: what each one carries (2026-08-31)

VRC-7 of [the OSC track](../../roadmap/osc-and-vrchat-trackers.md#vrc-7--cross-source-evidence),
and the completion condition
[§11](../../roadmap/osc-and-vrchat-trackers.md#11-the-fourth-observation-of-one-session)
rewrote on 2026-08-30 when the sender turned out to be unable to produce the
session the original one asked for. One labelled sequence set, performed twice,
observed three ways, compared at the canonical layer:

```text
2026-08-15 take ──┬─ mocopi native UDP ─> vrmAdapterMocopi ──┐
                  └─ that app's BVH export -> motionBvh ─────┤
                                                             ├─> compared
2026-08-30 take ─── mocopi VRChat OSC ──> vrmAdapterVrchatOsc┘
```

**The deliverable is [§7](#7-what-each-path-cannot-carry)**, and it is a list
rather than a green tick — §9.6 and §11's stated outcome both. The numbers in
front of it are what make the list trustworthy, and one of them is a defect this
comparison found and the change it sits in fixes
([§5](#5-the-defect-this-comparison-found)).

**No new hardware session.** Every byte read here was recorded on 2026-08-15 and
2026-08-30 and measured before; what is new is that all three paths were driven
to a canonical clip by the tools as they stand today, and read side by side.

## 0. What was compared

| | |
| --- | --- |
| Native + BVH | the 2026-08-15 mocopi session, [`recorded/manifest.json`](../../../adapters/liveCapture/mocopi/tests/corpus/recorded/manifest.json), measured in [report 01](01-2026-08-15-mocopi-cross-source.md) |
| VRChat OSC | the 2026-08-30 mocopi `VRChat (OSC)` session, [session manifest](../../../adapters/liveCapture/vrchatOsc/tests/corpus/recorded/manifests/2026-08-30-mocopi-vrchat-osc.json), inventoried in [report 02](02-2026-08-30-vrchat-osc-address-inventory.md) |
| Sender | `mocopi-app-2.7.2`, six sensors, iPhone — the same application and the same body on both dates |
| Sequences | `neutral-standing` · `head-turn` · `arm-raise-left` · `walk-root-motion` · `session-restart`, performed on both dates from the same written instructions |
| Driven by | `mocopi_record --export-trace`, `vrchat_osc_record --inspect --export-trace --assign`, `motion_bvh_convert`, then `motion_capture` — release binaries at `2694776`, no argument naming a protocol |
| Read by | five scripts beside the captures, listed in [§10](#10-reproducing-it) |

The comparison reads `UsdSkelAnimation`s and, where a question is about what the
adapter delivered rather than what a consumer replays, the
`motion-capture-trace` between them. Nothing in it parses a datagram, a BVH row
or an OSC address.

**The assignment is the operator's and is written here once**:
`1=hips 2=leftFoot 3=rightFoot head=head`. It is not derived and could not be —
a tracker index is not a body role
([§5.1](../../roadmap/osc-and-vrchat-trackers.md#51-assignment-is-a-third-thing-and-it-belongs-to-neither-end))
— and report 03 §1.1's standing heights are what say it is the right one for
this session.

## 1. Two performances, and the exact size of the loss

Report 01 compared two observations of **one** take and its central result was a
median 0.084° per bone. That number is not available here and no tolerance
buys it back: the OSC session is a different twenty seconds of the same person
doing the same labelled thing, because this product's transfer format is
exclusive and it records no BVH while sending OSC
([report 02](02-2026-08-30-vrchat-osc-address-inventory.md) §5).

So every row below is a property of a **path** — which bones it reaches, what it
drops, which way its numbers point, what it does at a restart — and none is a
property of a take. Where a magnitude is quoted across the two dates it is
quoted as an order and not as an agreement.

The native and BVH halves *are* one take, and they are kept in the comparison
for that reason: they are the control. Where those two agree exactly and the
third differs, the difference is the third path's.

## 2. Which bones each path reaches

| | native UDP | BVH export | VRChat OSC |
| --- | --- | --- | --- |
| canonical bones | **22** | **22**, the same 22 | **4** |
| which | the humanoid rig minus the fingers, eyes and jaw | identical | `hips`, `head`, `leftFoot`, `rightFoot` |
| chosen by | the sender's rig | the file's joint list, through a profile | **the operator's assignment** |

The third column is the one that is a different kind of thing. The first two are
determined by what the sender sends; the third is determined by a sentence an
operator typed, and the same capture with `2=leftHand` in it reaches a different
rig. A tracker source's bone set is a **statement**, and that is the whole of
[§5](../../roadmap/osc-and-vrchat-trackers.md#5-a-tracker-source-is-not-a-pose-source)
arriving as a measurement.

**The skeletons differ in shape and not only in size.** The tracker path's clip
carries `hips`, `hips/head`, `hips/leftFoot`, `hips/rightFoot` — a foot hangs
off the hips where the two pose paths put a whole leg chain. So an authored
rotation named `leftFoot` is *not the same quantity* on the two sides, and every
cross-path comparison below is made in skel space, which is also where a
retarget reads.

**Both pose paths agree about an absence.** `leftToes` and `rightToes` move by
exactly 0.000° in both — the rig declares them and the solver never rotates
them, report 01's finding, unchanged. The tracker path has no toes to agree
about.

## 3. The body's travel: report 01's one entry, closed

Report 01's deliverable table had one row that mattered — **4.81 m of hips path
reached the recorded path and nothing at all reached the live one**, because no
layer there composed a `RootMotion` while the root/hips question was open. That
question was answered in v0.7.0 and this is the first cross-source reading since.

Re-running the identical 2026-08-15 capture through today's `mocopi_record`:

```text
mocopi_record: the trace carries 4.81282 m of hips path (0.690738 m net) as root motion
```

The same sentence report 01 quoted, with the verb the other way round. **All
three paths now carry the body's travel.** On `walk-root-motion`, performed on
both dates:

| | native UDP (08-15) | VRChat OSC (08-30) |
| --- | --- | --- |
| hips path | 4.392 m | 2.146 m |
| net displacement | 0.300 m | 1.291 m |
| what the take contains | four steps out **and back** | the forward half only |

The two rows disagree in the way the takes do. The OSC recorder stops at 6 200
datagrams — 20 s of this stream — and the return walk happened after that
(report 03 §1.3), so a net of 1.29 m against a path of 2.15 m is one direction,
and a net of 0.30 m against a path of 4.39 m is out and back. A comparison that
had quoted only net displacement would have called the tracker path the one that
travels four times further.

**Three paths, three origins.** The BVH clip's first root translation is
`(0.0000, 0.9599, 0.0000)` exactly: the export re-bases the body to the origin
and the room coordinate is gone. Both live paths carry the sender's own space —
`(+0.109, +0.952, +0.281)` and `(−0.003, +0.890, −0.144)` for the two standing
takes. A path that starts at the origin cannot answer "where in the room", and
neither can two sessions of a path that starts wherever the calibration put it.

**The root height is not the same height.** The pose paths' root is the rig's
hips *joint*, at 0.952–0.960 m; the tracker path's root is the hips *tracker*,
at 0.890 m. Six centimetres, consistently, and nothing in the pipeline
reconciles them — a retarget reads a delta from the session's own first root, so
the difference costs nothing today and would cost something the moment two
sessions were composed.

## 4. Which way the numbers point

`head-turn` is the take whose note was written before anyone knew what the
numbers would say: *5 s still, head **left**, centre, right, centre*. Read as
head yaw **relative to the hips**, in skel space, so the body's own turning is
out of it:

| path | first excursion past 30° | most positive | most negative |
| --- | --- | --- | --- |
| BVH (08-15) | **+30.31°** at 6.20 s | +76.41° at 7.10 s | −82.90° at 10.42 s |
| native UDP (08-15) | **+31.94°** at 6.26 s | +76.41° at 7.14 s | −82.90° at 10.46 s |
| VRChat OSC (08-30) | **+26.13°** at 9.90 s | +79.25° at 12.30 s | −67.42° at 18.26 s |

Three readings of the same instruction, and the sign is the same on all three:
**a head turned to the operator's left arrives as a positive yaw** in canonical
space. On the tracker path that is VRC-3's reflection through X applied to a
left-handed sender ([report 03](03-2026-08-30-vrchat-osc-tracking-space.md) §1.4)
and on the pose paths it is the profile and the decoder; the two derivations
share no code and agree.

Two things worth reading off the table rather than past it. **The two 08-15 rows
are identical to 0.00°** and 0.04 s apart, which is report 01's alignment lag —
that is the control saying this measurement is measuring what it claims to.
**The 08-30 row's magnitudes differ by 2.8° on the left turn and 15.5° on the
right**, and that is two performances rather than two paths. Nothing here can
separate a sender difference from a person turning their head less far the
second time, and the honest form of the row is the sign plus an order.

## 5. The defect this comparison found

`neutral-standing` is the floor: stand still for the whole take. Reading the
clips, the tracker path was doing something the pose path never does.

```text
osc-neutral-standing   biggest single-frame step, per bone
  hips         1.59 deg     0 frame(s) step more than 5 deg
  head        33.60 deg    20 frame(s) step more than 5 deg
  leftFoot    33.61 deg    22 frame(s)
  rightFoot   33.61 deg    22 frame(s)

udp-neutral-standing
  every one of 22 bones: 0.15–0.18 deg, 0 frame(s) step more than 5 deg
```

Three bones snapping 33.6° on a standing take, together, and the fourth not
moving. In the trace, before anything resamples it, the frames either side of one
of them are unambiguous:

```text
t 29.368038   root pos + root rot + b hips + head + leftFoot + rightFoot
t 29.383819   root pos              (no root rot, no b hips)   head/feet 33.60 deg away
t 29.416858   root pos              (no root rot, no b hips)   head/feet 33.60 deg away
t 29.450252   root pos + root rot + b hips + head + leftFoot + rightFoot   back
```

The hips tracker delivered a position and no rotation in those two frames — 16
of 777 in this take, and `--inspect` had been reporting them all along as
`withoutRotation: hips 16`. **33.60° is the hips' own orientation**: at
`t 29.368038` the hips quaternion has `w = 0.957319`, and `2·acos(0.957319)` is
33.60° to five figures.

**The cause.** `SolveTrackerPose` composes a bone's local rotation as
`inverse(parent chain) · observed world`, and an ancestor it did not author
contributes identity — correct for a bone nobody observes, since nothing ever
authors a spine and a consumer leaves it at rest for the whole session. It was
also being applied to a bone the assignment **did** place that happened to carry
no rotation in one frame, and that case is different: every consumer in this
workspace replays with `missingBones = hold`, so what it has for the hips in
that frame is the value from a frame ago, not identity. The locals were
therefore divided by identity and composed against 33.6°.

It is not a rounding error and it is not confined to the trace: it survived
`motion_capture` into the clip, on 20–22 frames of a 20-second recording, and an
avatar driven by that clip snaps its head and both feet a third of a right angle
and back, sixteen times, while the operator stands still.

**The fix, in this change.** A bone whose *assigned* ancestor could not be
oriented in this frame is not authored either — it is reported in the new
`TrackerSolve::withheldWithParent` and the consumer holds it beside the ancestor
it depends on. A frame with an unknown root orientation is a frame in which the
body holds, which is what such a frame actually says. Carrying the last known
parent forward would be better motion and needs a stateful solve; this one is a
function of one frame by construction, and the trade is 26 ms of held head
against a 33.6° pop.

**There are two ways an ancestor fails to arrive and the rule reads both**, which
the first version of it did not. A statement whose tracker sends a position and
no rotation produces a binding (`withoutRotation`, which is what this take hit);
a statement whose tracker does not arrive in the frame at all produces **no
binding** and lands in `TrackerAssignment::absent`. This session has both — the
`stated but absent` line reads `hips 1, leftFoot 1, rightFoot 1` on the very take
above — and a consumer holds the bone identically under each, so a rule that read
only the bindings would have left the whole class of dropped trackers composing
against identity. Same snap, neighbouring door; found in review of this change
and closed in it.

Re-exported and re-measured on the same bytes:

| | before | after |
| --- | --- | --- |
| `osc-neutral-standing` worst single-frame step | **33.60°** | **2.46°** |
| frames stepping more than 5° | 20–22 per bone | **0** |
| what the report prints | `withoutRotation: hips 16` | the same, plus `withheldWithParent: head 15, leftFoot 16, rightFoot 16` |

Across the five takes the hips carried no rotation in 16, 6, 5, 5 and 19 frames,
so no take escaped it. **A test asserted the defect**, which is the part worth
recording: the export suite required the rotation-less frame to carry three
bones, and `TestAPositionOnlyTrackerCannotOrientAJoint` required the head to be
`placed`. Both now assert the opposite and say why in the file, because the next
reader's question is which of the two behaviours was chosen on purpose.

**Only a cross-source reading could have raised it.** Every existing check of
this path is internal to it — the solve reproduces the orientations it was
given, the corpus replays to the counts the generator implies — and all of them
pass against the defect, because within one frame the composition is exactly
self-consistent. What fails is the comparison to a path that never has an absent
parent, on the one take where the truth is known: nothing moved.

## 6. Rate, delivery, and the settled floor

| | native UDP | BVH export | VRChat OSC |
| --- | --- | --- | --- |
| delivered rate | 59.945–60.000 Hz | 59.9988 Hz (`frameTime 0.016667`) | 32.998–39.047 Hz |
| emitted rate | ~60 Hz | — | ~58 Hz (report 02) |
| median delivery interval | 16.67 ms | — | 17.2–18.0 ms |
| worst delivery interval | 16.67–33.33 ms | — | 67–169 ms |

The tracker path delivers about two thirds of what its sender emits, which
report 02 measured at the datagram and this confirms at the frame: ~39 Hz of
solved frames from a ~58 Hz sender, over a wire whose worst gap in a
twenty-second take is ten times the median. The native path's worst gap in the
standing take is its median. A file has neither number and reporting one for it
would be inventing it.

**The floors agree once the takes are settled.** Every VRChat OSC take opens
with one to two seconds of settling — several degrees, walked and then stopped —
which a naive "worst deviation from frame 0" reads as motion for the whole take.
The native takes do not do this. Dropping the first 3 s of `neutral-standing`:

```text
                worst    median   biggest step   (degrees, over ~650 / ~875 frames)
VRChat OSC   head   5.78    3.56       0.21
             hips   3.22    2.59       0.17
             feet   2.4-3.0 1.9-2.3    0.21-0.25
native UDP   head   2.04    1.27       0.16
             hips   2.71    1.49       0.16
             feet   0.7-0.9 0.2-0.5    0.15-0.16
```

Same order of magnitude, the tracker path roughly twice as loose, and — after
§5 — as smooth frame to frame. Two facts that only exist beside each other: the
opening transient is a property of this wire, and the steady state is not.

**Which makes the arm raise the sharpest row in the report.** `arm-raise-left`
was performed on both dates with the same instruction, and the paths were not
observing the same event:

| | native UDP | VRChat OSC |
| --- | --- | --- |
| `leftUpperArm` | **76.97°** from frame 0 | *no such bone* |
| `leftHand` | 57.06° | *no such bone* |
| `rightUpperArm` | 15.73° | *no such bone* |
| the four assigned bones, settled | — | 3.2–8.7°, against a standing floor of 2.4–5.8° |

On the pose path the label is legible in the numbers: the left arm moves five
times as far as the right, which is what settled handedness for that path in the
first place. On the tracker path a left arm raised to horizontal and held is
**indistinguishable from standing still**, and it must be — nothing observes an
arm, and estimating one is IK.

## 7. What each path cannot carry

The deliverable. Every row is a measurement from the sessions above.

| What | Kept by | Lost by | Measured |
| --- | --- | --- | --- |
| **The limbs nobody wears a tracker on** | native, BVH | **VRChat OSC** | 22 bones against 4. A labelled left-arm raise reads as 3.2–8.7° on the four assigned bones, against a 2.4–5.8° standing floor ([§6](#6-rate-delivery-and-the-settled-floor)). |
| **The body's travel** | *all three* | — | 4.81 m of hips path on the native path, which report 01 measured being dropped and v0.7.0 closed; 2.15 m on the tracker path's forward half ([§3](#3-the-bodys-travel-report-01s-one-entry-closed)). |
| **The room** | native, VRChat OSC | **BVH** | the BVH clip's first root translation is `(0, 0.9599, 0)` exactly; the two live paths carry the sender's own coordinates. |
| **Which body the root height is of** | *neither kind* | both | the pose paths' root is the hips joint at 0.952–0.960 m, the tracker path's is the hips tracker at 0.890 m. Nothing reconciles them and a retarget's delta hides it. |
| **Transport facts** | native, VRChat OSC | **BVH** | arrival times, loss, restarts, a source that went quiet. Latency is a live-path measurement only. |
| **The frames the sender emitted** | native | **VRChat OSC** | ~39 Hz delivered from ~58 Hz emitted, worst gap 169 ms against a 17 ms median. |
| **A restart** | native | **VRChat OSC (from a file)** | the native capture holds two sessions and the tool refuses one trace for them (`--source-session 1..2`); the OSC capture holds one 4.85 s gap and no identity change, so a file cannot tell it from a silence ([§8](#8-how-each-path-represents-a-restart)). |
| **The device identity** | capture | **trace** | unchanged from report 01: `mocopi-packet-capture` and `vrchat-osc-packet-capture` both have a `device` header key; `motion-capture-trace` has three provenance keys and none is that. |
| **Tracking state** | *neither kind* | all three | no per-joint confidence or state field on either wire, and this product's UX does not produce the state either (report 01 §5). |
| **Which tracker is which body part** | *nothing* | all | on the tracker path it is an operator's sentence, and the paths that do not need one cannot supply it. |

## 8. How each path represents a restart

`session-restart` was performed on both dates and the two paths answer
differently in a way that is about the wire and not the tool.

**Native.** The sender's stream clock returns to zero, so one capture holds two
sessions whose clocks overlap. `mocopi_record` refuses to flatten them into one
trace and names the two: `--source-session 1..2`, 1281 and 476 frames. The
device was dark for 233 frames — 3.8833 s — and `VRM_MOCOPI_SOURCE_RESTARTED`
was raised from the clock branch (report 01 §5).

**VRChat OSC.** This wire carries three floats and no timestamp at all, so there
is no sender clock to reset. What changes is the sender's ephemeral source port,
and the live report of that take named two peers where the capture header names
one — the format that recorded it in August could not carry a per-record peer.
So the file replays as one 779-frame session with a 4.85 s hole in it, which
`motion_capture` reports as `peakLagSeconds 4.82268`, and nothing in it says the
identity changed.

Both refusals exist and their justifications are not transferable, which
[VRC-6](../../roadmap/osc-and-vrchat-trackers.md#vrc-6--cli-and-record) already
recorded from the other side: the native path refuses because two clocks
overlap, and the tracker path refuses because two calibrations are not one
space. **A capture recorded today would answer differently** — the format grew a
per-record peer later that same day — and that is worth stating precisely, because it
is the one row in this report that a re-recording changes.

## 9. The classification

§11 asked for every difference to be classified rather than absorbed. What the
comparison produced, against the list it predicted:

| category | found | example |
| --- | --- | --- |
| native only | yes | the eighteen bones no tracker observes; the sender's own frame rate |
| VRChat OSC only | no | nothing this path reaches that the native one does not |
| BVH only | no | — (report 01's one instance, body travel, closed in v0.7.0) |
| relay only | **unobserved** | no VMC relay session exists; the fourth arm of the diagram is still open |
| conversion difference | no | the head-turn sign agrees on all three paths, independently derived |
| **solve difference** | **yes** | [§5](#5-the-defect-this-comparison-found), and it was a defect |
| timing difference | yes | ~39 Hz against ~60 Hz, and a 169 ms worst gap against 33 ms |
| unknown | one | the 15.5° disagreement on the right-hand head turn, which two performances cannot separate from a sender difference |

**The category §11 predicted would be the useful one was.** A tracker path is
the only one where the pose is solved rather than transported, so a difference
there is attributable to the solve in a way no difference between the native and
BVH paths ever was — and the single largest difference in this whole comparison
turned out to be exactly that, on the one take where the truth was known.

## 10. Reproducing it

The bytes live outside the repository and cannot come in
([the corpus policy](../../roadmap/current.md#standing-corpus-policy--recorded-evidence-is-not-the-generated-corpus)).
Given them:

```powershell
# the tracker path, per take
vrchat_osc_record --inspect <capture>.vrchatoscpackets --export-trace osc-<take>.trace `
    --assign "1=hips 2=leftFoot 3=rightFoot head=head"
motion_capture --trace osc-<take>.trace --output osc-<take>.usda

# the native path, per take
mocopi_record --inspect <take>.mocopipackets --export-trace udp-<take>.trace
motion_capture --trace udp-<take>.trace --output udp-<take>.usda

# the recorded path, once
motion_bvh_convert cross-source.bvh --profile mocopi-mobile-bvh-default-v1 `
    --profile-dir profiles/motion --output bvh-cross-source.usda
```

and then five scripts, which sit beside the captures rather than in the
repository — they are one afternoon's measurement of eleven clips, not a
component: `carry_drop.py` (bones, rates, translation, rotation travel per
clip), `common_bones.py` (the four common bones in skel space, and head yaw
relative to hips), `trace_floor.py` (the same questions asked of the trace,
before anything resamples it), `settled_floor.py` (the floor with the opening
transient dropped) and `step.py` (the frames either side of a single-frame
step, which is what §5 was found with).

## 11. What is still open

- **The relay arm.** No VMC relay session has been recorded, so §11's diagram is
  three of four and the VMC half of the root/hips decision stays open. It is
  [on the operator-evidence list](../../roadmap/current.md#carried-out-of-v070--evidence-an-operator-produces)
  and this report does not shorten it.
- **The right-hand head turn's 15.5°.** Two performances cannot separate a
  sender difference from a person. A shared take would have settled it and this
  product cannot produce one.
- **The Euler order is still three-of-six** ([report 03](03-2026-08-30-vrchat-osc-tracking-space.md) §2.3),
  and its residual — median 0.21°, 12.33° at worst — is inside every tracker-path
  magnitude quoted here. One labelled *rolled* take closes it.
- **The restart row is dated.** §8's answer is a property of a capture written
  before the format carried a per-record peer, and a re-recording changes it.
- **Nothing here runs in CI**, by design: the hardware lane is opt-in and never a
  required gate. What CI gained from this comparison is §5's fix and the two
  tests that now assert it.
