// SPDX-License-Identifier: Apache-2.0
#include "motionSource/SourceAnimation.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>

namespace motionSource
{
namespace
{

// Storage order per enumerator: the axis of the first, second and third angle,
// as 0 = X, 1 = Y, 2 = Z. One table rather than a switch, so the enumerator list
// and the axis list cannot drift apart without the count check below failing.
constexpr std::array<std::array<int, 3>, SourceEulerOrderCount> kEulerAxes = {{
    {{0, 1, 2}}, // XYZ
    {{0, 2, 1}}, // XZY
    {{1, 0, 2}}, // YXZ
    {{1, 2, 0}}, // YZX
    {{2, 0, 1}}, // ZXY
    {{2, 1, 0}}, // ZYX
}};

constexpr std::array<std::string_view, SourceEulerOrderCount> kEulerNames = {
    "XYZ", "XZY", "YXZ", "YZX", "ZXY", "ZYX",
};

constexpr std::array<std::string_view, 2> kAngleUnitNames = {
    "degrees", "radians",
};

char
LowerAscii(char c) noexcept
{
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

bool
EqualsAscii(std::string_view a, std::string_view b) noexcept
{
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (LowerAscii(a[i]) != LowerAscii(b[i])) {
            return false;
        }
    }
    return true;
}

bool
IsFinite(const SourceVec3& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y)
           && std::isfinite(value.z);
}

bool
IsFinite(const SourceEulerAngles& value) noexcept
{
    return std::isfinite(value.first) && std::isfinite(value.second)
           && std::isfinite(value.third);
}

bool
IsFinite(const SourceQuat& value) noexcept
{
    return std::isfinite(value.w) && std::isfinite(value.x)
           && std::isfinite(value.y) && std::isfinite(value.z);
}

bool
IsZero(const SourceQuat& value) noexcept
{
    return value.w == 0.0f && value.x == 0.0f && value.y == 0.0f
           && value.z == 0.0f;
}

bool
Fail(std::string* reason, std::string text)
{
    if (reason) {
        *reason = std::move(text);
    }
    return false;
}

std::string
TrackLabel(const SourceSkeleton& skeleton, std::size_t index)
{
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "track %zu", index);
    std::string label(buffer);
    if (index < skeleton.joints.size()) {
        label += " '" + skeleton.joints[index].name + "'";
    }
    return label;
}

} // namespace

std::string_view
SourceEulerOrderName(SourceEulerOrder order) noexcept
{
    const auto index = static_cast<std::size_t>(order);
    return index < kEulerNames.size() ? kEulerNames[index] : std::string_view();
}

std::optional<SourceEulerOrder>
FindSourceEulerOrder(std::string_view name) noexcept
{
    for (std::size_t i = 0; i < kEulerNames.size(); ++i) {
        if (EqualsAscii(name, kEulerNames[i])) {
            return static_cast<SourceEulerOrder>(i);
        }
    }
    return std::nullopt;
}

std::optional<int>
SourceEulerAxis(SourceEulerOrder order, std::size_t component) noexcept
{
    const auto index = static_cast<std::size_t>(order);
    if (index >= kEulerAxes.size() || component >= 3) {
        return std::nullopt;
    }
    return kEulerAxes[index][component];
}

std::string_view
SourceAngleUnitName(SourceAngleUnit unit) noexcept
{
    const auto index = static_cast<std::size_t>(unit);
    return index < kAngleUnitNames.size() ? kAngleUnitNames[index]
                                          : std::string_view();
}

std::optional<SourceAngleUnit>
FindSourceAngleUnit(std::string_view name) noexcept
{
    for (std::size_t i = 0; i < kAngleUnitNames.size(); ++i) {
        if (EqualsAscii(name, kAngleUnitNames[i])) {
            return static_cast<SourceAngleUnit>(i);
        }
    }
    return std::nullopt;
}

bool
operator==(const SourceJointTrack& lhs, const SourceJointTrack& rhs) noexcept
{
    // The order and the unit are compared only when angles are present. A track
    // holding no angles carries defaults nothing set, and two such tracks
    // describe the same motion whatever those defaults happen to be — which is
    // the header's claim that the fields mean nothing without angles beside
    // them, made true rather than merely written down.
    if (!lhs.eulerAngles.empty()
        && (lhs.eulerOrder != rhs.eulerOrder || lhs.angleUnit != rhs.angleUnit)) {
        return false;
    }
    return lhs.translations == rhs.translations
           && lhs.eulerAngles == rhs.eulerAngles
           && lhs.rotations == rhs.rotations;
}

bool
operator!=(const SourceJointTrack& lhs, const SourceJointTrack& rhs) noexcept
{
    return !(lhs == rhs);
}

