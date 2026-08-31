// SPDX-License-Identifier: Apache-2.0
//
// Assigned observations to a canonical pose — the third of
// [§5.1](../../../../docs/roadmap/osc-and-vrchat-trackers.md#51-assignment-is-a-third-thing-and-it-belongs-to-neither-end)'s
// decisions, and the only one that may name a bone.
//
// Decode is the adapter's, assignment is the operator's (TrackerAssignment.h),
// and this is where a body role finally becomes a joint. It is the one file in
// this library that names `motion::HumanBone`, and that is the whole shape of
// the boundary check beside it: the region vocabulary and the assignment are
// scanned for a bone exactly as they were, because a `TrackerRegion` that
// resolved to a `HumanBone` would make assignment a lookup and leave this file
// nothing to do ([WORKSPACE.md §2](../../../../docs/architecture/WORKSPACE.md)).
//
// ## This solve is direct, and that is a stated stopping point
//
// It authors what it observed and **infers nothing**. A tracker rig observes a
// handful of places on a body; an IK solve turns those into the joints between
// them, and needs limb lengths — which belong to a target rig, which this layer
// does not have and must not acquire. So:
//
// * an observed **orientation** becomes the local rotation of the bone its
//   region names, composed so that forward kinematics reproduces it exactly;
// * an observed **position** is consumed in exactly one place, the hips, on the
//   rule the root/hips record already states
//   ([MOTION_CONTRACT.md](../../../../docs/design/MOTION_CONTRACT.md)); every
//   other one is reported unused rather than dropped silently;
// * a joint nobody observed stays at **rest** — identity, unset in
//   `validRotations` — rather than being estimated.
//
// What comes out is a pose that stands where the wearer stood, faces where the
// wearer faced, and holds its limbs where the rig's own rest pose puts them. It
// is honest rather than complete, and the release says so: this is tracker
// *input* reaching the canonical layer, not tracker-driven full-body motion.
//
// **An IK solve is a second function over the same values**, taking a rest pose
// as the input this one refuses to invent, and producing this same
// `TrackerSolve`. Nothing here has to change shape for it to arrive, which is
// why the contract is written as a value and a refusal rather than as a class
// with a policy on it.
//
// ## The composition, and the invariant a test can read
//
// A `HumanoidPose` carries rotations **local to the semantic humanoid parent**,
// and a tracker reports an orientation in the world. So for each placed bone
//
//     local = inverse(world rotation of its parent chain) * observed world
//
// where the parent chain is composed from the rotations this solve has already
// authored, and every bone in it **nobody was told to observe** contributes
// identity. Bones are placed parent-first, which costs nothing to arrange:
// every bone's parent has a smaller enumerator than it does (`motionCore`'s own
// table says so), so sorting by enumerator is a valid order.
//
// The property that follows is the one worth testing, and it holds for any
// combination of regions: **composing the authored locals from the root down to
// a placed bone returns the orientation that bone's tracker reported.** A chest
// tracker changes what the head's local rotation *is* and not what the head's
// world orientation *is*, which is exactly what a consumer that adds a chest
// strap mid-session should see.
//
// ## An assigned ancestor that falls silent is not an unobserved one
//
// The identity above is a statement about a bone **the assignment never
// named**: nothing observes a spine, no frame ever authors one, and a consumer
// replaying the stream leaves it at rest for the whole session. Identity is
// what it is, at every instant, and composing against it is exact.
//
// A bone the assignment *did* name is a different case the moment one frame
// arrives without its rotation. A stream is replayed with a missing-bone policy
// — `hold` for every consumer in this workspace — so the value a consumer has
// for that bone in that frame is **the one it carried a frame ago**, not
// identity. Localising its children against identity there produces locals
// which, composed against what the consumer actually holds, put every one of
// them a whole parent rotation away from the orientation its own tracker
// reported.
//
// It is not a small error and it is not hypothetical: a real 20 s standing
// session dropped the hips rotation on 16 of 777 frames and the head and both
// feet snapped **33.6°** — the hips' own orientation, exactly — on every one of
// them, in the trace and again in the clip replayed from it
// ([report 04](../../../../docs/reports/motion/04-2026-08-31-cross-source-carry-drop.md)
// §5).
//
// So a bone whose **bound** ancestor carried no rotation this frame is not
// authored either. It goes to `withheldWithParent`, and the consumer holds it
// beside the ancestor it depends on: the body holds as a unit for that frame,
// which is what a frame with an unknown root orientation actually says. The
// alternative — carrying the last known parent forward — is a stateful solve,
// and this one is a function of one frame by construction ([§10 of the OSC
// track](../../../../docs/roadmap/osc-and-vrchat-trackers.md)).
//
// ## Which regions this solve places, and the two it refuses
//
// | region | bone | why |
// | --- | --- | --- |
// | `Hips` | `Hips` | and the root, below |
// | `Chest` | `Chest` | the strap observes the ribcage; `Chest` is the joint under it |
// | `Head` | `Head` | |
// | `LeftHand` / `RightHand` | `LeftHand` / `RightHand` | |
// | `LeftFoot` / `RightFoot` | `LeftFoot` / `RightFoot` | |
// | `LeftKnee` / `RightKnee` | **none** | a knee strap sits between two bones |
// | `LeftElbow` / `RightElbow` | **none** | an elbow strap sits between two bones |
//
// **The four refusals are this library's own argument read forwards.**
// `TrackerRegion` exists because a knee is not a joint — `LeftUpperLeg` and
// `LeftLowerLeg` meet there and which one the strap observes is a question
// about the solve. This solve's answer is that it does not know: with no limb
// lengths it cannot tell a bent knee from a rotated thigh, and picking one
// would be an IK assumption spelled as a table. They are reported in
// `unsolved`, which is data rather than a refusal — a six-point rig plus knees
// still produces a pose, and the caller is told which two straps did not reach
// it.
//
// A caller that wants the table without a solve can ask for it: see
// `TrackerRegionBone`, which is this solve's answer and not the region's
// identity.
//
// ## The root, on the rule that already exists
//
// A hips tracker is a body translation observed at one place, which is the case
// the root/hips record answers: its position is `RootMotion::worldPosition`,
// its rotation is `RootMotion::worldOrientation`, and that same rotation
// remains the `HumanBone::Hips` local rotation, because a rig rooted at its
// hips has a root path of one joint. Authoring a second convention here would
// make two observations of one session incomparable field for field, which is
// the cost that record was written to stop paying.
//
// `TrackerSolveConfig::authorRootMotion` turns the *position* half off for a
// session whose hips position is not trusted. The rotation is authored either
// way: a body that turned turned whatever the translation is worth.
#pragma once

