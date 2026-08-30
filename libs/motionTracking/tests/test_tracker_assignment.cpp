// SPDX-License-Identifier: Apache-2.0
//
// The assignment policy, checked as a policy rather than as a map.
//
// Almost nothing here is arithmetic. What this library can get wrong is a
// *decision* — which refusal a caller is told about, and therefore whether a
// live session stops or waits — so the cases below are named for the decision
// they pin rather than for the function they call, and the three policies are
// exercised against the **same** observation so the difference between them is
// visible in one place.
//
// The rig in every fixture is a made-up one. There is no tracker identity here
// copied off a wire, and the boundary check beside this file refuses one: a
// generic contract whose fixtures are one source's numbering is a generic
// contract in name only. `t1`/`t2`/`t3` are what a device could be called and
// what none of them is.
#include "motionTracking/TrackerAssignment.h"
#include "motionTracking/TrackerRegion.h"

#include <cassert>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace
{

using motionTracking::AssignTrackers;
using motionTracking::ParseTrackerAssignmentSpec;
using motionTracking::ParseTrackerRegion;
using motionTracking::ParseUnplacedTrackerPolicy;
using motionTracking::TrackerAssignment;
using motionTracking::TrackerAssignmentRefusal;
using motionTracking::TrackerAssignmentRefusalCount;
using motionTracking::TrackerAssignmentRefusalName;
using motionTracking::TrackerAssignmentSpec;
using motionTracking::TrackerRegion;
using motionTracking::TrackerRegionCount;
using motionTracking::TrackerRegionName;
using motionTracking::TrackerRegionStatement;
using motionTracking::UnplacedTrackerPolicy;
using motionTracking::UnplacedTrackerPolicyName;
using motionTracking::ValidateTrackerAssignmentSpec;

TrackerRegionStatement
Statement(std::string tracker, TrackerRegion region)
{
    TrackerRegionStatement statement;
    statement.tracker = std::move(tracker);
    statement.region = region;
    return statement;
}

// A three-point statement: head and two hands, which is the rig every tracker
// source can present and the smallest one worth stating.
TrackerAssignmentSpec
ThreePoint()
{
    TrackerAssignmentSpec spec;
    spec.statements.push_back(Statement("t1", TrackerRegion::Head));
    spec.statements.push_back(Statement("t2", TrackerRegion::LeftHand));
    spec.statements.push_back(Statement("t3", TrackerRegion::RightHand));
    return spec;
}

void
TestEveryRegionHasANameAndRoundTrips()
{
    for (std::size_t i = 0; i < TrackerRegionCount; ++i)
    {
        const auto region = static_cast<TrackerRegion>(i);
        const std::string_view name = TrackerRegionName(region);
        assert(!name.empty());
        const auto parsed = ParseTrackerRegion(name);
        assert(parsed.has_value());
        assert(*parsed == region);
    }

    // Past the end is a hole, not the first entry.
    assert(TrackerRegionName(TrackerRegion::Count).empty());
    assert(TrackerRegionName(static_cast<TrackerRegion>(200)).empty());

    // Exact match only. A near miss is a refusal rather than a guess, which is
    // what keeps a tolerant parser from becoming the automatic assignment this
    // milestone deliberately does not build.
    assert(!ParseTrackerRegion("LeftFoot").has_value());
    assert(!ParseTrackerRegion("leftfoot").has_value());
    assert(!ParseTrackerRegion("lfoot").has_value());
    assert(!ParseTrackerRegion("").has_value());
    assert(ParseTrackerRegion("leftFoot").has_value());
}

void
TestEveryRefusalAndPolicyHasAName()
{
    for (std::size_t i = 0; i < TrackerAssignmentRefusalCount; ++i)
    {
        assert(!TrackerAssignmentRefusalName(
                    static_cast<TrackerAssignmentRefusal>(i))
                    .empty());
    }
    assert(TrackerAssignmentRefusalName(TrackerAssignmentRefusal::Count)
               .empty());

    for (std::size_t i = 0;
         i < static_cast<std::size_t>(UnplacedTrackerPolicy::Count); ++i)
    {
        const auto policy = static_cast<UnplacedTrackerPolicy>(i);
        const std::string_view name = UnplacedTrackerPolicyName(policy);
        assert(!name.empty());
        const auto parsed = ParseUnplacedTrackerPolicy(name);
        assert(parsed.has_value());
        assert(*parsed == policy);
    }
    assert(UnplacedTrackerPolicyName(UnplacedTrackerPolicy::Count).empty());
    assert(!ParseUnplacedTrackerPolicy("Refuse").has_value());
}

void
TestADefaultAssignmentHasConcludedNothing()
{
    // A struct whose default state claimed a binding would be a trap for
    // exactly the paths that forget to check.
    const TrackerAssignment assignment;
    assert(!assignment.Placed());
    assert(assignment.refusal == TrackerAssignmentRefusal::SpecInvalid);
    assert(assignment.bound.empty());
}

void
TestAStatedRigPlacesEveryTrackerInDeclarationOrder()
{
    const std::vector<std::string_view> observed = {"t3", "t1", "t2"};
    const TrackerAssignment assignment = AssignTrackers(ThreePoint(), observed);

    assert(assignment.Placed());
    assert(assignment.refusal == TrackerAssignmentRefusal::None);
    assert(assignment.detail.empty());
    assert(assignment.unplaced.empty());
    assert(assignment.absent.empty());

    // The report order is the operator's, not the observation's: the statement
    // named head, then left, then right, and it reads back that way even though
    // the wire delivered them backwards.
    assert(assignment.bound.size() == 3);
    assert(assignment.bound[0].region == TrackerRegion::Head);
    assert(assignment.bound[0].observedIndex == 1);
    assert(assignment.bound[1].region == TrackerRegion::LeftHand);
    assert(assignment.bound[1].observedIndex == 2);
    assert(assignment.bound[2].region == TrackerRegion::RightHand);
    assert(assignment.bound[2].observedIndex == 0);

    // Both lookups, in both directions.
    assert(assignment.ObservedFor(TrackerRegion::LeftHand) == std::size_t{2});
    assert(!assignment.ObservedFor(TrackerRegion::Hips).has_value());
    assert(assignment.RegionFor(0) == TrackerRegion::RightHand);
    assert(!assignment.RegionFor(9).has_value());
}

void
TestAnUnplacedTrackerIsThreeAnswersAndTheEnumeratorIsTheDifference()
{
    // One observation, three policies. The rig carries a fourth device the
    // three-point statement says nothing about.
    const std::vector<std::string_view> observed = {"t1", "t2", "t3", "t4"};

    TrackerAssignmentSpec refuse = ThreePoint();
    refuse.unplaced = UnplacedTrackerPolicy::Refuse;
    const TrackerAssignment refused = AssignTrackers(refuse, observed);
    assert(!refused.Placed());
    assert(refused.refusal == TrackerAssignmentRefusal::UnplacedTracker);

    TrackerAssignmentSpec hold = ThreePoint();
    hold.unplaced = UnplacedTrackerPolicy::Hold;
    const TrackerAssignment held = AssignTrackers(hold, observed);
    assert(!held.Placed());
    assert(held.refusal == TrackerAssignmentRefusal::Held);

    TrackerAssignmentSpec ignore = ThreePoint();
    ignore.unplaced = UnplacedTrackerPolicy::Ignore;
    const TrackerAssignment ignored = AssignTrackers(ignore, observed);
    assert(ignored.Placed());
    assert(ignored.refusal == TrackerAssignmentRefusal::None);

    // `Refuse` and `Held` are the same observation and the same evidence, and
    // the enumerator is the whole of the difference — a caller stops on one and
    // waits on the other. Collapsing them would make a mis-numbered tracker and
    // a device that has not powered on the same event.
    assert(refused.unplaced == held.unplaced);
    assert(refused.bound == held.bound);
    assert(refused.refusal != held.refusal);

    // And every policy sees the same unplaced device, including the one that
    // succeeds: `Ignore` reports it rather than forgetting it.
    for (const TrackerAssignment* result : {&refused, &held, &ignored})
    {
        assert(result->unplaced.size() == 1);
        assert(result->unplaced[0] == 3);
        assert(result->bound.size() == 3);
    }
}

void
TestEveryVectorIsFilledWhateverTheRefusal()
{
    // The caller that most needs this struct is the one reporting on an
    // assignment that did not succeed. A refusal that cleared its own evidence
    // would make "the operator named a device this rig does not carry" and
    // "nothing was tried" indistinguishable.
    TrackerAssignmentSpec spec = ThreePoint();
    spec.statements.push_back(Statement("t9", TrackerRegion::Hips));
    spec.unplaced = UnplacedTrackerPolicy::Refuse;

    const std::vector<std::string_view> observed = {"t1", "t2", "t4"};
    const TrackerAssignment assignment = AssignTrackers(spec, observed);

    assert(!assignment.Placed());
    assert(assignment.refusal == TrackerAssignmentRefusal::UnplacedTracker);
    assert(!assignment.detail.empty());
    assert(assignment.bound.size() == 2);
    assert(assignment.absent.size() == 2);
    assert(assignment.absent[0] == TrackerRegion::RightHand);
    assert(assignment.absent[1] == TrackerRegion::Hips);
    assert(assignment.unplaced.size() == 1);
    assert(assignment.unplaced[0] == 2);
}

void
TestAStatedTrackerThatDidNotArriveIsDataUnderTwoPoliciesAndHeldUnderTheThird()
{
    // The other direction from `unplaced`. Under `Refuse` and `Ignore` it is
    // data and a partial rig still assigns, because a statement listing more
    // than the rig carries is something an operator has to see rather than
    // something this library can resolve.
    const std::vector<std::string_view> observed = {"t1", "t2"};

    for (const UnplacedTrackerPolicy policy :
         {UnplacedTrackerPolicy::Refuse, UnplacedTrackerPolicy::Ignore})
    {
        TrackerAssignmentSpec spec = ThreePoint();
        spec.unplaced = policy;
        const TrackerAssignment assignment = AssignTrackers(spec, observed);

        assert(assignment.Placed());
        assert(assignment.unplaced.empty());
        assert(assignment.bound.size() == 2);
        assert(assignment.absent.size() == 1);
        assert(assignment.absent[0] == TrackerRegion::RightHand);
    }

    // And this is the case `Hold` exists for: a rig coming up one device at a
    // time is short of a *stated* tracker rather than carrying an extra one, so
    // a Hold that read only the unplaced side would never fire here — which is
    // the whole of what its row promises.
    TrackerAssignmentSpec holding = ThreePoint();
    holding.unplaced = UnplacedTrackerPolicy::Hold;
    const TrackerAssignment held = AssignTrackers(holding, observed);
    assert(!held.Placed());
    assert(held.refusal == TrackerAssignmentRefusal::Held);
    assert(held.bound.size() == 2);
    assert(held.absent.size() == 1);
    assert(held.unplaced.empty());

    // The rig completes and the same statement assigns, with nothing held.
    const TrackerAssignment complete =
        AssignTrackers(holding, {"t1", "t2", "t3"});
    assert(complete.Placed());
    assert(complete.absent.empty());
}

void
TestAnAssignmentThatPlacedNothingIsRefusedUnderEveryPolicy()
{
    // `Ignore` is the policy this exists for: without it, an observation of
    // devices a statement says nothing about would return success with an empty
    // binding set and let a caller drive a solve from nothing.
    const std::vector<std::string_view> observed = {"t7", "t8"};

    for (const UnplacedTrackerPolicy policy :
         {UnplacedTrackerPolicy::Refuse, UnplacedTrackerPolicy::Ignore,
          UnplacedTrackerPolicy::Hold})
    {
        TrackerAssignmentSpec spec = ThreePoint();
        spec.unplaced = policy;
        const TrackerAssignment assignment = AssignTrackers(spec, observed);

        assert(!assignment.Placed());
        assert(assignment.bound.empty());
        // The unplaced policies get their own refusal first, because it is the
        // more specific one and it says what the operator can act on. `Ignore`
        // has no such refusal, so it lands on the general one.
        if (policy == UnplacedTrackerPolicy::Ignore)
        {
            assert(assignment.refusal
                   == TrackerAssignmentRefusal::NothingPlaced);
        }
        else
        {
            assert(assignment.refusal != TrackerAssignmentRefusal::None);
            assert(assignment.refusal
                   != TrackerAssignmentRefusal::NothingPlaced);
        }
    }

    // An observation carrying nothing at all has no unplaced device to refuse,
    // so `Refuse` and `Ignore` land on the general refusal. `Hold` does not: an
    // empty observation is short of every stated tracker, which is exactly what
    // it waits for — so `NothingPlaced` is unreachable under it, and that is
    // stated in the enum rather than pretended.
    for (const UnplacedTrackerPolicy policy :
         {UnplacedTrackerPolicy::Refuse, UnplacedTrackerPolicy::Ignore})
    {
        TrackerAssignmentSpec spec = ThreePoint();
        spec.unplaced = policy;
        const TrackerAssignment assignment = AssignTrackers(spec, {});
        assert(assignment.refusal == TrackerAssignmentRefusal::NothingPlaced);
        assert(assignment.absent.size() == 3);
    }

    TrackerAssignmentSpec holding = ThreePoint();
    holding.unplaced = UnplacedTrackerPolicy::Hold;
    const TrackerAssignment nothing = AssignTrackers(holding, {});
    assert(nothing.refusal == TrackerAssignmentRefusal::Held);
    assert(nothing.absent.size() == 3);
}

void
TestAStatementThatIsNotOneIsRefusedBeforeAnyRig()
{
    const std::vector<std::string_view> observed = {"t1", "t2", "t3"};

    // Six ways to fail, each made to fail on its own.
    std::vector<TrackerAssignmentSpec> broken;

    broken.emplace_back(); // no statements at all

    TrackerAssignmentSpec emptyIdentity;
    emptyIdentity.statements.push_back(Statement("", TrackerRegion::Head));
    broken.push_back(emptyIdentity);

    // An identity that cannot be written down is not an operator's statement:
    // the only required path is one a person types, and this one could not have
    // been typed or restated.
    TrackerAssignmentSpec unwritable;
    unwritable.statements.push_back(Statement("t 1", TrackerRegion::Head));
    broken.push_back(unwritable);

    TrackerAssignmentSpec unwritableSeparator;
    unwritableSeparator.statements.push_back(
        Statement("t=1", TrackerRegion::Head));
    broken.push_back(unwritableSeparator);

    TrackerAssignmentSpec outsideVocabulary;
    outsideVocabulary.statements.push_back(
        Statement("t1", static_cast<TrackerRegion>(200)));
    broken.push_back(outsideVocabulary);

    TrackerAssignmentSpec twiceStated;
    twiceStated.statements.push_back(Statement("t1", TrackerRegion::Head));
    twiceStated.statements.push_back(Statement("t1", TrackerRegion::Hips));
    broken.push_back(twiceStated);

    // Two devices on one region is a mis-statement, not a redundancy: a solve
    // reading one of them would be reading whichever the loop met first.
    TrackerAssignmentSpec sharedRegion;
    sharedRegion.statements.push_back(Statement("t1", TrackerRegion::Head));
    sharedRegion.statements.push_back(Statement("t2", TrackerRegion::Head));
    broken.push_back(sharedRegion);

    TrackerAssignmentSpec badPolicy = ThreePoint();
    badPolicy.unplaced = static_cast<UnplacedTrackerPolicy>(200);
    broken.push_back(badPolicy);

    for (const TrackerAssignmentSpec& spec : broken)
    {
        std::string reason;
        assert(!ValidateTrackerAssignmentSpec(spec, &reason));
        assert(!reason.empty());

        // And the refusal reaches a caller through the assignment, with the
        // validator's reason carried verbatim rather than reworded.
        const TrackerAssignment assignment = AssignTrackers(spec, observed);
        assert(assignment.refusal == TrackerAssignmentRefusal::SpecInvalid);
        assert(assignment.detail == reason);
    }

    // A null `reason` is not a second code path.
    TrackerAssignmentSpec empty;
    assert(!ValidateTrackerAssignmentSpec(empty, nullptr));
    assert(ValidateTrackerAssignmentSpec(ThreePoint(), nullptr));
}

void
TestAnObservationThatIsNotOneIsRefusedRatherThanHalfBound()
{
    // Binding one of two identically named devices is how a rig gets half
    // assigned with nobody told, so it is a refusal and not a first-wins rule.
    const TrackerAssignment repeated =
        AssignTrackers(ThreePoint(), {"t1", "t2", "t2"});
    assert(!repeated.Placed());
    assert(repeated.refusal == TrackerAssignmentRefusal::ObservationInvalid);
    assert(!repeated.detail.empty());

    const TrackerAssignment nameless =
        AssignTrackers(ThreePoint(), {"t1", "", "t3"});
    assert(!nameless.Placed());
    assert(nameless.refusal == TrackerAssignmentRefusal::ObservationInvalid);

    // It outranks the unplaced policy, because a statement cannot be judged
    // against an observation that is not one. `t4` here would be unplaced under
    // `Refuse`, and the observation's own defect is what comes back.
    const TrackerAssignment both =
        AssignTrackers(ThreePoint(), {"t1", "t1", "t4"});
    assert(both.refusal == TrackerAssignmentRefusal::ObservationInvalid);
}

void
TestTheTextFormIsWhatAnOperatorTypes()
{
    TrackerAssignmentSpec spec;
    std::string reason;
    assert(ParseTrackerAssignmentSpec(
        "t1=head t2=leftHand,t3=rightHand  # a three-point rig\n"
        "t4=hips",
        &spec, &reason));
    assert(reason.empty());
    assert(spec.statements.size() == 4);
    assert(spec.statements[0] == Statement("t1", TrackerRegion::Head));
    assert(spec.statements[3] == Statement("t4", TrackerRegion::Hips));

    // The policy is not in the syntax, and parsing does not reset it: a caller
    // that chose `Hold` and then read a statement did not ask to be put back.
    TrackerAssignmentSpec holding;
    holding.unplaced = UnplacedTrackerPolicy::Hold;
    assert(ParseTrackerAssignmentSpec("t1=head", &holding, nullptr));
    assert(holding.unplaced == UnplacedTrackerPolicy::Hold);

    // Everything a statement can fail on, and each one names what it saw.
    for (const std::string_view text : {
             std::string_view("t1"),          // no `=` at all
             std::string_view("t1=head=hips"),// two, so the split is ambiguous
             std::string_view("t1=elbow"),    // outside the vocabulary
             std::string_view("t1=Head"),     // a near miss is not a guess
             std::string_view(""),            // parses cleanly, validates to nothing
             std::string_view("# only a comment"),
             std::string_view("t1=head t1=hips"),      // stated twice
             std::string_view("t1=head t2=head"),      // one region, two devices
         })
    {
        TrackerAssignmentSpec parsed;
        std::string why;
        assert(!ParseTrackerAssignmentSpec(text, &parsed, &why));
        assert(!why.empty());
        // A refused parse leaves the caller's spec untouched rather than half
        // filled — the assignment it already had is still the one it has.
        assert(parsed.statements.empty());
    }

    // A parse that validates is a spec that assigns, with no second step.
    TrackerAssignmentSpec typed;
    assert(ParseTrackerAssignmentSpec("t1=head, t2=leftFoot, t3=rightFoot",
                                      &typed, nullptr));
    assert(ValidateTrackerAssignmentSpec(typed, nullptr));
    const TrackerAssignment assignment =
        AssignTrackers(typed, {"t1", "t2", "t3"});
    assert(assignment.Placed());
    assert(assignment.ObservedFor(TrackerRegion::RightFoot)
           == std::size_t{2});

    assert(!ParseTrackerAssignmentSpec("t1=head", nullptr, &reason));
}

} // namespace

int
main()
{
    TestEveryRegionHasANameAndRoundTrips();
    TestEveryRefusalAndPolicyHasAName();
    TestADefaultAssignmentHasConcludedNothing();
    TestAStatedRigPlacesEveryTrackerInDeclarationOrder();
    TestAnUnplacedTrackerIsThreeAnswersAndTheEnumeratorIsTheDifference();
    TestEveryVectorIsFilledWhateverTheRefusal();
    TestAStatedTrackerThatDidNotArriveIsDataUnderTwoPoliciesAndHeldUnderTheThird();
    TestAnAssignmentThatPlacedNothingIsRefusedUnderEveryPolicy();
    TestAStatementThatIsNotOneIsRefusedBeforeAnyRig();
    TestAnObservationThatIsNotOneIsRefusedRatherThanHalfBound();
    TestTheTextFormIsWhatAnOperatorTypes();
    std::puts("motionTracking tracker assignment tests passed");
    return 0;
}
