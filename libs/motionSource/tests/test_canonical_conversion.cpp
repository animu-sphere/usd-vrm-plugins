// SPDX-License-Identifier: Apache-2.0
//
// The converter: a recording plus what its producer meant, becoming canonical
// humanoid motion.
//
// Two kinds of case, and the split is deliberate. The basis and the angle
// composition are checked *physically* — a direction is rotated and the answer
// is compared against where that direction has to end up — because both are
// places where a wrong convention produces plausible motion rather than an
// error, and a test comparing components against components would agree with a
// mirrored implementation as readily as with a correct one. Everything above
// them is checked structurally, over rigs assembled by hand.
//
// Tolerances are `motion::MotionTolerance`'s and never a number chosen here: an
// epsilon picked to make one machine pass is the defect the contract's
// comparison semantics exist to avoid.
//
// No producer appears. Every rig below is a plausible shape with ordinary names.
#include "motionSource/CanonicalConversion.h"

#include "motionSource/SourceAnimation.h"
#include "motionSource/SourceProfile.h"
#include "motionSource/SourceSkeleton.h"

#include "motionCore/Compare.h"
#include "motionCore/Humanoid.h"

#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace
{

using motionSource::CanonicalBasis;
using motionSource::ComposeSourceRotation;
using motionSource::ConversionRefusal;
using motionSource::ConvertPosition;
using motionSource::ConvertRotation;
using motionSource::ConvertSourceToCanonical;
using motionSource::MakeCanonicalBasis;
using motionSource::RestPoseSource;
using motionSource::RootRotationPolicy;
using motionSource::RootTranslationPolicy;
using motionSource::SourceAngleUnit;
using motionSource::SourceAnimation;
using motionSource::SourceAxis;
using motionSource::SourceConversion;
using motionSource::SourceEulerAngles;
using motionSource::SourceEulerOrder;
using motionSource::SourceHandedness;
using motionSource::SourceJoint;
using motionSource::SourceJointMapping;
using motionSource::SourceJointTrack;
using motionSource::SourceLengthUnit;
using motionSource::SourceProfile;
using motionSource::SourceQuat;
using motionSource::SourceSkeleton;
using motionSource::SourceVec3;
using motionSource::UnmappedJointPolicy;

const motion::MotionTolerance kTolerance;

SourceVec3
Vec(float x, float y, float z)
{
    SourceVec3 value;
    value.x = x;
    value.y = y;
    value.z = z;
    return value;
}

bool
NearVector(const pxr::GfVec3f& actual, const pxr::GfVec3f& expected)
{
    return (actual - expected).GetLength() <= kTolerance.distance;
}

bool
NearRotation(const pxr::GfQuatf& actual, const pxr::GfQuatf& expected)
{
    return motion::AngleBetween(actual, expected) <= kTolerance.angle;
}

pxr::GfQuatf
AboutY(float degrees)
{
    const float half =
        static_cast<float>(degrees * 3.14159265358979323846 / 360.0);
    return pxr::GfQuatf(std::cos(half),
                        pxr::GfVec3f(0.0f, std::sin(half), 0.0f));
}

// A right-handed source that already agrees with canonical about everything but
// its unit -- the case where the basis has nothing to do and the unit does.
SourceProfile
BaseProfile()
{
    SourceProfile profile;
    profile.id = "example-source-default-v1";
    profile.producer = "Example Producer";
    profile.rootJoint = "root";
    profile.handedness = SourceHandedness::Right;
    profile.upAxis = SourceAxis::PlusY;
    profile.forwardAxis = SourceAxis::PlusZ;
    profile.translationUnit = SourceLengthUnit::Centimeters;
    profile.rootTranslation = RootTranslationPolicy::AbsolutePosition;
    profile.rootRotation = RootRotationPolicy::BodyOrientation;
    profile.restPose = RestPoseSource::RestOffsets;
    profile.unmappedJoints = UnmappedJointPolicy::Refuse;
    profile.joints = {
        SourceJointMapping{"root", motion::HumanBone::Hips, true},
        SourceJointMapping{"back", motion::HumanBone::Spine, true},
        SourceJointMapping{"crown", motion::HumanBone::Head, true},
    };
    // The segment between `root` and `back` carries no canonical bone and is
    // named here so that `refuse` above means what it says.
    profile.ignoredJoints = {"segment"};
    return profile;
}

// root -> segment -> back -> crown, with `segment` mapped to nothing: the shape
// the composition rule exists for.
SourceSkeleton
BaseSkeleton()
{
    SourceSkeleton skeleton;
    SourceJoint root;
    root.name = "root";
    root.parent = -1;
    root.restTranslation = Vec(0.0f, 90.0f, 0.0f);
    SourceJoint segment;
    segment.name = "segment";
    segment.parent = 0;
    segment.restTranslation = Vec(0.0f, 10.0f, 0.0f);
    SourceJoint back;
    back.name = "back";
    back.parent = 1;
    back.restTranslation = Vec(0.0f, 10.0f, 0.0f);
    SourceJoint crown;
    crown.name = "crown";
    crown.parent = 2;
    crown.restTranslation = Vec(0.0f, 30.0f, 0.0f);
    crown.tipOffset = Vec(0.0f, 0.1f, 0.0f);
    skeleton.joints = {root, segment, back, crown};
    return skeleton;
}

// The same rig with a reference node above it: `reference` -> `root` ->
// `segment` -> `back` -> `crown`, where `root` still carries the hips and
// `reference` carries no canonical bone at all.
//
// This is the shape the second producer's export has (roadmap §9) and the first
// producer's does not, and the two are indistinguishable until one of them
// exists: a rig whose root *is* its hips answers "where is the body" on one
// joint, and this one spreads it over two. `root`'s rest translation is
// deliberately unlike anything a body has -- the export this is modelled on
// states an offset that records where the capture volume put the performer.
SourceSkeleton
SplitRootSkeleton()
{
    SourceSkeleton skeleton;
    SourceJoint reference;
    reference.name = "reference";
    reference.parent = -1;
    SourceJoint root;
    root.name = "root";
    root.parent = 0;
    root.restTranslation = Vec(0.0f, 90.0f, -400.0f);
    SourceSkeleton base = BaseSkeleton();
    skeleton.joints = {reference, root};
    for (std::size_t index = 1; index < base.joints.size(); ++index) {
        SourceJoint joint = base.joints[index];
        joint.parent += 1;
        skeleton.joints.push_back(joint);
    }
    return skeleton;
}

SourceProfile
SplitRootProfile()
{
    SourceProfile profile = BaseProfile();
    // The rig's root, which is what a profile is *matched* by. Which joint the
    // body's placement is read from is derived from the mapping instead, and is
    // the one bound to the hips.
    profile.rootJoint = "reference";
    profile.ignoredJoints = {"reference", "segment"};
    return profile;
}

SourceJointTrack
RotationTrack(const std::vector<SourceEulerAngles>& angles,
              SourceEulerOrder order = SourceEulerOrder::ZXY)
{
    SourceJointTrack track;
    track.eulerAngles = angles;
    track.eulerOrder = order;
    track.angleUnit = SourceAngleUnit::Degrees;
    return track;
}

SourceEulerAngles
Angles(float first, float second, float third)
{
    SourceEulerAngles value;
    value.first = first;
    value.second = second;
    value.third = third;
    return value;
}

SourceAnimation BaseAnimation();

// The placement split the way the second producer splits it: `reference`
// translates and never turns, `root` turns and states a translation of its own
// that has nothing to do with its rest offset.
SourceAnimation
SplitRootAnimation()
{
    SourceAnimation animation = BaseAnimation();
    SourceJointTrack reference =
        RotationTrack({Angles(0.0f, 0.0f, 0.0f), Angles(0.0f, 0.0f, 0.0f)});
    reference.translations = {Vec(0.0f, 0.0f, 0.0f), Vec(0.0f, 0.0f, 20.0f)};
    SourceJointTrack root = RotationTrack({Angles(0.0f, 0.0f, 45.0f),
                                           Angles(0.0f, 0.0f, 45.0f)});
    root.translations = {Vec(10.0f, 92.0f, 0.0f), Vec(10.0f, 92.0f, 0.0f)};
    animation.tracks.insert(animation.tracks.begin(), reference);
    animation.tracks[1] = root;
    return animation;
}

// Two frames, every joint still, the root standing at its own rest height.
SourceAnimation
BaseAnimation()
{
    SourceAnimation animation;
    animation.frameCount = 2;
    animation.frameTime = 0.5;
    animation.provenance.format = "example";
    animation.provenance.sourceId = "capture.example";

    SourceJointTrack root = RotationTrack({Angles(0.0f, 0.0f, 0.0f),
                                           Angles(0.0f, 0.0f, 0.0f)});
    root.translations = {Vec(0.0f, 90.0f, 0.0f), Vec(0.0f, 90.0f, 0.0f)};
    animation.tracks = {
        root,
        RotationTrack({Angles(0.0f, 0.0f, 0.0f), Angles(0.0f, 0.0f, 0.0f)}),
        RotationTrack({Angles(0.0f, 0.0f, 0.0f), Angles(0.0f, 0.0f, 0.0f)}),
        RotationTrack({Angles(0.0f, 0.0f, 0.0f), Angles(0.0f, 0.0f, 0.0f)}),
    };
    return animation;
}

// --- the basis -------------------------------------------------------------

void
TestBasisNeedsAStatedProfile()
{
    SourceProfile profile = BaseProfile();
    assert(MakeCanonicalBasis(profile).has_value());

    SourceProfile unstated;
    // Default-constructed: every convention `Unspecified`, which is what makes
    // "there is no default profile" checkable rather than merely intended.
    assert(!MakeCanonicalBasis(unstated).has_value());

    profile.forwardAxis = profile.upAxis;
    assert(!MakeCanonicalBasis(profile).has_value());
}

void
TestBasisOfAnAgreeingSource()
{
    const CanonicalBasis basis = *MakeCanonicalBasis(BaseProfile());
    assert(basis.determinant == 1);
    assert(basis.component[0] == 0 && basis.component[1] == 1
           && basis.component[2] == 2);
    assert(!basis.negate[0] && !basis.negate[1] && !basis.negate[2]);
    // Centimetres, so the only thing this basis does is divide by a hundred.
    assert(NearVector(ConvertPosition(basis, Vec(0.0f, 90.0f, 0.0f)),
                      pxr::GfVec3f(0.0f, 0.9f, 0.0f)));
}

// A Z-up right-handed source: the up and forward axes swap, and the third axis
// flips because that is what keeps the change of basis a rotation rather than a
// mirror.
void
TestBasisOfAZUpSource()
{
    SourceProfile profile = BaseProfile();
    profile.upAxis = SourceAxis::PlusZ;
    profile.forwardAxis = SourceAxis::PlusY;
    profile.translationUnit = SourceLengthUnit::Meters;
    const CanonicalBasis basis = *MakeCanonicalBasis(profile);
    assert(basis.determinant == 1);
    assert(NearVector(ConvertPosition(basis, Vec(1.0f, 2.0f, 3.0f)),
                      pxr::GfVec3f(-1.0f, 3.0f, 2.0f)));
}

// A *negative* forward axis: a source that faces the other way. The profile
// contract makes the sign load-bearing on purpose -- "+Z forward" and
// "-Z forward" are both real, and an unsigned axis would make a rig that faces
// backwards look like one that does not -- so this is the case that would pass
// under an implementation that dropped the sign and produce a character walking
// backwards.
void
TestBasisOfABackwardFacingSource()
{
    SourceProfile profile = BaseProfile();
    profile.forwardAxis = SourceAxis::MinusZ;
    profile.translationUnit = SourceLengthUnit::Meters;
    const CanonicalBasis basis = *MakeCanonicalBasis(profile);
    // Still right-handed, so still a rotation and not a mirror -- the third
    // axis is what absorbs the flip.
    assert(basis.determinant == 1);
    // The source's forward is canonical's forward.
    assert(NearVector(ConvertPosition(basis, Vec(0.0f, 0.0f, -1.0f)),
                      pxr::GfVec3f(0.0f, 0.0f, 1.0f)));
    // And its own +X is canonical -X, because in a right-handed source with +Y
    // up and -Z forward, up x forward is -X.
    assert(NearVector(ConvertPosition(basis, Vec(1.0f, 2.0f, 3.0f)),
                      pxr::GfVec3f(-1.0f, 2.0f, -3.0f)));
}

// A negative *up* axis, which is the other half of the same statement and flips
// the determinant's bookkeeping rather than the handedness.
void
TestBasisOfAnUpsideDownSource()
{
    SourceProfile profile = BaseProfile();
    profile.upAxis = SourceAxis::MinusY;
    profile.translationUnit = SourceLengthUnit::Meters;
    const CanonicalBasis basis = *MakeCanonicalBasis(profile);
    assert(basis.determinant == 1);
    // -Y up with +Z forward puts up x forward at -X, so the source's own +X is
    // canonical -X and the flip is not confined to the up axis.
    assert(NearVector(ConvertPosition(basis, Vec(1.0f, 2.0f, 3.0f)),
                      pxr::GfVec3f(-1.0f, -2.0f, 3.0f)));
}

// The mirror. A left-handed source with the same up and forward as canonical
// differs from it in exactly one axis, and the sign is not a free choice: it is
// whatever makes the determinant negative.
void
TestBasisOfALeftHandedSource()
{
    SourceProfile profile = BaseProfile();
    profile.handedness = SourceHandedness::Left;
    profile.translationUnit = SourceLengthUnit::Meters;
    const CanonicalBasis basis = *MakeCanonicalBasis(profile);
    assert(basis.determinant == -1);
    assert(NearVector(ConvertPosition(basis, Vec(1.0f, 2.0f, 3.0f)),
                      pxr::GfVec3f(-1.0f, 2.0f, 3.0f)));

    // And the rotation half, checked physically. A positive turn about the up
    // axis in a left-handed source carries the character's forward direction
    // towards its own +X, which is canonical -X. The canonical rotation that
    // takes +Z to -X is a *negative* ninety degrees about +Y -- so the mirror
    // has reversed the angle, which is precisely what a left-handed rotation
    // becomes once mirrored. A converter that also flipped the angle sign while
    // composing would land on +90 here and be right in every axis-aligned pose.
    const SourceQuat turn = ComposeSourceRotation(
        Angles(0.0f, 0.0f, 90.0f), SourceEulerOrder::ZXY,
        SourceAngleUnit::Degrees);
    const pxr::GfQuatf canonical = ConvertRotation(basis, turn);
    assert(NearRotation(canonical, AboutY(-90.0f)));
    assert(NearVector(canonical.Transform(pxr::GfVec3f(0.0f, 0.0f, 1.0f)),
                      pxr::GfVec3f(-1.0f, 0.0f, 0.0f)));
}

// A right-handed source of the same shape keeps the angle, which is the other
// half of the same statement.
void
TestRightHandedRotationKeepsItsAngle()
{
    const CanonicalBasis basis = *MakeCanonicalBasis(BaseProfile());
    const SourceQuat turn = ComposeSourceRotation(
        Angles(0.0f, 0.0f, 90.0f), SourceEulerOrder::ZXY,
        SourceAngleUnit::Degrees);
    assert(NearRotation(ConvertRotation(basis, turn), AboutY(90.0f)));
}

// The normalisation and the check that admits a rotation have to agree about
// precision. `ValidateSourceAnimation` and `ValidateSourceSkeleton` refuse only
// a quaternion whose four components are *exactly* zero, so they admit every
// magnitude below the float denormal floor -- and a length formed with
// `GfQuatf::GetLength()` squares in float and underflows those to exactly zero,
// returning the rotation un-normalised to collapse every composition it enters.
// The value arrives looking like data rather than like an error, which is what
// makes it worth a test at a magnitude no capture will ever carry.
void
TestARotationTooSmallToSquareIsStillNormalised()
{
    const CanonicalBasis basis = *MakeCanonicalBasis(BaseProfile());
    const float half = static_cast<float>(3.14159265358979323846 / 8.0);
    SourceQuat tiny;
    tiny.w = std::cos(half) * 1e-23f;
    tiny.y = std::sin(half) * 1e-23f;

    const pxr::GfQuatf converted = ConvertRotation(basis, tiny);
    assert(std::abs(converted.GetLength() - 1.0f) <= kTolerance.angle);
    assert(NearRotation(converted, AboutY(45.0f)));

    // And it survives the layer, rather than only the arithmetic: a stated rest
    // rotation is the route by which a source-supplied `SourceQuat` reaches
    // `ConvertRotation`, and the composition below it multiplies whatever came
    // back.
    SourceSkeleton skeleton = BaseSkeleton();
    skeleton.joints[1].restRotation = tiny;
    SourceProfile profile = BaseProfile();
    profile.restPose = RestPoseSource::StatedRestRotations;
    const SourceConversion result =
        ConvertSourceToCanonical(skeleton, BaseAnimation(), profile);
    assert(result.Converted());
    const auto spine = static_cast<std::size_t>(motion::HumanBone::Spine);
    assert(NearRotation(result.rest.localRotations[spine], AboutY(45.0f)));
    assert(std::abs(result.rest.localRotations[spine].GetLength() - 1.0f)
           <= kTolerance.angle);
}

// --- angle composition -----------------------------------------------------

void
TestComposeUsesTheDeclaredOrder()
{
    // The same three numbers under two orders are two different rotations, and
    // a converter that sorted its channels would make them one.
    const SourceEulerAngles angles = Angles(30.0f, 40.0f, 50.0f);
    const CanonicalBasis basis = *MakeCanonicalBasis(BaseProfile());
    const pxr::GfQuatf zxy = ConvertRotation(
        basis, ComposeSourceRotation(angles, SourceEulerOrder::ZXY,
                                     SourceAngleUnit::Degrees));
    const pxr::GfQuatf xyz = ConvertRotation(
        basis, ComposeSourceRotation(angles, SourceEulerOrder::XYZ,
                                     SourceAngleUnit::Degrees));
    assert(!NearRotation(zxy, xyz));
}

// The composition applies the *last* declared angle first, which is the
// intrinsic reading of a named order. Checked with two axis-aligned quarter
// turns whose two orderings land a direction in different places -- the
// opposite reading agrees with this one everywhere only one axis moves, so a
// single-axis case would pass under both.
void
TestCompositionOrderIsLastFirst()
{
    const CanonicalBasis basis = *MakeCanonicalBasis(BaseProfile());
    // Order XYZ: first is the X angle, third is the Z angle, so the rotation is
    // Rx * Rz and the Z turn happens first.
    const pxr::GfQuatf composed = ConvertRotation(
        basis, ComposeSourceRotation(Angles(90.0f, 0.0f, 90.0f),
                                     SourceEulerOrder::XYZ,
                                     SourceAngleUnit::Degrees));
    // Rz(90) takes +X to +Y; Rx(90) then takes +Y to +Z.
    assert(NearVector(composed.Transform(pxr::GfVec3f(1.0f, 0.0f, 0.0f)),
                      pxr::GfVec3f(0.0f, 0.0f, 1.0f)));
}

void
TestAngleUnitIsTheTracksAnswer()
{
    const CanonicalBasis basis = *MakeCanonicalBasis(BaseProfile());
    const pxr::GfQuatf degrees = ConvertRotation(
        basis, ComposeSourceRotation(Angles(0.0f, 0.0f, 90.0f),
                                     SourceEulerOrder::ZXY,
                                     SourceAngleUnit::Degrees));
    const pxr::GfQuatf radians = ConvertRotation(
        basis, ComposeSourceRotation(
                   Angles(0.0f, 0.0f,
                          static_cast<float>(3.14159265358979323846 / 2.0)),
                   SourceEulerOrder::ZXY, SourceAngleUnit::Radians));
    assert(NearRotation(degrees, radians));
}

// --- the conversion --------------------------------------------------------

void
TestRestPoseFromOffsets()
{
    const SourceConversion result = ConvertSourceToCanonical(
        BaseSkeleton(), BaseAnimation(), BaseProfile());
    assert(result.Converted());

    const auto hips = static_cast<std::size_t>(motion::HumanBone::Hips);
    const auto spine = static_cast<std::size_t>(motion::HumanBone::Spine);
    const auto head = static_cast<std::size_t>(motion::HumanBone::Head);
    assert(result.rest.present.test(hips));
    assert(result.rest.present.test(spine));
    assert(result.rest.present.test(head));
    assert(result.rest.present.count() == 3);

    // `rest-offsets` states no rest rotation, so every canonical rest rotation
    // is identity -- and that is a reading of the file rather than a default.
    assert(NearRotation(result.rest.localRotations[spine],
                        pxr::GfQuatf(1.0f, pxr::GfVec3f(0.0f))));

    // The hips sit at the root's own offset, in metres.
    assert(NearVector(result.rest.localTranslations[hips],
                      pxr::GfVec3f(0.0f, 0.9f, 0.0f)));
    // The spine is two source joints below it, and the segment nothing maps is
    // *on the path*: 10 + 10 centimetres.
    assert(NearVector(result.rest.localTranslations[spine],
                      pxr::GfVec3f(0.0f, 0.2f, 0.0f)));
    assert(NearVector(result.rest.localTranslations[head],
                      pxr::GfVec3f(0.0f, 0.3f, 0.0f)));

    // Which bone absorbed a chain is reported, because a cross-source
    // comparison will want to know.
    assert(result.report.composedBones.size() == 1);
    assert(result.report.composedBones[0] == motion::HumanBone::Spine);
}

// `stated-rest-rotations`: the source states a rest orientation per joint, and
// the rest pose composes those along the same paths. This is also the only route
// by which a source-supplied `SourceQuat` reaches `ConvertRotation`, so it is
// where a non-unit quaternion has to be normalised rather than carried.
void
TestRestPoseFromStatedRotations()
{
    SourceSkeleton skeleton = BaseSkeleton();
    // Deliberately *not* unit length: a source that wrote 0.9-scaled components
    // wrote them, and `SourceQuat` keeps them (SourceSkeleton.h). Canonical
    // motion is unit quaternions, and this layer is the last one able to say so.
    const float half = static_cast<float>(3.14159265358979323846 / 8.0);
    SourceQuat stated;
    stated.w = std::cos(half) * 2.0f;
    stated.y = std::sin(half) * 2.0f;
    // The segment nothing maps carries the rest rotation; the joint below it is
    // straight. So the composed rest of `spine` is the segment's, which is what
    // a walk that skipped unmapped joints would lose.
    skeleton.joints[1].restRotation = stated;

    SourceProfile profile = BaseProfile();
    profile.restPose = RestPoseSource::StatedRestRotations;
    const SourceConversion result =
        ConvertSourceToCanonical(skeleton, BaseAnimation(), profile);
    assert(result.Converted());

    const auto spine = static_cast<std::size_t>(motion::HumanBone::Spine);
    assert(NearRotation(result.rest.localRotations[spine], AboutY(45.0f)));
    // Normalised on the way in: the double-length quaternion above describes a
    // 45-degree turn and nothing else, and `GetLength` says so.
    assert(std::abs(result.rest.localRotations[spine].GetLength() - 1.0f)
           <= kTolerance.angle);
    // The hips are above it and unaffected; the head is below a bound joint that
    // states nothing.
    const auto hips = static_cast<std::size_t>(motion::HumanBone::Hips);
    assert(NearRotation(result.rest.localRotations[hips],
                        pxr::GfQuatf(1.0f, pxr::GfVec3f(0.0f))));

    // The same rig read as `rest-offsets` states no rest rotation at all, which
    // is the difference between "the source has none" and "the source says
    // identity" that `SourceJoint::restRotation` exists to keep.
    const SourceConversion offsets =
        ConvertSourceToCanonical(skeleton, BaseAnimation(), BaseProfile());
    assert(offsets.Converted());
    assert(NearRotation(offsets.rest.localRotations[spine],
                        pxr::GfQuatf(1.0f, pxr::GfVec3f(0.0f))));
}

// `first-frame`: the writer's first sample is the rest pose. The clip still
// carries absolute local rotations -- it is the *rest* that moves, and the
// retargeter is what subtracts one from the other.
void
TestRestPoseFromFirstFrame()
{
    SourceAnimation animation = BaseAnimation();
    animation.tracks[2] = RotationTrack({Angles(0.0f, 0.0f, 30.0f),
                                         Angles(0.0f, 0.0f, 80.0f)});
    SourceProfile profile = BaseProfile();
    profile.restPose = RestPoseSource::FirstFrame;
    const SourceConversion result =
        ConvertSourceToCanonical(BaseSkeleton(), animation, profile);
    assert(result.Converted());

    const auto spine = static_cast<std::size_t>(motion::HumanBone::Spine);
    // The rest is frame 0's rotation.
    assert(NearRotation(result.rest.localRotations[spine], AboutY(30.0f)));
    // And the samples are unchanged by that: frame 0 still reports 30, not the
    // zero a rest-relative clip would carry.
    assert(NearRotation(result.animation.samples[0].localRotations[spine],
                        AboutY(30.0f)));
    assert(NearRotation(result.animation.samples[1].localRotations[spine],
                        AboutY(80.0f)));

    // An empty clip has no first frame to read, and the rest falls back to the
    // offsets rather than to whatever an out-of-range read would return.
    SourceAnimation empty = BaseAnimation();
    empty.frameCount = 0;
    for (SourceJointTrack& track : empty.tracks) {
        track.translations.clear();
        track.eulerAngles.clear();
    }
    empty.frameTime = 0.0;
    const SourceConversion none =
        ConvertSourceToCanonical(BaseSkeleton(), empty, profile);
    assert(none.Converted());
    assert(none.animation.samples.empty());
    assert(NearRotation(none.rest.localRotations[spine],
                        pxr::GfQuatf(1.0f, pxr::GfVec3f(0.0f))));
}

// An absent bone is not an identity sample (MOTION_CONTRACT.md). A bone whose
// whole path states no rotation gets no presence bit, rather than an identity
// nobody wrote.
void
TestABoneNothingRotatesIsNotValid()
{
    SourceAnimation animation = BaseAnimation();
    // Both joints on the spine's path go silent; the rig still carries them.
    animation.tracks[1] = SourceJointTrack();
    animation.tracks[2] = SourceJointTrack();
    const SourceConversion result =
        ConvertSourceToCanonical(BaseSkeleton(), animation, BaseProfile());
    assert(result.Converted());

    const auto spine = static_cast<std::size_t>(motion::HumanBone::Spine);
    const auto head = static_cast<std::size_t>(motion::HumanBone::Head);
    assert(!result.animation.samples[0].validRotations.test(spine));
    // The head is below a silent joint but states its own rotation, so it is
    // reported.
    assert(result.animation.samples[0].validRotations.test(head));
    // The rest pose still carries the bone: the rig has it, and what the clip
    // declines to claim is only how it turned.
    assert(result.rest.present.test(spine));
}

// The rule roadmap §10 wrote down: a joint between two mapped ones is on the
// path between them, and its rotation belongs to the lower bone.
void
TestUnmappedJointRotationIsComposedNotDropped()
{
    SourceAnimation animation = BaseAnimation();
    // The segment turns thirty degrees about the up axis; the joint below it
    // turns sixty. Nothing else moves.
    animation.tracks[1] = RotationTrack({Angles(0.0f, 0.0f, 30.0f),
                                         Angles(0.0f, 0.0f, 30.0f)});
    animation.tracks[2] = RotationTrack({Angles(0.0f, 0.0f, 60.0f),
                                         Angles(0.0f, 0.0f, 60.0f)});
    const SourceConversion result =
        ConvertSourceToCanonical(BaseSkeleton(), animation, BaseProfile());
    assert(result.Converted());

    const auto spine = static_cast<std::size_t>(motion::HumanBone::Spine);
    // Ninety, not sixty: the segment's thirty degrees would otherwise be lost
    // and everything below it would sit thirty degrees wrong -- a subtly
    // misassembled body rather than a failure.
    assert(NearRotation(result.animation.samples[0].localRotations[spine],
                        AboutY(90.0f)));

    // The head is directly under a bound joint, so nothing is composed into it.
    const auto head = static_cast<std::size_t>(motion::HumanBone::Head);
    assert(NearRotation(result.animation.samples[0].localRotations[head],
                        pxr::GfQuatf(1.0f, pxr::GfVec3f(0.0f))));
}

void
TestRootTranslationPolicies()
{
    SourceAnimation animation = BaseAnimation();
    animation.tracks[0].translations = {Vec(0.0f, 90.0f, 0.0f),
                                        Vec(5.0f, 95.0f, 0.0f)};

    {
        // Absolute: the sample is the position, and 95 centimetres of height is
        // 0.95 metres of it.
        const SourceConversion result =
            ConvertSourceToCanonical(BaseSkeleton(), animation, BaseProfile());
        assert(result.Converted());
        assert(result.animation.samples[1].root.hasPosition);
        assert(NearVector(result.animation.samples[1].root.worldPosition,
                          pxr::GfVec3f(0.05f, 0.95f, 0.0f)));
    }
    {
        // Rest-relative: the same numbers mean a displacement from the root's
        // rest, so they land 0.9 metres higher. Reading one as the other is the
        // failure that made these two separate words.
        SourceProfile profile = BaseProfile();
        profile.rootTranslation = RootTranslationPolicy::RestRelative;
        const SourceConversion result =
            ConvertSourceToCanonical(BaseSkeleton(), animation, profile);
        assert(result.Converted());
        assert(NearVector(result.animation.samples[1].root.worldPosition,
                          pxr::GfVec3f(0.05f, 1.85f, 0.0f)));
    }
    {
        // None: the channel carries no motion and is dropped rather than
        // converted, so the sample reports no position at all.
        SourceProfile profile = BaseProfile();
        profile.rootTranslation = RootTranslationPolicy::None;
        const SourceConversion result =
            ConvertSourceToCanonical(BaseSkeleton(), animation, profile);
        assert(result.Converted());
        assert(!result.animation.samples[1].root.hasPosition);
        // And it is reported as motion the clip does not carry, because these
        // samples do vary.
        assert(result.report.droppedTranslationJoints.size() == 1);
        assert(result.report.droppedTranslationJoints[0] == 0);
    }
}

void
TestRootRotationPolicies()
{
    SourceAnimation animation = BaseAnimation();
    animation.tracks[0] = RotationTrack({Angles(0.0f, 0.0f, 45.0f),
                                         Angles(0.0f, 0.0f, 45.0f)});
    animation.tracks[0].translations = {Vec(0.0f, 90.0f, 0.0f),
                                        Vec(0.0f, 90.0f, 0.0f)};
    const auto hips = static_cast<std::size_t>(motion::HumanBone::Hips);
    {
        const SourceConversion result =
            ConvertSourceToCanonical(BaseSkeleton(), animation, BaseProfile());
        assert(result.Converted());
        assert(result.animation.samples[0].root.hasOrientation);
        assert(NearRotation(result.animation.samples[0].root.worldOrientation,
                            AboutY(45.0f)));
        assert(NearRotation(result.animation.samples[0].localRotations[hips],
                            AboutY(45.0f)));
    }
    {
        // A root whose rotation says nothing about the body: dropped from the
        // sample *and* from the composition, so the bone bound to it does not
        // quietly receive it either.
        SourceProfile profile = BaseProfile();
        profile.rootRotation = RootRotationPolicy::None;
        const SourceConversion result =
            ConvertSourceToCanonical(BaseSkeleton(), animation, profile);
        assert(result.Converted());
        assert(!result.animation.samples[0].root.hasOrientation);
        assert(NearRotation(result.animation.samples[0].localRotations[hips],
                            pxr::GfQuatf(1.0f, pxr::GfVec3f(0.0f))));
        // And the hips carry no presence bit either. The profile said this
        // root's rotation is not the body's; the only joint on the hips' path
        // *is* that root, so the clip states nothing about how the hips turned
        // rather than stating that they did not.
        assert(!result.animation.samples[0].validRotations.test(hips));
    }
}

// Where the body is and which way it faces is asked of the path down to the
// hips, not of the rig's root joint (MOTION_CONTRACT.md). Every other test in
// this file uses a rig whose root *is* its hips, which cannot tell the two
// readings apart -- that is why this one exists, and why the rule could be
// written only once a second producer had been read.
void
TestRootPlacementComposesThePathToTheHips()
{
    {
        const SourceConversion result = ConvertSourceToCanonical(
            SplitRootSkeleton(), SplitRootAnimation(), SplitRootProfile());
        assert(result.Converted());

        // 20 centimetres of Z from the reference node and the hips' own
        // (10, 92, 0) on top of it. Reading the reference node alone gives
        // (0, 0, 0.20) and reading the hips alone gives (0.10, 0.92, 0):
        // each is half a placement, and each looks like a plausible clip.
        assert(result.animation.samples[1].root.hasPosition);
        assert(NearVector(result.animation.samples[1].root.worldPosition,
                          pxr::GfVec3f(0.10f, 0.92f, 0.20f)));

        // The orientation is on the hips, and the reference node never turns.
        // Read from joint 0 this is identity -- a body that walks without ever
        // facing anywhere, which is motion that looks merely odd rather than
        // broken.
        assert(result.animation.samples[1].root.hasOrientation);
        assert(NearRotation(result.animation.samples[1].root.worldOrientation,
                            AboutY(45.0f)));
    }
    {
        // A composition, not a sum. Turning the reference node halfway round
        // must turn the offset the hips state underneath it; 180 degrees is
        // chosen because it negates X and Z whichever way the basis reads.
        SourceAnimation animation = SplitRootAnimation();
        animation.tracks[0] = RotationTrack({Angles(0.0f, 0.0f, 180.0f),
                                             Angles(0.0f, 0.0f, 180.0f)});
        animation.tracks[0].translations = {Vec(0.0f, 0.0f, 0.0f),
                                            Vec(0.0f, 0.0f, 20.0f)};
        const SourceConversion result = ConvertSourceToCanonical(
            SplitRootSkeleton(), animation, SplitRootProfile());
        assert(result.Converted());
        assert(NearVector(result.animation.samples[1].root.worldPosition,
                          pxr::GfVec3f(-0.10f, 0.92f, 0.20f)));
    }
}

// A rest taken from the first frame is taken from the first frame entirely.
// Rotations from frame 0 and translations from the offsets is one rest built
// out of two poses, and it is wrong for precisely the export that needs the
// setting: one whose offsets are not a pose at all.
void
TestRestFromFirstFrameTakesTranslationsToo()
{
    const auto hips = static_cast<std::size_t>(motion::HumanBone::Hips);
    {
        const SourceConversion result = ConvertSourceToCanonical(
            SplitRootSkeleton(), SplitRootAnimation(), SplitRootProfile());
        assert(result.Converted());
        // `rest-offsets` says the offsets are the rest and is taken at its
        // word, artefact and all: four metres of Z that no sample in the clip
        // goes anywhere near. A profile stating this of such a rig is wrong,
        // and the converter is not the layer that gets to decide so.
        assert(NearVector(result.rest.localTranslations[hips],
                          pxr::GfVec3f(0.0f, 0.90f, -4.0f)));
    }
    {
        SourceProfile profile = SplitRootProfile();
        profile.restPose = RestPoseSource::FirstFrame;
        const SourceConversion result = ConvertSourceToCanonical(
            SplitRootSkeleton(), SplitRootAnimation(), profile);
        assert(result.Converted());
        // Frame 0's translations. The four metres are gone, and they would
        // otherwise have reached `vrmRetarget` as a rest for it to subtract
        // from every frame of the clip.
        assert(NearVector(result.rest.localTranslations[hips],
                          pxr::GfVec3f(0.10f, 0.92f, 0.0f)));
    }
}

// Every joint on the root path is carried, so none of them is reported as
// motion the clip could not take. Before the path rule the hips' translation
// landed in `droppedTranslationJoints` -- which was the honest half of getting
// that export wrong, and is the wrong answer now that it is carried.
void
TestRootPathTranslationIsCarriedNotDropped()
{
    {
        const SourceConversion result = ConvertSourceToCanonical(
            SplitRootSkeleton(), SplitRootAnimation(), SplitRootProfile());
        assert(result.Converted());
        assert(result.report.droppedTranslationJoints.empty());
        assert(result.report.restatedTranslationJoints.empty());
    }
    {
        SourceProfile profile = SplitRootProfile();
        profile.rootTranslation = RootTranslationPolicy::None;
        const SourceConversion result = ConvertSourceToCanonical(
            SplitRootSkeleton(), SplitRootAnimation(), profile);
        assert(result.Converted());
        assert(!result.animation.samples[1].root.hasPosition);
        // Both of them, because the policy drops the whole path's answer and
        // both joints on it state translations that vary.
        assert(result.report.droppedTranslationJoints.size() == 2);
    }
}

// A path whose joints all state rest geometry and no translation channel says
// nothing about where the body is. Reporting a position anyway would claim the
// rig sat at its own offsets rather than admit the source never said.
//
// Unlike the three above, this one passes on the converter as it stood: it
// pins behaviour the path rule had to *keep*, where those pin behaviour it
// changed. Said here so nobody reads a green run as evidence of the change.
void
TestARootPathStatingNoTranslationHasNoPlacement()
{
    SourceAnimation animation = SplitRootAnimation();
    animation.tracks[0].translations.clear();
    animation.tracks[1].translations.clear();
    const SourceConversion result = ConvertSourceToCanonical(
        SplitRootSkeleton(), animation, SplitRootProfile());
    assert(result.Converted());
    assert(!result.animation.samples[0].root.hasPosition);
    // Orientation is the other question and the path still answers it.
    assert(result.animation.samples[0].root.hasOrientation);
}

// A rig whose offsets all run down their own bones, which is what an export
// retargeted onto a character rig looks like: composed at identity it is not a
// figure at all, so `rest-offsets` describes nothing and `t-pose` is what says
// what its neutral is.
//
// `reference` -> `root`(hips) -> `back`(spine) -> `crown`(head), and an arm off
// `back`: `wing`(leftShoulder) -> `limb`(leftUpperArm) -> `tip`(leftHand). Every
// offset is +X in its own parent's frame, and the arm is what makes the test
// mean something -- a spine and an arm that agree in the source and must
// disagree by ninety degrees in the rest.
SourceSkeleton
BoneLocalSkeleton()
{
    SourceSkeleton skeleton;
    const auto joint = [](const char* name, int parent, float length) {
        SourceJoint out;
        out.name = name;
        out.parent = parent;
        out.restTranslation = Vec(length, 0.0f, 0.0f);
        return out;
    };
    SourceJoint reference = joint("reference", -1, 0.0f);
    reference.restTranslation = Vec(0.0f, 90.0f, 0.0f);
    skeleton.joints = {reference,
                       joint("root", 0, 0.0f),
                       joint("back", 1, 20.0f),
                       joint("crown", 2, 30.0f),
                       joint("wing", 2, 10.0f),
                       joint("limb", 4, 5.0f),
                       joint("tip", 5, 25.0f)};
    return skeleton;
}

SourceProfile
BoneLocalProfile()
{
    SourceProfile profile = BaseProfile();
    profile.rootJoint = "reference";
    profile.restPose = RestPoseSource::TPose;
    profile.joints = {
        SourceJointMapping{"root", motion::HumanBone::Hips, true},
        SourceJointMapping{"back", motion::HumanBone::Spine, true},
        SourceJointMapping{"crown", motion::HumanBone::Head, true},
        SourceJointMapping{"wing", motion::HumanBone::LeftShoulder, true},
        SourceJointMapping{"limb", motion::HumanBone::LeftUpperArm, true},
        SourceJointMapping{"tip", motion::HumanBone::LeftHand, true},
    };
    profile.ignoredJoints = {"reference"};
    return profile;
}

// The same bone-local rig with no reference node above it: the hips *are* joint
// 0. That is the only shape where the T-pose aim lands on the root itself, and
// therefore the only one where aiming it and dropping its rotation can disagree.
SourceSkeleton
HipsRootedSkeleton()
{
    SourceSkeleton skeleton = BoneLocalSkeleton();
    skeleton.joints.erase(skeleton.joints.begin());
    skeleton.joints[0].parent = -1;
    skeleton.joints[0].restTranslation = Vec(0.0f, 90.0f, 0.0f);
    for (std::size_t index = 1; index < skeleton.joints.size(); ++index) {
        skeleton.joints[index].parent -= 1;
    }
    return skeleton;
}

SourceProfile
HipsRootedProfile()
{
    SourceProfile profile = BoneLocalProfile();
    profile.rootJoint = "root";
    profile.ignoredJoints = {};
    return profile;
}

SourceAnimation
HipsRootedAnimation()
{
    SourceAnimation animation;
    animation.frameCount = 2;
    animation.frameTime = 0.5;
    animation.provenance.format = "example";
    animation.provenance.sourceId = "capture.example";
    animation.tracks.assign(
        6, RotationTrack({Angles(0.0f, 0.0f, 0.0f), Angles(0.0f, 0.0f, 0.0f)}));
    animation.tracks[0].translations = {Vec(0.0f, 90.0f, 0.0f),
                                        Vec(0.0f, 90.0f, 0.0f)};
    return animation;
}

SourceAnimation
BoneLocalAnimation()
{
    SourceAnimation animation;
    animation.frameCount = 2;
    animation.frameTime = 0.5;
    animation.provenance.format = "example";
    animation.provenance.sourceId = "capture.example";
    animation.tracks.assign(
        7, RotationTrack({Angles(0.0f, 0.0f, 0.0f), Angles(0.0f, 0.0f, 0.0f)}));
    animation.tracks[0].translations = {Vec(0.0f, 90.0f, 0.0f),
                                        Vec(0.0f, 90.0f, 0.0f)};
    return animation;
}

// The world rest of a bone, walked up the canonical humanoid. `CanonicalRestPose`
// states each bone from its nearest *present* ancestor, so this is the same walk
// its consumers make and not a shortcut past one.
std::pair<pxr::GfQuatf, pxr::GfVec3f>
WorldRest(const motionSource::CanonicalRestPose& rest, motion::HumanBone bone)
{
    std::vector<motion::HumanBone> chain;
    for (std::optional<motion::HumanBone> at = bone; at;
         at = motion::NearestPresentAncestor(*at, rest.present)) {
        chain.push_back(*at);
    }
    pxr::GfQuatf rotation(1.0f, pxr::GfVec3f(0.0f));
    pxr::GfVec3f position(0.0f);
    for (auto step = chain.rbegin(); step != chain.rend(); ++step) {
        const auto slot = static_cast<std::size_t>(*step);
        position += rotation.Transform(rest.localTranslations[slot]);
        rotation = rotation * rest.localRotations[slot];
    }
    return {rotation, position};
}

// `t-pose` is a claim about a rig, and this is the claim: the spine goes up and
// the arm goes out, from offsets that all point the same way.
void
TestTPoseRestStandsTheRigUp()
{
    const SourceConversion result = ConvertSourceToCanonical(
        BoneLocalSkeleton(), BoneLocalAnimation(), BoneLocalProfile());
    assert(result.Converted());

    const pxr::GfVec3f hips =
        WorldRest(result.rest, motion::HumanBone::Hips).second;
    const pxr::GfVec3f spine =
        WorldRest(result.rest, motion::HumanBone::Spine).second;
    const pxr::GfVec3f head =
        WorldRest(result.rest, motion::HumanBone::Head).second;
    const pxr::GfVec3f hand =
        WorldRest(result.rest, motion::HumanBone::LeftHand).second;

    // Centimetres in, metres out. The spine sits its own offset above the hips
    // and the head above that: 0.20 and 0.30 of the rig's own bone lengths,
    // stacked up the canonical up axis rather than along the source's +X.
    assert(NearVector(hips, pxr::GfVec3f(0.0f, 0.90f, 0.0f)));
    assert(NearVector(spine, pxr::GfVec3f(0.0f, 1.10f, 0.0f)));
    assert(NearVector(head, pxr::GfVec3f(0.0f, 1.40f, 0.0f)));

    // The arm leaves sideways, which is the half of the claim the spine cannot
    // make. Its shoulder is stated 0.10 along the *spine's* own direction, so
    // it sits above the spine at 1.20 and the arm goes out from there: 0.05 to
    // the upper arm and 0.25 to the hand, both along the lateral axis. Under
    // `rest-offsets` the whole rig instead lies along +X and the hand lands at
    // (0.60, 0.90, 0), out of the hips and level with the head -- the figure
    // that is not one.
    assert(NearVector(hand, pxr::GfVec3f(0.30f, 1.20f, 0.0f)));

    // And the same rig read the other way, so the claim is checked rather than
    // described: `rest-offsets` takes the offsets at their word, every joint
    // stacks along +X, and the whole body lies down one axis. A conversion that
    // produced this and a retarget that trusted it would place an avatar's
    // every bone by a rest that is not a pose.
    SourceProfile offsets = BoneLocalProfile();
    offsets.restPose = RestPoseSource::RestOffsets;
    const SourceConversion flat = ConvertSourceToCanonical(
        BoneLocalSkeleton(), BoneLocalAnimation(), offsets);
    assert(flat.Converted());
    assert(NearVector(WorldRest(flat.rest, motion::HumanBone::Head).second,
                      pxr::GfVec3f(0.50f, 0.90f, 0.0f)));
    assert(NearVector(WorldRest(flat.rest, motion::HumanBone::LeftHand).second,
                      pxr::GfVec3f(0.60f, 0.90f, 0.0f)));
}

// `t-pose` and a dropped root rotation meet in one place, and only on a rig
// whose root carries a bone: the aim lands on the root itself, and dropping the
// root's rotation *after* the walk that stated every descendant relative to it
// leaves each of them turned by an aim that no longer exists. The body then
// lies down an axis while the profile reads as perfectly ordinary.
//
// Nothing forbids the pair, so it is dropped inside the walk instead. What that
// costs is honest and is not hidden: a root the profile has silenced cannot aim
// anything, so the first bone below it goes where the identity frame sends it
// and the rig is not stood up. That is the profile's statement being obeyed --
// a root whose rotation says nothing about the body cannot be the joint that
// orients it.
void
TestTPoseRestObeysADroppedRootRotation()
{
    SourceProfile profile = HipsRootedProfile();
    profile.rootRotation = RootRotationPolicy::None;
    const SourceConversion result = ConvertSourceToCanonical(
        HipsRootedSkeleton(), HipsRootedAnimation(), profile);
    assert(result.Converted());

    // The spine leaves the silenced hips along the rig's own +X and only then
    // is the chain aimed: 0.20 out, 0.30 up. Built the other way round -- the
    // root aimed and then erased -- the head lands at (0.50, 0.90, 0), flat
    // along the axis the offsets happen to use.
    assert(NearVector(WorldRest(result.rest, motion::HumanBone::Head).second,
                      pxr::GfVec3f(0.20f, 1.20f, 0.0f)));

    // And with the rotation kept, the same rig stands all the way up, which is
    // what says the difference above is the policy and not the construction.
    const SourceConversion kept = ConvertSourceToCanonical(
        HipsRootedSkeleton(), HipsRootedAnimation(), HipsRootedProfile());
    assert(kept.Converted());
    assert(NearVector(WorldRest(kept.rest, motion::HumanBone::Head).second,
                      pxr::GfVec3f(0.0f, 1.40f, 0.0f)));
}

// The rig's own lengths, never a canonical skeleton's. A T-pose says which way
// the bones point and has no opinion about how long they are, and a rest that
// quietly normalised them would put every producer's clip on one body.
void
TestTPoseRestKeepsTheRigsOwnProportions()
{
    SourceSkeleton skeleton = BoneLocalSkeleton();
    skeleton.joints[3].restTranslation = Vec(45.0f, 0.0f, 0.0f); // a long neck
    const SourceConversion result = ConvertSourceToCanonical(
        skeleton, BoneLocalAnimation(), BoneLocalProfile());
    assert(result.Converted());
    assert(NearVector(WorldRest(result.rest, motion::HumanBone::Head).second,
                      pxr::GfVec3f(0.0f, 1.55f, 0.0f)));
}

// A rig that restates its rest geometry every frame loses nothing; one whose
// joint actually translates loses motion. Reporting both with one word would
// hide the second inside the first.
void
TestTranslationReportSeparatesLossFromNoise()
{
    SourceAnimation animation = BaseAnimation();
    // `back` restates its own offset; `crown` actually moves.
    animation.tracks[2].translations = {Vec(0.0f, 10.0f, 0.0f),
                                        Vec(0.0f, 10.0f, 0.0f)};
    animation.tracks[3].translations = {Vec(0.0f, 30.0f, 0.0f),
                                        Vec(1.0f, 30.0f, 0.0f)};
    const SourceConversion result =
        ConvertSourceToCanonical(BaseSkeleton(), animation, BaseProfile());
    assert(result.Converted());
    assert(result.report.restatedTranslationJoints.size() == 1);
    assert(result.report.restatedTranslationJoints[0] == 2);
    assert(result.report.droppedTranslationJoints.size() == 1);
    assert(result.report.droppedTranslationJoints[0] == 3);
    // The root's own translation is carried rather than dropped, so it is in
    // neither list.
}

void
TestTiming()
{
    const SourceConversion result = ConvertSourceToCanonical(
        BaseSkeleton(), BaseAnimation(), BaseProfile());
    assert(result.Converted());
    assert(result.animation.samples.size() == 2);
    assert(std::abs(result.animation.samples[0].timestamp - 0.0)
           <= kTolerance.time);
    assert(std::abs(result.animation.samples[1].timestamp - 0.5)
           <= kTolerance.time);
    assert(std::abs(result.animation.startTime - 0.0) <= kTolerance.time);
    // One sample is an instant: the span is between the first and last, not
    // frameCount * frameTime.
    assert(std::abs(result.animation.endTime - 0.5) <= kTolerance.time);
    assert(std::abs(result.animation.nominalFrameRate - 2.0) < 1e-9);
}

void
TestProvenance()
{
    const SourceConversion result = ConvertSourceToCanonical(
        BaseSkeleton(), BaseAnimation(), BaseProfile());
    assert(result.Converted());
    // The two answers only this layer holds: a reader states neither.
    assert(result.provenance.producer == "Example Producer");
    assert(result.provenance.profileId == "example-source-default-v1");
    // And the reader's, carried through untouched.
    assert(result.provenance.format == "example");
    assert(result.provenance.sourceId == "capture.example");

    // The narrowing: producer -> provider, format -> protocol, always a clip,
    // and the producer version and profile id dropped.
    assert(result.animation.source.kind == motion::MotionSourceKind::Clip);
    assert(result.animation.source.provider == "Example Producer");
    assert(result.animation.source.protocol == "example");
    assert(result.animation.source.sourceId == "capture.example");
    // Not repeated per sample: it cannot vary within a clip.
    assert(!result.animation.samples[0].source.has_value());
}

void
TestDeterminism()
{
    const SourceSkeleton skeleton = BaseSkeleton();
    const SourceAnimation animation = BaseAnimation();
    const SourceProfile profile = BaseProfile();
    const SourceConversion first =
        ConvertSourceToCanonical(skeleton, animation, profile);
    const SourceConversion second =
        ConvertSourceToCanonical(skeleton, animation, profile);
    assert(first.Converted() && second.Converted());
    // `operator==` and not `NearlyEqual`: two runs over one input are the same
    // recorded values or they are a defect, not a tolerance question.
    assert(first.animation == second.animation);
    assert(first.report.composedBones == second.report.composedBones);
    assert(first.provenance == second.provenance);
}

// --- refusals --------------------------------------------------------------

void
TestProfileMismatchIsRefused()
{
    SourceSkeleton skeleton = BaseSkeleton();
    // The profile requires `crown`; this rig calls it something else.
    skeleton.joints[3].name = "top";
    const SourceConversion result =
        ConvertSourceToCanonical(skeleton, BaseAnimation(), BaseProfile());
    assert(!result.Converted());
    assert(result.refusal == ConversionRefusal::ProfileMismatch);
    assert(!result.detail.empty());
    // Filled whatever the refusal, because the caller that most needs it is one
    // reporting on a profile that did not match.
    assert(!result.match.bound.empty());
    assert(result.animation.samples.empty());
}

void
TestInvalidAnimationIsRefused()
{
    SourceAnimation animation = BaseAnimation();
    // One track short: an animation checked alone would pass while describing a
    // different rig.
    animation.tracks.pop_back();
    const SourceConversion result =
        ConvertSourceToCanonical(BaseSkeleton(), animation, BaseProfile());
    assert(!result.Converted());
    assert(result.refusal == ConversionRefusal::AnimationInvalid);
    assert(!result.detail.empty());
}

void
TestQuaternionTrackIsRefusedWithAReason()
{
    SourceAnimation animation = BaseAnimation();
    SourceJointTrack quaternions;
    quaternions.rotations = {SourceQuat(), SourceQuat()};
    animation.tracks[2] = quaternions;
    const SourceConversion result =
        ConvertSourceToCanonical(BaseSkeleton(), animation, BaseProfile());
    assert(!result.Converted());
    assert(result.refusal == ConversionRefusal::UnsupportedRotationForm);
    // The reason names the joint and says why, rather than reporting a form
    // nobody can act on: no reader writes this yet, so converting it would mean
    // testing a path against a value this repository invented.
    assert(result.detail.find("back") != std::string::npos);
}

void
TestRefusalNamesAreComplete()
{
    for (std::size_t index = 0;
         index < motionSource::ConversionRefusalCount; ++index) {
        assert(!motionSource::ConversionRefusalName(
                    static_cast<ConversionRefusal>(index))
                    .empty());
    }
}

} // namespace

