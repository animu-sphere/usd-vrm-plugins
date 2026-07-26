// SPDX-License-Identifier: Apache-2.0
#include "motionRuntime/Recorder.h"

#include <algorithm>
#include <utility>

namespace motion
{

CaptureRecorder::CaptureRecorder(double frameRate)
    : _frameRate(frameRate > 0.0 ? frameRate : 30.0)
{
}

bool
CaptureRecorder::Record(const PoseSampleResult& result)
{
    ++_report.ticks;
    _report.peakLagSeconds = std::max(_report.peakLagSeconds, result.lag);

    switch (result.status) {
    case PoseSampleStatus::Sampled:
        ++_report.sampled;
        break;
    case PoseSampleStatus::Held:
        ++_report.held;
        break;
    case PoseSampleStatus::Extrapolated:
        ++_report.extrapolated;
        break;
    case PoseSampleStatus::Unavailable:
        ++_report.unavailable;
        break;
    }

    if (!result.pose) {
        return false;
    }
    if (!_animation.samples.empty()
        && result.pose->timestamp <= _animation.samples.back().timestamp) {
        ++_report.rejected;
        return false;
    }

    _animation.samples.push_back(*result.pose);
    return true;
}

HumanoidAnimation
CaptureRecorder::Take()
{
    HumanoidAnimation clip = std::move(_animation);
    _animation = HumanoidAnimation();

    clip.nominalFrameRate = _frameRate;
    if (!clip.samples.empty()) {
        clip.startTime = clip.samples.front().timestamp;
        clip.endTime = clip.samples.back().timestamp;
        if (clip.samples.front().source) {
            clip.source = *clip.samples.front().source;
        }
    }
    return clip;
}

void
CaptureRecorder::Clear()
{
    _animation = HumanoidAnimation();
    _report = RecordReport();
}

} // namespace motion
