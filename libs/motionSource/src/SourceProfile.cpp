// SPDX-License-Identifier: Apache-2.0
#include "motionSource/SourceProfile.h"

#include <algorithm>
#include <array>
#include <bitset>
#include <cstdio>
#include <string>
#include <utility>

namespace motionSource
{
namespace
{

constexpr std::size_t kNoJoint = static_cast<std::size_t>(-1);

std::size_t
BoneIndex(motion::HumanBone bone) noexcept
{
    return static_cast<std::size_t>(bone);
}

char
LowerAscii(char c) noexcept
{
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

bool
EqualsIgnoreCase(std::string_view lhs, std::string_view rhs) noexcept
{
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (std::size_t i = 0; i < lhs.size(); ++i) {
        if (LowerAscii(lhs[i]) != LowerAscii(rhs[i])) {
            return false;
        }
    }
    return true;
}

bool
Fail(std::string* reason, std::string text)
{
    if (reason) {
        *reason = std::move(text);
    }
    return false;
}

// The one table per vocabulary. `Name` indexes it and `Find` scans it, so the
// canonical spelling and the accepted one cannot drift apart -- an alias lives
// beside the entry it aliases rather than in a second switch.
//
// Each table is a `std::array` sized by its enum's `Count` and asserted to be in
// enumerator order, which is what turns two silent failures into compile errors:
// an enumerator appended without a row would otherwise return an empty name and
// never be found by any spelling, and a row inserted in the wrong place would
// return a neighbour's. Seven vocabularies is too many to keep in step by
// attention, and the sibling table in SourceAnimation.cpp is sized the same way
// for the same reason.
template <typename Enum>
struct Term
{
    Enum value;
    std::string_view name;
    std::string_view alias; // empty when there is none
};

template <typename Enum, std::size_t N>
constexpr bool
TermsAreInEnumOrder(const std::array<Term<Enum>, N>& terms) noexcept
{
    for (std::size_t index = 0; index < N; ++index) {
        if (static_cast<std::size_t>(terms[index].value) != index) {
            return false;
        }
    }
    return true;
}

template <typename Enum, std::size_t N>
std::string_view
NameOf(const std::array<Term<Enum>, N>& terms, Enum value) noexcept
{
    const auto index = static_cast<std::size_t>(value);
    return index < N ? terms[index].name : std::string_view();
}

template <typename Enum, std::size_t N>
std::optional<Enum>
FindIn(const std::array<Term<Enum>, N>& terms, std::string_view name) noexcept
{
    for (const Term<Enum>& term : terms) {
        if (EqualsIgnoreCase(term.name, name)
            || (!term.alias.empty() && EqualsIgnoreCase(term.alias, name))) {
            return term.value;
        }
    }
    return std::nullopt;
}

constexpr std::array<Term<SourceHandedness>, SourceHandednessCount>
    kHandedness = {{
    {SourceHandedness::Unspecified, "unspecified", {}},
    {SourceHandedness::Right, "right", {}},
    {SourceHandedness::Left, "left", {}},
}};
static_assert(TermsAreInEnumOrder(kHandedness),
              "kHandedness must be in enumerator order");

// The alias column is the unsigned spelling: a profile writing `Y` means `+Y`
// (SourceProfile.h).
constexpr std::array<Term<SourceAxis>, SourceAxisCount> kAxes = {{
    {SourceAxis::Unspecified, "unspecified", {}},
    {SourceAxis::PlusX, "+X", "X"},
    {SourceAxis::MinusX, "-X", {}},
    {SourceAxis::PlusY, "+Y", "Y"},
    {SourceAxis::MinusY, "-Y", {}},
    {SourceAxis::PlusZ, "+Z", "Z"},
    {SourceAxis::MinusZ, "-Z", {}},
}};
static_assert(TermsAreInEnumOrder(kAxes),
              "kAxes must be in enumerator order");

constexpr std::array<Term<SourceLengthUnit>, SourceLengthUnitCount>
    kLengthUnits = {{
    {SourceLengthUnit::Unspecified, "unspecified", {}},
    {SourceLengthUnit::Meters, "meters", "metres"},
    {SourceLengthUnit::Centimeters, "centimeters", "centimetres"},
    {SourceLengthUnit::Millimeters, "millimeters", "millimetres"},
    {SourceLengthUnit::Inches, "inches", {}},
}};
static_assert(TermsAreInEnumOrder(kLengthUnits),
              "kLengthUnits must be in enumerator order");

constexpr std::array<Term<RootTranslationPolicy>, RootTranslationPolicyCount>
    kRootTranslationPolicies = {{
    {RootTranslationPolicy::Unspecified, "unspecified", {}},
    {RootTranslationPolicy::AbsolutePosition, "absolute-position", {}},
    {RootTranslationPolicy::RestRelative, "rest-relative", {}},
    {RootTranslationPolicy::None, "none", {}},
}};
static_assert(TermsAreInEnumOrder(kRootTranslationPolicies),
              "kRootTranslationPolicies must be in enumerator order");

constexpr std::array<Term<RootRotationPolicy>, RootRotationPolicyCount>
    kRootRotationPolicies = {{
    {RootRotationPolicy::Unspecified, "unspecified", {}},
    {RootRotationPolicy::BodyOrientation, "body-orientation", {}},
    {RootRotationPolicy::None, "none", {}},
}};
static_assert(TermsAreInEnumOrder(kRootRotationPolicies),
              "kRootRotationPolicies must be in enumerator order");

constexpr std::array<Term<RestPoseSource>, RestPoseSourceCount>
    kRestPoseSources = {{
    {RestPoseSource::Unspecified, "unspecified", {}},
    {RestPoseSource::RestOffsets, "rest-offsets", {}},
    {RestPoseSource::StatedRestRotations, "stated-rest-rotations", {}},
    {RestPoseSource::FirstFrame, "first-frame", {}},
}};
static_assert(TermsAreInEnumOrder(kRestPoseSources),
              "kRestPoseSources must be in enumerator order");

constexpr std::array<Term<UnmappedJointPolicy>, UnmappedJointPolicyCount>
    kUnmappedJointPolicies = {{
    {UnmappedJointPolicy::Unspecified, "unspecified", {}},
    {UnmappedJointPolicy::Ignore, "ignore", {}},
    {UnmappedJointPolicy::Report, "report", {}},
    {UnmappedJointPolicy::Refuse, "refuse", {}},
}};
static_assert(TermsAreInEnumOrder(kUnmappedJointPolicies),
              "kUnmappedJointPolicies must be in enumerator order");

constexpr std::array<Term<SourceProfileRefusal>, SourceProfileRefusalCount>
    kRefusals = {{
    {SourceProfileRefusal::None, "none", {}},
    {SourceProfileRefusal::ProfileInvalid, "profile-invalid", {}},
    {SourceProfileRefusal::SkeletonInvalid, "skeleton-invalid", {}},
    {SourceProfileRefusal::RootJointMismatch, "root-joint-mismatch", {}},
    {SourceProfileRefusal::AmbiguousJointName, "ambiguous-joint-name", {}},
    {SourceProfileRefusal::RequiredJointMissing, "required-joint-missing", {}},
    {SourceProfileRefusal::HierarchyMismatch, "hierarchy-mismatch", {}},
    {SourceProfileRefusal::UnmappedJointRefused, "unmapped-joint-refused", {}},
}};
static_assert(TermsAreInEnumOrder(kRefusals),
              "kRefusals must be in enumerator order");

// Whether `ancestor` is above `descendant` in the rig. Bounded by the joint
// count rather than by reaching the root: this runs over rigs a caller
// assembled, and a parent chain that cycles is one `ValidateSourceSkeleton`
// rejects but this function may still be handed.
bool
IsAncestorJoint(const SourceSkeleton& skeleton, std::size_t ancestor,
                std::size_t descendant) noexcept
{
    if (ancestor >= skeleton.joints.size()
        || descendant >= skeleton.joints.size()) {
        return false;
    }
    std::size_t current = descendant;
    for (std::size_t step = 0; step < skeleton.joints.size(); ++step) {
        const int parent = skeleton.joints[current].parent;
        if (parent < 0) {
            return false;
        }
        const std::size_t parentIndex = static_cast<std::size_t>(parent);
        if (parentIndex >= skeleton.joints.size()) {
            return false;
        }
        if (parentIndex == ancestor) {
            return true;
        }
        current = parentIndex;
    }
    return false;
}

std::string
JointLabel(const SourceSkeleton& skeleton, std::size_t index)
{
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "joint %zu", index);
    return std::string(buffer) + " '" + skeleton.joints[index].name + "'";
}

std::string
MappingLabel(std::size_t index, const SourceJointMapping& mapping)
{
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "mapping %zu", index);
    return std::string(buffer) + " '" + mapping.sourceName + "'";
}

} // namespace

