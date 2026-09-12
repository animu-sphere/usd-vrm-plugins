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

#include <motionCore/Humanoid.h>
#include <vrmRetarget/HumanoidMap.h>
#include <vrmRetarget/TargetSkeleton.h>

#include "pxr/base/gf/matrix4d.h"
#include "pxr/base/gf/quatf.h"
#include "pxr/base/gf/vec3d.h"
#include "pxr/base/gf/vec3f.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace {

using motion::HumanBone;

bool NearlyEqual(float a, float b, float tolerance = 1e-6f)
{
    return std::abs(a - b) <= tolerance;
}

bool NearlyEqual(const pxr::GfVec3f& a, const pxr::GfVec3f& b)
{
    return NearlyEqual(a[0], b[0]) && NearlyEqual(a[1], b[1])
        && NearlyEqual(a[2], b[2]);
}

bool NearlyEqual(const pxr::GfQuatf& a, const pxr::GfQuatf& b)
{
    return NearlyEqual(a.GetReal(), b.GetReal())
        && NearlyEqual(a.GetImaginary(), b.GetImaginary());
}

pxr::GfMatrix4d Translate(double x, double y, double z)
{
    return pxr::GfMatrix4d(1.0).SetTranslate(pxr::GfVec3d(x, y, z));
}

// The fixture's rig, as plain values: humanoid_rig.usda's seven joints.
const char* const kRoot = "Root";
const char* const kHips = "Root/J_Bip_C_Hips";
const char* const kSpine = "Root/J_Bip_C_Hips/J_Bip_C_Spine";
const char* const kChest = "Root/J_Bip_C_Hips/J_Bip_C_Spine/J_Bip_C_Chest";
const char* const kNeck =
    "Root/J_Bip_C_Hips/J_Bip_C_Spine/J_Bip_C_Chest/J_Bip_C_Neck";
const char* const kHead =
    "Root/J_Bip_C_Hips/J_Bip_C_Spine/J_Bip_C_Chest/J_Bip_C_Neck/J_Bip_C_Head";
const char* const kUpperArm =
    "Root/J_Bip_C_Hips/J_Bip_C_Spine/J_Bip_C_Chest/J_Bip_L_UpperArm";

// Turned 90 degrees about +Z and scaled by 2, in USD's row-vector convention:
// the first row is where +X goes (to +Y), the second where +Y goes (to -X).
pxr::GfMatrix4d TurnedAndScaled()
{
    return pxr::GfMatrix4d(0, 2, 0, 0,
                           -2, 0, 0, 0,
                           0, 0, 2, 0,
                           0.1, 0.15, 0, 1);
}

execvrm::SkeletonRest FixtureRest()
{
    execvrm::SkeletonRest rest;
    rest.joints = {kRoot, kHips, kSpine, kChest, kNeck, kHead, kUpperArm};
    rest.restTransforms = {
        pxr::GfMatrix4d(1.0),     Translate(0, 1, 0),  Translate(0, 0.1, 0),
        Translate(0, 0.15, 0),    Translate(0, 0.2, 0), Translate(0, 0.1, 0),
        TurnedAndScaled()};
    return rest;
}

vrmRetarget::TargetSkeleton FixtureSkeleton()
{
    execvrm::SkeletonOutcome outcome =
        execvrm::TargetSkeletonFromRest(FixtureRest());
    assert(outcome.skeleton);
    return *outcome.skeleton;
}

std::vector<std::pair<HumanBone, std::string>> FixtureBindings()
{
    return {{HumanBone::Hips, kHips},   {HumanBone::Spine, kSpine},
            {HumanBone::Chest, kChest}, {HumanBone::Neck, kNeck},
            {HumanBone::Head, kHead},   {HumanBone::LeftUpperArm, kUpperArm}};
}

execvrm::HumanoidInputs FixtureInputs()
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
void TestTheAttributeNamesAreTheVocabularysOwn()
{
    const auto& names = execvrm::HumanBoneAttributeNames();
    assert(names.size() == motion::HumanBoneCount);
    assert(names[static_cast<std::size_t>(HumanBone::Hips)] ==
           "vrm:humanBones:hips");
    assert(names[static_cast<std::size_t>(HumanBone::LeftThumbMetacarpal)] ==
           "vrm:humanBones:leftThumbMetacarpal");
    assert(names[static_cast<std::size_t>(HumanBone::RightLittleDistal)] ==
           "vrm:humanBones:rightLittleDistal");

    // One input is declared per entry, so two equal names would be one input
    // declared twice and a bone that could never be read.
    std::set<std::string> distinct;
    for (const auto& name : names) {
        distinct.insert(name.GetString());
    }
    assert(distinct.size() == motion::HumanBoneCount);
    std::printf("execVrm rig: %zu attribute names, one per bone\n",
                names.size());
}

