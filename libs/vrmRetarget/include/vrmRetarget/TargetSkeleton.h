// SPDX-License-Identifier: Apache-2.0
//
// A target rig described as plain values.
//
// This is deliberately not a UsdSkelSkeleton. vrmRetarget never opens a stage:
// the caller reads the skeleton off the stage and hands the values in, so the
// retarget core stays testable without USD composition and reusable by a live
// source that has no stage at all (WORKSPACE.md §2).
#pragma once

#include "vrmRetarget/api.h"

#include "pxr/base/gf/quatf.h"
#include "pxr/base/gf/vec3f.h"

#include <cstddef>
#include <string>
#include <vector>

namespace vrmRetarget
{

// One joint of the target rig, in the rig's own joint order.
struct TargetJoint
{
    // The joint's token exactly as it appears in UsdSkelSkeleton.joints — a
    // full joint path such as "Root/Pelvis/SpineA", not the leaf name.
    std::string token;

    // Index into TargetSkeleton::joints, or kNoParent for a root joint.
    int parent = -1;

    // Rest transform, decomposed. Scale is not carried: the motion contract
    // ignores scale channels, and a retargeted clip never authors one.
    pxr::GfQuatf restRotation = pxr::GfQuatf(1.0f, pxr::GfVec3f(0.0f));
    pxr::GfVec3f restTranslation = pxr::GfVec3f(0.0f);
};

class VRMRETARGET_API TargetSkeleton
{
public:
    static constexpr int kNoParent = -1;

    TargetSkeleton() = default;
    explicit TargetSkeleton(std::vector<TargetJoint> joints)
        : _joints(std::move(joints))
    {
    }

    const std::vector<TargetJoint>& GetJoints() const noexcept
    {
        return _joints;
    }
    std::size_t GetSize() const noexcept { return _joints.size(); }
    bool IsEmpty() const noexcept { return _joints.empty(); }

    void AddJoint(const TargetJoint& joint) { _joints.push_back(joint); }

    // Returns the joint's index, or kNoParent when no joint carries the token.
    // Matching is exact on the full joint path.
    int FindJoint(const std::string& token) const;

    // Derives `parent` for every joint from the "a/b/c" joint-path convention
    // UsdSkelSkeleton uses. A joint whose parent path is absent from the
    // skeleton is treated as a root. Call this after populating tokens when the
    // source did not supply parent indices.
    void ResolveParentsFromTokens();

    // True when every parent index is either kNoParent or a strictly smaller
    // index — UsdSkelSkeleton requires parents to precede their children.
    bool IsTopologicallyOrdered() const;

    // The joint's rest orientation in skeleton space: its own rest rotation
    // with every ancestor's composed on the left, root-first. kNoParent — or
    // any out-of-range index — yields identity, which is exactly what a root
    // joint's absent parent contributes to a rest-pose correction.
    pxr::GfQuatf GetWorldRestRotation(int jointIndex) const;

private:
    std::vector<TargetJoint> _joints;
};

} // namespace vrmRetarget