std::string_view
SourceHandednessName(SourceHandedness handedness) noexcept
{
    return NameOf(kHandedness, handedness);
}

std::optional<SourceHandedness>
FindSourceHandedness(std::string_view name) noexcept
{
    return FindIn(kHandedness, name);
}

std::string_view
SourceAxisName(SourceAxis axis) noexcept
{
    return NameOf(kAxes, axis);
}

std::optional<SourceAxis>
FindSourceAxis(std::string_view name) noexcept
{
    return FindIn(kAxes, name);
}

std::optional<int>
SourceAxisComponent(SourceAxis axis) noexcept
{
    switch (axis) {
    case SourceAxis::PlusX:
    case SourceAxis::MinusX:
        return 0;
    case SourceAxis::PlusY:
    case SourceAxis::MinusY:
        return 1;
    case SourceAxis::PlusZ:
    case SourceAxis::MinusZ:
        return 2;
    default:
        return std::nullopt;
    }
}

bool
SourceAxisIsNegative(SourceAxis axis) noexcept
{
    return axis == SourceAxis::MinusX || axis == SourceAxis::MinusY
           || axis == SourceAxis::MinusZ;
}

std::string_view
SourceLengthUnitName(SourceLengthUnit unit) noexcept
{
    return NameOf(kLengthUnits, unit);
}

