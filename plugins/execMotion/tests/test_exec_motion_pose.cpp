// SPDX-License-Identifier: Apache-2.0
//
// The seam, with no stage, no exec system and no request.
//
// This suite compiles ExecMotionPose.cpp directly rather than loading the
// plugin: the point of keeping the computations' decisions in a function over
// plain values is that they can be checked without any of the mechanism, so a
// wrong pose and a wrong request are never the same failure.

#include "ExecMotionPose.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <optional>
#include <string>
#include <utility>
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

// ---------------------------------------------------------------------------
// The filter step
// ---------------------------------------------------------------------------
// One `motion::PoseFilter` step over two poses, with the state passed in. The
// three checks here are the ones that decide whether the *node* is honest: that
// an absent policy is the library's answer and not one this bundle invented,
// that filtering against yourself is the identity, and that a step lands where
// the library's own weight formula says.

motion::HumanoidPose PoseWithHeadAndHips(double timestamp, float headAngleDeg,
                                         const pxr::GfVec3f& hips)
{
    motion::HumanoidPose pose;
    pose.timestamp = timestamp;
    const float radians = headAngleDeg * float(M_PI) / 180.0f;
    pose.localRotations[static_cast<std::size_t>(motion::HumanBone::Head)] =
        pxr::GfQuatf(std::cos(radians * 0.5f),
                     pxr::GfVec3f(0.0f, std::sin(radians * 0.5f), 0.0f));
    pose.validRotations.set(static_cast<std::size_t>(motion::HumanBone::Head));
    pose.validRotations.set(static_cast<std::size_t>(motion::HumanBone::Hips));
    pose.root.worldPosition = hips;
    pose.root.hasPosition = true;
    return pose;
}

float HeadAngleDegrees(const motion::HumanoidPose& pose)
{
    const pxr::GfQuatf head =
        pose.localRotations[static_cast<std::size_t>(motion::HumanBone::Head)]
            .GetNormalized();
    const double w = std::min(1.0, std::max(-1.0, double(head.GetReal())));
    return float(2.0 * std::acos(w) * 180.0 / M_PI);
}

void TestFilteringAgainstYourselfChangesNothing()
{
    const motion::HumanoidPose pose =
        PoseWithHeadAndHips(1.0, 45.0f, pxr::GfVec3f(0.0f, 0.5f, 1.0f));

    // The same pose as both arguments is what the node computes when nobody
    // overrides `motion.priorPose`, and the answer has to be the pose itself:
    // the elapsed time is zero, so `PoseFilter` reseeds and passes it through.
    // Nothing in this file special-cases it -- it is the library's rule for a
    // non-increasing timestamp, and the node inherits it.
    assert(execmotion::FilteredPose(pose, pose, {}) == pose);

    // And so does a prior pose from the future, which is what a seek backwards
    // looks like.
    const motion::HumanoidPose later =
        PoseWithHeadAndHips(2.0, 0.0f, pxr::GfVec3f(0.0f));
    assert(execmotion::FilteredPose(later, pose, {}) == pose);
}

void TestAnAbsentPolicyIsTheLibrarysOwn()
{
    const motion::HumanoidPose prior =
        PoseWithHeadAndHips(0.98, 0.0f, pxr::GfVec3f(0.0f));
    const motion::HumanoidPose pose =
        PoseWithHeadAndHips(1.0, 45.0f, pxr::GfVec3f(0.0f, 0.5f, 1.0f));

    // 6 Hz is `motion::PoseFilter::Options`' default, and a policy stating
    // nothing must land on exactly the same pose as one stating 6. If this
    // bundle ever grew a default of its own, these two would part company.
    execmotion::FilterPolicy stated;
    stated.cutoffHz = 6.0f;
    assert(execmotion::FilteredPose(prior, pose, {}) ==
           execmotion::FilteredPose(prior, pose, stated));

    // The weight itself, from the library's documented definition rather than
    // from the library: 1 - exp(-2*pi*cutoff*dt), and a slerp between two
    // rotations about one axis moves the angle linearly.
    constexpr double kTwoPi = 6.2831853071795862;
    const double weight = 1.0 - std::exp(-kTwoPi * 6.0 * 0.02);
    const motion::HumanoidPose filtered =
        execmotion::FilteredPose(prior, pose, {});
    assert(std::abs(HeadAngleDegrees(filtered) - float(45.0 * weight)) < 1e-2f);
    assert(std::abs(filtered.root.worldPosition[2] - float(weight)) < 1e-4f);

    // A filtered pose belongs to the frame it was asked for, not to the state
    // it started from.
    assert(filtered.timestamp == pose.timestamp);
}

