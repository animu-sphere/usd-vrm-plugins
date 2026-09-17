// SPDX-License-Identifier: Apache-2.0
#include "vrmRetarget/TargetSkeleton.h"

#include "pxr/base/gf/quatd.h"
#include "pxr/base/gf/vec3d.h"

#include <unordered_map>

namespace vrmRetarget
{

int
TargetSkeleton::FindJoint(const std::string& token) const
{
    for (std::size_t i = 0; i < _joints.size(); ++i)
    {
        if (_joints[i].token == token)
        {
            return static_cast<int>(i);
        }
    }
    return kNoParent;
}

void
TargetSkeleton::ResolveParentsFromTokens()
{
    std::unordered_map<std::string, int> byToken;
    byToken.reserve(_joints.size());
    for (std::size_t i = 0; i < _joints.size(); ++i)
    {
        byToken.emplace(_joints[i].token, static_cast<int>(i));
    }

    for (std::size_t i = 0; i < _joints.size(); ++i)
    {
        TargetJoint& joint = _joints[i];
        const std::size_t separator = joint.token.rfind('/');
        if (separator == std::string::npos)
        {
            joint.parent = kNoParent;
            continue;
        }
        const auto found = byToken.find(joint.token.substr(0, separator));
        // A joint whose parent path is not itself a joint of this skeleton is
        // treated as a root rather than dangling.
        joint.parent = (found == byToken.end()) ? kNoParent : found->second;
    }
}

pxr::GfQuatf
TargetSkeleton::GetWorldRestRotation(int jointIndex) const
{
    static const pxr::GfQuatf identity(1.0f, pxr::GfVec3f(0.0f));
    // world = R_root * ... * R_parent * R_joint, so each ancestor composes on
    // the left as the walk climbs. The depth cap terminates a malformed parent
    // cycle; a well-formed skeleton never revisits a joint.
    pxr::GfQuatf world = identity;
    int cursor = jointIndex;
    for (std::size_t depth = 0;
         depth < _joints.size() && cursor >= 0 && static_cast<std::size_t>(cursor) < _joints.size();
         ++depth)
    {
        const TargetJoint& joint = _joints[static_cast<std::size_t>(cursor)];
        world = joint.restRotation.GetNormalized() * world;
        cursor = joint.parent;
    }
    return world.GetNormalized();
}

bool
TargetSkeleton::IsTopologicallyOrdered() const
{
    for (std::size_t i = 0; i < _joints.size(); ++i)
    {
        const int parent = _joints[i].parent;
        if (parent == kNoParent)
        {
            continue;
        }
        if (parent < 0 || parent >= static_cast<int>(i))
        {
            return false;
        }
    }
    return true;
}

void
DecomposeRestTransform(const pxr::GfMatrix4d& matrix, TargetJoint* joint)
{
    const pxr::GfVec3d translation = matrix.ExtractTranslation();
    joint->restTranslation =
        pxr::GfVec3f(static_cast<float>(translation[0]), static_cast<float>(translation[1]),
                     static_cast<float>(translation[2]));
    const pxr::GfQuatd rotation = matrix.RemoveScaleShear().ExtractRotationQuat();
    joint->restRotation = pxr::GfQuatf(static_cast<float>(rotation.GetReal()),
                                       pxr::GfVec3f(static_cast<float>(rotation.GetImaginary()[0]),
                                                    static_cast<float>(rotation.GetImaginary()[1]),
                                                    static_cast<float>(rotation.GetImaginary()[2])))
                              .GetNormalized();
    // Row-vector convention: row i is basis axis i, rotated and scaled by the
    // i-th scale, so its length is that scale. A shear leaks into the lengths;
    // UsdSkel's own rest transforms carry none.
    joint->restScale = pxr::GfVec3f(static_cast<float>(matrix.GetRow3(0).GetLength()),
                                    static_cast<float>(matrix.GetRow3(1).GetLength()),
                                    static_cast<float>(matrix.GetRow3(2).GetLength()));
}

bool
operator==(const TargetJoint& a, const TargetJoint& b) noexcept
{
    return a.token == b.token && a.parent == b.parent && a.restRotation == b.restRotation &&
           a.restTranslation == b.restTranslation && a.restScale == b.restScale;
}

bool
operator!=(const TargetJoint& a, const TargetJoint& b) noexcept
{
    return !(a == b);
}

bool
operator==(const TargetSkeleton& a, const TargetSkeleton& b) noexcept
{
    return a.GetJoints() == b.GetJoints();
}

bool
operator!=(const TargetSkeleton& a, const TargetSkeleton& b) noexcept
{
    return !(a == b);
}

} // namespace vrmRetarget
