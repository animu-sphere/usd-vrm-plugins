// SPDX-License-Identifier: Apache-2.0
//
// The vendor-neutral live-capture intake (motion policy §6.1, §8.2).
//
// A capture system's pipeline is
//
//     capture device -> protocol decode -> coordinate conversion
//                    -> LiveCaptureSource -> HumanoidPose
//
// and everything left of this class is an adapter's job. This class therefore
// owns no transport: it neither opens a socket nor reads a clock. An adapter
// decodes a frame, converts it into the canonical humanoid basis, and calls
// Push(). That boundary is what makes a live source replayable — the same
// recorded frames pushed in the same order produce byte-identical results,
// which is what the Phase D tests depend on.
//
// Product names (a capture vendor, an SDK, a research model) are forbidden
// here and appear only under adapters/ (WORKSPACE.md §1, motion policy §8.1).
#pragma once

#include "motionRuntime/Filter.h"
#include "motionRuntime/MotionSource.h"
#include "motionRuntime/PoseBuffer.h"
#include "motionRuntime/api.h"

#include "motionCore/Humanoid.h"

#include <bitset>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace motion
{

// What to do with a bone a frame does not carry — either because the capture
// system never solves it (fingers are the usual case) or because it dropped
// below the confidence floor for this frame.
enum class MissingBonePolicy : std::uint8_t
{
    // Leave the bone unbound. Downstream sees `validRotations` clear and is
    // free to fall back to the target rig's rest pose.
    LeaveUnbound,
    // Carry the last observed rotation forward. A brief dropout then reads as
    // a frozen limb rather than a limb snapping to rest and back.
    HoldLast,
};

// Live systems disagree about what they report for the root: some send an
// absolute world position, some a position and a velocity, some nothing at all.
enum class RootMotionIntake : std::uint8_t
{
    // Record exactly what the adapter delivered.
    Passthrough,
    // Drop root motion entirely; only the joint hierarchy survives. Use when
    // the avatar's placement is owned by the application, not the capture.
    Ignore,
    // Passthrough, plus: when a frame carries a position but no linear
    // velocity, derive the velocity from the previous frame. This is what
    // makes PoseBuffer::SampleExtrapolated able to hide a late frame, so it is
    // the default.
    DeriveVelocity,
};

struct LiveCaptureConfig
{
    std::size_t bufferCapacity = PoseBuffer::DefaultCapacity;

    // Bones whose reported confidence is below this floor are treated as
    // missing and resolved by `missingBones`. Frames with no confidence array
    // are never gated — an adapter that cannot measure confidence must not be
    // punished for saying so.
    float confidenceFloor = 0.0f;

    MissingBonePolicy missingBones = MissingBonePolicy::HoldLast;
    RootMotionIntake rootMotion = RootMotionIntake::DeriveVelocity;

    // Past the newest observed frame, carry the root along its last velocity
    // for at most this long. Zero disables extrapolation and holds instead.
    double maxExtrapolationSeconds = 0.1;

    // A positive cutoff smooths every accepted frame before it is buffered,
    // frame-rate independently (see PoseFilter). Zero disables smoothing.
    float smoothingCutoffHz = 0.0f;

    // Splits the two reasons a frame can arrive non-increasing. A frame more
    // than this far behind the newest accepted one is a straggler and is
    // counted as stale; anything closer is counted as out-of-order, which
    // means the source's clock is jittering and the caller has a fault to
    // investigate (PoseBuffer.h). Both are refused either way; zero classifies
    // every non-increasing frame as out-of-order.
    double staleFrameSeconds = 0.0;
};

// Everything an operator needs to judge a capture session without a debugger.
// These are the numbers the Phase D evaluation reports: how much arrived, how
// much was refused and why, how much of the humanoid was actually observed,
// and how often evaluation ran ahead of the data.
struct LiveCaptureStats
{
    std::uint64_t framesAccepted = 0;
    std::uint64_t framesRejectedOutOfOrder = 0;
    std::uint64_t framesRejectedStale = 0;
    std::uint64_t framesRejectedEmpty = 0;

    std::uint64_t bonesObserved = 0;
    std::uint64_t bonesGatedByConfidence = 0;
    std::uint64_t bonesHeld = 0;
    std::uint64_t bonesUnbound = 0;

    std::uint64_t rootSamplesObserved = 0;
    std::uint64_t rootVelocitiesDerived = 0;

    std::uint64_t samplesServed = 0;
    std::uint64_t samplesHeld = 0;
    std::uint64_t samplesExtrapolated = 0;
    std::uint64_t samplesUnavailable = 0;

    // The largest positive lag any Sample() call saw, in seconds.
    double peakLagSeconds = 0.0;
};

class MOTIONRUNTIME_API LiveCaptureSource final : public IMotionSource
{
public:
    explicit LiveCaptureSource(const LiveCaptureConfig& config = {});

    const LiveCaptureConfig& GetConfig() const noexcept { return _config; }
    // Applying a config re-seats the buffer capacity and the filter. Buffered
    // history survives; the smoothing state does not, because a cutoff change
    // mid-stream would otherwise blend two different filters.
    void SetConfig(const LiveCaptureConfig& config);

    void SetSourceMetadata(const MotionSourceMetadata& metadata);
    MotionSourceMetadata GetSourceMetadata() const override
    {
        return _metadata;
    }

    // Accepts one decoded frame, stamped in the capture system's own clock.
    // Returns false when the frame was refused — out of order, stale, or
    // carrying neither a valid bone nor root data — and the stats record why.
    bool Push(const HumanoidPose& pose);

    // captureTime = evaluationTime + clockOffset. An adapter that knows its
    // stream's epoch sets this directly; one that does not calls AlignClock().
    void SetClockOffset(double clockOffset) noexcept
    {
        _clockOffset = clockOffset;
    }
    double GetClockOffset() const noexcept { return _clockOffset; }

    // Pins the newest buffered frame to `evaluationTime`, so that evaluation
    // consumes the stream from its current head. A deliberate playback delay is
    // the same operation with an earlier `evaluationTime`. No-op on an empty
    // buffer; returns false in that case.
    bool AlignClock(double evaluationTime) noexcept;

    PoseSampleResult Sample(double evaluationTime) override;
    bool GetTimeRange(double* startTime, double* endTime) const override;

    const PoseBuffer& GetBuffer() const noexcept { return _buffer; }
    bool IsEmpty() const noexcept { return _buffer.IsEmpty(); }

    // Bones this session has ever observed at or above the confidence floor.
    // A capture rig that solves no fingers reports it here rather than in a
    // per-frame diff.
    const std::bitset<HumanBoneCount>& GetObservedBones() const noexcept
    {
        return _observedBones;
    }

    const LiveCaptureStats& GetStats() const noexcept { return _stats; }
    void ResetStats() noexcept { _stats = LiveCaptureStats(); }

    // Drops buffered history, the held-bone state, and the smoothing state.
    // Stats and the clock offset survive; use ResetStats() for those.
    void Reset();

private:
    HumanoidPose _Condition(const HumanoidPose& pose);

    LiveCaptureConfig _config;
    PoseBuffer _buffer;
    PoseFilter _filter;
    MotionSourceMetadata _metadata;

    std::optional<HumanoidPose> _lastAccepted;
    std::bitset<HumanBoneCount> _observedBones;
    double _clockOffset = 0.0;
    LiveCaptureStats _stats;
};

} // namespace motion
