// SPDX-License-Identifier: Apache-2.0
#include "motionRuntime/LiveCaptureSource.h"

#include <algorithm>
#include <array>
#include <utility>

namespace motion
{

namespace
{

bool
CarriesAnything(const HumanoidPose& pose)
{
    return pose.validRotations.any() || pose.root.hasPosition
        || pose.root.hasOrientation;
}

} // namespace

LiveCaptureSource::LiveCaptureSource(const LiveCaptureConfig& config)
    : _buffer(config.bufferCapacity)
{
    _metadata.kind = MotionSourceKind::LiveCapture;
    SetConfig(config);
}

void
LiveCaptureSource::SetConfig(const LiveCaptureConfig& config)
{
    _config = config;
    _buffer.SetCapacity(_config.bufferCapacity);

    PoseFilter::Options options;
    options.cutoffHz = _config.smoothingCutoffHz;
    _filter.SetOptions(options);
    _filter.Reset();
}

void
LiveCaptureSource::SetSourceMetadata(const MotionSourceMetadata& metadata)
{
    _metadata = metadata;
    // The kind is not the adapter's to choose: anything arriving through this
    // class is a live capture by construction (motion policy §9).
    _metadata.kind = MotionSourceKind::LiveCapture;
}

HumanoidPose
LiveCaptureSource::_Condition(const HumanoidPose& pose)
{
    HumanoidPose conditioned = pose;

    // 1. Confidence gate. A frame that reports no confidence at all is trusted
    //    as given -- an adapter that cannot measure confidence must not lose
    //    its bones for saying so.
    if (conditioned.confidence && _config.confidenceFloor > 0.0f) {
        const std::array<float, HumanBoneCount>& scores =
            *conditioned.confidence;
        for (std::size_t bone = 0; bone < HumanBoneCount; ++bone) {
            if (conditioned.validRotations.test(bone)
                && scores[bone] < _config.confidenceFloor) {
                conditioned.validRotations.reset(bone);
                ++_stats.bonesGatedByConfidence;
            }
        }
    }

    // 2. Whatever survived the gate is a real observation.
    for (std::size_t bone = 0; bone < HumanBoneCount; ++bone) {
        if (conditioned.validRotations.test(bone)) {
            _observedBones.set(bone);
            ++_stats.bonesObserved;
        }
    }

    // 3. Resolve what is still missing. Holding is per bone and comes from the
    //    last accepted frame, so a dropout freezes one limb rather than
    //    reverting it toward rest -- the same invariant PoseBuffer keeps for a
    //    missing sample.
    for (std::size_t bone = 0; bone < HumanBoneCount; ++bone) {
        if (conditioned.validRotations.test(bone)) {
            continue;
        }
        if (_config.missingBones == MissingBonePolicy::HoldLast
            && _lastAccepted && _lastAccepted->validRotations.test(bone)) {
            conditioned.localRotations[bone] = _lastAccepted->localRotations[bone];
            conditioned.validRotations.set(bone);
            ++_stats.bonesHeld;
        } else {
            ++_stats.bonesUnbound;
        }
    }

    // 4. Root motion.
    switch (_config.rootMotion) {
    case RootMotionIntake::Ignore:
        conditioned.root = RootMotion();
        break;
    case RootMotionIntake::Passthrough:
    case RootMotionIntake::DeriveVelocity:
        if (conditioned.root.hasPosition) {
            ++_stats.rootSamplesObserved;
        }
        if (_config.rootMotion == RootMotionIntake::DeriveVelocity
            && conditioned.root.hasPosition
            && !conditioned.root.hasLinearVelocity && _lastAccepted
            && _lastAccepted->root.hasPosition) {
            const double delta =
                conditioned.timestamp - _lastAccepted->timestamp;
            if (delta > 0.0) {
                conditioned.root.linearVelocity =
                    (conditioned.root.worldPosition
                     - _lastAccepted->root.worldPosition)
                    / static_cast<float>(delta);
                conditioned.root.hasLinearVelocity = true;
                ++_stats.rootVelocitiesDerived;
            }
        }
        break;
    }

    // 5. Provenance is stamped on every buffered pose, so a pose that outlives
    //    this object still says where it came from.
    conditioned.source = _metadata;

    // 6. Smoothing runs last, on the fully resolved frame, so a held bone is
    //    smoothed on the same terms as an observed one.
    if (_config.smoothingCutoffHz > 0.0f) {
        conditioned = _filter.Apply(conditioned);
    }
    return conditioned;
}

bool
LiveCaptureSource::Push(const HumanoidPose& pose)
{
    if (!CarriesAnything(pose)) {
        ++_stats.framesRejectedEmpty;
        return false;
    }

    if (_lastAccepted) {
        const double delta = pose.timestamp - _lastAccepted->timestamp;
        if (delta <= 0.0) {
            if (_config.staleFrameSeconds > 0.0
                && -delta > _config.staleFrameSeconds) {
                ++_stats.framesRejectedStale;
            } else {
                ++_stats.framesRejectedOutOfOrder;
            }
            return false;
        }
    }

    const HumanoidPose conditioned = _Condition(pose);
    if (!_buffer.Push(conditioned)) {
        // Unreachable while _lastAccepted tracks the buffer head, but the
        // buffer -- not this class -- owns the ordering rule, so defer to it.
        ++_stats.framesRejectedOutOfOrder;
        return false;
    }

    _lastAccepted = conditioned;
    ++_stats.framesAccepted;
    return true;
}

bool
LiveCaptureSource::AlignClock(double evaluationTime) noexcept
{
    double newest = 0.0;
    if (!_buffer.GetTimeRange(nullptr, &newest)) {
        return false;
    }
    _clockOffset = newest - evaluationTime;
    return true;
}

PoseSampleResult
LiveCaptureSource::Sample(double evaluationTime)
{
    PoseSampleResult result;

    double oldest = 0.0;
    double newest = 0.0;
    if (!_buffer.GetTimeRange(&oldest, &newest)) {
        ++_stats.samplesUnavailable;
        return result;
    }

    const double captureTime = evaluationTime + _clockOffset;
    result.lag = captureTime - newest;
    _stats.peakLagSeconds = std::max(_stats.peakLagSeconds, result.lag);

    std::optional<HumanoidPose> pose =
        _config.maxExtrapolationSeconds > 0.0
        ? _buffer.SampleExtrapolated(captureTime, _config.maxExtrapolationSeconds)
        : _buffer.Sample(captureTime);
    if (!pose) {
        ++_stats.samplesUnavailable;
        return result;
    }

    // The boundary carries PoseSampleTimeTolerance: a request that lands on an
    // observed sample to within the timeline's own precision *is* that sample,
    // and reporting it as extrapolated would make a clean session look like a
    // failing one.
    if (captureTime < oldest - PoseSampleTimeTolerance
        || captureTime > newest + PoseSampleTimeTolerance) {
        // Extrapolation only ever moves the root; when the newest frame had no
        // usable velocity nothing advanced and this is an ordinary hold. Report
        // what happened, not what was requested.
        const bool advanced = captureTime > newest + PoseSampleTimeTolerance
            && pose->root.hasPosition
            && pose->root.worldPosition != _buffer.GetNewest().root.worldPosition;
        result.status = advanced ? PoseSampleStatus::Extrapolated
                                 : PoseSampleStatus::Held;
        if (advanced) {
            ++_stats.samplesExtrapolated;
        } else {
            ++_stats.samplesHeld;
        }
    } else {
        result.status = PoseSampleStatus::Sampled;
        ++_stats.samplesServed;
    }

    // Report on the consumer's clock. SampleExtrapolated caps its own
    // timestamp at the lead limit, which would otherwise leak the buffer's
    // clock into a downstream timeline.
    pose->timestamp = evaluationTime;
    result.pose = std::move(pose);
    return result;
}

bool
LiveCaptureSource::GetTimeRange(double* startTime, double* endTime) const
{
    double oldest = 0.0;
    double newest = 0.0;
    if (!_buffer.GetTimeRange(&oldest, &newest)) {
        return false;
    }
    if (startTime) {
        *startTime = oldest - _clockOffset;
    }
    if (endTime) {
        *endTime = newest - _clockOffset;
    }
    return true;
}

void
LiveCaptureSource::Reset()
{
    _buffer.Clear();
    _filter.Reset();
    _lastAccepted.reset();
    _observedBones.reset();
}

} // namespace motion
