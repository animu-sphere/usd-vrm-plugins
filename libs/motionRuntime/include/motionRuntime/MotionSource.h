// SPDX-License-Identifier: Apache-2.0
//
// The observation/playback interface every motion producer resolves to
// (motion policy §6.1). A clip, a live capture, and a generated animation all
// answer the same question — "what is the pose at this evaluation time?" — so
// from retarget onward nothing downstream distinguishes them.
//
// Provenance travels as metadata (motion policy §9), never as a type
// distinction: `MotionSourceMetadata::provider` is recorded and reported, and
// it is never a branch condition here.
#pragma once

#include "motionRuntime/api.h"

#include "motionCore/Humanoid.h"

#include <cstdint>
#include <optional>

namespace motion
{

// How a sample was resolved. The caller needs this to tell a real observation
// from a boundary hold: a live source that has stopped delivering keeps
// answering `Held` forever, which is the signal that the capture is stale.
enum class PoseSampleStatus : std::uint8_t
{
    // The source holds nothing that can answer the request.
    Unavailable,
    // Interpolated between, or landing exactly on, two observed samples.
    Sampled,
    // Outside the observed range; the nearest boundary pose is held unchanged.
    Held,
    // Past the newest observed sample; the root translation was carried
    // forward along its last observed velocity. Rotations are still held.
    Extrapolated,
};

MOTIONRUNTIME_API const char* PoseSampleStatusName(
    PoseSampleStatus status) noexcept;

// How close to an observed sample a request must be to count as landing on it.
//
// Timestamps are seconds in a double, but the values in them are quantised by
// whatever produced them -- a recorded trace stores six decimals, so 1/30 s
// reads back as 0.033333 and 2/30 s as 0.066667, one just below the exact
// instant and one just above. A consumer ticking at that same rate computes
// `k / rate` exactly, so without a tolerance half its requests land a fraction
// of a microsecond outside the observed range and are reported `Held` or
// `Extrapolated` when the data for them is right there.
//
// The correction those requests receive is numerically irrelevant (a root
// moving at 1 m/s advances 0.3 µm in 0.3 µs). The *status* is not: it is the
// signal a caller uses to tell a live source from a stopped one, and a clean
// session must not report itself as running on extrapolation. Half a
// microsecond is orders of magnitude below any real capture cadence, so this
// cannot mask a delay anyone would care about.
inline constexpr double PoseSampleTimeTolerance = 5e-7;

struct PoseSampleResult
{
    PoseSampleStatus status = PoseSampleStatus::Unavailable;
    std::optional<HumanoidPose> pose;

    // Seconds between the request and the newest sample the source holds, in
    // the source's own clock. Positive means the source is behind the request
    // — the ordinary live case, and the number a latency budget is checked
    // against. Zero when the source holds nothing.
    double lag = 0.0;

    bool IsValid() const noexcept { return pose.has_value(); }
    explicit operator bool() const noexcept { return IsValid(); }
};

class MOTIONRUNTIME_API IMotionSource
{
public:
    virtual ~IMotionSource();

    IMotionSource(const IMotionSource&) = delete;
    IMotionSource& operator=(const IMotionSource&) = delete;

    // `evaluationTime` is in the consumer's clock. A source that keeps its own
    // clock (a live capture) is responsible for the mapping.
    virtual PoseSampleResult Sample(double evaluationTime) = 0;

    virtual MotionSourceMetadata GetSourceMetadata() const = 0;

    // False when the source holds nothing; otherwise writes the oldest and
    // newest times it can answer for, in the consumer's clock. Either pointer
    // may be null.
    virtual bool GetTimeRange(double* startTime, double* endTime) const = 0;

protected:
    IMotionSource() = default;
};

// A finished `HumanoidAnimation` served through the same interface. This is
// what a `.vrma` clip, a recorded trace, and a generated animation all become
// once they are complete (motion policy §6.2) — the retarget core cannot tell
// them apart, which is the point.
class MOTIONRUNTIME_API ClipSource final : public IMotionSource
{
public:
    ClipSource() = default;
    explicit ClipSource(HumanoidAnimation animation);

    void SetAnimation(HumanoidAnimation animation);
    const HumanoidAnimation& GetAnimation() const noexcept
    {
        return _animation;
    }

    // Shifts the clip along the consumer's clock: an evaluation time `t`
    // resolves the clip at `t - startOffset`. Lets a clip and a live source be
    // driven from one timeline without rewriting either one's timestamps.
    void SetStartOffset(double startOffset) noexcept
    {
        _startOffset = startOffset;
    }
    double GetStartOffset() const noexcept { return _startOffset; }

    PoseSampleResult Sample(double evaluationTime) override;
    MotionSourceMetadata GetSourceMetadata() const override;
    bool GetTimeRange(double* startTime, double* endTime) const override;

private:
    HumanoidAnimation _animation;
    double _startOffset = 0.0;
};

} // namespace motion