std::optional<SourceLengthUnit>
FindSourceLengthUnit(std::string_view name) noexcept
{
    return FindIn(kLengthUnits, name);
}

std::optional<double>
SourceLengthUnitInMeters(SourceLengthUnit unit) noexcept
{
    switch (unit) {
    case SourceLengthUnit::Meters:
        return 1.0;
    case SourceLengthUnit::Centimeters:
        return 0.01;
    case SourceLengthUnit::Millimeters:
        return 0.001;
    case SourceLengthUnit::Inches:
        // The international inch, exactly. Not a measurement: the definition.
        return 0.0254;
    default:
        return std::nullopt;
    }
}

std::string_view
RootTranslationPolicyName(RootTranslationPolicy policy) noexcept
{
    return NameOf(kRootTranslationPolicies, policy);
}

std::optional<RootTranslationPolicy>
FindRootTranslationPolicy(std::string_view name) noexcept
{
    return FindIn(kRootTranslationPolicies, name);
}

std::string_view
RootRotationPolicyName(RootRotationPolicy policy) noexcept
{
    return NameOf(kRootRotationPolicies, policy);
}

std::optional<RootRotationPolicy>
FindRootRotationPolicy(std::string_view name) noexcept
{
    return FindIn(kRootRotationPolicies, name);
}

std::string_view
RestPoseSourceName(RestPoseSource source) noexcept
{
    return NameOf(kRestPoseSources, source);
}

std::optional<RestPoseSource>
FindRestPoseSource(std::string_view name) noexcept
{
    return FindIn(kRestPoseSources, name);
}

std::string_view
UnmappedJointPolicyName(UnmappedJointPolicy policy) noexcept
{
    return NameOf(kUnmappedJointPolicies, policy);
}

std::optional<UnmappedJointPolicy>
FindUnmappedJointPolicy(std::string_view name) noexcept
{
    return FindIn(kUnmappedJointPolicies, name);
}

std::string_view
SourceProfileRefusalName(SourceProfileRefusal refusal) noexcept
{
    return NameOf(kRefusals, refusal);
}

bool
operator==(const SourceJointMapping& lhs,
           const SourceJointMapping& rhs) noexcept
{
    return lhs.sourceName == rhs.sourceName && lhs.bone == rhs.bone
           && lhs.required == rhs.required;
}

