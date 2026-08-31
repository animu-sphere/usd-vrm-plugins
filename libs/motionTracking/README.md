# motionTracking

`motionTracking` answers two questions in the order they have to be asked:
**which tracker is on which body region**, and **what that means for a
skeleton**. It never answers the first by way of the second, which is the whole
reason it is one library and not a header in `motionCore`.

A tracker source carries numbered observations that are pre-IK, and a tracker
index is not a body role — it is an index into whatever the wearer strapped on.
Three decisions sit between that index and a bone, and collapsing any two of
them is how one protocol's semantics leak into the motion layer
([the OSC track §5.1](../../docs/roadmap/osc-and-vrchat-trackers.md#51-assignment-is-a-third-thing-and-it-belongs-to-neither-end)):

| Decision | Owner | What it may know |
| --- | --- | --- |
| **Decode** — bytes to an observation | the adapter | addresses, type tags, argument order |
| **Assignment** — tracker to body region | **this library** | tracker identities, a region vocabulary, an operator's statement |
| **Solve** — assigned observations to a pose | **this library** | canonical bones, target-independent. Never an avatar |

## A region is not a bone

`TrackerRegion` reads like a short `HumanBone` and is deliberately not one. The
two places it stops being one are the two rigs people actually wear: a **knee**
tracker sits on a strap between two bones — there is no knee joint, and which of
the two the device observes is the solve's question — and a **chest** tracker
observes a ribcage rather than the joint a solve produces.

[WORKSPACE.md §2](../../docs/architecture/WORKSPACE.md) forbids the alias by
name, and `tests/check_boundaries.py` enforces it against the sources rather
than against the link line, because the realistic failure is the enum arriving
as a *copy* and a copy leaves no link line to fail on.

## Assignment is stated, never detected

The required path is an operator saying which device is where, and it is the
only one:

```text
t1=head t2=leftHand t3=rightHand      # comments run to end of line
```

That is the same rule `motion_bvh_convert` follows with `--profile`: there is no
default and no name heuristic, because a detector written before the contract
settles the contract on whichever rig was recorded first. Automatic assignment
from rest geometry is a later aid **over** this contract — a producer of
`TrackerAssignmentSpec`, never a second way to reach a binding.

## A set it cannot place is three answers

Three-point, six-point and full-body rigs differ in what is observable, not in
what is solvable. An observation can miss a statement in **two** directions — a
tracker the statement does not place is *unplaced*, a stated tracker that did not
arrive is *absent* — and which of those matters is the caller's choice:

| policy | reads | what it is for | refusal |
| --- | --- | --- | --- |
| `Refuse` (default) | unplaced | the statement is wrong for this rig | `UnplacedTracker` |
| `Ignore` | neither | the rig carries more than the solve needs | none |
| `Hold` | both | the rig is not yet the rig that was stated | `Held` |

`Hold` is the only one that reads both directions, and that is what makes it the
policy its row describes: a rig coming up one device at a time is short of a
*stated* tracker rather than carrying an extra one, so a `Hold` watching only the
unplaced side would never fire for the case it exists for.

`Refuse` and `Hold` both refuse, and the enumerator is the difference that
matters to a live caller: `UnplacedTracker` will still be true next frame, so a
caller stops and tells the operator; `Held` may not be, so a caller keeps the
assignment it had and tries again.

Under `Refuse` and `Ignore` an absence is **data** rather than a refusal, and a
partial rig still assigns — exactly as a missing tracker is data on a frame one
layer over. `Hold` is the policy for a caller that wants the waiting done here
instead.

## The solve is direct, and that is a stated stopping point

`SolveTrackerPose` authors what it observed and **infers nothing**. An observed
orientation becomes the local rotation of the bone its region names, composed so
that forward kinematics reproduces it exactly; a joint nobody observed stays at
rest; and an observed **position** is consumed in one place only, the hips,
where the [motion contract](../../docs/design/MOTION_CONTRACT.md)'s root/hips
rule already says what a body translation observed at one place is.

Everything else a rig reports is **reported rather than dropped**: a position
this solve does not consume, a strap it cannot place, a tracker that sent no
rotation. Consuming a hand's position is IK, IK needs limb lengths, and limb
lengths belong to a target rig this layer does not have and must not acquire.

So what comes out stands where the wearer stood, faces where the wearer faced,
and holds its limbs where the target's own rest pose puts them. An IK solve is a
second function over the same values, taking the rest pose this one refuses to
invent and producing the same `TrackerSolve`.

Two regions per limb are refused outright — the knees and the elbows — and that
is this library's own argument read forwards: a strap between two bones is not
either of them, and with no limb lengths a bent knee and a rotated thigh are the
same observation.

## What it does not have

One edge, taken by the solve alone: `motionCore`, because a `HumanoidPose` is
that library's type. Beyond it, nothing — no platform primitive, no socket, no
file format, no address literal, no adapter identity, and no diagnostic code. A
refusal names the **event**, and whoever knows which adapter it is supplies the
code — `motionSource`'s `SourceProfileRefusal` and `osc`'s `OscDecodeError` are
the same shape.

The **assignment half keeps the empty edge set it was given**, and that is a
per-file rule rather than a per-library one: `tests/check_boundaries.py` scans
the region vocabulary and the assignment for `motionCore`, for OpenUSD in any
form and for a bone, and scans the solve for everything except those — with the
alias forbidden in both halves, in either direction, because that is the failure
with no link line to fail on. A file in neither half is an error, so a new one
chooses its rules deliberately or not at all.

It is on the **product** side of
[WORKSPACE.md §5](../../docs/architecture/WORKSPACE.md)'s split, and its first
consumer arrived on 2026-08-31: `vrchat_osc_record --export-trace` takes the
permission §2 grants — `adapters/*/tools/* -> motionTracking` — and turns a
tracker frame into a canonical pose (VRC-6). It is an **adapter's CLI**, so a
library on the product's side of that split currently travels only in an
artifact the product excludes. Its absence from a product artifact therefore
still says which tools exist rather than which side this library is on, and is
not the exclusion the two shared leaves carry.

No adapter *library* names it, and none may: an adapter that resolved an
assignment would have invented a calibration and hidden it inside a decoder,
which is why `adapters/* -> motionTracking` is a refused source token in all
three adapters' boundary checks.
