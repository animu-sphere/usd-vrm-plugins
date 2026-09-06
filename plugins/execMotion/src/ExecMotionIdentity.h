// SPDX-License-Identifier: Apache-2.0
//
// The identity pose, over the joints a `UsdSkelAnimation` names.
//
// This is the bundle's project-owned seam and it takes **plain values**: a list
// of joint paths and a time, never a `VdfContext` and never a stage. Marshalling
// exec's inputs into these arguments is the registration TU's job (motion policy
// §11.4: a computation is a thin wrapper over a library that does not know exec
// exists). Keeping the seam free of exec means it is testable with no stage, no
// system, and no request -- and that a failure in the mechanism cannot be
// mistaken for a failure in the value.
#pragma once

#include <motionCore/Humanoid.h>

#include <string>
#include <vector>

namespace execmotion {

/// The identity pose for `jointPaths`.
///
/// Every rotation is identity and `validRotations` carries exactly the bones the
/// joint paths name -- a joint path being `UsdSkelAnimation`'s own spelling,
/// e.g. `hips/spine/chest`, of which only the last segment is a bone name
/// (`motion::HumanBoneJointPath` authors the same shape). A path whose leaf
/// names no canonical bone contributes nothing and is not an error here: this
/// function reports what it recognized, and whoever knows which clip it is
/// decides whether a gap matters.
///
/// The pose's `timestamp` is left at zero, and the caller does not get to pass
/// one. `HumanoidPose::timestamp` is **seconds**, an OpenExec computation is
/// handed a **frame**, and the rate between them is stage metadata a computation
/// cannot reach -- so the only timestamp this layer could produce would be a
/// guess (see ExecMotionRegistration.cpp). A pose with no time is a pose that
/// says nothing about time; a pose carrying a frame in a seconds field is a
/// wrong number every consumer downstream would believe.
///
/// No sampling, no interpolation, no retarget. This is the identity, and it is
/// what makes the first OpenExec computation attributable: a wrong result is a
/// wrong mechanism, because there is no algorithm to blame.
motion::HumanoidPose IdentityPoseForJoints(
    const std::vector<std::string>& jointPaths);

/// The bone a `UsdSkelAnimation` joint path names, or nullopt.
///
/// Exposed for the tests, which check the leaf-segment rule directly rather than
/// through a pose.
std::optional<motion::HumanBone> BoneForJointPath(const std::string& jointPath);

} // namespace execmotion
