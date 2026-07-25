// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "motionRuntime/api.h"

#include "motionCore/Humanoid.h"

namespace motion
{

// Resamples an animation onto a uniform timeline at `frameRate` Hz spanning
// [startTime, endTime]. The endpoints are always emitted, so a clip whose
// duration is not an exact multiple of the step still ends on its last sample.
//
// Returns an animation with no samples when the input has none or when
// `frameRate` is not positive; source metadata and the time range are carried
// through unchanged so provenance survives a resample.
MOTIONRUNTIME_API HumanoidAnimation Resample(
    const HumanoidAnimation& animation, double frameRate);

// Samples an animation at an arbitrary time using the same hold-at-the-edges
// rule as PoseBuffer::Sample. Returns a default-constructed pose when the
// animation has no samples.
MOTIONRUNTIME_API HumanoidPose SampleAnimation(
    const HumanoidAnimation& animation, double timestamp);

} // namespace motion
