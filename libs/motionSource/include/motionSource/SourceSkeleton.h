// SPDX-License-Identifier: Apache-2.0
//
// The rig a recorded file describes, with no file format and no producer in it.
//
// This is the layer a reader hands its extraction to and a producer profile is
// matched against (roadmap/recorded-motion-sources.md §2). It carries joint
// names as the writer spelled them, the parent relation, and each joint's rest
// offset from its parent. It carries no unit, no up axis, no handedness, no
// humanoid bone, and no target rig — the first four are a *writer's* answers and
// live in a profile, and the last is `vrmRetarget`'s and lives one pipeline
// stage further on.
//
// Four decisions, each a choice rather than a detail.
//
// **The values stay in the source's own convention, and are therefore not
// geometric vectors.** There is deliberately no `GfVec3f` here, for the reason
// the syntax layer below states for its own triples: a `GfVec3f` implies a
// coordinate system, and nothing at this layer knows the file's handedness, up
// axis, or unit. The basis change happens in the converter, which has a profile
// in hand. That this library links `motionCore` and could have borrowed Gf makes
// the omission a statement rather than an accident.
//
// **A skeleton is a value on its own, not a member of an animation.** A profile
// is matched against a rig — joint names, hierarchy shape, a root — and that
// match is worth making before a single frame is read, both to refuse early and
// so a detector can report candidates for a file it has only skimmed. An
// animation that owned its skeleton would make every such caller carry frames it
// has no use for.
//
// **A rest rotation is optional, because sources disagree about whether they
// have one.** A format whose rest pose is offsets alone leaves it unset; one
// that states a rest orientation per joint fills it in. An identity default
// would have made those two indistinguishable, and the difference matters to a
// converter building a source rest pose.
//
// **A leaf's tip is not a joint.** A source that marks where a chain ends —
// giving a terminator an offset and no name, no samples, and no children — puts
// that offset on its parent as `tipOffset`. Modelling it as a nameless joint
// would put something a profile can never map into every loop that walks joints,
// and every one of those loops would have to skip it.
#pragma once

#include "motionSource/api.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace motionSource
{

// Three numbers in the source's own basis and unit. See the header note above.
struct SourceVec3
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    friend bool operator==(const SourceVec3& lhs,
                           const SourceVec3& rhs) noexcept
    {
        return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
    }
    friend bool operator!=(const SourceVec3& lhs,
                           const SourceVec3& rhs) noexcept
    {
        return !(lhs == rhs);
    }
};

// A rotation as the source stated it, about the source's own axes. Real part
// first, matching the `GfQuatf` constructor a converter will hand it to, so a
// field order cannot quietly reverse at that boundary.
//
// Not normalised here and not required to be: a source that wrote a
// slightly-off-unit quaternion wrote it, and a value type that silently
// corrected it would hide the writer from whoever is judging the export. Only
// finiteness and a non-zero magnitude are required, because neither of those has
// an interpretation at any layer above.
struct SourceQuat
{
    float w = 1.0f;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    friend bool operator==(const SourceQuat& lhs,
                           const SourceQuat& rhs) noexcept
    {
        return lhs.w == rhs.w && lhs.x == rhs.x && lhs.y == rhs.y
               && lhs.z == rhs.z;
    }
    friend bool operator!=(const SourceQuat& lhs,
                           const SourceQuat& rhs) noexcept
    {
        return !(lhs == rhs);
    }
};

struct SourceJoint
{
    // Verbatim from the source. Sources do not require joint names to be unique
    // and real exports repeat them, so this is not a key — see `FindJoints`.
    std::string name;
    // Index into `SourceSkeleton::joints`, or -1 for the root. Always less than
    // this joint's own index: parents are stored before their children.
    int parent = -1;
    // Rest offset from the parent joint, in the source's own basis and unit.
    SourceVec3 restTranslation;
    // Rest orientation, for sources that state one. See the header note.
    std::optional<SourceQuat> restRotation;
    // Where this joint's chain ends, when the source marks it. A direction
    // marker or a bone length depending on the writer — which one is a profile's
    // answer, not this layer's.
    std::optional<SourceVec3> tipOffset;

    MOTIONSOURCE_API friend bool operator==(const SourceJoint& lhs,
                                            const SourceJoint& rhs) noexcept;
    MOTIONSOURCE_API friend bool operator!=(const SourceJoint& lhs,
                                            const SourceJoint& rhs) noexcept;
};

struct SourceSkeleton
{
    // Parent before child, the root at index 0. Exactly one root: a file
    // describing two disconnected rigs is refused by its reader rather than
    // represented here, because every layer above — a profile's required-joint
    // check, a root policy, a rest pose — is written for one character and would
    // silently describe only the first.
    std::vector<SourceJoint> joints;

    // The first joint with this name, exactly as the source spelled it. Nullopt
    // when there is none.
    MOTIONSOURCE_API std::optional<std::size_t> FindJoint(
        std::string_view name) const;

    // Every joint with this name. A profile maps joints by name, so it needs to
    // see an ambiguity rather than silently take the first one — which is the
    // whole reason this exists beside `FindJoint`.
    MOTIONSOURCE_API std::vector<std::size_t> FindJoints(
        std::string_view name) const;

    MOTIONSOURCE_API bool HasUniqueJointNames() const;

    // Root-relative depth: 0 for the root, 1 for its children. Zero for an
    // out-of-range index, and zero for a parent chain that cycles — neither is a
    // depth, and a skeleton carrying either is one `ValidateSourceSkeleton`
    // rejects. Both answers exist because this is a plain struct: a reader
    // cannot produce such a skeleton, and a caller assembling one by hand can.
    MOTIONSOURCE_API std::size_t Depth(std::size_t jointIndex) const noexcept;

    MOTIONSOURCE_API std::size_t MaxDepth() const noexcept;

    // Indices of `jointIndex`'s immediate children, in joint order.
    MOTIONSOURCE_API std::vector<std::size_t> ChildJoints(
        std::size_t jointIndex) const;

    MOTIONSOURCE_API friend bool operator==(const SourceSkeleton& lhs,
                                            const SourceSkeleton& rhs) noexcept;
    MOTIONSOURCE_API friend bool operator!=(const SourceSkeleton& lhs,
                                            const SourceSkeleton& rhs) noexcept;
};

// Whether a skeleton is internally consistent: it has at least one joint, joint
// 0 is the only root, every other parent index precedes its own joint, no name
// is empty, and every number is finite with no zero-magnitude rest rotation.
//
// A reader produces nothing else, so this is not a re-check of one. It exists
// because a `SourceSkeleton` is a plain struct that a test, a profile fixture,
// or a future reader can also assemble by hand, and the profile and converter
// layers above are entitled to these invariants rather than to a re-derivation
// of them.
//
// `reason` is plain text and not a diagnostic code, deliberately. The frozen
// diagnostic set (roadmap §6) belongs to a **reader's** path and is named after
// the format that reader reads — so a library forbidden to know which readers
// exist cannot raise a code from it without becoming the reversal WORKSPACE.md
// §2 names. A caller holding both a reader and this layer is the one that can
// say which code a structural refusal maps to.
MOTIONSOURCE_API bool ValidateSourceSkeleton(const SourceSkeleton& skeleton,
                                             std::string* reason = nullptr);

} // namespace motionSource
