# 03 — Which way this sender's numbers point (2026-08-30)

VRC-3 of [the OSC track](../../roadmap/osc-and-vrchat-trackers.md#vrc-3--tracking-space-normalisation):
the tracking space a mocopi `VRChat (OSC)` stream is expressed in, **measured
from the session rather than read off VRChat's documentation**. The two agree,
and that agreement is the result.

It is worth being blunt about why a milestone exists for a conversion whose
answer is written on a web page. The sibling adapter's handedness was taken from
documentation once, and the failure that produced is invisible: a mirrored
conversion is correct in every axis-aligned test pose and wrong the moment
anything turns, so it survives a unit suite, a corpus and a review, and shows up
as an avatar whose left hand moves when the operator moves their right
([the adapter plan §9.6](../../roadmap/adapters-mocopi-vmc-ardy.md#96-cross-source-comparison)).
The only thing that catches it is a recording of somebody doing a **labelled**
thing.

**No new hardware session.** This report re-reads the six captures VRC-1
recorded — the numbers below are a second measurement of the same bytes, and
[report 02](02-2026-08-30-vrchat-osc-address-inventory.md) is what those bytes
are.

## 0. What was measured

| | |
| --- | --- |
| Source | the six 2026-08-30 captures, 44 918 datagrams, [session manifest](../../../adapters/liveCapture/vrchatOsc/tests/corpus/recorded/manifests/2026-08-30-mocopi-vrchat-osc.json) |
| Takes read | `neutral-standing` · `head-turn` · `walk-root-motion`, plus all six for the ranges |
| Measured by | a throwaway script over the capture files, as report 02 §3's cadence figures were |
| Landed as | [`TrackingSpace.h`](../../../adapters/liveCapture/vrchatOsc/include/vrmAdapterVrchatOsc/TrackingSpace.h) and its suite |

**None of these numbers is in the session manifest, deliberately.** That file
holds readings taken by a shipped tool and nothing else
([its README](../../../adapters/liveCapture/vrchatOsc/tests/corpus/recorded/manifests/README.md)),
and `vrchat_osc_record --inspect` counts messages rather than reading their
values — so this report is where they live, exactly as report 02 §3's cadence
figures do. What would change that is one feature: an `--inspect` that printed a
per-address value census. It is worth having when the tool is next opened
(VRC-6), because it would make every constant in the suite below reproducible by
a command rather than by a script that no longer exists.

The takes' own notes are the evidence for three of the five findings, and they
were written **before** anyone knew what the numbers would say: "stand still for
the whole take"; "5 s still, head **left**, centre, right, centre, holding 1 s at
each"; "5 s still, 4 steps forward, 4 steps back without turning". A session
without those sentences could not answer the handedness question at all.

## 1. The five readings

| Property | Reading | What says so |
| --- | --- | --- |
| Unit | **metres** | a standing operator's head at 1.5178, hips at 0.8922, both feet at 0.092 |
| Up axis | **+Y** | those three heights separate on the second component and on no other |
| Forward | **+Z** | 1.401 m of walking travels (-0.034, 1.400) in (x, z) at a yaw of +8.9° |
| Handedness | **left-handed**, +X is the body's right | a head turned to the operator's **left** reports a yaw of -77.5° |
| Angles | **degrees**, `[0, 360)` | 44 918 messages span -0.0053 to 359.9942 |

Which is Unity's tracking space, as VRChat documents it. Every row was
measurable, and the fourth is the one that could have come out the other way.

### 1.1 Metres, +Y up, and a floor at zero

`neutral-standing`, the 20–30 s window, in the sender's own space:

```text
tracker              x        y        z
head           +0.0168  +1.5178  -0.0854
1              +0.0046  +0.8922  -0.1284
2              -0.1599  +0.0921  -0.1030
3              +0.0915  +0.0918  -0.2467
```

A person, in metres, with the floor at zero: head, hips, and two feet within
0.3 mm of the same height. No other unit produces this, and no other component
carries the height — which is the whole of the unit and up-axis question.

**A tracker index is still not a body role.** The rows above are labelled with
what the address said and nothing else; that the one at 0.89 m is a hips and the
two at 0.09 m are feet is an inference this adapter does not make and does not
need — assignment is a separate contract with an operator's statement as its
first path
([§5.1](../../roadmap/osc-and-vrchat-trackers.md#51-assignment-is-a-third-thing-and-it-belongs-to-neither-end)).
It is recorded here because a later reader will want to know what this session's
three numbered trackers were, and because VRC-4a's fixtures can be checked
against it.

Over all six captures the position components span:

```text
x  -0.1788 .. +0.4141      y  +0.0568 .. +1.5261      z  -1.1925 .. +0.4172
```

The vertical range is a person standing in a room and never leaving the floor;
the horizontal ranges are the walk.

### 1.2 Degrees, and a wrap the decoder has to survive

Over the same 44 918 messages, every rotation component lies in

```text
x  -0.005327 .. 359.993683
y  -0.004906 .. 359.993835
z  -0.005482 .. 359.994171
```

`[0, 360)` with float error on both sides of the wrap — so a tracker at rest
reports the *same* component as 359.994 and as -0.005 within one take, and a
consumer that treated the sign as meaningful would see a full turn every few
frames. Radians are excluded by the same numbers: a radian reading puts a
standing operator's feet through fifty-seven full turns.

### 1.3 Forward is +Z

`walk-root-motion`, "4 steps forward", hips before and after:

```text
from  (+0.224, +0.897, -1.185)
to    (+0.190, +0.893, +0.216)
travel (-0.034, -0.004, +1.400)   |horizontal| 1.401 m   bearing -1.4 deg
```

with the hips yaw at +8.9° mean (-4.6° to +19.0°) and the head yaw at +7.1°
through the take. A body walking forward while facing forward travels along the
axis it faces, so **+Z is forward** and the 10° between the bearing and the yaw
is a person not walking exactly along their own centre line.

The take was recorded as "4 steps forward, 4 steps back". **Only the forward
half is in the capture**: the recorder stops at 6 200 datagrams, which is 20 s of
this stream, and the return walk happened after that. It costs nothing here —
one direction settles an axis — and it is stated because the manifest says the
take contains both.

### 1.4 Handedness, which needed the label

`head-turn`, at the four holds the take's note describes, head rotation in
degrees (unwrapped to ±180 for reading):

| Hold, as performed | x | y | z |
| --- | --- | --- | --- |
| still | -6.66 | -1.14 | -1.55 |
| head **left** | -11.97 | **-77.48** | +1.89 |
| centre | -12.68 | -1.77 | +0.90 |
| head **right** | -18.36 | **+82.53** | -2.59 |

Turning left reports a **negative** yaw. A yaw of θ carries the facing direction
to `(sin θ, 0, cos θ)` in the sender's own components, so a left turn moves the
face toward **-X**: the body's left is -X, the body's right is +X, and with +Y up
and +Z forward that is a **left-handed** basis.

This is the row the same device could have failed on. Its native wire is
right-handed with +X on the body's **left** — the opposite sign, measured on
2026-08-12 — so one application's two outputs disagree about X, and a decoder
that carried the native reading across would have mirrored every session
silently.

### 1.5 What the head's position channel does not do

Through both 80° turns the head's position moves at most **3.3 cm** from its
still mean of (0.0104, 1.5204, 0.1947). The position on this wire is the head
*joint*, not a point offset from it on the skull — which is a fact VRC-4 needs
and this layer does not: a position that orbited a pivot would let a frame
assembler infer the rotation from two consecutive positions, and this one
cannot.

## 2. The Euler order, measured to three of six

A rotation arrives as three angles, and three angles are not an orientation
until something says in what order they compose. VRChat's documentation says
Unity's; this section is what the session says.

### 2.1 A still take cannot answer it

The obvious experiment is the rest pose: standing still, every tracker's own up
axis should be the world's. Run over `neutral-standing`, the mean deviation from
vertical, per composition `R = R_a · R_b · R_c`:

```text
order        1       2       3    head
XYZ       0.67    2.02    2.30    7.54
XZY       0.67    1.76    2.98    6.68
YXZ       0.67    1.76    2.98    6.68
YZX       0.67    1.76    2.98    6.68
ZXY       0.67    1.76    2.98    6.68
ZYX       0.65    1.45    3.53    5.69
```

Six orders inside 1.9° of each other: the take separates nothing, because a
person standing still rotates about one axis at a time and every composition
agrees there. **This is why the labelled turn is the experiment and the rest
pose is not.**

### 2.2 A labelled turn answers half of it

A head turning left and right does not roll. So take the head's lateral axis and
measure how far it leaves the horizontal plane, at each of the four holds:

| order | still | head left | centre | head right |
| --- | --- | --- | --- | --- |
| `Rx·Ry·Rz` | -1.42 | **+13.39** | +1.27 | **-20.75** |
| `Rx·Rz·Ry` | -1.42 | **+11.81** | +1.27 | **-18.47** |
| `Ry·Rx·Rz` | -1.54 | +1.86 | +0.88 | -2.46 |
| `Ry·Rz·Rx` | -1.55 | +1.89 | +0.90 | -2.59 |
| `Rz·Rx·Ry` | -1.43 | **+11.81** | +1.29 | **-18.47** |
| `Rz·Ry·Rx` | -1.55 | +0.31 | +0.90 | -0.31 |

The three orders that do **not** apply the yaw outermost drag the head's 12–18°
of pitch into 12–21° of apparent roll, in opposite directions at the two ends of
the turn. The three that do hold the head within 2.6° of level throughout.

**The yaw is outermost — applied about the world's vertical, after the other
two.** That is measured, on a body doing a thing it was labelled as doing, and
it is the strongest statement this session supports.

The still and centred columns are in the table for what they show about method:
with the yaw near zero all six agree to within a degree, so the discriminating
power is entirely in the labelled 80°.

### 2.3 The half it cannot answer, and what that costs

Which of X and Z sits inside the yaw is **not** measurable here, and the reason
is a property of the session rather than of the method: across all 44 918
messages, the second-largest component of any orientation is **25.2°**. Nobody
tilted. The three survivors differ only where roll and pitch are both large.

What the residual costs, over the whole session:

```text
six orders        up to 25.69 deg apart   (head, at 341.3 / 85.9 / 357.6)
three survivors   median 0.21, p95 1.75, max 12.33 deg
                  96 % of samples inside 2 deg
```

The worst survivor sample is a head at 31° of pitch and -22° of roll — which
names the take that would close it exactly: **a labelled rolled head or foot,
held**. Twenty seconds of hardware, and this session has none because tilting
was on nobody's list.

The adapter composes `Ry · Rx · Rz`, the survivor Unity documents (Unity spells
the same composition "ZXY", naming its angles in application order). Choosing it
is documentation breaking a tie the measurement narrowed from six to three — not
documentation standing in for a measurement.

## 3. What landed

[`TrackingSpace.h`](../../../adapters/liveCapture/vrchatOsc/include/vrmAdapterVrchatOsc/TrackingSpace.h),
its implementation and
[its suite](../../../adapters/liveCapture/vrchatOsc/tests/test_tracking_space.cpp).
Positions and rotations reach the canonical basis — right-handed, +Y up, +Z
forward, metres — by VRM 1.0's reflection through X, `M v` and
`(w, det(M) · M v)` for `M = diag(-1, 1, 1)`, which is the sibling adapter's
line and the general form the recorded half writes as a `CanonicalBasis`.

The suite asserts the measurements above, **physically**: a direction is rotated
and compared against where a body that did that has to end up. A
component-against-component test agrees with a mirrored conversion in every pose
in this session, so it would have proved nothing.

Six mutations were each made to fail before the suite was believed — the mirror
dropped, the position mirror dropped, VRM 0.x's mirror through Z instead of X,
the yaw moved innermost, the angles read as radians, and the channel guard
removed. All six turn the unit suite red and the restored source is green.

**The corpus pass cannot check any of it**, and the test says so rather than
implying otherwise: it asserts that every message the decoder accepts is
converted, and that the corpus produced at least one non-identity rotation and
one non-zero position, so a conversion returning identity and zero fails it.
Everything else is a claim about a basis, and the generated corpus's numbers are
this repository's own invention. Only a labelled session can verify a basis,
which is the same reason VRC-1 came before VRC-2.

This is the change where **the adapter's binaries first load OpenUSD**. VRC-0
recorded that its closure was empty of it and predicted a decoder would change
that; VRC-2 arrived and it did not, because `libs/osc` links nothing. Producing
a canonical value is what does it: the library takes the `motionCore` edge for
the value types.

**The boundary check did not notice, and that took a second change.** Its
allowlist — `arch`, `boost`, `gf`, `python`, `tf`, `vt` — was meant to stop
being a check that the closure is *empty* the day a canonical value arrived, and
it did not, because a static archive contributes only the objects a binary
references: the exe the gate inspected calls nothing in `TrackingSpace.cpp`, so
the linker drops that object and `dumpbin /dependents` finds no `usd_*` in it at
all. The gate now names the conversion's own suite — the binary linking the
widest layer this adapter touches — and `tests/CMakeLists.txt` states that as a
rule for the next file that reaches a new one. Verified in both directions: with
`gf` removed from the allowlist the new binary fails by name and the old one
passes, which is the defect stated as an experiment.

It is worth recording how this was caught, because none of it was: the first
version of this change rewrote three paragraphs to say the closure had grown
before anybody ran `dumpbin` on the binary the check reads. A check whose
subject is chosen once and never re-examined is a check that measures whatever
its subject happened to contain.

## 4. What this does not say

- **Nothing about which tracker is which body region.** §1.1's heights are
  recorded as heights.
- **Nothing about a frame.** Position and rotation arrive in separate datagrams;
  what makes two messages one observation is VRC-4's window, and
  `VRM_VRCHAT_OSC_TRACKER_PARTIAL` is still raised nowhere.
- **Nothing about calibration.** These positions are in the space the sending
  application established; that a second session's origin is the same as this
  one's is not something six captures on one afternoon can show.
- **Nothing about a pose.** No `HumanoidPose` is produced anywhere in this
  change, and no avatar is named.
- **The Euler order is three-of-six**, quantified in §2.3, and the take that
  would finish it is one sentence long.
