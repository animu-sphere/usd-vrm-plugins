// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "vrmRetarget/api.h"

#include "vrmRetarget/HumanoidMap.h"
#include "vrmRetarget/TargetSkeleton.h"

#include "motionCore/Humanoid.h"

#include "pxr/base/gf/quatf.h"
#include "pxr/base/gf/vec3f.h"

#include <array>
#include <cstddef>

namespace vrmRetarget
{

// The clip's own rest pose, per human bone. `usdVrmaFileFormat` authors a
// semantic skeleton whose rest rotations are all identity, but a clip from
// another producer need not, so the correction is computed rather than assumed.
struct SourceRestPose
{
    VRMRETARGET_API SourceRestPose();

    // Marks a bone with no semantic parent.
    static constexpr std::size_t kNoParent = motion::HumanBoneCount;

    std::array<pxr::GfQuatf, motion::HumanBoneCount> localRotations;
    std::array<pxr::GfVec3f, motion::HumanBoneCount> localTranslations;

    // Semantic parent of each bone, as an index into the arrays above, or
    // kNoParent. The default is "every bone is a root", which is exactly right
    // for an all-identity rest pose (an identity parent contributes nothing to
    // the correction) and keeps this library free of a second copy of the VRM
    // humanoid taxonomy. A producer with a non-identity rest pose fills it in.
    std::array<std::size_t, motion::HumanBoneCount> parents;

    VRMRETARGET_API void SetParent(motion::HumanBone bone,
                                   motion::HumanBone parent);
    VRMRETARGET_API pxr::GfQuatf GetParentRotation(motion::HumanBone bone) const;
};

// Per-bone correction carrying a rest-relative rotation from the source rig
// onto a target whose rest pose differs.
//
// The invariant is that the bone's world-space rotation *away from its own
// rest* is preserved. With source rest `S`, source parent rest `Sp`, target
// rest `T`, and target parent rest `Tp`, equating the two world deltas
//
//     Tp * Qt * T^-1 * Tp^-1  ==  Sp * Qs * S^-1 * Sp^-1
//
// gives
//
//     Qt = (Tp^-1 * Sp) * Qs * (S^-1 * Sp^-1 * Tp * T)   [= pre * Qs * post]
//
// Composition is OpenUSD's: `a * b` applies `b` first. Where both rest poses
// are identity — the `usdVrmaFileFormat` case — pre and post are identity and
// the sample passes through untouched.
struct RestPoseCorrection
{
    VRMRETARGET_API RestPoseCorrection();

    std::array<pxr::GfQuatf, motion::HumanBoneCount> pre;
    std::array<pxr::GfQuatf, motion::HumanBoneCount> post;
    std::array<bool, motion::HumanBoneCount> identity;

    // Returns `rotation` unchanged when the bone's correction is identity.
    VRMRETARGET_API pxr::GfQuatf Apply(motion::HumanBone bone,
                                       const pxr::GfQuatf& rotation) const;
};

// Builds the correction for every mapped bone; unmapped bones stay identity.
VRMRETARGET_API RestPoseCorrection ComputeRestPoseCorrection(
    const SourceRestPose& source, const TargetSkeleton& target,
    const HumanoidMap& map);

} // namespace vrmRetarget
