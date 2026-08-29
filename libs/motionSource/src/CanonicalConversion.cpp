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

// Each entry carries its own enumerator, and the `static_assert` below is what
// makes that pay: a bare array of names sized by the enum accepts a short
// initialiser silently -- the tail value-initialises to an empty `string_view`,
// and a refusal added without its spelling comes back nameless at runtime
// instead of failing to build. This is `SourceProfile.cpp`'s shape for the same
// reason it uses it there, and having one vocabulary in this library weaker than
// the other seven is exactly the drift the pattern exists to stop.
struct RefusalTerm
{
    ConversionRefusal value = ConversionRefusal::None;
    std::string_view name;
};

constexpr std::array<RefusalTerm, ConversionRefusalCount> kRefusals = {{
    {ConversionRefusal::None, "none"},
    {ConversionRefusal::ProfileMismatch, "profile-mismatch"},
    {ConversionRefusal::AnimationInvalid, "animation-invalid"},
    {ConversionRefusal::UnsupportedRotationForm, "unsupported-rotation-form"},
}};

constexpr bool
RefusalsAreInEnumOrder() noexcept
{
    for (std::size_t index = 0; index < kRefusals.size(); ++index) {
        if (static_cast<std::size_t>(kRefusals[index].value) != index
            || kRefusals[index].name.empty()) {
            return false;
        }
    }
    return true;
}

static_assert(RefusalsAreInEnumOrder(),
              "kRefusals must be in enumerator order, one name each");

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

// Which way the bone *leaving* `bone` points when a humanoid stands in a
// T-pose, in canonical axes: +Y up, +Z forward, +X the character's left.
//
// Canonical vocabulary rather than a producer's answer -- every T-posed
// humanoid has its arms along the lateral axis and its legs down, which is what
// makes the pose worth naming. A bone this table has no direction for
// contributes identity, so a rig carrying fingers or a jaw is not refused; it
// simply gets no T-pose opinion about them.
pxr::GfVec3f
TPoseDirection(motion::HumanBone bone) noexcept
{
    const pxr::GfVec3f up(0.0f, 1.0f, 0.0f);
    const pxr::GfVec3f left(1.0f, 0.0f, 0.0f);
    const pxr::GfVec3f forward(0.0f, 0.0f, 1.0f);
    switch (bone) {
        case motion::HumanBone::Hips:
        case motion::HumanBone::Spine:
        case motion::HumanBone::Chest:
        case motion::HumanBone::UpperChest:
        case motion::HumanBone::Neck:
            return up;
        case motion::HumanBone::LeftShoulder:
        case motion::HumanBone::LeftUpperArm:
        case motion::HumanBone::LeftLowerArm:
            return left;
        case motion::HumanBone::RightShoulder:
        case motion::HumanBone::RightUpperArm:
        case motion::HumanBone::RightLowerArm:
            return -left;
        case motion::HumanBone::LeftUpperLeg:
        case motion::HumanBone::LeftLowerLeg:
        case motion::HumanBone::RightUpperLeg:
        case motion::HumanBone::RightLowerLeg:
            return -up;
        case motion::HumanBone::LeftFoot:
        case motion::HumanBone::RightFoot:
            return forward;
        // The chain-ending bones. Their own segment still has a direction in a
        // T-pose -- a skull rises, a hand continues outward, a toe points
        // ahead -- and stating it is what lets the walk above recognise which
        // child *continues* a bone when the alternatives leave it sideways. A
        // rig that ends a chain here simply has no child to aim.
        case motion::HumanBone::Head:
            return up;
        case motion::HumanBone::LeftHand:
            return left;
        case motion::HumanBone::RightHand:
            return -left;
        case motion::HumanBone::LeftToes:
        case motion::HumanBone::RightToes:
            return forward;
        default:
            return pxr::GfVec3f(0.0f);
    }
}

