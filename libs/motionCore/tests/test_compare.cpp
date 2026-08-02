// SPDX-License-Identifier: Apache-2.0
//
// The two comparisons, and the decisions Compare.h states about them.
#include "motionCore/Compare.h"
#include "motionCore/Humanoid.h"

#include <cassert>
#include <cmath>
#include <cstddef>
#include <limits>
#include <string>

namespace
{

using motion::HumanBone;
using motion::HumanBoneCount;
using motion::HumanoidAnimation;
using motion::HumanoidPose;
using motion::MotionSourceMetadata;
using motion::MotionTolerance;
using motion::NearlyEqual;

constexpr double kPi = 3.14159265358979323846;

pxr::GfQuatf
AboutY(double radians)
{
    return pxr::GfQuatf(
        static_cast<float>(std::cos(radians * 0.5)),
        pxr::GfVec3f(0.0f, static_cast<float>(std::sin(radians * 0.5)), 0.0f));
}

pxr::GfQuatf
Negated(const pxr::GfQuatf& q)
{
    return pxr::GfQuatf(-q.GetReal(), -q.GetImaginary());
}

// Six decimals, the way the recorded-trace writer rounds. Compare.h derives
// every default tolerance from surviving exactly this.
float
Rounded(float value)
{
    return static_cast<float>(std::round(static_cast<double>(value) * 1e6)
                              / 1e6);
}

pxr::GfQuatf
Rounded(const pxr::GfQuatf& q)
{
    return pxr::GfQuatf(Rounded(q.GetReal()),
                        pxr::GfVec3f(Rounded(q.GetImaginary()[0]),
                                     Rounded(q.GetImaginary()[1]),
                                     Rounded(q.GetImaginary()[2])));
}

// A pose with enough of every field set that a comparison has something to
// walk: two bones, a root carrying position and linear velocity, confidence,
// contacts, and provenance.
HumanoidPose
SamplePose()
{
    HumanoidPose pose;
    pose.timestamp = 1.5;
    pose.root.worldPosition = pxr::GfVec3f(0.25f, 0.9f, -1.0f);
    pose.root.hasPosition = true;
    pose.root.linearVelocity = pxr::GfVec3f(0.0f, 0.0f, 1.2f);
    pose.root.hasLinearVelocity = true;

    pose.localRotations[static_cast<std::size_t>(HumanBone::Hips)] =
        AboutY(0.1);
    pose.validRotations.set(static_cast<std::size_t>(HumanBone::Hips));
    pose.localRotations[static_cast<std::size_t>(HumanBone::LeftUpperArm)] =
        AboutY(-0.75);
    pose.validRotations.set(static_cast<std::size_t>(HumanBone::LeftUpperArm));

    std::array<float, HumanBoneCount> confidence{};
    confidence.fill(1.0f);
    confidence[static_cast<std::size_t>(HumanBone::LeftUpperArm)] = 0.5f;
    pose.confidence = confidence;

    motion::ContactState contacts;
    contacts.leftFoot = motion::FootContact::InContact;
    contacts.rightFoot = motion::FootContact::NotInContact;
    pose.contacts = contacts;

    MotionSourceMetadata source;
    source.kind = motion::MotionSourceKind::LiveCapture;
    source.provider = "example.sender";
    source.protocol = "vmc";
    source.sourceId = "session-01";
    pose.source = source;
    return pose;
}

HumanoidAnimation
SampleAnimation()
{
    HumanoidAnimation animation;
    for (int frame = 0; frame != 3; ++frame) {
        HumanoidPose pose = SamplePose();
        pose.timestamp = frame / 30.0;
        pose.localRotations[static_cast<std::size_t>(HumanBone::Hips)] =
            AboutY(0.1 * frame);
        animation.samples.push_back(pose);
    }
    animation.startTime = 0.0;
    animation.endTime = 2.0 / 30.0;
    animation.nominalFrameRate = 30.0;
    animation.source = *animation.samples.front().source;
    return animation;
}

void
TestAngleBetween()
{
    const pxr::GfQuatf identity(1.0f, pxr::GfVec3f(0.0f));
    assert(motion::AngleBetween(identity, identity) == 0.0f);

    // The double cover: the same orientation, the opposite components.
    const pxr::GfQuatf quarter = AboutY(kPi * 0.5);
    assert(std::abs(motion::AngleBetween(identity, quarter) - kPi * 0.5)
           < 1e-5);

    // Antipodal orientations are pi apart, not 2pi: the arc is the short one.
    assert(std::abs(motion::AngleBetween(identity, AboutY(kPi)) - kPi) < 1e-5);

    // A zero quaternion is not an orientation, so it is no distance from
    // anything -- including itself.
    const pxr::GfQuatf zero(0.0f, pxr::GfVec3f(0.0f));
    assert(std::isnan(motion::AngleBetween(zero, identity)));
    assert(std::isnan(motion::AngleBetween(zero, zero)));

    // The numerics, which are not incidental here. `acos` is infinitely steep
    // at 1, so a relative mismatch of e between the dot product and the length
    // product surfaces as roughly 2*sqrt(2e) radians -- 9e-4 for a float-sized
    // e, which is an order above the default tolerance and lands exactly where
    // the answer should be zero. Both are formed in double from the same
    // components, so these hold on every platform rather than on the one the
    // rounding happened to suit.
    const pxr::GfQuatf unnormalised = AboutY(0.4) * 7.5f;
    assert(motion::AngleBetween(unnormalised, unnormalised) == 0.0f);
    assert(motion::AngleBetween(quarter, quarter) == 0.0f);
    assert(motion::AngleBetween(quarter, Negated(quarter)) == 0.0f);

    // Length is not orientation: scaling one side changes nothing measurable.
    // Not exactly zero -- scaling rounds each component -- but three orders
    // below the default tolerance rather than one above it.
    assert(motion::AngleBetween(AboutY(0.4), unnormalised) < 1e-5f);
}

void
TestExactEquality()
{
    const HumanoidPose pose = SamplePose();
    assert(pose == pose);
    assert(!(pose != pose));

    HumanoidPose other = pose;
    other.localRotations[static_cast<std::size_t>(HumanBone::Hips)] =
        AboutY(0.100001);
    assert(other != pose);

    // A default-constructed pose claims no bones, so two of them are equal
    // whatever their rotation slots hold.
    HumanoidPose emptyA;
    HumanoidPose emptyB;
    emptyB.localRotations[static_cast<std::size_t>(HumanBone::Head)] =
        AboutY(1.0);
    assert(emptyA == emptyB);
    assert(NearlyEqual(emptyA, emptyB));

    // The claim itself is compared: one pose carrying a bone the other does
    // not is a different pose even when the rotations match.
    HumanoidPose withHead = emptyA;
    withHead.validRotations.set(static_cast<std::size_t>(HumanBone::Head));
    assert(withHead != emptyA);
    assert(!NearlyEqual(withHead, emptyA));

    // The same rule on the root: an unset field's value is not part of the
    // pose.
    HumanoidPose rootA;
    HumanoidPose rootB;
    rootB.root.worldPosition = pxr::GfVec3f(5.0f, 5.0f, 5.0f);
    assert(rootA == rootB);
    rootB.root.hasPosition = true;
    assert(rootA != rootB);

    // Confidence is read only where a bone is claimed, and its presence is a
    // fact of its own: an adapter that cannot measure confidence reports none.
    HumanoidPose loose = pose;
    (*loose.confidence)[static_cast<std::size_t>(HumanBone::Head)] = 0.0f;
    assert(loose == pose);
    loose.confidence.reset();
    assert(loose != pose);
    assert(!NearlyEqual(loose, pose));
}

void
TestValueAndMotionDiverge()
{
    const HumanoidPose pose = SamplePose();

    // A quaternion and its negation: the same motion, a different value.
    HumanoidPose flipped = pose;
    const std::size_t arm = static_cast<std::size_t>(HumanBone::LeftUpperArm);
    flipped.localRotations[arm] = Negated(pose.localRotations[arm]);
    assert(flipped != pose);
    assert(NearlyEqual(flipped, pose));

    // Provenance: a different value, the same motion.
    HumanoidPose relabelled = pose;
    relabelled.source->provider = "other.sender";
    assert(relabelled != pose);
    assert(NearlyEqual(relabelled, pose));

    HumanoidPose anonymous = pose;
    anonymous.source.reset();
    assert(anonymous != pose);
    assert(NearlyEqual(anonymous, pose));

    // And on an animation, where the metadata is not optional.
    HumanoidAnimation clip = SampleAnimation();
    HumanoidAnimation renamed = clip;
    renamed.source.sourceId = "session-02";
    assert(renamed != clip);
    assert(NearlyEqual(renamed, clip));
}

void
TestTolerance()
{
    const HumanoidPose pose = SamplePose();
    const MotionTolerance tolerance;

    // A pose written through the trace format's six decimals and read back is
    // the same motion. This is the floor every default is derived from.
    HumanoidPose quantised = pose;
    for (std::size_t index = 0; index != HumanBoneCount; ++index) {
        quantised.localRotations[index] = Rounded(pose.localRotations[index]);
    }
    quantised.root.worldPosition =
        pxr::GfVec3f(Rounded(pose.root.worldPosition[0]),
                     Rounded(pose.root.worldPosition[1]),
                     Rounded(pose.root.worldPosition[2]));
    quantised.timestamp = std::round(pose.timestamp * 1e6) / 1e6;
    assert(NearlyEqual(quantised, pose));

    // Each tolerance holds either side of its own limit.
    HumanoidPose nudged = pose;
    const std::size_t hips = static_cast<std::size_t>(HumanBone::Hips);
    nudged.localRotations[hips] = AboutY(0.1 + 1e-5);
    assert(NearlyEqual(nudged, pose));
    nudged.localRotations[hips] = AboutY(0.1 + 1e-3);
    assert(!NearlyEqual(nudged, pose));

    HumanoidPose moved = pose;
    moved.root.worldPosition[1] += 1e-6f;
    assert(NearlyEqual(moved, pose));
    moved.root.worldPosition[1] += 1e-3f;
    assert(!NearlyEqual(moved, pose));

    HumanoidPose later = pose;
    later.timestamp += 1e-7;
    assert(NearlyEqual(later, pose));
    later.timestamp += 1e-3;
    assert(!NearlyEqual(later, pose));

    // A velocity is allowed more room than a position, because it is derived
    // by dividing one by a frame interval.
    HumanoidPose drifting = pose;
    drifting.root.linearVelocity[2] += 5e-5f;
    assert(NearlyEqual(drifting, pose));
    assert(!NearlyEqual(drifting, pose, MotionTolerance{1e-4f, 1e-5f, 1e-6f}));

    // The caller can state its own, and the stated one is what is applied.
    assert(NearlyEqual(nudged, pose, MotionTolerance{1e-2f}));
}

void
TestNonFinite()
{
    HumanoidPose pose = SamplePose();
    pose.timestamp = std::numeric_limits<double>::quiet_NaN();
    // A NaN equals nothing, including itself, under both comparisons.
    assert(pose != pose);
    assert(!NearlyEqual(pose, pose));

    HumanoidPose broken = SamplePose();
    broken.localRotations[static_cast<std::size_t>(HumanBone::Hips)] =
        pxr::GfQuatf(std::numeric_limits<float>::quiet_NaN(),
                     pxr::GfVec3f(0.0f));
    assert(broken != broken);
    assert(!NearlyEqual(broken, broken));
}

void
TestDifferenceReport()
{
    const HumanoidPose pose = SamplePose();
    std::string difference = "untouched";

    // Nothing is written when the two agree.
    assert(NearlyEqual(pose, pose, MotionTolerance{}, &difference));
    assert(difference == "untouched");

    HumanoidPose nudged = pose;
    nudged.localRotations[static_cast<std::size_t>(HumanBone::LeftUpperArm)] =
        AboutY(-0.75 + 0.01);
    assert(!NearlyEqual(nudged, pose, MotionTolerance{}, &difference));
    assert(difference.rfind("leftUpperArm rotation differs by ", 0) == 0);

    // The first difference in a fixed order, so the same pair always reports
    // the same line: the timestamp is checked before any bone.
    nudged.timestamp += 1.0;
    assert(!NearlyEqual(nudged, pose, MotionTolerance{}, &difference));
    assert(difference.rfind("timestamp differs by ", 0) == 0);

    HumanoidPose absent = pose;
    absent.validRotations.reset(
        static_cast<std::size_t>(HumanBone::LeftUpperArm));
    assert(!NearlyEqual(absent, pose, MotionTolerance{}, &difference));
    assert(difference == "leftUpperArm is present only in the second pose");
    assert(!NearlyEqual(pose, absent, MotionTolerance{}, &difference));
    assert(difference == "leftUpperArm is present only in the first pose");
}

void
TestAnimation()
{
    const HumanoidAnimation clip = SampleAnimation();
    assert(clip == clip);
    assert(NearlyEqual(clip, clip));

    std::string difference;
    HumanoidAnimation shorter = clip;
    shorter.samples.pop_back();
    assert(shorter != clip);
    assert(!NearlyEqual(shorter, clip, MotionTolerance{}, &difference));
    assert(difference == "sample count differs: 2 vs 3");

    // A sample-level difference is reported with the sample that carried it.
    HumanoidAnimation bent = clip;
    bent.samples[1].localRotations[static_cast<std::size_t>(HumanBone::Hips)] =
        AboutY(0.5);
    assert(bent != clip);
    assert(!NearlyEqual(bent, clip, MotionTolerance{}, &difference));
    assert(difference.rfind("sample 1: hips rotation differs by ", 0) == 0);

    HumanoidAnimation faster = clip;
    faster.nominalFrameRate = 60.0;
    assert(faster != clip);
    assert(!NearlyEqual(faster, clip, MotionTolerance{}, &difference));
    assert(difference.rfind("nominalFrameRate differs by ", 0) == 0);
}

void
TestExactImpliesNearly()
{
    // The two comparisons read the same fields, so the strict one can never
    // accept a pair the tolerant one rejects.
    const HumanoidPose pose = SamplePose();
    HumanoidPose copy = pose;
    assert(copy == pose && NearlyEqual(copy, pose));

    const HumanoidAnimation clip = SampleAnimation();
    HumanoidAnimation clipCopy = clip;
    assert(clipCopy == clip && NearlyEqual(clipCopy, clip));

    HumanoidPose empty;
    assert(empty == HumanoidPose() && NearlyEqual(empty, HumanoidPose()));
}

} // namespace

int
main()
{
    TestAngleBetween();
    TestExactEquality();
    TestValueAndMotionDiverge();
    TestTolerance();
    TestNonFinite();
    TestDifferenceReport();
    TestAnimation();
    TestExactImpliesNearly();
    return 0;
}
