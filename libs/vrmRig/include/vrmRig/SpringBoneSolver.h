// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "vrmRig/api.h"
#include "pxr/base/gf/quatf.h"
#include "pxr/base/gf/vec3f.h"

#include <string>
#include <vector>

#define VRMRIG_SPRING_BONE_API 1

namespace vrmRig
{

// Rigid transforms only, in metres and one caller-selected world basis.
// Scale and stage-unit conversion belong to the stage reader (see README).
struct SpringBoneTransform
{
    pxr::GfVec3f translation = pxr::GfVec3f(0.0f);
    pxr::GfQuatf rotation = pxr::GfQuatf(1.0f);
};

struct SpringBoneParameters
{
    float stiffness = 1.0f;
    float dragForce = 0.4f;
    float gravityPower = 0.0f;
    pxr::GfVec3f gravityDirection = pxr::GfVec3f(0.0f, -1.0f, 0.0f);
    float hitRadius = 0.0f;
};

enum class SpringBoneColliderShape
{
    Sphere,
    Capsule
};

struct SpringBoneCollider
{
    // -1 attaches to world; otherwise offsets are local to this skeleton joint.
    int joint = -1;
    SpringBoneColliderShape shape = SpringBoneColliderShape::Sphere;
    pxr::GfVec3f offset = pxr::GfVec3f(0.0f);
    pxr::GfVec3f tail = pxr::GfVec3f(0.0f);
    float radius = 0.0f;
};

struct SpringBoneSpring
{
    // Author exactly one declaration. Chain joints must be direct children;
    // the final joint is a tail, never independently rotated (VRM 1.0).
    std::vector<int> joints;
    // VRM 0.x: expand all descendants, one solver joint per skeleton joint.
    std::vector<int> rootJoints;
    // Parallel to the selected declaration, including the final chain joint.
    std::vector<SpringBoneParameters> parameters;
    std::vector<int> colliderGroups;
    int center = -1;
};

struct SpringBoneRig
{
    // Skeleton order is topological: -1 for a root, otherwise parent < joint.
    // Child order (including the first child chosen on a VRM 0.x branch) is
    // skeleton order. Rest translations define lengths and virtual-tail axes.
    std::vector<int> parents;
    std::vector<SpringBoneTransform> restTransforms;
    std::vector<SpringBoneCollider> colliders;
    std::vector<std::vector<int>> colliderGroups;
    std::vector<SpringBoneSpring> springs;
};

struct SpringBonePose
{
    std::vector<SpringBoneTransform> localTransforms;
    SpringBoneTransform placement;
};

struct SpringBoneTail
{
    int joint = -1;
    pxr::GfVec3f position = pxr::GfVec3f(0.0f); // world metres
};

struct SpringBoneResult
{
    // Absolute joint-local rotations in full skeleton order, not deltas.
    // Unsimulated joints retain the input pose's rotation.
    std::vector<pxr::GfQuatf> localRotations;
    std::vector<SpringBoneTail> tails; // simulated joints, skeleton order
};

// Plain values; no stage, scheduler, accumulator, or rigid-body world.
// Configure validates transactionally and requires Reset before Step.
// Reset zeroes velocity at the supplied *current animated* pose. Each Step
// supplies that tick's unsimulated pose; never feed the previous result back
// as animation. Failed calls change neither state nor the caller's result.
class SpringBoneSolver
{
  public:
    VRMRIG_API bool Configure(const SpringBoneRig& rig, std::string* error = nullptr);
    VRMRIG_API bool Reset(const SpringBonePose& pose, SpringBoneResult* result,
                          std::string* error = nullptr);
    VRMRIG_API bool Step(float seconds, const SpringBonePose& pose, SpringBoneResult* result,
                         std::string* error = nullptr);

  private:
    struct Joint
    {
        int index = -1;
        int center = -1;
        pxr::GfVec3f localTail = pxr::GfVec3f(0.0f);
        SpringBoneParameters parameters;
        std::vector<int> colliders;
        pxr::GfVec3f current = pxr::GfVec3f(0.0f); // center space
        pxr::GfVec3f previous = pxr::GfVec3f(0.0f);
    };
    bool ValidatePose(const SpringBonePose& pose, SpringBoneResult* result,
                      std::string* error) const;
    SpringBoneRig _rig;
    std::vector<Joint> _joints;
    bool _configured = false;
    bool _initialized = false;
};

} // namespace vrmRig
