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

// The canonical VRM 1.0 humanoid hierarchy. Nullopt for Hips, which is the
// root, and for Count.
//
// This lives here rather than in each consumer because a second copy of the
// humanoid taxonomy is a defect waiting to happen: the `.vrma` reader and the
// live-capture path author the same semantic skeleton, and two tables that can
// disagree would produce two skeletons that look alike and do not compose.
MOTIONCORE_API std::optional<HumanBone> HumanBoneParent(
    HumanBone bone) noexcept;

// The nearest ancestor of `bone` that `present` carries, skipping bones the
// rig does not solve -- a capture rig with no `upperChest` still parents its
// shoulders somewhere. Nullopt when no ancestor is present.
MOTIONCORE_API std::optional<HumanBone> NearestPresentAncestor(
    HumanBone bone, const std::bitset<HumanBoneCount>& present) noexcept;

// The semantic joint path for `bone` within a rig carrying `present`, e.g.
// "hips/spine/chest/neck/head". This is the token a `UsdSkelSkeleton` built
// from humanoid semantics carries; the string is plain text, and authoring it
// onto a stage stays with the consumer.
MOTIONCORE_API std::string HumanBoneJointPath(
    HumanBone bone, const std::bitset<HumanBoneCount>& present);

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

// Exact value equality, on the aggregates that cross a boundary: the two
// OpenExec asks for it, a trace round-trip is defined by it, and a test that
// wants to know whether two poses describe the same *motion* wants
// `NearlyEqual` from Compare.h instead. That header carries the reasoning for
// both, including why a pose does not compare the fields it says it does not
// carry. The declarative `MotionConstraintSet` types are deliberately left
// without one: nothing compares them yet, and an operator no caller exercises
// is untested surface.
MOTIONCORE_API bool operator==(const MotionSourceMetadata& a,
                               const MotionSourceMetadata& b) noexcept;
MOTIONCORE_API bool operator!=(const MotionSourceMetadata& a,
                               const MotionSourceMetadata& b) noexcept;

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

MOTIONCORE_API bool operator==(const RootMotion& a,
                               const RootMotion& b) noexcept;
MOTIONCORE_API bool operator!=(const RootMotion& a,
                               const RootMotion& b) noexcept;

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

MOTIONCORE_API bool operator==(const ContactState& a,
                               const ContactState& b) noexcept;
MOTIONCORE_API bool operator!=(const ContactState& a,
                               const ContactState& b) noexcept;

// One expression weight, under the name its producer used.
//
// The humanoid vocabulary is closed, so it is an enum. The expression
// vocabulary is not: VRM 1.0 defines preset names and then lets an author add
// their own, and a VMC sender's blend-shape names are whatever its model
// carries. So a name is carried verbatim. Deciding that one producer's "Joy"
// is a particular rig's `happy` is a resolve step in the consumer -- it needs
// the rig, which this layer does not have, and a table here would be a guess
// applied to every producer at once (Motion Phase G).
struct ExpressionWeight
{
    std::string name;

    // Conventionally in [0, 1] and deliberately not clamped: a sender that
    // said 1.5 said 1.5, and a value type that quietly corrected it would hide
    // the sender from the operator judging the session.
    float weight = 0.0f;
};

MOTIONCORE_API bool operator==(const ExpressionWeight& a,
                               const ExpressionWeight& b) noexcept;
MOTIONCORE_API bool operator!=(const ExpressionWeight& a,
                               const ExpressionWeight& b) noexcept;

// The expression weights one sample reported.
//
// Sorted by name, each name once -- an invariant rather than a convention, and
// `Set` is what maintains it. Two producers that reported the same weights in a
// different order are the same motion, so they must be the same value; and a
// trace written from either has to round-trip to the same bytes. Neither holds
// for a list in arrival order, and both are load-bearing here.
//
// An absent name is not a zero weight. A name this set does not carry was not
// reported, exactly as a bone outside `validRotations` was not reported. `Find`
// answers with a pointer rather than a value so the two cannot be confused.
struct ExpressionWeights
{
    std::vector<ExpressionWeight> entries;

    // Sets `name` to `weight`, keeping `entries` sorted. Returns false when
    // `name` was already present: the new weight replaces the old one, and the
    // caller is told, because only the caller knows whether a repeat is a
    // duplicate delivery to count or an update to accept.
    MOTIONCORE_API bool Set(std::string_view name, float weight);

    // The weight reported for `name`, or null when it was not reported.
    MOTIONCORE_API const float* Find(std::string_view name) const noexcept;

    bool IsEmpty() const noexcept { return entries.empty(); }
};

MOTIONCORE_API bool operator==(const ExpressionWeights& a,
                               const ExpressionWeights& b) noexcept;
MOTIONCORE_API bool operator!=(const ExpressionWeights& a,
                               const ExpressionWeights& b) noexcept;

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

    // What this sample said about the character's face, on the same timeline as
    // its body. Empty means the sample reported no weights, and there is no
    // separate absent state because there is nothing it would mean that "the
    // producer reported none" does not -- an `optional` here would make two
    // values differ over a distinction neither carries.
    //
    // This is on the pose rather than in a track of its own, and that was the
    // decision (MOTION_CONTRACT.md). Both producers put expressions on the
    // pose's instants already: a VMC datagram carries bones and blend values
    // under one `/VMC/Ext/T`, and the `.vrma` reader already evaluates every
    // channel at the union of their key times. A parallel track would have
    // needed a second buffer, a second intake policy and a second resampler in
    // `motionRuntime` to carry data that arrives at the same instants anyway.
    ExpressionWeights expressions;

    // Where this sample says the character is looking: a target *point*, in the
    // same space as `root.worldPosition`, and never a direction.
    //
    // A direction is only meaningful next to a head, and which head -- where it
    // sits, how far the eyes are from it -- is a property of a rig this layer
    // does not have. So the point the producer named is carried and
    // `LookAtEvaluate` (Motion Phase G) turns it into eye rotations or
    // expression weights against one avatar's own look-at configuration. This
    // is the same division the expression weights above are under, for the same
    // reason.
    //
    // The offset from the head bone that the *source* rig measured is a
    // constant of that rig rather than of a sample, so it travels beside the
    // clip and not here.
    //
    // Optional rather than a sentinel, because the origin is a point a producer
    // can legitimately look at: "reported no target" cannot be spelled as a
    // value of the target.
    std::optional<pxr::GfVec3f> lookAtTarget;

    std::optional<MotionSourceMetadata> source;
};

MOTIONCORE_API bool operator==(const HumanoidPose& a,
                               const HumanoidPose& b) noexcept;
MOTIONCORE_API bool operator!=(const HumanoidPose& a,
                               const HumanoidPose& b) noexcept;

struct HumanoidAnimation
{
    std::vector<HumanoidPose> samples;
    double startTime = 0.0;
    double endTime = 0.0;
    double nominalFrameRate = 30.0;
    MotionSourceMetadata source;
};

MOTIONCORE_API bool operator==(const HumanoidAnimation& a,
                               const HumanoidAnimation& b) noexcept;
MOTIONCORE_API bool operator!=(const HumanoidAnimation& a,
                               const HumanoidAnimation& b) noexcept;

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
