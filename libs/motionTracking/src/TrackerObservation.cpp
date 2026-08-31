// SPDX-License-Identifier: Apache-2.0
#include "motionTracking/TrackerObservation.h"

namespace motionTracking
{

std::vector<std::string_view>
TrackerIdentities(const std::vector<TrackerObservation>& observed)
{
    std::vector<std::string_view> identities;
    identities.reserve(observed.size());
    for (const TrackerObservation& observation : observed)
    {
        identities.emplace_back(observation.tracker);
    }
    return identities;
}

} // namespace motionTracking
