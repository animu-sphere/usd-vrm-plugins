// SPDX-License-Identifier: Apache-2.0
#include "motionRuntime/Filter.h"

#include "motionRuntime/Interpolation.h"

#include <cmath>

namespace motion
{
namespace
{

constexpr float kTwoPi = 6.2831853071795862f;

// Exponential smoothing weight for a step of `dt` seconds at `cutoffHz`. The
// time constant form keeps the response identical regardless of sample rate.
float
SmoothingAlpha(float cutoffHz, double dt)
{
    if (cutoffHz <= 0.0f || dt <= 0.0) {
        return 1.0f;
    }
    const float exponent = -kTwoPi * cutoffHz * static_cast<float>(dt);
    return 1.0f - std::exp(exponent);
}

} // namespace

HumanoidPose
PoseFilter::Apply(const HumanoidPose& pose)
{
    if (_options.cutoffHz <= 0.0f || !_state) {
        _state = pose;
        return pose;
    }

    const double dt = pose.timestamp - _state->timestamp;
    if (dt <= 0.0) {
        _state = pose;
        return pose;
    }

    const float alpha = SmoothingAlpha(_options.cutoffHz, dt);
    HumanoidPose result = pose;

    for (std::size_t i = 0; i < HumanBoneCount; ++i) {
        if (!pose.validRotations.test(i)) {
            // A dropout leaves both the output bone and the retained state
            // alone, so the filter resumes from the last real sample instead of
            // restarting when the bone comes back.
            continue;
        }
        if (!_state->validRotations.test(i)) {
            continue;
        }
        result.localRotations[i] =
            SlerpShortest(_state->localRotations[i], pose.localRotations[i],
                          alpha);
    }

    if (_options.filterRootPosition && pose.root.hasPosition
        && _state->root.hasPosition) {
        result.root.worldPosition = _state->root.worldPosition
            + (pose.root.worldPosition - _state->root.worldPosition) * alpha;
    }
    if (_options.filterRootOrientation && pose.root.hasOrientation
        && _state->root.hasOrientation) {
        result.root.worldOrientation = SlerpShortest(
            _state->root.worldOrientation, pose.root.worldOrientation, alpha);
    }

    // Carry forward the bones this pose did not report so their history
    // survives the dropout.
    HumanoidPose nextState = result;
    for (std::size_t i = 0; i < HumanBoneCount; ++i) {
        if (!pose.validRotations.test(i) && _state->validRotations.test(i)) {
            nextState.localRotations[i] = _state->localRotations[i];
            nextState.validRotations.set(i);
        }
    }
    _state = std::move(nextState);
    return result;
}

} // namespace motion
