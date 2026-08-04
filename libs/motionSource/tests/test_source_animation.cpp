// SPDX-License-Identifier: Apache-2.0
//
// The source animation model: what a recording animated, in the source's own
// angle order and unit, with the invariants that keep a track index meaning a
// joint index.
#include "motionSource/SourceAnimation.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace
{

using motionSource::FindSourceAngleUnit;
using motionSource::FindSourceEulerOrder;
using motionSource::SourceAngleUnit;
using motionSource::SourceAnimation;
using motionSource::SourceAngleUnitName;
using motionSource::SourceEulerAngles;
using motionSource::SourceEulerAxis;
using motionSource::SourceEulerOrder;
using motionSource::SourceEulerOrderName;
using motionSource::SourceJoint;
using motionSource::SourceJointTrack;
using motionSource::SourceQuat;
using motionSource::SourceSkeleton;
using motionSource::SourceVec3;
using motionSource::ValidateSourceAnimation;

SourceSkeleton
MakeSkeleton()
{
    SourceSkeleton skeleton;
    SourceJoint root;
    root.name = "root";
    root.parent = -1;
    SourceJoint spine;
    spine.name = "spine";
    spine.parent = 0;
    skeleton.joints = {root, spine};
    return skeleton;
}

// Three frames at 50 Hz: the root translates and rotates, the spine only
// rotates. The spine's empty `translations` is the model's claim that the source
// animated nothing there -- not that it held still.
SourceAnimation
MakeAnimation()
{
    SourceAnimation animation;
    animation.frameCount = 3;
    animation.frameTime = 0.02;
    animation.tracks.resize(2);

    SourceJointTrack& root = animation.tracks[0];
    root.translations = {
        {0.0f, 95.98f, 0.0f}, {0.5f, 95.90f, 0.1f}, {1.0f, 95.80f, 0.2f}};
    root.eulerAngles = {{0.0f, 0.0f, 0.0f}, {1.0f, 2.0f, 3.0f},
                        {2.0f, 4.0f, 6.0f}};
    root.eulerOrder = SourceEulerOrder::ZXY;
    root.angleUnit = SourceAngleUnit::Degrees;

    SourceJointTrack& spine = animation.tracks[1];
    spine.eulerAngles = {{0.0f, 0.0f, 0.0f}, {0.5f, 0.0f, 0.0f},
                         {1.0f, 0.0f, 0.0f}};
    spine.eulerOrder = SourceEulerOrder::ZXY;

    animation.provenance.format = "test-fixture";
    return animation;
}

void
TestEulerOrderVocabulary()
{
    const SourceEulerOrder every[] = {
        SourceEulerOrder::XYZ, SourceEulerOrder::XZY, SourceEulerOrder::YXZ,
        SourceEulerOrder::YZX, SourceEulerOrder::ZXY, SourceEulerOrder::ZYX};

    for (SourceEulerOrder order : every) {
        const std::string_view name = SourceEulerOrderName(order);
        assert(name.size() == 3);
        assert(FindSourceEulerOrder(name).has_value());
        assert(*FindSourceEulerOrder(name) == order);

        // The enumerator names the *storage* order, so the axis of each
        // component is readable straight off the name. Nothing here says how the
        // three compose into a rotation: that needs the handedness, which is a
        // profile's answer.
        for (std::size_t component = 0; component < 3; ++component) {
            const std::optional<int> axis = SourceEulerAxis(order, component);
            assert(axis.has_value());
            assert(*axis == name[component] - 'X');
        }
        assert(!SourceEulerAxis(order, 3).has_value());
    }

    // Writers disagree about case; a case difference is not a meaning
    // difference.
    assert(FindSourceEulerOrder("zxy") == SourceEulerOrder::ZXY);
    assert(!FindSourceEulerOrder("ZZY").has_value());
    assert(!FindSourceEulerOrder("").has_value());
    assert(SourceEulerOrderName(SourceEulerOrder::Count).empty());
    assert(!SourceEulerAxis(SourceEulerOrder::Count, 0).has_value());
}

void
TestAngleUnitVocabulary()
{
    assert(SourceAngleUnitName(SourceAngleUnit::Degrees) == "degrees");
    assert(SourceAngleUnitName(SourceAngleUnit::Radians) == "radians");
    assert(SourceAngleUnitName(SourceAngleUnit::Count).empty());
    assert(FindSourceAngleUnit("RADIANS") == SourceAngleUnit::Radians);
    assert(!FindSourceAngleUnit("gradians").has_value());
}

void
TestTiming()
{
    SourceAnimation animation = MakeAnimation();
    assert(animation.Time(0).has_value());
    assert(*animation.Time(0) == 0.0);
    assert(*animation.Time(2) == 0.04);
    assert(!animation.Time(3).has_value());

    // (frameCount - 1) * frameTime: one sample is an instant, and the interval
    // after the last sample was never observed.
    assert(animation.Duration() == 0.04);
    assert(animation.EndTime() == 0.04);
    assert(animation.FrameRate().has_value());
    // A derived rate, so a tolerance: 1 / 0.02 is a division, and pinning it to
    // an exact double would be testing the FPU rather than the model.
    assert(std::fabs(*animation.FrameRate() - 50.0) < 1e-9);

    animation.startTime = 10.0;
    assert(*animation.Time(0) == 10.0);
    assert(animation.EndTime() == 10.0 + animation.Duration());
    assert(animation.Duration() == 0.04);

    SourceAnimation single;
    single.tracks.resize(2);
    single.frameCount = 1;
    single.frameTime = 0.0;
    assert(single.Duration() == 0.0);
    assert(single.EndTime() == 0.0);
    assert(!single.FrameRate().has_value());

    SourceAnimation empty;
    empty.startTime = 4.0;
    assert(!empty.Time(0).has_value());
    assert(empty.EndTime() == 4.0);
}

void
TestTrackPredicates()
{
    const SourceAnimation animation = MakeAnimation();
    assert(animation.tracks[0].HasTranslation());
    assert(animation.tracks[0].HasRotation());
    assert(!animation.tracks[0].IsEmpty());

    assert(!animation.tracks[1].HasTranslation());
    assert(animation.tracks[1].HasRotation());

    assert(SourceJointTrack{}.IsEmpty());
}

void
TestEquality()
{
    const SourceAnimation animation = MakeAnimation();
    SourceAnimation other = MakeAnimation();
    assert(animation == other);

    other.tracks[1].eulerAngles[1].first = 0.75f;
    assert(animation != other);

    // The order is part of a rotation's meaning, so two identical angle triples
    // under different orders are different motion.
    other = MakeAnimation();
    other.tracks[1].eulerOrder = SourceEulerOrder::XYZ;
    assert(animation != other);

    other = MakeAnimation();
    other.tracks[1].angleUnit = SourceAngleUnit::Radians;
    assert(animation != other);

    // ...but a track with no angles carries defaults nothing set, so they must
    // not be able to make two such tracks differ.
    SourceAnimation bare = MakeAnimation();
    bare.tracks[1] = SourceJointTrack{};
    SourceAnimation bareOtherOrder = bare;
    bareOtherOrder.tracks[1].eulerOrder = SourceEulerOrder::YZX;
    bareOtherOrder.tracks[1].angleUnit = SourceAngleUnit::Radians;
    assert(bare == bareOtherOrder);

    other = MakeAnimation();
    other.frameTime = 0.04;
    assert(animation != other);

    other = MakeAnimation();
    other.provenance.sourceId = "elsewhere";
    assert(animation != other);
}

void
Refuses(const SourceAnimation& animation, const SourceSkeleton& skeleton,
        const char* expectedFragment)
{
    std::string reason;
    assert(!ValidateSourceAnimation(animation, skeleton, &reason));
    assert(reason.find(expectedFragment) != std::string::npos);
}

void
TestValidationRefusals()
{
    const SourceSkeleton skeleton = MakeSkeleton();
    std::string reason = "not cleared";
    assert(ValidateSourceAnimation(MakeAnimation(), skeleton, &reason));
    assert(reason.empty());

    // The two values are bound by an index and nothing else, so an animation
    // checked against the wrong rig has to be caught here.
    SourceAnimation animation = MakeAnimation();
    animation.tracks.pop_back();
    Refuses(animation, skeleton, "1 track(s) for 2 joint(s)");

    animation = MakeAnimation();
    animation.tracks[1].rotations = {SourceQuat{}, SourceQuat{}, SourceQuat{}};
    Refuses(animation, skeleton, "both angle and quaternion rotations");

    animation = MakeAnimation();
    animation.tracks[0].translations.pop_back();
    Refuses(animation, skeleton, "carries 2 translation(s) for 3 frame(s)");

    animation = MakeAnimation();
    animation.tracks[1].eulerAngles.pop_back();
    Refuses(animation, skeleton, "carries 2 angle sample(s) for 3 frame(s)");

    animation = MakeAnimation();
    animation.tracks[0].eulerOrder = static_cast<SourceEulerOrder>(9);
    Refuses(animation, skeleton, "no known Euler order");

    animation = MakeAnimation();
    animation.tracks[0].angleUnit = static_cast<SourceAngleUnit>(9);
    Refuses(animation, skeleton, "no known angle unit");

    const float infinity = std::numeric_limits<float>::infinity();
    const float notANumber = std::numeric_limits<float>::quiet_NaN();

    animation = MakeAnimation();
    animation.tracks[0].translations[1].x = infinity;
    Refuses(animation, skeleton, "non-finite translation");

    animation = MakeAnimation();
    animation.tracks[1].eulerAngles[2].second = notANumber;
    Refuses(animation, skeleton, "non-finite angle");

    // A frame time is only zero where no interval was observed.
    animation = MakeAnimation();
    animation.frameTime = 0.0;
    Refuses(animation, skeleton, "frame time is zero");

    animation = MakeAnimation();
    animation.frameTime = -0.02;
    Refuses(animation, skeleton, "non-negative interval");

    animation = MakeAnimation();
    animation.startTime = notANumber;
    Refuses(animation, skeleton, "start time is not finite");

    // An invalid rig is reported as one rather than as a track failure: the
    // caller's next move is different for each.
    SourceSkeleton broken = MakeSkeleton();
    broken.joints[1].name.clear();
    Refuses(MakeAnimation(), broken, "skeleton is invalid");
}

// Below two frames there is no interval, so zero is the honest value and the
// validator has to accept it.
void
TestSingleFrameClip()
{
    const SourceSkeleton skeleton = MakeSkeleton();
    SourceAnimation animation;
    animation.tracks.resize(2);
    animation.frameCount = 1;
    animation.frameTime = 0.0;
    animation.tracks[0].translations = {{0.0f, 95.98f, 0.0f}};
    assert(ValidateSourceAnimation(animation, skeleton));

    animation.frameCount = 0;
    animation.tracks[0].translations.clear();
    assert(ValidateSourceAnimation(animation, skeleton));
}

// A quaternion-stated rotation is a first-class form here, not a conversion of
// an angle triple: decomposing one into angles would invent an order the source
// never declared.
void
TestQuaternionTrack()
{
    const SourceSkeleton skeleton = MakeSkeleton();
    SourceAnimation animation;
    animation.tracks.resize(2);
    animation.frameCount = 2;
    animation.frameTime = 0.02;
    animation.tracks[1].rotations = {SourceQuat{1.0f, 0.0f, 0.0f, 0.0f},
                                     SourceQuat{0.9f, 0.1f, 0.0f, 0.0f}};
    assert(ValidateSourceAnimation(animation, skeleton));
    assert(animation.tracks[1].HasRotation());

    // Un-normalised is a writer's imprecision and is kept; zero is not a
    // rotation in any convention and is refused.
    animation.tracks[1].rotations[1] = SourceQuat{0.0f, 0.0f, 0.0f, 0.0f};
    Refuses(animation, skeleton, "zero-magnitude rotation");

    animation.tracks[1].rotations[1] =
        SourceQuat{std::numeric_limits<float>::infinity(), 0.0f, 0.0f, 0.0f};
    Refuses(animation, skeleton, "non-finite rotation");
}

} // namespace

int
main()
{
    TestEulerOrderVocabulary();
    TestAngleUnitVocabulary();
    TestTiming();
    TestTrackPredicates();
    TestEquality();
    TestValidationRefusals();
    TestSingleFrameClip();
    TestQuaternionTrack();
    std::printf("motionSource animation model: verified\n");
    return 0;
}
