// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "vrmRetarget/api.h"

#include "motionCore/Humanoid.h"

#include "pxr/base/gf/vec3f.h"

namespace vrmRetarget
{

// Where a clip's root motion lands on the target rig.
//
// The motion contract keeps RootMotion a separate object precisely so this is a
// choice made at retarget time rather than baked into the clip.
enum class RootMotionMode
{
    // Author no translation from the clip. Every joint keeps its rest
    // translation, so the avatar animates in place.
    Ignore,

    // Apply the root delta to the joint bound to HumanBone::Hips. This is the
    // default: it matches how `.vrma` carries body translation, and it keeps
    // the result playable on a rig with no dedicated motion root.
    Hips,

    // Apply the root delta to an explicitly named root joint, leaving the hips
    // joint at its rest translation. Use this when the rig's root joint is what
    // downstream tools expect to drive.
    RootJoint,
};

struct RootMotionOptions
{
    RootMotionMode mode = RootMotionMode::Hips;

    // Target joint index for RootMotionMode::RootJoint. Ignored otherwise; when
    // it is negative the mode degrades to Ignore and the caller is told.
    int rootJointIndex = -1;

    // Uniform scale applied to the root translation delta, for retargeting
    // between rigs of different height. 1.0 preserves the source distance.
    float translationScale = 1.0f;

    // When true only the horizontal (XZ) delta is taken and the target's rest
    // height is preserved. Useful when the source and target hip heights differ
    // enough that copying Y would sink or float the avatar.
    bool preserveTargetHeight = false;
};

// Resolves the translation to author on the receiving joint.
//
// `sourceTranslation` is the clip's hips translation for this sample and
// `sourceRestTranslation` the clip's hips rest translation; the delta between
// them is what carries onto the target, added to `targetRestTranslation`. Using
// the delta rather than the absolute value is what lets a clip authored on a
// 1.0 m rig drive a 1.6 m one without the avatar jumping to the source's hip
// height.
VRMRETARGET_API pxr::GfVec3f ResolveRootTranslation(
    const RootMotionOptions& options,
    const pxr::GfVec3f& sourceTranslation,
    const pxr::GfVec3f& sourceRestTranslation,
    const pxr::GfVec3f& targetRestTranslation);

} // namespace vrmRetarget