// ---------------------------------------------------------------------------
// vrm.computeTargetSkeleton
// ---------------------------------------------------------------------------
void TestTheSkeletonIsTheRestPoseDecomposed()
{
    const vrmRetarget::TargetSkeleton skeleton = FixtureSkeleton();
    const std::vector<vrmRetarget::TargetJoint>& joints = skeleton.GetJoints();
    assert(joints.size() == 7);

    // Tokens verbatim, in the skeleton's own order -- the order a map's indices
    // count into.
    assert(joints[0].token == kRoot && joints[6].token == kUpperArm);

    // Parents from the joint paths: the arm hangs off the chest, not the neck.
    const int parents[] = {-1, 0, 1, 2, 3, 4, 3};
    for (std::size_t i = 0; i < joints.size(); ++i) {
        assert(joints[i].parent == parents[i]);
    }
    assert(skeleton.IsTopologicallyOrdered());

    // A translation-only rest is an identity rotation and that translation.
    assert(joints[1].restRotation == pxr::GfQuatf(1.0f, pxr::GfVec3f(0.0f)));
    assert(NearlyEqual(joints[1].restTranslation, pxr::GfVec3f(0, 1, 0)));

    // 90 degrees about +Z, with the scale of 2 dropped rather than folded into
    // the rotation: (cos 45, 0, 0, sin 45), and the translation off the matrix.
    const float half = std::sqrt(0.5f);
    assert(NearlyEqual(joints[6].restRotation,
                       pxr::GfQuatf(half, 0.0f, 0.0f, half)) &&
           "the scaled rest transform did not decompose to its rotation");
    assert(NearlyEqual(joints[6].restTranslation,
                       pxr::GfVec3f(0.1f, 0.15f, 0.0f)));
    std::printf("execVrm rig: the rest pose decomposes, scale dropped\n");
}

