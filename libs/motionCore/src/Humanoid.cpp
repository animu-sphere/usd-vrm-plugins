// SPDX-License-Identifier: Apache-2.0
#include "motionCore/Humanoid.h"

#include <array>

namespace motion
{
namespace
{

constexpr std::array<std::string_view, HumanBoneCount> kHumanBoneNames = {
    "hips", "spine", "chest", "upperChest", "neck", "head", "leftEye",
    "rightEye", "jaw",
    "leftUpperLeg", "leftLowerLeg", "leftFoot", "leftToes", "rightUpperLeg",
    "rightLowerLeg", "rightFoot", "rightToes",
    "leftShoulder", "leftUpperArm", "leftLowerArm", "leftHand", "rightShoulder",
    "rightUpperArm", "rightLowerArm", "rightHand",
    "leftThumbMetacarpal", "leftThumbProximal", "leftThumbDistal",
    "leftIndexProximal", "leftIndexIntermediate", "leftIndexDistal",
    "leftMiddleProximal", "leftMiddleIntermediate", "leftMiddleDistal",
    "leftRingProximal", "leftRingIntermediate", "leftRingDistal",
    "leftLittleProximal", "leftLittleIntermediate", "leftLittleDistal",
    "rightThumbMetacarpal", "rightThumbProximal", "rightThumbDistal",
    "rightIndexProximal", "rightIndexIntermediate", "rightIndexDistal",
    "rightMiddleProximal", "rightMiddleIntermediate", "rightMiddleDistal",
    "rightRingProximal", "rightRingIntermediate", "rightRingDistal",
    "rightLittleProximal", "rightLittleIntermediate", "rightLittleDistal",
};

static_assert(kHumanBoneNames.size() == HumanBoneCount,
              "HumanBone names must cover the complete enum");

} // namespace

bool
IsValidHumanBone(HumanBone bone) noexcept
{
    return static_cast<std::size_t>(bone) < HumanBoneCount;
}

std::string_view
HumanBoneName(HumanBone bone) noexcept
{
    if (!IsValidHumanBone(bone)) {
        return {};
    }
    return kHumanBoneNames[static_cast<std::size_t>(bone)];
}

std::optional<HumanBone>
FindHumanBone(std::string_view name) noexcept
{
    for (std::size_t index = 0; index != kHumanBoneNames.size(); ++index) {
        if (kHumanBoneNames[index] == name) {
            return static_cast<HumanBone>(index);
        }
    }
    return std::nullopt;
}

HumanoidPose::HumanoidPose()
{
    // Exported rather than inline so every consumer receives the same identity
    // defaults across the motionCore DLL boundary.
    localRotations.fill(pxr::GfQuatf(1.0f, pxr::GfVec3f(0.0f)));
}

} // namespace motion
