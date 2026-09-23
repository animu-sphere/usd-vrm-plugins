// SPDX-License-Identifier: Apache-2.0
//
// The seam, with no stage and no exec: what execVrm's computations decide,
// over the plain values the registration TU hands them.
//
// Every expected value is written from a definition -- a matrix whose rotation
// is known, a joint path whose parent is known -- or produced by the library
// call the node is supposed to *be*, where the claim under test is exactly that
// the node is that call and nothing more. The two are kept apart below.

#include "ExecVrmRig.h"

#include <motionCore/MotionPose.h>
#include <vrmRetarget/HumanoidMap.h>
#include <vrmRetarget/RestPose.h>
#include <vrmRetarget/TargetSkeleton.h>

#include "pxr/base/gf/matrix4d.h"
#include "pxr/base/gf/quatd.h"
#include "pxr/base/gf/quatf.h"
#include "pxr/base/gf/vec3d.h"
#include "pxr/base/gf/vec3f.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace
{

using openstrata::motion::HumanJoint;

bool
NearlyEqual(float a, float b, float tolerance = 1e-6f)
{
    return std::abs(a - b) <= tolerance;
}

bool
NearlyEqual(const pxr::GfVec3f& a, const pxr::GfVec3f& b)
{
    return NearlyEqual(a[0], b[0]) && NearlyEqual(a[1], b[1]) && NearlyEqual(a[2], b[2]);
}

bool
NearlyEqual(const pxr::GfQuatf& a, const pxr::GfQuatf& b)
{
    return NearlyEqual(a.GetReal(), b.GetReal()) && NearlyEqual(a.GetImaginary(), b.GetImaginary());
}

pxr::GfMatrix4d
Translate(double x, double y, double z)
{
    return pxr::GfMatrix4d(1.0).SetTranslate(pxr::GfVec3d(x, y, z));
}

// The fixture's rig, as plain values: humanoid_rig.usda's seven joints.
const char* const kRoot = "Root";
const char* const kHips = "Root/J_Bip_C_Hips";
const char* const kSpine = "Root/J_Bip_C_Hips/J_Bip_C_Spine";
const char* const kChest = "Root/J_Bip_C_Hips/J_Bip_C_Spine/J_Bip_C_Chest";
const char* const kNeck = "Root/J_Bip_C_Hips/J_Bip_C_Spine/J_Bip_C_Chest/J_Bip_C_Neck";
const char* const kHead = "Root/J_Bip_C_Hips/J_Bip_C_Spine/J_Bip_C_Chest/J_Bip_C_Neck/J_Bip_C_Head";
const char* const kUpperArm = "Root/J_Bip_C_Hips/J_Bip_C_Spine/J_Bip_C_Chest/J_Bip_L_UpperArm";

// Turned 90 degrees about +Z and scaled by 2, in USD's row-vector convention:
// the first row is where +X goes (to +Y), the second where +Y goes (to -X).
pxr::GfMatrix4d
TurnedAndScaled()
{
    return pxr::GfMatrix4d(0, 2, 0, 0, -2, 0, 0, 0, 0, 0, 2, 0, 0.1, 0.15, 0, 1);
}

execvrm::SkeletonRest
FixtureRest()
{
    execvrm::SkeletonRest rest;
    rest.joints = {kRoot, kHips, kSpine, kChest, kNeck, kHead, kUpperArm};
    rest.restTransforms = {pxr::GfMatrix4d(1.0),  Translate(0, 1, 0),   Translate(0, 0.1, 0),
                           Translate(0, 0.15, 0), Translate(0, 0.2, 0), Translate(0, 0.1, 0),
                           TurnedAndScaled()};
    return rest;
}

vrmRetarget::TargetSkeleton
FixtureSkeleton()
{
    execvrm::SkeletonOutcome outcome = execvrm::TargetSkeletonFromRest(FixtureRest());
    assert(outcome.skeleton);
    return *outcome.skeleton;
}

std::vector<std::pair<HumanJoint, std::string>>
FixtureBindings()
{
    return {{HumanJoint::Hips, kHips},   {HumanJoint::Spine, kSpine},
            {HumanJoint::Chest, kChest}, {HumanJoint::Neck, kNeck},
            {HumanJoint::Head, kHead},   {HumanJoint::LeftUpperArm, kUpperArm}};
}

execvrm::HumanoidInputs
FixtureInputs()
{
    execvrm::HumanoidInputs inputs;
    inputs.skeletonTargetCount = 1;
    inputs.skeletons = {FixtureSkeleton()};
    inputs.bindings = FixtureBindings();
    return inputs;
}

// ---------------------------------------------------------------------------
// The attribute names
// ---------------------------------------------------------------------------
void
TestTheAttributeNamesAreTheVocabularysOwn()
{
    const auto& names = execvrm::HumanBoneAttributeNames();
    assert(names.size() == openstrata::motion::HumanJointCount);
    assert(names[static_cast<std::size_t>(HumanJoint::Hips)] == "vrm:humanBones:hips");
    assert(names[static_cast<std::size_t>(HumanJoint::LeftThumbMetacarpal)] ==
           "vrm:humanBones:leftThumbMetacarpal");
    assert(names[static_cast<std::size_t>(HumanJoint::RightLittleDistal)] ==
           "vrm:humanBones:rightLittleDistal");

    // One input is declared per entry, so two equal names would be one input
    // declared twice and a bone that could never be read.
    std::set<std::string> distinct;
    for (const auto& name : names)
    {
        distinct.insert(name.GetString());
    }
    assert(distinct.size() == openstrata::motion::HumanJointCount);
    std::printf("execVrm rig: %zu attribute names, one per bone\n", names.size());
}

// ---------------------------------------------------------------------------
// vrm.computeTargetSkeleton
// ---------------------------------------------------------------------------
void
TestTheSkeletonIsTheRestPoseDecomposed()
{
    const vrmRetarget::TargetSkeleton skeleton = FixtureSkeleton();
    const std::vector<vrmRetarget::TargetJoint>& joints = skeleton.GetJoints();
    assert(joints.size() == 7);

    // Tokens verbatim, in the skeleton's own order -- the order a map's indices
    // count into.
    assert(joints[0].token == kRoot && joints[6].token == kUpperArm);

    // Parents from the joint paths: the arm hangs off the chest, not the neck.
    const int parents[] = {-1, 0, 1, 2, 3, 4, 3};
    for (std::size_t i = 0; i < joints.size(); ++i)
    {
        assert(joints[i].parent == parents[i]);
    }
    assert(skeleton.IsTopologicallyOrdered());

    // A translation-only rest is an identity rotation and that translation.
    assert(joints[1].restRotation == pxr::GfQuatf(1.0f, pxr::GfVec3f(0.0f)));
    assert(NearlyEqual(joints[1].restTranslation, pxr::GfVec3f(0, 1, 0)));

    // 90 degrees about +Z, with the scale of 2 kept apart rather than folded
    // into the rotation: (cos 45, 0, 0, sin 45), the translation off the
    // matrix, and the scale as its own field.
    const float half = std::sqrt(0.5f);
    assert(NearlyEqual(joints[6].restRotation, pxr::GfQuatf(half, 0.0f, 0.0f, half)) &&
           "the scaled rest transform did not decompose to its rotation");
    assert(NearlyEqual(joints[6].restTranslation, pxr::GfVec3f(0.1f, 0.15f, 0.0f)));
    assert(NearlyEqual(joints[6].restScale, pxr::GfVec3f(2.0f)) &&
           "the scaled rest transform did not decompose to its scale");
    assert(joints[1].restScale == pxr::GfVec3f(1.0f));
    std::printf("execVrm rig: the rest pose decomposes, scale apart\n");
}

void
TestARestPoseThatDoesNotPairIsRefused()
{
    // Absent: an attribute the skeleton does not carry arrives as no matrices.
    execvrm::SkeletonRest absent = FixtureRest();
    absent.restTransforms.clear();
    execvrm::SkeletonOutcome outcome = execvrm::TargetSkeletonFromRest(absent);
    assert(!outcome.skeleton && "a skeleton with no rest pose was given one -- an identity rest is "
                                "numbers nobody can tell from measured ones");
    assert(outcome.refusal == execvrm::SkeletonRefusal::RestTransformCount);

    // Short by one.
    execvrm::SkeletonRest shortBy = FixtureRest();
    shortBy.restTransforms.pop_back();
    assert(!execvrm::TargetSkeletonFromRest(shortBy).skeleton);

    // Long by one.
    execvrm::SkeletonRest longBy = FixtureRest();
    longBy.restTransforms.push_back(pxr::GfMatrix4d(1.0));
    assert(!execvrm::TargetSkeletonFromRest(longBy).skeleton);
    std::printf("execVrm rig: a rest pose that does not pair is refused\n");
}

void
TestNoJointsIsAnAnswer()
{
    // A skeleton with no joints, and so no rest transforms, is an empty
    // skeleton -- a value no skeleton that has a joint could be mistaken for.
    execvrm::SkeletonOutcome outcome = execvrm::TargetSkeletonFromRest(execvrm::SkeletonRest{});
    assert(outcome.skeleton && outcome.skeleton->IsEmpty());

    // Whatever the rest transforms say: with no joint for one to belong to,
    // none can become a number. The first is what exec delivers for
    // `joints = []` beside an unauthored `restTransforms` -- one fallback
    // matrix -- and it is not a count the stage stated.
    execvrm::SkeletonRest fallback;
    fallback.restTransforms = {pxr::GfMatrix4d(1.0)};
    outcome = execvrm::TargetSkeletonFromRest(fallback);
    assert(outcome.skeleton && outcome.skeleton->IsEmpty() &&
           "joints = [] beside an unauthored restTransforms was refused");

    execvrm::SkeletonRest stray;
    stray.restTransforms = {Translate(0, 1, 0), Translate(0, 2, 0)};
    outcome = execvrm::TargetSkeletonFromRest(stray);
    assert(outcome.skeleton && outcome.skeleton->IsEmpty());
    std::printf("execVrm rig: a skeleton with no joints is an empty skeleton, "
                "whatever its rest transforms say\n");
}

void
TestAnEmptyJointTokenIsRefused()
{
    // What exec hands over for a skeleton that authors no `joints` at all: one
    // fallback token, and one fallback matrix beside it. The counts pair, so
    // only the token can say it.
    execvrm::SkeletonRest unauthored;
    unauthored.joints = {""};
    unauthored.restTransforms = {pxr::GfMatrix4d(1.0)};
    execvrm::SkeletonOutcome outcome = execvrm::TargetSkeletonFromRest(unauthored);
    assert(!outcome.skeleton && "a skeleton that authors no joints came back as one joint named "
                                "nothing");
    assert(outcome.refusal == execvrm::SkeletonRefusal::EmptyJointToken);

    // And anywhere in the list, not only alone.
    execvrm::SkeletonRest inside = FixtureRest();
    inside.joints[3].clear();
    assert(execvrm::TargetSkeletonFromRest(inside).refusal ==
           execvrm::SkeletonRefusal::EmptyJointToken);
    std::printf("execVrm rig: an empty joint token is refused\n");
}

void
TestAnUnorderedSkeletonIsCarriedFaithfully()
{
    // Child before parent. The value carries it and says so; refusing it would
    // be a policy the retargeter does not have.
    execvrm::SkeletonRest rest;
    rest.joints = {kHips, kRoot};
    rest.restTransforms = {Translate(0, 1, 0), pxr::GfMatrix4d(1.0)};
    execvrm::SkeletonOutcome outcome = execvrm::TargetSkeletonFromRest(rest);
    assert(outcome.skeleton);
    assert(outcome.skeleton->GetJoints()[0].parent == 1);
    assert(!outcome.skeleton->IsTopologicallyOrdered());
    std::printf("execVrm rig: an unordered skeleton is carried and reported\n");
}

// ---------------------------------------------------------------------------
// vrm.computeHumanoidMap
// ---------------------------------------------------------------------------
void
TestTheMapIsTheLibrarysBindings()
{
    const execvrm::MapOutcome outcome = execvrm::HumanoidMapFor(FixtureInputs());
    assert(outcome.map);
    const vrmRetarget::HumanoidMap& map = *outcome.map;

    // The wrapper claim: the node's map IS `SetJointToken` over the bindings,
    // against the same skeleton -- so the library's map is the expected value.
    vrmRetarget::HumanoidMap expected;
    const vrmRetarget::TargetSkeleton skeleton = FixtureSkeleton();
    for (const auto& [bone, token] : FixtureBindings())
    {
        assert(expected.SetJointToken(bone, token, skeleton));
    }
    assert(map == expected);

    // And the indices themselves, from the fixture's joint order.
    assert(map.GetMappedCount() == 6);
    assert(map.GetJointIndex(HumanJoint::Hips) == 1);
    assert(map.GetJointIndex(HumanJoint::Head) == 5);
    assert(map.GetJointIndex(HumanJoint::LeftUpperArm) == 6);
    assert(!map.IsMapped(HumanJoint::RightUpperArm));

    // Eleven of VRM 1.0's seventeen required bones are unbound, and the value
    // says so rather than the node refusing for it.
    const std::vector<HumanJoint> missing = map.FindMissingRequiredBones();
    assert(missing.size() == 11);
    assert(std::find(missing.begin(), missing.end(), HumanJoint::LeftUpperLeg) != missing.end());
    std::printf("execVrm rig: the map is SetJointToken over the bindings, and "
                "carries its own gaps\n");
}

void
TestTheSkeletonTargetIsCounted()
{
    execvrm::HumanoidInputs none = FixtureInputs();
    none.skeletonTargetCount = 0;
    none.skeletons.clear();
    execvrm::MapOutcome outcome = execvrm::HumanoidMapFor(none);
    assert(!outcome.map && outcome.refusal == execvrm::MapRefusal::NoSkeleton);

    execvrm::HumanoidInputs two = FixtureInputs();
    two.skeletonTargetCount = 2;
    two.skeletons.push_back(FixtureSkeleton());
    outcome = execvrm::HumanoidMapFor(two);
    assert(!outcome.map && outcome.refusal == execvrm::MapRefusal::SeveralSkeletons);

    // Two targets, one answering: still two targets. The count is decided on
    // what the relationship reaches, before what came back.
    execvrm::HumanoidInputs twoOneAnswered = FixtureInputs();
    twoOneAnswered.skeletonTargetCount = 2;
    outcome = execvrm::HumanoidMapFor(twoOneAnswered);
    assert(!outcome.map && outcome.refusal == execvrm::MapRefusal::SeveralSkeletons);

    // One target, nothing back: the fan-in dropped it.
    execvrm::HumanoidInputs dropped = FixtureInputs();
    dropped.skeletons.clear();
    outcome = execvrm::HumanoidMapFor(dropped);
    assert(!outcome.map && outcome.refusal == execvrm::MapRefusal::SkeletonUnanswered);
    std::printf("execVrm rig: exactly one skeleton, counted twice\n");
}

void
TestABindingToNoJointIsRefusedAndNamed()
{
    execvrm::HumanoidInputs inputs = FixtureInputs();
    // The leaf name a heuristic would try: not a joint path of this skeleton.
    inputs.bindings[0].second = "J_Bip_C_Hips";
    inputs.bindings[4].second = "Root/Head";
    const execvrm::MapOutcome outcome = execvrm::HumanoidMapFor(inputs);
    assert(!outcome.map && "a binding the skeleton cannot honour was dropped instead -- a map "
                           "without the bone reads as a humanoid that never named it");
    assert(outcome.refusal == execvrm::MapRefusal::UnknownJoint);
    assert(outcome.offending.size() == 2);
    assert(outcome.offending[0] == std::make_pair(HumanJoint::Hips, std::string("J_Bip_C_Hips")));
    assert(outcome.offending[1] == std::make_pair(HumanJoint::Head, std::string("Root/Head")));
    std::printf("execVrm rig: a binding to no joint is refused, by bone\n");
}

void
TestTwoBonesOnOneJointAreRefusedAndBothNamed()
{
    execvrm::HumanoidInputs inputs = FixtureInputs();
    inputs.bindings.emplace_back(HumanJoint::UpperChest, kChest);
    const execvrm::MapOutcome outcome = execvrm::HumanoidMapFor(inputs);
    assert(!outcome.map && "two bones on one joint were kept, and a retarget would let one "
                           "silently win");
    assert(outcome.refusal == execvrm::MapRefusal::DuplicateJoint);
    assert(outcome.offending.size() == 2);
    assert(outcome.offending[0].first == HumanJoint::Chest);
    assert(outcome.offending[1].first == HumanJoint::UpperChest);
    std::printf("execVrm rig: two bones on one joint are refused, both named\n");
}

void
TestAnEmptyTokenBindsNothing()
{
    execvrm::HumanoidInputs inputs = FixtureInputs();
    inputs.bindings[5].second.clear();
    const execvrm::MapOutcome outcome = execvrm::HumanoidMapFor(inputs);
    assert(outcome.map && "an empty token was refused -- it names no joint, which is not a "
                          "misspelling of one");
    assert(outcome.map->GetMappedCount() == 5);
    assert(!outcome.map->IsMapped(HumanJoint::LeftUpperArm));

    // And a humanoid stating nothing at all is the empty map.
    execvrm::HumanoidInputs nothing = FixtureInputs();
    nothing.bindings.clear();
    const execvrm::MapOutcome empty = execvrm::HumanoidMapFor(nothing);
    assert(empty.map && empty.map->GetMappedCount() == 0);
    std::printf("execVrm rig: an empty token binds nothing, and no bindings "
                "is the empty map\n");
}

// ---------------------------------------------------------------------------
// vrm.computeRestPoseCorrection
// ---------------------------------------------------------------------------

pxr::GfQuatf
About(const pxr::GfVec3f& axis, float degrees)
{
    const float half = degrees * 3.14159265358979324f / 360.0f;
    return pxr::GfQuatf(std::cos(half), axis * std::sin(half));
}

// A matrix with a rotation and a translation, row-vector convention.
pxr::GfMatrix4d
Rest(const pxr::GfQuatf& rotation, const pxr::GfVec3d& at)
{
    pxr::GfMatrix4d matrix(1.0);
    matrix.SetRotate(pxr::GfQuatd(rotation.GetReal(), pxr::GfVec3d(rotation.GetImaginary())));
    matrix.SetTranslateOnly(at);
    return matrix;
}

// A semantic skeleton, the shape usdVrmaFileFormat authors under a reference
// joint that is no bone: turned rests on the hips and the arm, and the
// reference's own rest turned too, which the source rest pose has no slot for.
vrmRetarget::TargetSkeleton
SemanticSkeleton()
{
    execvrm::SkeletonRest rest;
    rest.joints = {"Reference", "Reference/hips", "Reference/hips/spine",
                   "Reference/hips/spine/chest", "Reference/hips/spine/chest/leftUpperArm"};
    rest.restTransforms = {Rest(About(pxr::GfVec3f(1, 0, 0), 90.0f), pxr::GfVec3d(0.0)),
                           Rest(About(pxr::GfVec3f(0, 1, 0), 30.0f), pxr::GfVec3d(0, 0.9, 0)),
                           Translate(0, 0.1, 0), Translate(0, 0.15, 0),
                           Rest(About(pxr::GfVec3f(0, 0, 1), -90.0f), pxr::GfVec3d(0.1, 0.15, 0))};
    execvrm::SkeletonOutcome outcome = execvrm::TargetSkeletonFromRest(rest);
    assert(outcome.skeleton);
    return *outcome.skeleton;
}

// What SemanticSkeleton states, written from its definition.
vrmRetarget::SourceRestPose
SemanticRest()
{
    vrmRetarget::SourceRestPose rest;
    const auto hips = static_cast<std::size_t>(HumanJoint::Hips);
    const auto arm = static_cast<std::size_t>(HumanJoint::LeftUpperArm);
    rest.localRotations[hips] = About(pxr::GfVec3f(0, 1, 0), 30.0f);
    rest.localTranslations[hips] = pxr::GfVec3f(0, 0.9f, 0);
    rest.localTranslations[static_cast<std::size_t>(HumanJoint::Spine)] = pxr::GfVec3f(0, 0.1f, 0);
    rest.localTranslations[static_cast<std::size_t>(HumanJoint::Chest)] = pxr::GfVec3f(0, 0.15f, 0);
    rest.localRotations[arm] = About(pxr::GfVec3f(0, 0, 1), -90.0f);
    rest.localTranslations[arm] = pxr::GfVec3f(0.1f, 0.15f, 0);
    rest.SetParent(HumanJoint::Spine, HumanJoint::Hips);
    rest.SetParent(HumanJoint::Chest, HumanJoint::Spine);
    rest.SetParent(HumanJoint::LeftUpperArm, HumanJoint::Chest);
    return rest;
}

// Orientation, not representation: a rest decomposed off a matrix may come
// back as -q (the -90 degree arm does -- GfMatrix4d::ExtractRotationQuat picks
// its sign by branch), and q and -q rest identically.
bool
SameOrientation(const pxr::GfQuatf& a, const pxr::GfQuatf& b)
{
    const pxr::GfQuatf na = a.GetNormalized();
    const pxr::GfQuatf nb = b.GetNormalized();
    return std::abs(std::abs(pxr::GfDot(na, nb)) - 1.0f) <= 1e-6f;
}

bool
SameRest(const vrmRetarget::SourceRestPose& a, const vrmRetarget::SourceRestPose& b)
{
    for (std::size_t slot = 0; slot < openstrata::motion::HumanJointCount; ++slot)
    {
        if (!SameOrientation(a.localRotations[slot], b.localRotations[slot]) ||
            !NearlyEqual(a.localTranslations[slot], b.localTranslations[slot]) ||
            a.parents[slot] != b.parents[slot])
        {
            std::fprintf(stderr, "the rests differ at %s\n",
                         std::string(openstrata::motion::HumanJointName(static_cast<HumanJoint>(slot))).c_str());
            return false;
        }
    }
    return true;
}

void
TestTheSourceRestIsReadOffTheSemanticSkeleton()
{
    const execvrm::SourceRestOutcome outcome = execvrm::SourceRestFromSkeleton(SemanticSkeleton());
    assert(outcome.rest);
    // Every slot, including the ones nothing named: identity, at the origin,
    // a root. The reference joint is in none of them, and the hips are a root
    // of the semantic chain because the reference is no bone.
    assert(SameRest(*outcome.rest, SemanticRest()) &&
           "the clip's rest pose is not what its skeleton states");
    assert(outcome.rest->parents[static_cast<std::size_t>(HumanJoint::Hips)] ==
           vrmRetarget::SourceRestPose::kNoParent);
    std::printf("execVrm rig: the clip's rest is read off its skeleton by "
                "leaf, a non-bone joint in no slot\n");
}

void
TestTheSourceParentIsTheParentPathsLeaf()
{
    // The tool's rule: the parent PATH's leaf, whether or not a joint of the
    // skeleton resolves the path. `Reference/hips` is no joint here, and the
    // spine is still parented to the hips.
    execvrm::SkeletonRest rest;
    rest.joints = {"hips", "Reference/hips/spine"};
    rest.restTransforms = {Translate(0, 1, 0), Translate(0, 0.1, 0)};
    const execvrm::SourceRestOutcome outcome =
        execvrm::SourceRestFromSkeleton(*execvrm::TargetSkeletonFromRest(rest).skeleton);
    assert(outcome.rest);
    assert(outcome.rest->parents[static_cast<std::size_t>(HumanJoint::Spine)] ==
           static_cast<std::size_t>(HumanJoint::Hips));
    std::printf("execVrm rig: a source bone's parent is its parent path's "
                "leaf\n");
}

void
TestASourceThatIsNotSemanticIsRefused()
{
    // The fixture's own rig, VRoid-named: a skeleton, and no leaf a bone.
    execvrm::SourceRestOutcome outcome = execvrm::SourceRestFromSkeleton(FixtureSkeleton());
    assert(!outcome.rest && "a skeleton naming no bone was read as an identity rest");
    assert(outcome.refusal == execvrm::SourceRestRefusal::NoHumanBone);

    // And the empty skeleton, for the same reason.
    outcome = execvrm::SourceRestFromSkeleton(vrmRetarget::TargetSkeleton());
    assert(!outcome.rest && outcome.refusal == execvrm::SourceRestRefusal::NoHumanBone);
    std::printf("execVrm rig: a source naming no bone is refused\n");
}

void
TestASourceNamingOneBoneTwiceIsRefusedAndBothNamed()
{
    execvrm::SkeletonRest rest;
    rest.joints = {"hips", "hips/spine", "Reference", "Reference/hips", "Reference/spine"};
    rest.restTransforms = std::vector<pxr::GfMatrix4d>(5, pxr::GfMatrix4d(1.0));
    const execvrm::SourceRestOutcome outcome =
        execvrm::SourceRestFromSkeleton(*execvrm::TargetSkeletonFromRest(rest).skeleton);
    assert(!outcome.rest && "two rests for one bone were resolved by keeping one of them");
    assert(outcome.refusal == execvrm::SourceRestRefusal::DuplicateBone);
    const std::vector<std::pair<HumanJoint, std::string>> expected = {
        {HumanJoint::Hips, "hips"},
        {HumanJoint::Hips, "Reference/hips"},
        {HumanJoint::Spine, "hips/spine"},
        {HumanJoint::Spine, "Reference/spine"}};
    assert(outcome.offending == expected);
    std::printf("execVrm rig: a source naming one bone twice is refused, "
                "every joint named\n");
}

execvrm::CorrectionInputs
CorrectionFixture(const vrmRetarget::HumanoidMap& map)
{
    execvrm::CorrectionInputs inputs;
    inputs.map = &map;
    inputs.targets = {FixtureSkeleton()};
    inputs.sourceTargetCount = 1;
    inputs.sources = {SemanticSkeleton()};
    return inputs;
}

void
TestTheCorrectionIsTheLibrarysCall()
{
    const vrmRetarget::HumanoidMap map = *execvrm::HumanoidMapFor(FixtureInputs()).map;
    const execvrm::CorrectionOutcome outcome =
        execvrm::RestPoseCorrectionFor(CorrectionFixture(map));
    assert(outcome.correction);

    // The wrapper claim: the library's correction over the rest the source
    // states -- written from its definition, not read back through the seam --
    // the same rig, and the same map.
    const vrmRetarget::RestPoseCorrection expected =
        vrmRetarget::ComputeRestPoseCorrection(SemanticRest(), FixtureSkeleton(), map);
    for (std::size_t slot = 0; slot < openstrata::motion::HumanJointCount; ++slot)
    {
        assert(outcome.correction->identity[slot] == expected.identity[slot]);
        assert(SameOrientation(outcome.correction->pre[slot], expected.pre[slot]));
        assert(SameOrientation(outcome.correction->post[slot], expected.post[slot]));
    }

    // And bit for bit when the seam's own reading is what the library is
    // handed -- the node is that call and nothing more.
    assert(*outcome.correction ==
           vrmRetarget::ComputeRestPoseCorrection(
               *execvrm::SourceRestFromSkeleton(SemanticSkeleton()).rest, FixtureSkeleton(), map));

    // A sample at the source's rest lands on the rig's rest -- the arm's -90 Z
    // onto its +90 Z -- and a bone the map does not bind stays identity.
    const float half = std::sqrt(0.5f);
    const pxr::GfQuatf landed =
        outcome.correction->Apply(HumanJoint::LeftUpperArm, About(pxr::GfVec3f(0, 0, 1), -90.0f));
    assert(SameOrientation(landed, pxr::GfQuatf(half, 0.0f, 0.0f, half)));
    assert(outcome.correction->identity[static_cast<std::size_t>(HumanJoint::RightUpperArm)]);
    std::printf("execVrm rig: the correction is ComputeRestPoseCorrection over "
                "the source's rest, the rig and the map\n");
}

void
TestTheCorrectionRefusesWhatItCannotHonour()
{
    const vrmRetarget::HumanoidMap map = *execvrm::HumanoidMapFor(FixtureInputs()).map;

    auto refusal = [](const execvrm::CorrectionInputs& inputs)
    {
        const execvrm::CorrectionOutcome outcome = execvrm::RestPoseCorrectionFor(inputs);
        assert(!outcome.correction);
        return outcome.refusal;
    };
    using execvrm::CorrectionRefusal;

    // The map refused, or its skeleton did not come back.
    execvrm::CorrectionInputs noMap = CorrectionFixture(map);
    noMap.map = nullptr;
    assert(refusal(noMap) == CorrectionRefusal::RigUnanswered);
    execvrm::CorrectionInputs noTarget = CorrectionFixture(map);
    noTarget.targets.clear();
    assert(refusal(noTarget) == CorrectionRefusal::RigUnanswered);

    // Nothing named as the source: refused, not defaulted to identity.
    execvrm::CorrectionInputs none = CorrectionFixture(map);
    none.sourceTargetCount = 0;
    none.sources.clear();
    assert(refusal(none) == CorrectionRefusal::NoSource);

    execvrm::CorrectionInputs two = CorrectionFixture(map);
    two.sourceTargetCount = 2;
    assert(refusal(two) == CorrectionRefusal::SeveralSources);

    execvrm::CorrectionInputs dropped = CorrectionFixture(map);
    dropped.sources.clear();
    assert(refusal(dropped) == CorrectionRefusal::SourceUnanswered);

    execvrm::CorrectionInputs notSemantic = CorrectionFixture(map);
    notSemantic.sources = {FixtureSkeleton()};
    const execvrm::CorrectionOutcome outcome = execvrm::RestPoseCorrectionFor(notSemantic);
    assert(!outcome.correction && outcome.refusal == CorrectionRefusal::SourceRest &&
           outcome.sourceRefusal == execvrm::SourceRestRefusal::NoHumanBone);
    std::printf("execVrm rig: the correction refuses a rig that did not "
                "answer, no source, two, one that did not answer and one that "
                "is not semantic\n");
}

// ---------------------------------------------------------------------------
// vrm.computeBoundPose
// ---------------------------------------------------------------------------

void
TestTheBoundPoseIsTheOnePoseForwarded()
{
    openstrata::motion::MotionPose pose;
    pose.timestamp = 0.5;
    pose.localRotations[static_cast<std::size_t>(HumanJoint::Head)] =
        About(pxr::GfVec3f(0, 1, 0), 20.0f);
    pose.validRotations.set(static_cast<std::size_t>(HumanJoint::Head));

    execvrm::BoundPoseInputs inputs;
    inputs.animationTargetCount = 1;
    inputs.poses = {pose};
    execvrm::BoundPoseOutcome outcome = execvrm::BoundPoseFor(inputs);
    assert(outcome.pose && *outcome.pose == pose);

    // An empty pose that CAME BACK is an answer: a clip naming no bone samples
    // to one. What is refused is a binding that reaches no animation.
    inputs.poses = {openstrata::motion::MotionPose()};
    outcome = execvrm::BoundPoseFor(inputs);
    assert(outcome.pose && *outcome.pose == openstrata::motion::MotionPose());

    using execvrm::BoundPoseRefusal;
    inputs.animationTargetCount = 0;
    inputs.poses.clear();
    outcome = execvrm::BoundPoseFor(inputs);
    assert(!outcome.pose && outcome.refusal == BoundPoseRefusal::NoAnimation);

    inputs.animationTargetCount = 2;
    inputs.poses = {pose};
    outcome = execvrm::BoundPoseFor(inputs);
    assert(!outcome.pose && outcome.refusal == BoundPoseRefusal::SeveralAnimations);

    inputs.animationTargetCount = 1;
    inputs.poses.clear();
    outcome = execvrm::BoundPoseFor(inputs);
    assert(!outcome.pose && outcome.refusal == BoundPoseRefusal::AnimationUnanswered);

    // UsdSkel's order: nothing bound here is what an ancestor binds, and an
    // own binding shadows it -- including one that is broken, which is
    // refused rather than passed over.
    openstrata::motion::MotionPose ancestor;
    ancestor.timestamp = 0.25;
    inputs.animationTargetCount = 0;
    inputs.poses.clear();
    inputs.inherited = &ancestor;
    outcome = execvrm::BoundPoseFor(inputs);
    assert(outcome.pose && *outcome.pose == ancestor);
    inputs.animationTargetCount = 1;
    inputs.poses = {pose};
    outcome = execvrm::BoundPoseFor(inputs);
    assert(outcome.pose && *outcome.pose == pose &&
           "an ancestor's binding won over the prim's own");
    inputs.poses.clear();
    outcome = execvrm::BoundPoseFor(inputs);
    assert(!outcome.pose && outcome.refusal == BoundPoseRefusal::AnimationUnanswered &&
           "a broken own binding fell through to the ancestor's");
    std::printf("execVrm rig: the bound pose is the one pose forwarded, else "
                "the ancestor's, and no animation, two, or one that answered "
                "nothing is refused\n");
}

// ---------------------------------------------------------------------------
// vrm.humanoidRetarget's root-motion statements
// ---------------------------------------------------------------------------

void
TestTheRootMotionOptionsAreTheToolsFlags()
{
    const vrmRetarget::TargetSkeleton rig = FixtureSkeleton();
    using vrmRetarget::RootMotionMode;

    // Nothing stated is the library's default, field for field.
    execvrm::RootMotionOutcome outcome =
        execvrm::RootMotionOptionsFor(execvrm::RootMotionStatements(), rig);
    assert(outcome.options);
    const vrmRetarget::RootMotionOptions defaults;
    assert(outcome.options->mode == defaults.mode);
    assert(outcome.options->rootJointIndex == defaults.rootJointIndex);
    assert(outcome.options->translationScale == defaults.translationScale);
    assert(outcome.options->preserveTargetHeight == defaults.preserveTargetHeight);

    auto mode = [&rig](const char* stated)
    {
        execvrm::RootMotionStatements statements;
        statements.mode = stated;
        statements.rootJoint = kRoot;
        const execvrm::RootMotionOutcome o = execvrm::RootMotionOptionsFor(statements, rig);
        assert(o.options);
        return *o.options;
    };
    assert(mode("hips").mode == RootMotionMode::Hips);
    assert(mode("ignore").mode == RootMotionMode::Ignore);
    const vrmRetarget::RootMotionOptions root = mode("root");
    assert(root.mode == RootMotionMode::RootJoint && root.rootJointIndex == 0);
    // Under any mode but root the joint is not read -- as `--root-joint` is
    // not -- so the index stays the library's "none".
    assert(mode("hips").rootJointIndex == -1);

    execvrm::RootMotionStatements scaled;
    scaled.translationScale = 2.0f;
    scaled.preserveTargetHeight = true;
    // A joint no rig has, stated beside a mode that does not read it.
    scaled.rootJoint = "NoSuchJoint";
    outcome = execvrm::RootMotionOptionsFor(scaled, rig);
    assert(outcome.options && outcome.options->translationScale == 2.0f &&
           outcome.options->preserveTargetHeight && outcome.options->mode == RootMotionMode::Hips);
    std::printf("execVrm rig: the root-motion statements are motion_retarget's "
                "flags, and nothing stated is the library's default\n");
}

void
TestTheRootMotionOptionsRefuseWhatTheToolRefuses()
{
    const vrmRetarget::TargetSkeleton rig = FixtureSkeleton();
    using execvrm::RootMotionRefusal;

    auto refusal = [&rig](const execvrm::RootMotionStatements& statements)
    {
        const execvrm::RootMotionOutcome outcome = execvrm::RootMotionOptionsFor(statements, rig);
        assert(!outcome.options);
        return outcome.refusal;
    };

    execvrm::RootMotionStatements statements;
    // The vocabulary is exact: no case folding, and the empty token -- what a
    // valueless attribute arrives as -- names no mode.
    statements.mode = "Hips";
    assert(refusal(statements) == RootMotionRefusal::UnknownMode);
    statements.mode = "";
    assert(refusal(statements) == RootMotionRefusal::UnknownMode);

    statements.mode = "root";
    assert(refusal(statements) == RootMotionRefusal::NoRootJoint);
    statements.rootJoint = "";
    assert(refusal(statements) == RootMotionRefusal::NoRootJoint);
    // Exact on the full path, as `--root-joint` is: a leaf is not a joint.
    statements.rootJoint = "J_Bip_C_Hips";
    assert(refusal(statements) == RootMotionRefusal::UnknownRootJoint);

    execvrm::RootMotionStatements scale;
    scale.translationScale = std::numeric_limits<float>::infinity();
    assert(refusal(scale) == RootMotionRefusal::TranslationScale);
    scale.translationScale = std::numeric_limits<float>::quiet_NaN();
    assert(refusal(scale) == RootMotionRefusal::TranslationScale);

    // A scale of zero is a value: the tool accepts one, and so does this.
    scale.translationScale = 0.0f;
    assert(execvrm::RootMotionOptionsFor(scale, rig).options);
    std::printf("execVrm rig: an unknown mode, root with no joint or an unknown "
                "one, and a scale that is no finite number are refused\n");
}

// ---------------------------------------------------------------------------
// vrm.humanoidRetarget
// ---------------------------------------------------------------------------

// A sample of the semantic clip: the spine and arm turned, the hips moved, and
// one bone the fixture's humanoid does not bind.
openstrata::motion::MotionPose
ClipSample()
{
    openstrata::motion::MotionPose pose;
    pose.timestamp = 0.75;
    const auto set = [&pose](HumanJoint bone, const pxr::GfQuatf& rotation)
    {
        pose.localRotations[static_cast<std::size_t>(bone)] = rotation;
        pose.validRotations.set(static_cast<std::size_t>(bone));
    };
    set(HumanJoint::Hips, About(pxr::GfVec3f(0, 1, 0), 30.0f));
    set(HumanJoint::Spine, About(pxr::GfVec3f(1, 0, 0), 30.0f));
    set(HumanJoint::LeftUpperArm, About(pxr::GfVec3f(0, 0, 1), 20.0f));
    set(HumanJoint::RightUpperArm, About(pxr::GfVec3f(0, 0, 1), -20.0f));
    pose.root.worldPosition = pxr::GfVec3f(0.3f, 1.0f, 0.1f);
    pose.root.hasPosition = true;
    return pose;
}

execvrm::RetargetInputs
RetargetFixture(const vrmRetarget::HumanoidMap& map)
{
    execvrm::RetargetInputs inputs;
    inputs.map = &map;
    inputs.targets = {FixtureSkeleton()};
    inputs.sourceTargetCount = 1;
    inputs.sources = {SemanticSkeleton()};
    inputs.poses = {ClipSample()};
    return inputs;
}

void
TestTheRetargetIsThePoseRetargetersCall()
{
    const vrmRetarget::HumanoidMap map = *execvrm::HumanoidMapFor(FixtureInputs()).map;
    const vrmRetarget::SourceRestPose rest =
        *execvrm::SourceRestFromSkeleton(SemanticSkeleton()).rest;

    // The wrapper claim, with the library's default options: bit for bit.
    execvrm::RetargetOutcome outcome = execvrm::HumanoidRetargetFor(RetargetFixture(map));
    assert(outcome.pose);
    assert(*outcome.pose ==
               vrmRetarget::PoseRetargeter(FixtureSkeleton(), map, rest).Retarget(ClipSample()) &&
           "the retarget is not PoseRetargeter over the same values");
    assert(outcome.pose->timestamp == 0.75);

    // And with every statement made, against the options the tool would have
    // built from the same four flags.
    execvrm::RetargetInputs stated = RetargetFixture(map);
    stated.rootMotion.mode = "root";
    stated.rootMotion.rootJoint = kRoot;
    stated.rootMotion.translationScale = 2.0f;
    stated.rootMotion.preserveTargetHeight = true;
    vrmRetarget::RetargetOptions options;
    options.rootMotion.mode = vrmRetarget::RootMotionMode::RootJoint;
    options.rootMotion.rootJointIndex = 0;
    options.rootMotion.translationScale = 2.0f;
    options.rootMotion.preserveTargetHeight = true;
    outcome = execvrm::HumanoidRetargetFor(stated);
    assert(outcome.pose &&
           *outcome.pose == vrmRetarget::PoseRetargeter(FixtureSkeleton(), map, rest, options)
                                .Retarget(ClipSample()));

    // The cost the node reports: what it applies to each mapped bone is the
    // correction vrm.computeRestPoseCorrection computes from the same inputs,
    // exactly -- computed again, here, because PoseRetargeter takes none.
    const vrmRetarget::RestPoseCorrection cached =
        *execvrm::RestPoseCorrectionFor(CorrectionFixture(map)).correction;
    outcome = execvrm::HumanoidRetargetFor(RetargetFixture(map));
    const openstrata::motion::MotionPose sample = ClipSample();
    for (const auto& [bone, token] : FixtureBindings())
    {
        const int joint = map.GetJointIndex(bone);
        const auto slot = static_cast<std::size_t>(bone);
        if (!sample.validRotations.test(slot))
        {
            continue;
        }
        assert(outcome.pose->rotations[static_cast<std::size_t>(joint)] ==
               cached.Apply(bone, sample.localRotations[slot]));
    }
    std::printf("execVrm rig: the retarget is PoseRetargeter over the rig, the "
                "map, the clip's rest and the statements, and applies the "
                "correction the correction node caches\n");
}

void
TestTheRetargetRefusesInItsOrder()
{
    const vrmRetarget::HumanoidMap map = *execvrm::HumanoidMapFor(FixtureInputs()).map;
    using execvrm::RetargetRefusal;

    auto refusal = [](const execvrm::RetargetInputs& inputs)
    {
        const execvrm::RetargetOutcome outcome = execvrm::HumanoidRetargetFor(inputs);
        assert(!outcome.pose);
        return outcome.refusal;
    };

    execvrm::RetargetInputs noMap = RetargetFixture(map);
    noMap.map = nullptr;
    assert(refusal(noMap) == RetargetRefusal::RigUnanswered);

    // The humanoid's own statements come before the source's, so a request
    // armed on a stage with both wrong says the nearer one first.
    execvrm::RetargetInputs statement = RetargetFixture(map);
    statement.rootMotion.mode = "sideways";
    statement.sourceTargetCount = 0;
    statement.sources.clear();
    const execvrm::RetargetOutcome o = execvrm::HumanoidRetargetFor(statement);
    assert(!o.pose && o.refusal == RetargetRefusal::RootMotion &&
           o.rootMotionRefusal == execvrm::RootMotionRefusal::UnknownMode);

    // The correction's three source refusals, for the correction's reasons.
    execvrm::RetargetInputs none = RetargetFixture(map);
    none.sourceTargetCount = 0;
    none.sources.clear();
    none.poses.clear();
    assert(refusal(none) == RetargetRefusal::NoSource);
    execvrm::RetargetInputs two = RetargetFixture(map);
    two.sourceTargetCount = 2;
    assert(refusal(two) == RetargetRefusal::SeveralSources);
    execvrm::RetargetInputs dropped = RetargetFixture(map);
    dropped.sources.clear();
    assert(refusal(dropped) == RetargetRefusal::SourceUnanswered);
    execvrm::RetargetInputs notSemantic = RetargetFixture(map);
    notSemantic.sources = {FixtureSkeleton()};
    const execvrm::RetargetOutcome rest = execvrm::HumanoidRetargetFor(notSemantic);
    assert(!rest.pose && rest.refusal == RetargetRefusal::SourceRest &&
           rest.sourceRefusal == execvrm::SourceRestRefusal::NoHumanBone);

    // A skeleton that answered and a pose that did not.
    execvrm::RetargetInputs noPose = RetargetFixture(map);
    noPose.poses.clear();
    assert(refusal(noPose) == RetargetRefusal::PoseUnanswered);

    // The default time code, last: after every statement, so arming a request
    // there still reports what the stage gets wrong.
    execvrm::RetargetInputs noInstant = RetargetFixture(map);
    noInstant.hasInstant = false;
    assert(refusal(noInstant) == RetargetRefusal::NoInstant);
    noInstant.poses.clear();
    assert(refusal(noInstant) == RetargetRefusal::PoseUnanswered);
    noInstant = RetargetFixture(map);
    noInstant.hasInstant = false;
    noInstant.rootMotion.mode = "root";
    assert(refusal(noInstant) == RetargetRefusal::RootMotion);
    std::printf("execVrm rig: the retarget refuses a rig, a statement, a source "
                "and a pose that did not answer, and the default time code, in "
                "that order\n");
}

// ---------------------------------------------------------------------------
// The retarget as an animation sample
// ---------------------------------------------------------------------------

execvrm::JointTransformsInputs
JointTransformsFixture(const vrmRetarget::RetargetedPose& pose)
{
    execvrm::JointTransformsInputs inputs;
    inputs.pose = &pose;
    inputs.targets = {FixtureSkeleton()};
    return inputs;
}

void
TestTheSampleIsTheRetargetWithTheBakesTwoAdditions()
{
    const vrmRetarget::HumanoidMap map = *execvrm::HumanoidMapFor(FixtureInputs()).map;
    const vrmRetarget::RetargetedPose pose =
        *execvrm::HumanoidRetargetFor(RetargetFixture(map)).pose;

    const execvrm::JointTransformsOutcome outcome =
        execvrm::JointLocalTransformsFor(JointTransformsFixture(pose));
    assert(outcome.sample);
    const vrmRetarget::JointLocalTransforms& sample = *outcome.sample;

    // Not a second retarget: the arrays and the timestamp pass through, bit
    // for bit.
    assert(sample.timestamp == pose.timestamp);
    assert(sample.rotations == pose.rotations);
    assert(sample.translations == pose.translations);

    // What a bake adds. The rig's tokens, verbatim and in the rig's order --
    // the order the arrays are already in -- and each joint's rest scale, the
    // arm's 2 included (the scale policy).
    const std::vector<std::string> expectedJoints = {kRoot, kHips, kSpine,   kChest,
                                                     kNeck, kHead, kUpperArm};
    assert(sample.joints == expectedJoints);
    assert(sample.scales.size() == expectedJoints.size());
    for (std::size_t j = 0; j < sample.scales.size(); ++j)
    {
        assert(sample.scales[j] == pxr::GfVec3h(expectedJoints[j] == kUpperArm ? 2.0f : 1.0f));
    }
    std::printf("execVrm rig: the joint transforms are the retarget's arrays "
                "with the rig's tokens and rest scales beside them\n");
}

void
TestTheSampleRefusesWhatCannotBeASample()
{
    const vrmRetarget::HumanoidMap map = *execvrm::HumanoidMapFor(FixtureInputs()).map;
    const vrmRetarget::RetargetedPose pose =
        *execvrm::HumanoidRetargetFor(RetargetFixture(map)).pose;
    using execvrm::JointTransformsRefusal;

    auto refused = [](const execvrm::JointTransformsInputs& inputs)
    {
        const execvrm::JointTransformsOutcome outcome = execvrm::JointLocalTransformsFor(inputs);
        assert(!outcome.sample);
        return outcome;
    };

    // The retarget first: its own error says why, and a skeleton missing too
    // is the same refusal one link further down.
    execvrm::JointTransformsInputs noPose = JointTransformsFixture(pose);
    noPose.pose = nullptr;
    assert(refused(noPose).refusal == JointTransformsRefusal::PoseUnanswered);
    noPose.targets.clear();
    assert(refused(noPose).refusal == JointTransformsRefusal::PoseUnanswered);

    // A rig that did not come back, or came back twice.
    execvrm::JointTransformsInputs noRig = JointTransformsFixture(pose);
    noRig.targets.clear();
    assert(refused(noRig).refusal == JointTransformsRefusal::RigUnanswered);
    noRig.targets = {FixtureSkeleton(), FixtureSkeleton()};
    assert(refused(noRig).refusal == JointTransformsRefusal::RigUnanswered);

    // A pose that was not retargeted onto this rig: one joint short, and one
    // whose two arrays disagree with each other. Both named with their sizes.
    vrmRetarget::RetargetedPose shortPose = pose;
    shortPose.rotations.pop_back();
    shortPose.translations.pop_back();
    execvrm::JointTransformsOutcome o = refused(JointTransformsFixture(shortPose));
    assert(o.refusal == JointTransformsRefusal::JointCount && o.joints == 7 && o.rotations == 6 &&
           o.translations == 6);
    vrmRetarget::RetargetedPose uneven = pose;
    uneven.translations.pop_back();
    o = refused(JointTransformsFixture(uneven));
    assert(o.refusal == JointTransformsRefusal::JointCount && o.joints == 7 && o.rotations == 7 &&
           o.translations == 6);

    // An empty rig and an empty pose pair: an empty sample is an answer, the
    // animation of a skeleton with no joints.
    const vrmRetarget::RetargetedPose nothing;
    execvrm::JointTransformsInputs empty;
    empty.pose = &nothing;
    empty.targets = {vrmRetarget::TargetSkeleton()};
    const execvrm::JointTransformsOutcome answered = execvrm::JointLocalTransformsFor(empty);
    assert(answered.sample && answered.sample->joints.empty() && answered.sample->scales.empty());
    std::printf("execVrm rig: the joint transforms refuse a retarget that did "
                "not answer, a rig that did not come back, and a pose that "
                "does not pair with the rig\n");
}

// ---------------------------------------------------------------------------
// vrm.computeRigDiagnostics and vrm.computeRetargetDiagnostics
// ---------------------------------------------------------------------------

execvrm::RigDiagnosticsInputs
RigDiagnosticsFixture(const vrmRetarget::HumanoidMap& map)
{
    execvrm::RigDiagnosticsInputs inputs;
    inputs.map = &map;
    inputs.targets = {FixtureSkeleton()};
    return inputs;
}

// The fixture's map with one binding left out, as the humanoid would state it
// with that attribute unauthored.
vrmRetarget::HumanoidMap
MapWithout(HumanJoint dropped)
{
    execvrm::HumanoidInputs inputs = FixtureInputs();
    inputs.bindings.erase(std::remove_if(inputs.bindings.begin(), inputs.bindings.end(),
                                         [dropped](const auto& binding)
                                         { return binding.first == dropped; }),
                          inputs.bindings.end());
    return *execvrm::HumanoidMapFor(inputs).map;
}

const vrmRetarget::RetargetDiagnostic*
Find(const vrmRetarget::RetargetDiagnostics& diagnostics, vrmRetarget::RetargetDiagnosticCode code,
     const std::string& subject)
{
    for (const vrmRetarget::RetargetDiagnostic& d : diagnostics.reported)
    {
        if (d.code == code && d.subject == subject)
        {
            return &d;
        }
    }
    return nullptr;
}

void
TestTheRigDiagnosticsAreDiagnoseRigsCall()
{
    using vrmRetarget::RetargetDiagnosticCode;
    const vrmRetarget::HumanoidMap map = *execvrm::HumanoidMapFor(FixtureInputs()).map;

    // The wrapper claim, with the library's default options: exactly.
    execvrm::RigDiagnosticsOutcome outcome = execvrm::RigDiagnosticsFor(RigDiagnosticsFixture(map));
    assert(outcome.diagnostics);
    assert(*outcome.diagnostics == vrmRetarget::DiagnoseRig(FixtureSkeleton(), map) &&
           "the rig's diagnostics are not DiagnoseRig over the same values");

    // And from the definition, so the equality above is not two empty lists:
    // the fixture binds six bones, and every required bone it leaves out is
    // named, in the vocabulary's order, while every one it binds is not.
    std::vector<std::string> missing;
    for (const HumanJoint bone : vrmRetarget::HumanoidMap::GetRequiredBones())
    {
        if (!map.IsMapped(bone))
        {
            missing.emplace_back(openstrata::motion::HumanJointName(bone));
        }
    }
    assert(missing.size() >= 10);
    assert(outcome.diagnostics->Subjects(RetargetDiagnosticCode::MissingRequiredBone) == missing);
    assert(outcome.diagnostics->reported.size() == missing.size() &&
           "the fixture's rig raised something beyond its missing bones");

    // The statements reach the options. A rig with no hips says what that
    // costs under root-motion mode 'hips', and only there.
    const vrmRetarget::HumanoidMap noHips = MapWithout(HumanJoint::Hips);
    const std::string hips = "hips";
    const auto hipsDetail = [&](const execvrm::RigDiagnosticsInputs& inputs)
    {
        const execvrm::RigDiagnosticsOutcome o = execvrm::RigDiagnosticsFor(inputs);
        assert(o.diagnostics);
        const vrmRetarget::RetargetDiagnostic* d =
            Find(*o.diagnostics, RetargetDiagnosticCode::MissingRequiredBone, hips);
        assert(d && "a rig with no hips did not say so");
        return d->detail;
    };
    execvrm::RigDiagnosticsInputs defaulted = RigDiagnosticsFixture(noHips);
    assert(hipsDetail(defaulted).find("root motion was dropped") != std::string::npos);
    execvrm::RigDiagnosticsInputs ignored = RigDiagnosticsFixture(noHips);
    ignored.rootMotion.mode = "ignore";
    assert(hipsDetail(ignored).find("root motion") == std::string::npos);

    // Every statement made, against the options the tool builds from the same
    // four flags.
    execvrm::RigDiagnosticsInputs stated = RigDiagnosticsFixture(noHips);
    stated.rootMotion.mode = "root";
    stated.rootMotion.rootJoint = kRoot;
    stated.rootMotion.translationScale = 2.0f;
    vrmRetarget::RetargetOptions options;
    options.rootMotion.mode = vrmRetarget::RootMotionMode::RootJoint;
    options.rootMotion.rootJointIndex = 0;
    options.rootMotion.translationScale = 2.0f;
    outcome = execvrm::RigDiagnosticsFor(stated);
    assert(outcome.diagnostics &&
           *outcome.diagnostics == vrmRetarget::DiagnoseRig(FixtureSkeleton(), noHips, options));

    // A rig out of parent-before-child order is an answer upstream, and so it
    // reaches this node as a code rather than a refusal.
    execvrm::SkeletonRest unordered = FixtureRest();
    std::swap(unordered.joints[4], unordered.joints[5]); // head before neck
    std::swap(unordered.restTransforms[4], unordered.restTransforms[5]);
    execvrm::HumanoidInputs unorderedHumanoid = FixtureInputs();
    unorderedHumanoid.skeletons = {*execvrm::TargetSkeletonFromRest(unordered).skeleton};
    const vrmRetarget::HumanoidMap unorderedMap = *execvrm::HumanoidMapFor(unorderedHumanoid).map;
    execvrm::RigDiagnosticsInputs hierarchy = RigDiagnosticsFixture(unorderedMap);
    hierarchy.targets = unorderedHumanoid.skeletons;
    outcome = execvrm::RigDiagnosticsFor(hierarchy);
    assert(outcome.diagnostics &&
           outcome.diagnostics->Subjects(RetargetDiagnosticCode::InvalidHierarchy) ==
               std::vector<std::string>({kHead}));
    std::printf("execVrm rig: the rig's diagnostics are DiagnoseRig over the "
                "rig, the map and the statements -- %zu missing required bones "
                "on the fixture, and a hierarchy out of order\n",
                missing.size());
}

void
TestTheRigDiagnosticsRefuseAsTheRetargetDoes()
{
    const vrmRetarget::HumanoidMap map = *execvrm::HumanoidMapFor(FixtureInputs()).map;
    using execvrm::RetargetRefusal;

    auto refused = [](const execvrm::RigDiagnosticsInputs& inputs)
    {
        const execvrm::RigDiagnosticsOutcome outcome = execvrm::RigDiagnosticsFor(inputs);
        assert(!outcome.diagnostics);
        return outcome;
    };

    execvrm::RigDiagnosticsInputs noMap = RigDiagnosticsFixture(map);
    noMap.map = nullptr;
    assert(refused(noMap).refusal == RetargetRefusal::RigUnanswered);
    execvrm::RigDiagnosticsInputs noRig = RigDiagnosticsFixture(map);
    noRig.targets.clear();
    assert(refused(noRig).refusal == RetargetRefusal::RigUnanswered);
    noRig.targets = {FixtureSkeleton(), FixtureSkeleton()};
    assert(refused(noRig).refusal == RetargetRefusal::RigUnanswered);

    // A statement the layer cannot honour is refused rather than diagnosed
    // under the default it would otherwise have fallen back to -- and that is
    // where INVALID_ROOT_JOINT goes: a root joint the rig lacks never becomes
    // an index, so the code DiagnoseRig raises for one is unreachable here.
    execvrm::RigDiagnosticsInputs statement = RigDiagnosticsFixture(map);
    statement.rootMotion.mode = "sideways";
    execvrm::RigDiagnosticsOutcome o = refused(statement);
    assert(o.refusal == RetargetRefusal::RootMotion &&
           o.rootMotionRefusal == execvrm::RootMotionRefusal::UnknownMode);
    statement.rootMotion.mode = "root";
    statement.rootMotion.rootJoint = "NoSuchJoint";
    o = refused(statement);
    assert(o.refusal == RetargetRefusal::RootMotion &&
           o.rootMotionRefusal == execvrm::RootMotionRefusal::UnknownRootJoint);
    std::printf("execVrm rig: the rig's diagnostics refuse a rig and a "
                "statement as the retarget does, and so never raise an "
                "invalid root joint\n");
}

void
TestTheRetargetDiagnosticsAreTheRigsThenThePoses()
{
    using vrmRetarget::RetargetDiagnosticCode;
    const vrmRetarget::HumanoidMap map = *execvrm::HumanoidMapFor(FixtureInputs()).map;
    const vrmRetarget::SourceRestPose rest =
        *execvrm::SourceRestFromSkeleton(SemanticSkeleton()).rest;
    const vrmRetarget::RetargetDiagnostics rig =
        *execvrm::RigDiagnosticsFor(RigDiagnosticsFixture(map)).diagnostics;
    const vrmRetarget::PoseRetargeter retargeter(FixtureSkeleton(), map, rest);

    // The wrapper claim: DiagnoseRig, then what the retarget of this one pose
    // reported -- the clip overload's order, for a clip of this one sample.
    const execvrm::RetargetDiagnosticsOutcome outcome =
        execvrm::RetargetDiagnosticsFor(RetargetFixture(map), &rig);
    assert(outcome.diagnostics && !outcome.rigUnanswered);
    vrmRetarget::RetargetDiagnostics expected = vrmRetarget::DiagnoseRig(FixtureSkeleton(), map);
    retargeter.Retarget(ClipSample(), &expected);
    assert(*outcome.diagnostics == expected);
    {
        openstrata::motion::MotionClip clip;
        clip.samples = {ClipSample()};
        vrmRetarget::RetargetDiagnostics overload;
        retargeter.Retarget(clip, &overload);
        assert(*outcome.diagnostics == overload &&
               "one sample's diagnostics are not a one-sample clip's");
    }

    // From the definition: the rig's list whole, then the one bone the sample
    // drives and the fixture does not bind.
    const std::vector<vrmRetarget::RetargetDiagnostic>& reported = outcome.diagnostics->reported;
    assert(reported.size() == rig.reported.size() + 1);
    assert(std::equal(rig.reported.begin(), rig.reported.end(), reported.begin()));
    assert(reported.back().code == RetargetDiagnosticCode::UnboundDrivenBone &&
           reported.back().subject == "rightUpperArm");

    // Diagnosing costs the pose nothing: the retarget answers the same bits
    // with a list to fill as without one.
    vrmRetarget::RetargetDiagnostics filled;
    assert(*execvrm::HumanoidRetargetFor(RetargetFixture(map), &filled).pose ==
           *execvrm::HumanoidRetargetFor(RetargetFixture(map)).pose);
    assert(filled.Has(RetargetDiagnosticCode::UnboundDrivenBone, "rightUpperArm"));

    // Merged over a clip's samples, in order, the nodes' answers are the list
    // the clip overload reports for the clip -- including a bone the clip
    // first drives on its second sample, which is what the harness relies on.
    openstrata::motion::MotionPose later = ClipSample();
    later.timestamp = 1.0;
    later.localRotations[static_cast<std::size_t>(HumanJoint::LeftLowerArm)] =
        About(pxr::GfVec3f(0, 1, 0), 15.0f);
    later.validRotations.set(static_cast<std::size_t>(HumanJoint::LeftLowerArm));
    execvrm::RetargetInputs second = RetargetFixture(map);
    second.poses = {later};
    vrmRetarget::RetargetDiagnostics merged;
    merged.Merge(*execvrm::RetargetDiagnosticsFor(RetargetFixture(map), &rig).diagnostics);
    merged.Merge(*execvrm::RetargetDiagnosticsFor(second, &rig).diagnostics);
    openstrata::motion::MotionClip clip;
    clip.samples = {ClipSample(), later};
    vrmRetarget::RetargetDiagnostics overload;
    retargeter.Retarget(clip, &overload);
    assert(merged == overload && "the samples' diagnostics, merged, are not the clip's");
    assert(merged.Subjects(RetargetDiagnosticCode::UnboundDrivenBone) ==
           std::vector<std::string>({"rightUpperArm", "leftLowerArm"}));
    std::printf("execVrm rig: a sample's diagnostics are the rig's then the "
                "pose's, and merged over the samples they are the clip's\n");
}

void
TestTheRetargetDiagnosticsRefuseWhenTheRetargetDoes()
{
    const vrmRetarget::HumanoidMap map = *execvrm::HumanoidMapFor(FixtureInputs()).map;
    const vrmRetarget::RetargetDiagnostics rig =
        *execvrm::RigDiagnosticsFor(RigDiagnosticsFixture(map)).diagnostics;
    using execvrm::RetargetRefusal;

    auto refused = [&rig](const execvrm::RetargetInputs& inputs)
    {
        const execvrm::RetargetDiagnosticsOutcome outcome =
            execvrm::RetargetDiagnosticsFor(inputs, &rig);
        assert(!outcome.diagnostics && !outcome.rigUnanswered);
        assert(!outcome.retarget.pose && "a refusal carried the retarget's pose out with it");
        return outcome.retarget;
    };

    // Each for the retarget's own reason, which is the retarget's refusal
    // order: nothing was retargeted, so nothing was reported -- and in
    // particular not the rig's list alone, which would read as a clean pose.
    execvrm::RetargetInputs noInstant = RetargetFixture(map);
    noInstant.hasInstant = false;
    assert(refused(noInstant).refusal == RetargetRefusal::NoInstant);
    execvrm::RetargetInputs none = RetargetFixture(map);
    none.sourceTargetCount = 0;
    none.sources.clear();
    none.poses.clear();
    assert(refused(none).refusal == RetargetRefusal::NoSource);
    execvrm::RetargetInputs statement = RetargetFixture(map);
    statement.rootMotion.mode = "sideways";
    const execvrm::RetargetOutcome o = refused(statement);
    assert(o.refusal == RetargetRefusal::RootMotion &&
           o.rootMotionRefusal == execvrm::RootMotionRefusal::UnknownMode);

    // A retarget that answered beside a rig list that did not come back.
    const execvrm::RetargetDiagnosticsOutcome noRig =
        execvrm::RetargetDiagnosticsFor(RetargetFixture(map), nullptr);
    assert(!noRig.diagnostics && noRig.rigUnanswered && !noRig.retarget.pose);
    std::printf("execVrm rig: a sample's diagnostics refuse whenever the "
                "retarget does, and without the rig's list in front\n");
}

} // namespace

