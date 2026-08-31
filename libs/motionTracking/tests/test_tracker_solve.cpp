// SPDX-License-Identifier: Apache-2.0
//
// The solve, checked against the one property it promises and the four it
// refuses.
//
// The assignment suite beside this one is almost free of arithmetic, because
// what an assignment can get wrong is a decision. This half is the opposite:
// the decision is a seven-row table, and what a solve can get wrong is a
// composition — a local rotation authored against the wrong parent chain looks
// entirely plausible in a debugger and puts a head on backwards on an avatar
// nobody here has.
//
// So the load-bearing case is the **invariant**: composing the authored locals
// from the root down to a placed bone returns the orientation that bone's
// tracker reported. It is checked with every rig this vocabulary can present,
// and it is what makes "an unobserved joint stays at rest" a measurement rather
// than a sentence — if an unobserved joint were quietly given a value, or a
// chain were composed in the other order, the world orientation it reproduces
// would stop being the observed one.
//
// The rig identities are made up here exactly as they are next door: `t1`,
// `t2`, `t3` are what a device could be called and what no wire calls one.
#include "motionTracking/TrackerAssignment.h"
#include "motionTracking/TrackerObservation.h"
#include "motionTracking/TrackerRegion.h"
#include "motionTracking/TrackerSolve.h"

#include "motionCore/Compare.h"
#include "motionCore/Humanoid.h"

