// SPDX-License-Identifier: Apache-2.0
//
// Where on a body a tracker is mounted — and deliberately not which bone that
// is.
//
// This enum is the reason this library exists rather than a header in
// `motionCore`. It reads like a short `HumanBone` and it is not one, and the
// two places it stops being one are the two rigs everybody actually wears:
//
//   * a **knee** tracker sits on a strap between two bones. There is no knee
//     bone in `motionCore` and there should not be one — `LeftUpperLeg` and
//     `LeftLowerLeg` meet there, and which of them the device observes is a
//     question about the solve, not about the strap.
//   * a **chest** tracker observes a torso. `HumanBone::Chest` is a joint whose
//     transform a solve produces; the strap is above it, below `UpperChest`,
//     and moves with the ribcage rather than with either.
//
// So a region names a *mount point*, the vocabulary is this library's own, and
// [WORKSPACE.md §2](../../../../docs/architecture/WORKSPACE.md) forbids the
// alias by name: the day `TrackerRegion` becomes a `HumanBone` typedef,
// assignment has become a lookup and the solve has nothing left to do, which is
// the collapse
// [the OSC track §5.1](../../../../docs/roadmap/osc-and-vrchat-trackers.md#51-assignment-is-a-third-thing-and-it-belongs-to-neither-end)
// separates three decisions to prevent.
//
// **Eleven regions, and the list is short on purpose.** It covers the three
// rigs a tracker source can present — three-point (head and two hands),
// six-point (those plus hips and two feet), and full-body (those plus knees and
// elbows, or a chest) — and nothing beyond them. A region nobody has worn is a
// region whose solve nobody has written, and adding one is cheaper than
// removing one: the enumerators are addressed by name in an operator's
// statement, so a spelling that ships is a spelling that has to keep working.
//
// **A name is matched exactly, and a near miss is a refusal rather than a
// guess.** `ParseTrackerRegion("LeftFoot")` fails where `"leftFoot"` succeeds.
// That is the same rule `motion_bvh_convert` applies to a profile id — there is
// no default profile and no name heuristic — and it is worth more here than
// there: a tolerant parser that resolved `"lfoot"` would be the first line of
// the automatic assignment this milestone deliberately does not build.
#pragma once

#include "motionTracking/api.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace motionTracking
{

// A place on a body where a tracker can be mounted. Not a joint; see above.
//
// The enumerator order is head-down and then left-before-right, and it is not
// a contract: nothing here indexes by value, and an operator addresses these by
// name. `Count` is the end marker every enum in this repository carries.
enum class TrackerRegion : std::uint8_t
{
    Head,
    Chest,
    Hips,

    LeftElbow,
    LeftHand,
    LeftKnee,
    LeftFoot,

    RightElbow,
    RightHand,
    RightKnee,
    RightFoot,

    Count,
};

inline constexpr std::size_t TrackerRegionCount =
    static_cast<std::size_t>(TrackerRegion::Count);

// The name an operator writes, in the lowerCamelCase every declarative file in
// this repository uses. Empty for `Count` and for a value outside the enum,
// which is what makes a printed assignment show a hole rather than invent one.
MOTIONTRACKING_API std::string_view TrackerRegionName(
    TrackerRegion region) noexcept;

// The inverse, exact-match only. `nullopt` for anything this vocabulary does
// not carry, including a differently-cased spelling of something it does.
MOTIONTRACKING_API std::optional<TrackerRegion> ParseTrackerRegion(
    std::string_view name) noexcept;

} // namespace motionTracking
