// SPDX-License-Identifier: Apache-2.0
#include "motionCore/Compare.h"

#include "motionCore/Humanoid.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <locale>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace motion
{
namespace
{

// Both comparisons walk one traversal, parameterised on how a number is
// compared. Keeping them in one function is what makes "both read the fields a
// pose says it carries" true by construction rather than by discipline -- the
// alternative is two field lists that agree until one of them is extended.
//
// `ReadsProvenance` is the single place they diverge, and Compare.h says why.

struct ExactPolicy
{
    static constexpr bool ReadsProvenance = true;

    static bool Time(double a, double b) noexcept { return a == b; }
    static bool Confidence(float a, float b) noexcept { return a == b; }
    static bool Weight(float a, float b) noexcept { return a == b; }

    static bool Vector(const pxr::GfVec3f& a, const pxr::GfVec3f& b) noexcept
    {
        return a[0] == b[0] && a[1] == b[1] && a[2] == b[2];
    }

    static bool Rotation(const pxr::GfQuatf& a, const pxr::GfQuatf& b) noexcept
    {
        return a.GetReal() == b.GetReal()
            && Vector(a.GetImaginary(), b.GetImaginary());
    }
};

struct TolerantPolicy
{
    static constexpr bool ReadsProvenance = false;

    MotionTolerance tolerance;

    // Written as `<=` on the absolute difference so a NaN answers false. Every
    // other comparison here is expressed through this one for that reason.
    static bool Within(double difference, double limit) noexcept
    {
        return std::abs(difference) <= limit;
    }

    bool Time(double a, double b) const noexcept
    {
        return Within(a - b, tolerance.time);
    }

    bool Confidence(float a, float b) const noexcept
    {
        return Within(static_cast<double>(a) - b, tolerance.confidence);
    }

    bool Weight(float a, float b) const noexcept
    {
        return Within(static_cast<double>(a) - b, tolerance.expression);
    }

    bool Vector(const pxr::GfVec3f& a, const pxr::GfVec3f& b) const noexcept
    {
        return Within((a - b).GetLength(), tolerance.distance);
    }

    bool Velocity(const pxr::GfVec3f& a, const pxr::GfVec3f& b) const noexcept
    {
        return Within((a - b).GetLength(), tolerance.velocity);
    }

    bool Rotation(const pxr::GfQuatf& a, const pxr::GfQuatf& b) const noexcept
    {
        return Within(AngleBetween(a, b), tolerance.angle);
    }
};

// Exact equality draws no distinction between a position and a velocity, so it
// gets no `Velocity` of its own -- one that forwarded to `Vector` would only
// invite the two to drift apart.
bool
CompareVelocity(const ExactPolicy&, const pxr::GfVec3f& a,
                const pxr::GfVec3f& b) noexcept
{
    return ExactPolicy::Vector(a, b);
}

bool
CompareVelocity(const TolerantPolicy& policy, const pxr::GfVec3f& a,
                const pxr::GfVec3f& b) noexcept
{
    return policy.Velocity(a, b);
}

// The message is built by a callable rather than passed as a string: a caller
// that wants only the answer -- `operator==`, and every comparison inside an
// exec computation -- must not pay an allocation to be told two values differ.
template <typename Describe>
void
Report(std::string* difference, Describe&& describe)
{
    if (difference) {
        *difference = describe();
    }
}

std::ostringstream
TextStream()
{
    std::ostringstream text;
    text.imbue(std::locale::classic());
    return text;
}

std::string
Amount(double difference, const char* unit)
{
    std::ostringstream text = TextStream();
    text << "differs by " << difference;
    if (*unit != '\0') {
        text << ' ' << unit;
    }
    return text.str();
}

std::string
BoneText(std::size_t index)
{
    return std::string(HumanBoneName(static_cast<HumanBone>(index)));
}

double
AngleDifference(const pxr::GfQuatf& a, const pxr::GfQuatf& b) noexcept
{
    return static_cast<double>(AngleBetween(a, b));
}

double
LengthSquared(const pxr::GfQuatf& q) noexcept
{
    return static_cast<double>(q.GetReal()) * q.GetReal()
        + static_cast<double>(q.GetImaginary()[0]) * q.GetImaginary()[0]
        + static_cast<double>(q.GetImaginary()[1]) * q.GetImaginary()[1]
        + static_cast<double>(q.GetImaginary()[2]) * q.GetImaginary()[2];
}

template <typename Policy>
bool
CompareRoot(const Policy& policy, const RootMotion& a, const RootMotion& b,
            std::string* difference)
{
    if (a.hasPosition != b.hasPosition || a.hasOrientation != b.hasOrientation
        || a.hasLinearVelocity != b.hasLinearVelocity
        || a.hasAngularVelocity != b.hasAngularVelocity) {
        Report(difference, [] { return std::string(
            "root carries different fields"); });
        return false;
    }
    if (a.hasPosition && !policy.Vector(a.worldPosition, b.worldPosition)) {
        Report(difference, [&] {
            return "root position "
                + Amount((a.worldPosition - b.worldPosition).GetLength(), "m");
        });
        return false;
    }
    if (a.hasOrientation
        && !policy.Rotation(a.worldOrientation, b.worldOrientation)) {
        Report(difference, [&] {
            return "root orientation "
                + Amount(AngleDifference(a.worldOrientation, b.worldOrientation),
                         "rad");
        });
        return false;
    }
    if (a.hasLinearVelocity
        && !CompareVelocity(policy, a.linearVelocity, b.linearVelocity)) {
        Report(difference, [&] {
            return "root linear velocity "
                + Amount((a.linearVelocity - b.linearVelocity).GetLength(),
                         "m/s");
        });
        return false;
    }
    if (a.hasAngularVelocity
        && !CompareVelocity(policy, a.angularVelocity, b.angularVelocity)) {
        Report(difference, [&] {
            return "root angular velocity "
                + Amount((a.angularVelocity - b.angularVelocity).GetLength(),
                         "rad/s");
        });
        return false;
    }
    return true;
}

template <typename Policy>
bool
ComparePose(const Policy& policy, const HumanoidPose& a, const HumanoidPose& b,
            std::string* difference)
{
    if (!policy.Time(a.timestamp, b.timestamp)) {
        Report(difference, [&] {
            return "timestamp " + Amount(a.timestamp - b.timestamp, "s");
        });
        return false;
    }
    if (!CompareRoot(policy, a.root, b.root, difference)) {
        return false;
    }
    if (a.validRotations != b.validRotations) {
        std::size_t index = 0;
        while (index != HumanBoneCount
               && a.validRotations.test(index) == b.validRotations.test(index)) {
            ++index;
        }
        Report(difference, [&] {
            return BoneText(index)
                + (a.validRotations.test(index)
                       ? " is present only in the first pose"
                       : " is present only in the second pose");
        });
        return false;
    }
    for (std::size_t index = 0; index != HumanBoneCount; ++index) {
        if (!a.validRotations.test(index)) {
            continue;
        }
        const pxr::GfQuatf& first = a.localRotations[index];
        const pxr::GfQuatf& second = b.localRotations[index];
        if (!policy.Rotation(first, second)) {
            Report(difference, [&] {
                return BoneText(index) + " rotation "
                    + Amount(AngleDifference(first, second), "rad");
            });
            return false;
        }
    }
    if (a.confidence.has_value() != b.confidence.has_value()) {
        Report(difference, [] { return std::string(
            "only one pose reports confidence"); });
        return false;
    }
    if (a.confidence) {
        for (std::size_t index = 0; index != HumanBoneCount; ++index) {
            if (!a.validRotations.test(index)) {
                continue;
            }
            const float first = (*a.confidence)[index];
            const float second = (*b.confidence)[index];
            if (!policy.Confidence(first, second)) {
                Report(difference, [&] {
                    return BoneText(index) + " confidence "
                        + Amount(static_cast<double>(first) - second, "");
                });
                return false;
            }
        }
    }
    if (a.contacts.has_value() != b.contacts.has_value()) {
        Report(difference, [] { return std::string(
            "only one pose reports foot contact"); });
        return false;
    }
    if (a.contacts && *a.contacts != *b.contacts) {
        Report(difference, [] { return std::string("foot contact differs"); });
        return false;
    }
    // A name is an identifier rather than a measurement, so it compares exactly
    // under both policies and only the weight takes a tolerance. Both sets are
    // sorted by name (Humanoid.h), so one walk finds the first difference, and
    // at a mismatch the lexicographically smaller name is the one its own pose
    // reported alone.
    {
        const std::vector<ExpressionWeight>& first = a.expressions.entries;
        const std::vector<ExpressionWeight>& second = b.expressions.entries;
        const std::size_t shared = std::min(first.size(), second.size());
        for (std::size_t index = 0; index != shared; ++index) {
            if (first[index].name != second[index].name) {
                Report(difference, [&] {
                    const bool onlyFirst =
                        first[index].name < second[index].name;
                    return "expression '"
                        + (onlyFirst ? first[index].name : second[index].name)
                        + "' is reported only by the "
                        + (onlyFirst ? "first" : "second") + " pose";
                });
                return false;
            }
            if (!policy.Weight(first[index].weight, second[index].weight)) {
                Report(difference, [&] {
                    return "expression '" + first[index].name + "' weight "
                        + Amount(static_cast<double>(first[index].weight)
                                     - second[index].weight,
                                 "");
                });
                return false;
            }
        }
        if (first.size() != second.size()) {
            const bool longerIsFirst = first.size() > second.size();
            Report(difference, [&] {
                return "expression '"
                    + (longerIsFirst ? first : second)[shared].name
                    + "' is reported only by the "
                    + (longerIsFirst ? "first" : "second") + " pose";
            });
            return false;
        }
    }
    // A look-at target is a point, so it takes the same tolerance a root
    // position does. Whether a pose carries one at all is compared first and
    // exactly, for the reason every other presence flag here is: a pose that
    // reported no gaze never equals one that reported a gaze at the origin.
    if (a.lookAtTarget.has_value() != b.lookAtTarget.has_value()) {
        Report(difference, [] { return std::string(
            "only one pose reports a look-at target"); });
        return false;
    }
    if (a.lookAtTarget && !policy.Vector(*a.lookAtTarget, *b.lookAtTarget)) {
        Report(difference, [&] {
            return "look-at target "
                + Amount((*a.lookAtTarget - *b.lookAtTarget).GetLength(), "m");
        });
        return false;
    }
    if constexpr (Policy::ReadsProvenance) {
        if (a.source.has_value() != b.source.has_value()
            || (a.source && *a.source != *b.source)) {
            Report(difference, [] { return std::string(
                "source metadata differs"); });
            return false;
        }
    }
    return true;
}

template <typename Policy>
bool
CompareAnimation(const Policy& policy, const HumanoidAnimation& a,
                 const HumanoidAnimation& b, std::string* difference)
{
    if (a.samples.size() != b.samples.size()) {
        Report(difference, [&] {
            std::ostringstream text = TextStream();
            text << "sample count differs: " << a.samples.size() << " vs "
                 << b.samples.size();
            return text.str();
        });
        return false;
    }
    if (!policy.Time(a.startTime, b.startTime)) {
        Report(difference, [&] {
            return "startTime " + Amount(a.startTime - b.startTime, "s");
        });
        return false;
    }
    if (!policy.Time(a.endTime, b.endTime)) {
        Report(difference, [&] {
            return "endTime " + Amount(a.endTime - b.endTime, "s");
        });
        return false;
    }
    // A nominal frame rate is metadata a producer states rather than a number
    // it computes, so the time tolerance is the right size for it: two clips
    // that agree on being 30 fps agree exactly.
    if (!policy.Time(a.nominalFrameRate, b.nominalFrameRate)) {
        Report(difference, [&] {
            return "nominalFrameRate "
                + Amount(a.nominalFrameRate - b.nominalFrameRate, "fps");
        });
        return false;
    }
    if constexpr (Policy::ReadsProvenance) {
        if (a.source != b.source) {
            Report(difference, [] { return std::string(
                "source metadata differs"); });
            return false;
        }
    }
    for (std::size_t index = 0; index != a.samples.size(); ++index) {
        std::string sampleDifference;
        if (!ComparePose(policy, a.samples[index], b.samples[index],
                         difference ? &sampleDifference : nullptr)) {
            Report(difference, [&] {
                std::ostringstream text = TextStream();
                text << "sample " << index << ": " << sampleDifference;
                return text.str();
            });
            return false;
        }
    }
    return true;
}

} // namespace

float
AngleBetween(const pxr::GfQuatf& a, const pxr::GfQuatf& b) noexcept
{
    const double dot = static_cast<double>(a.GetReal()) * b.GetReal()
        + static_cast<double>(a.GetImaginary()[0]) * b.GetImaginary()[0]
        + static_cast<double>(a.GetImaginary()[1]) * b.GetImaginary()[1]
        + static_cast<double>(a.GetImaginary()[2]) * b.GetImaginary()[2];
    // Both lengths are formed in double from the same components the dot
    // product used, rather than through `GfQuatf::GetLength()`. That is not a
    // micro-optimisation, it is the difference between this function being
    // usable and not: `acos` is infinitely steep at 1, so a mismatch of e in
    // the ratio becomes about 2*sqrt(2e) radians of reported angle. A float
    // length carries e ~ 1e-7 against a dot computed in double, which is 9e-4
    // rad -- an order *above* the contract's own default tolerance, at exactly
    // the place the answer should be zero. Computed this way, a quaternion
    // compared with itself gives a ratio of exactly 1 and an angle of exactly
    // 0, whatever its length.
    const double lengthSquaredA = LengthSquared(a);
    const double lengthSquaredB = LengthSquared(b);
    const double lengths = std::sqrt(lengthSquaredA * lengthSquaredB);
    // Also the NaN branch: every comparison against a NaN length is false.
    if (!(lengths > 0.0)) {
        return std::numeric_limits<float>::quiet_NaN();
    }
    // The absolute value is the double cover: `q` and `-q` differ in the sign
    // of every component, so the shorter of the two arcs is always the one
    // meant.
    double cosHalfAngle = std::abs(dot) / lengths;
    if (cosHalfAngle > 1.0) {
        // Rounding only -- the ratio cannot exceed 1 in exact arithmetic, and
        // acos(1 + 1e-16) is NaN rather than 0.
        cosHalfAngle = 1.0;
    }
    return static_cast<float>(2.0 * std::acos(cosHalfAngle));
}

bool
NearlyEqual(const RootMotion& a, const RootMotion& b,
            const MotionTolerance& tolerance, std::string* difference)
{
    return CompareRoot(TolerantPolicy{tolerance}, a, b, difference);
}

bool
NearlyEqual(const HumanoidPose& a, const HumanoidPose& b,
            const MotionTolerance& tolerance, std::string* difference)
{
    return ComparePose(TolerantPolicy{tolerance}, a, b, difference);
}

bool
NearlyEqual(const HumanoidAnimation& a, const HumanoidAnimation& b,
            const MotionTolerance& tolerance, std::string* difference)
{
    return CompareAnimation(TolerantPolicy{tolerance}, a, b, difference);
}

bool
operator==(const MotionSourceMetadata& a,
           const MotionSourceMetadata& b) noexcept
{
    return a.kind == b.kind && a.provider == b.provider
        && a.protocol == b.protocol && a.sourceId == b.sourceId;
}

bool
operator!=(const MotionSourceMetadata& a,
           const MotionSourceMetadata& b) noexcept
{
    return !(a == b);
}

bool
operator==(const RootMotion& a, const RootMotion& b) noexcept
{
    return CompareRoot(ExactPolicy{}, a, b, nullptr);
}

bool
operator!=(const RootMotion& a, const RootMotion& b) noexcept
{
    return !(a == b);
}

bool
operator==(const ContactState& a, const ContactState& b) noexcept
{
    return a.leftFoot == b.leftFoot && a.rightFoot == b.rightFoot;
}

bool
operator!=(const ContactState& a, const ContactState& b) noexcept
{
    return !(a == b);
}

bool
operator==(const ExpressionWeight& a, const ExpressionWeight& b) noexcept
{
    return a.name == b.name && a.weight == b.weight;
}

bool
operator!=(const ExpressionWeight& a, const ExpressionWeight& b) noexcept
{
    return !(a == b);
}

bool
operator==(const ExpressionWeights& a, const ExpressionWeights& b) noexcept
{
    return a.entries == b.entries;
}

bool
operator!=(const ExpressionWeights& a, const ExpressionWeights& b) noexcept
{
    return !(a == b);
}

bool
operator==(const HumanoidPose& a, const HumanoidPose& b) noexcept
{
    return ComparePose(ExactPolicy{}, a, b, nullptr);
}

bool
operator!=(const HumanoidPose& a, const HumanoidPose& b) noexcept
{
    return !(a == b);
}

bool
operator==(const HumanoidAnimation& a, const HumanoidAnimation& b) noexcept
{
    return CompareAnimation(ExactPolicy{}, a, b, nullptr);
}

bool
operator!=(const HumanoidAnimation& a, const HumanoidAnimation& b) noexcept
{
    return !(a == b);
}

} // namespace motion
