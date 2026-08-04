// SPDX-License-Identifier: Apache-2.0
#include "motionSource/SourceSkeleton.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <utility>

namespace motionSource
{
namespace
{

bool
IsFinite(const SourceVec3& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y)
           && std::isfinite(value.z);
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
JointLabel(const SourceSkeleton& skeleton, std::size_t index)
{
    // Both the index and the name: a source that repeats a name would otherwise
    // produce two identical refusals for two different joints.
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "joint %zu", index);
    return std::string(buffer) + " '" + skeleton.joints[index].name + "'";
}

} // namespace

bool
operator==(const SourceJoint& lhs, const SourceJoint& rhs) noexcept
{
    return lhs.name == rhs.name && lhs.parent == rhs.parent
           && lhs.restTranslation == rhs.restTranslation
           && lhs.restRotation == rhs.restRotation
           && lhs.tipOffset == rhs.tipOffset;
}

bool
operator!=(const SourceJoint& lhs, const SourceJoint& rhs) noexcept
{
    return !(lhs == rhs);
}

bool
operator==(const SourceSkeleton& lhs, const SourceSkeleton& rhs) noexcept
{
    return lhs.joints == rhs.joints;
}

bool
operator!=(const SourceSkeleton& lhs, const SourceSkeleton& rhs) noexcept
{
    return !(lhs == rhs);
}

std::optional<std::size_t>
SourceSkeleton::FindJoint(std::string_view name) const
{
    for (std::size_t i = 0; i < joints.size(); ++i) {
        if (joints[i].name == name) {
            return i;
        }
    }
    return std::nullopt;
}

std::vector<std::size_t>
SourceSkeleton::FindJoints(std::string_view name) const
{
    std::vector<std::size_t> found;
    for (std::size_t i = 0; i < joints.size(); ++i) {
        if (joints[i].name == name) {
            found.push_back(i);
        }
    }
    return found;
}

bool
SourceSkeleton::HasUniqueJointNames() const
{
    for (std::size_t i = 0; i < joints.size(); ++i) {
        for (std::size_t j = i + 1; j < joints.size(); ++j) {
            if (joints[i].name == joints[j].name) {
                return false;
            }
        }
    }
    return true;
}

std::size_t
SourceSkeleton::Depth(std::size_t jointIndex) const noexcept
{
    if (jointIndex >= joints.size()) {
        return 0;
    }
    std::size_t depth = 0;
    std::size_t current = jointIndex;
    // Bounded by the joint count: a cycle a hand-built skeleton can contain
    // would otherwise walk forever, and this is called from `MaxDepth` over
    // every joint.
    for (std::size_t step = 0; step < joints.size(); ++step) {
        const int parent = joints[current].parent;
        if (parent < 0) {
            return depth;
        }
        const std::size_t parentIndex = static_cast<std::size_t>(parent);
        if (parentIndex >= joints.size()) {
            return 0;
        }
        current = parentIndex;
        ++depth;
    }
    return 0;
}

std::size_t
SourceSkeleton::MaxDepth() const noexcept
{
    std::size_t deepest = 0;
    for (std::size_t i = 0; i < joints.size(); ++i) {
        const std::size_t depth = Depth(i);
        if (depth > deepest) {
            deepest = depth;
        }
    }
    return deepest;
}

std::vector<std::size_t>
SourceSkeleton::ChildJoints(std::size_t jointIndex) const
{
    std::vector<std::size_t> children;
    if (jointIndex >= joints.size()) {
        return children;
    }
    for (std::size_t i = 0; i < joints.size(); ++i) {
        if (joints[i].parent >= 0
            && static_cast<std::size_t>(joints[i].parent) == jointIndex) {
            children.push_back(i);
        }
    }
    return children;
}

bool
ValidateSourceSkeleton(const SourceSkeleton& skeleton, std::string* reason)
{
    if (skeleton.joints.empty()) {
        return Fail(reason, "skeleton carries no joints");
    }
    if (skeleton.joints[0].parent != -1) {
        return Fail(reason, "joint 0 is not the root");
    }

    // Filled as the parents are walked below, so the tip check afterwards costs
    // one pass rather than a `ChildJoints` call per joint.
    std::vector<bool> hasChildren(skeleton.joints.size(), false);

    for (std::size_t i = 0; i < skeleton.joints.size(); ++i) {
        const SourceJoint& joint = skeleton.joints[i];
        if (joint.name.empty()) {
            char buffer[64];
            std::snprintf(buffer, sizeof(buffer), "joint %zu has no name", i);
            return Fail(reason, buffer);
        }
        if (i > 0) {
            if (joint.parent < 0) {
                return Fail(reason,
                            JointLabel(skeleton, i)
                                + " is a second root; a skeleton has one");
            }
            if (static_cast<std::size_t>(joint.parent) >= i) {
                return Fail(reason,
                            JointLabel(skeleton, i)
                                + " does not follow its parent");
            }
            hasChildren[static_cast<std::size_t>(joint.parent)] = true;
        }
        if (!IsFinite(joint.restTranslation)) {
            return Fail(reason,
                        JointLabel(skeleton, i)
                            + " has a non-finite rest translation");
        }
        if (joint.restRotation) {
            if (!IsFinite(*joint.restRotation)) {
                return Fail(reason,
                            JointLabel(skeleton, i)
                                + " has a non-finite rest rotation");
            }
            if (IsZero(*joint.restRotation)) {
                return Fail(reason,
                            JointLabel(skeleton, i)
                                + " has a zero-magnitude rest rotation");
            }
        }
        if (joint.tipOffset && !IsFinite(*joint.tipOffset)) {
            return Fail(reason,
                        JointLabel(skeleton, i)
                            + " has a non-finite tip offset");
        }
    }

    // A tip says where a chain *ends*, so a joint that has children has not
    // ended and its tip has no meaning any layer above could act on. Checked
    // after the walk because it is the one invariant that needs the whole
    // hierarchy rather than one joint and its parent.
    for (std::size_t i = 0; i < skeleton.joints.size(); ++i) {
        if (skeleton.joints[i].tipOffset && hasChildren[i]) {
            return Fail(reason,
                        JointLabel(skeleton, i)
                            + " has a tip offset and children");
        }
    }

    if (reason) {
        reason->clear();
    }
    return true;
}

} // namespace motionSource
