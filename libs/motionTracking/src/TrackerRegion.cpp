// SPDX-License-Identifier: Apache-2.0
#include "motionTracking/TrackerRegion.h"

#include <array>

namespace motionTracking
{

namespace
{

// Indexed by enumerator, and the static_assert below is what keeps that true:
// a region added to the enum without a name here is a compile error rather than
// an empty string somebody reads as "unset" three layers up.
constexpr std::array<std::string_view, TrackerRegionCount> kNames = {
    "head",
    "chest",
    "hips",
    "leftElbow",
    "leftHand",
    "leftKnee",
    "leftFoot",
    "rightElbow",
    "rightHand",
    "rightKnee",
    "rightFoot",
};

static_assert(kNames.size() == TrackerRegionCount,
              "every TrackerRegion needs the name an operator writes");

} // namespace

std::string_view
TrackerRegionName(TrackerRegion region) noexcept
{
    const auto index = static_cast<std::size_t>(region);
    if (index >= TrackerRegionCount)
    {
        return {};
    }
    return kNames[index];
}

std::optional<TrackerRegion>
ParseTrackerRegion(std::string_view name) noexcept
{
    // Linear over eleven entries, and exact. A map would be the same speed at
    // this size and would invite a case-insensitive comparator, which is the
    // first step of the name heuristic this vocabulary refuses to have.
    for (std::size_t i = 0; i < TrackerRegionCount; ++i)
    {
        if (kNames[i] == name)
        {
            return static_cast<TrackerRegion>(i);
        }
    }
    return std::nullopt;
}

} // namespace motionTracking