#include "pxr/base/gf/quatd.h"
#include "pxr/base/gf/rotation.h"
#include "pxr/base/gf/vec3d.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace
{

using motionTracking::AssignTrackers;
using motionTracking::ParseTrackerAssignmentSpec;
using motionTracking::SolveTrackerPose;
using motionTracking::TrackerAssignment;
using motionTracking::TrackerAssignmentBinding;
using motionTracking::TrackerAssignmentRefusal;
using motionTracking::TrackerAssignmentRefusalName;
using motionTracking::TrackerAssignmentSpec;
using motionTracking::TrackerIdentities;
using motionTracking::TrackerObservation;
using motionTracking::TrackerRegion;
using motionTracking::TrackerRegionBone;
using motionTracking::TrackerSolve;
using motionTracking::TrackerSolveConfig;
using motionTracking::TrackerSolveRefusal;
using motionTracking::TrackerSolveRefusalCount;
using motionTracking::TrackerSolveRefusalName;
using motionTracking::UnplacedTrackerPolicy;

// A list of regions an `assert` can hold. The braced initialiser cannot: the
// preprocessor balances parentheses and not braces, so `{A, B}` inside a macro
// arrives as two arguments and the diagnostic points at the wrong line.
std::vector<TrackerRegion>
Regions(std::initializer_list<TrackerRegion> regions)
{
    return std::vector<TrackerRegion>(regions);
}

pxr::GfQuatf
Turn(const pxr::GfVec3d& axis, double degrees)
{
    return pxr::GfQuatf(pxr::GfRotation(axis, degrees).GetQuat());
}

TrackerObservation
Reporting(std::string tracker, const pxr::GfQuatf& rotation)
{
    TrackerObservation observation;
    observation.tracker = std::move(tracker);
    observation.rotation = rotation;
    observation.hasRotation = true;
    return observation;
}

TrackerObservation
Reporting(std::string tracker, const pxr::GfVec3f& position,
          const pxr::GfQuatf& rotation)
{
    TrackerObservation observation = Reporting(std::move(tracker), rotation);
    observation.position = position;
    observation.hasPosition = true;
    return observation;
}

// The world rotation of `bone` under `pose`, composed here rather than borrowed
// from the library: a test that reused the solve's own composition would agree
// with it by construction and measure nothing.
pxr::GfQuatd
WorldRotation(const motion::HumanoidPose& pose, motion::HumanBone bone)
{
    std::vector<motion::HumanBone> chain;
    std::optional<motion::HumanBone> walk = bone;
    while (walk)
    {
        chain.push_back(*walk);
        walk = motion::HumanBoneParent(*walk);
    }

    pxr::GfQuatd world = pxr::GfQuatd::GetIdentity();
    for (auto it = chain.rbegin(); it != chain.rend(); ++it)
    {
        const std::size_t index = static_cast<std::size_t>(*it);
        if (pose.validRotations.test(index))
        {
            world = world * pxr::GfQuatd(pose.localRotations[index]);
        }
    }
    return world;
}

// Radians between two orientations, with the dot and the length product formed
// in the SAME precision. Mixing them is what turns a 1e-7 disagreement into a
// 9e-4 one at zero, which cost this repository a red macOS lane once already.
double
AngleBetween(const pxr::GfQuatd& lhs, const pxr::GfQuatd& rhs)
{
    const pxr::GfQuatd a = lhs.GetNormalized();
    const pxr::GfQuatd b = rhs.GetNormalized();
    double dot = a.GetReal() * b.GetReal()
                 + pxr::GfDot(a.GetImaginary(), b.GetImaginary());
    dot = std::fabs(dot);
    if (dot > 1.0)
    {
        dot = 1.0;
    }
    return 2.0 * std::acos(dot);
}

void
AssertReproduces(const motion::HumanoidPose& pose, motion::HumanBone bone,
                 const pxr::GfQuatf& observed)
{
    const double angle =
        AngleBetween(WorldRotation(pose, bone), pxr::GfQuatd(observed));
    // The comparison semantics' own tolerance, never a hand-picked epsilon:
    // what this solve is allowed to lose is what a retarget composition is
    // allowed to lose.
    assert(angle < static_cast<double>(motion::MotionTolerance{}.angle));
}

// A six-point rig: hips, head, two hands, two feet. Every fixture below starts
// from this and removes or adds one thing, so what a difference costs is
// visible in one place.
struct SixPoint
{
    std::vector<TrackerObservation> observed;
    TrackerAssignmentSpec spec;
    TrackerAssignment assignment;
};

SixPoint
MakeSixPoint()
{
    SixPoint rig;
    rig.observed = {
        Reporting("t1", pxr::GfVec3f(0.2f, 0.95f, -1.5f),
                  Turn(pxr::GfVec3d(0.0, 1.0, 0.0), 35.0)),
        Reporting("t2", Turn(pxr::GfVec3d(1.0, 0.0, 0.0), -20.0)),
        Reporting("t3", Turn(pxr::GfVec3d(0.0, 0.0, 1.0), 47.0)),
        Reporting("t4", Turn(pxr::GfVec3d(0.4, 0.5, 0.2), 61.0)),
        Reporting("t5", Turn(pxr::GfVec3d(1.0, 1.0, 0.0), 12.0)),
        Reporting("t6", Turn(pxr::GfVec3d(0.0, 1.0, 1.0), -73.0)),
    };
    assert(ParseTrackerAssignmentSpec(
        "t1=hips t2=head t3=leftHand t4=rightHand t5=leftFoot t6=rightFoot",
        &rig.spec, nullptr));
    rig.assignment = AssignTrackers(rig.spec, TrackerIdentities(rig.observed));
    assert(rig.assignment.Placed());
    return rig;
}

void
TestEveryRefusalHasAName()
{
    for (std::size_t i = 0; i < TrackerSolveRefusalCount; ++i)
    {
        assert(!TrackerSolveRefusalName(
                    static_cast<TrackerSolveRefusal>(i))
                    .empty());
    }
    // Outside the enum is empty rather than a guess, on the vocabulary's rule:
    // a printed report shows a hole rather than inventing a refusal.
    assert(TrackerSolveRefusalName(TrackerSolveRefusal::Count).empty());
    assert(TrackerSolveRefusalName(static_cast<TrackerSolveRefusal>(200))
               .empty());
}

void
TestARegionReachesABoneOnlyWhereThisSolveKnowsWhichOne()
{
    assert(TrackerRegionBone(TrackerRegion::Hips)
           == motion::HumanBone::Hips);
    assert(TrackerRegionBone(TrackerRegion::Chest)
           == motion::HumanBone::Chest);
    assert(TrackerRegionBone(TrackerRegion::Head)
           == motion::HumanBone::Head);
    assert(TrackerRegionBone(TrackerRegion::LeftHand)
           == motion::HumanBone::LeftHand);
    assert(TrackerRegionBone(TrackerRegion::RightHand)
           == motion::HumanBone::RightHand);
    assert(TrackerRegionBone(TrackerRegion::LeftFoot)
           == motion::HumanBone::LeftFoot);
    assert(TrackerRegionBone(TrackerRegion::RightFoot)
           == motion::HumanBone::RightFoot);

    // The four straps that sit between two bones. This is the library's own
    // argument read forwards, and it is checked rather than described because a
    // table is the cheapest place to answer a question this solve cannot.
    assert(!TrackerRegionBone(TrackerRegion::LeftKnee));
    assert(!TrackerRegionBone(TrackerRegion::RightKnee));
    assert(!TrackerRegionBone(TrackerRegion::LeftElbow));
    assert(!TrackerRegionBone(TrackerRegion::RightElbow));
    assert(!TrackerRegionBone(TrackerRegion::Count));
}

void
TestADefaultSolveHasConcludedNothing()
{
    const TrackerSolve solve;
    assert(!solve.Solved());
    assert(solve.refusal == TrackerSolveRefusal::AssignmentRefused);
    assert(solve.pose.validRotations.none());
    assert(!solve.pose.root.hasPosition);
    assert(!solve.pose.root.hasOrientation);
}

void
TestASolvedRigReproducesEveryObservedOrientation()
{
    const SixPoint rig = MakeSixPoint();
    const TrackerSolve solve =
        SolveTrackerPose(rig.assignment, rig.observed, 12.5);

    assert(solve.Solved());
    assert(solve.detail.empty());
    assert(solve.pose.timestamp == 12.5);

    // The invariant, once per placed bone. A hand is four joints below the
    // root and a foot is three, so a chain composed in the wrong order or
    // against the wrong parent fails here and only here.
    AssertReproduces(solve.pose, motion::HumanBone::Hips, rig.observed[0].rotation);
    AssertReproduces(solve.pose, motion::HumanBone::Head, rig.observed[1].rotation);
    AssertReproduces(solve.pose, motion::HumanBone::LeftHand,
                     rig.observed[2].rotation);
    AssertReproduces(solve.pose, motion::HumanBone::RightHand,
                     rig.observed[3].rotation);
    AssertReproduces(solve.pose, motion::HumanBone::LeftFoot,
                     rig.observed[4].rotation);
    AssertReproduces(solve.pose, motion::HumanBone::RightFoot,
                     rig.observed[5].rotation);

    // Sparse by construction: six placed bones and nothing else, with every
    // unobserved joint left at rest rather than estimated.
    assert(solve.pose.validRotations.count() == 6);
    assert(!solve.pose.validRotations.test(
        static_cast<std::size_t>(motion::HumanBone::Spine)));
    assert(!solve.pose.validRotations.test(
        static_cast<std::size_t>(motion::HumanBone::Neck)));
    assert(solve.pose.localRotations[static_cast<std::size_t>(
               motion::HumanBone::Neck)]
           == pxr::GfQuatf::GetIdentity());

    // Reported in binding order, which is the operator's declaration order.
    assert(solve.placed
           == Regions({
               TrackerRegion::Hips, TrackerRegion::Head,
               TrackerRegion::LeftHand, TrackerRegion::RightHand,
               TrackerRegion::LeftFoot, TrackerRegion::RightFoot}));
    assert(solve.unsolved.empty());
    assert(solve.withoutRotation.empty());
    // Every one of the six reported an orientation, so nothing above anything
    // fell silent and the fifth vector is empty on the path that works.
    assert(solve.withheldWithParent.empty());

    // Nothing about a tracker reaches the pose: a consumer that cannot tell
    // this from a clip-driven pose is reading it correctly.
    assert(!solve.pose.source);
    assert(!solve.pose.confidence);
    assert(solve.pose.expressions.IsEmpty());
}

void
TestAnAddedStrapChangesALocalRotationAndNoWorldOne()
{
    SixPoint rig = MakeSixPoint();
    const TrackerSolve without =
        SolveTrackerPose(rig.assignment, rig.observed, 0.0);

    // The same session with a chest strap added mid-rig. The head's tracker
    // reports exactly what it reported before.
    //
    // The chest turns about a **different axis from the hips**, and that is
    // load bearing rather than decorative: two rotations about one axis
    // commute, so a chain composed in the wrong order would reproduce the
    // observation anyway and this whole case would pass against the defect it
    // exists for. It did, until a mutation pass said so.
    rig.observed.push_back(
        Reporting("t7", Turn(pxr::GfVec3d(1.0, 0.0, 0.35), -28.0)));
    TrackerAssignmentSpec spec;
    assert(ParseTrackerAssignmentSpec(
        "t1=hips t2=head t3=leftHand t4=rightHand t5=leftFoot t6=rightFoot "
        "t7=chest",
        &spec, nullptr));
    const TrackerAssignment assignment =
        AssignTrackers(spec, TrackerIdentities(rig.observed));
    assert(assignment.Placed());
    const TrackerSolve with = SolveTrackerPose(assignment, rig.observed, 0.0);
    assert(with.Solved());

    const std::size_t head = static_cast<std::size_t>(motion::HumanBone::Head);
    // The chest absorbed part of the head's rotation, so the *local* value
    // moved...
    assert(with.pose.localRotations[head] != without.pose.localRotations[head]);
    // ...and the world orientation the head's tracker reported did not.
    AssertReproduces(with.pose, motion::HumanBone::Head,
                     rig.observed[1].rotation);
    AssertReproduces(with.pose, motion::HumanBone::Chest,
                     rig.observed[6].rotation);
    // The hands hang off the upper chest, which the chest strap moved: their
    // world orientations are still their own.
    AssertReproduces(with.pose, motion::HumanBone::LeftHand,
                     rig.observed[2].rotation);
    AssertReproduces(with.pose, motion::HumanBone::RightHand,
                     rig.observed[3].rotation);
}

void
TestTheHipsAreTheRootAndTheHipsBoneAtOnce()
{
    const SixPoint rig = MakeSixPoint();
    const TrackerSolve solve =
        SolveTrackerPose(rig.assignment, rig.observed, 0.0);

    assert(solve.pose.root.hasPosition);
    assert(solve.pose.root.worldPosition == rig.observed[0].position);
    assert(solve.pose.root.hasOrientation);

    // The rule the root/hips record states: a rig rooted at its hips has a root
    // path of one joint, so the body's orientation and the hips' local rotation
    // are the same value twice rather than two readings of one session.
    const std::size_t hips = static_cast<std::size_t>(motion::HumanBone::Hips);
    assert(AngleBetween(pxr::GfQuatd(solve.pose.root.worldOrientation),
                        pxr::GfQuatd(solve.pose.localRotations[hips]))
           < static_cast<double>(motion::MotionTolerance{}.angle));

    // Neither velocity is derived here. A solve sees one frame, and a velocity
    // computed from one frame would be a zero pretending to be a measurement.
    assert(!solve.pose.root.hasLinearVelocity);
    assert(!solve.pose.root.hasAngularVelocity);

    // Only the hips' position is consumed; every other one is reported.
    assert(solve.positionsUnused.empty());
}

void
TestARootThePolicyDoesNotAuthorIsReportedRatherThanDropped()
{
    const SixPoint rig = MakeSixPoint();
    TrackerSolveConfig config;
    config.authorRootMotion = false;
    const TrackerSolve solve =
        SolveTrackerPose(rig.assignment, rig.observed, 0.0, config);

    assert(solve.Solved());
    assert(!solve.pose.root.hasPosition);
    // The rotation is authored either way: a body that turned turned, whatever
    // the translation is worth.
    assert(solve.pose.root.hasOrientation);
    assert(solve.pose.validRotations.test(
        static_cast<std::size_t>(motion::HumanBone::Hips)));
    // And the position it did not take is on the report rather than gone.
    assert(solve.positionsUnused
           == Regions({TrackerRegion::Hips}));
}

void
TestEveryPositionButTheHipsIsReportedUnused()
{
    std::vector<TrackerObservation> observed = {
        Reporting("t1", pxr::GfVec3f(0.0f, 0.9f, 0.0f),
                  Turn(pxr::GfVec3d(0.0, 1.0, 0.0), 10.0)),
        Reporting("t2", pxr::GfVec3f(0.0f, 1.6f, 0.1f),
                  Turn(pxr::GfVec3d(0.0, 1.0, 0.0), 15.0)),
        Reporting("t3", pxr::GfVec3f(-0.3f, 1.1f, 0.2f),
                  Turn(pxr::GfVec3d(1.0, 0.0, 0.0), 5.0)),
    };
    TrackerAssignmentSpec spec;
    assert(ParseTrackerAssignmentSpec("t1=hips t2=head t3=leftHand", &spec,
                                      nullptr));
    const TrackerSolve solve = SolveTrackerPose(
        AssignTrackers(spec, TrackerIdentities(observed)), observed, 0.0);

    assert(solve.Solved());
    // Consuming one of these is IK, and IK needs limb lengths that belong to a
    // target rig. Reporting them is what keeps that a stated stopping point
    // rather than a silent drop.
    assert(solve.positionsUnused
           == Regions({TrackerRegion::Head,
                                         TrackerRegion::LeftHand}));
}

void
TestAStrapBetweenTwoBonesIsDataRatherThanARefusal()
{
    std::vector<TrackerObservation> observed = {
        Reporting("t1", pxr::GfVec3f(0.0f, 0.9f, 0.0f),
                  Turn(pxr::GfVec3d(0.0, 1.0, 0.0), 10.0)),
        // The knee carries a position, which is what a real full-body rig
        // sends: a strap this solve places onto no bone still reported a
        // number, and the two reports it produces answer different questions.
        Reporting("t2", pxr::GfVec3f(-0.2f, 0.45f, 0.05f),
                  Turn(pxr::GfVec3d(1.0, 0.0, 0.0), 30.0)),
        Reporting("t3", Turn(pxr::GfVec3d(1.0, 0.0, 0.0), -30.0)),
    };
    TrackerAssignmentSpec spec;
    assert(ParseTrackerAssignmentSpec("t1=hips t2=leftKnee t3=rightElbow",
                                      &spec, nullptr));
    const TrackerSolve solve = SolveTrackerPose(
        AssignTrackers(spec, TrackerIdentities(observed)), observed, 0.0);

    // A rig carrying straps this solve cannot place still produces a pose. The
    // two that did not reach a bone are named, so an operator sees which
    // devices are not driving anything rather than inferring it from a count.
    assert(solve.Solved());
    assert(solve.placed == Regions({TrackerRegion::Hips}));
    assert(solve.unsolved
           == Regions({TrackerRegion::LeftKnee,
                                         TrackerRegion::RightElbow}));
    assert(solve.pose.validRotations.count() == 1);
    // Bound, not placed: the knee is in both vectors, because `unsolved` says
    // a strap reached no bone and `positionsUnused` says a number nothing
    // read. Omitting it here would answer the first question twice.
    assert(solve.positionsUnused == Regions({TrackerRegion::LeftKnee}));
}

void
TestAnUnsetHalfIsNotCompared()
{
    // `motionCore::RootMotion`'s rule, and here for its reason: an unset half
    // is not required to hold its default, so a stale number under a cleared
    // flag must not make two reports of "this tracker sent no position"
    // differ.
    TrackerObservation quiet;
    quiet.tracker = "t1";
    TrackerObservation stale = quiet;
    stale.position = pxr::GfVec3f(9.0f, -3.0f, 0.5f);
    stale.rotation = Turn(pxr::GfVec3d(0.0, 1.0, 0.0), 90.0);
    assert(quiet == stale);
    assert(!(quiet != stale));

    // Under a set flag the value is the observation, and it is compared
    // exactly.
    TrackerObservation reported = quiet;
    reported.hasPosition = true;
    TrackerObservation elsewhere = reported;
    elsewhere.position = pxr::GfVec3f(0.0f, 0.0f, 1.0f);
    assert(reported != elsewhere);

    TrackerObservation turned = quiet;
    turned.hasRotation = true;
    TrackerObservation turnedFurther = turned;
    turnedFurther.rotation = Turn(pxr::GfVec3d(0.0, 1.0, 0.0), 15.0);
    assert(turned != turnedFurther);

    // A flag is itself part of the observation: "sent nothing" and "sent a
    // rotation that happens to be identity" are different reports.
    assert(quiet != turned);
    assert(Reporting("t1", pxr::GfQuatf::GetIdentity()) != quiet);
    // And the identity is compared too, so two devices reporting the same
    // thing are still two observations.
    TrackerObservation other = turned;
    other.tracker = "t2";
    assert(turned != other);
}

void
TestAPositionOnlyTrackerCannotOrientAJoint()
{
    std::vector<TrackerObservation> observed;
    TrackerObservation hips;
    hips.tracker = "t1";
    hips.position = pxr::GfVec3f(0.0f, 0.92f, 0.3f);
    hips.hasPosition = true;
    observed.push_back(hips);
    observed.push_back(
        Reporting("t2", Turn(pxr::GfVec3d(0.0, 1.0, 0.0), 20.0)));

    TrackerAssignmentSpec spec;
    assert(ParseTrackerAssignmentSpec("t1=hips t2=head", &spec, nullptr));
    const TrackerSolve solve = SolveTrackerPose(
        AssignTrackers(spec, TrackerIdentities(observed)), observed, 0.0);

    assert(solve.Solved());
    // The alternative — authoring identity — is bit-for-bit a tracker
    // reporting rest, so a consumer could not tell the two apart.
    assert(!solve.pose.validRotations.test(
        static_cast<std::size_t>(motion::HumanBone::Hips)));
    assert(!solve.pose.root.hasOrientation);
    assert(solve.withoutRotation
           == Regions({TrackerRegion::Hips}));
    // The position half still reached the root: half an observation is half an
    // observation, not none.
    assert(solve.pose.root.hasPosition);

    // And the head goes with it, although its own tracker sent an orientation.
    //
    // **This assertion used to read `placed == {Head}` and that was the
    // defect.** A consumer replays a stream with `hold`, so the hips it has in
    // this frame is the one it carried a frame ago -- not the identity the
    // composition would otherwise divide by. A real session made the cost
    // measurable: 16 frames of 777 dropped the hips rotation and the head and
    // both feet snapped 33.6 degrees, the hips' own orientation exactly, in the
    // trace and again in the clip replayed from it (report 04 section 5).
    assert(solve.placed.empty());
    assert(solve.withheldWithParent == Regions({TrackerRegion::Head}));
    assert(!solve.pose.validRotations.test(
        static_cast<std::size_t>(motion::HumanBone::Head)));
}

void
TestAWithheldBoneIsTheOneWhoseAncestorWasAssigned()
{
    // Two rigs differing in one thing: whether the silent hips is a bone this
    // assignment placed at all. That is the whole of the distinction the
    // header draws, so it is the whole of this fixture.
    const pxr::GfQuatf turned = Turn(pxr::GfVec3d(0.0, 1.0, 0.0), 33.6);

    // A head over an assigned hips that fell silent: withheld.
    {
        std::vector<TrackerObservation> observed;
        TrackerObservation hips;
        hips.tracker = "t1";
        observed.push_back(hips);
        observed.push_back(Reporting("t2", turned));

        TrackerAssignmentSpec spec;
        assert(ParseTrackerAssignmentSpec("t1=hips t2=head", &spec, nullptr));
        const TrackerSolve solve = SolveTrackerPose(
            AssignTrackers(spec, TrackerIdentities(observed)), observed, 0.0);
        assert(!solve.Solved());
        // Nothing was authored and no root was either, which is what an empty
        // pose is called. The frame carries the operator no less information
        // for it: the report says one strap sent nothing and one was withheld.
        assert(solve.refusal == TrackerSolveRefusal::NothingSolved);
        assert(solve.withoutRotation == Regions({TrackerRegion::Hips}));
        assert(solve.withheldWithParent == Regions({TrackerRegion::Head}));
    }

    // A head over an assigned hips whose tracker did not arrive in this frame
    // at all: withheld too, and this is the door the first version of the rule
    // left open. An absent statement produces **no binding** -- it goes to
    // `TrackerAssignment::absent` -- so a rule reading `bound` alone saw no
    // hips above the head and authored it against identity, which is the same
    // snap through the neighbouring failure. A consumer holds the bone
    // identically under both, and that is the whole argument.
    {
        std::vector<TrackerObservation> observed = {Reporting("t2", turned)};
        TrackerAssignmentSpec spec;
        assert(ParseTrackerAssignmentSpec("t1=hips t2=head", &spec, nullptr));
        const TrackerAssignment assignment =
            AssignTrackers(spec, TrackerIdentities(observed));
        // `Refuse` is about a tracker no statement places, never about a
        // statement no tracker arrived for, so this assignment placed.
        assert(assignment.Placed());
        assert(assignment.absent == Regions({TrackerRegion::Hips}));
        const TrackerSolve solve = SolveTrackerPose(assignment, observed, 0.0);
        assert(!solve.Solved());
        assert(solve.refusal == TrackerSolveRefusal::NothingSolved);
        assert(solve.placed.empty());
        assert(solve.withheldWithParent == Regions({TrackerRegion::Head}));
        // An absent region produces no binding, so it reaches neither of the
        // two vectors that are filled from one.
        assert(solve.withoutRotation.empty());
        // And the refusal says which of the two things happened. The tallies a
        // caller reads are over solved frames, so this line is the only place a
        // refused frame reports its regions.
        assert(solve.detail.find("withheld") != std::string::npos);
    }

    // The same head over a hips nobody assigned: placed, and the spine, chest,
    // upperChest and neck between them contribute identity exactly as before.
    // An unobserved bone is at rest in every frame of the session, so dividing
    // by identity is not an assumption about what a consumer holds.
    {
        std::vector<TrackerObservation> observed = {Reporting("t2", turned)};
        TrackerAssignmentSpec spec;
        assert(ParseTrackerAssignmentSpec("t2=head", &spec, nullptr));
        const TrackerSolve solve = SolveTrackerPose(
            AssignTrackers(spec, TrackerIdentities(observed)), observed, 0.0);
        assert(solve.Solved());
        assert(solve.placed == Regions({TrackerRegion::Head}));
        assert(solve.withheldWithParent.empty());
        AssertReproduces(solve.pose, motion::HumanBone::Head, turned);
    }
}

void
TestAWithheldBoneWithholdsWhatIsUnderItToo()
{
    // hips silent, chest and head both reporting. The chest is withheld under
    // the hips and the head under the chest -- and the head's chain is checked
    // against the hips as well, so a rig that grew a fourth level would not
    // need a fixed point to reach the same answer.
    std::vector<TrackerObservation> observed;
    TrackerObservation hips;
    hips.tracker = "t1";
    hips.position = pxr::GfVec3f(0.1f, 0.9f, 0.0f);
    hips.hasPosition = true;
    observed.push_back(hips);
    observed.push_back(
        Reporting("t2", Turn(pxr::GfVec3d(1.0, 0.0, 0.0), 12.0)));
    observed.push_back(
        Reporting("t3", Turn(pxr::GfVec3d(0.0, 1.0, 0.0), -40.0)));
    observed.push_back(
        Reporting("t4", Turn(pxr::GfVec3d(0.0, 0.0, 1.0), 25.0)));

    TrackerAssignmentSpec spec;
    assert(ParseTrackerAssignmentSpec(
        "t1=hips t2=chest t3=head t4=leftFoot", &spec, nullptr));
    const TrackerSolve solve = SolveTrackerPose(
        AssignTrackers(spec, TrackerIdentities(observed)), observed, 0.0);

    // The root position still arrives -- a hips that sent a position and no
    // rotation told the pipeline where the body is and not which way it faces,
    // and only the second of those is what a child's local rotation needs.
    assert(solve.Solved());
    assert(solve.pose.root.hasPosition);
    assert(!solve.pose.root.hasOrientation);
    assert(solve.placed.empty());
    assert(solve.withheldWithParent
           == Regions({TrackerRegion::Chest, TrackerRegion::Head,
                       TrackerRegion::LeftFoot}));
    assert(solve.pose.validRotations.none());
}

void
TestAnAssignmentThatRefusedRefusesTheSolveWithItsReasonAttached()
{
    std::vector<TrackerObservation> observed = {
        Reporting("t1", Turn(pxr::GfVec3d(0.0, 1.0, 0.0), 10.0)),
        Reporting("t9", Turn(pxr::GfVec3d(0.0, 1.0, 0.0), 20.0)),
    };
    TrackerAssignmentSpec spec;
    assert(ParseTrackerAssignmentSpec("t1=head", &spec, nullptr));
    spec.unplaced = UnplacedTrackerPolicy::Refuse;
    const TrackerAssignment assignment =
        AssignTrackers(spec, TrackerIdentities(observed));
    assert(assignment.refusal == TrackerAssignmentRefusal::UnplacedTracker);

    const TrackerSolve solve = SolveTrackerPose(assignment, observed, 0.0);
    assert(!solve.Solved());
    assert(solve.refusal == TrackerSolveRefusal::AssignmentRefused);
    // The layer below's refusal survives into the detail. A caller reading only
    // this enumerator would otherwise lose which of five things happened, and
    // "the operator mis-numbered a tracker" and "the rig is still coming up"
    // are not the same event.
    assert(solve.detail.find(std::string(TrackerAssignmentRefusalName(
               TrackerAssignmentRefusal::UnplacedTracker)))
           != std::string::npos);
    assert(solve.detail.find(assignment.detail) != std::string::npos);

    // Under a refusal about the bindings themselves there is nothing to
    // classify, and the vectors say so rather than carrying a half reading.
    assert(solve.placed.empty());
    assert(solve.unsolved.empty());
    assert(solve.withoutRotation.empty());
    assert(solve.withheldWithParent.empty());
    assert(solve.positionsUnused.empty());
    assert(solve.pose.validRotations.none());
}

void
TestAnAssignmentAppliedToADifferentArrayIsRefused()
{
    const SixPoint rig = MakeSixPoint();

    // The same assignment against a shorter observation: what a caller that
    // rebuilt one array and not the other produces.
    std::vector<TrackerObservation> shorter(rig.observed.begin(),
                                            rig.observed.begin() + 2);
    const TrackerSolve stale = SolveTrackerPose(rig.assignment, shorter, 0.0);
    assert(stale.refusal == TrackerSolveRefusal::AssignmentUnusable);
    assert(stale.detail.find("leftHand") != std::string::npos);

    // A hand-built assignment naming one region twice. `AssignTrackers` cannot
    // produce this — a spec stating a region twice is refused there — so it is
    // reachable only from a caller that assembled the bindings itself, which is
    // a supported way to drive this function.
    TrackerAssignment doubled;
    doubled.refusal = TrackerAssignmentRefusal::None;
    TrackerAssignmentBinding first;
    first.region = TrackerRegion::Head;
    first.observedIndex = 0;
    TrackerAssignmentBinding second;
    second.region = TrackerRegion::Head;
    second.observedIndex = 1;
    doubled.bound = {first, second};
    const TrackerSolve twice =
        SolveTrackerPose(doubled, rig.observed, 0.0);
    assert(twice.refusal == TrackerSolveRefusal::AssignmentUnusable);
    assert(twice.detail.find("head") != std::string::npos);
}

void
TestAValueThatIsNotOneIsRefusedAndOneNobodyReadsIsNot()
{
    const float nan = std::numeric_limits<float>::quiet_NaN();

    {
        SixPoint rig = MakeSixPoint();
        rig.observed[1].rotation = pxr::GfQuatf(nan, pxr::GfVec3f(0.0f));
        const TrackerSolve solve =
            SolveTrackerPose(rig.assignment, rig.observed, 0.0);
        assert(solve.refusal == TrackerSolveRefusal::ObservationInvalid);
        assert(solve.detail.find("t2") != std::string::npos);
        // Classified before it was refused, on the assignment layer's rule: a
        // report of a refused solve reads the same evidence a successful one
        // does.
        assert(!solve.placed.empty());
    }
    {
        SixPoint rig = MakeSixPoint();
        rig.observed[0].rotation = pxr::GfQuatf(0.0f, pxr::GfVec3f(0.0f));
        const TrackerSolve solve =
            SolveTrackerPose(rig.assignment, rig.observed, 0.0);
        // A zero quaternion has no orientation to normalise towards, and it is
        // exactly what a default-constructed value written by a caller looks
        // like.
        assert(solve.refusal == TrackerSolveRefusal::ObservationInvalid);
    }
    {
        SixPoint rig = MakeSixPoint();
        rig.observed[0].position = pxr::GfVec3f(nan, 0.0f, 0.0f);
        const TrackerSolve solve =
            SolveTrackerPose(rig.assignment, rig.observed, 0.0);
        assert(solve.refusal == TrackerSolveRefusal::ObservationInvalid);
    }
    {
        // The same bad position on a region whose position this solve does not
        // consume. Refusing here would let an observation nothing reads stop a
        // pose, and the value is reported unused exactly as a good one is.
        SixPoint rig = MakeSixPoint();
        rig.observed[1].position = pxr::GfVec3f(0.0f, nan, 0.0f);
        rig.observed[1].hasPosition = true;
        const TrackerSolve solve =
            SolveTrackerPose(rig.assignment, rig.observed, 0.0);
        assert(solve.Solved());
        assert(solve.positionsUnused
               == Regions({TrackerRegion::Head}));
    }
    {
        // And the hips' own position, once the policy stops reading it.
        SixPoint rig = MakeSixPoint();
        rig.observed[0].position = pxr::GfVec3f(nan, 0.0f, 0.0f);
        TrackerSolveConfig config;
        config.authorRootMotion = false;
        const TrackerSolve solve =
            SolveTrackerPose(rig.assignment, rig.observed, 0.0, config);
        assert(solve.Solved());
    }
}

void
TestNothingSolvedIsWhatAnEmptyPoseIsCalled()
{
    std::vector<TrackerObservation> observed;
    TrackerObservation bare;
    bare.tracker = "t1";
    observed.push_back(bare);
    observed.push_back(
        Reporting("t2", Turn(pxr::GfVec3d(1.0, 0.0, 0.0), 15.0)));

    TrackerAssignmentSpec spec;
    assert(ParseTrackerAssignmentSpec("t1=hips t2=leftKnee", &spec, nullptr));
    const TrackerSolve solve = SolveTrackerPose(
        AssignTrackers(spec, TrackerIdentities(observed)), observed, 0.0);

    // A rig whose only orientation is on a strap this solve refuses, and whose
    // hips reported neither half. The assignment placed both trackers, so
    // nothing below it objected — and a pose carrying nothing is not a solve.
    assert(!solve.Solved());
    assert(solve.refusal == TrackerSolveRefusal::NothingSolved);
    assert(solve.unsolved
           == Regions({TrackerRegion::LeftKnee}));
    assert(solve.withoutRotation
           == Regions({TrackerRegion::Hips}));
}

void
TestARotationIsNormalisedRatherThanTrusted()
{
    const pxr::GfQuatf turned = Turn(pxr::GfVec3d(0.0, 1.0, 0.0), 40.0);
    pxr::GfQuatf scaled = turned;
    scaled *= 2.5f;

    std::vector<TrackerObservation> observed = {Reporting("t1", scaled)};
    TrackerAssignmentSpec spec;
    assert(ParseTrackerAssignmentSpec("t1=head", &spec, nullptr));
    const TrackerSolve solve = SolveTrackerPose(
        AssignTrackers(spec, TrackerIdentities(observed)), observed, 0.0);

    assert(solve.Solved());
    const std::size_t head = static_cast<std::size_t>(motion::HumanBone::Head);
    // A non-unit quaternion is a scaling as well as a rotation, and a pose that
    // carried one would compose that scale into every bone below it.
    assert(std::fabs(solve.pose.localRotations[head].GetLength() - 1.0f)
           < 1e-5f);
    AssertReproduces(solve.pose, motion::HumanBone::Head, turned);
}

void
TestTheFirstRefusalWinsInTheStatedOrder()
{
    // An observation that is invalid *and* an assignment that cannot be applied
    // to it. The order is assignment · applicability · values · nothing solved,
    // so this is `AssignmentUnusable` and a test comparing one enumerator knows
    // which.
    SixPoint rig = MakeSixPoint();
    rig.observed[0].rotation =
        pxr::GfQuatf(std::numeric_limits<float>::quiet_NaN(),
                     pxr::GfVec3f(0.0f));
    std::vector<TrackerObservation> shorter(rig.observed.begin(),
                                            rig.observed.begin() + 1);
    const TrackerSolve solve = SolveTrackerPose(rig.assignment, shorter, 0.0);
    assert(solve.refusal == TrackerSolveRefusal::AssignmentUnusable);

    // And an assignment that refused outranks both.
    TrackerAssignment refused;
    refused.refusal = TrackerAssignmentRefusal::SpecInvalid;
    const TrackerSolve outermost =
        SolveTrackerPose(refused, shorter, 0.0);
    assert(outermost.refusal == TrackerSolveRefusal::AssignmentRefused);
}

void
TestIdentitiesAreTheArrayTheAssignmentWasMadeFrom()
{
    const SixPoint rig = MakeSixPoint();
    const std::vector<std::string_view> identities =
        TrackerIdentities(rig.observed);
    assert(identities.size() == rig.observed.size());
    for (std::size_t i = 0; i < identities.size(); ++i)
    {
        assert(identities[i] == rig.observed[i].tracker);
    }

    // Empty in, empty out — a caller with no observation gets no identities
    // rather than a one-element vector holding an empty view.
    assert(TrackerIdentities({}).empty());
}

} // namespace

