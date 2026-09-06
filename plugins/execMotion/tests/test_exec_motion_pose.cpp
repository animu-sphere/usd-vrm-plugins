// SPDX-License-Identifier: Apache-2.0
//
// The seam, with no stage, no exec system and no request.
//
// This suite compiles ExecMotionPose.cpp directly rather than loading the
// plugin: the point of keeping the computations' decisions in a function over
// plain values is that they can be checked without any of the mechanism, so a
// wrong pose and a wrong request are never the same failure.

#include "ExecMotionPose.h"

#include <cassert>
#include <cmath>
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

    const motion::HumanoidPose pose = execmotion::IdentityPoseForJoints(joints);

    // No timestamp, and no way to pass one: the pose says nothing about time
    // because a computation cannot learn the stage's timeCodesPerSecond, and a
    // frame written into a seconds field would be a wrong number rather than a
    // missing one.
    assert(pose.timestamp == 0.0);
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
    const motion::HumanoidPose pose = execmotion::IdentityPoseForJoints({});
    assert(CountValid(pose) == 0);

    // An empty pose still compares equal to itself, which is not a tautology
    // here: exec requires equality comparability of every registered value type,
    // and this is the type execMotion registers.
    assert(pose == motion::HumanoidPose{});
}

void TestARepeatedJointIsNotCountedTwice()
{
    const motion::HumanoidPose pose =
        execmotion::IdentityPoseForJoints({"hips", "hips", "hips/spine"});
    assert(CountValid(pose) == 2);
}

// ---------------------------------------------------------------------------
// The sampled pose
// ---------------------------------------------------------------------------

// The fixture clip, as plain values: four canonical bones, one joint that names
// none, and a rotation on the head so a pose that dropped the frame is visible.
execmotion::ClipSample FourBonesAndAProp()
{
    execmotion::ClipSample sample;
    sample.jointPaths = {"hips", "hips/spine", "hips/spine/chest",
                         "hips/spine/chest/neck/head", "prop"};
    sample.rotations = {
        pxr::GfQuatf(1.0f, pxr::GfVec3f(0.0f)),
        pxr::GfQuatf(1.0f, pxr::GfVec3f(0.0f)),
        pxr::GfQuatf(1.0f, pxr::GfVec3f(0.0f)),
        pxr::GfQuatf(0.70710678f, pxr::GfVec3f(0.0f, 0.70710678f, 0.0f)),
        pxr::GfQuatf(1.0f, pxr::GfVec3f(0.0f))};
    sample.translations = {
        pxr::GfVec3f(0.0f, 1.0f, 2.0f),
        pxr::GfVec3f(0.0f, 0.1f, 0.0f),
        pxr::GfVec3f(0.0f, 0.2f, 0.0f),
        pxr::GfVec3f(0.0f, 0.3f, 0.0f),
        pxr::GfVec3f(9.0f, 9.0f, 9.0f)};
    sample.timeCode = 100.0;
    sample.hasTimeCode = true;
    sample.timeCodesPerSecond = 50.0;
    return sample;
}

void TestTheFrameBecomesASecond()
{
    const std::optional<motion::HumanoidPose> pose =
        execmotion::PoseFromClipSample(FourBonesAndAProp());
    assert(pose.has_value());

    // 100 frames at 50 time codes per second is two seconds. This is the one
    // arithmetic step in the node, and it is the whole reason the rate has to
    // be an input: nothing inside a computation can find it
    // (docs/reports/openusd/26.08-openexec-mechanism.md section 5).
    assert(pose->timestamp == 2.0);

    assert(CountValid(*pose) == 4);
    assert(Has(*pose, motion::HumanBone::Head));
    assert(!Has(*pose, motion::HumanBone::UpperChest));

    // Only the hips carry body translation: the prop's 9,9,9 is in the array
    // and must not be anywhere in the pose.
    assert(pose->root.hasPosition);
    assert(pose->root.worldPosition == pxr::GfVec3f(0.0f, 1.0f, 2.0f));
}

