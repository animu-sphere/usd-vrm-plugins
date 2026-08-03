// SPDX-License-Identifier: Apache-2.0
#include "motionRuntime/Blend.h"
#include "motionRuntime/Filter.h"
#include "motionRuntime/Interpolation.h"
#include "motionRuntime/PoseBuffer.h"
#include "motionRuntime/Resample.h"

#include <cassert>
#include <cmath>
#include <cstdio>

namespace
{

constexpr float kEpsilon = 1e-4f;

bool
NearlyEqual(float a, float b)
{
    return std::fabs(a - b) <= kEpsilon;
}

bool
NearlyEqual(const pxr::GfVec3f& a, const pxr::GfVec3f& b)
{
    return NearlyEqual(a[0], b[0]) && NearlyEqual(a[1], b[1])
        && NearlyEqual(a[2], b[2]);
}

// Compares orientations, not representations: q and -q are the same rotation.
bool
SameOrientation(const pxr::GfQuatf& a, const pxr::GfQuatf& b)
{
    const pxr::GfQuatf na = a.GetNormalized();
    pxr::GfQuatf nb = b.GetNormalized();
    if (pxr::GfDot(na, nb) < 0.0f) {
        nb = pxr::GfQuatf(-nb.GetReal(), -nb.GetImaginary());
    }
    return NearlyEqual(na.GetReal(), nb.GetReal())
        && NearlyEqual(na.GetImaginary(), nb.GetImaginary());
}

pxr::GfQuatf
RotationX(float degrees)
{
    const float radians = degrees * 3.14159265358979324f / 180.0f;
    return pxr::GfQuatf(std::cos(radians * 0.5f),
                        pxr::GfVec3f(std::sin(radians * 0.5f), 0.0f, 0.0f));
}

motion::HumanoidPose
MakePose(double timestamp, float hipsDegrees, const pxr::GfVec3f& rootPosition)
{
    motion::HumanoidPose pose;
    pose.timestamp = timestamp;
    pose.localRotations[static_cast<std::size_t>(motion::HumanBone::Hips)] =
        RotationX(hipsDegrees);
    pose.validRotations.set(static_cast<std::size_t>(motion::HumanBone::Hips));
    pose.root.worldPosition = rootPosition;
    pose.root.hasPosition = true;
    return pose;
}

void
TestSlerpTakesTheShortArc()
{
    const pxr::GfQuatf identity(1.0f, pxr::GfVec3f(0.0f));
    const pxr::GfQuatf ninety = RotationX(90.0f);
    const pxr::GfQuatf ninetyFlipped(-ninety.GetReal(), -ninety.GetImaginary());

    const pxr::GfQuatf direct = motion::SlerpShortest(identity, ninety, 0.5f);
    const pxr::GfQuatf viaFlipped =
        motion::SlerpShortest(identity, ninetyFlipped, 0.5f);

    assert(SameOrientation(direct, RotationX(45.0f)));
    // The sign-flipped representative must not spin the long way round.
    assert(SameOrientation(direct, viaFlipped));

    assert(SameOrientation(motion::SlerpShortest(identity, ninety, -1.0f),
                           identity));
    assert(SameOrientation(motion::SlerpShortest(identity, ninety, 2.0f),
                           ninety));
}

void
TestMissingBonesAreHeldNotFaded()
{
    motion::HumanoidPose a;
    a.timestamp = 0.0;
    const auto head = static_cast<std::size_t>(motion::HumanBone::Head);
    a.localRotations[head] = RotationX(40.0f);
    a.validRotations.set(head);

    motion::HumanoidPose b;
    b.timestamp = 1.0;

    const motion::HumanoidPose mid = motion::LerpPose(a, b, 0.5f);
    assert(mid.validRotations.test(head));
    // Held at its only observed value rather than dragged toward identity.
    assert(SameOrientation(mid.localRotations[head], RotationX(40.0f)));
    assert(NearlyEqual(static_cast<float>(mid.timestamp), 0.5f));

    // A bone present in neither endpoint stays absent.
    const auto jaw = static_cast<std::size_t>(motion::HumanBone::Jaw);
    assert(!mid.validRotations.test(jaw));
}

void
TestExpressionsAreHeldNotFaded()
{
    motion::HumanoidPose a;
    a.timestamp = 0.0;
    a.expressions.Set("happy", 0.2f);
    a.expressions.Set("blink", 1.0f);

    motion::HumanoidPose b;
    b.timestamp = 1.0;
    b.expressions.Set("happy", 0.6f);
    b.expressions.Set("aa", 0.5f);

    const motion::HumanoidPose mid = motion::LerpPose(a, b, 0.5f);

    // Reported by both: interpolated.
    const float* happy = mid.expressions.Find("happy");
    assert(happy != nullptr && NearlyEqual(*happy, 0.4f));

    // Reported by one endpoint only: held at that weight. Fading it toward zero
    // would invent a blink closing that neither pose described -- the same rule
    // a missing bone follows, for the same reason.
    const float* blink = mid.expressions.Find("blink");
    assert(blink != nullptr && NearlyEqual(*blink, 1.0f));
    const float* aa = mid.expressions.Find("aa");
    assert(aa != nullptr && NearlyEqual(*aa, 0.5f));

    // A name neither endpoint reported stays unreported rather than becoming 0.
    assert(mid.expressions.Find("sad") == nullptr);
    // The union arrives sorted, whichever endpoint each name came from.
    assert(mid.expressions.entries.size() == 3);
    assert(mid.expressions.entries[0].name == "aa");
    assert(mid.expressions.entries[1].name == "blink");
    assert(mid.expressions.entries[2].name == "happy");
}

void
TestRootMotionFlagsSurviveInterpolation()
{
    motion::RootMotion a;
    a.worldPosition = pxr::GfVec3f(0.0f, 1.0f, 0.0f);
    a.hasPosition = true;

    motion::RootMotion b;
    b.worldPosition = pxr::GfVec3f(0.0f, 1.0f, 2.0f);
    b.hasPosition = true;
    b.worldOrientation = RotationX(90.0f);
    b.hasOrientation = true;

    const motion::RootMotion mid = motion::LerpRootMotion(a, b, 0.5f);
    assert(mid.hasPosition);
    assert(NearlyEqual(mid.worldPosition, pxr::GfVec3f(0.0f, 1.0f, 1.0f)));
    // Orientation is carried from the only endpoint that reported one.
    assert(mid.hasOrientation);
    assert(SameOrientation(mid.worldOrientation, RotationX(90.0f)));
    assert(!mid.hasLinearVelocity);
}

void
TestPoseBufferOrderingAndSampling()
{
    motion::PoseBuffer buffer(3);
    assert(buffer.IsEmpty());
    assert(!buffer.Sample(0.0).has_value());

    assert(buffer.Push(MakePose(0.0, 0.0f, pxr::GfVec3f(0.0f))));
    assert(buffer.Push(MakePose(1.0, 90.0f, pxr::GfVec3f(0.0f, 0.0f, 1.0f))));
    // Out-of-order and duplicate timestamps are refused, not reordered.
    assert(!buffer.Push(MakePose(0.5, 0.0f, pxr::GfVec3f(0.0f))));
    assert(!buffer.Push(MakePose(1.0, 0.0f, pxr::GfVec3f(0.0f))));
    assert(buffer.GetSize() == 2);

    double start = 0.0;
    double end = 0.0;
    assert(buffer.GetTimeRange(&start, &end));
    assert(NearlyEqual(static_cast<float>(start), 0.0f));
    assert(NearlyEqual(static_cast<float>(end), 1.0f));

    const auto mid = buffer.Sample(0.5);
    assert(mid.has_value());
    const auto hips = static_cast<std::size_t>(motion::HumanBone::Hips);
    assert(SameOrientation(mid->localRotations[hips], RotationX(45.0f)));
    assert(NearlyEqual(mid->root.worldPosition,
                       pxr::GfVec3f(0.0f, 0.0f, 0.5f)));

    // Outside the buffered range the boundary pose is held.
    assert(SameOrientation(buffer.Sample(-5.0)->localRotations[hips],
                           RotationX(0.0f)));
    assert(SameOrientation(buffer.Sample(5.0)->localRotations[hips],
                           RotationX(90.0f)));

    // Capacity evicts oldest-first.
    assert(buffer.Push(MakePose(2.0, 90.0f, pxr::GfVec3f(0.0f, 0.0f, 2.0f))));
    assert(buffer.Push(MakePose(3.0, 90.0f, pxr::GfVec3f(0.0f, 0.0f, 3.0f))));
    assert(buffer.GetSize() == 3);
    assert(NearlyEqual(static_cast<float>(buffer.GetOldest().timestamp), 1.0f));
    assert(NearlyEqual(static_cast<float>(buffer.GetNewest().timestamp), 3.0f));
}

void
TestPoseBufferExtrapolatesPositionOnly()
{
    motion::PoseBuffer buffer;
    buffer.Push(MakePose(0.0, 0.0f, pxr::GfVec3f(0.0f)));
    buffer.Push(MakePose(1.0, 90.0f, pxr::GfVec3f(0.0f, 0.0f, 1.0f)));

    const auto lead = buffer.SampleExtrapolated(1.1, 0.2);
    assert(lead.has_value());
    // 1 m/s derived from the last two samples, advanced by 0.1 s.
    assert(NearlyEqual(lead->root.worldPosition,
                       pxr::GfVec3f(0.0f, 0.0f, 1.1f)));
    // Rotation is held, never extrapolated.
    const auto hips = static_cast<std::size_t>(motion::HumanBone::Hips);
    assert(SameOrientation(lead->localRotations[hips], RotationX(90.0f)));

    // The lead is capped.
    const auto capped = buffer.SampleExtrapolated(5.0, 0.2);
    assert(NearlyEqual(capped->root.worldPosition,
                       pxr::GfVec3f(0.0f, 0.0f, 1.2f)));

    // Before the newest sample it behaves exactly like Sample.
    const auto inside = buffer.SampleExtrapolated(0.5, 0.2);
    assert(SameOrientation(inside->localRotations[hips], RotationX(45.0f)));
}

void
TestResampleCoversTheWholeInterval()
{
    motion::HumanoidAnimation animation;
    animation.startTime = 0.0;
    animation.endTime = 1.0;
    animation.nominalFrameRate = 30.0;
    animation.samples.push_back(MakePose(0.0, 0.0f, pxr::GfVec3f(0.0f)));
    animation.samples.push_back(
        MakePose(1.0, 90.0f, pxr::GfVec3f(0.0f, 0.0f, 1.0f)));

    const motion::HumanoidAnimation uniform = motion::Resample(animation, 4.0);
    assert(uniform.samples.size() == 5);
    assert(NearlyEqual(static_cast<float>(uniform.nominalFrameRate), 4.0f));
    const auto hips = static_cast<std::size_t>(motion::HumanBone::Hips);
    assert(SameOrientation(uniform.samples[1].localRotations[hips],
                           RotationX(22.5f)));
    assert(NearlyEqual(static_cast<float>(uniform.samples.back().timestamp),
                       1.0f));

    // A duration that is not a multiple of the step still ends on endTime.
    animation.endTime = 0.7;
    const motion::HumanoidAnimation ragged = motion::Resample(animation, 4.0);
    assert(NearlyEqual(static_cast<float>(ragged.samples.back().timestamp),
                       0.7f));

    // A clip that carries samples but never declared its interval falls back to
    // the sample timestamps instead of collapsing to a single pose.
    motion::HumanoidAnimation undeclared;
    undeclared.samples = animation.samples;
    const motion::HumanoidAnimation recovered =
        motion::Resample(undeclared, 4.0);
    assert(recovered.samples.size() == 5);
    assert(NearlyEqual(static_cast<float>(recovered.endTime), 1.0f));

    // Degenerate inputs yield no samples rather than a fabricated timeline.
    assert(motion::Resample(animation, 0.0).samples.empty());
    assert(motion::Resample(motion::HumanoidAnimation(), 30.0).samples.empty());
}

void
TestFilterIsFrameRateIndependentAndTolerantOfDropouts()
{
    motion::PoseFilter::Options options;
    options.cutoffHz = 0.0f;
    motion::PoseFilter passthrough(options);
    const motion::HumanoidPose raw = MakePose(0.0, 90.0f, pxr::GfVec3f(0.0f));
    const auto hips = static_cast<std::size_t>(motion::HumanBone::Hips);
    assert(SameOrientation(passthrough.Apply(raw).localRotations[hips],
                           RotationX(90.0f)));

    options.cutoffHz = 1.0f;
    motion::PoseFilter filter(options);
    // The first pose seeds the state and passes through untouched.
    assert(SameOrientation(
        filter.Apply(MakePose(0.0, 0.0f, pxr::GfVec3f(0.0f)))
            .localRotations[hips],
        RotationX(0.0f)));

    const motion::HumanoidPose smoothed =
        filter.Apply(MakePose(0.1, 90.0f, pxr::GfVec3f(0.0f, 0.0f, 1.0f)));
    // Moves toward the new sample without reaching it.
    const float real = smoothed.localRotations[hips].GetNormalized().GetReal();
    assert(real < std::cos(0.0f));
    assert(real > RotationX(90.0f).GetReal());
    assert(smoothed.root.worldPosition[2] > 0.0f);
    assert(smoothed.root.worldPosition[2] < 1.0f);

    // A bone that drops out is not invented, and its history is retained.
    motion::HumanoidPose withoutHips;
    withoutHips.timestamp = 0.2;
    const motion::HumanoidPose dropout = filter.Apply(withoutHips);
    assert(!dropout.validRotations.test(hips));
    assert(filter.HasState());

    filter.Reset();
    assert(!filter.HasState());
}

void
TestBlendWeightsAndUnitLength()
{
    const motion::HumanoidPose a = MakePose(0.0, 0.0f, pxr::GfVec3f(0.0f));
    const motion::HumanoidPose b =
        MakePose(0.0, 90.0f, pxr::GfVec3f(0.0f, 0.0f, 1.0f));
    const auto hips = static_cast<std::size_t>(motion::HumanBone::Hips);

    assert(SameOrientation(motion::BlendPoses(a, b, 0.0f).localRotations[hips],
                           RotationX(0.0f)));
    assert(SameOrientation(motion::BlendPoses(a, b, 1.0f).localRotations[hips],
                           RotationX(90.0f)));

    const motion::HumanoidPose even = motion::BlendPoses({{a, 1.0f}, {b, 1.0f}});
    assert(SameOrientation(even.localRotations[hips], RotationX(45.0f)));
    assert(NearlyEqual(even.localRotations[hips].GetLength(), 1.0f));

    // Non-positive weights drop out; the surviving pose wins outright.
    const motion::HumanoidPose skewed =
        motion::BlendPoses({{a, 0.0f}, {b, 2.0f}, {a, -1.0f}});
    assert(SameOrientation(skewed.localRotations[hips], RotationX(90.0f)));

    assert(!motion::BlendPoses({}).validRotations.any());
}

} // namespace

int
main()
{
    TestSlerpTakesTheShortArc();
    TestMissingBonesAreHeldNotFaded();
    TestExpressionsAreHeldNotFaded();
    TestRootMotionFlagsSurviveInterpolation();
    TestPoseBufferOrderingAndSampling();
    TestPoseBufferExtrapolatesPositionOnly();
    TestResampleCoversTheWholeInterval();
    TestFilterIsFrameRateIndependentAndTolerantOfDropouts();
    TestBlendWeightsAndUnitLength();
    std::puts("motionRuntime unit tests passed");
    return 0;
}
