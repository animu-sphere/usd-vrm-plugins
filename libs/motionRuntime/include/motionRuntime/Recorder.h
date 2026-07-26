// SPDX-License-Identifier: Apache-2.0
//
// Turns a stream back into a clip (motion policy §5: "a recorder accumulates
// [HumanoidPose] into a HumanoidAnimation when needed").
//
// This is the join between the live half of the motion layer and the offline
// half. A live source answers one evaluation time at a time; the retarget core,
// the `.vrma` writer, and every bake want a finished `HumanoidAnimation`. The
// recorder is what makes the two the same pipeline rather than two pipelines --
// and because it records the *status* of every tick alongside the pose, a clip
// baked from a laggy session carries the evidence of that lag instead of
// quietly looking fine.
#pragma once

#include "motionRuntime/MotionSource.h"
#include "motionRuntime/api.h"

#include "motionCore/Humanoid.h"

#include <cstddef>

namespace motion
{

struct RecordReport
{
    std::size_t ticks = 0;
    std::size_t sampled = 0;
    std::size_t held = 0;
    std::size_t extrapolated = 0;
    std::size_t unavailable = 0;
    std::size_t rejected = 0;
    double peakLagSeconds = 0.0;

    // Ticks that produced a pose from real bracketing data. A session whose
    // `sampled` count is far below `ticks` was evaluated ahead of its data.
    std::size_t Recorded() const noexcept
    {
        return sampled + held + extrapolated;
    }
};

class MOTIONRUNTIME_API CaptureRecorder
{
public:
    explicit CaptureRecorder(double frameRate = 30.0);

    // Appends the pose the result carries. A tick the source could not answer
    // is counted and dropped, not padded with an invented pose. Returns false
    // when nothing was appended -- either the result was empty, or its
    // timestamp did not advance.
    bool Record(const PoseSampleResult& result);

    const RecordReport& GetReport() const noexcept { return _report; }
    std::size_t GetFrameCount() const noexcept
    {
        return _animation.samples.size();
    }

    // Stamps the time range, the nominal rate, and the provenance of the first
    // recorded pose onto the clip, and hands it over. The recorder is empty
    // afterwards.
    HumanoidAnimation Take();

    void Clear();

private:
    HumanoidAnimation _animation;
    RecordReport _report;
    double _frameRate;
};

} // namespace motion
