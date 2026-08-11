// SPDX-License-Identifier: Apache-2.0
//
// Joint ids into canonical humanoid semantics, and the device's basis into the
// canonical one.
//
// Two things are checked here that no test below this layer could be:
//
// **Sides.** The corpus deliberately describes its bones by id, because the
// decoder names none — so `arms-lowered-60hz` says "bones 12 and 16 turn in
// opposite directions" and stops. This is the layer that says which of them is
// the left arm, so this is where the fixture is finally asked the question it
// was written for, and the assertion is *physical*: the left forearm's rest
// direction is rotated by the sample and the answer compared against where a
// lowered arm has to be. A map that swapped the sides would raise one arm and
// lower the other, and no component-against-component test would notice.
//
// **The path rule.** Four of the rig's seven torso segments and one of its two
// neck segments carry no canonical bone. Their rotations are not dropped, and
// the unit tests state that twice over: once by comparing a bound bone against
// the product along its path, and once by checking that the chain's *total*
// orientation is the same whichever joints the map happened to bind. The second
// is the invariant that makes the three-of-seven torso choice a decision rather
// than an error.
//
// The unit tests below build `MotionSkeleton` and `MotionFrame` values directly
// rather than bytes. That is not a shortcut around the corpus: this layer takes
// decoded structs, and a test that went through the wire format would be
// re-testing the decoder and would not be able to state a rig the decoder has
// never seen.
#include "vrmAdapterMocopi/SkeletonMap.h"

#include "vrmAdapterMocopi/MotionPacket.h"
#include "vrmAdapterMocopi/PacketCapture.h"

