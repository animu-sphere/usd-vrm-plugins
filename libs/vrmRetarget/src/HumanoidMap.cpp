// SPDX-License-Identifier: Apache-2.0
#include "vrmRetarget/HumanoidMap.h"

#include <algorithm>
#include <unordered_map>

namespace vrmRetarget
{

HumanoidMap::HumanoidMap()
{
    _jointIndices.fill(kUnmapped);
}

bool
HumanoidMap::SetJointIndex(motion::HumanBone bone, int jointIndex,
                           std::size_t jointCount)
{
    if (!motion::IsValidHumanBone(bone)) {
        return false;
    }
    const auto slot = static_cast<std::size_t>(bone);
    if (jointIndex < 0 || static_cast<std::size_t>(jointIndex) >= jointCount) {
        // Rejected, not bound: the bone is left unmapped and the caller is told
        // so, rather than having to re-query IsMapped to find out.
        _jointIndices[slot] = kUnmapped;
        _mapped.reset(slot);
        return false;
    }
    _jointIndices[slot] = jointIndex;
    _mapped.set(slot);
    return true;
}

bool
HumanoidMap::SetJointToken(motion::HumanBone bone, const std::string& token,
                           const TargetSkeleton& skeleton)
{
    const int index = skeleton.FindJoint(token);
    if (index == TargetSkeleton::kNoParent) {
        return false;
    }
    return SetJointIndex(bone, index, skeleton.GetSize());
}

void
HumanoidMap::Clear()
{
    _jointIndices.fill(kUnmapped);
    _mapped.reset();
}

int
HumanoidMap::GetJointIndex(motion::HumanBone bone) const
{
    if (!motion::IsValidHumanBone(bone)) {
        return kUnmapped;
    }
    return _jointIndices[static_cast<std::size_t>(bone)];
}

bool
HumanoidMap::IsMapped(motion::HumanBone bone) const
{
    return motion::IsValidHumanBone(bone)
        && _mapped.test(static_cast<std::size_t>(bone));
}

const std::vector<motion::HumanBone>&
HumanoidMap::GetRequiredBones()
{
    // VRM 1.0's required humanoid bones. Eyes, jaw, toes, shoulders, fingers,
    // and upperChest are optional and deliberately absent.
    static const std::vector<motion::HumanBone> required = {
        motion::HumanBone::Hips,
        motion::HumanBone::Spine,
        motion::HumanBone::Chest,
        motion::HumanBone::Neck,
        motion::HumanBone::Head,
        motion::HumanBone::LeftUpperLeg,
        motion::HumanBone::LeftLowerLeg,
        motion::HumanBone::LeftFoot,
        motion::HumanBone::RightUpperLeg,
        motion::HumanBone::RightLowerLeg,
        motion::HumanBone::RightFoot,
        motion::HumanBone::LeftUpperArm,
        motion::HumanBone::LeftLowerArm,
        motion::HumanBone::LeftHand,
        motion::HumanBone::RightUpperArm,
        motion::HumanBone::RightLowerArm,
        motion::HumanBone::RightHand,
    };
    return required;
}

std::vector<motion::HumanBone>
HumanoidMap::FindMissingRequiredBones() const
{
    std::vector<motion::HumanBone> missing;
    for (const motion::HumanBone bone : GetRequiredBones()) {
        if (!IsMapped(bone)) {
            missing.push_back(bone);
        }
    }
    return missing;
}

std::vector<int>
HumanoidMap::FindDuplicateJointIndices() const
{
    std::unordered_map<int, int> counts;
    for (std::size_t i = 0; i < motion::HumanBoneCount; ++i) {
        if (_mapped.test(i)) {
            ++counts[_jointIndices[i]];
        }
    }
    std::vector<int> duplicates;
    for (const auto& entry : counts) {
        if (entry.second > 1) {
            duplicates.push_back(entry.first);
        }
    }
    std::sort(duplicates.begin(), duplicates.end());
    return duplicates;
}

} // namespace vrmRetarget
