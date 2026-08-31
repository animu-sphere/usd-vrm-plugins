// SPDX-License-Identifier: Apache-2.0
#include "motionTracking/TrackerSolve.h"

#include "pxr/base/gf/quatd.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <string>

namespace motionTracking
{

namespace
{

constexpr std::array<std::string_view, TrackerSolveRefusalCount> kRefusalNames =
    {"None", "AssignmentRefused", "AssignmentUnusable", "ObservationInvalid",
     "NothingSolved"};

static_assert(kRefusalNames.size() == TrackerSolveRefusalCount,
              "every refusal needs a name a report can print");

// Below this, normalising is amplifying noise rather than removing it: a
// quaternion this short carries no orientation to recover, and the zero one —
// which is what a default-constructed value written by a caller looks like — is
// the case that actually arrives.
constexpr double kShortestUsableRotation = 1e-6;

std::string
Quote(std::string_view text)
{
    return "\"" + std::string(text) + "\"";
}

std::string
RegionText(TrackerRegion region)
{
    const std::string_view name = TrackerRegionName(region);
    return name.empty() ? std::string("<outside the vocabulary>")
                        : Quote(name);
}

bool
IsFinite(const pxr::GfVec3f& value) noexcept
{
    return std::isfinite(value[0]) && std::isfinite(value[1])
           && std::isfinite(value[2]);
}

bool
IsFinite(const pxr::GfQuatf& value) noexcept
{
    return std::isfinite(value.GetReal()) && IsFinite(value.GetImaginary());
}

// Whether any bone above `bone` is one the assignment placed that carried no
// rotation in this frame.
//
// That is the case the header separates from an unobserved ancestor: a
// consumer holds such a bone at the value it had a frame ago rather than at
// rest, so the identity `ParentWorldRotation` would divide by is not the
// rotation the consumer will be composing against. Transitivity comes free —
// a chain's ancestors are the union of its parent's and its parent — so this
// reads one chain and not a fixed point.
bool
AncestorFellSilent(motion::HumanBone bone,
                   const std::bitset<motion::HumanBoneCount>& placedBones,
                   const std::bitset<motion::HumanBoneCount>& withRotation)
{
    std::optional<motion::HumanBone> parent = motion::HumanBoneParent(bone);
    while (parent)
    {
        const std::size_t index = static_cast<std::size_t>(*parent);
        if (placedBones.test(index) && !withRotation.test(index))
        {
            return true;
        }
        parent = motion::HumanBoneParent(*parent);
    }
    return false;
}

// The world rotation of `bone`'s parent chain, composed from what this solve
// has authored so far. Every bone the chain carries that nobody observed
// contributes identity, which is the header's "an unobserved joint stays at
// rest" read as arithmetic.
//
// Composed in double and stored as float. A chain is at most six products deep
// and float would hold the comparison tolerance comfortably; doubles cost
// nothing here and keep the residual of the invariant below the rounding of the
// values themselves, so a test that fails is reporting the solve rather than
// the arithmetic.
pxr::GfQuatd
ParentWorldRotation(motion::HumanBone bone,
                    const std::array<pxr::GfQuatd, motion::HumanBoneCount>&
                        authored,
                    const std::bitset<motion::HumanBoneCount>& valid)
{
    std::vector<motion::HumanBone> chain;
    std::optional<motion::HumanBone> parent = motion::HumanBoneParent(bone);
    while (parent)
    {
        chain.push_back(*parent);
        parent = motion::HumanBoneParent(*parent);
    }

    // `chain` runs child-ward to root-ward; compose from the root down, because
    // world = parentWorld * local and the outermost rotation is applied last.
    pxr::GfQuatd world = pxr::GfQuatd::GetIdentity();
    for (auto it = chain.rbegin(); it != chain.rend(); ++it)
    {
        const std::size_t index = static_cast<std::size_t>(*it);
        if (valid.test(index))
        {
            world = world * authored[index];
        }
    }
    return world;
}

} // namespace

std::optional<motion::HumanBone>
TrackerRegionBone(TrackerRegion region) noexcept
{
    switch (region)
    {
    case TrackerRegion::Head:
        return motion::HumanBone::Head;
    case TrackerRegion::Chest:
        return motion::HumanBone::Chest;
    case TrackerRegion::Hips:
        return motion::HumanBone::Hips;
    case TrackerRegion::LeftHand:
        return motion::HumanBone::LeftHand;
    case TrackerRegion::RightHand:
        return motion::HumanBone::RightHand;
    case TrackerRegion::LeftFoot:
        return motion::HumanBone::LeftFoot;
    case TrackerRegion::RightFoot:
        return motion::HumanBone::RightFoot;

    // The four this solve refuses, and the refusal is this library's own
    // argument read forwards: a strap between two bones is not either of them,
    // and telling a bent knee from a rotated thigh needs limb lengths that
    // belong to a target rig. Listed rather than defaulted, so a region added
    // to the vocabulary fails to compile here instead of silently becoming
    // unsolvable.
    case TrackerRegion::LeftElbow:
    case TrackerRegion::RightElbow:
    case TrackerRegion::LeftKnee:
    case TrackerRegion::RightKnee:
    case TrackerRegion::Count:
        break;
    }
    return std::nullopt;
}

std::string_view
TrackerSolveRefusalName(TrackerSolveRefusal refusal) noexcept
{
    const std::size_t index = static_cast<std::size_t>(refusal);
    return index < kRefusalNames.size() ? kRefusalNames[index]
                                        : std::string_view();
}

TrackerSolve
SolveTrackerPose(const TrackerAssignment& assignment,
                 const std::vector<TrackerObservation>& observed,
                 double timestamp, const TrackerSolveConfig& config)
{
    TrackerSolve solve;
    solve.pose.timestamp = timestamp;

    // Outermost first: an assignment that refused says nothing about an
    // observation. The detail carries the layer below's own refusal, because a
    // caller reading only this enumerator would otherwise lose which of five
    // things happened there.
    if (!assignment.Placed())
    {
        solve.refusal = TrackerSolveRefusal::AssignmentRefused;
        solve.detail =
            std::string(TrackerAssignmentRefusalName(assignment.refusal));
        if (!assignment.detail.empty())
        {
            solve.detail += ": " + assignment.detail;
        }
        return solve;
    }

    // Whether these bindings can be read against *this* array at all. Neither
    // failure is producible by `AssignTrackers` on the identities the array
    // gave it, so both mean the two calls were handed different arrays.
    for (std::size_t i = 0; i < assignment.bound.size(); ++i)
    {
        const TrackerAssignmentBinding& binding = assignment.bound[i];
        if (binding.observedIndex >= observed.size())
        {
            solve.refusal = TrackerSolveRefusal::AssignmentUnusable;
            solve.detail = "region " + RegionText(binding.region)
                           + " is bound to observed tracker "
                           + std::to_string(binding.observedIndex)
                           + ", and this observation carries "
                           + std::to_string(observed.size());
            return solve;
        }
        for (std::size_t j = 0; j < i; ++j)
        {
            if (assignment.bound[j].region == binding.region)
            {
                solve.refusal = TrackerSolveRefusal::AssignmentUnusable;
                solve.detail = "region " + RegionText(binding.region)
                               + " is bound twice";
                return solve;
            }
        }
    }

    // Which bones this assignment places, and which of them this frame can
    // orient. Both are needed before any region is classified, because whether
    // a head is placed depends on a hips two bindings later in the list.
    std::bitset<motion::HumanBoneCount> placedBones;
    std::bitset<motion::HumanBoneCount> withRotation;
    for (const TrackerAssignmentBinding& binding : assignment.bound)
    {
        const std::optional<motion::HumanBone> bone =
            TrackerRegionBone(binding.region);
        if (!bone)
        {
            continue;
        }
        const std::size_t index = static_cast<std::size_t>(*bone);
        placedBones.set(index);
        if (observed[binding.observedIndex].hasRotation)
        {
            withRotation.set(index);
        }
    }

    // Classify first, refuse afterwards, on the assignment layer's rule: a
    // report of a refused solve reads the same evidence a successful one does.
    for (const TrackerAssignmentBinding& binding : assignment.bound)
    {
        const TrackerObservation& observation = observed[binding.observedIndex];
        const std::optional<motion::HumanBone> bone =
            TrackerRegionBone(binding.region);

        if (!bone)
        {
            solve.unsolved.push_back(binding.region);
        }
        else if (!observation.hasRotation)
        {
            solve.withoutRotation.push_back(binding.region);
        }
        else if (AncestorFellSilent(*bone, placedBones, withRotation))
        {
            solve.withheldWithParent.push_back(binding.region);
        }
        else
        {
            solve.placed.push_back(binding.region);
        }

        const bool consumesPosition = bone
                                      && *bone == motion::HumanBone::Hips
                                      && config.authorRootMotion
                                      && observation.hasPosition;
        if (observation.hasPosition && !consumesPosition)
        {
            solve.positionsUnused.push_back(binding.region);
        }
    }

    // Only the values this solve consumes are checked, and the omission is
    // deliberate: a knee tracker reporting a NaN is a defect this solve cannot
    // see, and refusing on a number nothing reads would let an unused
    // observation stop a pose. Neither shape reaches here from a wire — every
    // adapter refuses a non-finite component at decode — so the caller that
    // built its observations by hand is the one this catches.
    for (const TrackerAssignmentBinding& binding : assignment.bound)
    {
        const TrackerObservation& observation = observed[binding.observedIndex];
        const std::optional<motion::HumanBone> bone =
            TrackerRegionBone(binding.region);
        if (!bone)
        {
            continue;
        }

        const bool consumesPosition = *bone == motion::HumanBone::Hips
                                      && config.authorRootMotion
                                      && observation.hasPosition;
        if (consumesPosition && !IsFinite(observation.position))
        {
            solve.refusal = TrackerSolveRefusal::ObservationInvalid;
            solve.detail = "tracker " + Quote(observation.tracker)
                           + " reports a position that is not finite";
            return solve;
        }
        if (!observation.hasRotation)
        {
            continue;
        }
        if (!IsFinite(observation.rotation))
        {
            solve.refusal = TrackerSolveRefusal::ObservationInvalid;
            solve.detail = "tracker " + Quote(observation.tracker)
                           + " reports a rotation that is not finite";
            return solve;
        }
        if (pxr::GfQuatd(observation.rotation).GetLength()
            < kShortestUsableRotation)
        {
            solve.refusal = TrackerSolveRefusal::ObservationInvalid;
            solve.detail = "tracker " + Quote(observation.tracker)
                           + " reports a rotation with no length to normalise";
            return solve;
        }
    }

    // Parent-first, which sorting by enumerator gives for free: every bone's
    // parent has a smaller value than it does, and `motionCore`'s own table
    // says so.
    std::vector<const TrackerAssignmentBinding*> ordered;
    ordered.reserve(assignment.bound.size());
    for (const TrackerAssignmentBinding& binding : assignment.bound)
    {
        ordered.push_back(&binding);
    }
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const TrackerAssignmentBinding* lhs,
                        const TrackerAssignmentBinding* rhs) {
                         const std::optional<motion::HumanBone> a =
                             TrackerRegionBone(lhs->region);
                         const std::optional<motion::HumanBone> b =
                             TrackerRegionBone(rhs->region);
                         return static_cast<std::size_t>(
                                    a.value_or(motion::HumanBone::Count))
                                < static_cast<std::size_t>(
                                    b.value_or(motion::HumanBone::Count));
                     });

    std::array<pxr::GfQuatd, motion::HumanBoneCount> authored;
    authored.fill(pxr::GfQuatd::GetIdentity());

    for (const TrackerAssignmentBinding* binding : ordered)
    {
        const TrackerObservation& observation = observed[binding->observedIndex];
        const std::optional<motion::HumanBone> bone =
            TrackerRegionBone(binding->region);
        if (!bone)
        {
            continue;
        }
        const std::size_t index = static_cast<std::size_t>(*bone);

        if (*bone == motion::HumanBone::Hips && config.authorRootMotion
            && observation.hasPosition)
        {
            // The root/hips rule, unchanged: a hips tracker is a body
            // translation observed at one place.
            solve.pose.root.worldPosition = observation.position;
            solve.pose.root.hasPosition = true;
        }
        if (!observation.hasRotation)
        {
            continue;
        }
        // Classified above, and skipped here for the reason stated there: the
        // parent a consumer will hold is not the identity this composition
        // would divide by. The hips can never reach this — nothing is above it
        // — so the root authored above is unaffected.
        if (AncestorFellSilent(*bone, placedBones, withRotation))
        {
            continue;
        }

        const pxr::GfQuatd world =
            pxr::GfQuatd(observation.rotation).GetNormalized();
        if (*bone == motion::HumanBone::Hips)
        {
            // And the other half of it: the same rotation is the body's
            // orientation and the hips' local rotation, because a rig rooted at
            // its hips has a root path of one joint.
            solve.pose.root.worldOrientation = pxr::GfQuatf(world);
            solve.pose.root.hasOrientation = true;
        }

        const pxr::GfQuatd parentWorld =
            ParentWorldRotation(*bone, authored, solve.pose.validRotations);
        const pxr::GfQuatd local = parentWorld.GetInverse() * world;

        authored[index] = local;
        solve.pose.localRotations[index] = pxr::GfQuatf(local);
        solve.pose.validRotations.set(index);
    }

    if (solve.pose.validRotations.none() && !solve.pose.root.hasPosition
        && !solve.pose.root.hasOrientation)
    {
        solve.refusal = TrackerSolveRefusal::NothingSolved;
        solve.detail = "none of the " + std::to_string(assignment.bound.size())
                       + " bound tracker(s) reached a bone or the root";
        return solve;
    }

    solve.refusal = TrackerSolveRefusal::None;
    return solve;
}

} // namespace motionTracking
