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
HumanoidMap::SetJointIndex(openstrata::motion::HumanJoint bone, int jointIndex, std::size_t jointCount)
{
    if (!openstrata::motion::IsValidHumanJoint(bone))
    {
        return false;
    }
    const auto slot = static_cast<std::size_t>(bone);
    if (jointIndex < 0 || static_cast<std::size_t>(jointIndex) >= jointCount)
    {
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
HumanoidMap::SetJointToken(openstrata::motion::HumanJoint bone, const std::string& token,
                           const TargetSkeleton& skeleton)
{
    // An unknown token is FindJoint's kNoParent, which SetJointIndex rejects as
    // out of range -- so a failed lookup unmaps the bone exactly as a rejected
    // index does, rather than leaving an earlier binding standing behind a
    // `false` that says this one did not take.
    return SetJointIndex(bone, skeleton.FindJoint(token), skeleton.GetSize());
}

void
HumanoidMap::Clear()
{
    _jointIndices.fill(kUnmapped);
    _mapped.reset();
}

int
HumanoidMap::GetJointIndex(openstrata::motion::HumanJoint bone) const
{
    if (!openstrata::motion::IsValidHumanJoint(bone))
    {
        return kUnmapped;
    }
    return _jointIndices[static_cast<std::size_t>(bone)];
}

bool
HumanoidMap::IsMapped(openstrata::motion::HumanJoint bone) const
{
    return openstrata::motion::IsValidHumanJoint(bone) && _mapped.test(static_cast<std::size_t>(bone));
}

const std::vector<openstrata::motion::HumanJoint>&
HumanoidMap::GetRequiredBones()
{
    // VRM 1.0's required humanoid bones. Eyes, jaw, toes, shoulders, fingers,
    // and upperChest are optional and deliberately absent.
    static const std::vector<openstrata::motion::HumanJoint> required = {
        openstrata::motion::HumanJoint::Hips,          openstrata::motion::HumanJoint::Spine,
        openstrata::motion::HumanJoint::Chest,         openstrata::motion::HumanJoint::Neck,
        openstrata::motion::HumanJoint::Head,          openstrata::motion::HumanJoint::LeftUpperLeg,
        openstrata::motion::HumanJoint::LeftLowerLeg,  openstrata::motion::HumanJoint::LeftFoot,
        openstrata::motion::HumanJoint::RightUpperLeg, openstrata::motion::HumanJoint::RightLowerLeg,
        openstrata::motion::HumanJoint::RightFoot,     openstrata::motion::HumanJoint::LeftUpperArm,
        openstrata::motion::HumanJoint::LeftLowerArm,  openstrata::motion::HumanJoint::LeftHand,
        openstrata::motion::HumanJoint::RightUpperArm, openstrata::motion::HumanJoint::RightLowerArm,
        openstrata::motion::HumanJoint::RightHand,
    };
    return required;
}

std::vector<openstrata::motion::HumanJoint>
HumanoidMap::FindMissingRequiredBones() const
{
    std::vector<openstrata::motion::HumanJoint> missing;
    for (const openstrata::motion::HumanJoint bone : GetRequiredBones())
    {
        if (!IsMapped(bone))
        {
            missing.push_back(bone);
        }
    }
    return missing;
}

std::vector<int>
HumanoidMap::FindDuplicateJointIndices() const
{
    std::unordered_map<int, int> counts;
    for (std::size_t i = 0; i < openstrata::motion::HumanJointCount; ++i)
    {
        if (_mapped.test(i))
        {
            ++counts[_jointIndices[i]];
        }
    }
    std::vector<int> duplicates;
    for (const auto& entry : counts)
    {
        if (entry.second > 1)
        {
            duplicates.push_back(entry.first);
        }
    }
    std::sort(duplicates.begin(), duplicates.end());
    return duplicates;
}

bool
operator==(const HumanoidMap& a, const HumanoidMap& b) noexcept
{
    // Both halves, although every writer keeps an unmapped slot at kUnmapped
    // and the bitset is therefore implied by the indices today: comparing the
    // two is what stays correct if a writer ever stops doing that.
    return a._mapped == b._mapped && a._jointIndices == b._jointIndices;
}

bool
operator!=(const HumanoidMap& a, const HumanoidMap& b) noexcept
{
    return !(a == b);
}

} // namespace vrmRetarget
