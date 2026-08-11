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

// A rig this map cannot read, reported at a severity the code's own default does
// not carry.
//
// `VRM_MOCOPI_UNSUPPORTED_JOINT` defaults to info, and for the case it was
// frozen to describe that is right: one joint of many maps to nothing and the
// session continues with the rest. This is the other case wearing the same code
// — *no* joint maps to anything, so a session gets no pose at all — and an
// operator filtering a live log at warning would otherwise watch silence at the
// same level as "three finger joints were ignored". The code stays one of the
// frozen nine; the severity says which of its two cases happened. It stays
// recoverable because the next skeleton packet may declare a rig this map reads.
Diagnostic
RigRefused(DiagnosticCode code, std::uint16_t boneId, std::string detail)
{
    Diagnostic diagnostic = JointDiagnostic(code, boneId, std::move(detail));
    diagnostic.severity = DiagnosticSeverity::Warning;
    return diagnostic;
}

void
Report(std::vector<Diagnostic>* diagnostics, Diagnostic diagnostic)
{
    if (diagnostics) {
        diagnostics->push_back(std::move(diagnostic));
    }
}

// Every joint's parent precedes it, which the measured rig satisfies and which
// the walk below relies on for termination. Asserted at compile time rather than
// left to `scripts/check_docs.py`: that check is a separate lane behind a path
// filter, and an edit making a joint its own ancestor would loop forever and
// write past the array a few lines down. Here it is a build error.
constexpr bool
ParentsPrecedeChildren() noexcept
{
    for (std::size_t index = 0; index < MeasuredParentColumn.size(); ++index) {
        if (MeasuredParentColumn[index] >= static_cast<std::int16_t>(index)) {
            return false;
        }
    }
    return true;
}

static_assert(ParentsPrecedeChildren(),
              "the measured parent column must be topologically ordered");

// The joints from just below `jointId`'s nearest bound ancestor down to
// `jointId` itself, root-first — the path rule (MOTION_CONTRACT.md). The longest
// in the measured rig is three (torso_5, torso_6, torso_7); the array is sized
// for the rig rather than for that measurement, because a bound of 3 would be a
// second thing to remember when the table changes.
struct JointPath
{
    std::array<std::uint16_t, MeasuredBoneCount> joints{};
    std::size_t size = 0;
};

constexpr JointPath
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
    for (std::size_t front = 0, back = path.size - 1; front < back;
         ++front, --back) {
        const std::uint16_t swap = path.joints[front];
        path.joints[front] = path.joints[back];
        path.joints[back] = swap;
    }
    return path;
}

// One path per joint, computed once at compile time. The paths are a pure
// function of two constexpr tables, so walking the column again for twenty-two
// bones sixty times a second was work with a known answer.
constexpr std::array<JointPath, MeasuredBoneCount>
MeasuredPaths() noexcept
{
    std::array<JointPath, MeasuredBoneCount> paths{};
    for (std::size_t jointId = 0; jointId < MeasuredBoneCount; ++jointId) {
        paths[jointId] =
            PathFromNearestBoundAncestor(static_cast<std::uint16_t>(jointId));
    }
    return paths;
}

