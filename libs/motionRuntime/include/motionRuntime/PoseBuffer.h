// SPDX-License-Identifier: Apache-2.0
//
// A bounded, timestamped pose history. This is the OpenExec-independent
// runtime's memory: live capture pushes into it, evaluation samples out of it,
// and neither side knows about the other.
#pragma once

#include "motionRuntime/api.h"

#include "motionCore/Humanoid.h"

#include <cstddef>
#include <deque>
#include <optional>

namespace motion
{

class MOTIONRUNTIME_API PoseBuffer
{
public:
    // 120 samples is four seconds at 30 Hz — enough history for smoothing and
    // late-arriving samples without unbounded growth under a live source.
    static constexpr std::size_t DefaultCapacity = 120;

    explicit PoseBuffer(std::size_t capacity = DefaultCapacity);

    // Dropping the capacity evicts the oldest samples immediately. A capacity
    // of zero is rejected and leaves the buffer unchanged.
    void SetCapacity(std::size_t capacity);
    std::size_t GetCapacity() const noexcept { return _capacity; }

    // Samples must arrive in strictly increasing timestamp order. An
    // out-of-order or duplicate timestamp is refused (returns false) rather
    // than silently reordering the history — a live source that jitters its
    // clock is a fault the caller must see.
    bool Push(const HumanoidPose& pose);

    void Clear() noexcept;
    bool IsEmpty() const noexcept { return _samples.empty(); }
    std::size_t GetSize() const noexcept { return _samples.size(); }

    // False when the buffer is empty; otherwise writes the oldest and newest
    // timestamps. Either pointer may be null.
    bool GetTimeRange(double* startTime, double* endTime) const;

    // Preconditions: !IsEmpty().
    const HumanoidPose& GetOldest() const { return _samples.front(); }
    const HumanoidPose& GetNewest() const { return _samples.back(); }

    // Interpolates between the two bracketing samples. Outside the buffered
    // range the boundary pose is held, never extrapolated. Returns nullopt only
    // when the buffer is empty.
    std::optional<HumanoidPose> Sample(double timestamp) const;

    // As Sample, but past the newest sample the root translation continues
    // along the last observed linear velocity for at most `maxLeadSeconds`.
    // Rotations are always held: extrapolating orientation from two samples
    // amplifies capture jitter far more than it hides latency.
    std::optional<HumanoidPose> SampleExtrapolated(
        double timestamp, double maxLeadSeconds = 0.1) const;

private:
    std::deque<HumanoidPose> _samples;
    std::size_t _capacity;
};

} // namespace motion
