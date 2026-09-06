// SPDX-License-Identifier: Apache-2.0
#include "ExecMotionIdentity.h"

#include <cstddef>

namespace execmotion {

std::optional<motion::HumanBone>
BoneForJointPath(const std::string& jointPath)
{
    // A joint path is `parent/child/leaf`; the bone is the leaf. A path with no
    // separator is already a leaf, which is what a flat rig authors.
    const std::size_t slash = jointPath.rfind('/');
    const std::string_view leaf =
        slash == std::string::npos
            ? std::string_view(jointPath)
            : std::string_view(jointPath).substr(slash + 1);
    if (leaf.empty()) {
        return std::nullopt;
    }
    return motion::FindHumanBone(leaf);
}

motion::HumanoidPose
IdentityPoseForJoints(const std::vector<std::string>& jointPaths,
                      double timestamp)
{
    motion::HumanoidPose pose;
    pose.timestamp = timestamp;
    // HumanoidPose's default constructor already fills localRotations with the
    // identity quaternion and clears validRotations, so this loop only says
    // which bones the clip named -- it authors no rotation at all.
    for (const std::string& jointPath : jointPaths) {
        if (const std::optional<motion::HumanBone> bone =
                BoneForJointPath(jointPath)) {
            pose.validRotations.set(static_cast<std::size_t>(*bone));
        }
    }
    return pose;
}

} // namespace execmotion