constexpr std::array<JointPath, MeasuredBoneCount> kPaths = MeasuredPaths();

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
    // The length is formed in the same precision the boundary check uses, and
    // the division is done in it too. That is load-bearing rather than tidy:
    // `GfQuatf::GetLength()` squares in float, so a quaternion whose components
    // sit near the denormal floor — accepted by `NamesAnOrientation` and by the
    // decoder, both of which accumulate in double — underflows to a length of
    // exactly zero and comes back un-normalised, collapsing every composition it
    // then enters. Narrowing after the divide instead of before it keeps that
    // whole range representable. This project has already paid once for two
    // magnitudes formed in different precisions (motionCore/Compare.h).
    double lengthSquared = 0.0;
    for (const float component : rotation) {
        lengthSquared += static_cast<double>(component) * component;
    }
    const double length = std::sqrt(lengthSquared);
    // Left alone when there is nothing to divide by: the caller's boundary
    // check has already refused a zero or non-finite rotation, and repairing
    // one here would put an identity where a refusal belongs.
    if (!std::isfinite(length) || length <= 0.0) {
        return pxr::GfQuatf(
            rotation[3],
            pxr::GfVec3f(rotation[0], rotation[1], rotation[2]));
    }
    const double inverse = 1.0 / length;
    return pxr::GfQuatf(
        static_cast<float>(rotation[3] * inverse),
        pxr::GfVec3f(static_cast<float>(rotation[0] * inverse),
                     static_cast<float>(rotation[1] * inverse),
                     static_cast<float>(rotation[2] * inverse)));
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
               RigRefused(
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
                   RigRefused(DiagnosticCode::UnsupportedJoint, joint.boneId,
                              "the measured rig carries bone "
                                  + std::to_string(expectedId)
                                  + " in this position"));
            return false;
        }
        if (joint.parentBoneId != MeasuredParentColumn[index]) {
            Report(diagnostics,
                   RigRefused(
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
                   RigRefused(DiagnosticCode::NonFiniteTransform, joint.boneId,
                              "a rest transform that names no orientation "
                              "leaves every bone below it unplaced"));
            return false;
        }
    }

    // A rig longer than the measured one keeps its leading joints — the loop
    // above has just proved they are the measured rig — and says here that it is
    // sending joints whose meaning nobody has measured. Once per skeleton
    // packet, which a device sends about every 3.5 s, rather than once per
    // frame at sixty a second: a longer rig is a property of the rig, so a
    // caller that rebuilds only when the table changed reports it once.
    //
    // Their ids are checked too, and this is the one thing that can still refuse
    // a rig here: a trailing joint claiming an id inside the measured range has
    // broken the identity every id above rests on, and the diagnostic would
    // otherwise name `bone 5` while bone 5 is a torso segment sitting where it
    // always was.
    for (std::size_t index = MeasuredBoneCount; index < skeleton.bones.size();
         ++index) {
        const BoneDefinition& joint = skeleton.bones[index];
        if (IsMeasuredJoint(joint.boneId)) {
            Report(diagnostics,
                   RigRefused(DiagnosticCode::UnsupportedJoint, joint.boneId,
                              "a joint beyond the measured rig claims an id "
                              "inside it, so an id is no longer a position"));
            return false;
        }
        Report(diagnostics,
               JointDiagnostic(DiagnosticCode::UnsupportedJoint, joint.boneId,
                               "beyond the measured rig, so this map carries no "
                               "canonical bone for it"));
    }

    SkeletonMap map;
    map.jointCount = skeleton.bones.size();
    for (std::size_t jointId = 0; jointId < MeasuredBoneCount; ++jointId) {
        map.jointRestTranslations[jointId] =
            skeleton.bones[jointId].restTransform.translation;
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
        const JointPath& path = kPaths[jointId];
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
        // A joint outside the measured rig, or one the session's own skeleton
        // packet never declared. A map nobody built declares none, so it lands
        // here for every record and produces an empty mapping rather than
        // reading a rig it does not have.
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
            // *usable* record is the one used, and the repeat is counted rather
            // than silently overwriting it. A first record that named no
            // orientation was refused below and kept nothing, so a later good
            // one still lands: a refused record is not a record.
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

    // Composed by joint id — the order the paths are stated in — and emitted by
    // canonical bone, which is not the same order and is the one `bones` says it
    // is in. This rig runs both arms off the torso where the humanoid vocabulary
    // numbers the legs first, so a single joint-id walk would emit every arm
    // ahead of every leg. That is the kind of difference a consumer merging two
    // sorted lists finds as one silently wrong sample rather than as a failure.
    std::array<pxr::GfQuatf, motion::HumanBoneCount> composed;
    for (std::uint16_t jointId = 0; jointId < MeasuredBoneCount; ++jointId) {
        const std::optional<HumanBone> bone = map.Bone(jointId);
        if (!bone) {
            continue;
        }
        const JointPath& path = kPaths[jointId];
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
        const auto slot = static_cast<std::size_t>(*bone);
        composed[slot] = rotation;
        mapping.present.set(slot);
    }

    mapping.bones.reserve(mapping.present.count());
    for (std::size_t slot = 0; slot < motion::HumanBoneCount; ++slot) {
        if (!mapping.present.test(slot)) {
            continue;
        }
        BoneSample sample;
        sample.bone = static_cast<HumanBone>(slot);
        sample.localRotation = composed[slot];
        mapping.bones.push_back(sample);
    }

    const bool mapped = !mapping.bones.empty();
    *out = std::move(mapping);
    return mapped;
}

} // namespace vrmAdapterMocopi
