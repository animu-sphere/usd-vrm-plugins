// SPDX-License-Identifier: Apache-2.0
#include "motionRuntime/PoseBuffer.h"

#include "motionRuntime/Interpolation.h"

#include <algorithm>

namespace motion
{

PoseBuffer::PoseBuffer(std::size_t capacity)
    : _capacity(capacity > 0 ? capacity : DefaultCapacity)
{
}

void
PoseBuffer::SetCapacity(std::size_t capacity)
{
    if (capacity == 0) {
        return;
    }
    _capacity = capacity;
    while (_samples.size() > _capacity) {
        _samples.pop_front();
    }
}

bool
PoseBuffer::Push(const HumanoidPose& pose)
{
    if (!_samples.empty() && pose.timestamp <= _samples.back().timestamp) {
        return false;
    }
    _samples.push_back(pose);
    while (_samples.size() > _capacity) {
        _samples.pop_front();
    }
    return true;
}

void
PoseBuffer::Clear() noexcept
{
    _samples.clear();
}

bool
PoseBuffer::GetTimeRange(double* startTime, double* endTime) const
{
    if (_samples.empty()) {
        return false;
    }
    if (startTime) {
        *startTime = _samples.front().timestamp;
    }
    if (endTime) {
        *endTime = _samples.back().timestamp;
    }
    return true;
}

std::optional<HumanoidPose>
PoseBuffer::Sample(double timestamp) const
{
    if (_samples.empty()) {
        return std::nullopt;
    }
    if (timestamp <= _samples.front().timestamp) {
        return _samples.front();
    }
    if (timestamp >= _samples.back().timestamp) {
        return _samples.back();
    }

    // Timestamps are strictly increasing (Push enforces it), so the first
    // sample at or after `timestamp` is the upper bracket.
    const auto upper = std::lower_bound(
        _samples.begin(), _samples.end(), timestamp,
        [](const HumanoidPose& sample, double time) {
            return sample.timestamp < time;
        });
    if (upper == _samples.begin()) {
        return _samples.front();
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

std::optional<HumanoidPose>
PoseBuffer::SampleExtrapolated(double timestamp, double maxLeadSeconds) const
{
    if (_samples.empty()) {
        return std::nullopt;
    }
    const HumanoidPose& newest = _samples.back();
    if (timestamp <= newest.timestamp) {
        return Sample(timestamp);
    }

    const double lead =
        std::min(timestamp - newest.timestamp, std::max(maxLeadSeconds, 0.0));
    HumanoidPose result = newest;
    result.timestamp = newest.timestamp + lead;
    if (lead <= 0.0) {
        return result;
    }

    // Prefer a reported velocity; otherwise derive one from the last two
    // samples. Either way only the root position advances.
    pxr::GfVec3f velocity(0.0f);
    bool hasVelocity = false;
    if (newest.root.hasLinearVelocity) {
        velocity = newest.root.linearVelocity;
        hasVelocity = true;
    } else if (_samples.size() >= 2 && newest.root.hasPosition) {
        const HumanoidPose& previous = _samples[_samples.size() - 2];
        const double delta = newest.timestamp - previous.timestamp;
        if (previous.root.hasPosition && delta > 0.0) {
            velocity = (newest.root.worldPosition - previous.root.worldPosition)
                / static_cast<float>(delta);
            hasVelocity = true;
        }
    }

    if (hasVelocity && newest.root.hasPosition) {
        result.root.worldPosition =
            newest.root.worldPosition + velocity * static_cast<float>(lead);
    }
    return result;
}

} // namespace motion
