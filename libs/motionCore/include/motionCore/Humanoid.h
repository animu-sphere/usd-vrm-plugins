// SPDX-License-Identifier: Apache-2.0
//
// Stable, vendor-neutral motion values. This is intentionally a value-type
// contract: no USD stage, plug, file-format, network, or vendor SDK API is
// allowed here.
#pragma once

#include "motionCore/api.h"

#include "pxr/base/gf/quatf.h"
#include "pxr/base/gf/vec3f.h"

#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace motion
{

// The VRM 1.0 humanoid vocabulary. Values are stable array indices; append new
// vocabulary only before Count with an explicit contract review.
enum class HumanBone : std::uint8_t
{
    Hips,
    Spine,
    Chest,
    UpperChest,
    Neck,
    Head,
    LeftEye,
    RightEye,
    Jaw,

    LeftUpperLeg,
    LeftLowerLeg,
    LeftFoot,
    LeftToes,
    RightUpperLeg,
    RightLowerLeg,
    RightFoot,
    RightToes,

    LeftShoulder,
    LeftUpperArm,
    LeftLowerArm,
    LeftHand,
    RightShoulder,
    RightUpperArm,
    RightLowerArm,
    RightHand,

    LeftThumbMetacarpal,
    LeftThumbProximal,
    LeftThumbDistal,
    LeftIndexProximal,
    LeftIndexIntermediate,
    LeftIndexDistal,
    LeftMiddleProximal,
    LeftMiddleIntermediate,
    LeftMiddleDistal,
    LeftRingProximal,
    LeftRingIntermediate,
    LeftRingDistal,
    LeftLittleProximal,
    LeftLittleIntermediate,
    LeftLittleDistal,

    RightThumbMetacarpal,
    RightThumbProximal,
    RightThumbDistal,
    RightIndexProximal,
    RightIndexIntermediate,
    RightIndexDistal,
    RightMiddleProximal,
    RightMiddleIntermediate,
    RightMiddleDistal,
    RightRingProximal,
    RightRingIntermediate,
    RightRingDistal,
    RightLittleProximal,
    RightLittleIntermediate,
    RightLittleDistal,

    Count,
};

inline constexpr std::size_t HumanBoneCount =
    static_cast<std::size_t>(HumanBone::Count);

MOTIONCORE_API bool IsValidHumanBone(HumanBone bone) noexcept;
MOTIONCORE_API std::string_view HumanBoneName(HumanBone bone) noexcept;
MOTIONCORE_API std::optional<HumanBone> FindHumanBone(
    std::string_view name) noexcept;

enum class MotionSourceKind : std::uint8_t
{
    Clip,
    LiveCapture,
    Generated,
    Procedural,
    Simulated,
};

struct MotionSourceMetadata
{
    MotionSourceKind kind = MotionSourceKind::Clip;
    std::string provider;
    std::string protocol;
    std::string sourceId;
};

// All positions and orientations are expressed independently of the hips local
// transform. Booleans distinguish a missing root sample from a zero-valued one.
struct RootMotion
{
    pxr::GfVec3f worldPosition = pxr::GfVec3f(0.0f);
    pxr::GfQuatf worldOrientation = pxr::GfQuatf(
        1.0f, pxr::GfVec3f(0.0f));
    pxr::GfVec3f linearVelocity = pxr::GfVec3f(0.0f);
    pxr::GfVec3f angularVelocity = pxr::GfVec3f(0.0f);

    bool hasPosition = false;
    bool hasOrientation = false;
    bool hasLinearVelocity = false;
    bool hasAngularVelocity = false;
};

enum class FootContact : std::uint8_t
{
    Unknown,
    NotInContact,
    InContact,
};

struct ContactState
{
    FootContact leftFoot = FootContact::Unknown;
    FootContact rightFoot = FootContact::Unknown;
};

struct HumanoidPose
{
    MOTIONCORE_API HumanoidPose();

    // Seconds, never integer frame numbers.
    double timestamp = 0.0;
    RootMotion root;

    // Rotations are local to the semantic humanoid parent. `validRotations`
    // allows sparse capture data and clips that intentionally omit a bone.
    std::array<pxr::GfQuatf, HumanBoneCount> localRotations;
    std::bitset<HumanBoneCount> validRotations;

    std::optional<std::array<float, HumanBoneCount>> confidence;
    std::optional<ContactState> contacts;
    std::optional<MotionSourceMetadata> source;
};

struct HumanoidAnimation
{
    std::vector<HumanoidPose> samples;
    double startTime = 0.0;
    double endTime = 0.0;
    double nominalFrameRate = 30.0;
    MotionSourceMetadata source;
};

// Coordinate-space identifiers are values, not USD schema names. Stage
// authoring and conversion live in the consuming file-format/retarget layers.
enum class CoordinateSpace : std::uint8_t
{
    World,
    Character,
    Skeleton,
    JointLocal,
};

struct ConstraintCommon
{
    double targetTime = 0.0;
    std::optional<HumanBone> joint;
    CoordinateSpace coordinateSpace = CoordinateSpace::Character;
    float weight = 1.0f;
    bool hard = false;
    std::optional<double> validFrom;
    std::optional<double> validUntil;
    MotionSourceMetadata source;
};

struct RootWaypoint
{
    ConstraintCommon common;
    pxr::GfVec3f position = pxr::GfVec3f(0.0f);
};

struct RootTrajectorySample
{
    ConstraintCommon common;
    pxr::GfVec3f position = pxr::GfVec3f(0.0f);
    std::optional<pxr::GfQuatf> orientation;
};

struct FullBodyKeyframe
{
    ConstraintCommon common;
    HumanoidPose pose;
};

struct JointPositionConstraint
{
    ConstraintCommon common;
    pxr::GfVec3f position = pxr::GfVec3f(0.0f);
};

struct JointRotationConstraint
{
    ConstraintCommon common;
    pxr::GfQuatf rotation = pxr::GfQuatf(1.0f, pxr::GfVec3f(0.0f));
};

struct MotionConstraintSet
{
    std::vector<RootWaypoint> rootWaypoints;
    std::vector<RootTrajectorySample> rootTrajectory;
    std::vector<FullBodyKeyframe> keyframes;
    std::vector<JointPositionConstraint> jointPositions;
    std::vector<JointRotationConstraint> jointRotations;
    std::optional<std::string> textPrompt;
};

} // namespace motion
