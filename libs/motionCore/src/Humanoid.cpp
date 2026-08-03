// SPDX-License-Identifier: Apache-2.0
#include "motionCore/Humanoid.h"

#include <algorithm>
#include <array>
#include <utility>

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

std::optional<HumanBone>
HumanBoneParent(HumanBone bone) noexcept
{
    using Bone = HumanBone;
    switch (bone) {
    case Bone::Hips: return std::nullopt;
    case Bone::Spine: return Bone::Hips;
    case Bone::Chest: return Bone::Spine;
    case Bone::UpperChest: return Bone::Chest;
    case Bone::Neck: return Bone::UpperChest;
    case Bone::Head: return Bone::Neck;
    case Bone::LeftEye:
    case Bone::RightEye:
    case Bone::Jaw: return Bone::Head;

    case Bone::LeftUpperLeg:
    case Bone::RightUpperLeg: return Bone::Hips;
    case Bone::LeftLowerLeg: return Bone::LeftUpperLeg;
    case Bone::LeftFoot: return Bone::LeftLowerLeg;
    case Bone::LeftToes: return Bone::LeftFoot;
    case Bone::RightLowerLeg: return Bone::RightUpperLeg;
    case Bone::RightFoot: return Bone::RightLowerLeg;
    case Bone::RightToes: return Bone::RightFoot;

    case Bone::LeftShoulder:
    case Bone::RightShoulder: return Bone::UpperChest;
    case Bone::LeftUpperArm: return Bone::LeftShoulder;
    case Bone::LeftLowerArm: return Bone::LeftUpperArm;
    case Bone::LeftHand: return Bone::LeftLowerArm;
    case Bone::RightUpperArm: return Bone::RightShoulder;
    case Bone::RightLowerArm: return Bone::RightUpperArm;
    case Bone::RightHand: return Bone::RightLowerArm;

    case Bone::LeftThumbMetacarpal:
    case Bone::LeftIndexProximal:
    case Bone::LeftMiddleProximal:
    case Bone::LeftRingProximal:
    case Bone::LeftLittleProximal: return Bone::LeftHand;
    case Bone::LeftThumbProximal: return Bone::LeftThumbMetacarpal;
    case Bone::LeftThumbDistal: return Bone::LeftThumbProximal;
    case Bone::LeftIndexIntermediate: return Bone::LeftIndexProximal;
    case Bone::LeftIndexDistal: return Bone::LeftIndexIntermediate;
    case Bone::LeftMiddleIntermediate: return Bone::LeftMiddleProximal;
    case Bone::LeftMiddleDistal: return Bone::LeftMiddleIntermediate;
    case Bone::LeftRingIntermediate: return Bone::LeftRingProximal;
    case Bone::LeftRingDistal: return Bone::LeftRingIntermediate;
    case Bone::LeftLittleIntermediate: return Bone::LeftLittleProximal;
    case Bone::LeftLittleDistal: return Bone::LeftLittleIntermediate;

    case Bone::RightThumbMetacarpal:
    case Bone::RightIndexProximal:
    case Bone::RightMiddleProximal:
    case Bone::RightRingProximal:
    case Bone::RightLittleProximal: return Bone::RightHand;
    case Bone::RightThumbProximal: return Bone::RightThumbMetacarpal;
    case Bone::RightThumbDistal: return Bone::RightThumbProximal;
    case Bone::RightIndexIntermediate: return Bone::RightIndexProximal;
    case Bone::RightIndexDistal: return Bone::RightIndexIntermediate;
    case Bone::RightMiddleIntermediate: return Bone::RightMiddleProximal;
    case Bone::RightMiddleDistal: return Bone::RightMiddleIntermediate;
    case Bone::RightRingIntermediate: return Bone::RightRingProximal;
    case Bone::RightRingDistal: return Bone::RightRingIntermediate;
    case Bone::RightLittleIntermediate: return Bone::RightLittleProximal;
    case Bone::RightLittleDistal: return Bone::RightLittleIntermediate;

    case Bone::Count: return std::nullopt;
    }
    return std::nullopt;
}

std::optional<HumanBone>
NearestPresentAncestor(HumanBone bone,
                       const std::bitset<HumanBoneCount>& present) noexcept
{
    std::optional<HumanBone> parent = HumanBoneParent(bone);
    while (parent) {
        if (present.test(static_cast<std::size_t>(*parent))) {
            return parent;
        }
        parent = HumanBoneParent(*parent);
    }
    return std::nullopt;
}

std::string
HumanBoneJointPath(HumanBone bone, const std::bitset<HumanBoneCount>& present)
{
    if (!IsValidHumanBone(bone)) {
        return {};
    }
    // Every bone's parent has a smaller enum value, so walking up terminates.
    std::string path(HumanBoneName(bone));
    std::optional<HumanBone> ancestor = NearestPresentAncestor(bone, present);
    while (ancestor) {
        path.insert(0, "/");
        path.insert(0, HumanBoneName(*ancestor));
        ancestor = NearestPresentAncestor(*ancestor, present);
    }
    return path;
}

HumanoidPose::HumanoidPose()
{
    // Exported rather than inline so every consumer receives the same identity
    // defaults across the motionCore DLL boundary.
    localRotations.fill(pxr::GfQuatf(1.0f, pxr::GfVec3f(0.0f)));
}

bool
ExpressionWeights::Set(std::string_view name, float weight)
{
    const auto position = std::lower_bound(
        entries.begin(), entries.end(), name,
        [](const ExpressionWeight& entry, std::string_view sought) {
            return entry.name < sought;
        });
    if (position != entries.end() && position->name == name) {
        position->weight = weight;
        return false;
    }
    ExpressionWeight entry;
    entry.name.assign(name);
    entry.weight = weight;
    entries.insert(position, std::move(entry));
    return true;
}

const float*
ExpressionWeights::Find(std::string_view name) const noexcept
{
    const auto position = std::lower_bound(
        entries.begin(), entries.end(), name,
        [](const ExpressionWeight& entry, std::string_view sought) {
            return entry.name < sought;
        });
    return position != entries.end() && position->name == name
        ? &position->weight
        : nullptr;
}

} // namespace motion