void TestEachPolicyFieldReachesTheOptionItNames()
{
    const motion::HumanoidPose prior =
        PoseWithHeadAndHips(0.98, 0.0f, pxr::GfVec3f(0.0f));
    const motion::HumanoidPose pose =
        PoseWithHeadAndHips(1.0, 45.0f, pxr::GfVec3f(0.0f, 0.5f, 1.0f));

    // A non-positive cutoff is the library's own pass-through, and it is the
    // only value of the policy that makes the node an identity over a real
    // step.
    execmotion::FilterPolicy off;
    off.cutoffHz = 0.0f;
    assert(execmotion::FilteredPose(prior, pose, off) == pose);

    // A higher cutoff is a longer step toward the new pose, in the direction
    // the library documents: more responsive, less smoothing.
    execmotion::FilterPolicy fast;
    fast.cutoffHz = 12.0f;
    assert(HeadAngleDegrees(execmotion::FilteredPose(prior, pose, fast)) >
           HeadAngleDegrees(execmotion::FilteredPose(prior, pose, {})));

    // The root flag reaches the root and nothing else: the hips stop moving,
    // the head goes on taking its step.
    execmotion::FilterPolicy heldRoot;
    heldRoot.filterRootPosition = false;
    const motion::HumanoidPose held =
        execmotion::FilteredPose(prior, pose, heldRoot);
    assert(held.root.worldPosition == pose.root.worldPosition);
    assert(std::abs(HeadAngleDegrees(held) -
                    HeadAngleDegrees(execmotion::FilteredPose(prior, pose, {})))
           < 1e-4f);
}

// ---------------------------------------------------------------------------
// What the round trip through a pose costs
// ---------------------------------------------------------------------------
// The one place the exec node and the streaming filter give different answers,
// measured against `motion::PoseFilter` itself rather than argued about.
//
// `PoseFilter` retains a state strictly richer than the pose it returns: a bone
// a pose does not report keeps its stored rotation *in the state* and stays out
// of the *result*, so a brief dropout does not restart that bone's history. Only
// the result can travel back through `motion.priorPose`, so the retained half
// does not survive the trip -- and a bone that comes back after a missing frame
// is passed through unfiltered here where the streaming filter would slerp it
// from what it kept.
//
// It costs nothing for a clip, whose `joints` are `uniform` and whose bones
// therefore never drop out, and it is real for a live source, which is what the
// node is aimed at. So it is pinned here, in both directions, and it is the
// sharpened half of this bundle's ask on `motionRuntime`: a one-step entry point
// has to hand back the state as well as the result, or a caller cannot carry the
// history that makes a dropout survivable.

motion::HumanoidPose PoseWithoutHead(double timestamp, const pxr::GfVec3f& hips)
{
    motion::HumanoidPose pose = PoseWithHeadAndHips(timestamp, 0.0f, hips);
    pose.validRotations.reset(static_cast<std::size_t>(motion::HumanBone::Head));
    return pose;
}

