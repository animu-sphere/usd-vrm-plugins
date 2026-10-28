// SPDX-License-Identifier: Apache-2.0
#include "vrmRig/SpringBoneSolver.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace vrmRig
{
namespace
{
using pxr::GfQuatf;
using pxr::GfVec3f;
using Transform = SpringBoneTransform;

bool
Fail(std::string* error, const std::string& message)
{
    if (error)
        *error = message;
    return false;
}
bool
Finite(const GfVec3f& v)
{
    return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
}
bool
Valid(const Transform& t)
{
    const float norm = t.rotation.GetLength();
    return Finite(t.translation) && Finite(t.rotation.GetImaginary()) &&
           std::isfinite(t.rotation.GetReal()) && std::isfinite(norm) &&
           std::abs(norm - 1.0f) <= 1e-5f;
}
bool
Nonnegative(float f)
{
    return std::isfinite(f) && f >= 0.0f;
}
bool
Valid(const SpringBoneParameters& p)
{
    return Nonnegative(p.stiffness) && Nonnegative(p.dragForce) && p.dragForce <= 1.0f &&
           Nonnegative(p.gravityPower) && Nonnegative(p.hitRadius) && Finite(p.gravityDirection);
}
GfVec3f
Point(const Transform& t, const GfVec3f& p)
{
    return t.translation + t.rotation.Transform(p);
}
GfVec3f
InversePoint(const Transform& t, const GfVec3f& p)
{
    return t.rotation.GetInverse().Transform(p - t.translation);
}
Transform
Compose(const Transform& parent, const Transform& local)
{
    return {Point(parent, local.translation), (parent.rotation * local.rotation).GetNormalized()};
}
std::vector<Transform>
World(const SpringBoneRig& rig, const SpringBonePose& pose)
{
    std::vector<Transform> result(rig.parents.size());
    for (size_t i = 0; i < result.size(); ++i)
        result[i] = Compose(rig.parents[i] < 0 ? pose.placement : result[rig.parents[i]],
                            pose.localTransforms[i]);
    return result;
}
GfVec3f
Unit(const GfVec3f& v, const GfVec3f& fallback)
{
    const float scale = std::max({std::abs(v[0]), std::abs(v[1]), std::abs(v[2])});
    if (scale <= 1e-8f)
        return fallback;
    // Scale before squaring so large but finite forces do not normalize to
    // zero when their squared magnitude overflows a float.
    const GfVec3f scaled = v / scale;
    return scaled / scaled.GetLength();
}
GfQuatf
FromTo(const GfVec3f& from, const GfVec3f& to)
{
    const float dot = std::clamp(pxr::GfDot(from, to), -1.0f, 1.0f);
    if (dot < -0.999999f)
    {
        const GfVec3f basis = std::abs(from[0]) < 0.9f ? GfVec3f(1, 0, 0) : GfVec3f(0, 1, 0);
        return GfQuatf(0.0f, pxr::GfCross(from, basis).GetNormalized());
    }
    return GfQuatf(1.0f + dot, pxr::GfCross(from, to)).GetNormalized();
}
} // namespace

bool
SpringBoneSolver::Configure(const SpringBoneRig& rig, std::string* error)
{
    const int count = static_cast<int>(rig.parents.size());
    const auto index = [count](int i) { return i >= 0 && i < count; };
    if (rig.restTransforms.size() != rig.parents.size())
        return Fail(error, "spring rig: restTransforms must match parents");
    std::vector<std::vector<int>> children(count);
    for (int i = 0; i < count; ++i)
    {
        if (rig.parents[i] < -1 || rig.parents[i] >= i || !Valid(rig.restTransforms[i]))
            return Fail(error, "spring rig: invalid parent order or rest transform at joint " +
                                   std::to_string(i));
        if (rig.parents[i] >= 0)
            children[rig.parents[i]].push_back(i);
    }
    for (const auto& collider : rig.colliders)
        if ((collider.joint != -1 && !index(collider.joint)) || !Finite(collider.offset) ||
            !Finite(collider.tail) || !Nonnegative(collider.radius) ||
            (collider.shape != SpringBoneColliderShape::Sphere &&
             collider.shape != SpringBoneColliderShape::Capsule))
            return Fail(error, "spring rig: invalid collider");
    for (const auto& group : rig.colliderGroups)
        for (int c : group)
            if (c < 0 || c >= static_cast<int>(rig.colliders.size()))
                return Fail(error, "spring rig: invalid collider index");

    std::vector<Joint> joints;
    std::vector<int> owner(count, -1);
    for (size_t s = 0; s < rig.springs.size(); ++s)
    {
        const auto& spring = rig.springs[s];
        if (spring.joints.empty() == spring.rootJoints.empty())
            return Fail(error, "spring rig: declare exactly one nonempty chain or root array");
        const bool roots = !spring.rootJoints.empty();
        const auto& declaration = roots ? spring.rootJoints : spring.joints;
        if (spring.parameters.size() != declaration.size())
            return Fail(error, "spring rig: parameters must match the declaration");
        for (size_t i = 0; i < declaration.size(); ++i)
        {
            if (!index(declaration[i]) || !Valid(spring.parameters[i]))
                return Fail(error, "spring rig: invalid joint or parameters");
            if (!roots && i && rig.parents[declaration[i]] != declaration[i - 1])
                return Fail(error, "spring rig: chain joints must be direct children");
        }
        if (spring.center != -1)
        {
            if (!index(spring.center))
                return Fail(error, "spring rig: invalid center");
            for (int root : declaration)
            {
                int ancestor = root;
                while (ancestor >= 0 && ancestor != spring.center)
                    ancestor = rig.parents[ancestor];
                if (ancestor < 0)
                    return Fail(error,
                                "spring rig: center must be a common ancestor or the root itself");
            }
        }
        std::vector<int> colliders;
        for (int g : spring.colliderGroups)
        {
            if (g < 0 || g >= static_cast<int>(rig.colliderGroups.size()))
                return Fail(error, "spring rig: invalid collider group");
            for (int c : rig.colliderGroups[g])
                if (std::find(colliders.begin(), colliders.end(), c) == colliders.end())
                    colliders.push_back(c);
        }

        // Resolve nested roots by topology, not declaration order. The highest
        // declared ancestor owns the subtree's parameters, and each joint runs once.
        std::vector<int> parameterIndex(count, -1);
        for (size_t i = 0; i < declaration.size(); ++i)
        {
            if (parameterIndex[declaration[i]] >= 0)
                return Fail(error, "spring rig: duplicate declared joint");
            parameterIndex[declaration[i]] = static_cast<int>(i);
        }
        if (roots)
            for (int i = 0; i < count; ++i)
                if (rig.parents[i] >= 0 && parameterIndex[rig.parents[i]] >= 0)
                    parameterIndex[i] = parameterIndex[rig.parents[i]];

        for (int i = 0; i < count; ++i)
        {
            if (parameterIndex[i] < 0)
                continue;
            if (owner[i] >= 0)
                return Fail(error, "spring rig: joint belongs to multiple springs");
            owner[i] = static_cast<int>(s);
            const int p = parameterIndex[i];
            if (!roots && p + 1 == static_cast<int>(declaration.size()))
                continue;
            Joint joint;
            joint.index = i;
            joint.center = spring.center;
            joint.parameters = spring.parameters[p];
            joint.colliders = colliders;
            if (!roots)
                joint.localTail = rig.restTransforms[declaration[p + 1]].translation;
            else if (!children[i].empty())
                joint.localTail = rig.restTransforms[children[i].front()].translation;
            else
            {
                // UniVRM's 7 cm continuation of the incoming world direction,
                // expressed back in the leaf's local rest frame.
                joint.localTail = rig.restTransforms[i].rotation.GetInverse().Transform(
                                      Unit(rig.restTransforms[i].translation, GfVec3f(0.0f))) *
                                  0.07f;
            }
            if (joint.localTail.GetLength() <= 1e-8f ||
                !std::isfinite(joint.localTail.GetLength()) || !Finite(joint.localTail))
                return Fail(error, "spring rig: zero-length tail at joint " + std::to_string(i));
            joints.push_back(std::move(joint));
        }
    }
    std::sort(joints.begin(), joints.end(),
              [](const Joint& a, const Joint& b) { return a.index < b.index; });
    std::vector<bool> driven(count, false);
    for (const auto& joint : joints)
        driven[joint.index] = true;
    const auto hasDrivenAncestor = [&](int i)
    {
        for (; i >= 0; i = rig.parents[i])
            if (driven[i])
                return true;
        return false;
    };
    for (const auto& joint : joints)
    {
        if (joint.center >= 0 && hasDrivenAncestor(rig.parents[joint.center]))
            return Fail(error, "spring rig: center cannot descend from a simulated joint");
        for (int c : joint.colliders)
            if (hasDrivenAncestor(rig.colliders[c].joint))
                return Fail(error,
                            "spring rig: collider cannot be attached to a simulated subtree");
    }
    _rig = rig;
    _joints = std::move(joints);
    _configured = true;
    _initialized = false;
    if (error)
        error->clear();
    return true;
}

bool
SpringBoneSolver::ValidatePose(const SpringBonePose& pose, SpringBoneResult* result,
                               std::string* error) const
{
    if (!_configured)
        return Fail(error, "spring solver: Configure is required");
    if (!result)
        return Fail(error, "spring solver: result is required");
    if (pose.localTransforms.size() != _rig.parents.size() || !Valid(pose.placement))
        return Fail(error, "spring solver: invalid pose size or placement");
    for (size_t i = 0; i < pose.localTransforms.size(); ++i)
        if (!Valid(pose.localTransforms[i]) ||
            (pose.localTransforms[i].translation - _rig.restTransforms[i].translation).GetLength() >
                1e-6f)
            return Fail(
                error,
                "spring solver: pose must have finite rigid transforms and rest translations");
    for (const auto& t : World(_rig, pose))
        if (!Valid(t))
            return Fail(error, "spring solver: world transform overflow");
    return true;
}

bool
SpringBoneSolver::Reset(const SpringBonePose& pose, SpringBoneResult* result, std::string* error)
{
    if (!ValidatePose(pose, result, error))
        return false;
    const auto world = World(_rig, pose);
    SpringBoneResult output;
    auto joints = _joints;
    for (const auto& t : pose.localTransforms)
        output.localRotations.push_back(t.rotation);
    for (auto& joint : joints)
    {
        const auto tail = Point(world[joint.index], joint.localTail);
        if (!Finite(tail))
            return Fail(error, "spring solver: tail position overflow");
        joint.current = joint.previous =
            joint.center < 0 ? tail : InversePoint(world[joint.center], tail);
        if (!Finite(joint.current))
            return Fail(error, "spring solver: center position overflow");
        output.tails.push_back({joint.index, tail});
    }
    *result = std::move(output);
    _joints = std::move(joints);
    _initialized = true;
    if (error)
        error->clear();
    return true;
}

bool
SpringBoneSolver::Step(float seconds, const SpringBonePose& pose, SpringBoneResult* result,
                       std::string* error)
{
    if (!ValidatePose(pose, result, error))
        return false;
    if (!_initialized)
        return Fail(error, "spring solver: Reset is required before Step");
    if (!std::isfinite(seconds) || seconds <= 0.0f)
        return Fail(error, "spring solver: step duration must be finite and positive");
    const auto animatedWorld = World(_rig, pose);
    auto locals = pose.localTransforms;
    auto world = animatedWorld;
    auto joints = _joints;
    SpringBoneResult output;
    size_t cursor = 0;
    for (size_t i = 0; i < _rig.parents.size(); ++i)
    {
        const Transform& parent = _rig.parents[i] < 0 ? pose.placement : world[_rig.parents[i]];
        world[i] = Compose(parent, locals[i]);
        if (cursor >= joints.size() || joints[cursor].index != static_cast<int>(i))
            continue;
        auto& joint = joints[cursor++];
        const Transform center = joint.center < 0 ? Transform{} : animatedWorld[joint.center];
        const auto& p = joint.parameters;
        const float length = joint.localTail.GetLength();
        const GfVec3f axis = joint.localTail / length;
        const GfVec3f restDirection = world[i].rotation.Transform(axis);
        const GfVec3f head = world[i].translation;
        GfVec3f next =
            Point(center, joint.current + (joint.current - joint.previous) * (1.0f - p.dragForce)) +
            restDirection * (p.stiffness * seconds) +
            p.gravityDirection * (p.gravityPower * seconds);
        next = head + Unit(next - head, restDirection) * length;
        for (int c : joint.colliders)
        {
            const auto& collider = _rig.colliders[c];
            const Transform attachment =
                collider.joint < 0 ? Transform{} : animatedWorld[collider.joint];
            GfVec3f closest = Point(attachment, collider.offset);
            if (collider.shape == SpringBoneColliderShape::Capsule)
            {
                const GfVec3f segment = Point(attachment, collider.tail) - closest;
                const float squared = segment.GetLengthSq();
                if (squared > 1e-16f)
                    closest += segment * std::clamp(pxr::GfDot(next - closest, segment) / squared,
                                                    0.0f, 1.0f);
            }
            const GfVec3f delta = next - closest;
            const float radius = collider.radius + p.hitRadius;
            if (delta.GetLengthSq() < radius * radius)
            {
                // Coincident centers have no unique normal. Pick a stable
                // perpendicular so the projection does not collapse to zero.
                const GfVec3f basis =
                    std::abs(restDirection[0]) < 0.9f ? GfVec3f(1, 0, 0) : GfVec3f(0, 1, 0);
                next = closest +
                       Unit(delta, pxr::GfCross(restDirection, basis).GetNormalized()) * radius;
                next = head + Unit(next - head, restDirection) * length;
            }
        }
        joint.previous = joint.current;
        joint.current = InversePoint(center, next);
        const GfVec3f localDirection =
            world[i].rotation.GetInverse().Transform(Unit(next - head, restDirection));
        locals[i].rotation = (locals[i].rotation * FromTo(axis, localDirection)).GetNormalized();
        world[i] = Compose(parent, locals[i]);
        if (!Finite(next) || !Finite(joint.current) || !Valid(world[i]))
            return Fail(error, "spring solver: numeric overflow; reset after correcting input");
        output.tails.push_back({static_cast<int>(i), next});
    }
    for (const auto& t : locals)
        output.localRotations.push_back(t.rotation);
    _joints = std::move(joints);
    *result = std::move(output);
    if (error)
        error->clear();
    return true;
}

} // namespace vrmRig