int
main()
{
    TestEveryRefusalHasAName();
    TestARegionReachesABoneOnlyWhereThisSolveKnowsWhichOne();
    TestADefaultSolveHasConcludedNothing();
    TestASolvedRigReproducesEveryObservedOrientation();
    TestAnAddedStrapChangesALocalRotationAndNoWorldOne();
    TestTheHipsAreTheRootAndTheHipsBoneAtOnce();
    TestARootThePolicyDoesNotAuthorIsReportedRatherThanDropped();
    TestEveryPositionButTheHipsIsReportedUnused();
    TestAStrapBetweenTwoBonesIsDataRatherThanARefusal();
    TestAnUnsetHalfIsNotCompared();
    TestAPositionOnlyTrackerCannotOrientAJoint();
    TestAWithheldBoneIsTheOneWhoseAncestorWasAssigned();
    TestAWithheldBoneWithholdsWhatIsUnderItToo();
    TestAnAssignmentThatRefusedRefusesTheSolveWithItsReasonAttached();
    TestAnAssignmentAppliedToADifferentArrayIsRefused();
    TestAValueThatIsNotOneIsRefusedAndOneNobodyReadsIsNot();
    TestNothingSolvedIsWhatAnEmptyPoseIsCalled();
    TestARotationIsNormalisedRatherThanTrusted();
    TestTheFirstRefusalWinsInTheStatedOrder();
    TestIdentitiesAreTheArrayTheAssignmentWasMadeFrom();
    std::puts("motionTracking tracker solve tests passed");
    return 0;
}
