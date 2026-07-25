// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "motionRuntime/api.h"

#include "motionCore/Humanoid.h"

#include <vector>

namespace motion
{

struct WeightedPose
{
    HumanoidPose pose;
    float weight = 1.0f;
};

// Two-pose blend. `weight` is clamped to [0, 1]: 0 yields `a`, 1 yields `b`.
// Bone validity follows LerpPose — a bone present in only one input is taken
// from that input rather than blended toward identity.
MOTIONRUNTIME_API HumanoidPose BlendPoses(
    const HumanoidPose& a, const HumanoidPose& b, float weight);

// N-pose blend by successive pairwise interpolation, which keeps every
// intermediate result a unit quaternion (a component-wise weighted sum does
// not). Negative weights are treated as zero; when the total weight is zero or
// the list is empty the result is a default-constructed pose.
MOTIONRUNTIME_API HumanoidPose BlendPoses(const std::vector<WeightedPose>& poses);

} // namespace motion
