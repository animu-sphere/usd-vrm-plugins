// SPDX-License-Identifier: Apache-2.0
//
// The profile contract: the vocabulary a profile file states by name, the
// invariants a profile has to satisfy to be one, and what matching it against a
// rig concludes.
//
// Both values are built by hand throughout, which is the case they are validated
// for: a profile arrives from a file or from a fixture, a rig from a reader or
// from a test, and the converter above is entitled to the invariants rather than
// to a re-derivation of them.
//
// No producer appears here. The rig below is a plausible shape with ordinary
// names, and the profile is written against those names -- a test that used a
// real product's joint set would be the first place this layer learned one.
#include "motionSource/SourceProfile.h"

#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

namespace
{

using motionSource::FindRestPoseSource;
using motionSource::FindRootRotationPolicy;
using motionSource::FindRootTranslationPolicy;
using motionSource::FindSourceAxis;
using motionSource::FindSourceHandedness;
using motionSource::FindSourceLengthUnit;
using motionSource::FindUnmappedJointPolicy;
using motionSource::MatchSourceProfile;
using motionSource::RestPoseSource;
using motionSource::RestPoseSourceCount;
using motionSource::RestPoseSourceName;
using motionSource::RootRotationPolicy;
using motionSource::RootRotationPolicyCount;
using motionSource::RootRotationPolicyName;
using motionSource::RootTranslationPolicy;
using motionSource::RootTranslationPolicyCount;
using motionSource::RootTranslationPolicyName;
using motionSource::SourceAxis;
using motionSource::SourceAxisComponent;
using motionSource::SourceAxisIsNegative;
using motionSource::SourceAxisCount;
using motionSource::SourceAxisName;
using motionSource::SourceHandedness;
using motionSource::SourceHandednessCount;
using motionSource::SourceHandednessName;
using motionSource::SourceJoint;
using motionSource::SourceJointMapping;
using motionSource::SourceLengthUnit;
using motionSource::SourceLengthUnitInMeters;
using motionSource::SourceLengthUnitCount;
using motionSource::SourceLengthUnitName;
using motionSource::SourceProfile;
using motionSource::SourceProfileMatch;
using motionSource::SourceProfileRefusal;
using motionSource::SourceProfileRefusalCount;
using motionSource::SourceProfileRefusalName;
using motionSource::SourceSkeleton;
using motionSource::SourceVec3;
using motionSource::UnmappedJointPolicy;
using motionSource::UnmappedJointPolicyCount;
using motionSource::UnmappedJointPolicyName;
using motionSource::ValidateSourceProfile;

using Bone = motion::HumanBone;

SourceJoint
MakeJoint(std::string name, int parent)
{
    SourceJoint joint;
    joint.name = std::move(name);
    joint.parent = parent;
    joint.restTranslation = SourceVec3{0.0f, 10.0f, 0.0f};
    return joint;
}

// A scene root above the body, a spine chain, a head chain, one arm, and one
// joint nothing maps -- every shape the match has something to say about.
//
//   0 root
//   1  hip
//   2   spine
//   3    chest
//   4     neck
//   5      head
//   6     shoulderL
//   7      armL
//   8       foreArmL
//   9        handL
//  10  propHandle
SourceSkeleton
MakeSkeleton()
{
    SourceSkeleton skeleton;
    skeleton.joints.push_back(MakeJoint("root", -1));
    skeleton.joints.push_back(MakeJoint("hip", 0));
    skeleton.joints.push_back(MakeJoint("spine", 1));
    skeleton.joints.push_back(MakeJoint("chest", 2));
    skeleton.joints.push_back(MakeJoint("neck", 3));
    skeleton.joints.push_back(MakeJoint("head", 4));
    skeleton.joints.push_back(MakeJoint("shoulderL", 3));
    skeleton.joints.push_back(MakeJoint("armL", 6));
    skeleton.joints.push_back(MakeJoint("foreArmL", 7));
    skeleton.joints.push_back(MakeJoint("handL", 8));
    skeleton.joints.push_back(MakeJoint("propHandle", 0));
    return skeleton;
}

SourceJointMapping
Map(std::string sourceName, Bone bone, bool required)
{
    SourceJointMapping mapping;
    mapping.sourceName = std::move(sourceName);
    mapping.bone = bone;
    mapping.required = required;
    return mapping;
}

SourceProfile
MakeProfile()
{
    SourceProfile profile;
    profile.id = "example-recorder-neutral-v1";
    profile.producer = "Example Recorder";
    profile.rootJoint = "root";
    profile.handedness = SourceHandedness::Right;
    profile.upAxis = SourceAxis::PlusY;
    profile.forwardAxis = SourceAxis::PlusZ;
    profile.translationUnit = SourceLengthUnit::Centimeters;
    profile.rootTranslation = RootTranslationPolicy::AbsolutePosition;
    profile.rootRotation = RootRotationPolicy::BodyOrientation;
    profile.restPose = RestPoseSource::RestOffsets;
    profile.unmappedJoints = UnmappedJointPolicy::Report;
    profile.joints = {
        Map("hip", Bone::Hips, true),
        Map("spine", Bone::Spine, true),
        Map("chest", Bone::Chest, false),
        Map("neck", Bone::Neck, false),
        Map("head", Bone::Head, true),
        Map("shoulderL", Bone::LeftShoulder, false),
        Map("armL", Bone::LeftUpperArm, true),
        Map("foreArmL", Bone::LeftLowerArm, true),
        Map("handL", Bone::LeftHand, true),
    };
    // The scene root carries no bone and the profile says so, which is what lets
    // an unmapped-joint policy of `refuse` be usable at all.
    profile.ignoredJoints = {"root"};
    return profile;
}

void
Refuses(const SourceProfile& profile, const std::string& fragment)
{
    std::string reason;
    assert(!ValidateSourceProfile(profile, &reason));
    assert(reason.find(fragment) != std::string::npos);
}

// Every convention is stated by name, and the names are the library's rather
// than a loader's -- so the round trip through them is what pins the vocabulary
// a profile file may use.
void
TestVocabulary()
{
    assert(SourceHandednessName(SourceHandedness::Right) == "right");
    assert(FindSourceHandedness("RIGHT") == SourceHandedness::Right);
    assert(FindSourceHandedness("clockwise") == std::nullopt);

    assert(SourceAxisName(SourceAxis::PlusY) == "+Y");
    assert(SourceAxisName(SourceAxis::MinusZ) == "-Z");
    assert(FindSourceAxis("+y") == SourceAxis::PlusY);
    assert(FindSourceAxis("-Z") == SourceAxis::MinusZ);
    // The unsigned spelling reads as the positive direction, which the plan's
    // own profile sketch writes.
    assert(FindSourceAxis("Y") == SourceAxis::PlusY);
    assert(FindSourceAxis("z") == SourceAxis::PlusZ);
    assert(FindSourceAxis("W") == std::nullopt);

    assert(SourceAxisComponent(SourceAxis::PlusX) == 0);
    assert(SourceAxisComponent(SourceAxis::MinusY) == 1);
    assert(SourceAxisComponent(SourceAxis::PlusZ) == 2);
    assert(SourceAxisComponent(SourceAxis::Unspecified) == std::nullopt);
    assert(!SourceAxisIsNegative(SourceAxis::PlusX));
    assert(SourceAxisIsNegative(SourceAxis::MinusX));

    assert(SourceLengthUnitName(SourceLengthUnit::Centimeters)
           == "centimeters");
    assert(FindSourceLengthUnit("centimetres") == SourceLengthUnit::Centimeters);
    assert(FindSourceLengthUnit("Meters") == SourceLengthUnit::Meters);
    assert(FindSourceLengthUnit("furlongs") == std::nullopt);
    assert(SourceLengthUnitInMeters(SourceLengthUnit::Meters) == 1.0);
    assert(SourceLengthUnitInMeters(SourceLengthUnit::Centimeters) == 0.01);
    assert(SourceLengthUnitInMeters(SourceLengthUnit::Inches) == 0.0254);
    assert(SourceLengthUnitInMeters(SourceLengthUnit::Unspecified)
           == std::nullopt);

    assert(RootTranslationPolicyName(RootTranslationPolicy::AbsolutePosition)
           == "absolute-position");
    assert(FindRootTranslationPolicy("rest-relative")
           == RootTranslationPolicy::RestRelative);
    assert(RootRotationPolicyName(RootRotationPolicy::BodyOrientation)
           == "body-orientation");
    assert(FindRootRotationPolicy("none") == RootRotationPolicy::None);
    assert(RestPoseSourceName(RestPoseSource::StatedRestRotations)
           == "stated-rest-rotations");
    assert(FindRestPoseSource("first-frame") == RestPoseSource::FirstFrame);
    assert(UnmappedJointPolicyName(UnmappedJointPolicy::Refuse) == "refuse");
    assert(FindUnmappedJointPolicy("report") == UnmappedJointPolicy::Report);

    assert(SourceProfileRefusalName(SourceProfileRefusal::HierarchyMismatch)
           == "hierarchy-mismatch");

    // `Count` is not a value and names nothing, in every vocabulary -- the same
    // assertion the animation model's enums carry, and the one that would catch
    // a table gone out of step with its enum at runtime if the static_assert
    // beside it were ever loosened.
    assert(SourceHandednessName(SourceHandedness::Count).empty());
    assert(SourceAxisName(SourceAxis::Count).empty());
    assert(SourceLengthUnitName(SourceLengthUnit::Count).empty());
    assert(RootTranslationPolicyName(RootTranslationPolicy::Count).empty());
    assert(RootRotationPolicyName(RootRotationPolicy::Count).empty());
    assert(RestPoseSourceName(RestPoseSource::Count).empty());
    assert(UnmappedJointPolicyName(UnmappedJointPolicy::Count).empty());
    assert(SourceProfileRefusalName(SourceProfileRefusal::Count).empty());

    // Every enumerator below Count has a spelling, and every spelling finds its
    // way back to it. The static_assert in the implementation makes a missing
    // row a compile error; this makes an *empty* one a test failure.
    for (std::size_t i = 0; i < SourceAxisCount; ++i) {
        const auto axis = static_cast<SourceAxis>(i);
        assert(!SourceAxisName(axis).empty());
        assert(FindSourceAxis(SourceAxisName(axis)) == axis);
    }
    for (std::size_t i = 0; i < SourceLengthUnitCount; ++i) {
        const auto unit = static_cast<SourceLengthUnit>(i);
        assert(!SourceLengthUnitName(unit).empty());
        assert(FindSourceLengthUnit(SourceLengthUnitName(unit)) == unit);
    }
    for (std::size_t i = 0; i < RootTranslationPolicyCount; ++i) {
        const auto policy = static_cast<RootTranslationPolicy>(i);
        assert(!RootTranslationPolicyName(policy).empty());
        assert(FindRootTranslationPolicy(RootTranslationPolicyName(policy))
               == policy);
    }
    for (std::size_t i = 0; i < RootRotationPolicyCount; ++i) {
        const auto policy = static_cast<RootRotationPolicy>(i);
        assert(!RootRotationPolicyName(policy).empty());
        assert(FindRootRotationPolicy(RootRotationPolicyName(policy))
               == policy);
    }
    for (std::size_t i = 0; i < RestPoseSourceCount; ++i) {
        const auto rest = static_cast<RestPoseSource>(i);
        assert(!RestPoseSourceName(rest).empty());
        assert(FindRestPoseSource(RestPoseSourceName(rest)) == rest);
    }
    for (std::size_t i = 0; i < UnmappedJointPolicyCount; ++i) {
        const auto policy = static_cast<UnmappedJointPolicy>(i);
        assert(!UnmappedJointPolicyName(policy).empty());
        assert(FindUnmappedJointPolicy(UnmappedJointPolicyName(policy))
               == policy);
    }
    for (std::size_t i = 0; i < SourceHandednessCount; ++i) {
        const auto handedness = static_cast<SourceHandedness>(i);
        assert(!SourceHandednessName(handedness).empty());
        assert(FindSourceHandedness(SourceHandednessName(handedness))
               == handedness);
    }
    for (std::size_t i = 0; i < SourceProfileRefusalCount; ++i) {
        assert(!SourceProfileRefusalName(
                    static_cast<SourceProfileRefusal>(i)).empty());
    }
}

// A value cast in from outside the enum is refused the same way `Unspecified`
// is. These branches exist because a profile is a plain struct a caller
// assembles, and a `>= Count` a test never reaches is a `>= Count` that can be
// wrong.
void
TestOutOfRangeConventionsAreRefused()
{
    SourceProfile profile = MakeProfile();
    profile.handedness = static_cast<SourceHandedness>(200);
    Refuses(profile, "handedness");

    profile = MakeProfile();
    profile.upAxis = static_cast<SourceAxis>(200);
    Refuses(profile, "up axis");

    profile = MakeProfile();
    profile.forwardAxis = static_cast<SourceAxis>(200);
    Refuses(profile, "forward axis");

    profile = MakeProfile();
    profile.translationUnit = static_cast<SourceLengthUnit>(200);
    Refuses(profile, "translation unit");

    profile = MakeProfile();
    profile.rootTranslation = static_cast<RootTranslationPolicy>(200);
    Refuses(profile, "root translation policy");

    profile = MakeProfile();
    profile.rootRotation = static_cast<RootRotationPolicy>(200);
    Refuses(profile, "root rotation policy");

    profile = MakeProfile();
    profile.restPose = static_cast<RestPoseSource>(200);
    Refuses(profile, "rest pose source");

    profile = MakeProfile();
    profile.unmappedJoints = static_cast<UnmappedJointPolicy>(200);
    Refuses(profile, "unmapped-joint policy");

    // And nothing out of range names anything.
    assert(SourceAxisName(static_cast<SourceAxis>(200)).empty());
    assert(SourceLengthUnitName(static_cast<SourceLengthUnit>(200)).empty());
}

// There is no default profile and no automatic fallback (roadmap §3.1), and the
// struct is shaped so that the absence of one is a refusal rather than a set of
// silent answers.
void
TestDefaultProfileIsNotAProfile()
{
    const SourceProfile profile;
    assert(!ValidateSourceProfile(profile));

    // Each convention refuses on its own, so a reader is told which line the
    // file is missing rather than that the file is incomplete.
    SourceProfile partial = MakeProfile();
    partial.handedness = SourceHandedness::Unspecified;
    Refuses(partial, "handedness");

    partial = MakeProfile();
    partial.upAxis = SourceAxis::Unspecified;
    Refuses(partial, "up axis");

    partial = MakeProfile();
    partial.forwardAxis = SourceAxis::Unspecified;
    Refuses(partial, "forward axis");

    partial = MakeProfile();
    partial.translationUnit = SourceLengthUnit::Unspecified;
    Refuses(partial, "translation unit");

    partial = MakeProfile();
    partial.rootTranslation = RootTranslationPolicy::Unspecified;
    Refuses(partial, "root translation policy");

    partial = MakeProfile();
    partial.rootRotation = RootRotationPolicy::Unspecified;
    Refuses(partial, "root rotation policy");

    partial = MakeProfile();
    partial.restPose = RestPoseSource::Unspecified;
    Refuses(partial, "rest pose source");

    partial = MakeProfile();
    partial.unmappedJoints = UnmappedJointPolicy::Unspecified;
    Refuses(partial, "unmapped-joint policy");
}

void
TestValidationRefusals()
{
    assert(ValidateSourceProfile(MakeProfile()));

    SourceProfile profile = MakeProfile();
    profile.id.clear();
    Refuses(profile, "no id");

    profile = MakeProfile();
    profile.producer.clear();
    Refuses(profile, "no producer");

    profile = MakeProfile();
    profile.rootJoint.clear();
    Refuses(profile, "no root joint");

    profile = MakeProfile();
    profile.joints.clear();
    Refuses(profile, "maps no joints");

    profile = MakeProfile();
    profile.joints.push_back(Map("hip", Bone::LeftToes, false));
    Refuses(profile, "already maps");

    // Two joints claiming one bone is two statements about one rotation.
    profile = MakeProfile();
    profile.joints.push_back(Map("chestUpper", Bone::Chest, false));
    Refuses(profile, "same bone");

    profile = MakeProfile();
    profile.joints.push_back(Map("mystery", Bone::Count, false));
    Refuses(profile, "no canonical bone");

    profile = MakeProfile();
    profile.ignoredJoints.push_back("hip");
    Refuses(profile, "both mapped and ignored");

    profile = MakeProfile();
    profile.ignoredJoints.push_back("root");
    Refuses(profile, "listed twice");

    // The canonical humanoid roots at the hips, so a profile that does not bind
    // them describes a rig no conversion can place -- and one that binds them
    // optionally is the same profile with the refusal deferred.
    profile = MakeProfile();
    profile.joints.erase(profile.joints.begin());
    Refuses(profile, "maps no hips");

    profile = MakeProfile();
    profile.joints[0].required = false;
    Refuses(profile, "canonical root and is not required");
}

// Two directions along one axis describe no basis, and the sign is not the
// difference that saves it.
void
TestUpAndForwardAreDifferentAxes()
{
    SourceProfile profile = MakeProfile();
    profile.upAxis = SourceAxis::PlusY;
    profile.forwardAxis = SourceAxis::PlusY;
    Refuses(profile, "same axis");

    profile.forwardAxis = SourceAxis::MinusY;
    Refuses(profile, "same axis");

    profile.forwardAxis = SourceAxis::MinusZ;
    assert(ValidateSourceProfile(profile));
}

void
TestMatch()
{
    const SourceProfile profile = MakeProfile();
    const SourceSkeleton skeleton = MakeSkeleton();
    const SourceProfileMatch match = MatchSourceProfile(profile, skeleton);

    assert(match.Matched());
    assert(match.refusal == SourceProfileRefusal::None);
    assert(match.detail.empty());
    assert(match.rootMatched);

    // In the profile's declaration order, not the rig's: a report whose row
    // order depended on the rig would be the worse golden test.
    assert(match.bound.size() == 9);
    assert(match.bound.front().bone == Bone::Hips);
    assert(match.bound.front().jointIndex == 1);
    assert(match.bound.back().bone == Bone::LeftHand);
    assert(match.bound.back().jointIndex == 9);

    assert(match.JointFor(Bone::Head) == 5);
    assert(match.JointFor(Bone::RightHand) == std::nullopt);

    assert(match.missingRequired.empty());
    assert(match.missingOptional.empty());
    assert(match.ambiguousNames.empty());

    // The scene root is ignored by name; the joint nothing maps is reported.
    assert(match.unmappedJoints == std::vector<std::size_t>{10});
    assert(profile.RequiredMappingCount() == 6);
}

// A rig that drops an optional joint is still this profile's rig, and the
// missing bone is reported rather than silently absent.
void
TestOptionalJointMissing()
{
    SourceProfile profile = MakeProfile();
    profile.joints.push_back(Map("toesL", Bone::LeftToes, false));

    const SourceProfileMatch match =
        MatchSourceProfile(profile, MakeSkeleton());
    assert(match.Matched());
    assert(match.missingOptional == std::vector<Bone>{Bone::LeftToes});
    assert(match.missingRequired.empty());
}

void
TestRootJointMismatch()
{
    SourceProfile profile = MakeProfile();
    profile.rootJoint = "reference";
    profile.ignoredJoints = {"reference", "root"};

    const SourceProfileMatch match =
        MatchSourceProfile(profile, MakeSkeleton());
    assert(!match.Matched());
    assert(match.refusal == SourceProfileRefusal::RootJointMismatch);
    assert(!match.rootMatched);
    assert(match.detail.find("'reference'") != std::string::npos);
    assert(match.detail.find("'root'") != std::string::npos);
}

// A profile addresses joints by name, so a rig that repeats one the profile maps
// is ambiguous -- never resolved by taking the first.
void
TestAmbiguousJointName()
{
    SourceSkeleton skeleton = MakeSkeleton();
    skeleton.joints.push_back(MakeJoint("handL", 3));

    const SourceProfileMatch match =
        MatchSourceProfile(MakeProfile(), skeleton);
    assert(!match.Matched());
    assert(match.refusal == SourceProfileRefusal::AmbiguousJointName);
    assert(match.ambiguousNames == std::vector<std::string>{"handL"});
    // Ambiguous is not missing, and it is not bound either.
    assert(match.JointFor(Bone::LeftHand) == std::nullopt);
    assert(match.missingRequired.empty());
    // Nor is it unmapped: the profile does map that name.
    assert(match.unmappedJoints == std::vector<std::size_t>{10});
}

// The one number roadmap §3.1's candidate report prints, and the one way of
// computing it that is wrong.
//
// A required mapping whose name the rig repeats binds nothing and is *not*
// missing -- it is ambiguous -- so `RequiredMappingCount()` less
// `missingRequired.size()` counts it as matched. `BoundRequiredCount()` is what
// the report has to use, and this pins the difference rather than leaving the
// first detector to find it.
void
TestBoundRequiredCountSurvivesAmbiguity()
{
    const SourceProfile profile = MakeProfile();
    assert(profile.RequiredMappingCount() == 6);

    // Nothing ambiguous: the two agree, which is why the wrong arithmetic looks
    // right for as long as nobody writes this test.
    SourceProfileMatch match = MatchSourceProfile(profile, MakeSkeleton());
    assert(match.BoundRequiredCount() == 6);
    assert(profile.RequiredMappingCount() - match.missingRequired.size() == 6);

    // The rig repeats a name a *required* mapping addresses.
    SourceSkeleton skeleton = MakeSkeleton();
    skeleton.joints.push_back(MakeJoint("handL", 3));
    match = MatchSourceProfile(profile, skeleton);

    assert(match.refusal == SourceProfileRefusal::AmbiguousJointName);
    assert(match.missingRequired.empty());
    // The subtraction still says six of six matched ...
    assert(profile.RequiredMappingCount() - match.missingRequired.size() == 6);
    // ... and five is the truth.
    assert(match.BoundRequiredCount() == 5);

    // An optional mapping going ambiguous moves the same way, without touching
    // the required count.
    skeleton = MakeSkeleton();
    skeleton.joints.push_back(MakeJoint("chest", 3));
    match = MatchSourceProfile(profile, skeleton);
    assert(match.BoundRequiredCount() == 6);
    assert(match.bound.size() == 8);
}

// A binding carries what the mapping declared, so the profile is not needed
// beside the match to read one.
void
TestBindingsCarryRequired()
{
    const SourceProfileMatch match =
        MatchSourceProfile(MakeProfile(), MakeSkeleton());
    for (const auto& binding : match.bound) {
        const bool expected = binding.bone == Bone::Hips
                              || binding.bone == Bone::Spine
                              || binding.bone == Bone::Head
                              || binding.bone == Bone::LeftUpperArm
                              || binding.bone == Bone::LeftLowerArm
                              || binding.bone == Bone::LeftHand;
        assert(binding.required == expected);
    }
}

void
TestRequiredJointMissing()
{
    SourceSkeleton skeleton = MakeSkeleton();
    skeleton.joints[9].name = "handLeft";

    const SourceProfileMatch match =
        MatchSourceProfile(MakeProfile(), skeleton);
    assert(!match.Matched());
    assert(match.refusal == SourceProfileRefusal::RequiredJointMissing);
    assert(match.missingRequired == std::vector<Bone>{Bone::LeftHand});
    assert(match.detail.find("leftHand") != std::string::npos);
    // The renamed joint is now one the profile neither maps nor ignores.
    assert(match.unmappedJoints == (std::vector<std::size_t>{9, 10}));
}

// The near-miss: every name matches and the body is assembled wrong. This is
// what a joint map being a hierarchy embedding rather than a name lookup buys,
// and it is the failure roadmap §3.1 calls worse than a refusal because it looks
// like a result.
void
TestHierarchyMismatch()
{
    SourceSkeleton skeleton = MakeSkeleton();
    skeleton.joints[5].parent = 1; // head hung off the hips

    const SourceProfileMatch match =
        MatchSourceProfile(MakeProfile(), skeleton);
    assert(!match.Matched());
    assert(match.refusal == SourceProfileRefusal::HierarchyMismatch);
    assert(match.detail.find("'head'") != std::string::npos);
    assert(match.detail.find("'neck'") != std::string::npos);
    // Every name still bound; nothing is missing. Only the shape disagreed.
    assert(match.bound.size() == 9);
    assert(match.missingRequired.empty());
}

// A bone whose canonical parent the rig does not solve is parented at the
// nearest bound ancestor, not refused: a rig with no upper chest still puts its
// shoulders somewhere.
void
TestUnsolvedAncestorIsNotAMismatch()
{
    const SourceProfileMatch match =
        MatchSourceProfile(MakeProfile(), MakeSkeleton());
    // shoulderL sits under chest, and LeftShoulder's canonical parent is the
    // upper chest, which this profile does not map at all.
    assert(match.Matched());
    assert(match.JointFor(Bone::LeftShoulder) == 6);
    assert(match.JointFor(Bone::UpperChest) == std::nullopt);
}

void
TestUnmappedJointPolicy()
{
    SourceProfile profile = MakeProfile();
    const SourceSkeleton skeleton = MakeSkeleton();

    profile.unmappedJoints = UnmappedJointPolicy::Ignore;
    SourceProfileMatch match = MatchSourceProfile(profile, skeleton);
    assert(match.Matched());
    // Reported either way: the policy decides what a caller does about the
    // joint, not whether this layer noticed it.
    assert(match.unmappedJoints == std::vector<std::size_t>{10});

    profile.unmappedJoints = UnmappedJointPolicy::Report;
    assert(MatchSourceProfile(profile, skeleton).Matched());

    profile.unmappedJoints = UnmappedJointPolicy::Refuse;
    match = MatchSourceProfile(profile, skeleton);
    assert(!match.Matched());
    assert(match.refusal == SourceProfileRefusal::UnmappedJointRefused);
    assert(match.detail.find("'propHandle'") != std::string::npos);

    // Naming it makes `refuse` usable, which is the whole reason a profile can
    // say which joints it maps to nothing on purpose.
    profile.ignoredJoints.push_back("propHandle");
    match = MatchSourceProfile(profile, skeleton);
    assert(match.Matched());
    assert(match.unmappedJoints.empty());
}

void
TestInvalidInputsAreDistinguished()
{
    SourceProfile profile = MakeProfile();
    profile.id.clear();
    SourceProfileMatch match = MatchSourceProfile(profile, MakeSkeleton());
    assert(match.refusal == SourceProfileRefusal::ProfileInvalid);
    assert(match.detail.find("no id") != std::string::npos);

    // A valid profile and a rig a caller assembled badly is a different thing
    // to fix, so it is a different refusal.
    SourceSkeleton skeleton = MakeSkeleton();
    skeleton.joints[4].parent = -1;
    match = MatchSourceProfile(MakeProfile(), skeleton);
    assert(match.refusal == SourceProfileRefusal::SkeletonInvalid);
    assert(match.detail.find("second root") != std::string::npos);
}

// A detector reports on candidates that did *not* match (roadmap §3.1), so a
// refusal that returned early would leave it nothing to report with.
void
TestFactsSurviveARefusal()
{
    SourceSkeleton skeleton = MakeSkeleton();
    skeleton.joints[9].name = "handLeft";
    skeleton.joints[0].name = "reference";

    const SourceProfileMatch match =
        MatchSourceProfile(MakeProfile(), skeleton);
    // The outermost refusal wins ...
    assert(match.refusal == SourceProfileRefusal::RootJointMismatch);
    // ... and everything a candidate report is made of is still there.
    assert(match.bound.size() == 8);
    assert(match.missingRequired == std::vector<Bone>{Bone::LeftHand});
    assert(!match.unmappedJoints.empty());
}

// The order is part of the contract, because a rig can fail several checks at
// once and a test comparing one enumerator needs to know which it gets.
void
TestRefusalOrder()
{
    SourceSkeleton skeleton = MakeSkeleton();
    skeleton.joints[9].name = "handLeft";     // required missing
    skeleton.joints.push_back(MakeJoint("spine", 3)); // ambiguous

    SourceProfile profile = MakeProfile();
    profile.unmappedJoints = UnmappedJointPolicy::Refuse;

    // ambiguity outranks a missing required joint ...
    SourceProfileMatch match = MatchSourceProfile(profile, skeleton);
    assert(match.refusal == SourceProfileRefusal::AmbiguousJointName);

    // ... a missing required joint outranks the unmapped policy ...
    skeleton.joints.pop_back();
    match = MatchSourceProfile(profile, skeleton);
    assert(match.refusal == SourceProfileRefusal::RequiredJointMissing);

    // ... and a hierarchy that disagrees outranks it too.
    skeleton.joints[9].name = "handL";
    skeleton.joints[5].parent = 1;
    match = MatchSourceProfile(profile, skeleton);
    assert(match.refusal == SourceProfileRefusal::HierarchyMismatch);
}

void
TestQueriesAndEquality()
{
    const SourceProfile profile = MakeProfile();
    assert(profile.FindMapping("armL") == 6);
    assert(profile.FindMapping("ArmL") == std::nullopt); // verbatim, never folded
    assert(profile.FindBoneMapping(Bone::LeftUpperArm) == 6);
    assert(profile.FindBoneMapping(Bone::RightUpperArm) == std::nullopt);
    assert(profile.IgnoresJoint("root"));
    assert(!profile.IgnoresJoint("propHandle"));

    SourceProfile other = MakeProfile();
    assert(profile == other);
    other.joints[3].required = !other.joints[3].required;
    assert(profile != other);

    other = MakeProfile();
    other.ignoredJoints.clear();
    assert(profile != other);

    other = MakeProfile();
    other.restPose = RestPoseSource::FirstFrame;
    assert(profile != other);
}

// A match nobody made has concluded nothing, and a struct whose default state
// claims success is a trap for the code paths that forget to check.
void
TestDefaultMatchIsNotAMatch()
{
    const SourceProfileMatch match;
    assert(!match.Matched());
}

} // namespace

int
main()
{
    TestVocabulary();
    TestOutOfRangeConventionsAreRefused();
    TestDefaultProfileIsNotAProfile();
    TestValidationRefusals();
    TestUpAndForwardAreDifferentAxes();
    TestMatch();
    TestOptionalJointMissing();
    TestRootJointMismatch();
    TestAmbiguousJointName();
    TestBoundRequiredCountSurvivesAmbiguity();
    TestBindingsCarryRequired();
    TestRequiredJointMissing();
    TestHierarchyMismatch();
    TestUnsolvedAncestorIsNotAMismatch();
    TestUnmappedJointPolicy();
    TestInvalidInputsAreDistinguished();
    TestFactsSurviveARefusal();
    TestRefusalOrder();
    TestQueriesAndEquality();
    TestDefaultMatchIsNotAMatch();
    std::printf("motionSource profile contract: verified\n");
    return 0;
}
