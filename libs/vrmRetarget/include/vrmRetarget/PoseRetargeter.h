// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "vrmRetarget/api.h"

#include "vrmRetarget/HumanoidMap.h"
#include "vrmRetarget/RestPose.h"
#include "vrmRetarget/RootMotionPolicy.h"
#include "vrmRetarget/TargetSkeleton.h"

#include "motionCore/Humanoid.h"

#include "pxr/base/gf/quatf.h"
#include "pxr/base/gf/vec3f.h"
#include "pxr/base/gf/vec3h.h"

#include <string>
#include <vector>

namespace vrmRetarget
{

// One sample expanded into the target rig's joint order. Every vector is
// GetSize() long, so it can be written straight into a UsdSkelAnimation without
// a second expansion pass.
struct RetargetedPose
{
    double timestamp = 0.0;
    std::vector<pxr::GfQuatf> rotations;
    std::vector<pxr::GfVec3f> translations;
};

// Exact, joint by joint: the same timestamp and the same rotation and
// translation in every slot, in the rig's order. It exists for the caller
// `RestPoseCorrection`'s does: `ExecTypeRegistry::RegisterType` will not
// register a type it cannot compare, and `execVrm`'s `vrm.humanoidRetarget`
// hands this value back whole.
//
// Exact means a rotation and its negation are *different* poses although they
// pose the joint identically -- the conservative answer, and the one
// `motion::HumanoidPose`'s `operator==` gives. There is no `NearlyEqual`: the
// OpenExec plan's parity step (P0-6) decides whether it compares these values
// or the arrays a bake authors from them, and asks for one if it is the first.
VRMRETARGET_API bool operator==(const RetargetedPose& a,
                                const RetargetedPose& b) noexcept;
VRMRETARGET_API bool operator!=(const RetargetedPose& a,
                                const RetargetedPose& b) noexcept;

struct RetargetedAnimation
{
    // The joint tokens these samples are ordered by — the target skeleton's
    // own order, so the consumer can author `joints` without re-deriving it.
    std::vector<std::string> joints;
    std::vector<RetargetedPose> samples;
    double startTime = 0.0;
    double endTime = 0.0;
    double frameRate = 30.0;
    motion::MotionSourceMetadata source;
};

// One retargeted sample in the shape a UsdSkelAnimation states it at one time
// code: the rig's joint tokens and one translation, rotation and scale per
// joint, in that order. It is a RetargetedPose with the two things a bake adds
// to one -- the `joints` the arrays are ordered by, and `scales` -- so a
// consumer can author, or compare against, an animation's four arrays without
// re-deriving either.
//
// `scales` is part of the shape rather than something a writer remembers:
// UsdSkel reads translations, rotations and scales as a unit, and an animation
// that authors no `scales` binds, reads back attribute by attribute, and then
// resolves no joint transforms at all -- the rig stays at rest. A retargeted
// clip never animates scale, so every entry is (1, 1, 1): the rule
// `motion_retarget` authors as one constant array.
//
// `timestamp` is the retarget's, in seconds. An animation places a sample at a
// time code instead; the writer scales one to the other.
//
// It exists for `execVrm`'s `vrm.computeJointLocalTransforms`, which answers it
// whole, so like every value that crosses an exec boundary it compares exactly.
struct JointLocalTransforms
{
    double timestamp = 0.0;
    std::vector<std::string> joints;
    std::vector<pxr::GfVec3f> translations;
    std::vector<pxr::GfQuatf> rotations;
    std::vector<pxr::GfVec3h> scales;
};

// Exact, joint by joint, in the rig's order: the same joints, the same
// timestamp, and the same entry in every slot of all three arrays. A rotation
// and its negation are different values, as for RetargetedPose, and there is
// no `NearlyEqual` for the same reason.
VRMRETARGET_API bool operator==(const JointLocalTransforms& a,
                                const JointLocalTransforms& b) noexcept;
VRMRETARGET_API bool operator!=(const JointLocalTransforms& a,
                                const JointLocalTransforms& b) noexcept;

// The skeleton-space transform of one joint of a retargeted pose: its own local
// rotation and translation with every ancestor's composed on, root first.
//
// A consumer needs this the moment it has to relate a retargeted rig to
// something outside the rig -- a look-at target being the case that brought it
// here, since `LookAtEvaluator` needs to know where the head *is* and a
// RetargetedPose states only where each joint is relative to its parent. The
// pose's arrays are one per joint of `skeleton`, so this walks the ancestor
// chain rather than the whole rig.
//
// Returns false, leaving both outputs untouched, when the index is out of range
// for either the skeleton or the pose, or when the ancestor chain does not
// terminate -- a parent cycle is a rig this cannot answer for, and looping on
// one would be worse than refusing.
VRMRETARGET_API bool GetJointWorldTransform(const TargetSkeleton& skeleton,
                                            const RetargetedPose& pose,
                                            int jointIndex,
                                            pxr::GfQuatf* orientation,
                                            pxr::GfVec3f* position);

struct RetargetOptions
{
    RootMotionOptions rootMotion;

    // Resample onto a uniform timeline at this rate before retargeting. Zero or
    // negative keeps the source sample times untouched.
    double resampleRate = 0.0;
};

// Diagnostics a caller should surface rather than swallow. Retargeting onto a
// rig that is missing bones is legal and useful; doing it silently is not.
struct RetargetDiagnostics
{
    std::vector<motion::HumanBone> unmappedSourceBones;
    std::vector<motion::HumanBone> missingRequiredBones;
    std::vector<std::string> warnings;

    bool IsClean() const
    {
        return unmappedSourceBones.empty() && missingRequiredBones.empty()
            && warnings.empty();
    }
};

// Expands semantic humanoid poses into a target rig's joint order.
//
// Joints the clip does not drive keep their rest rotation and rest translation,
// so a partial clip (upper body only, say) leaves the rest of the rig at rest
// instead of collapsing it to identity.
class VRMRETARGET_API PoseRetargeter
{
public:
    PoseRetargeter(TargetSkeleton skeleton, HumanoidMap map,
                   SourceRestPose sourceRest = SourceRestPose(),
                   RetargetOptions options = RetargetOptions());

    const TargetSkeleton& GetSkeleton() const noexcept { return _skeleton; }
    const HumanoidMap& GetMap() const noexcept { return _map; }
    const RetargetOptions& GetOptions() const noexcept { return _options; }

    // Expands one pose. `diagnostics` may be null.
    RetargetedPose Retarget(const motion::HumanoidPose& pose,
                            RetargetDiagnostics* diagnostics = nullptr) const;

    // Expands a whole clip, resampling first when RetargetOptions asks for it.
    RetargetedAnimation Retarget(const motion::HumanoidAnimation& animation,
                                 RetargetDiagnostics* diagnostics = nullptr) const;

private:
    RetargetedPose _RestPose() const;

    TargetSkeleton _skeleton;
    HumanoidMap _map;
    SourceRestPose _sourceRest;
    RetargetOptions _options;
    RestPoseCorrection _correction;
};

} // namespace vrmRetarget