std::optional<double>
SourceAnimation::Time(std::size_t frameIndex) const noexcept
{
    if (frameIndex >= frameCount) {
        return std::nullopt;
    }
    return startTime + static_cast<double>(frameIndex) * frameTime;
}

double
SourceAnimation::EndTime() const noexcept
{
    return frameCount == 0 ? startTime : startTime + Duration();
}

double
SourceAnimation::Duration() const noexcept
{
    return frameCount < 2 ? 0.0
                          : static_cast<double>(frameCount - 1) * frameTime;
}

std::optional<double>
SourceAnimation::FrameRate() const noexcept
{
    if (frameTime <= 0.0) {
        return std::nullopt;
    }
    return 1.0 / frameTime;
}

bool
operator==(const SourceAnimation& lhs, const SourceAnimation& rhs) noexcept
{
    return lhs.frameCount == rhs.frameCount && lhs.frameTime == rhs.frameTime
           && lhs.startTime == rhs.startTime && lhs.provenance == rhs.provenance
           && lhs.tracks == rhs.tracks;
}

bool
operator!=(const SourceAnimation& lhs, const SourceAnimation& rhs) noexcept
{
    return !(lhs == rhs);
}

bool
ValidateSourceAnimation(const SourceAnimation& animation,
                        const SourceSkeleton& skeleton, std::string* reason)
{
    std::string skeletonReason;
    if (!ValidateSourceSkeleton(skeleton, &skeletonReason)) {
        return Fail(reason, "skeleton is invalid: " + skeletonReason);
    }

    if (animation.tracks.size() != skeleton.joints.size()) {
        char buffer[96];
        std::snprintf(buffer, sizeof(buffer),
                      "%zu track(s) for %zu joint(s)", animation.tracks.size(),
                      skeleton.joints.size());
        return Fail(reason, buffer);
    }

    if (!std::isfinite(animation.frameTime) || animation.frameTime < 0.0) {
        return Fail(reason, "frame time is not a non-negative interval");
    }
    if (animation.frameCount >= 2 && animation.frameTime <= 0.0) {
        return Fail(reason,
                    "frame time is zero for a clip carrying two or more "
                    "frames");
    }
    if (!std::isfinite(animation.startTime)) {
        return Fail(reason, "start time is not finite");
    }

    for (std::size_t i = 0; i < animation.tracks.size(); ++i) {
        const SourceJointTrack& track = animation.tracks[i];

        if (!track.eulerAngles.empty() && !track.rotations.empty()) {
            return Fail(reason,
                        TrackLabel(skeleton, i)
                            + " carries both angle and quaternion rotations");
        }
        if (static_cast<std::size_t>(track.eulerOrder) >= SourceEulerOrderCount) {
            return Fail(reason,
                        TrackLabel(skeleton, i) + " has no known Euler order");
        }
        if (static_cast<std::size_t>(track.angleUnit)
            >= static_cast<std::size_t>(SourceAngleUnit::Count)) {
            return Fail(reason,
                        TrackLabel(skeleton, i) + " has no known angle unit");
        }

        const auto checkLength = [&](std::size_t size,
                                     const char* what) -> bool {
            if (size == 0 || size == animation.frameCount) {
                return true;
            }
            char buffer[96];
            std::snprintf(buffer, sizeof(buffer),
                          " carries %zu %s for %zu frame(s)", size, what,
                          animation.frameCount);
            return Fail(reason, TrackLabel(skeleton, i) + buffer);
        };
        if (!checkLength(track.translations.size(), "translation(s)")
            || !checkLength(track.eulerAngles.size(), "angle sample(s)")
            || !checkLength(track.rotations.size(), "rotation(s)")) {
            return false;
        }

        for (const SourceVec3& translation : track.translations) {
            if (!IsFinite(translation)) {
                return Fail(reason,
                            TrackLabel(skeleton, i)
                                + " has a non-finite translation");
            }
        }
        for (const SourceEulerAngles& angles : track.eulerAngles) {
            if (!IsFinite(angles)) {
                return Fail(reason,
                            TrackLabel(skeleton, i) + " has a non-finite angle");
            }
        }
        for (const SourceQuat& rotation : track.rotations) {
            if (!IsFinite(rotation)) {
                return Fail(reason,
                            TrackLabel(skeleton, i)
                                + " has a non-finite rotation");
            }
            if (IsZero(rotation)) {
                return Fail(reason,
                            TrackLabel(skeleton, i)
                                + " has a zero-magnitude rotation");
            }
        }
    }

    if (reason) {
        reason->clear();
    }
    return true;
}

} // namespace motionSource