bool
operator!=(const SourceJointMapping& lhs,
           const SourceJointMapping& rhs) noexcept
{
    return !(lhs == rhs);
}

bool
operator==(const SourceProfile& lhs, const SourceProfile& rhs) noexcept
{
    return lhs.id == rhs.id && lhs.producer == rhs.producer
           && lhs.rootJoint == rhs.rootJoint
           && lhs.handedness == rhs.handedness && lhs.upAxis == rhs.upAxis
           && lhs.forwardAxis == rhs.forwardAxis
           && lhs.translationUnit == rhs.translationUnit
           && lhs.rootTranslation == rhs.rootTranslation
           && lhs.rootRotation == rhs.rootRotation
           && lhs.restPose == rhs.restPose
           && lhs.unmappedJoints == rhs.unmappedJoints
           && lhs.joints == rhs.joints
           && lhs.ignoredJoints == rhs.ignoredJoints;
}

bool
operator!=(const SourceProfile& lhs, const SourceProfile& rhs) noexcept
{
    return !(lhs == rhs);
}

std::optional<std::size_t>
SourceProfile::FindMapping(std::string_view sourceName) const
{
    for (std::size_t i = 0; i < joints.size(); ++i) {
        if (joints[i].sourceName == sourceName) {
            return i;
        }
    }
    return std::nullopt;
}

std::optional<std::size_t>
SourceProfile::FindBoneMapping(motion::HumanBone bone) const
{
    for (std::size_t i = 0; i < joints.size(); ++i) {
        if (joints[i].bone == bone) {
            return i;
        }
    }
    return std::nullopt;
}

bool
SourceProfile::IgnoresJoint(std::string_view sourceName) const
{
    return std::find(ignoredJoints.begin(), ignoredJoints.end(), sourceName)
           != ignoredJoints.end();
}

std::size_t
SourceProfile::RequiredMappingCount() const noexcept
{
    std::size_t required = 0;
    for (const SourceJointMapping& mapping : joints) {
        if (mapping.required) {
            ++required;
        }
    }
    return required;
}

