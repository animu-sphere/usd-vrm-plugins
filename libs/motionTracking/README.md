# motionTracking

`motionTracking` answers one question: **which tracker is on which body
region**. Not what it observed, and not what that means for a joint.

A tracker source carries numbered observations that are pre-IK, and a tracker
index is not a body role — it is an index into whatever the wearer strapped on.
Three decisions sit between that index and a bone, and collapsing any two of
them is how one protocol's semantics leak into the motion layer
([the OSC track §5.1](../../docs/roadmap/osc-and-vrchat-trackers.md#51-assignment-is-a-third-thing-and-it-belongs-to-neither-end)):

| Decision | Owner | What it may know |
| --- | --- | --- |
| **Decode** — bytes to an observation | the adapter | addresses, type tags, argument order |
| **Assignment** — tracker to body region | **this library** | tracker identities, a region vocabulary, an operator's statement |
| **Solve** — assigned observations to a pose | the motion layer | canonical bones, target-independent |

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

## What it does not have

An empty edge set, and the edge it most looks like it should have is the one
that would end it: no `motionCore`, no OpenUSD, no platform primitive, no
socket, no file format, no address literal, no adapter identity, and no
diagnostic code. A refusal names the **event**, and whoever knows which adapter
it is supplies the code — `motionSource`'s `SourceProfileRefusal` and `osc`'s
`OscDecodeError` are the same shape.

It is on the **product** side of
[WORKSPACE.md §5](../../docs/architecture/WORKSPACE.md)'s split, and as of
2026-08-31 it has **no consumer at all**: the root `CMakeLists.txt` configures it
and `tests/consumer/motionTracking/` measures its package, and nothing links it.
The permission §2 grants is `adapters/*/tools/* -> motionTracking`, and the CLI
that will take it is VRC-6's. So it appears in no artifact — which says only that
it has no consumer, and is not the exclusion the two shared leaves carry.