int
main()
{
    TestTheAttributeNamesAreTheVocabularysOwn();
    TestTheSkeletonIsTheRestPoseDecomposed();
    TestARestPoseThatDoesNotPairIsRefused();
    TestNoJointsIsAnAnswer();
    TestAnEmptyJointTokenIsRefused();
    TestAnUnorderedSkeletonIsCarriedFaithfully();
    TestTheMapIsTheLibrarysBindings();
    TestTheSkeletonTargetIsCounted();
    TestABindingToNoJointIsRefusedAndNamed();
    TestTwoBonesOnOneJointAreRefusedAndBothNamed();
    TestAnEmptyTokenBindsNothing();
    TestTheSourceRestIsReadOffTheSemanticSkeleton();
    TestTheSourceParentIsTheParentPathsLeaf();
    TestASourceThatIsNotSemanticIsRefused();
    TestASourceNamingOneBoneTwiceIsRefusedAndBothNamed();
    TestTheCorrectionIsTheLibrarysCall();
    TestTheCorrectionRefusesWhatItCannotHonour();
    TestTheBoundPoseIsTheOnePoseForwarded();
    TestTheRootMotionOptionsAreTheToolsFlags();
    TestTheRootMotionOptionsRefuseWhatTheToolRefuses();
    TestTheRetargetIsThePoseRetargetersCall();
    TestTheRetargetRefusesInItsOrder();
    TestTheSampleIsTheRetargetWithTheBakesTwoAdditions();
    TestTheSampleRefusesWhatCannotBeASample();
    TestTheRigDiagnosticsAreDiagnoseRigsCall();
    TestTheRigDiagnosticsRefuseAsTheRetargetDoes();
    TestTheRetargetDiagnosticsAreTheRigsThenThePoses();
    TestTheRetargetDiagnosticsRefuseWhenTheRetargetDoes();
    std::puts("execVrm rig: all checks passed");
    return 0;
}