void TestARestPoseThatDoesNotPairIsRefused()
{
    // Absent: an attribute the skeleton does not carry arrives as no matrices.
    execvrm::SkeletonRest absent = FixtureRest();
    absent.restTransforms.clear();
    execvrm::SkeletonOutcome outcome = execvrm::TargetSkeletonFromRest(absent);
    assert(!outcome.skeleton &&
           "a skeleton with no rest pose was given one -- an identity rest is "
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

void TestNoJointsIsAnAnswer()
{
    // A skeleton with no joints, and so no rest transforms, is an empty
    // skeleton -- a value no skeleton that has a joint could be mistaken for.
    execvrm::SkeletonOutcome outcome =
        execvrm::TargetSkeletonFromRest(execvrm::SkeletonRest{});
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

void TestAnEmptyJointTokenIsRefused()
{
    // What exec hands over for a skeleton that authors no `joints` at all: one
    // fallback token, and one fallback matrix beside it. The counts pair, so
    // only the token can say it.
    execvrm::SkeletonRest unauthored;
    unauthored.joints = {""};
    unauthored.restTransforms = {pxr::GfMatrix4d(1.0)};
    execvrm::SkeletonOutcome outcome =
        execvrm::TargetSkeletonFromRest(unauthored);
    assert(!outcome.skeleton &&
           "a skeleton that authors no joints came back as one joint named "
           "nothing");
    assert(outcome.refusal == execvrm::SkeletonRefusal::EmptyJointToken);

    // And anywhere in the list, not only alone.
    execvrm::SkeletonRest inside = FixtureRest();
    inside.joints[3].clear();
    assert(execvrm::TargetSkeletonFromRest(inside).refusal ==
           execvrm::SkeletonRefusal::EmptyJointToken);
    std::printf("execVrm rig: an empty joint token is refused\n");
}

void TestAnUnorderedSkeletonIsCarriedFaithfully()
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
void TestTheMapIsTheLibrarysBindings()
{
    const execvrm::MapOutcome outcome = execvrm::HumanoidMapFor(FixtureInputs());
    assert(outcome.map);
    const vrmRetarget::HumanoidMap& map = *outcome.map;

    // The wrapper claim: the node's map IS `SetJointToken` over the bindings,
    // against the same skeleton -- so the library's map is the expected value.
    vrmRetarget::HumanoidMap expected;
    const vrmRetarget::TargetSkeleton skeleton = FixtureSkeleton();
    for (const auto& [bone, token] : FixtureBindings()) {
        assert(expected.SetJointToken(bone, token, skeleton));
    }
    assert(map == expected);

    // And the indices themselves, from the fixture's joint order.
    assert(map.GetMappedCount() == 6);
    assert(map.GetJointIndex(HumanBone::Hips) == 1);
    assert(map.GetJointIndex(HumanBone::Head) == 5);
    assert(map.GetJointIndex(HumanBone::LeftUpperArm) == 6);
    assert(!map.IsMapped(HumanBone::RightUpperArm));

    // Eleven of VRM 1.0's seventeen required bones are unbound, and the value
    // says so rather than the node refusing for it.
    const std::vector<HumanBone> missing = map.FindMissingRequiredBones();
    assert(missing.size() == 11);
    assert(std::find(missing.begin(), missing.end(), HumanBone::LeftUpperLeg)
           != missing.end());
    std::printf("execVrm rig: the map is SetJointToken over the bindings, and "
                "carries its own gaps\n");
}

void TestTheSkeletonTargetIsCounted()
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
    assert(!outcome.map &&
           outcome.refusal == execvrm::MapRefusal::SeveralSkeletons);

    // Two targets, one answering: still two targets. The count is decided on
    // what the relationship reaches, before what came back.
    execvrm::HumanoidInputs twoOneAnswered = FixtureInputs();
    twoOneAnswered.skeletonTargetCount = 2;
    outcome = execvrm::HumanoidMapFor(twoOneAnswered);
    assert(!outcome.map &&
           outcome.refusal == execvrm::MapRefusal::SeveralSkeletons);

    // One target, nothing back: the fan-in dropped it.
    execvrm::HumanoidInputs dropped = FixtureInputs();
    dropped.skeletons.clear();
    outcome = execvrm::HumanoidMapFor(dropped);
    assert(!outcome.map &&
           outcome.refusal == execvrm::MapRefusal::SkeletonUnanswered);
    std::printf("execVrm rig: exactly one skeleton, counted twice\n");
}

void TestABindingToNoJointIsRefusedAndNamed()
{
    execvrm::HumanoidInputs inputs = FixtureInputs();
    // The leaf name a heuristic would try: not a joint path of this skeleton.
    inputs.bindings[0].second = "J_Bip_C_Hips";
    inputs.bindings[4].second = "Root/Head";
    const execvrm::MapOutcome outcome = execvrm::HumanoidMapFor(inputs);
    assert(!outcome.map &&
           "a binding the skeleton cannot honour was dropped instead -- a map "
           "without the bone reads as a humanoid that never named it");
    assert(outcome.refusal == execvrm::MapRefusal::UnknownJoint);
    assert(outcome.offending.size() == 2);
    assert(outcome.offending[0] ==
           std::make_pair(HumanBone::Hips, std::string("J_Bip_C_Hips")));
    assert(outcome.offending[1] ==
           std::make_pair(HumanBone::Head, std::string("Root/Head")));
    std::printf("execVrm rig: a binding to no joint is refused, by bone\n");
}

void TestTwoBonesOnOneJointAreRefusedAndBothNamed()
{
    execvrm::HumanoidInputs inputs = FixtureInputs();
    inputs.bindings.emplace_back(HumanBone::UpperChest, kChest);
    const execvrm::MapOutcome outcome = execvrm::HumanoidMapFor(inputs);
    assert(!outcome.map &&
           "two bones on one joint were kept, and a retarget would let one "
           "silently win");
    assert(outcome.refusal == execvrm::MapRefusal::DuplicateJoint);
    assert(outcome.offending.size() == 2);
    assert(outcome.offending[0].first == HumanBone::Chest);
    assert(outcome.offending[1].first == HumanBone::UpperChest);
    std::printf("execVrm rig: two bones on one joint are refused, both named\n");
}

void TestAnEmptyTokenBindsNothing()
{
    execvrm::HumanoidInputs inputs = FixtureInputs();
    inputs.bindings[5].second.clear();
    const execvrm::MapOutcome outcome = execvrm::HumanoidMapFor(inputs);
    assert(outcome.map &&
           "an empty token was refused -- it names no joint, which is not a "
           "misspelling of one");
    assert(outcome.map->GetMappedCount() == 5);
    assert(!outcome.map->IsMapped(HumanBone::LeftUpperArm));

    // And a humanoid stating nothing at all is the empty map.
    execvrm::HumanoidInputs nothing = FixtureInputs();
    nothing.bindings.clear();
    const execvrm::MapOutcome empty = execvrm::HumanoidMapFor(nothing);
    assert(empty.map && empty.map->GetMappedCount() == 0);
    std::printf("execVrm rig: an empty token binds nothing, and no bindings "
                "is the empty map\n");
}

} // namespace

int main()
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
    std::puts("execVrm rig: all checks passed");
    return 0;
}
