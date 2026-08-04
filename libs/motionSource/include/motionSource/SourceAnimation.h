// SPDX-License-Identifier: Apache-2.0
//
// What a recorded file animated, in the source's own terms.
//
// One track per skeleton joint, each holding the samples the source carried for
// that joint and nothing it did not. Values stay in the source's basis, unit and
// angle convention; turning them into canonical humanoid motion needs a producer
// profile and happens in the converter.
//
// Four decisions, each of which a different shape would have quietly cost.
//
// **A rotation is stored the way the source expressed it, and is never
// re-expressed here.** A source that states three angles states an *order* with
// them, and composing them into a quaternion needs the handedness — a profile's
// answer, not this layer's. A source that states quaternions states no order at
// all, and decomposing one into angles would invent an order the writer never
// declared. So both forms exist, a track carries at most one of them, and
// neither is converted into the other before a profile is in hand.
//
// **The angle unit is the reader's answer, not the profile's.** A format states
// whether its angles are degrees or radians; a *producer* does not get to
// disagree with its own format about that. This is the one convention on this
// page that does not come from a profile, and it is a field for exactly that
// reason — the profile contract (roadmap §3) carries a translation unit and no
// angle unit, which would otherwise leave this undecidable.
//
// **An unanimated component is an empty vector, not a run of identity values.**
// A source that animated a joint's rotation and not its translation said
// something a run of zeros would not: filling one in would make "held at rest"
// and "authored as rest" the same value, and the converter needs them apart to
// decide what a root translation means.
//
// **Sampling is uniform, and that is a stated boundary rather than an
// assumption nobody noticed.** A recorded motion file declares one frame
// interval, and both the format read today and the mocap formats behind it are
// uniform. A key-timed source is the case that would extend this — with a
// per-sample time vector, whose absence here is what keeps every consumer from
// having to handle two timing paths for one producer.
#pragma once