void TestADropoutDoesNotSurviveTheRoundTrip()
{
    // Three instants one frame apart at 50 Hz: the head is at identity, then
    // absent for one frame, then at 45 degrees.
    const motion::HumanoidPose first =
        PoseWithHeadAndHips(0.00, 0.0f, pxr::GfVec3f(0.0f));
    const motion::HumanoidPose dropout =
        PoseWithoutHead(0.02, pxr::GfVec3f(0.0f));
    const motion::HumanoidPose back =
        PoseWithHeadAndHips(0.04, 45.0f, pxr::GfVec3f(0.0f));

    // ---- what motion::PoseFilter does, driven as the streaming operator ----
    motion::PoseFilter streaming;
    streaming.Apply(first);
    const motion::HumanoidPose streamedDropout = streaming.Apply(dropout);
    const motion::HumanoidPose streamedBack = streaming.Apply(back);

    // The dropout frame reports no head either way: the library does not invent
    // a bone the pose did not carry, which is the behaviour the round trip is
    // not allowed to change.
    assert(!Has(streamedDropout, motion::HumanBone::Head));

    // And the frame after it is smoothed from the head the filter kept.
    constexpr double kTwoPi = 6.2831853071795862;
    const double weight = 1.0 - std::exp(-kTwoPi * 6.0 * 0.02);
    assert(std::abs(HeadAngleDegrees(streamedBack) - float(45.0 * weight))
           < 1e-2f);

    // ---- what the node does, with the result fed back as the prior pose ----
    const motion::HumanoidPose steppedFirst =
        execmotion::FilteredPose(first, first, {});
    const motion::HumanoidPose steppedDropout =
        execmotion::FilteredPose(steppedFirst, dropout, {});
    const motion::HumanoidPose steppedBack =
        execmotion::FilteredPose(steppedDropout, back, {});

    assert(!Has(steppedDropout, motion::HumanBone::Head) &&
           "the seam invented a bone the pose did not report");

    // The divergence, stated as a number rather than as a risk: the head comes
    // back at the full 45 degrees, because the pose that travelled back carried
    // no head for the step to start from.
    assert(std::abs(HeadAngleDegrees(steppedBack) - 45.0f) < 1e-2f &&
           "a bone returning from a dropout was smoothed, so the seam is "
           "carrying history a pose cannot carry");
    assert(std::abs(HeadAngleDegrees(steppedBack) -
                    HeadAngleDegrees(streamedBack)) > 1.0f &&
           "the two now agree -- either PoseFilter stopped retaining dropped "
           "bones, or the seam grew a state, and the parity note that says they "
           "differ (P0-6) is stale either way");

    // Everything the dropout did not touch is unaffected: the hips take their
    // step in both, which is what makes the difference above about retained
    // history and not about the filter running at all.
    assert(streamedBack.root.worldPosition == steppedBack.root.worldPosition);
}

// ---------------------------------------------------------------------------
// The root intake
// ---------------------------------------------------------------------------
// `motion::RootMotionIntake`'s three policies over two instants. The library's
// rule lives in a private method of a capture *session* (see ExecMotionPose.h),
// so these checks are written against the rule's definition rather than against
// a call -- which is exactly why they are here rather than taken on trust.

void TestTheIntakeTokenTableIsTheLibrarysEnum()
{
    assert(execmotion::RootIntakeForToken("passthrough") ==
           motion::RootMotionIntake::Passthrough);
    assert(execmotion::RootIntakeForToken("ignore") ==
           motion::RootMotionIntake::Ignore);
    assert(execmotion::RootIntakeForToken("deriveVelocity") ==
           motion::RootMotionIntake::DeriveVelocity);

    // Nothing else is a policy. A differently-cased spelling is a different
    // token, the same rule the bone table keeps: one vocabulary, and a laxer
    // second one here would be the duplicate the workspace forbids.
    assert(!execmotion::RootIntakeForToken("").has_value());
    assert(!execmotion::RootIntakeForToken("Passthrough").has_value());
    assert(!execmotion::RootIntakeForToken("derive").has_value());
    assert(!execmotion::RootIntakeForToken("smooth").has_value());
}