bool
ValidateSourceProfile(const SourceProfile& profile, std::string* reason)
{
    if (profile.id.empty()) {
        return Fail(reason, "profile has no id");
    }
    if (profile.producer.empty()) {
        return Fail(reason, "profile names no producer");
    }
    if (profile.rootJoint.empty()) {
        return Fail(reason, "profile names no root joint");
    }

    // Each convention separately, because "the profile is incomplete" sends a
    // reader back to the file to find out which line is missing.
    if (profile.handedness == SourceHandedness::Unspecified
        || profile.handedness >= SourceHandedness::Count) {
        return Fail(reason, "profile states no handedness");
    }
    if (SourceAxisComponent(profile.upAxis) == std::nullopt) {
        return Fail(reason, "profile states no up axis");
    }
    if (SourceAxisComponent(profile.forwardAxis) == std::nullopt) {
        return Fail(reason, "profile states no forward axis");
    }
    if (*SourceAxisComponent(profile.upAxis)
        == *SourceAxisComponent(profile.forwardAxis)) {
        // Two directions along one axis describe no basis at all, and the sign
        // is not the difference that saves it: `+Y` up with `-Y` forward is as
        // degenerate as `+Y` with `+Y`.
        return Fail(reason,
                    std::string("profile's up axis ")
                        + std::string(SourceAxisName(profile.upAxis))
                        + " and forward axis "
                        + std::string(SourceAxisName(profile.forwardAxis))
                        + " are the same axis");
    }
    if (SourceLengthUnitInMeters(profile.translationUnit) == std::nullopt) {
        return Fail(reason, "profile states no translation unit");
    }
    if (profile.rootTranslation == RootTranslationPolicy::Unspecified
        || profile.rootTranslation >= RootTranslationPolicy::Count) {
        return Fail(reason, "profile states no root translation policy");
    }
    if (profile.rootRotation == RootRotationPolicy::Unspecified
        || profile.rootRotation >= RootRotationPolicy::Count) {
        return Fail(reason, "profile states no root rotation policy");
    }
    if (profile.restPose == RestPoseSource::Unspecified
        || profile.restPose >= RestPoseSource::Count) {
        return Fail(reason, "profile states no rest pose source");
    }
    if (profile.unmappedJoints == UnmappedJointPolicy::Unspecified
        || profile.unmappedJoints >= UnmappedJointPolicy::Count) {
        return Fail(reason, "profile states no unmapped-joint policy");
    }

    if (profile.joints.empty()) {
        return Fail(reason, "profile maps no joints");
    }

    for (std::size_t i = 0; i < profile.joints.size(); ++i) {
        const SourceJointMapping& mapping = profile.joints[i];
        if (mapping.sourceName.empty()) {
            char buffer[48];
            std::snprintf(buffer, sizeof(buffer), "mapping %zu has no name", i);
            return Fail(reason, buffer);
        }
        if (!motion::IsValidHumanBone(mapping.bone)) {
            return Fail(reason,
                        MappingLabel(i, mapping) + " names no canonical bone");
        }
        for (std::size_t j = 0; j < i; ++j) {
            if (profile.joints[j].sourceName == mapping.sourceName) {
                return Fail(reason,
                            MappingLabel(i, mapping)
                                + " maps a name this profile already maps");
            }
            if (profile.joints[j].bone == mapping.bone) {
                // Two joints claiming one bone is two statements about one
                // rotation, with nothing in a conversion able to arbitrate.
                return Fail(reason,
                            MappingLabel(i, mapping) + " and "
                                + MappingLabel(j, profile.joints[j])
                                + " map the same bone "
                                + std::string(
                                    motion::HumanBoneName(mapping.bone)));
            }
        }
        if (profile.IgnoresJoint(mapping.sourceName)) {
            return Fail(reason,
                        MappingLabel(i, mapping)
                            + " is both mapped and ignored");
        }
    }

    for (std::size_t i = 0; i < profile.ignoredJoints.size(); ++i) {
        if (profile.ignoredJoints[i].empty()) {
            char buffer[56];
            std::snprintf(buffer, sizeof(buffer),
                          "ignored joint %zu has no name", i);
            return Fail(reason, buffer);
        }
        for (std::size_t j = 0; j < i; ++j) {
            if (profile.ignoredJoints[j] == profile.ignoredJoints[i]) {
                return Fail(reason,
                            "ignored joint '" + profile.ignoredJoints[i]
                                + "' is listed twice");
            }
        }
    }

    // The canonical humanoid roots at the hips, so a profile that does not bind
    // them describes a rig no conversion can place. Required rather than merely
    // present: a hips mapping the rig is allowed to omit is the same profile
    // with the refusal deferred to the converter.
    const std::optional<std::size_t> hips =
        profile.FindBoneMapping(motion::HumanBone::Hips);
    if (!hips) {
        return Fail(reason,
                    std::string("profile maps no ")
                        + std::string(
                            motion::HumanBoneName(motion::HumanBone::Hips)));
    }
    if (!profile.joints[*hips].required) {
        return Fail(reason,
                    MappingLabel(*hips, profile.joints[*hips])
                        + " maps the canonical root and is not required");
    }

    if (reason) {
        reason->clear();
    }
    return true;
}

std::optional<std::size_t>
SourceProfileMatch::JointFor(motion::HumanBone bone) const
{
    for (const SourceProfileBinding& binding : bound) {
        if (binding.bone == bone) {
            return binding.jointIndex;
        }
    }
    return std::nullopt;
}

std::size_t
SourceProfileMatch::BoundRequiredCount() const noexcept
{
    std::size_t count = 0;
    for (const SourceProfileBinding& binding : bound) {
        if (binding.required) {
            ++count;
        }
    }
    return count;
}