#include "motionSource/SourceProvenance.h"
#include "motionSource/SourceSkeleton.h"
#include "motionSource/api.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace motionSource
{

// The axis sequence three angles are given in. Values are stable array indices;
// append only before Count.
//
// The enumerator names the order the angles are *stored* in, so `XYZ` means
// `first` is the X angle. It does not state how they compose into a rotation:
// that needs the handedness, and a converter decides it with a profile in hand.
enum class SourceEulerOrder : std::uint8_t
{
    XYZ,
    XZY,
    YXZ,
    YZX,
    ZXY,
    ZYX,

    Count,
};

inline constexpr std::size_t SourceEulerOrderCount =
    static_cast<std::size_t>(SourceEulerOrder::Count);

// The canonical spelling, e.g. "ZXY".
MOTIONSOURCE_API std::string_view SourceEulerOrderName(
    SourceEulerOrder order) noexcept;

// ASCII case-insensitive: writers disagree about case and a case difference is
// not a meaning difference.
MOTIONSOURCE_API std::optional<SourceEulerOrder> FindSourceEulerOrder(
    std::string_view name) noexcept;

// The axis of the `component`-th angle (0, 1, 2) under `order`: 0 for X, 1 for
// Y, 2 for Z. Nullopt for an out-of-range component or a non-enumerator order.
MOTIONSOURCE_API std::optional<int> SourceEulerAxis(
    SourceEulerOrder order, std::size_t component) noexcept;

enum class SourceAngleUnit : std::uint8_t
{
    Degrees,
    Radians,

    Count,
};

MOTIONSOURCE_API std::string_view SourceAngleUnitName(
    SourceAngleUnit unit) noexcept;
MOTIONSOURCE_API std::optional<SourceAngleUnit> FindSourceAngleUnit(
    std::string_view name) noexcept;

// Three angles in the track's order and unit. Named by position rather than by
// axis, because which axis `first` is depends on the order beside it and a field
// called `x` would read as an answer to that.
struct SourceEulerAngles
{
    float first = 0.0f;
    float second = 0.0f;
    float third = 0.0f;

    friend bool operator==(const SourceEulerAngles& lhs,
                           const SourceEulerAngles& rhs) noexcept
    {
        return lhs.first == rhs.first && lhs.second == rhs.second
               && lhs.third == rhs.third;
    }
    friend bool operator!=(const SourceEulerAngles& lhs,
                           const SourceEulerAngles& rhs) noexcept
    {
        return !(lhs == rhs);
    }
};

// One joint's samples across the whole clip. Every non-empty vector is exactly
// `SourceAnimation::frameCount` long.
struct SourceJointTrack
{
    // Local translation per frame, in the source's basis and unit. Empty when
    // the source animated no translation for this joint.
    std::vector<SourceVec3> translations;

    // Local rotation per frame, as angles. Empty when the source animated no
    // rotation for this joint, or expressed it as `rotations` instead.
    std::vector<SourceEulerAngles> eulerAngles;

    // Local rotation per frame, as quaternions, for sources that state them.
    // Never non-empty at the same time as `eulerAngles`: a track carrying both
    // would be two statements about one rotation with nothing here able to
    // arbitrate between them.
    std::vector<SourceQuat> rotations;

    // Meaningful only when `eulerAngles` is non-empty. The defaults are not a
    // claim about a source that carries no angles.
    SourceEulerOrder eulerOrder = SourceEulerOrder::XYZ;
    SourceAngleUnit angleUnit = SourceAngleUnit::Degrees;

    bool HasTranslation() const noexcept { return !translations.empty(); }
    bool HasRotation() const noexcept
    {
        return !eulerAngles.empty() || !rotations.empty();
    }
    bool IsEmpty() const noexcept
    {
        return !HasTranslation() && !HasRotation();
    }

    MOTIONSOURCE_API friend bool operator==(
        const SourceJointTrack& lhs, const SourceJointTrack& rhs) noexcept;
    MOTIONSOURCE_API friend bool operator!=(
        const SourceJointTrack& lhs, const SourceJointTrack& rhs) noexcept;
};

struct SourceAnimation
{
    // One per skeleton joint, at the same indices. A joint the source animated
    // nothing about still has a track, an empty one — dropping it would make a
    // track index stop meaning a joint index, which is the one thing tying these
    // two values together.
    std::vector<SourceJointTrack> tracks;

    std::size_t frameCount = 0;

    // Seconds between consecutive frames, and positive whenever the clip
    // carries two or more. Below that no interval was observed, and a reader
    // with none states zero rather than inventing one — a rate a source never
    // stated is a claim, not a default.
    double frameTime = 0.0;

    // Seconds at frame 0. Almost always zero, and a field rather than an
    // assumption because a reader extracting one take out of a longer recording
    // has an offset to keep.
    double startTime = 0.0;

    SourceProvenance provenance;

    // The instant of `frameIndex`, or nullopt when it is out of range.
    MOTIONSOURCE_API std::optional<double> Time(
        std::size_t frameIndex) const noexcept;

    // The instant of the last frame, or `startTime` for an empty clip.
    MOTIONSOURCE_API double EndTime() const noexcept;

    // The span between the first and last samples — `(frameCount - 1) *
    // frameTime`, and zero below two frames. Not `frameCount * frameTime`: one
    // sample is an instant, and the interval after the last sample was never
    // observed.
    MOTIONSOURCE_API double Duration() const noexcept;

    // Frames per second derived from `frameTime`, or nullopt when there is no
    // interval to derive one from.
    MOTIONSOURCE_API std::optional<double> FrameRate() const noexcept;

    MOTIONSOURCE_API friend bool operator==(
        const SourceAnimation& lhs, const SourceAnimation& rhs) noexcept;
    MOTIONSOURCE_API friend bool operator!=(
        const SourceAnimation& lhs, const SourceAnimation& rhs) noexcept;
};

// Whether an animation is internally consistent and describes `skeleton`: one
// track per joint, every non-empty component vector exactly `frameCount` long,
// no track carrying both rotation forms, enumerators in range, every number
// finite with no zero-magnitude rotation, and a frame time that is positive
// whenever an interval was observed.
//
// It takes the skeleton because the two are separate values (SourceSkeleton.h)
// and the only thing that binds them is an index — so an animation checked
// alone would pass while describing a different rig.
//
// `reason` is plain text rather than a diagnostic code, for the reason
// `ValidateSourceSkeleton` states.
MOTIONSOURCE_API bool ValidateSourceAnimation(const SourceAnimation& animation,
                                              const SourceSkeleton& skeleton,
                                              std::string* reason = nullptr);

} // namespace motionSource