void TestAnAbsentIntakeIsTheLibrarysOwn()
{
    const motion::HumanoidPose prior =
        PoseWithHeadAndHips(0.98, 0.0f, pxr::GfVec3f(0.0f));
    const motion::HumanoidPose pose =
        PoseWithHeadAndHips(1.0, 45.0f, pxr::GfVec3f(0.0f, 0.5f, 1.0f));

    // An absent policy is `LiveCaptureConfig`'s, read from the library rather
    // than restated -- so this assertion is against the library's own field and
    // moves with it if it ever moves.
    execmotion::RootPolicy stated;
    stated.intake = motion::LiveCaptureConfig{}.rootMotion;
    assert(execmotion::RootMotionFrom(prior, pose, {}) ==
           execmotion::RootMotionFrom(prior, pose, stated));

    // And the default is distinguishable from the other two over these poses,
    // which is what makes the line above worth asserting: a bundle that had
    // picked `Passthrough` would derive nothing here.
    execmotion::RootPolicy passthrough;
    passthrough.intake = motion::RootMotionIntake::Passthrough;
    assert(execmotion::RootMotionFrom(prior, pose, {}) !=
           execmotion::RootMotionFrom(prior, pose, passthrough));
}

void TestPassthroughIsThePoseSOwnRoot()
{
    const motion::HumanoidPose prior =
        PoseWithHeadAndHips(0.98, 0.0f, pxr::GfVec3f(0.0f));
    const motion::HumanoidPose pose =
        PoseWithHeadAndHips(1.0, 45.0f, pxr::GfVec3f(0.0f, 0.5f, 1.0f));

    execmotion::RootPolicy policy;
    policy.intake = motion::RootMotionIntake::Passthrough;
    assert(execmotion::RootMotionFrom(prior, pose, policy) == pose.root &&
           "passthrough is the one policy with nothing to do, and it did "
           "something");
}

void TestIgnoreClearsRatherThanZeroes()
{
    const motion::HumanoidPose pose =
        PoseWithHeadAndHips(1.0, 45.0f, pxr::GfVec3f(0.0f, 0.5f, 1.0f));

    execmotion::RootPolicy policy;
    policy.intake = motion::RootMotionIntake::Ignore;
    const motion::RootMotion root = execmotion::RootMotionFrom(pose, pose,
                                                               policy);

    // The difference matters downstream: a cleared root says "this clip does
    // not place the body", and a zeroed position with `hasPosition` set says
    // "the body is at the origin". The first lets a rig keep its own placement.
    assert(root == motion::RootMotion{});
    assert(!root.hasPosition &&
           "ignore zeroed the position instead of clearing it");
}

