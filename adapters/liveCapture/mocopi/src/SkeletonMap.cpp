// SPDX-License-Identifier: Apache-2.0

#include "vrmAdapterMocopi/SkeletonMap.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <utility>

namespace vrmAdapterMocopi
{

namespace
{

using motion::HumanBone;

// Which canonical bone each joint of the measured rig carries, by id. `Count`
// is the five the canonical vocabulary has no bone for: they are on the path
// between two bones that are mapped, so their rotations reach the humanoid
// through the composition rather than being dropped (SkeletonMap.h).
//
// Written out rather than derived, and checked against the recorded track's
// profile from outside (`scripts/check_docs.py`) rather than shared with it.
constexpr std::array<HumanBone, MeasuredBoneCount> kMeasuredBones = {{
    HumanBone::Hips,       //  0  root
    HumanBone::Count,      //  1  torso_1
    HumanBone::Spine,      //  2  torso_2
    HumanBone::Count,      //  3  torso_3
    HumanBone::Chest,      //  4  torso_4
    HumanBone::Count,      //  5  torso_5
    HumanBone::Count,      //  6  torso_6
    HumanBone::UpperChest, //  7  torso_7, where both shoulders and the neck sit
    HumanBone::Neck,       //  8  neck_1
    HumanBone::Count,      //  9  neck_2
    HumanBone::Head,       // 10  head

    HumanBone::LeftShoulder,  // 11
    HumanBone::LeftUpperArm,  // 12
    HumanBone::LeftLowerArm,  // 13
    HumanBone::LeftHand,      // 14
    HumanBone::RightShoulder, // 15
    HumanBone::RightUpperArm, // 16
    HumanBone::RightLowerArm, // 17
    HumanBone::RightHand,     // 18

    HumanBone::LeftUpperLeg,  // 19
    HumanBone::LeftLowerLeg,  // 20
    HumanBone::LeftFoot,      // 21
    HumanBone::LeftToes,      // 22
    HumanBone::RightUpperLeg, // 23
    HumanBone::RightLowerLeg, // 24
    HumanBone::RightFoot,     // 25
    HumanBone::RightToes,     // 26
}};

static_assert(kMeasuredBones.size() == MeasuredParentColumn.size(),
              "a joint's bone and its parent are the same rig");

// The joint the frames and the rest pose both take the body's placement from.
constexpr std::uint16_t kRootJoint = 0;

pxr::GfQuatf
Identity() noexcept
{
    return pxr::GfQuatf(1.0f, pxr::GfVec3f(0.0f));
}

bool
IsFinite(const std::array<float, 3>& translation) noexcept
{
    return std::isfinite(translation[0]) && std::isfinite(translation[1])
        && std::isfinite(translation[2]);
}

// A transform that names an orientation: finite throughout, and a rotation with
// a length to divide by. This is `VRM_MOCOPI_NON_FINITE_TRANSFORM`'s own
// definition, and it is the decoder's too — the two agree because they are
// checking the same thing about the same seven floats.
bool
NamesAnOrientation(const BoneTransform& transform) noexcept
{
    if (!IsFinite(transform.translation)) {
        return false;
    }
    const std::array<float, 4>& rotation = transform.rotation;
    double lengthSquared = 0.0;
    for (const float component : rotation) {
        if (!std::isfinite(component)) {
            return false;
        }
        lengthSquared += static_cast<double>(component) * component;
    }
    return lengthSquared > 0.0;
}

Diagnostic
JointDiagnostic(DiagnosticCode code, std::uint16_t boneId, std::string detail)
{
    Diagnostic diagnostic = MakeDiagnostic(code, std::move(detail));
    diagnostic.subject = "bone " + std::to_string(boneId);
    return diagnostic;
}

void
Report(std::vector<Diagnostic>* diagnostics, Diagnostic diagnostic)
{
    if (diagnostics) {
        diagnostics->push_back(std::move(diagnostic));
    }
}

// The joints from just below `jointId`'s nearest bound ancestor down to
// `jointId` itself, root-first — the path rule (MOTION_CONTRACT.md). Bounded by
// the measured rig, whose longest is three: torso_5, torso_6, torso_7.
struct JointPath
{
    std::array<std::uint16_t, MeasuredBoneCount> joints{};
    std::size_t size = 0;
};

JointPath
PathFromNearestBoundAncestor(std::uint16_t jointId) noexcept
{
    JointPath path;
    std::uint16_t walker = jointId;
    while (true) {
        path.joints[path.size++] = walker;
        const std::int16_t parent = MeasuredParentColumn[walker];
        if (parent < 0) {
            break;
        }
        const auto parentId = static_cast<std::uint16_t>(parent);
        if (kMeasuredBones[parentId] != HumanBone::Count) {
            break;
        }
        walker = parentId;
    }
    std::reverse(path.joints.begin(), path.joints.begin() + path.size);
    return path;
}

} // namespace

std::optional<HumanBone>
MeasuredHumanBone(std::uint16_t boneId) noexcept
{
    if (!IsMeasuredJoint(boneId)) {
        return std::nullopt;
    }
    const HumanBone bone = kMeasuredBones[boneId];
    if (bone == HumanBone::Count) {
        return std::nullopt;
    }
    return bone;
}

bool
IsMeasuredJoint(std::uint16_t boneId) noexcept
{
    return static_cast<std::size_t>(boneId) < MeasuredBoneCount;
}

pxr::GfVec3f
ToCanonicalPosition(const std::array<float, 3>& translation) noexcept
{
    // The identity, and measured rather than assumed: this device's basis and
    // the canonical one are both right-handed, +Y up, +Z forward, in metres,
    // with +X the body's left (SkeletonMap.h). A component copy is the whole of
    // the change of basis, which is why the tests check it against the rest
    // pose's own geometry rather than against these three lines.
    return pxr::GfVec3f(translation[0], translation[1], translation[2]);
}

pxr::GfQuatf
ToCanonicalRotation(const std::array<float, 4>& rotation) noexcept
{
    // The wire order is (x, y, z, w) and GfQuatf takes the real part first. No
    // sign changes: the two bases agree, so there is nothing to mirror and no
    // angle to reverse.
    pxr::GfQuatf converted(rotation[3],
                           pxr::GfVec3f(rotation[0], rotation[1], rotation[2]));
    const float length = converted.GetLength();
    // Left alone when there is nothing to divide by: the caller's boundary
    // check has already refused a zero or non-finite rotation, and repairing
    // one here would put an identity where a refusal belongs.
    if (std::isfinite(length) && length > 0.0f) {
        converted /= length;
    }
    return converted;
}

SkeletonMap::SkeletonMap()
{
    restRotations.fill(Identity());
    restTranslations.fill(pxr::GfVec3f(0.0f));
}

std::optional<HumanBone>
SkeletonMap::Bone(std::uint16_t boneId) const noexcept
{
    if (static_cast<std::size_t>(boneId) >= jointCount) {
        return std::nullopt;
    }
    return MeasuredHumanBone(boneId);
}

bool
MakeSkeletonMap(const MotionSkeleton& skeleton, SkeletonMap* out,
                std::vector<Diagnostic>* diagnostics)
{
    if (out == nullptr) {
        return false;
    }
    if (skeleton.bones.size() < MeasuredBoneCount) {
        Report(diagnostics,
               JointDiagnostic(
                   DiagnosticCode::UnsupportedJoint,
                   static_cast<std::uint16_t>(skeleton.bones.size()),
                   "the rig ends here and the measured one has "
                       + std::to_string(MeasuredBoneCount)
                       + " joints, so no id in it has a measured meaning"));
        return false;
    }

    // Ids are positions, so the leading joints must be the measured rig
    // *exactly*: same ids, same parents, same order. The decoder deliberately
    // requires none of that — it reports what arrived — and this layer is where
    // the difference is paid for, because the safety of every id below comes
    // from the topology being the one those ids were measured on. Matching a
    // permuted list would be asking whether two graphs are isomorphic, and a
    // rig that answered yes would still not say which of its arms is the left.
    for (std::size_t index = 0; index < MeasuredBoneCount; ++index) {
        const BoneDefinition& joint = skeleton.bones[index];
        const auto expectedId = static_cast<std::uint16_t>(index);
        if (joint.boneId != expectedId) {
            Report(diagnostics,
                   JointDiagnostic(DiagnosticCode::UnsupportedJoint,
                                   joint.boneId,
                                   "the measured rig carries bone "
                                       + std::to_string(expectedId)
                                       + " in this position"));
            return false;
        }
        if (joint.parentBoneId != MeasuredParentColumn[index]) {
            Report(diagnostics,
                   JointDiagnostic(
                       DiagnosticCode::UnsupportedJoint, joint.boneId,
                       "its parent is "
                           + std::to_string(joint.parentBoneId)
                           + " and the measured rig's is "
                           + std::to_string(MeasuredParentColumn[index])
                           + ", so this is a different rig"));
            return false;
        }
        if (!NamesAnOrientation(joint.restTransform)) {
            Report(diagnostics,
                   JointDiagnostic(DiagnosticCode::NonFiniteTransform,
                                   joint.boneId,
                                   "a rest transform that names no orientation "
                                   "leaves every bone below it unplaced"));
            return false;
        }
    }

    SkeletonMap map;
    map.jointCount = skeleton.bones.size();
    map.jointRestTranslations.reserve(map.jointCount);
    for (const BoneDefinition& joint : skeleton.bones) {
        map.jointRestTranslations.push_back(joint.restTransform.translation);
    }

    // A rig longer than the measured one keeps its leading joints — the loop
    // above has just proved they are the measured rig — and says once, here,
    // that it is sending joints whose meaning nobody has measured. Once per
    // session rather than once per frame: a longer rig is a property of the
    // session, and a per-frame diagnostic would report the same fact sixty
    // times a second.
    for (std::size_t index = MeasuredBoneCount; index < skeleton.bones.size();
         ++index) {
        Report(diagnostics,
               JointDiagnostic(DiagnosticCode::UnsupportedJoint,
                               skeleton.bones[index].boneId,
                               "beyond the measured rig, so this map carries no "
                               "canonical bone for it"));
    }

    // One walk, two callers: the rest pose here and every frame below take the
    // same paths, so they cannot disagree. A rest built by a second traversal
    // would differ from the frames by a constant per bone, which reads as a bad
    // capture rather than as a bug (MOTION_CONTRACT.md).
    for (std::uint16_t jointId = 0; jointId < MeasuredBoneCount; ++jointId) {
        const std::optional<HumanBone> bone = MeasuredHumanBone(jointId);
        if (!bone) {
            continue;
        }
        const JointPath path = PathFromNearestBoundAncestor(jointId);
        pxr::GfQuatf rotation = Identity();
        pxr::GfVec3f translation(0.0f);
        for (std::size_t step = 0; step < path.size; ++step) {
            const BoneTransform& rest =
                skeleton.bones[path.joints[step]].restTransform;
            // Root-first, so each joint's own offset is stated in the frame the
            // rotations composed so far have already established.
            translation += rotation.Transform(
                ToCanonicalPosition(rest.translation));
            rotation = rotation * ToCanonicalRotation(rest.rotation);
        }
        const auto slot = static_cast<std::size_t>(*bone);
        map.present.set(slot);
        map.restRotations[slot] = rotation;
        map.restTranslations[slot] = translation;
    }

    *out = std::move(map);
    return true;
}

bool
MapMotionFrame(const SkeletonMap& map, const MotionFrame& frame,
               FrameMapping* out, std::vector<Diagnostic>* diagnostics)
{
    if (out == nullptr) {
        return false;
    }

    FrameMapping mapping;

    // The frame's records by joint id, so a path can ask for a joint rather
    // than searching for it. Only the measured joints have a slot: everything
    // else is a record this mapping cannot carry, which is what `unusedJoints`
    // counts.
    std::array<const BoneTransform*, MeasuredBoneCount> records{};
    for (const BoneFrame& bone : frame.bones) {
        // The second half of this condition is what makes a map nobody built
        // safe rather than merely useless: its rig declares no joint, so no
        // record is kept, so nothing below indexes a rest table that is empty.
        if (!IsMeasuredJoint(bone.boneId)
            || static_cast<std::size_t>(bone.boneId) >= map.jointCount) {
            ++mapping.unusedJoints;
            continue;
        }
        if (records[bone.boneId] != nullptr) {
            // A repeat of an id already read. The decoder declines to judge
            // duplicates — in a fixed-position encoding the position is the
            // name, so there is no second joint for a second record to be — and
            // a canonical pose has one slot per bone either way. The first
            // record is the one used, and the repeat is counted rather than
            // being silently overwritten by it.
            ++mapping.unusedJoints;
            continue;
        }
        if (!NamesAnOrientation(bone.transform)) {
            // Unreachable through `DecodeMotionPacket`, which refuses such a
            // record before it reaches a frame — and reachable through any
            // other caller, since this function takes structs rather than
            // datagrams. The joint is left absent, so every bone whose path
            // runs through it is absent too.
            Report(diagnostics,
                   JointDiagnostic(DiagnosticCode::NonFiniteTransform,
                                   bone.boneId,
                                   "a transform that names no orientation is "
                                   "not a sample this joint can contribute"));
            continue;
        }
        records[bone.boneId] = &bone.transform;
    }

    // The body's placement: the hips joint's own translation, absolute and in
    // metres. There is no second root channel to compose it with, which is the
    // ambiguity the sibling adapter has and this one does not (SkeletonMap.h).
    if (records[kRootJoint] != nullptr) {
        mapping.hasHipsPosition = true;
        mapping.hipsPosition =
            ToCanonicalPosition(records[kRootJoint]->translation);
    }

    // Every other joint restates its rest offset, in every measured session and
    // bit for bit. One that does not has said something the canonical pose has
    // nowhere to put — only hips translation is body translation — so it is
    // dropped, and counted so that an operator can see a session doing what the
    // measurement says sessions do not do.
    for (std::uint16_t jointId = 1; jointId < MeasuredBoneCount; ++jointId) {
        if (records[jointId] == nullptr) {
            continue;
        }
        if (records[jointId]->translation != map.jointRestTranslations[jointId]) {
            ++mapping.droppedTranslations;
        }
    }

    mapping.bones.reserve(map.present.count());
    for (std::uint16_t jointId = 0; jointId < MeasuredBoneCount; ++jointId) {
        const std::optional<HumanBone> bone = map.Bone(jointId);
        if (!bone) {
            continue;
        }
        const JointPath path = PathFromNearestBoundAncestor(jointId);
        bool complete = true;
        pxr::GfQuatf rotation = Identity();
        for (std::size_t step = 0; step < path.size; ++step) {
            const BoneTransform* record = records[path.joints[step]];
            if (record == nullptr) {
                // A path is only as present as its joints: a bone composed from
                // the part that arrived would be a rotation the device never
                // sent, and an absent bone is not an identity sample.
                complete = false;
                break;
            }
            rotation = rotation * ToCanonicalRotation(record->rotation);
        }
        if (!complete) {
            ++mapping.missingBones;
            continue;
        }
        BoneSample sample;
        sample.bone = *bone;
        sample.localRotation = rotation;
        mapping.bones.push_back(sample);
        mapping.present.set(static_cast<std::size_t>(*bone));
    }

    const bool mapped = !mapping.bones.empty();
    *out = std::move(mapping);
    return mapped;
}

} // namespace vrmAdapterMocopi
