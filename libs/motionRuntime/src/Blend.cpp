// SPDX-License-Identifier: Apache-2.0
#include "motionRuntime/Blend.h"

#include "motionRuntime/Interpolation.h"

#include <algorithm>

namespace motion
{

HumanoidPose
BlendPoses(const HumanoidPose& a, const HumanoidPose& b, float weight)
{
    return LerpPose(a, b, weight);
}

HumanoidPose
BlendPoses(const std::vector<WeightedPose>& poses)
{
    HumanoidPose result;
    double accumulated = 0.0;
    bool seeded = false;

    for (const WeightedPose& entry : poses) {
        const double weight = std::max(entry.weight, 0.0f);
        if (weight <= 0.0) {
            continue;
        }
        if (!seeded) {
            result = entry.pose;
            accumulated = weight;
            seeded = true;
            continue;
        }
        // Fold each pose in at its share of the running total. Successive
        // pairwise slerps keep every intermediate a unit quaternion, which a
        // component-wise weighted sum would not.
        accumulated += weight;
        result = LerpPose(result, entry.pose,
                          static_cast<float>(weight / accumulated));
    }

    return result;
}

} // namespace motion
