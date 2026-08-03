// SPDX-License-Identifier: Apache-2.0
//
// What a BVH file says, and nothing about what it means.
//
// This model carries `HIERARCHY`, `ROOT` / `JOINT`, `OFFSET`, `CHANNELS`,
// `End Site`, `MOTION`, the frame count, the frame time, and the channel values
// in declaration order. It carries no unit, no up axis, no handedness, no
// rotation order, no root policy, and no humanoid bone — those are facts about
// the application that *wrote* the file, not about the format, and they live in
// a producer profile one layer up (roadmap/recorded-motion-sources.md §2).
//
// Three decisions are worth stating before the API, because each is a choice
// rather than a detail.
//
// **Channel declaration order is retained and never normalised.** A BVH file's
// Euler order *is* its rotation-channel declaration order, so a model that
// sorted channels into a canonical order would destroy the only statement the
// file makes about it — and the loss would be invisible until a file declared
// `Yrotation Xrotation Zrotation` and came out rotated. This layer does not
// decide what the order means; it refuses to lose it.
//
// **An `End Site` is a terminator, not a joint.** BVH gives it an offset and no
// name, no channels, and no children, and at most one per joint. Modelling it
// as a nameless entry in `joints` would put something with no name in every
// loop that walks joints, and every one of those loops would have to skip it.
// It sits on its parent as `endSiteOffset`, where the converter looks for it
// when it builds a source rest pose.
//
// **An offset is three numbers, not a vector.** There is deliberately no
// `GfVec3f` here. A `GfVec3f` implies a coordinate system, and this layer does
// not know the file's handedness, up axis, or unit — so a value typed as a
// geometric vector would be an assertion the parser is in no position to make.
// The basis change happens at the converter, with a profile in hand.
#pragma once

#include "motionBvh/Diagnostics.h"
#include "motionBvh/api.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace motionBvh
{

// Three numbers in the file's own convention. See the header note above.
struct BvhVec3
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    friend bool operator==(const BvhVec3& lhs, const BvhVec3& rhs) noexcept
    {
        return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
    }
    friend bool operator!=(const BvhVec3& lhs, const BvhVec3& rhs) noexcept
    {
        return !(lhs == rhs);
    }
};

// The six channel tokens BVH defines. Values are stable array indices.
//
// The enumerator order is the conventional listing order, and it is *not* a
// rotation order: `BvhJoint::channels` holds what the file declared, in the
// order it declared it.
enum class BvhChannel : std::uint8_t
{
    Xposition,
    Yposition,
    Zposition,
    Xrotation,
    Yrotation,
    Zrotation,

    Count,
};

inline constexpr std::size_t BvhChannelCount =
    static_cast<std::size_t>(BvhChannel::Count);

// The canonical spelling, e.g. "Xrotation". Files disagree about case and the
// parser accepts any of them; what this returns is the one this library prints.
MOTIONBVH_API std::string_view BvhChannelName(BvhChannel channel) noexcept;

// ASCII case-insensitive, because writers disagree about case and a case
// difference is not a meaning difference.
MOTIONBVH_API std::optional<BvhChannel> FindBvhChannel(
    std::string_view token) noexcept;

MOTIONBVH_API bool BvhChannelIsPosition(BvhChannel channel) noexcept;
MOTIONBVH_API bool BvhChannelIsRotation(BvhChannel channel) noexcept;

struct BvhJoint
{
    // Verbatim from the file. BVH does not require joint names to be unique and
    // real exports repeat them, so this is not a key — see `FindJoints`.
    std::string name;
    // Index into `BvhDocument::joints`, or -1 for the `ROOT`. Always less than
    // this joint's own index: parents are stored before their children.
    int parent = -1;
    BvhVec3 offset;
    // Declaration order, never sorted. May be empty: a joint with `CHANNELS 0`
    // is legal and means the file animates nothing about it.
    std::vector<BvhChannel> channels;
    // Column of this joint's first channel within a motion row. Derived from
    // declaration order across the whole hierarchy, which is the only thing
    // that maps a row's numbers back to joints.
    std::size_t channelOffset = 0;
    // The `End Site` terminator's offset, when the file gave this joint one.
    std::optional<BvhVec3> endSiteOffset;
};

struct BvhDocument
{
    // Parent before child, `ROOT` at index 0. A document with no joints is not
    // a valid BVH file and the parser never produces one.
    std::vector<BvhJoint> joints;
    // Total channels across every joint — the width of one motion row.
    std::size_t channelCount = 0;
    // As declared by `Frames:`, and equal to the number of rows read.
    std::size_t frameCount = 0;
    // Seconds, as declared by `Frame Time:`. Zero only when the file declares
    // fewer than two frames (BvhParser.h states why).
    double frameTime = 0.0;
    // `frameCount * channelCount` values, row-major: frame 0's channels in
    // declaration order, then frame 1's.
    std::vector<float> values;

    // The row of `frameIndex`, or nullptr when it is out of range. The row is
    // `channelCount` values wide.
    MOTIONBVH_API const float* Frame(std::size_t frameIndex) const noexcept;

    // The value of `jointIndex`'s `channelIndex`-th *declared* channel in
    // `frameIndex` — so `channelIndex` is an index into that joint's `channels`
    // vector, not one of the six `BvhChannel` values. Nullopt when any index is
    // out of range.
    MOTIONBVH_API std::optional<float> ChannelValue(
        std::size_t frameIndex, std::size_t jointIndex,
        std::size_t channelIndex) const noexcept;

    // The first joint with this name, exactly as the file spelled it. Nullopt
    // when there is none.
    MOTIONBVH_API std::optional<std::size_t> FindJoint(
        std::string_view name) const;

    // Every joint with this name. A profile matching joints by name needs to
    // see an ambiguity rather than silently take the first one, which is the
    // whole reason this exists beside `FindJoint`.
    MOTIONBVH_API std::vector<std::size_t> FindJoints(
        std::string_view name) const;

    MOTIONBVH_API bool HasUniqueJointNames() const;

    // Root-relative depth: 0 for the `ROOT`, 1 for its children. Zero for an
    // out-of-range index, and zero for a parent chain that cycles — neither is
    // a depth, and a document carrying either is one `ValidateBvhDocument`
    // rejects. Both answers exist because this is a plain struct: the parser
    // cannot produce such a document, and a caller assembling one by hand can.
    MOTIONBVH_API std::size_t Depth(std::size_t jointIndex) const noexcept;

    // The deepest joint's depth, or 0 for an empty document.
    MOTIONBVH_API std::size_t MaxDepth() const noexcept;

    // The total across every joint, which `channelCount` must equal.
    MOTIONBVH_API std::size_t TotalDeclaredChannels() const noexcept;
};

// Whether a document is internally consistent: parents precede children, the
// root is the only parentless joint, channel offsets follow declaration order,
// `channelCount` agrees with the joints, `values` is exactly
// `frameCount * channelCount` long, and every number is finite.
//
// The parser produces nothing else, so this is not a re-check of it. It exists
// because a `BvhDocument` is a plain struct that a caller — a test, a future
// generator, an inspect tool building one to print — can also assemble by hand,
// and every layer above is entitled to assume these invariants rather than
// re-derive them. Failures are `VRM_BVH_PARSE_FAILED`: a document that cannot
// have come from a file is not a semantic disagreement.
MOTIONBVH_API bool ValidateBvhDocument(const BvhDocument& document,
                                       Diagnostic* diagnostic = nullptr);

} // namespace motionBvh