void TestNoRateIsARefusalAndNotAZero()
{
    execmotion::ClipSample sample = FourBonesAndAProp();
    sample.timeCodesPerSecond = 0.0;
    assert(!execmotion::PoseFromClipSample(sample).has_value());

    // A negative rate is the same refusal rather than a negative second: a clip
    // that states one has not said anything this layer can use.
    sample.timeCodesPerSecond = -50.0;
    assert(!execmotion::PoseFromClipSample(sample).has_value());

    // The refusal is about the rate alone, so a rate with no frame beside it is
    // still a pose -- see below for what the frame's absence means.
    sample.timeCodesPerSecond = 50.0;
    sample.hasTimeCode = false;
    assert(execmotion::PoseFromClipSample(sample).has_value());
}

void TestTheDefaultTimeCodeCarriesNoSecond()
{
    execmotion::ClipSample sample = FourBonesAndAProp();
    sample.hasTimeCode = false;
    sample.timeCode = 100.0;  // ignored: there is no numeric frame

    const std::optional<motion::HumanoidPose> pose =
        execmotion::PoseFromClipSample(sample);
    assert(pose.has_value());
    assert(pose->timestamp == 0.0 &&
           "a frame was read from a sample that says it has none");

    // The values themselves still come through: what USD resolved at the
    // default time is what the clip states there, and only the stamp is absent.
    assert(CountValid(*pose) == 4);
}

void TestAnArrayThatDoesNotFitTheJointsIsNotGuessedAt()
{
    execmotion::ClipSample sample = FourBonesAndAProp();
    sample.rotations.pop_back();

    const std::optional<motion::HumanoidPose> pose =
        execmotion::PoseFromClipSample(sample);
    assert(pose.has_value());

    // Not four bones with one dropped: none at all. A clip whose rotation array
    // is a different length than its joints has not said which joint any of
    // them belongs to, and pairing them by index anyway would silently put the
    // spine's rotation on the hips.
    assert(CountValid(*pose) == 0);

    // The translations still fit, so the root survives -- the two arrays are
    // judged separately, which is what makes a clip that authors only rotations
    // usable at all.
    assert(pose->root.hasPosition);

    execmotion::ClipSample noTranslations = FourBonesAndAProp();
    noTranslations.translations.clear();
    const std::optional<motion::HumanoidPose> rotationsOnly =
        execmotion::PoseFromClipSample(noTranslations);
    assert(rotationsOnly.has_value());
    assert(CountValid(*rotationsOnly) == 4);
    assert(!rotationsOnly->root.hasPosition &&
           "a clip with no translations reported a root position anyway");
}

void TestARotationIsNormalizedOnTheWayIn()
{
    execmotion::ClipSample sample = FourBonesAndAProp();
    // Twice the length, same direction: a clip may author a quaternion that has
    // drifted, and every consumer of a canonical pose is entitled to a rotation
    // rather than to a scaled one.
    sample.rotations[3] =
        pxr::GfQuatf(1.4142136f, pxr::GfVec3f(0.0f, 1.4142136f, 0.0f));

    const std::optional<motion::HumanoidPose> pose =
        execmotion::PoseFromClipSample(sample);
    assert(pose.has_value());
    const pxr::GfQuatf& head =
        pose->localRotations[static_cast<std::size_t>(motion::HumanBone::Head)];
    assert(std::abs(head.GetLength() - 1.0f) < 1e-5f);
    assert(std::abs(head.GetReal() - 0.70710678f) < 1e-5f);
}

} // namespace

int main()
{
    TestLeafSegmentIsTheBone();
    TestIdentityPoseNamesOnlyWhatItRecognized();
    TestEmptyClipIsAPoseAndNotAFailure();
    TestARepeatedJointIsNotCountedTwice();
    TestTheFrameBecomesASecond();
    TestNoRateIsARefusalAndNotAZero();
    TestTheDefaultTimeCodeCarriesNoSecond();
    TestAnArrayThatDoesNotFitTheJointsIsNotGuessedAt();
    TestARotationIsNormalizedOnTheWayIn();
    std::printf("execMotion pose: all checks passed\n");
    return 0;
}
