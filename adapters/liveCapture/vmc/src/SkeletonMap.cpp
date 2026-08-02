// SPDX-License-Identifier: Apache-2.0

#include "vrmAdapterVmc/SkeletonMap.h"

#include <cmath>
#include <cstddef>
#include <string>
#include <utility>

namespace vrmAdapterVmc
{

namespace
{

// Unity's `HumanBodyBones` spelling, in `motion::HumanBone` order. Written out
// rather than derived from the VRM 1.0 names, because three of the entries are
// not the same word: the thumb chain (see SkeletonMap.h). A generated table
// would be right for fifty-two bones and silently wrong for the other three,
// which is the worst of the two failure modes available here.
constexpr std::array<std::string_view, motion::HumanBoneCount> kVmcBoneNames = {
    "Hips", "Spine", "Chest", "UpperChest", "Neck", "Head", "LeftEye",
    "RightEye", "Jaw",

    "LeftUpperLeg", "LeftLowerLeg", "LeftFoot", "LeftToes", "RightUpperLeg",
    "RightLowerLeg", "RightFoot", "RightToes",

    "LeftShoulder", "LeftUpperArm", "LeftLowerArm", "LeftHand",
    "RightShoulder", "RightUpperArm", "RightLowerArm", "RightHand",

    // leftThumbMetacarpal, leftThumbProximal, leftThumbDistal.
    "LeftThumbProximal", "LeftThumbIntermediate", "LeftThumbDistal",
    "LeftIndexProximal", "LeftIndexIntermediate", "LeftIndexDistal",
    "LeftMiddleProximal", "LeftMiddleIntermediate", "LeftMiddleDistal",
    "LeftRingProximal", "LeftRingIntermediate", "LeftRingDistal",
    "LeftLittleProximal", "LeftLittleIntermediate", "LeftLittleDistal",

    // rightThumbMetacarpal, rightThumbProximal, rightThumbDistal.
    "RightThumbProximal", "RightThumbIntermediate", "RightThumbDistal",
    "RightIndexProximal", "RightIndexIntermediate", "RightIndexDistal",
    "RightMiddleProximal", "RightMiddleIntermediate", "RightMiddleDistal",
    "RightRingProximal", "RightRingIntermediate", "RightRingDistal",
    "RightLittleProximal", "RightLittleIntermediate", "RightLittleDistal",
};

static_assert(kVmcBoneNames.size() == motion::HumanBoneCount,
              "the VMC bone vocabulary must cover the complete humanoid");

bool
Refuse(Diagnostic* diagnostic, DiagnosticCode code, std::string_view subject,
       std::string detail)
{
    if (diagnostic) {
        *diagnostic = MakeDiagnostic(code, std::move(detail));
        diagnostic->subject.assign(subject);
    }
    return false;
}

bool
IsFinite(const std::array<float, 3>& position) noexcept
{
    return std::isfinite(position[0]) && std::isfinite(position[1])
        && std::isfinite(position[2]);
}

bool
IsFinite(const std::array<float, 4>& rotation) noexcept
{
    return std::isfinite(rotation[0]) && std::isfinite(rotation[1])
        && std::isfinite(rotation[2]) && std::isfinite(rotation[3]);
}

// The one value check both mapping functions share. `subject` is the bone name
// or the root's, so a refusal names what the sender was describing.
bool
CheckTransform(const VmcTransform& transform, std::string_view subject,
               Diagnostic* diagnostic)
{
    if (!IsFinite(transform.position)) {
        return Refuse(diagnostic, DiagnosticCode::PacketMalformed, subject,
                      "position is not finite");
    }
    if (!IsFinite(transform.rotation)) {
        return Refuse(diagnostic, DiagnosticCode::PacketMalformed, subject,
                      "rotation is not finite");
    }
    const double lengthSquared =
        static_cast<double>(transform.rotation[0]) * transform.rotation[0]
        + static_cast<double>(transform.rotation[1]) * transform.rotation[1]
        + static_cast<double>(transform.rotation[2]) * transform.rotation[2]
        + static_cast<double>(transform.rotation[3]) * transform.rotation[3];
    if (lengthSquared <= 0.0) {
        return Refuse(diagnostic, DiagnosticCode::PacketMalformed, subject,
                      "rotation has zero length and names no orientation");
    }
    return true;
}

} // namespace

std::string_view
VmcHumanBoneName(motion::HumanBone bone) noexcept
{
    if (!motion::IsValidHumanBone(bone)) {
        return {};
    }
    return kVmcBoneNames[static_cast<std::size_t>(bone)];
}

std::optional<motion::HumanBone>
FindVmcHumanBone(std::string_view name) noexcept
{
    for (std::size_t index = 0; index != kVmcBoneNames.size(); ++index) {
        if (kVmcBoneNames[index] == name) {
            return static_cast<motion::HumanBone>(index);
        }
    }
    return std::nullopt;
}

pxr::GfVec3f
ToCanonicalPosition(const std::array<float, 3>& position) noexcept
{
    return pxr::GfVec3f(-position[0], position[1], position[2]);
}

pxr::GfQuatf
ToCanonicalRotation(const std::array<float, 4>& rotation) noexcept
{
    // The wire order is (x, y, z, w); GfQuatf takes the real part first.
    pxr::GfQuatf converted(rotation[3], pxr::GfVec3f(rotation[0], -rotation[1],
                                                     -rotation[2]));
    const float length = converted.GetLength();
    // Left alone when there is nothing to divide by: the caller's boundary
    // check has already refused a zero or non-finite rotation, and repairing
    // one here would put an identity where a refusal belongs.
    if (std::isfinite(length) && length > 0.0f) {
        converted /= length;
    }
    return converted;
}

bool
MapVmcBoneTransform(const VmcMessage& message, VmcBoneSample* out,
                    Diagnostic* diagnostic)
{
    if (message.kind != VmcMessageKind::BoneTransform || out == nullptr) {
        return Refuse(diagnostic, DiagnosticCode::PacketMalformed,
                      VmcMessageKindAddress(VmcMessageKind::BoneTransform),
                      "not a bone transform, or no place to put one");
    }
    const std::optional<motion::HumanBone> bone =
        FindVmcHumanBone(message.name);
    if (!bone) {
        return Refuse(diagnostic, DiagnosticCode::UnsupportedMessage,
                      message.name,
                      "not a bone in the Unity humanoid vocabulary");
    }
    if (!CheckTransform(message.transform, message.name, diagnostic)) {
        return false;
    }
    out->bone = *bone;
    out->localRotation = ToCanonicalRotation(message.transform.rotation);
    out->localPosition = ToCanonicalPosition(message.transform.position);
    return true;
}

bool
MapVmcRootTransform(const VmcMessage& message, motion::RootMotion* out,
                    Diagnostic* diagnostic)
{
    if (message.kind != VmcMessageKind::RootTransform || out == nullptr) {
        return Refuse(diagnostic, DiagnosticCode::PacketMalformed,
                      VmcMessageKindAddress(VmcMessageKind::RootTransform),
                      "not a root transform, or no place to put one");
    }
    // The root's name is the sender's own label for it -- "root" in every
    // capture recorded so far -- and this layer neither checks it nor keeps it:
    // there is one root, and a sender that calls it something else has not said
    // anything a canonical value could carry.
    if (!CheckTransform(message.transform, message.name, diagnostic)) {
        return false;
    }
    // Assigned whole rather than field by field, so "the velocity fields are
    // left absent" stays true when a caller reuses one `RootMotion` across a
    // session instead of default-constructing per frame.
    motion::RootMotion root;
    root.worldPosition = ToCanonicalPosition(message.transform.position);
    root.hasPosition = true;
    root.worldOrientation = ToCanonicalRotation(message.transform.rotation);
    root.hasOrientation = true;
    *out = root;
    return true;
}

} // namespace vrmAdapterVmc