#include "motionCore/Compare.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace
{

using motion::HumanBone;
using vrmAdapterMocopi::BoneDefinition;
using vrmAdapterMocopi::BoneFrame;
using vrmAdapterMocopi::BoneTransform;
using vrmAdapterMocopi::Diagnostic;
using vrmAdapterMocopi::DiagnosticCode;
using vrmAdapterMocopi::FrameMapping;
using vrmAdapterMocopi::MeasuredBoneCount;
using vrmAdapterMocopi::MeasuredParentColumn;
using vrmAdapterMocopi::MotionFrame;
using vrmAdapterMocopi::MotionSkeleton;
using vrmAdapterMocopi::SkeletonMap;

// The rest offsets the corpus generator invented, in bone order — round numbers
// that are nobody's body. Repeated here rather than read from a capture, so that
// what a composed rest pose is compared against is a stated table rather than
// the fixture agreeing with itself.
const float kRestOffsets[MeasuredBoneCount][3] = {
    {0.0f, 0.90f, 0.0f},    {0.0f, 0.06f, 0.0f},   {0.0f, 0.06f, 0.0f},
    {0.0f, 0.06f, 0.0f},    {0.0f, 0.06f, 0.0f},   {0.0f, 0.06f, 0.0f},
    {0.0f, 0.06f, 0.0f},    {0.0f, 0.10f, 0.0f},   {0.0f, 0.05f, 0.0f},
    {0.0f, 0.05f, 0.0f},    {0.0f, 0.05f, 0.0f},   {0.02f, -0.08f, 0.08f},
    {0.14f, 0.0f, 0.0f},    {0.30f, 0.0f, 0.0f},   {0.25f, 0.0f, 0.0f},
    {-0.02f, -0.08f, 0.08f}, {-0.14f, 0.0f, 0.0f}, {-0.30f, 0.0f, 0.0f},
    {-0.25f, 0.0f, 0.0f},   {0.09f, -0.05f, 0.0f}, {0.0f, -0.40f, 0.0f},
    {0.0f, -0.42f, 0.0f},   {0.0f, -0.10f, 0.13f}, {-0.09f, -0.05f, 0.0f},
    {0.0f, -0.40f, 0.0f},   {0.0f, -0.42f, 0.0f},  {0.0f, -0.10f, 0.13f},
};

constexpr std::size_t kCanonicalBoneCount = 22;

// The tolerances are `motionCore`'s, never a number picked here: a test that
// chose its own would be asserting a contract nobody reviewed (Compare.h).
constexpr motion::MotionTolerance kTolerance{};

// The one number this file does choose, and it is not a motion tolerance: a
// quaternion's length is arithmetic rather than a measurement, so "did this get
// normalised" is a question about float division and not about how far apart
// two poses may be and still be the same motion.
constexpr float kUnitLengthEpsilon = 1e-6f;

constexpr float kPi = 3.14159265358979323846f;

bool
SameOrientation(const pxr::GfQuatf& a, const pxr::GfQuatf& b)
{
    return motion::AngleBetween(a, b) <= kTolerance.angle;
}

bool
NearlyEqual(const pxr::GfVec3f& a, const pxr::GfVec3f& b)
{
    return (a - b).GetLength() <= kTolerance.distance;
}

// A wire rotation: scalar-last, as this protocol serialises one.
std::array<float, 4>
WireRotation(const pxr::GfVec3f& axis, float degrees)
{
    const float radians = degrees * kPi / 180.0f;
    const pxr::GfVec3f unit = axis.GetNormalized();
    const float sine = std::sin(0.5f * radians);
    return {{unit[0] * sine, unit[1] * sine, unit[2] * sine,
             std::cos(0.5f * radians)}};
}

std::array<float, 4>
WireIdentity()
{
    return {{0.0f, 0.0f, 0.0f, 1.0f}};
}

std::array<float, 3>
RestOffset(std::size_t jointId)
{
    return {{kRestOffsets[jointId][0], kRestOffsets[jointId][1],
             kRestOffsets[jointId][2]}};
}

// The rig all five measured sessions sent, with the corpus's invented offsets.
MotionSkeleton
MeasuredSkeleton()
{
    MotionSkeleton skeleton;
    for (std::size_t jointId = 0; jointId < MeasuredBoneCount; ++jointId) {
        BoneDefinition joint;
        joint.boneId = static_cast<std::uint16_t>(jointId);
        joint.parentBoneId = MeasuredParentColumn[jointId];
        joint.restTransform.rotation = WireIdentity();
        joint.restTransform.translation = RestOffset(jointId);
        skeleton.bones.push_back(joint);
    }
    return skeleton;
}

// A frame that restates the rest pose, which is what every measured frame does
// for every joint but the root.
MotionFrame
RestFrame()
{
    MotionFrame frame;
    for (std::size_t jointId = 0; jointId < MeasuredBoneCount; ++jointId) {
        BoneFrame bone;
        bone.boneId = static_cast<std::uint16_t>(jointId);
        bone.transform.rotation = WireIdentity();
        bone.transform.translation = RestOffset(jointId);
        frame.bones.push_back(bone);
    }
    return frame;
}

BoneFrame*
Joint(MotionFrame* frame, std::uint16_t boneId)
{
    for (BoneFrame& bone : frame->bones) {
        if (bone.boneId == boneId) {
            return &bone;
        }
    }
    return nullptr;
}

const pxr::GfQuatf*
Rotation(const FrameMapping& mapping, HumanBone bone)
{
    for (const vrmAdapterMocopi::BoneSample& sample : mapping.bones) {
        if (sample.bone == bone) {
            return &sample.localRotation;
        }
    }
    return nullptr;
}

SkeletonMap
MeasuredMap()
{
    SkeletonMap map;
    assert(vrmAdapterMocopi::MakeSkeletonMap(MeasuredSkeleton(), &map));
    return map;
}

// ---------------------------------------------------------------------------
// The rig, and the rigs this map declines to read
// ---------------------------------------------------------------------------

void
TestTheMeasuredRigCarriesTwentyTwoBones()
{
    std::vector<Diagnostic> diagnostics;
    SkeletonMap map;
    assert(vrmAdapterMocopi::MakeSkeletonMap(MeasuredSkeleton(), &map,
                                             &diagnostics));
    assert(diagnostics.empty());
    assert(map.jointCount == MeasuredBoneCount);
    assert(map.present.count() == kCanonicalBoneCount);

    // The sides, stated once: this is the table the whole layer exists to hold,
    // and it is the same one `mocopi-mobile-bvh-default-v1.yaml` states for the
    // same rig over a different transport.
    assert(map.Bone(0) == HumanBone::Hips);
    assert(map.Bone(2) == HumanBone::Spine);
    assert(map.Bone(4) == HumanBone::Chest);
    assert(map.Bone(7) == HumanBone::UpperChest);
    assert(map.Bone(8) == HumanBone::Neck);
    assert(map.Bone(10) == HumanBone::Head);
    assert(map.Bone(11) == HumanBone::LeftShoulder);
    assert(map.Bone(12) == HumanBone::LeftUpperArm);
    assert(map.Bone(14) == HumanBone::LeftHand);
    assert(map.Bone(16) == HumanBone::RightUpperArm);
    assert(map.Bone(18) == HumanBone::RightHand);
    assert(map.Bone(19) == HumanBone::LeftUpperLeg);
    assert(map.Bone(22) == HumanBone::LeftToes);
    assert(map.Bone(23) == HumanBone::RightUpperLeg);
    assert(map.Bone(26) == HumanBone::RightToes);

    // The five the canonical vocabulary has no bone for. They are joints of the
    // rig — which is why `IsMeasuredJoint` says yes and `Bone` says nothing —
    // and their rotations reach the humanoid through the composition.
    for (const int jointId : {1, 3, 5, 6, 9}) {
        const auto id = static_cast<std::uint16_t>(jointId);
        assert(vrmAdapterMocopi::IsMeasuredJoint(id));
        assert(!map.Bone(id).has_value());
    }
    assert(!vrmAdapterMocopi::IsMeasuredJoint(
        static_cast<std::uint16_t>(MeasuredBoneCount)));
}

void
TestARigThatIsNotTheMeasuredOneIsRefused()
{
    // Shorter: the eleven-bone rig the `extended-form` capture carries. Not a
    // malformed packet one layer down, and not a rig any id here has a measured
    // meaning in.
    MotionSkeleton shortRig;
    for (std::size_t jointId = 0; jointId < 11; ++jointId) {
        BoneDefinition joint;
        joint.boneId = static_cast<std::uint16_t>(jointId);
        joint.parentBoneId = MeasuredParentColumn[jointId];
        joint.restTransform.rotation = WireIdentity();
        joint.restTransform.translation = RestOffset(jointId);
        shortRig.bones.push_back(joint);
    }
    std::vector<Diagnostic> diagnostics;
    SkeletonMap map;
    assert(!vrmAdapterMocopi::MakeSkeletonMap(shortRig, &map, &diagnostics));
    assert(diagnostics.size() == 1);
    assert(diagnostics[0].code == DiagnosticCode::UnsupportedJoint);
    assert(diagnostics[0].subject == "bone 11");
    // Louder than the code's own default, because this is the other case the
    // code covers: no joint maps rather than one, so the session produces
    // nothing at all and an operator filtering at warning has to see it.
    assert(diagnostics[0].severity
           == vrmAdapterMocopi::DiagnosticSeverity::Warning);
    assert(diagnostics[0].recoverable);
    // Nothing was written: a caller that ignored the return value gets an empty
    // map rather than a half-built one.
    assert(map.present.none());
    assert(map.jointCount == 0);

    // A joint moved to a different parent. The count still matches and every id
    // is still present, so this is the case an id-keyed table with no topology
    // check would map straight through — the left arm hanging off the head.
    MotionSkeleton reparented = MeasuredSkeleton();
    reparented.bones[12].parentBoneId = 10;
    diagnostics.clear();
    assert(!vrmAdapterMocopi::MakeSkeletonMap(reparented, &map, &diagnostics));
    assert(diagnostics.size() == 1);
    assert(diagnostics[0].code == DiagnosticCode::UnsupportedJoint);
    assert(diagnostics[0].subject == "bone 12");

    // Ids that are not their own positions. The measurement is of a rig whose
    // records arrive in id order, and this layer requires what it measured.
    MotionSkeleton permuted = MeasuredSkeleton();
    std::swap(permuted.bones[19].boneId, permuted.bones[23].boneId);
    diagnostics.clear();
    assert(!vrmAdapterMocopi::MakeSkeletonMap(permuted, &map, &diagnostics));
    assert(diagnostics.size() == 1);
    assert(diagnostics[0].code == DiagnosticCode::UnsupportedJoint);

    // A rest transform that names no orientation. The decoder refuses such a
    // skeleton packet whole and this never arrives from it — but this function
    // takes a struct, and a rest table with a hole in it leaves every bone below
    // the hole unplaced.
    MotionSkeleton unusable = MeasuredSkeleton();
    unusable.bones[4].restTransform.rotation = {{0.0f, 0.0f, 0.0f, 0.0f}};
    diagnostics.clear();
    assert(!vrmAdapterMocopi::MakeSkeletonMap(unusable, &map, &diagnostics));
    assert(diagnostics.size() == 1);
    assert(diagnostics[0].code == DiagnosticCode::NonFiniteTransform);
}

void
TestALongerRigKeepsItsMeasuredJoints()
{
    // A later application version that appends joints — fingers, say. Its
    // leading twenty-seven are the measured rig bone for bone, so they are
    // mapped, and the joints past them are reported once for the session rather
    // than sixty times a second.
    MotionSkeleton longer = MeasuredSkeleton();
    for (std::size_t jointId = MeasuredBoneCount; jointId < MeasuredBoneCount + 3;
         ++jointId) {
        BoneDefinition joint;
        joint.boneId = static_cast<std::uint16_t>(jointId);
        joint.parentBoneId = 14;
        joint.restTransform.rotation = WireIdentity();
        joint.restTransform.translation = {{0.02f, 0.0f, 0.0f}};
        longer.bones.push_back(joint);
    }

    std::vector<Diagnostic> diagnostics;
    SkeletonMap map;
    assert(vrmAdapterMocopi::MakeSkeletonMap(longer, &map, &diagnostics));
    assert(map.jointCount == MeasuredBoneCount + 3);
    assert(map.present.count() == kCanonicalBoneCount);
    assert(diagnostics.size() == 3);
    for (const Diagnostic& diagnostic : diagnostics) {
        assert(diagnostic.code == DiagnosticCode::UnsupportedJoint);
        assert(diagnostic.recoverable);
        // Info, the code's own default: three joints nobody has measured is a
        // session working with an extension, and a refused rig is the case that
        // reads louder.
        assert(diagnostic.severity
               == vrmAdapterMocopi::DiagnosticSeverity::Info);
    }

    // A trailing joint claiming an id inside the measured range is a different
    // thing entirely: an id is a position, and that rig has broken the identity
    // every id above it rests on. Refused rather than reported as "bone 5 is
    // beyond the measured rig" while bone 5 sits where it always was.
    MotionSkeleton collided = longer;
    collided.bones.back().boneId = 5;
    diagnostics.clear();
    assert(!vrmAdapterMocopi::MakeSkeletonMap(collided, &map, &diagnostics));
    assert(!diagnostics.empty());
    assert(diagnostics.back().code == DiagnosticCode::UnsupportedJoint);
    assert(diagnostics.back().severity
           == vrmAdapterMocopi::DiagnosticSeverity::Warning);

    // A frame from that rig maps its twenty-two bones and counts the rest.
    MotionFrame frame = RestFrame();
    for (std::uint16_t jointId = MeasuredBoneCount;
         jointId < MeasuredBoneCount + 3; ++jointId) {
        BoneFrame bone;
        bone.boneId = jointId;
        bone.transform.rotation = WireRotation(pxr::GfVec3f(0, 0, 1), 20.0f);
        bone.transform.translation = {{0.02f, 0.0f, 0.0f}};
        frame.bones.push_back(bone);
    }
    FrameMapping mapping;
    diagnostics.clear();
    assert(vrmAdapterMocopi::MapMotionFrame(map, frame, &mapping,
                                            &diagnostics));
    assert(mapping.present.count() == kCanonicalBoneCount);
    assert(mapping.unusedJoints == 3);
    // The session already said this once. Saying it again per frame is the
    // thing this layer is not allowed to do.
    assert(diagnostics.empty());
}

// ---------------------------------------------------------------------------
// The basis, checked physically
// ---------------------------------------------------------------------------

void
TestTheBasisChangeIsTheIdentity()
{
    // A position is copied component for component. The claim is not that these
    // three lines are right — it is that the *body* comes out the right way
    // round, so it is checked against the rig's own geometry: the left arm's
    // rest offsets run along +X, which is the body's left in the canonical basis
    // as well as in this device's.
    const SkeletonMap map = MeasuredMap();
    assert(map.restTranslations[static_cast<std::size_t>(
               HumanBone::LeftUpperArm)][0]
           > 0.0f);
    assert(map.restTranslations[static_cast<std::size_t>(
               HumanBone::RightUpperArm)][0]
           < 0.0f);
    // Up is +Y and forward is +Z, from the same geometry: the hips sit at hip
    // height and the toes sit forward of the ankle.
    assert(map.restTranslations[static_cast<std::size_t>(HumanBone::Hips)][1]
           > 0.5f);
    assert(map.restTranslations[static_cast<std::size_t>(HumanBone::LeftToes)][2]
           > 0.0f);

    // A rotation is reordered and normalised and not otherwise touched, and
    // that is checked by rotating a direction rather than by comparing
    // components: a mirrored implementation agrees component for component with
    // a correct one as readily as it disagrees.
    const pxr::GfQuatf quarterTurn =
        vrmAdapterMocopi::ToCanonicalRotation(
            WireRotation(pxr::GfVec3f(0, 0, 1), 90.0f));
    assert(NearlyEqual(quarterTurn.Transform(pxr::GfVec3f(1, 0, 0)),
                       pxr::GfVec3f(0, 1, 0)));

    // Scalar-last in, scalar-first out. This is the one thing that *does*
    // change, and a reader mistaking it for a basis change would go looking for
    // a handedness flip that is not there. An implementation that took the wire
    // components in order would name a different orientation, which is what the
    // comparison below is asking about rather than which slot holds 0.4.
    const pxr::GfQuatf converted =
        vrmAdapterMocopi::ToCanonicalRotation({{0.1f, 0.2f, 0.3f, 0.4f}});
    const float length =
        std::sqrt(0.1f * 0.1f + 0.2f * 0.2f + 0.3f * 0.3f + 0.4f * 0.4f);
    assert(SameOrientation(
        converted,
        pxr::GfQuatf(0.4f / length,
                     pxr::GfVec3f(0.1f, 0.2f, 0.3f) / length)));

    // Un-normalised in, unit out; zero-length in, zero-length out. Repairing the
    // second would put an identity where a refusal belongs, and the refusal is
    // the mapping functions'.
    assert(std::fabs(converted.GetLength() - 1.0f) <= kUnitLengthEpsilon);
    const pxr::GfQuatf zero =
        vrmAdapterMocopi::ToCanonicalRotation({{0.0f, 0.0f, 0.0f, 0.0f}});
    assert(zero.GetLength() == 0.0f);
}

// ---------------------------------------------------------------------------
// The path rule
// ---------------------------------------------------------------------------

void
TestAnUnmappedJointIsNotARotationThrownAway()
{
    const SkeletonMap map = MeasuredMap();
    MotionFrame frame = RestFrame();
    // `torso_1` carries no canonical bone and `torso_2` is the spine. A map that
    // took the spine's own rotation verbatim would lose the first of these
    // entirely.
    Joint(&frame, 1)->transform.rotation =
        WireRotation(pxr::GfVec3f(1, 0, 0), 20.0f);
    Joint(&frame, 2)->transform.rotation =
        WireRotation(pxr::GfVec3f(1, 0, 0), 10.0f);

    FrameMapping mapping;
    assert(vrmAdapterMocopi::MapMotionFrame(map, frame, &mapping));
    const pxr::GfQuatf* spine = Rotation(mapping, HumanBone::Spine);
    assert(spine != nullptr);
    assert(SameOrientation(*spine,
                           vrmAdapterMocopi::ToCanonicalRotation(
                               WireRotation(pxr::GfVec3f(1, 0, 0), 30.0f))));
    // And it is *not* the joint's own rotation, which is the failure this test
    // exists to catch rather than a restatement of the line above.
    assert(!SameOrientation(*spine,
                            vrmAdapterMocopi::ToCanonicalRotation(
                                WireRotation(pxr::GfVec3f(1, 0, 0), 10.0f))));
}

void
TestWhichJointsAreBoundChangesDistributionAndNotTheWhole()
{
    // The invariant that makes the three-of-seven torso mapping a decision this
    // layer is allowed to make: whatever it binds, the orientation the chain
    // arrives at is the product of every joint on it.
    const SkeletonMap map = MeasuredMap();
    MotionFrame frame = RestFrame();
    pxr::GfQuatf expected(1.0f, pxr::GfVec3f(0.0f));
    for (std::uint16_t jointId = 0; jointId <= 7; ++jointId) {
        const pxr::GfVec3f axis(jointId % 3 == 0 ? 1.0f : 0.0f,
                                jointId % 3 == 1 ? 1.0f : 0.0f,
                                jointId % 3 == 2 ? 1.0f : 0.0f);
        const std::array<float, 4> rotation =
            WireRotation(axis, 3.0f + static_cast<float>(jointId));
        Joint(&frame, jointId)->transform.rotation = rotation;
        expected = expected * vrmAdapterMocopi::ToCanonicalRotation(rotation);
    }

    FrameMapping mapping;
    assert(vrmAdapterMocopi::MapMotionFrame(map, frame, &mapping));
    pxr::GfQuatf composed(1.0f, pxr::GfVec3f(0.0f));
    for (const HumanBone bone : {HumanBone::Hips, HumanBone::Spine,
                                 HumanBone::Chest, HumanBone::UpperChest}) {
        const pxr::GfQuatf* rotation = Rotation(mapping, bone);
        assert(rotation != nullptr);
        composed = composed * *rotation;
    }
    assert(SameOrientation(composed, expected));
}

void
TestAPathIsOnlyAsPresentAsItsJoints()
{
    // The decoder refuses a bone record and keeps the frame, so a frame can
    // arrive with a joint missing. When that joint is on a bound bone's path,
    // the bone cannot be composed — and a bone composed from the part that
    // arrived would be a rotation the device never sent.
    const SkeletonMap map = MeasuredMap();
    MotionFrame frame = RestFrame();
    frame.bones.erase(frame.bones.begin() + 5); // torso_5, on upperChest's path

    FrameMapping mapping;
    assert(vrmAdapterMocopi::MapMotionFrame(map, frame, &mapping));
    assert(!mapping.present.test(static_cast<std::size_t>(
        HumanBone::UpperChest)));
    assert(mapping.missingBones == 1);
    // The neck hangs off `torso_7` and its own record arrived, so its path is
    // intact: one missing segment costs the bone it was on the path to and not
    // the ones below it. What a consumer does with a parent that stopped
    // arriving is `LiveCaptureSource`'s missing-bone policy, not this layer's.
    assert(mapping.present.test(static_cast<std::size_t>(HumanBone::Neck)));
    assert(mapping.present.count() == kCanonicalBoneCount - 1);
}

// ---------------------------------------------------------------------------
// Translations, and the rest pose the device sends
// ---------------------------------------------------------------------------

void
TestOnlyTheHipsTranslates()
{
    const SkeletonMap map = MeasuredMap();
    MotionFrame frame = RestFrame();
    Joint(&frame, 0)->transform.translation = {{0.1f, 0.95f, -0.2f}};

    FrameMapping mapping;
    assert(vrmAdapterMocopi::MapMotionFrame(map, frame, &mapping));
    assert(mapping.hasHipsPosition);
    assert(NearlyEqual(mapping.hipsPosition, pxr::GfVec3f(0.1f, 0.95f, -0.2f)));
    // Every other joint restated its rest offset, which is what the measurement
    // says a frame does.
    assert(mapping.droppedTranslations == 0);

    // One that does not. The canonical pose has nowhere to put it — only hips
    // translation is body translation — so it is dropped and counted, which is
    // how an operator finds out a session did something the measurement says
    // sessions do not do.
    Joint(&frame, 13)->transform.translation = {{0.31f, 0.0f, 0.0f}};
    assert(vrmAdapterMocopi::MapMotionFrame(map, frame, &mapping));
    assert(mapping.droppedTranslations == 1);
    assert(mapping.present.count() == kCanonicalBoneCount);

    // A frame with no hips record says so rather than reporting the origin.
    MotionFrame headless = RestFrame();
    headless.bones.erase(headless.bones.begin());
    assert(vrmAdapterMocopi::MapMotionFrame(map, headless, &mapping));
    assert(!mapping.hasHipsPosition);
    assert(!mapping.present.test(static_cast<std::size_t>(HumanBone::Hips)));
}

void
TestTheRestPoseIsTheDevicesOwn()
{
    // A skeleton packet is a rest pose, so nothing here is manufactured from
    // the first frame — which is the thing the sibling adapter's header says an
    // adapter must not do, and cannot avoid doing, because VMC sends no rest.
    const SkeletonMap map = MeasuredMap();
    const auto spine = static_cast<std::size_t>(HumanBone::Spine);
    const auto upperChest = static_cast<std::size_t>(HumanBone::UpperChest);
    // Composed along the same paths the frames are: the spine's rest offset is
    // `torso_1` plus `torso_2`, and the upper chest's is three segments.
    assert(NearlyEqual(map.restTranslations[spine],
                       pxr::GfVec3f(0.0f, 0.12f, 0.0f)));
    assert(NearlyEqual(map.restTranslations[upperChest],
                       pxr::GfVec3f(0.0f, 0.22f, 0.0f)));
    assert(NearlyEqual(
        map.restTranslations[static_cast<std::size_t>(HumanBone::Hips)],
        pxr::GfVec3f(0.0f, 0.90f, 0.0f)));

    // Every measured skeleton packet states identity rotations. This reports
    // what the packet said rather than that expectation.
    MotionSkeleton turned = MeasuredSkeleton();
    turned.bones[1].restTransform.rotation =
        WireRotation(pxr::GfVec3f(0, 0, 1), 15.0f);
    turned.bones[2].restTransform.rotation =
        WireRotation(pxr::GfVec3f(0, 0, 1), 5.0f);
    SkeletonMap rotatedRest;
    assert(vrmAdapterMocopi::MakeSkeletonMap(turned, &rotatedRest));
    assert(SameOrientation(rotatedRest.restRotations[spine],
                           vrmAdapterMocopi::ToCanonicalRotation(
                               WireRotation(pxr::GfVec3f(0, 0, 1), 20.0f))));
}

// ---------------------------------------------------------------------------
// What a frame may carry that a pose cannot
// ---------------------------------------------------------------------------

void
TestARecordThatNoBoneCameFromIsCounted()
{
    const SkeletonMap map = MeasuredMap();

    // A repeat of an id already read. The first record is the one used, and the
    // repeat is counted rather than silently overwriting it.
    MotionFrame repeated = RestFrame();
    BoneFrame again;
    again.boneId = 12;
    again.transform.rotation = WireRotation(pxr::GfVec3f(0, 0, 1), 40.0f);
    again.transform.translation = RestOffset(12);
    repeated.bones.push_back(again);

    FrameMapping mapping;
    assert(vrmAdapterMocopi::MapMotionFrame(map, repeated, &mapping));
    assert(mapping.unusedJoints == 1);
    const pxr::GfQuatf* upperArm = Rotation(mapping, HumanBone::LeftUpperArm);
    assert(upperArm != nullptr);
    assert(SameOrientation(*upperArm, pxr::GfQuatf(1.0f, pxr::GfVec3f(0.0f))));

    // A joint the session's skeleton packet never declared.
    MotionFrame stranger = RestFrame();
    BoneFrame extra;
    extra.boneId = 40;
    extra.transform.rotation = WireIdentity();
    extra.transform.translation = {{0.0f, 0.0f, 0.0f}};
    stranger.bones.push_back(extra);
    assert(vrmAdapterMocopi::MapMotionFrame(map, stranger, &mapping));
    assert(mapping.unusedJoints == 1);
    assert(mapping.present.count() == kCanonicalBoneCount);
}

void
TestATransformThatNamesNoOrientationIsRefused()
{
    // Unreachable through the decoder, which refuses such a record before a
    // frame is built. Reachable through any other caller, because this function
    // takes structs and nothing in the type system says where one came from.
    const SkeletonMap map = MeasuredMap();
    MotionFrame frame = RestFrame();
    Joint(&frame, 12)->transform.rotation = {{std::nanf(""), 0.0f, 0.0f, 1.0f}};

    std::vector<Diagnostic> diagnostics;
    FrameMapping mapping;
    assert(vrmAdapterMocopi::MapMotionFrame(map, frame, &mapping,
                                            &diagnostics));
    assert(diagnostics.size() == 1);
    assert(diagnostics[0].code == DiagnosticCode::NonFiniteTransform);
    assert(diagnostics[0].subject == "bone 12");
    // The upper arm is absent, and so is everything whose path runs through it.
    assert(!mapping.present.test(static_cast<std::size_t>(
        HumanBone::LeftUpperArm)));
    assert(mapping.present.test(static_cast<std::size_t>(
        HumanBone::LeftLowerArm)));
    assert(mapping.missingBones == 1);
}

void
TestSamplesComeOutInHumanoidOrder()
{
    // The order the header states, and it is not the order the joints arrive
    // in: this rig runs both arms off the top of the torso where the canonical
    // vocabulary numbers the legs first, so a walk by joint id emits every arm
    // ahead of every leg. A consumer merging this against another list sorted by
    // bone would find that as one wrong sample rather than as a failure.
    const SkeletonMap map = MeasuredMap();
    FrameMapping mapping;
    assert(vrmAdapterMocopi::MapMotionFrame(map, RestFrame(), &mapping));
    assert(mapping.bones.size() == kCanonicalBoneCount);
    for (std::size_t index = 1; index < mapping.bones.size(); ++index) {
        assert(mapping.bones[index - 1].bone < mapping.bones[index].bone);
    }
    // Named once so the test states the claim rather than only the invariant:
    // the legs precede the arms, which the wire order reverses.
    assert(mapping.bones[6].bone == HumanBone::LeftUpperLeg);
    assert(mapping.bones[14].bone == HumanBone::LeftShoulder);

    // `bones` and `present` are two spellings of one answer.
    std::bitset<motion::HumanBoneCount> listed;
    for (const vrmAdapterMocopi::BoneSample& sample : mapping.bones) {
        listed.set(static_cast<std::size_t>(sample.bone));
    }
    assert(listed == mapping.present);
}

void
TestARotationTooSmallToSquareIsStillNormalised()
{
    // The boundary check accumulates in double and the normalisation used to
    // divide by a float length. A quaternion whose components sit near the
    // denormal floor passes the first and underflows the second to exactly
    // zero, which returns it un-normalised and collapses every composition it
    // enters — the same two-precisions trap `motionCore/Compare.h` records
    // paying for once already.
    const float tiny = 1e-23f;
    const pxr::GfQuatf converted =
        vrmAdapterMocopi::ToCanonicalRotation({{tiny, tiny, tiny, tiny}});
    assert(std::fabs(converted.GetLength() - 1.0f) <= kUnitLengthEpsilon);
    assert(SameOrientation(converted,
                           vrmAdapterMocopi::ToCanonicalRotation(
                               {{1.0f, 1.0f, 1.0f, 1.0f}})));

    // And it survives the layer, rather than only the arithmetic.
    const SkeletonMap map = MeasuredMap();
    MotionFrame frame = RestFrame();
    Joint(&frame, 12)->transform.rotation = {{0.0f, 0.0f, tiny, tiny}};
    FrameMapping mapping;
    assert(vrmAdapterMocopi::MapMotionFrame(map, frame, &mapping));
    const pxr::GfQuatf* upperArm = Rotation(mapping, HumanBone::LeftUpperArm);
    assert(upperArm != nullptr);
    assert(std::fabs(upperArm->GetLength() - 1.0f) <= kUnitLengthEpsilon);
}

void
TestAFrameThatFormsNoBoneIsNotAPose()
{
    const SkeletonMap map = MeasuredMap();
    MotionFrame empty;
    FrameMapping mapping;
    assert(!vrmAdapterMocopi::MapMotionFrame(map, empty, &mapping));
    assert(mapping.bones.empty());
    assert(mapping.present.none());
    assert(mapping.missingBones == kCanonicalBoneCount);
}

// ---------------------------------------------------------------------------
// Corpus mode
// ---------------------------------------------------------------------------

int
Failed(const std::string& name, const std::string& why)
{
    std::fprintf(stderr, "%s: %s\n", name.c_str(), why.c_str());
    return 1;
}

// One capture, decoded and then mapped: the skeleton packets build the map and
// the frame packets go through it.
//
// A capture with no skeleton packet in it — `refused-bones-60hz` is one — is
// mapped through the measured rig this file states, because that is the rig the
// generator wrote its frames from. That is a property of the corpus and it is
// worth being explicit about: a live session cannot do this, and the map it
// would build instead is exactly what `declaredRig` records having been used.
struct MappedCapture
{
    bool declaredRig = false;
    bool rigRefused = false;
    SkeletonMap map;
    std::vector<FrameMapping> frames;
    std::vector<Diagnostic> diagnostics;
    std::size_t skeletons = 0;
    std::size_t refusedSkeletons = 0;
    // Frames that mapped no canonical bone at all. Not the same as a frame that
    // arrived, which is what `frames.size()` counts.
    std::size_t poselessFrames = 0;
};

int
CheckNeutralStanding(const MappedCapture& capture, const std::string& name)
{
    if (!capture.declaredRig || capture.frames.size() != 5
        || capture.poselessFrames != 0) {
        return Failed(name, "a rig and five frames of motion were expected");
    }
    for (const FrameMapping& frame : capture.frames) {
        if (frame.present.count() != kCanonicalBoneCount
            || frame.missingBones != 0 || frame.unusedJoints != 0
            || frame.droppedTranslations != 0) {
            return Failed(name, "a frame did not map the whole rig");
        }
        if (!frame.hasHipsPosition
            || !NearlyEqual(frame.hipsPosition,
                            pxr::GfVec3f(0.0f, 0.90f, 0.0f))) {
            return Failed(name, "the hips are not where the rest pose puts "
                                "them");
        }
        for (const vrmAdapterMocopi::BoneSample& sample : frame.bones) {
            if (!SameOrientation(sample.localRotation,
                                 pxr::GfQuatf(1.0f, pxr::GfVec3f(0.0f)))) {
                return Failed(name, "a rotation is not identity");
            }
        }
    }
    return 0;
}

int
CheckArmsLowered(const MappedCapture& capture, const std::string& name)
{
    if (!capture.declaredRig || capture.frames.size() != 3) {
        return Failed(name, "a rig and three frames were expected");
    }
    // Which side is which, and it is a fact about the *basis* rather than about
    // the motion: +X is the body's left, so the left arm's rest offsets run
    // along +X. This is checked first and separately because the rig is
    // bilaterally symmetric — measured, not assumed: swapping the two arms in
    // the map's table leaves every "both arms went down" assertion below still
    // passing, because the rest direction and the rotation are swapped together.
    // A symmetric rig cannot be asked which arm moved; it can only be asked
    // which arm is where.
    if (capture.map.restTranslations[static_cast<std::size_t>(
                                        HumanBone::LeftUpperArm)][0]
            <= 0.0f
        || capture.map.restTranslations[static_cast<std::size_t>(
                                            HumanBone::RightUpperArm)][0]
               >= 0.0f) {
        return Failed(name, "the left arm is not on +X, so the sides are "
                            "swapped or the basis is mirrored");
    }

    // The claim the fixture was written for and could not state one layer down:
    // that these two rotations lower two arms. It is asked physically — the
    // forearm's rest direction is rotated by the sample and compared against
    // where a lowered arm has to be — because a component-against-component
    // check agrees with a mirrored implementation as readily as with a correct
    // one.
    //
    // The direction comes from the *map's* rest pose rather than from a literal
    // here, which is what makes it cover the position half of the basis too:
    // mirror the position conversion alone and the arm starts on the wrong
    // side, so the same rotation lifts it.
    const std::pair<HumanBone, HumanBone> arms[] = {
        {HumanBone::LeftUpperArm, HumanBone::LeftLowerArm},
        {HumanBone::RightUpperArm, HumanBone::RightLowerArm},
    };
    for (const FrameMapping& frame : capture.frames) {
        for (const auto& arm : arms) {
            const pxr::GfQuatf* rotation = Rotation(frame, arm.first);
            if (rotation == nullptr) {
                return Failed(name, "an upper arm did not map");
            }
            const pxr::GfVec3f rest =
                capture.map
                    .restTranslations[static_cast<std::size_t>(arm.second)];
            if (std::fabs(rest[0]) < 0.1f) {
                return Failed(name, "a forearm does not sit along the rig's "
                                    "lateral axis in rest");
            }
            const pxr::GfVec3f lowered =
                rotation->Transform(rest.GetNormalized());
            if (lowered[1] > -0.9f) {
                return Failed(name, "an arm is not pointing down");
            }
        }
    }
    // The hips move, and forward is +Z: down a centimetre and forward two per
    // frame.
    if (!capture.frames[2].hasHipsPosition
        || !NearlyEqual(capture.frames[2].hipsPosition,
                        pxr::GfVec3f(0.0f, 0.88f, 0.04f))) {
        return Failed(name, "the hips did not move as the capture says");
    }
    return 0;
}

int
CheckFrameLoss(const MappedCapture& capture, const std::string& name)
{
    // A gap, a duplicate delivery and a restart are all transport and sequence
    // questions, and this layer answers none of them: every frame in the file
    // maps the whole rig, and which of them the session should keep is the
    // assembler's.
    if (!capture.declaredRig || capture.frames.size() != 7
        || capture.poselessFrames != 0) {
        return Failed(name, "a rig and seven frames of motion were expected");
    }
    for (const FrameMapping& frame : capture.frames) {
        if (frame.present.count() != kCanonicalBoneCount) {
            return Failed(name, "a frame did not map the whole rig");
        }
    }
    return 0;
}

int
CheckRefusedBones(const MappedCapture& capture, const std::string& name)
{
    // The one capture in the corpus with no skeleton packet in it, so the rig is
    // the measured one this file states rather than one the session declared.
    if (capture.declaredRig || capture.frames.size() != 2) {
        return Failed(name, "two frames of the measured rig were expected");
    }
    // The bone-not-frame rule reaching the humanoid, and this is the assertion
    // the fixture could not make one layer down: the decoder dropped three bone
    // *records* — 5, 9 and 20 — and what that costs the pose is three canonical
    // bones, two of which arrived intact. `torso_5` and `neck_2` carry no bone
    // at all; they are on the paths to the upper chest and the head.
    const FrameMapping& damaged = capture.frames[0];
    const HumanBone missing[] = {HumanBone::UpperChest, HumanBone::Head,
                                 HumanBone::LeftLowerLeg};
    for (const HumanBone bone : missing) {
        if (damaged.present.test(static_cast<std::size_t>(bone))) {
            return Failed(name, "a bone whose path lost a joint is present");
        }
    }
    if (damaged.missingBones != 3
        || damaged.present.count() != kCanonicalBoneCount - 3) {
        return Failed(name, "three refused records did not cost three bones");
    }
    // The foot hangs off the lower leg that went missing, and it is still here:
    // absence is reported, never propagated down a chain this layer does not
    // own.
    if (!damaged.present.test(static_cast<std::size_t>(HumanBone::LeftFoot))) {
        return Failed(name, "an absence was propagated past the bone it cost");
    }
    if (capture.frames[1].present.count() != kCanonicalBoneCount) {
        return Failed(name, "the undamaged frame did not map the whole rig");
    }
    return 0;
}

int
CheckExtendedForm(const MappedCapture& capture, const std::string& name)
{
    // The eleven-bone rig. A different rig is not a malformed packet — the
    // decoder says so and reports it — and it is also not a rig any id in this
    // map has a measured meaning in, so the map is refused and the frames that
    // follow it have nothing to be mapped through. The one frame that does map
    // is the twenty-seven-bone one that arrives before the rig changes.
    if (capture.declaredRig || capture.refusedSkeletons != 1) {
        return Failed(name, "a rig this map cannot read was accepted");
    }
    if (capture.frames.size() != 1 || capture.poselessFrames != 0
        || capture.frames[0].present.count() != kCanonicalBoneCount) {
        return Failed(name, "the frame before the rig changed did not map");
    }
    if (capture.diagnostics.empty()
        || capture.diagnostics.back().code != DiagnosticCode::UnsupportedJoint) {
        return Failed(name, "the refusal did not name an unsupported joint");
    }
    return 0;
}

int
CheckNothingMaps(const MappedCapture& capture, const std::string& name)
{
    // Every datagram in the two malformed captures is refused by a layer below
    // this one, so nothing reaches it. Registered as an assertion rather than
    // skipped, because "this capture pins nothing here" is a claim that can stop
    // being true.
    if (capture.skeletons != 0 || !capture.frames.empty()) {
        return Failed(name, "a malformed capture produced a rig or a frame");
    }
    return 0;
}

int
CheckCorpus(const std::filesystem::path& directory)
{
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (entry.is_regular_file()
            && entry.path().extension() == ".mocopipackets") {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    if (files.empty()) {
        std::fprintf(stderr, "%s: no captures found\n",
                     directory.string().c_str());
        return 1;
    }

    int failures = 0;
    for (const std::filesystem::path& file : files) {
        const std::string name = file.filename().string();
        vrmAdapterMocopi::PacketCapture capture;
        vrmAdapterMocopi::PacketCaptureError error;
        if (!vrmAdapterMocopi::ReadPacketCaptureFile(file.string(), &capture,
                                                     &error)) {
            failures += Failed(name, "line " + std::to_string(error.line) + ": "
                                         + error.message);
            continue;
        }

        MappedCapture mapped;
        mapped.map = MeasuredMap();
        for (const vrmAdapterMocopi::RecordedDatagram& datagram :
             capture.datagrams) {
            vrmAdapterMocopi::MotionPacket packet;
            // The decoder's own diagnostics are not this test's subject: its
            // corpus mode already pins them, and mixing the two lists would
            // make a refusal here indistinguishable from one there.
            if (!vrmAdapterMocopi::DecodeMotionPacket(datagram.bytes, &packet)) {
                continue;
            }
            if (packet.skeleton) {
                ++mapped.skeletons;
                SkeletonMap map;
                if (vrmAdapterMocopi::MakeSkeletonMap(*packet.skeleton, &map,
                                                      &mapped.diagnostics)) {
                    mapped.map = std::move(map);
                    mapped.declaredRig = true;
                    // Cleared as well as set: a session whose rig this map
                    // cannot read may declare a readable one afterwards, and a
                    // latch would drop every frame after it and blame the
                    // fixture's frame count for a defect in this loop.
                    mapped.rigRefused = false;
                } else {
                    ++mapped.refusedSkeletons;
                    // Everything after it belongs to a rig this map cannot
                    // read. Carrying on with the previous one would be mapping
                    // one session's ids through another session's rig, which is
                    // the whole thing the topology check exists to prevent.
                    mapped.rigRefused = true;
                }
            } else if (packet.frame && !mapped.rigRefused) {
                FrameMapping mapping;
                // The return value is the layer's only "this is not a pose"
                // signal, so it is kept rather than discarded: a frame that
                // mapped nothing would otherwise be counted as a frame by every
                // assertion below that reads `frames.size()`.
                if (!vrmAdapterMocopi::MapMotionFrame(mapped.map,
                                                      *packet.frame, &mapping,
                                                      &mapped.diagnostics)) {
                    ++mapped.poselessFrames;
                }
                mapped.frames.push_back(std::move(mapping));
            }
        }

        int result = 0;
        if (capture.sourceId == "neutral-standing-01") {
            result = CheckNeutralStanding(mapped, name);
        } else if (capture.sourceId == "arms-lowered-01") {
            result = CheckArmsLowered(mapped, name);
        } else if (capture.sourceId == "frame-loss-01") {
            result = CheckFrameLoss(mapped, name);
        } else if (capture.sourceId == "refused-bones-01") {
            result = CheckRefusedBones(mapped, name);
        } else if (capture.sourceId == "extended-form-01") {
            result = CheckExtendedForm(mapped, name);
        } else if (capture.sourceId == "malformed-container-01"
                   || capture.sourceId == "malformed-packets-01") {
            result = CheckNothingMaps(mapped, name);
        } else {
            // A capture nothing asserts against is a capture that pins nothing,
            // and the corpus is small enough that this is a mistake rather than
            // a policy.
            result = Failed(name, "no assertion is registered for sourceId '"
                                      + capture.sourceId + "'");
        }
        failures += result;
        if (result == 0) {
            std::printf("%s: %zu frame(s) mapped\n", name.c_str(),
                        mapped.frames.size());
        }
    }

    if (failures != 0) {
        std::fprintf(stderr, "%d corpus capture(s) failed\n", failures);
        return 1;
    }
    std::printf("mocopi skeleton map corpus: %zu capture(s) verified\n",
                files.size());
    return 0;
}

} // namespace

int
main(int argc, char** argv)
{
    if (argc > 1) {
        return CheckCorpus(std::filesystem::path(argv[1]));
    }

    TestTheMeasuredRigCarriesTwentyTwoBones();
    TestARigThatIsNotTheMeasuredOneIsRefused();
    TestALongerRigKeepsItsMeasuredJoints();
    TestTheBasisChangeIsTheIdentity();
    TestAnUnmappedJointIsNotARotationThrownAway();
    TestWhichJointsAreBoundChangesDistributionAndNotTheWhole();
    TestAPathIsOnlyAsPresentAsItsJoints();
    TestOnlyTheHipsTranslates();
    TestTheRestPoseIsTheDevicesOwn();
    TestARecordThatNoBoneCameFromIsCounted();
    TestATransformThatNamesNoOrientationIsRefused();
    TestSamplesComeOutInHumanoidOrder();
    TestARotationTooSmallToSquareIsStillNormalised();
    TestAFrameThatFormsNoBoneIsNotAPose();
    std::puts("vrmAdapterMocopi skeleton map tests passed");
    return 0;
}