SourceProfileMatch
MatchSourceProfile(const SourceProfile& profile, const SourceSkeleton& skeleton)
{
    SourceProfileMatch match;

    std::string profileReason;
    const bool profileValid = ValidateSourceProfile(profile, &profileReason);
    std::string skeletonReason;
    const bool skeletonValid = ValidateSourceSkeleton(skeleton, &skeletonReason);

    // Everything countable first, and the refusal only afterwards: a detector
    // reports candidates that did *not* match, so a refusal that returned early
    // would leave it nothing to report with (SourceProfile.h).
    match.rootMatched = !skeleton.joints.empty()
                        && skeleton.joints[0].name == profile.rootJoint;

    std::bitset<motion::HumanBoneCount> boundBones;
    std::vector<std::size_t> boneJoint(motion::HumanBoneCount, kNoJoint);

    for (const SourceJointMapping& mapping : profile.joints) {
        if (!motion::IsValidHumanBone(mapping.bone)) {
            // An invalid profile, already refused above. Counting it as missing
            // would put `Count` in a vector of bones.
            continue;
        }
        const std::vector<std::size_t> found =
            skeleton.FindJoints(mapping.sourceName);
        if (found.size() > 1) {
            match.ambiguousNames.push_back(mapping.sourceName);
            continue;
        }
        if (found.empty()) {
            if (mapping.required) {
                match.missingRequired.push_back(mapping.bone);
            } else {
                match.missingOptional.push_back(mapping.bone);
            }
            continue;
        }
        match.bound.push_back(
            SourceProfileBinding{mapping.bone, found[0], mapping.required});
        boundBones.set(BoneIndex(mapping.bone));
        boneJoint[BoneIndex(mapping.bone)] = found[0];
    }

    for (std::size_t i = 0; i < skeleton.joints.size(); ++i) {
        const std::string& name = skeleton.joints[i].name;
        // By name, not by binding: a name the profile maps but the rig repeats
        // is ambiguous, which is a different refusal from unmapped.
        if (profile.FindMapping(name) || profile.IgnoresJoint(name)) {
            continue;
        }
        match.unmappedJoints.push_back(i);
    }

    std::string hierarchyDetail;
    for (const SourceProfileBinding& binding : match.bound) {
        const std::optional<motion::HumanBone> ancestorBone =
            motion::NearestPresentAncestor(binding.bone, boundBones);
        if (!ancestorBone) {
            continue;
        }
        const std::size_t ancestorJoint = boneJoint[BoneIndex(*ancestorBone)];
        if (IsAncestorJoint(skeleton, ancestorJoint, binding.jointIndex)) {
            continue;
        }
        if (hierarchyDetail.empty()) {
            hierarchyDetail =
                JointLabel(skeleton, binding.jointIndex) + " carries "
                + std::string(motion::HumanBoneName(binding.bone))
                + " but does not sit under "
                + JointLabel(skeleton, ancestorJoint) + ", which carries its "
                + "nearest bound ancestor "
                + std::string(motion::HumanBoneName(*ancestorBone));
        }
    }

    // The first refusal wins, outermost first, and the order is the contract
    // (SourceProfile.h).
    if (!profileValid) {
        match.refusal = SourceProfileRefusal::ProfileInvalid;
        match.detail = std::move(profileReason);
    } else if (!skeletonValid) {
        match.refusal = SourceProfileRefusal::SkeletonInvalid;
        match.detail = std::move(skeletonReason);
    } else if (!match.rootMatched) {
        match.refusal = SourceProfileRefusal::RootJointMismatch;
        match.detail = "profile roots at '" + profile.rootJoint
                       + "'; the rig roots at '" + skeleton.joints[0].name + "'";
    } else if (!match.ambiguousNames.empty()) {
        match.refusal = SourceProfileRefusal::AmbiguousJointName;
        match.detail = "the rig carries '" + match.ambiguousNames.front()
                       + "' more than once and the profile maps it";
    } else if (!match.missingRequired.empty()) {
        match.refusal = SourceProfileRefusal::RequiredJointMissing;
        match.detail =
            "the rig carries no joint for required bone "
            + std::string(motion::HumanBoneName(match.missingRequired.front()));
    } else if (!hierarchyDetail.empty()) {
        match.refusal = SourceProfileRefusal::HierarchyMismatch;
        match.detail = std::move(hierarchyDetail);
    } else if (!match.unmappedJoints.empty()
               && profile.unmappedJoints == UnmappedJointPolicy::Refuse) {
        match.refusal = SourceProfileRefusal::UnmappedJointRefused;
        match.detail = "the profile neither maps nor ignores "
                       + JointLabel(skeleton, match.unmappedJoints.front());
    } else {
        match.refusal = SourceProfileRefusal::None;
    }

    return match;
}

} // namespace motionSource
