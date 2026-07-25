// SPDX-License-Identifier: Apache-2.0
#include "vrmRetarget/RestPose.h"

#include <cmath>

namespace vrmRetarget
{
namespace
{

const pxr::GfQuatf&
Identity()
{
    static const pxr::GfQuatf identity(1.0f, pxr::GfVec3f(0.0f));
    return identity;
}

// |real| rather than real: -1 is the same orientation as +1.
bool
IsIdentityRotation(const pxr::GfQuatf& q)
{
    const pxr::GfQuatf n = q.GetNormalized();
    return std::fabs(std::fabs(n.GetReal()) - 1.0f) <= 1e-6f;
}

} // namespace

SourceRestPose::SourceRestPose()
{
    localRotations.fill(Identity());
    localTranslations.fill(pxr::GfVec3f(0.0f));
    parents.fill(kNoParent);
}

void
SourceRestPose::SetParent(motion::HumanBone bone, motion::HumanBone parent)
{
    if (!motion::IsValidHumanBone(bone)) {
        return;
    }
    parents[static_cast<std::size_t>(bone)] =
        motion::IsValidHumanBone(parent) ? static_cast<std::size_t>(parent)
                                         : kNoParent;
}

pxr::GfQuatf
SourceRestPose::GetParentRotation(motion::HumanBone bone) const
{
    if (!motion::IsValidHumanBone(bone)) {
        return Identity();
    }
    const std::size_t parent = parents[static_cast<std::size_t>(bone)];
    if (parent >= motion::HumanBoneCount) {
        return Identity();
    }
    return localRotations[parent];
}

RestPoseCorrection::RestPoseCorrection()
{
    pre.fill(Identity());
    post.fill(Identity());
    identity.fill(true);
}

pxr::GfQuatf
RestPoseCorrection::Apply(motion::HumanBone bone,
                          const pxr::GfQuatf& rotation) const
{
    if (!motion::IsValidHumanBone(bone)) {
        return rotation;
    }
    const auto slot = static_cast<std::size_t>(bone);
    if (identity[slot]) {
        return rotation;
    }
    return (pre[slot] * rotation * post[slot]).GetNormalized();
}

RestPoseCorrection
ComputeRestPoseCorrection(const SourceRestPose& source,
                          const TargetSkeleton& target, const HumanoidMap& map)
{
    RestPoseCorrection correction;
    const std::vector<TargetJoint>& joints = target.GetJoints();

    for (std::size_t slot = 0; slot < motion::HumanBoneCount; ++slot) {
        const auto bone = static_cast<motion::HumanBone>(slot);
        const int jointIndex = map.GetJointIndex(bone);
        if (jointIndex < 0
            || static_cast<std::size_t>(jointIndex) >= joints.size()) {
            continue;
        }

        const pxr::GfQuatf sourceRest =
            source.localRotations[slot].GetNormalized();
        const pxr::GfQuatf sourceParentRest =
            source.GetParentRotation(bone).GetNormalized();

        const TargetJoint& joint = joints[static_cast<std::size_t>(jointIndex)];
        const pxr::GfQuatf targetRest = joint.restRotation.GetNormalized();
        const pxr::GfQuatf targetParentRest =
            (joint.parent >= 0
             && static_cast<std::size_t>(joint.parent) < joints.size())
                ? joints[static_cast<std::size_t>(joint.parent)]
                      .restRotation.GetNormalized()
                : Identity();

        if (IsIdentityRotation(sourceRest) && IsIdentityRotation(sourceParentRest)
            && IsIdentityRotation(targetRest)
            && IsIdentityRotation(targetParentRest)) {
            continue;
        }

        // Qt = (Tp^-1 * Sp) * Qs * (S^-1 * Sp^-1 * Tp * T); see RestPose.h.
        correction.pre[slot] =
            (targetParentRest.GetInverse() * sourceParentRest).GetNormalized();
        correction.post[slot] = (sourceRest.GetInverse()
                                 * sourceParentRest.GetInverse()
                                 * targetParentRest * targetRest)
                                    .GetNormalized();
        correction.identity[slot] = false;
    }

    return correction;
}

} // namespace vrmRetarget
