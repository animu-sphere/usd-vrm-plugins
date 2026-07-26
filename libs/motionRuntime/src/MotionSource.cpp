// SPDX-License-Identifier: Apache-2.0
#include "motionRuntime/MotionSource.h"

#include "motionRuntime/Resample.h"

namespace motion
{

IMotionSource::~IMotionSource() = default;

const char*
PoseSampleStatusName(PoseSampleStatus status) noexcept
{
    switch (status) {
    case PoseSampleStatus::Unavailable:
        return "unavailable";
    case PoseSampleStatus::Sampled:
        return "sampled";
    case PoseSampleStatus::Held:
        return "held";
    case PoseSampleStatus::Extrapolated:
        return "extrapolated";
    }
    return "unavailable";
}

ClipSource::ClipSource(HumanoidAnimation animation)
    : _animation(std::move(animation))
{
}

void
ClipSource::SetAnimation(HumanoidAnimation animation)
{
    _animation = std::move(animation);
}

PoseSampleResult
ClipSource::Sample(double evaluationTime)
{
    PoseSampleResult result;
    if (_animation.samples.empty()) {
        return result;
    }

    const double clipTime = evaluationTime - _startOffset;
    const double first = _animation.samples.front().timestamp;
    const double last = _animation.samples.back().timestamp;

    result.pose = SampleAnimation(_animation, clipTime);
    result.status = (clipTime < first - PoseSampleTimeTolerance
                     || clipTime > last + PoseSampleTimeTolerance)
        ? PoseSampleStatus::Held
        : PoseSampleStatus::Sampled;
    result.lag = clipTime - last;

    // The pose is reported on the consumer's clock, not the clip's, so a
    // caller that pushes it into a buffer keeps one coherent timeline.
    result.pose->timestamp = evaluationTime;
    return result;
}

MotionSourceMetadata
ClipSource::GetSourceMetadata() const
{
    return _animation.source;
}

bool
ClipSource::GetTimeRange(double* startTime, double* endTime) const
{
    if (_animation.samples.empty()) {
        return false;
    }
    if (startTime) {
        *startTime = _animation.samples.front().timestamp + _startOffset;
    }
    if (endTime) {
        *endTime = _animation.samples.back().timestamp + _startOffset;
    }
    return true;
}

} // namespace motion