#include "motionTracking/TrackerAssignment.h"
#include "motionTracking/TrackerObservation.h"
#include "motionTracking/TrackerRegion.h"
#include "motionTracking/api.h"

#include "motionCore/Humanoid.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace motionTracking
{

// The bone this solve places `region` on, or nullopt for a region it refuses.
//
// **This is a function and not a mapping on the type**, and the difference is
// the one the library is built around: it answers "where does *this* solve put
// a chest strap", not "which bone is the chest region", and a second solve with
// limb lengths may answer differently for the knees without changing a single
// region's identity. It is public because a CLI reporting what an assignment
// will reach needs it before it has a frame, on the same argument that makes
// `motion_bvh_inspect` able to print a profile's bones without converting a
// file.
MOTIONTRACKING_API std::optional<motion::HumanBone> TrackerRegionBone(
    TrackerRegion region) noexcept;

struct TrackerSolveConfig
{
    // Whether an observed hips **position** becomes `RootMotion::worldPosition`.
    // The hips rotation is authored regardless; see the header.
    //
    // On by default, because a tracker source that observes a hips position and
    // drops it produces a session that walks on the spot — which is exactly
    // what the root/hips record measured a live path costing before it was
    // written down.
    bool authorRootMotion = true;
};

// Why a solve produced no pose. As with the assignment layer, these are not the
// contract a user reads — an adapter's diagnostic codes are, and this library
// holds none.
enum class TrackerSolveRefusal : std::uint8_t
{
    None,
    // The assignment handed in did not place: `Refuse`, `Hold`, an invalid
    // spec, or an observation the assignment layer rejected. `detail` carries
    // that layer's refusal name and its own detail, because a caller reading
    // only the solve's enumerator would otherwise lose which of five things
    // happened one layer down.
    AssignmentRefused,
    // The assignment cannot be applied to *this* observation array: a binding
    // indexes past its end, or two bindings name one region. Neither is
    // producible by `AssignTrackers` on the array its identities came from, so
    // this refusal means the two calls were given different arrays — which
    // would otherwise bind a region to a device nobody wore.
    AssignmentUnusable,
    // A value that is not one: a non-finite component, or a rotation that
    // cannot be normalised. Refused rather than sanitised — a pose carrying a
    // NaN propagates into every consumer downstream, and a zero quaternion has
    // no orientation to fall back to.
    ObservationInvalid,
    // Nothing was authored: no placed region carried a rotation, and no root
    // motion was authored either. An empty pose is not a solve, on the same
    // rule that makes an empty binding set not an assignment.
    NothingSolved,

    Count,
};

inline constexpr std::size_t TrackerSolveRefusalCount =
    static_cast<std::size_t>(TrackerSolveRefusal::Count);

MOTIONTRACKING_API std::string_view TrackerSolveRefusalName(
    TrackerSolveRefusal refusal) noexcept;

// What solving an assignment against an observation produced.
//
// **Every vector is filled from the point the bindings can be read at all**,
// which is the assignment layer's rule with the one qualification this layer
// needs. There it is unconditional, because an assignment always has an
// observation to report against; here the first two refusals are about the
// bindings *themselves* — an assignment that refused, or one applied to an
// array it was not made from — and under those there is nothing to classify.
// They come back with five empty vectors rather than a half reading, and the
// suite asserts that rather than leaving it to be discovered. From
// `ObservationInvalid` onward the rule is the layer below's exactly: the caller
// that most needs this struct is the one reporting on a solve that did not
// succeed.
struct TrackerSolve
{
    // Defaults to a refusal. A `TrackerSolve` nobody solved has concluded
    // nothing, and `pose` is default-constructed rather than absent, so the
    // enumerator is the only thing that says whether reading it is meaningful.
    TrackerSolveRefusal refusal = TrackerSolveRefusal::AssignmentRefused;

    // Plain text naming what was refused. Empty when nothing was.
    std::string detail;

    // Sparse by construction: `validRotations` carries what was placed and
    // nothing else, exactly as a clip that omits a bone does. No tracker
    // identity and no per-bone provenance reaches it — a consumer that cannot
    // tell this pose from a clip-driven one is reading it correctly, and
    // `HumanoidPose::source` is where a producer says what it was.
    motion::HumanoidPose pose;

    // Regions placed onto a bone, in the assignment's binding order.
    std::vector<TrackerRegion> placed;
    // Regions bound to an observation this solve does not place onto any bone:
    // the knees and the elbows. Data, not a refusal.
    std::vector<TrackerRegion> unsolved;
    // Regions this solve would place whose observation carried no rotation.
    // A position-only tracker cannot orient a joint, and the alternative —
    // authoring identity — is bit-for-bit a tracker reporting rest.
    std::vector<TrackerRegion> withoutRotation;
    // Regions whose own observation carried a rotation, withheld because a
    // bone the assignment placed above them carried none in this frame. Their
    // world orientation is known and their **local** rotation is not, because
    // the parent a consumer will hold is not the identity this solve would
    // otherwise divide by; see the header. A caller reporting on a session
    // wants this apart from `withoutRotation`: one says a tracker sent no
    // orientation, the other says a tracker's orientation could not be
    // expressed relative to what a consumer will be holding.
    std::vector<TrackerRegion> withheldWithParent;
    // Every bound region whose observation carried a position this solve did
    // not consume: every region except the hips, and the hips too when
    // `authorRootMotion` is off. Consuming one is IK.
    //
    // **Bound, not placed** — a knee that carried a position appears here as
    // well as in `unsolved`, and the overlap is deliberate. The two vectors
    // answer different questions: `unsolved` says a strap reached no bone, and
    // this says a number nothing read. A caller reporting to an operator on
    // what a rig sent and what a pose used wants both, and a list that
    // silently omitted the positions of the four straps this solve refuses
    // would be answering the first question twice.
    std::vector<TrackerRegion> positionsUnused;

    bool Solved() const noexcept
    {
        return refusal == TrackerSolveRefusal::None;
    }
};

// Solve `assignment` against the observations it was made from.
//
// `observed` must be the array whose identities the assignment was built on —
// `TrackerIdentities` is one line and exists for that — because a binding holds
// an index into it. `timestamp` is the pose's, in seconds, and comes from the
// caller: a frame's clock belongs to the layer that assembled the frame, and an
// observation carries none (TrackerObservation.h).
//
// **The first refusal wins and the order is part of the contract**, as it is
// one layer down: assignment · applicability · observation values · nothing
// solved. It runs outermost-first, because an assignment that refused says
// nothing about an observation and an assignment applied to the wrong array is
// not addressed by any check below it.
MOTIONTRACKING_API TrackerSolve SolveTrackerPose(
    const TrackerAssignment& assignment,
    const std::vector<TrackerObservation>& observed, double timestamp,
    const TrackerSolveConfig& config = {});

} // namespace motionTracking
