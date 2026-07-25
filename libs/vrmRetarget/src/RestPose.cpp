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
SourceRestPose::GetWorldRestRotation(motion::HumanBone bone) const
{
    if (!motion::IsValidHumanBone(bone)) {
        return Identity();
    }
    // world = L_root * ... * L_parent * L_bone, so each ancestor composes on
    // the left as the walk climbs. The depth cap makes a malformed `parents`
    // cycle terminate instead of spinning; a well-formed chain never revisits a
    // bone, so it cannot reach the cap.
    pxr::GfQuatf world = Identity();
    std::size_t cursor = static_cast<std::size_t>(bone);
    for (std::size_t depth = 0;
         depth < motion::HumanBoneCount && cursor < motion::HumanBoneCount;
         ++depth) {
        world = localRotations[cursor].GetNormalized() * world;
        cursor = parents[cursor];
    }
    return world.GetNormalized();
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

        // Sp and Tp are the parents' *accumulated* rest rotations. Using each
        // parent's own local rotation would agree only where the parent is
        // itself a root, and would silently mis-retarget every bone below the
        // second level of a rig whose rest pose is not identity.
        const pxr::GfQuatf sourceRest =
            source.localRotations[slot].GetNormalized();
        const std::size_t sourceParent = source.parents[slot];
        const pxr::GfQuatf sourceParentRest =
            sourceParent < motion::HumanBoneCount
                ? source.GetWorldRestRotation(
                      static_cast<motion::HumanBone>(sourceParent))
                : Identity();

        const TargetJoint& joint = joints[static_cast<std::size_t>(jointIndex)];
        const pxr::GfQuatf targetRest = joint.restRotation.GetNormalized();
        const pxr::GfQuatf targetParentRest =
            target.GetWorldRestRotation(joint.parent);

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