int
main()
{
    TestBasisNeedsAStatedProfile();
    TestBasisOfAnAgreeingSource();
    TestBasisOfAZUpSource();
    TestBasisOfABackwardFacingSource();
    TestBasisOfAnUpsideDownSource();
    TestBasisOfALeftHandedSource();
    TestRightHandedRotationKeepsItsAngle();
    TestARotationTooSmallToSquareIsStillNormalised();
    TestComposeUsesTheDeclaredOrder();
    TestCompositionOrderIsLastFirst();
    TestAngleUnitIsTheTracksAnswer();
    TestRestPoseFromOffsets();
    TestRestPoseFromStatedRotations();
    TestRestPoseFromFirstFrame();
    TestABoneNothingRotatesIsNotValid();
    TestUnmappedJointRotationIsComposedNotDropped();
    TestRootTranslationPolicies();
    TestRootRotationPolicies();
    TestRootPlacementComposesThePathToTheHips();
    TestRestFromFirstFrameTakesTranslationsToo();
    TestRootPathTranslationIsCarriedNotDropped();
    TestARootPathStatingNoTranslationHasNoPlacement();
    TestTPoseRestStandsTheRigUp();
    TestTPoseRestObeysADroppedRootRotation();
    TestTPoseRestKeepsTheRigsOwnProportions();
    TestTranslationReportSeparatesLossFromNoise();
    TestTiming();
    TestProvenance();
    TestDeterminism();
    TestProfileMismatchIsRefused();
    TestInvalidAnimationIsRefused();
    TestQuaternionTrackIsRefusedWithAReason();
    TestRefusalNamesAreComplete();
    std::printf("motionSource conversion: verified\n");
    return 0;
}
