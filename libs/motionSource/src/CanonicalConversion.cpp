// SPDX-License-Identifier: Apache-2.0

#include "motionSource/CanonicalConversion.h"

#include "motionSource/CanonicalMetadata.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <utility>

namespace motionSource
{

namespace
{

constexpr std::array<std::string_view, ConversionRefusalCount> kRefusalNames = {
    "none", "profile-mismatch", "animation-invalid",
    "unsupported-rotation-form",
};

constexpr double kPi = 3.14159265358979323846;

pxr::GfQuatf
Identity() noexcept
{
    return pxr::GfQuatf(1.0f, pxr::GfVec3f(0.0f));
}

// The sign of the permutation (a, b, c) of (0, 1, 2): +1 when it is reachable
// from the identity by an even number of swaps. Written as the product of the
// pairwise orderings rather than by counting swaps, because three elements make
// that a closed form and a loop here would be a loop to read.
int
PermutationSign(int a, int b, int c) noexcept
{
    int sign = 1;
    if (a > b) {
        sign = -sign;
    }
    if (b > c) {
        sign = -sign;
    }
    if (a > c) {
        sign = -sign;
    }
    return sign;
}

double
AngleInRadians(float angle, SourceAngleUnit unit) noexcept
{
    const double value = static_cast<double>(angle);
    return unit == SourceAngleUnit::Degrees ? value * kPi / 180.0 : value;
}

SourceQuat
AxisRotation(int axis, double radians) noexcept
{
    const double half = radians * 0.5;
    const auto sine = static_cast<float>(std::sin(half));
    SourceQuat out;
    out.w = static_cast<float>(std::cos(half));
    out.x = axis == 0 ? sine : 0.0f;
    out.y = axis == 1 ? sine : 0.0f;
    out.z = axis == 2 ? sine : 0.0f;
    return out;
}

// Hamilton product in the source's own component space, with OpenUSD's
// convention: `Multiply(a, b)` applies `b` first.
SourceQuat
Multiply(const SourceQuat& a, const SourceQuat& b) noexcept
{
    SourceQuat out;
    out.w = a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z;
    out.x = a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y;
    out.y = a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x;
    out.z = a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w;
    return out;
}

// The rotation `track` states for `frame`, in the source's own terms. Identity
// for a track that states none, which is what an unrotated joint on the path
// between two mapped ones contributes.
SourceQuat
SourceRotationAt(const SourceJointTrack& track, std::size_t frame) noexcept
{
    if (frame >= track.eulerAngles.size()) {
        return SourceQuat();
    }
    return ComposeSourceRotation(track.eulerAngles[frame], track.eulerOrder,
                                 track.angleUnit);
}

// The chain from just below `jointIndex`'s nearest bound ancestor down to
// `jointIndex`, root-first. A joint with no bound ancestor gets the whole chain
// from the rig's root, which is what puts a scene node's rotation on the first
// bound bone below it — and is exactly why `RootRotationPolicy::None` has to
// act before this walk rather than after it.
std::vector<std::size_t>
PathFromNearestBoundAncestor(const SourceSkeleton& skeleton,
                             std::size_t jointIndex,
                             const std::vector<bool>& bound)
{
    std::vector<std::size_t> path;
    std::size_t walker = jointIndex;
    while (true) {
        path.push_back(walker);
        const int parent = skeleton.joints[walker].parent;
        if (parent < 0) {
            break;
        }
        const auto parentIndex = static_cast<std::size_t>(parent);
        if (bound[parentIndex]) {
            break;
        }
        walker = parentIndex;
    }
    std::reverse(path.begin(), path.end());
    return path;
}

SourceConversion
Refuse(SourceConversion result, ConversionRefusal refusal, std::string detail)
{
    result.refusal = refusal;
    result.detail = std::move(detail);
    return result;
}

} // namespace

std::string_view
ConversionRefusalName(ConversionRefusal refusal) noexcept
{
    const auto index = static_cast<std::size_t>(refusal);
    return index < kRefusalNames.size() ? kRefusalNames[index]
                                        : std::string_view();
}

CanonicalRestPose::CanonicalRestPose()
{
    // Exported rather than inline so every consumer receives the same identity
    // defaults, the way `motion::HumanoidPose` does.
    localRotations.fill(Identity());
    localTranslations.fill(pxr::GfVec3f(0.0f));
}

SourceConversion::SourceConversion() = default;

std::optional<CanonicalBasis>
MakeCanonicalBasis(const SourceProfile& profile)
{
    const std::optional<int> upComponent = SourceAxisComponent(profile.upAxis);
    const std::optional<int> forwardComponent =
        SourceAxisComponent(profile.forwardAxis);
    const std::optional<double> unit =
        SourceLengthUnitInMeters(profile.translationUnit);
    if (!upComponent || !forwardComponent || !unit
        || *upComponent == *forwardComponent
        || profile.handedness == SourceHandedness::Unspecified
        || profile.handedness >= SourceHandedness::Count) {
        return std::nullopt;
    }

    // Canonical +Y is the source's up and canonical +Z is its forward; the
    // remaining component is whichever of the three neither of them used.
    const int sideComponent = 3 - *upComponent - *forwardComponent;
    const int upSign = SourceAxisIsNegative(profile.upAxis) ? -1 : 1;
    const int forwardSign = SourceAxisIsNegative(profile.forwardAxis) ? -1 : 1;
    const int target =
        profile.handedness == SourceHandedness::Right ? 1 : -1;
    // The third row's sign is not free: it is whatever makes the determinant
    // come out at `target`, which is how the mirror a left-handed source needs
    // gets applied without a second step to forget. See CanonicalConversion.h.
    const int sideSign = target
                         * PermutationSign(sideComponent, *upComponent,
                                           *forwardComponent)
                         * upSign * forwardSign;

    CanonicalBasis basis;
    basis.component = {sideComponent, *upComponent, *forwardComponent};
    basis.negate = {sideSign < 0, upSign < 0, forwardSign < 0};
    basis.determinant = target;
    basis.scale = *unit / CanonicalUnitInMeters;
    return basis;
}

pxr::GfVec3f
ConvertPosition(const CanonicalBasis& basis, const SourceVec3& value)
{
    const float components[3] = {value.x, value.y, value.z};
    pxr::GfVec3f out(0.0f);
    for (std::size_t index = 0; index < 3; ++index) {
        const int source = basis.component[index];
        if (source < 0 || source > 2) {
            continue;
        }
        const double read = static_cast<double>(components[source]);
        out[static_cast<int>(index)] = static_cast<float>(
            (basis.negate[index] ? -read : read) * basis.scale);
    }
    return out;
}

pxr::GfQuatf
ConvertRotation(const CanonicalBasis& basis, const SourceQuat& value)
{
    const float components[3] = {value.x, value.y, value.z};
    pxr::GfVec3f imaginary(0.0f);
    for (std::size_t index = 0; index < 3; ++index) {
        const int source = basis.component[index];
        if (source < 0 || source > 2) {
            continue;
        }
        const float read = components[source];
        imaginary[static_cast<int>(index)] = basis.negate[index] ? -read : read;
    }
    // A mirror negates the angle and leaves the axis where the permutation put
    // it, which for a quaternion is one sign on the imaginary part.
    if (basis.determinant < 0) {
        imaginary = -imaginary;
    }
    pxr::GfQuatf out(value.w, imaginary);
    const float length = out.GetLength();
    // Left alone when there is nothing to divide by: the validators this
    // conversion runs first have already refused a zero-magnitude rotation, and
    // repairing one here would put an identity where a refusal belongs.
    if (std::isfinite(length) && length > 0.0f) {
        out /= length;
    }
    return out;
}

SourceQuat
ComposeSourceRotation(const SourceEulerAngles& angles, SourceEulerOrder order,
                      SourceAngleUnit unit) noexcept
{
    const float values[3] = {angles.first, angles.second, angles.third};
    SourceQuat composed;
    for (std::size_t component = 0; component < 3; ++component) {
        const std::optional<int> axis = SourceEulerAxis(order, component);
        if (!axis) {
            return SourceQuat();
        }
        composed = Multiply(
            composed, AxisRotation(*axis, AngleInRadians(values[component],
                                                         unit)));
    }
    return composed;
}

SourceConversion
ConvertSourceToCanonical(const SourceSkeleton& skeleton,
                         const SourceAnimation& animation,
                         const SourceProfile& profile)
{
    SourceConversion result;
    result.match = MatchSourceProfile(profile, skeleton);
    if (!result.match.Matched()) {
        // Built before the move rather than in the argument list: argument
        // evaluation order is unspecified, so reading `result` beside a
        // `std::move(result)` is reading a moved-from value on some compilers
        // and not on others.
        std::string detail =
            result.match.detail.empty()
                ? std::string(SourceProfileRefusalName(result.match.refusal))
                : result.match.detail;
        return Refuse(std::move(result), ConversionRefusal::ProfileMismatch,
                      std::move(detail));
    }

    std::string reason;
    if (!ValidateSourceAnimation(animation, skeleton, &reason)) {
        return Refuse(std::move(result), ConversionRefusal::AnimationInvalid,
                      std::move(reason));
    }
    for (std::size_t index = 0; index < animation.tracks.size(); ++index) {
        if (!animation.tracks[index].rotations.empty()) {
            return Refuse(
                std::move(result), ConversionRefusal::UnsupportedRotationForm,
                "joint '" + skeleton.joints[index].name
                    + "' states its rotation as quaternions, which no reader "
                      "writes yet");
        }
    }

    const std::optional<CanonicalBasis> basis = MakeCanonicalBasis(profile);
    if (!basis) {
        // Unreachable through a matched profile -- `MatchSourceProfile` runs
        // `ValidateSourceProfile` first and every convention this needs is one
        // of its checks. Handled anyway, and reported as the profile's problem,
        // because the alternative is dereferencing an empty optional if that
        // ever stops being true.
        return Refuse(std::move(result), ConversionRefusal::ProfileMismatch,
                      "the profile states no convertible basis");
    }

    // Which rig joint carries which bone, and the inverse question the path
    // walk asks per joint.
    std::vector<motion::HumanBone> boneForJoint(
        skeleton.joints.size(), motion::HumanBone::Count);
    std::vector<bool> bound(skeleton.joints.size(), false);
    for (const SourceProfileBinding& binding : result.match.bound) {
        boneForJoint[binding.jointIndex] = binding.bone;
        bound[binding.jointIndex] = true;
        result.rest.present.set(static_cast<std::size_t>(binding.bone));
    }

    // The root joint is index 0 by `ValidateSourceSkeleton`, and the match has
    // already established that it is the joint the profile names.
    constexpr std::size_t kRootJoint = 0;
    const bool dropRootRotation =
        profile.rootRotation == RootRotationPolicy::None;

    // One path per bound joint, walked once: it serves the rest pose and every
    // frame, and two walks that can disagree would show up as a constant
    // per-bone offset that looks like a bad capture.
    struct BoundPath
    {
        motion::HumanBone bone = motion::HumanBone::Count;
        std::vector<std::size_t> joints;
    };
    std::vector<BoundPath> paths;
    paths.reserve(result.match.bound.size());
    for (const SourceProfileBinding& binding : result.match.bound) {
        BoundPath path;
        path.bone = binding.bone;
        path.joints =
            PathFromNearestBoundAncestor(skeleton, binding.jointIndex, bound);
        if (path.joints.size() > 1) {
            result.report.composedBones.push_back(binding.bone);
        }
        paths.push_back(std::move(path));
    }

    // --- the rest pose -----------------------------------------------------
    //
    // Which rotations are the rest is the profile's answer; composing them is
    // this file's, and it is the same composition the frames get.
    std::vector<pxr::GfQuatf> restRotations(skeleton.joints.size(), Identity());
    if (profile.restPose == RestPoseSource::StatedRestRotations) {
        for (std::size_t index = 0; index < skeleton.joints.size(); ++index) {
            if (skeleton.joints[index].restRotation) {
                restRotations[index] =
                    ConvertRotation(*basis, *skeleton.joints[index].restRotation);
            }
        }
    } else if (profile.restPose == RestPoseSource::FirstFrame
               && animation.frameCount > 0) {
        for (std::size_t index = 0; index < skeleton.joints.size(); ++index) {
            restRotations[index] = ConvertRotation(
                *basis, SourceRotationAt(animation.tracks[index], 0));
        }
    }
    if (dropRootRotation) {
        restRotations[kRootJoint] = Identity();
    }

    std::vector<pxr::GfVec3f> restOffsets;
    restOffsets.reserve(skeleton.joints.size());
    for (const SourceJoint& joint : skeleton.joints) {
        restOffsets.push_back(ConvertPosition(*basis, joint.restTranslation));
    }

    for (const BoundPath& path : paths) {
        const auto slot = static_cast<std::size_t>(path.bone);
        pxr::GfQuatf rotation = Identity();
        pxr::GfVec3f translation(0.0f);
        // Root-first, so each joint's own offset is stated in the frame the
        // rotations composed so far have already established.
        for (const std::size_t jointIndex : path.joints) {
            translation += rotation.Transform(restOffsets[jointIndex]);
            rotation = rotation * restRotations[jointIndex];
        }
        result.rest.localRotations[slot] = rotation;
        result.rest.localTranslations[slot] = translation;
    }

    // --- what the clip could not carry -------------------------------------
    for (std::size_t index = 0; index < skeleton.joints.size(); ++index) {
        const SourceJointTrack& track = animation.tracks[index];
        if (!track.HasTranslation()) {
            continue;
        }
        const bool isRoot = index == kRootJoint;
        const bool carried =
            isRoot && profile.rootTranslation != RootTranslationPolicy::None;
        if (carried) {
            continue;
        }
        const SourceVec3& rest = skeleton.joints[index].restTranslation;
        const bool restated =
            std::all_of(track.translations.begin(), track.translations.end(),
                        [&rest](const SourceVec3& value) {
                            return value == rest;
                        });
        if (restated) {
            result.report.restatedTranslationJoints.push_back(index);
        } else {
            result.report.droppedTranslationJoints.push_back(index);
        }
    }

    // --- the frames --------------------------------------------------------
    motion::HumanoidAnimation& clip = result.animation;
    clip.startTime = animation.startTime;
    clip.endTime = animation.EndTime();
    if (const std::optional<double> rate = animation.FrameRate()) {
        clip.nominalFrameRate = *rate;
    }

    result.provenance = animation.provenance;
    result.provenance.producer = profile.producer;
    result.provenance.profileId = profile.id;
    clip.source = CanonicalMetadata(result.provenance);

    const pxr::GfVec3f rootRest = restOffsets[kRootJoint];
    std::vector<pxr::GfQuatf> jointRotations(skeleton.joints.size());
    clip.samples.reserve(animation.frameCount);
    for (std::size_t frame = 0; frame < animation.frameCount; ++frame) {
        for (std::size_t index = 0; index < skeleton.joints.size(); ++index) {
            if (index == kRootJoint && dropRootRotation) {
                jointRotations[index] = Identity();
                continue;
            }
            jointRotations[index] = ConvertRotation(
                *basis, SourceRotationAt(animation.tracks[index], frame));
        }

        motion::HumanoidPose pose;
        if (const std::optional<double> time = animation.Time(frame)) {
            pose.timestamp = *time;
        }
        for (const BoundPath& path : paths) {
            pxr::GfQuatf rotation = Identity();
            for (const std::size_t jointIndex : path.joints) {
                rotation = rotation * jointRotations[jointIndex];
            }
            const auto slot = static_cast<std::size_t>(path.bone);
            pose.localRotations[slot] = rotation;
            pose.validRotations.set(slot);
        }

        const SourceJointTrack& rootTrack = animation.tracks[kRootJoint];
        if (profile.rootTranslation != RootTranslationPolicy::None
            && frame < rootTrack.translations.size()) {
            pxr::GfVec3f position =
                ConvertPosition(*basis, rootTrack.translations[frame]);
            if (profile.rootTranslation == RootTranslationPolicy::RestRelative) {
                // Both policies land on the same canonical thing -- an absolute
                // position in the clip's own space -- and this is the only line
                // that differs between them.
                position += rootRest;
            }
            pose.root.worldPosition = position;
            pose.root.hasPosition = true;
        }
        if (!dropRootRotation) {
            pose.root.worldOrientation = jointRotations[kRootJoint];
            pose.root.hasOrientation = true;
        }
        // `pose.source` stays absent. It cannot vary within a clip, the
        // animation carries it once, and a per-sample copy is the cost
        // SourceProvenance.h argues against paying.
        clip.samples.push_back(std::move(pose));
    }

    result.refusal = ConversionRefusal::None;
    return result;
}

} // namespace motionSource