void TestAVelocityIsDerivedExactlyWhereTheLibraryDerivesOne()
{
    execmotion::RootPolicy derive;
    derive.intake = motion::RootMotionIntake::DeriveVelocity;

    const motion::HumanoidPose prior =
        PoseWithHeadAndHips(0.98, 0.0f, pxr::GfVec3f(0.0f));
    const motion::HumanoidPose pose =
        PoseWithHeadAndHips(1.0, 45.0f, pxr::GfVec3f(0.0f, 0.5f, 1.0f));

    // The definition: the distance between two instants over the time between
    // them. Written out rather than called, because the rule this wraps has no
    // call to make (ExecMotionPose.h).
    {
        const motion::RootMotion root =
            execmotion::RootMotionFrom(prior, pose, derive);
        assert(root.hasLinearVelocity);
        const pxr::GfVec3f expected =
            (pose.root.worldPosition - prior.root.worldPosition) / 0.02f;
        assert(std::abs(root.linearVelocity[1] - expected[1]) < 1e-2f);
        assert(std::abs(root.linearVelocity[2] - expected[2]) < 1e-2f);
        // The position it was derived from is untouched.
        assert(root.worldPosition == pose.root.worldPosition);
    }

    // The same instant twice, which is what an un-overridden node computes.
    {
        const motion::RootMotion root =
            execmotion::RootMotionFrom(pose, pose, derive);
        assert(!root.hasLinearVelocity &&
               "a velocity was derived between a pose and itself");
        assert(root == pose.root);
    }

    // A prior pose from the future: a seek backwards, and the same answer, for
    // the same reason the filter reseeds there.
    {
        const motion::HumanoidPose later =
            PoseWithHeadAndHips(2.0, 0.0f, pxr::GfVec3f(0.0f, 9.0f, 9.0f));
        assert(!execmotion::RootMotionFrom(later, pose, derive)
                    .hasLinearVelocity);
    }

    // A source that already reported one keeps it. The library derives a
    // velocity only where none arrived, and overwriting a measured value with a
    // differentiated one would be this bundle deciding it knows better.
    {
        motion::HumanoidPose reported = pose;
        reported.root.linearVelocity = pxr::GfVec3f(1.0f, 2.0f, 3.0f);
        reported.root.hasLinearVelocity = true;
        const motion::RootMotion root =
            execmotion::RootMotionFrom(prior, reported, derive);
        assert(root.linearVelocity == pxr::GfVec3f(1.0f, 2.0f, 3.0f) &&
               "a reported velocity was replaced by a derived one");
    }

    // Nothing to differentiate, on either side: a pose with no position, and a
    // prior with none. Both leave the root as it arrived rather than inventing
    // a zero, which is the same rule the rest of the seam keeps.
    {
        motion::HumanoidPose noPosition = pose;
        noPosition.root = motion::RootMotion{};
        assert(!execmotion::RootMotionFrom(prior, noPosition, derive)
                    .hasLinearVelocity);

        motion::HumanoidPose priorNoPosition = prior;
        priorNoPosition.root = motion::RootMotion{};
        assert(!execmotion::RootMotionFrom(priorNoPosition, pose, derive)
                    .hasLinearVelocity);
    }
}

// ---------------------------------------------------------------------------
// The history, and what it states at one instant
// ---------------------------------------------------------------------------
// `SampleHistory` is a wrapper -- `motion::ClipSource::Sample` over the
// snapshot -- so what these check is that the library's answer arrives whole:
// the bracket, the hold, the stamp and the status. The expected values are
// written from the definitions (half of a 90-degree turn about one axis is 45
// degrees; the lag is the request minus the newest sample), for the reason every
// suite here gives: an expected value produced by the code under test asserts
// only that it equals itself.

motion::HumanoidAnimation HistoryOf(std::vector<motion::HumanoidPose> samples)
{
    motion::HumanoidAnimation history;
    history.samples = std::move(samples);
    return history;
}

void TestAHistoryOfOneAnswersItsOwnPose()
{
    const motion::HumanoidPose pose =
        PoseWithHeadAndHips(1.0, 45.0f, pxr::GfVec3f(0.0f, 0.5f, 1.0f));

    const motion::HumanoidAnimation history = execmotion::HistoryOfOne(pose);
    assert(history.samples.size() == 1);
    assert(history.samples.front() == pose);
    assert(history.startTime == 1.0 && history.endTime == 1.0 &&
           "a history of one sample spans that sample's instant");

    // What the node computes when nobody overrides `motion.poseHistory`: the
    // pose itself, **sampled** -- the instant is the one sample's, so there is
    // neither a bracket nor a hold -- with no lag. Nothing special-cases it.
    const std::optional<motion::PoseSampleResult> result =
        execmotion::SampleHistory(history, pose.timestamp);
    assert(result.has_value());
    assert(result->status == motion::PoseSampleStatus::Sampled);
    assert(result->pose && *result->pose == pose &&
           "a history of one pose, sampled at its own instant, answered "
           "something other than that pose");
    assert(result->lag == 0.0);
}

