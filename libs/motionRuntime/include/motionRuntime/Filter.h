// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "motionRuntime/api.h"

#include "motionCore/Humanoid.h"

#include <optional>

namespace motion
{

// Frame-rate independent exponential smoothing.
//
// The cutoff is expressed in hertz rather than as a blend weight so the same
// options behave identically on a 30 Hz clip and a 90 Hz live source: the
// per-step weight is derived from the actual elapsed time between poses.
class MOTIONRUNTIME_API PoseFilter
{
public:
    struct Options
    {
        // Higher cutoff = more responsive, less smoothing. A non-positive
        // cutoff disables filtering and passes poses through unchanged.
        float cutoffHz = 6.0f;
        bool filterRootPosition = true;
        bool filterRootOrientation = true;
    };

    PoseFilter() = default;
    explicit PoseFilter(const Options& options)
        : _options(options)
    {
    }

    const Options& GetOptions() const noexcept { return _options; }
    void SetOptions(const Options& options) { _options = options; }

    // Forgets the accumulated state; the next pose passes through untouched and
    // becomes the new seed. Call this on a source switch or a seek.
    void Reset() noexcept { _state.reset(); }
    bool HasState() const noexcept { return _state.has_value(); }

    // Smooths `pose` against the accumulated state and returns the result. A
    // bone absent from `pose` is not invented from history: it stays absent,
    // and its stored state is left untouched so a brief dropout does not
    // restart the filter for that bone.
    //
    // A non-increasing timestamp yields no smoothing step (the pose is
    // returned unchanged and reseeds the state).
    HumanoidPose Apply(const HumanoidPose& pose);

private:
    Options _options;
    std::optional<HumanoidPose> _state;
};

} // namespace motion
