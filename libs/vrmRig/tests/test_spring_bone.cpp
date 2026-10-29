// SPDX-License-Identifier: Apache-2.0
#include "vrmRig/SpringBoneSolver.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>

namespace
{
using namespace vrmRig;
using pxr::GfQuatf;
using pxr::GfVec3f;

SpringBoneRig
Chain(bool roots = false)
{
    SpringBoneRig rig;
    rig.parents = {-1, 0, 1, 2};
    rig.restTransforms.resize(4);
    for (int i = 1; i < 4; ++i)
        rig.restTransforms[i].translation = GfVec3f(0, 0.3f, 0);
    SpringBoneSpring spring;
    if (roots)
        spring.rootJoints = {1};
    else
        spring.joints = {1, 2, 3};
    spring.parameters.resize(roots ? 1 : 3);
    for (auto& p : spring.parameters)
    {
        p.stiffness = 0.5f;
        p.gravityPower = 0.8f;
        p.gravityDirection = GfVec3f(1, 0, 0);
    }
    rig.springs = {spring};
    return rig;
}
SpringBonePose
Pose(const SpringBoneRig& rig)
{
    return {rig.restTransforms, {}};
}
bool
Near(const GfVec3f& a, const GfVec3f& b)
{
    return (a - b).GetLength() < 1e-5f;
}
void
Same(const SpringBoneResult& a, const SpringBoneResult& b)
{
    assert(a.localRotations == b.localRotations);
    assert(a.tails.size() == b.tails.size());
    for (size_t i = 0; i < a.tails.size(); ++i)
    {
        assert(a.tails[i].joint == b.tails[i].joint);
        assert(a.tails[i].position == b.tails[i].position);
    }
}
void
IndependentHistoryCopies()
{
    for (bool roots : {false, true})
    {
        const auto rig = Chain(roots);
        const auto pose = Pose(rig);
        SpringBoneSolver solver, control;
        SpringBoneResult a, b, trialResult;
        assert(solver.Configure(rig) && control.Configure(rig));
        assert(solver.Reset(pose, &a) && control.Reset(pose, &b));
        for (int i = 0; i < 20; ++i)
        {
            assert(solver.Step(1.0f / 60, pose, &a));
            assert(control.Step(1.0f / 60, pose, &b));
        }
        SpringBoneSolver trial(solver); // copy live, nonzero-velocity history
        for (int i = 0; i < 2; ++i)
        {
            if (i)
                trial = solver; // assignment replaces the independently reset rig/history
            assert(trial.Step(1.0f / 60, pose, &trialResult));
            assert(control.Step(1.0f / 60, pose, &b));
            Same(trialResult, b);
            auto otherPose = pose;
            otherPose.placement.translation = GfVec3f(20, 3, -4);
            assert(trial.Reset(otherPose, &trialResult));
            auto otherRig = rig;
            otherRig.springs[0].parameters[0].gravityPower = 12;
            assert(trial.Configure(otherRig));
            assert(trial.Reset(otherPose, &trialResult));
            assert(trial.Step(1.0f / 30, otherPose, &trialResult));
            // Discarding trial work leaves the original exactly on its old tick.
            assert(solver.Step(1.0f / 60, pose, &a));
            Same(a, b);
        }
    }
}
void
SettleAndReset()
{
    const auto rig = Chain();
    auto pose = Pose(rig);
    SpringBoneSolver solver;
    SpringBoneResult initial, a, b;
    std::string error;
    assert(!solver.Step(1.0f / 60, pose, &a, &error));
    assert(solver.Configure(rig, &error));
    assert(!solver.Step(1.0f / 60, pose, &a, &error));
    assert(solver.Reset(pose, &initial, &error) && error.empty());
    for (int i = 0; i < 400; ++i)
        assert(solver.Step(1.0f / 60, pose, &a));
    assert(a.tails.front().position[0] > 0.2f); // bends toward gravity
    assert(solver.Step(1.0f / 60, pose, &b));
    assert(Near(a.tails.front().position, b.tails.front().position));
    assert(a.localRotations[0] == pose.localTransforms[0].rotation);
    assert(a.localRotations[3] == pose.localTransforms[3].rotation); // terminal is a tail
    assert(solver.Reset(pose, &b));
    Same(initial, b);
    for (int i = 0; i < 400; ++i)
        assert(solver.Step(1.0f / 60, pose, &b));
    Same(a, b); // exact repeat on this build

    auto stiff = rig;
    for (auto& p : stiff.springs[0].parameters)
    {
        p.stiffness = 10;
        p.gravityPower = 0;
    }
    assert(solver.Configure(stiff));
    pose.localTransforms[1].rotation = GfQuatf(0.9238795f, GfVec3f(0, 0, 0.3826834f));
    assert(solver.Reset(pose, &initial));
    pose = Pose(stiff);
    for (int i = 0; i < 200; ++i)
        assert(solver.Step(1.0f / 60, pose, &a));
    assert(Near(a.tails.front().position, GfVec3f(0, 0.6f, 0))); // returns to animated rest

    pose.placement.translation = GfVec3f(20, -2, 4); // seek/teleport is reset
    assert(solver.Reset(pose, &a));
    assert(Near(a.tails.front().position, GfVec3f(20, -1.4f, 4)));
}
void
Branches()
{
    auto rig = Chain(true);
    rig.parents = {-1, 0, 1, 1, 3};
    rig.restTransforms.resize(5);
    rig.restTransforms[3].translation = GfVec3f(0.2f, 0.2f, 0);
    rig.restTransforms[4].translation = GfVec3f(0, 0.2f, 0);
    SpringBoneSolver a, b;
    SpringBoneResult x, y;
    assert(a.Configure(rig));
    auto nested = rig;
    nested.springs[0].rootJoints = {3, 1}; // descendant listed first
    nested.springs[0].parameters.resize(2, rig.springs[0].parameters[0]);
    nested.springs[0].parameters[0].gravityPower = 40; // ancestor wins
    assert(b.Configure(nested));
    const auto pose = Pose(rig);
    assert(a.Reset(pose, &x) && b.Reset(pose, &y));
    assert(x.tails.size() == 4);
    assert(Near(x.tails[1].position, GfVec3f(0, 0.67f, 0))); // 7 cm leaf continuation
    for (int i = 0; i < 60; ++i)
    {
        assert(a.Step(1.0f / 60, pose, &x) && b.Step(1.0f / 60, pose, &y));
        Same(x, y);
        for (int j = 0; j < 4; ++j)
            assert(x.tails[j].joint == j + 1);
    }
    // Both root declarations and chains reject cross-spring overlap.
    nested.springs.push_back(rig.springs[0]);
    assert(!b.Configure(nested));
    assert(b.Step(1.0f / 60, pose, &y)); // rejected configure preserved state
}
void
Collisions()
{
    for (auto shape : {SpringBoneColliderShape::Sphere, SpringBoneColliderShape::Capsule})
    {
        auto rig = Chain();
        rig.springs[0].joints = {1, 2};
        rig.springs[0].parameters.resize(2);
        for (auto& p : rig.springs[0].parameters)
        {
            p.gravityPower = 0;
            p.hitRadius = 0.02f;
        }
        SpringBoneCollider collider;
        collider.joint = 0;
        collider.shape = shape;
        collider.offset = GfVec3f(0.04f, 0.6f, -0.1f);
        collider.tail = GfVec3f(0.04f, 0.6f, 0.1f);
        if (shape == SpringBoneColliderShape::Sphere)
            collider.offset[2] = 0;
        collider.radius = 0.08f;
        rig.colliders = {collider};
        rig.colliderGroups = {{0}};
        rig.springs[0].colliderGroups = {0};
        SpringBoneSolver solver;
        SpringBoneResult out;
        assert(solver.Configure(rig) && solver.Reset(Pose(rig), &out));
        assert(solver.Step(1.0f / 60, Pose(rig), &out));
        assert(out.tails[0].position[0] < -0.05f);
        GfVec3f closest(0.04f, 0.6f, 0);
        assert((out.tails[0].position - closest).GetLength() > 0.099f);
        assert(std::abs((out.tails[0].position - GfVec3f(0, 0.3f, 0)).GetLength() - 0.3f) < 1e-5f);
        // Degenerate capsule is a sphere; coincident tail has a stable normal.
        rig.colliders[0].offset = rig.colliders[0].tail = GfVec3f(0, 0.6f, 0);
        assert(solver.Configure(rig) && solver.Reset(Pose(rig), &out));
        assert(solver.Step(1.0f / 60, Pose(rig), &out));
        assert(std::isfinite(out.tails[0].position[2]));
    }
}

void
RotatedLeaf()
{
    auto rig = Chain(true);
    rig.restTransforms[3].rotation = GfQuatf(std::sqrt(0.5f), GfVec3f(0, 0, std::sqrt(0.5f)));
    for (auto& p : rig.springs[0].parameters)
    {
        p.stiffness = 0;
        p.gravityPower = 0;
    }
    SpringBoneSolver solver;
    SpringBoneResult out;
    assert(solver.Configure(rig) && solver.Reset(Pose(rig), &out));
    // A rotated leaf still continues the incoming world direction, rather
    // than rotating that direction a second time by its own rest rotation.
    assert(Near(out.tails.back().position, GfVec3f(0, 0.97f, 0)));
    assert(solver.Step(1.0f / 60, Pose(rig), &out));
    assert(Near(out.tails.back().position, GfVec3f(0, 0.97f, 0)));
    assert(std::abs(pxr::GfDot(out.localRotations[3], rig.restTransforms[3].rotation)) > 0.99999f);
}
void
CenterAndValidation()
{
    auto rig = Chain();
    rig.springs[0].center = 0;
    for (auto& p : rig.springs[0].parameters)
    {
        p.stiffness = 0;
        p.gravityPower = 0;
    }
    SpringBoneSolver solver, control;
    SpringBoneResult a, b, before;
    auto pose = Pose(rig);
    assert(solver.Configure(rig) && control.Configure(rig));
    assert(solver.Reset(pose, &a) && control.Reset(pose, &b));
    pose.placement.translation = GfVec3f(3, 4, 5);
    pose.placement.rotation = GfQuatf(0.9238795f, GfVec3f(0, 0, 0.3826834f));
    assert(solver.Step(1.0f / 60, pose, &a));
    assert(Near(a.tails[0].position, pose.placement.translation +
                                         pose.placement.rotation.Transform(GfVec3f(0, 0.6f, 0))));
    assert(solver.Reset(Pose(rig), &a));
    before = a;
    for (float dt : {0.0f, -1.0f, std::numeric_limits<float>::infinity(),
                     std::numeric_limits<float>::quiet_NaN()})
    {
        assert(!solver.Step(dt, Pose(rig), &a));
        Same(before, a);
    }
    auto badPose = Pose(rig);
    badPose.localTransforms[1].translation[0] = 1;
    assert(!solver.Reset(badPose, &a));
    Same(before, a);
    badPose = Pose(rig);
    badPose.placement.rotation = GfQuatf(0.0f);
    assert(!solver.Step(1.0f / 60, badPose, &a));
    assert(!solver.Step(1.0f / 60, Pose(rig), nullptr));
    assert(solver.Step(1.0f / 60, Pose(rig), &a) && control.Step(1.0f / 60, Pose(rig), &b));
    Same(a, b);
    auto bad = rig;
    bad.parents[0] = 1;
    assert(!solver.Configure(bad));
    bad = rig;
    bad.springs[0].rootJoints = {1};
    assert(!solver.Configure(bad));
    bad = rig;
    bad.springs[0].parameters[0].dragForce = 2;
    assert(!solver.Configure(bad));
    bad = rig;
    bad.springs[0].center = 3;
    assert(!solver.Configure(bad));
    bad = rig;
    bad.restTransforms[2].translation = GfVec3f(0);
    assert(!solver.Configure(bad));
    bad = rig;
    bad.colliders.resize(1);
    bad.colliders[0].joint = 3;
    bad.colliderGroups = {{0}};
    bad.springs[0].colliderGroups = {0};
    assert(!solver.Configure(bad));
}
} // namespace

int
main()
{
    SettleAndReset();
    IndependentHistoryCopies();
    Branches();
    Collisions();
    RotatedLeaf();
    CenterAndValidation();
    std::puts("spring bones: gravity, stiffness, branching, colliders, reset, center and "
              "validation passed");
}