void TestAnInstantBetweenTwoSamplesIsInterpolated()
{
    // A driver's snapshot: two samples four hundredths of a second apart,
    // bracketing the instant the system evaluates at. The head turns 90
    // degrees about +Y between them and the hips move from the origin to
    // (0, 1, 2).
    const motion::HumanoidAnimation history = HistoryOf({
        PoseWithHeadAndHips(0.98, 0.0f, pxr::GfVec3f(0.0f)),
        PoseWithHeadAndHips(1.02, 90.0f, pxr::GfVec3f(0.0f, 1.0f, 2.0f))});

    const std::optional<motion::PoseSampleResult> result =
        execmotion::SampleHistory(history, 1.0);
    assert(result.has_value() && result->pose);
    assert(result->status == motion::PoseSampleStatus::Sampled);

    const motion::HumanoidPose& pose = *result->pose;
    assert(std::abs(HeadAngleDegrees(pose) - 45.0f) < 1e-3f &&
           "halfway between 0 and 90 degrees about one axis is not 45");
    assert(std::abs(pose.root.worldPosition[1] - 0.5f) < 1e-5f);
    assert(std::abs(pose.root.worldPosition[2] - 1.0f) < 1e-5f);

    // Stamped at the instant it was asked for, on the consumer's clock.
    assert(std::abs(pose.timestamp - 1.0) < 1e-12);

    // The request is behind the newest sample, so the lag is negative: the
    // source is ahead of the consumer, which is what a buffered delay is.
    assert(std::abs(result->lag - (1.0 - 1.02)) < 1e-12);
}

void TestAnInstantOutsideTheHistoryIsHeldAndSaysSo()
{
    // A source that stopped delivering at 0.9 s, asked about 1.0 s.
    const motion::HumanoidPose newest =
        PoseWithHeadAndHips(0.9, 90.0f, pxr::GfVec3f(0.0f, 1.0f, 2.0f));
    const motion::HumanoidAnimation history = HistoryOf({
        PoseWithHeadAndHips(0.5, 0.0f, pxr::GfVec3f(0.0f)), newest});

    const std::optional<motion::PoseSampleResult> held =
        execmotion::SampleHistory(history, 1.0);
    assert(held.has_value() && held->pose);
    assert(held->status == motion::PoseSampleStatus::Held &&
           "an instant past the newest sample was not reported as a hold");
    assert(std::abs(HeadAngleDegrees(*held->pose) - 90.0f) < 1e-3f &&
           "a hold did not repeat the newest sample");
    assert(std::abs(held->lag - 0.1) < 1e-12 &&
           "the lag is not the request minus the newest sample");

    // **Why the result is not a bare pose.** The held pose is stamped at the
    // evaluated instant -- exactly as a sampled one is -- so its timestamp
    // cannot tell a consumer that the source has stopped. The status and the
    // lag are the only fields that do, and a node that answered with the pose
    // alone would have dropped both.
    assert(held->pose->timestamp == 1.0 &&
           "the library no longer stamps a hold at the request, so the reason "
           "this node returns the status as well needs restating");

    // And the other edge: before the oldest sample, the oldest is held.
    const std::optional<motion::PoseSampleResult> early =
        execmotion::SampleHistory(history, 0.1);
    assert(early.has_value() && early->pose);
    assert(early->status == motion::PoseSampleStatus::Held);
    assert(std::abs(HeadAngleDegrees(*early->pose)) < 1e-3f);
}

