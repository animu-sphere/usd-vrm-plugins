// SPDX-License-Identifier: Apache-2.0
//
// The poses this bundle's computations produce, over plain values.
//
// This is the bundle's project-owned seam and it takes **plain values**: joint
// paths, rotation and translation arrays, a frame and a rate -- never a
// `VdfContext` and never a stage. Marshalling exec's inputs into these arguments
// is the registration TU's job (motion policy §11.4: a computation is a thin
// wrapper over a library that does not know exec exists). Keeping the seam free
// of exec means it is testable with no stage, no system, and no request -- and
// that a failure in the mechanism cannot be mistaken for a failure in the value.
#pragma once

#include <motionCore/Humanoid.h>

#include "pxr/base/gf/quatf.h"
#include "pxr/base/gf/vec3f.h"

#include <optional>
#include <string>
#include <vector>

namespace execmotion {

/// The bone a `UsdSkelAnimation` joint path names, or nullopt.
///
/// A joint path is `UsdSkelAnimation`'s own spelling, e.g. `hips/spine/chest`,
/// of which only the last segment is a bone name (`motion::HumanBoneJointPath`
/// authors the same shape). A path whose leaf names no canonical bone
/// contributes nothing and is not an error: this layer reports what it
/// recognized, and whoever knows which clip it is decides whether a gap matters.
///
/// Exposed for the tests, which check the leaf-segment rule directly rather than
/// through a pose.
std::optional<motion::HumanBone> BoneForJointPath(const std::string& jointPath);

/// The identity pose for `jointPaths`.
///
/// Every rotation is identity and `validRotations` carries exactly the bones the
/// joint paths name.
///
/// The pose's `timestamp` is left at zero, and the caller does not get to pass
/// one. This computation is the same pose at every frame, so it declares no time
/// input at all (see ExecMotionRegistration.cpp).
///
/// No sampling, no interpolation, no retarget. This is the identity, and it is
/// what makes the first OpenExec computation attributable: a wrong result is a
/// wrong mechanism, because there is no algorithm to blame.
motion::HumanoidPose IdentityPoseForJoints(
    const std::vector<std::string>& jointPaths);

/// What a `UsdSkelAnimation` states at one instant, as plain values.
///
/// `rotations` and `translations` are already resolved **at** `timeCode`: exec
/// resolves a time-sampled attribute input at the time the computation is
/// evaluated at, so this layer interpolates nothing and holds nothing. That is
/// the one behavioural difference from `motion::SampleAnimation`, which is
/// handed a whole `HumanoidAnimation` and does its own hold-at-the-edges lookup;
/// which of the two answers a frame between keys is USD's question here and
/// `motionRuntime`'s there, and P0-6 parity is where the two get compared.
struct ClipSample
{
    /// `UsdSkelAnimation`'s `joints`, in the order the clip authored them.
    std::vector<std::string> jointPaths;

    /// `rotations` and `translations` at `timeCode`. An array whose length
    /// disagrees with `jointPaths` contributes nothing, because a clip that
    /// cannot say which joint a value belongs to has not said it -- the same
    /// rule the offline reader applies (tools/motionRetarget StageIo.cpp).
    std::vector<pxr::GfQuatf> rotations;
    std::vector<pxr::GfVec3f> translations;

    /// The frame this sample was resolved at. `hasTimeCode` is false for the
    /// **default** time code, which is what an exec system evaluates at until
    /// `ChangeTime` is called -- so it is the common case rather than an edge
    /// one, and `UsdTimeCode::GetValue()` is a coding error on it.
    double timeCode = 0.0;
    bool hasTimeCode = false;

    /// The rate that turns `timeCode` into the seconds `HumanoidPose::timestamp`
    /// is expressed in. It is an authored input rather than stage metadata
    /// because a computation cannot reach `timeCodesPerSecond`
    /// (docs/reports/openusd/26.08-openexec-mechanism.md §5).
    double timeCodesPerSecond = 0.0;
};

/// The pose `sample` states, or nullopt when it cannot be stamped.
///
/// Returns nullopt for a non-positive `timeCodesPerSecond`, which covers both an
/// absent rate and a nonsense one. That is a refusal rather than a fallback on
/// purpose: `HumanoidPose::timestamp` is a plain double with no absent state, so
/// a pose produced without a rate would carry a second every consumer downstream
/// would take at face value, and there is no value of the field that spells
/// "unknown". A clip whose rate is missing is a clip this layer will not sample.
///
/// Everything else is a partial answer rather than a refusal, because a clip is
/// allowed to be sparse: a joint path naming no canonical bone contributes
/// nothing, an array whose length disagrees with `joints` contributes nothing,
/// and a clip that authors no translations produces a pose with no root
/// position. Only `hips` carries body translation (motion contract); the rest of
/// a `translations` array is rest-pose data a retargeter re-derives per rig.
std::optional<motion::HumanoidPose> PoseFromClipSample(const ClipSample& sample);

} // namespace execmotion
