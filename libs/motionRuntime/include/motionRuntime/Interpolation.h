// SPDX-License-Identifier: Apache-2.0
//
// Pose interpolation. Like motionCore this is a value-only contract: no USD
// stage, plug, file-format, network, or vendor SDK API is allowed here.
#pragma once

#include "motionRuntime/api.h"

#include "motionCore/Humanoid.h"

#include "pxr/base/gf/quatf.h"
#include "pxr/base/gf/vec3f.h"

namespace motion
{

// Shortest-arc interpolation between two unit quaternions. `t` is clamped to
// [0, 1]; near-antipodal inputs take the short path, so a clip authored with a
// sign-flipped quaternion does not spin the long way round.
MOTIONRUNTIME_API pxr::GfQuatf SlerpShortest(
    const pxr::GfQuatf& a, const pxr::GfQuatf& b, float t);

// Component-wise interpolation. A presence flag survives only where both
// endpoints carry the component; where exactly one does, that endpoint's value
// is held rather than faded toward zero, because a missing sample is not a
// zero-valued sample (motion contract, `motionCore` value contract).
MOTIONRUNTIME_API RootMotion LerpRootMotion(
    const RootMotion& a, const RootMotion& b, float t);

// Interpolates two poses bone by bone. A bone valid in both endpoints is
// slerped; a bone valid in exactly one is copied from that endpoint; a bone
// valid in neither stays absent. The result's timestamp is interpolated.
//
// Optional channels (confidence, contacts, source) follow the same
// hold-not-fade rule: confidence is interpolated only where both endpoints
// carry it, and contacts/source are taken from the nearer endpoint because
// they are discrete.
MOTIONRUNTIME_API HumanoidPose LerpPose(
    const HumanoidPose& a, const HumanoidPose& b, float t);

} // namespace motion