void TestAMissingBoneIsHeldAcrossTheBracket()
{
    // The library's rule, arriving through the wrapper unchanged: a bone one
    // bracketing sample reports and the other does not is held at the value it
    // was reported with, never faded toward identity.
    motion::HumanoidPose headless =
        PoseWithHeadAndHips(1.02, 0.0f, pxr::GfVec3f(0.0f, 1.0f, 2.0f));
    headless.validRotations.reset(
        static_cast<std::size_t>(motion::HumanBone::Head));

    const motion::HumanoidAnimation history = HistoryOf({
        PoseWithHeadAndHips(0.98, 60.0f, pxr::GfVec3f(0.0f)), headless});

    const std::optional<motion::PoseSampleResult> result =
        execmotion::SampleHistory(history, 1.0);
    assert(result.has_value() && result->pose);
    assert(Has(*result->pose, motion::HumanBone::Head));
    assert(std::abs(HeadAngleDegrees(*result->pose) - 60.0f) < 1e-3f &&
           "a bone only one sample reported was faded rather than held");
}

void TestAnEmptyHistoryIsAnAnswerAndNotARefusal()
{
    // The library's `Unavailable`: a value, with no pose in it. It is an
    // *answer* -- the history holds nothing, and the type can say so -- which
    // makes it the first result in this bundle with an absent state of its own,
    // and so the first one with nothing for a refusal to protect.
    const std::optional<motion::PoseSampleResult> result =
        execmotion::SampleHistory(motion::HumanoidAnimation{}, 1.0);
    assert(result.has_value() &&
           "an empty history was refused, which spends the bundle's one "
           "refusal on something the library already answers");
    assert(result->status == motion::PoseSampleStatus::Unavailable);
    assert(!result->pose);
    assert(*result == motion::PoseSampleResult{});
}

void TestAHistoryOutOfOrderIsRefused()
{
    // Decreasing: the library's binary search would bracket the instant with
    // samples that do not surround it, and answer with a pose nobody measured.
    const motion::HumanoidAnimation backwards = HistoryOf({
        PoseWithHeadAndHips(1.02, 90.0f, pxr::GfVec3f(0.0f, 1.0f, 2.0f)),
        PoseWithHeadAndHips(0.98, 0.0f, pxr::GfVec3f(0.0f))});
    assert(!execmotion::SampleHistory(backwards, 1.0).has_value());

    // Repeated is not decreasing, and is not refused: the library answers it.
    // A stricter check would be this bundle's policy -- `PoseBuffer::Push`'s
    // strictly-increasing rule is about filling a buffer, not sampling one.
    const motion::HumanoidAnimation repeated = HistoryOf({
        PoseWithHeadAndHips(0.98, 0.0f, pxr::GfVec3f(0.0f)),
        PoseWithHeadAndHips(0.98, 0.0f, pxr::GfVec3f(0.0f)),
        PoseWithHeadAndHips(1.02, 90.0f, pxr::GfVec3f(0.0f, 1.0f, 2.0f))});
    const std::optional<motion::PoseSampleResult> result =
        execmotion::SampleHistory(repeated, 1.0);
    assert(result.has_value() && result->pose);
    assert(std::abs(HeadAngleDegrees(*result->pose) - 45.0f) < 1e-3f);
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
    TestFilteringAgainstYourselfChangesNothing();
    TestAnAbsentPolicyIsTheLibrarysOwn();
    TestEachPolicyFieldReachesTheOptionItNames();
    TestADropoutDoesNotSurviveTheRoundTrip();
    TestTheIntakeTokenTableIsTheLibrarysEnum();
    TestAnAbsentIntakeIsTheLibrarysOwn();
    TestPassthroughIsThePoseSOwnRoot();
    TestIgnoreClearsRatherThanZeroes();
    TestAVelocityIsDerivedExactlyWhereTheLibraryDerivesOne();
    TestAHistoryOfOneAnswersItsOwnPose();
    TestAnInstantBetweenTwoSamplesIsInterpolated();
    TestAnInstantOutsideTheHistoryIsHeldAndSaysSo();
    TestAMissingBoneIsHeldAcrossTheBracket();
    TestAnEmptyHistoryIsAnAnswerAndNotARefusal();
    TestAHistoryOutOfOrderIsRefused();
    std::printf("execMotion pose: all checks passed\n");
    return 0;
}