// The shortest rotation taking `from` to `to`, both unit-length. Shortest
// rather than any rotation that satisfies the constraint, because the
// difference between them is a twist about the bone's own axis -- which is
// exactly the freedom a T-pose does not pin, and which would show up as a
// forearm or shin rotated about itself while every joint position stayed right.
pxr::GfQuatf
ShortestRotation(const pxr::GfVec3f& from, const pxr::GfVec3f& to) noexcept
{
    const float dot = pxr::GfDot(from, to);
    if (dot > 0.999999f) {
        return Identity();
    }
    if (dot < -0.999999f) {
        // Opposed: any axis perpendicular to `from` is a half turn, and one has
        // to be chosen. Taking the larger cross product of the two coordinate
        // axes keeps it away from the degenerate one.
        pxr::GfVec3f axis = pxr::GfCross(from, pxr::GfVec3f(1.0f, 0.0f, 0.0f));
        if (axis.GetLength() < 1e-3f) {
            axis = pxr::GfCross(from, pxr::GfVec3f(0.0f, 1.0f, 0.0f));
        }
        axis.Normalize();
        return pxr::GfQuatf(0.0f, axis);
    }
    const pxr::GfVec3f axis = pxr::GfCross(from, to);
    pxr::GfQuatf out(1.0f + dot, axis);
    out.Normalize();
    return out;
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
    return index < kRefusals.size() ? kRefusals[index].name : std::string_view();
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
    // The length is formed in double and the division is done in it too, then
    // narrowed. That is load-bearing rather than tidy: `GfQuatf::GetLength()`
    // squares in float, so a quaternion whose components sit near the denormal
    // floor -- and the validators only ask that they are not all exactly zero,
    // so they admit every one of them -- underflows to a length of
    // exactly zero and comes back un-normalised, collapsing every composition it
    // then enters. Narrowing after the divide instead of before it keeps that
    // whole range representable. This project has already paid once for two
    // magnitudes formed in different precisions (motionCore/Compare.h).
    const double parts[4] = {static_cast<double>(imaginary[0]),
                             static_cast<double>(imaginary[1]),
                             static_cast<double>(imaginary[2]),
                             static_cast<double>(value.w)};
    double lengthSquared = 0.0;
    for (const double part : parts) {
        lengthSquared += part * part;
    }
    const double length = std::sqrt(lengthSquared);
    // Left alone when there is nothing to divide by: the validators this
    // conversion runs first have already refused a zero-magnitude rotation, and
    // repairing one here would put an identity where a refusal belongs.
    if (!std::isfinite(length) || length <= 0.0) {
        return pxr::GfQuatf(value.w, imaginary);
    }
    const double inverse = 1.0 / length;
    return pxr::GfQuatf(static_cast<float>(parts[3] * inverse),
                        pxr::GfVec3f(static_cast<float>(parts[0] * inverse),
                                     static_cast<float>(parts[1] * inverse),
                                     static_cast<float>(parts[2] * inverse)));
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

    // The rig's root joint is index 0 by `ValidateSourceSkeleton`, and the
    // match has already established that it is the joint the profile names.
    //
    // It is *not* where the body's placement is read from -- that is a path
    // ending at the hips, built below. What this index still means is the one
    // thing `RootRotationPolicy::None` is about: the rig's topmost node, whose
    // rotation a profile may declare to say nothing about a body. Dropping it
    // has to happen before every path walk rather than after, because a scene
    // node's rotation otherwise reaches the first bound bone underneath it.
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
        // Whether anything on the path states a rotation at all. An absent bone
        // is not an identity sample (MOTION_CONTRACT.md), so a bone whose whole
        // path is silent gets no presence bit rather than an identity the source
        // never wrote. `CHANNELS 0` is legal in the format below and a profile
        // may map such a joint, which is the whole of how this arises -- and a
        // root whose rotation the profile drops is the other way in.
        bool rotated = false;
    };
    std::vector<BoundPath> paths;
    paths.reserve(result.match.bound.size());
    for (const SourceProfileBinding& binding : result.match.bound) {
        BoundPath path;
        path.bone = binding.bone;
        path.joints =
            PathFromNearestBoundAncestor(skeleton, binding.jointIndex, bound);
        for (const std::size_t jointIndex : path.joints) {
            if (jointIndex == kRootJoint && dropRootRotation) {
                continue;
            }
            if (animation.tracks[jointIndex].HasRotation()) {
                path.rotated = true;
                break;
            }
        }
        if (path.joints.size() > 1) {
            result.report.composedBones.push_back(binding.bone);
        }
        paths.push_back(std::move(path));
    }

    // Where the body is and which way it faces is asked of a *path*, not of a
    // joint (MOTION_CONTRACT.md): a rig may spread that one fact over a
    // reference node that translates and a hips that turns. The path is the one
    // ending at the joint bound to `hips`, and this loop has already walked it
    // -- `hips` is the canonical root, so it has no bound ancestor and
    // `PathFromNearestBoundAncestor` handed it the whole chain from joint 0.
    // Reusing it is the same "one composition, two callers" the rest pose
    // relies on, and it cannot drift from the hips bone's own path because it
    // *is* that path.
    // Copied rather than pointed at. `paths` is complete here and nothing below
    // grows it, so a reference would be valid today -- and silently dangling the
    // day somebody appends to it, which is a class of bug worth more than the
    // handful of indices this costs once per conversion.
    std::vector<std::size_t> rootPath;
    for (const BoundPath& path : paths) {
        if (path.bone == motion::HumanBone::Hips) {
            rootPath = path.joints;
            break;
        }
    }
    if (rootPath.empty()) {
        // Unreachable: `ValidateSourceProfile` refuses a profile that does not
        // map the hips or maps them optionally, and a required bone that did
        // not bind has already been refused above as a profile mismatch.
        // Handled rather than asserted, because the alternative is a null
        // dereference if either of those two ever stops being true.
        return Refuse(std::move(result), ConversionRefusal::ProfileMismatch,
                      "the profile binds no "
                          + std::string(motion::HumanBoneName(
                              motion::HumanBone::Hips))
                          + ", so the body has no placement");
    }
    std::vector<bool> onRootPath(skeleton.joints.size(), false);
    for (const std::size_t jointIndex : rootPath) {
        onRootPath[jointIndex] = true;
    }

    // The local translation a joint has at `frame`, in the source's own terms:
    // the sample where its track states one, and the rest translation where it
    // does not -- a joint on the root path with no translation channel still
    // displaces its child by its own offset. `rootPolicy` says whether this
    // joint's samples are subject to the profile's root translation policy,
    // which only the root path's are; `rest-relative` samples are displacements
    // from the rest rather than positions, so the rest is added back. The two
    // policies differ in where their zero is and in nothing else.
    //
    // A track is either empty or exactly `frameCount` long
    // (`ValidateSourceAnimation`), so the bounds check here is the same
    // question as `HasTranslation()` and is written as the one that cannot be
    // wrong.
    const bool restRelative =
        profile.rootTranslation == RootTranslationPolicy::RestRelative;
    const auto localTranslation = [&](std::size_t joint, std::size_t frame,
                                      bool rootPolicy) -> SourceVec3 {
        const SourceJointTrack& track = animation.tracks[joint];
        const SourceVec3& rest = skeleton.joints[joint].restTranslation;
        if (frame >= track.translations.size()) {
            return rest;
        }
        const SourceVec3& sample = track.translations[frame];
        if (!rootPolicy || !restRelative) {
            return sample;
        }
        return SourceVec3{rest.x + sample.x, rest.y + sample.y,
                          rest.z + sample.z};
    };

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
    } else if (profile.restPose == RestPoseSource::TPose) {
        // The rig's neutral is the T-pose, and the file states neither it nor
        // the joint orientations that would reach it. What it does state is
        // every bone's *direction in its own parent's frame* -- the rest
        // offsets -- so the rotation each joint needs is the one carrying that
        // direction onto the direction a T-posed humanoid's bone points. Both
        // are known here, and neither comes from the producer: one is the rig's
        // own geometry and the other is canonical vocabulary.
        //
        // Walked root-first so a parent's world rotation is settled before the
        // child that is stated relative to it. Index order *is* that walk:
        // `ValidateSourceSkeleton` refuses a joint whose parent does not come
        // before it, and the match has already run it. Stated because this loop
        // reads `worldRest[parent]` and would compose against an identity
        // nobody wrote if that ever stopped being true.
        std::vector<pxr::GfQuatf> worldRest(skeleton.joints.size(), Identity());
        std::vector<std::vector<std::size_t>> children(skeleton.joints.size());
        for (std::size_t index = 0; index < skeleton.joints.size(); ++index) {
            const int parent = skeleton.joints[index].parent;
            if (parent >= 0) {
                children[static_cast<std::size_t>(parent)].push_back(index);
            }
        }
        for (std::size_t index = 0; index < skeleton.joints.size(); ++index) {
            const int parent = skeleton.joints[index].parent;
            const pxr::GfQuatf inherited =
                parent >= 0 ? worldRest[static_cast<std::size_t>(parent)]
                            : Identity();
            worldRest[index] = inherited;

            // A root whose rotation the profile drops is dropped here too, and
            // it has to be dropped *inside* this walk rather than after it.
            // Forcing the root's local rest to identity below, once every
            // descendant had been stated relative to a root that was not
            // identity, would turn each of them by whatever the aim had put
            // there -- a rest that is no longer the T-pose it just built, for a
            // profile pairing `t-pose` with `rotation: none`, which is a pair
            // nothing forbids.
            if (index == kRootJoint && dropRootRotation) {
                continue;
            }

            const pxr::GfVec3f wanted =
                boneForJoint[index] != motion::HumanBone::Count
                    ? TPoseDirection(boneForJoint[index])
                    : pxr::GfVec3f(0.0f);
            if (wanted == pxr::GfVec3f(0.0f) || children[index].empty()) {
                continue;
            }
            // Which child continues *this* bone. A hips joint parents the spine
            // and both legs, and only one of the three is the bone that points
            // the way the hips do; picking the first child would put the whole
            // torso down a leg. The one whose own bone agrees on direction is
            // that child, and a joint with a single child has no such question
            // to answer.
            std::size_t follower = children[index].front();
            if (children[index].size() > 1) {
                bool found = false;
                for (const std::size_t child : children[index]) {
                    const motion::HumanBone bone = boneForJoint[child];
                    if (bone != motion::HumanBone::Count
                        && TPoseDirection(bone) == wanted) {
                        follower = child;
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    continue;
                }
            }
            pxr::GfVec3f along = ConvertPosition(
                *basis, skeleton.joints[follower].restTranslation);
            if (along.GetLength() < 1e-9f) {
                continue;
            }
            along.Normalize();

            // The offsets pin which way each bone *points* and say nothing
            // about its roll around itself, and a rig's bind carries a roll per
            // joint that the format never states. Choosing one -- the shortest
            // rotation, say -- gets the skeleton's positions exactly right and
            // its frames wrong, which reads as a limb twisted about its own
            // axis while every joint sits where it should.
            //
            // So the roll comes from the file and only the aim comes from the
            // T-pose: start at the joint's orientation in the first frame, and
            // turn it by the least that brings its bone onto the T-pose
            // direction. A frame of motion is a poor rest and a fine roll --
            // the direction it disagrees about is exactly the part being
            // replaced here.
            const pxr::GfQuatf posed =
                animation.frameCount > 0
                    ? inherited * ConvertRotation(
                          *basis, SourceRotationAt(animation.tracks[index], 0))
                    : inherited;
            const pxr::GfVec3f aimed = posed.Transform(along);
            worldRest[index] = ShortestRotation(aimed, wanted) * posed;
            restRotations[index] = inherited.GetInverse() * worldRest[index];
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

    // A rest taken from the first frame is taken from the first frame entirely
    // (MOTION_CONTRACT.md). Rotations from frame 0 and translations from the
    // rest offsets is one rest assembled out of two poses -- invisible while a
    // producer's offsets *are* its rest, and a producer whose offsets are its
    // rest does not choose `first-frame`. The setting exists for the export
    // whose offsets compose into no figure at all, and that is exactly the
    // export for which the mixture is wrong: its root offset can be where the
    // capture volume happened to put the performer.
    const bool restFromFirstFrame =
        profile.restPose == RestPoseSource::FirstFrame
        && animation.frameCount > 0;
    std::vector<pxr::GfVec3f> restOffsets;
    restOffsets.reserve(skeleton.joints.size());
    for (std::size_t index = 0; index < skeleton.joints.size(); ++index) {
        restOffsets.push_back(ConvertPosition(
            *basis,
            restFromFirstFrame
                ? localTranslation(index, 0, onRootPath[index])
                : skeleton.joints[index].restTranslation));
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
        // Carried, not dropped, for every joint on the root path: the body's
        // placement is the composition along it, so a reference node's
        // translation and the hips' own are both in the clip. Reporting the
        // second of them as dropped is what this converter did before the path
        // rule, and it was the honest half of getting that export wrong.
        const bool carried =
            onRootPath[index]
            && profile.rootTranslation != RootTranslationPolicy::None;
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

    // Whether the root path says anything about position at all. A path of
    // joints that all declare rest geometry and no translation channel states
    // no placement, and a clip that reported one would be claiming the rig sat
    // at its own offsets rather than admitting the source never said.
    const bool rootPathTranslates =
        std::any_of(rootPath.begin(), rootPath.end(),
                    [&animation](std::size_t jointIndex) {
                        return animation.tracks[jointIndex].HasTranslation();
                    });
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
            // Not unconditional: see `BoundPath::rotated`. The rest pose still
            // carries the bone, because the rig has it -- what the clip does not
            // claim is that the source said anything about how it turned.
            if (path.rotated) {
                pose.validRotations.set(slot);
            }
        }

        // The body's placement, composed down the root path. Both translation
        // policies land on the same canonical thing -- an absolute position in
        // the clip's own space, which is what `vrmRetarget` subtracts each
        // rig's own hips rest from -- and `localTranslation` is the only place
        // they differ. A path of one joint, which is every rig whose root is
        // its hips, reduces to reading that joint's sample.
        pxr::GfVec3f position(0.0f);
        pxr::GfQuatf orientation = Identity();
        for (const std::size_t jointIndex : rootPath) {
            position += orientation.Transform(ConvertPosition(
                *basis, localTranslation(jointIndex, frame, true)));
            orientation = orientation * jointRotations[jointIndex];
        }
        if (profile.rootTranslation != RootTranslationPolicy::None
            && rootPathTranslates) {
            pose.root.worldPosition = position;
            pose.root.hasPosition = true;
        }
        if (!dropRootRotation) {
            pose.root.worldOrientation = orientation;
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
