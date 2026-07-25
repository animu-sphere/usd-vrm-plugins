// SPDX-License-Identifier: Apache-2.0
#include "motionRuntime/Resample.h"

#include "motionRuntime/Interpolation.h"

#include <algorithm>
#include <cmath>

namespace motion
{

HumanoidPose
SampleAnimation(const HumanoidAnimation& animation, double timestamp)
{
    const std::vector<HumanoidPose>& samples = animation.samples;
    if (samples.empty()) {
        return HumanoidPose();
    }
    if (timestamp <= samples.front().timestamp) {
        return samples.front();
    }
    if (timestamp >= samples.back().timestamp) {
        return samples.back();
    }

    const auto upper = std::lower_bound(
        samples.begin(), samples.end(), timestamp,
        [](const HumanoidPose& sample, double time) {
            return sample.timestamp < time;
        });
    if (upper == samples.begin() || upper == samples.end()) {
        return upper == samples.begin() ? samples.front() : samples.back();
    }
    const auto lower = std::prev(upper);

    const double span = upper->timestamp - lower->timestamp;
    if (span <= 0.0) {
        return *lower;
    }
    const float alpha =
        static_cast<float>((timestamp - lower->timestamp) / span);
    return LerpPose(*lower, *upper, alpha);
}

HumanoidAnimation
Resample(const HumanoidAnimation& animation, double frameRate)
{
    HumanoidAnimation result;
    result.startTime = animation.startTime;
    result.endTime = animation.endTime;
    result.nominalFrameRate = animation.nominalFrameRate;
    result.source = animation.source;

    if (animation.samples.empty() || frameRate <= 0.0) {
        return result;
    }

    result.nominalFrameRate = frameRate;
    double start = animation.startTime;
    double end = animation.endTime;
    if (end <= start) {
        // A caller that filled `samples` but left the declared interval at its
        // default would otherwise collapse the whole clip to a single pose. The
        // samples are the authority on what the clip actually spans.
        start = animation.samples.front().timestamp;
        end = animation.samples.back().timestamp;
        result.startTime = start;
        result.endTime = end;
    }
    if (end <= start) {
        result.samples.push_back(SampleAnimation(animation, start));
        result.samples.back().timestamp = start;
        return result;
    }

    const double step = 1.0 / frameRate;
    const auto steps =
        static_cast<std::size_t>(std::floor((end - start) / step + 1e-9));
    result.samples.reserve(steps + 2);
    for (std::size_t i = 0; i <= steps; ++i) {
        const double time = start + static_cast<double>(i) * step;
        HumanoidPose pose = SampleAnimation(animation, time);
        pose.timestamp = time;
        result.samples.push_back(std::move(pose));
    }
    // The last uniform step lands short of `end` whenever the duration is not
    // an exact multiple of the step. Emit the real end so the resampled clip
    // covers the same interval as its source.
    if (result.samples.back().timestamp < end) {
        HumanoidPose pose = SampleAnimation(animation, end);
        pose.timestamp = end;
        result.samples.push_back(std::move(pose));
    }
    return result;
}

} // namespace motion
