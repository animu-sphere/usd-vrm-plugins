// SPDX-License-Identifier: Apache-2.0
//
// The seam, with no stage, no exec system and no request.
//
// This suite compiles ExecMotionIdentity.cpp directly rather than loading the
// plugin: the point of keeping the computation's decisions in a function over
// plain values is that they can be checked without any of the mechanism, so a
// wrong pose and a wrong request are never the same failure.

#include "ExecMotionIdentity.h"

#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

namespace {

std::size_t CountValid(const motion::HumanoidPose& pose)
{
    return pose.validRotations.count();
}

bool Has(const motion::HumanoidPose& pose, motion::HumanBone bone)
{
    return pose.validRotations.test(static_cast<std::size_t>(bone));
}

void TestLeafSegmentIsTheBone()
{
    // A joint path is UsdSkelAnimation's own spelling and the bone is its leaf:
    // "hips/spine/chest" is the chest, not something named after the whole path.
    assert(execmotion::BoneForJointPath("hips") == motion::HumanBone::Hips);
    assert(execmotion::BoneForJointPath("hips/spine") ==
           motion::HumanBone::Spine);
    assert(execmotion::BoneForJointPath("hips/spine/chest/neck/head") ==
           motion::HumanBone::Head);

    // A path whose leaf is not a canonical bone maps to nothing, and so does a
    // trailing separator -- which names no leaf at all.
    assert(!execmotion::BoneForJointPath("prop").has_value());
    assert(!execmotion::BoneForJointPath("hips/").has_value());
    assert(!execmotion::BoneForJointPath("").has_value());

    // The names are the canonical ones, so a differently-cased spelling is a
    // different name rather than the same bone. Nothing normalizes here: the
    // one table lives in motionCore and a second, laxer one in this bundle is
    // exactly the duplicate the workspace forbids.
    assert(!execmotion::BoneForJointPath("Hips").has_value());
}

void TestIdentityPoseNamesOnlyWhatItRecognized()
{
    const std::vector<std::string> joints = {
        "hips", "hips/spine", "hips/spine/chest",
        "hips/spine/chest/neck/head", "prop"};

    const motion::HumanoidPose pose =
        execmotion::IdentityPoseForJoints(joints, 2.5);

    assert(pose.timestamp == 2.5);
    assert(CountValid(pose) == 4);
    assert(Has(pose, motion::HumanBone::Hips));
    assert(Has(pose, motion::HumanBone::Spine));
    assert(Has(pose, motion::HumanBone::Chest));
    assert(Has(pose, motion::HumanBone::Head));

    // The unrecognized joint contributed nothing rather than a bone of its own.
    assert(!Has(pose, motion::HumanBone::UpperChest));

    // Every rotation is the identity, including the four this pose claims.
    // "Identity computation" is the literal description of what this returns.
    for (const pxr::GfQuatf& rotation : pose.localRotations) {
        assert(rotation == pxr::GfQuatf(1.0f, pxr::GfVec3f(0.0f)));
    }
}

void TestEmptyClipIsAPoseAndNotAFailure()
{
    const motion::HumanoidPose pose =
        execmotion::IdentityPoseForJoints({}, 0.0);
    assert(CountValid(pose) == 0);

    // An empty pose still compares equal to itself, which is not a tautology
    // here: exec requires equality comparability of every registered value type,
    // and this is the type execMotion registers.
    assert(pose == motion::HumanoidPose{});
}

void TestARepeatedJointIsNotCountedTwice()
{
    const motion::HumanoidPose pose = execmotion::IdentityPoseForJoints(
        {"hips", "hips", "hips/spine"}, 0.0);
    assert(CountValid(pose) == 2);
}

} // namespace

int main()
{
    TestLeafSegmentIsTheBone();
    TestIdentityPoseNamesOnlyWhatItRecognized();
    TestEmptyClipIsAPoseAndNotAFailure();
    TestARepeatedJointIsNotCountedTwice();
    std::printf("execMotion identity: all checks passed\n");
    return 0;
}
