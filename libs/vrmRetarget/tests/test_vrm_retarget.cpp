// SPDX-License-Identifier: Apache-2.0
#include "vrmRetarget/HumanoidMap.h"
#include "vrmRetarget/PoseRetargeter.h"
#include "vrmRetarget/RestPose.h"
#include "vrmRetarget/RootMotionPolicy.h"
#include "vrmRetarget/TargetSkeleton.h"

#include <algorithm>
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
        && NearlyEqual(na.GetImaginary()[0], nb.GetImaginary()[0])
        && NearlyEqual(na.GetImaginary()[1], nb.GetImaginary()[1])
        && NearlyEqual(na.GetImaginary()[2], nb.GetImaginary()[2]);
}

pxr::GfQuatf
Rotation(const pxr::GfVec3f& axis, float degrees)
{
    const float radians = degrees * 3.14159265358979324f / 180.0f;
    return pxr::GfQuatf(std::cos(radians * 0.5f),
                        axis.GetNormalized() * std::sin(radians * 0.5f));
}

const pxr::GfVec3f kAxisX(1.0f, 0.0f, 0.0f);
const pxr::GfVec3f kAxisY(0.0f, 1.0f, 0.0f);
const pxr::GfVec3f kAxisZ(0.0f, 0.0f, 1.0f);

// The design triplet's target rig (docs/design/fixtures/motion/avatar.usda):
// Root, Pelvis, SpineA, ChestA with translation-only rest transforms. The
// fixture lists them as flat sibling tokens; here they carry the hierarchical
// "a/b/c" joint paths so parent resolution is exercised too. Both spellings are
// valid UsdSkelSkeleton.joints, and the end-to-end test covers the flat one.
vrmRetarget::TargetSkeleton
DesignAvatar()
{
    vrmRetarget::TargetSkeleton skeleton;
    vrmRetarget::TargetJoint root;
    root.token = "Root";
    skeleton.AddJoint(root);

    vrmRetarget::TargetJoint pelvis;
    pelvis.token = "Root/Pelvis";
    pelvis.restTranslation = pxr::GfVec3f(0.0f, 1.0f, 0.0f);
    skeleton.AddJoint(pelvis);

    vrmRetarget::TargetJoint spine;
    spine.token = "Root/Pelvis/SpineA";
    spine.restTranslation = pxr::GfVec3f(0.0f, 0.5f, 0.0f);
    skeleton.AddJoint(spine);

    vrmRetarget::TargetJoint chest;
    chest.token = "Root/Pelvis/SpineA/ChestA";
    chest.restTranslation = pxr::GfVec3f(0.0f, 0.5f, 0.0f);
    skeleton.AddJoint(chest);

    skeleton.ResolveParentsFromTokens();
    return skeleton;
}

vrmRetarget::HumanoidMap
DesignMap(const vrmRetarget::TargetSkeleton& skeleton)
{
    vrmRetarget::HumanoidMap map;
    assert(map.SetJointToken(motion::HumanBone::Hips, "Root/Pelvis", skeleton));
    assert(map.SetJointToken(motion::HumanBone::Spine, "Root/Pelvis/SpineA",
                             skeleton));
    assert(map.SetJointToken(motion::HumanBone::Chest,
                             "Root/Pelvis/SpineA/ChestA", skeleton));
    return map;
}

// The clip's rest pose: hips at 1.0 m, matching canonical_walk.usda.
vrmRetarget::SourceRestPose
DesignSourceRest()
{
    vrmRetarget::SourceRestPose rest;
    rest.localTranslations[static_cast<std::size_t>(motion::HumanBone::Hips)] =
        pxr::GfVec3f(0.0f, 1.0f, 0.0f);
    rest.SetParent(motion::HumanBone::Spine, motion::HumanBone::Hips);
    rest.SetParent(motion::HumanBone::Chest, motion::HumanBone::Spine);
    return rest;
}

void
TestSkeletonParentsComeFromJointPaths()
{
    const vrmRetarget::TargetSkeleton skeleton = DesignAvatar();
    const std::vector<vrmRetarget::TargetJoint>& joints = skeleton.GetJoints();
    assert(joints[0].parent == vrmRetarget::TargetSkeleton::kNoParent);
    assert(joints[1].parent == 0);
    assert(joints[2].parent == 1);
    assert(joints[3].parent == 2);
    assert(skeleton.IsTopologicallyOrdered());

    // Lookup is on the full joint path, not the leaf name.
    assert(skeleton.FindJoint("Root/Pelvis") == 1);
    assert(skeleton.FindJoint("Pelvis") == vrmRetarget::TargetSkeleton::kNoParent);

    // A joint whose parent path is absent is a root, not a dangling index.
    vrmRetarget::TargetSkeleton orphaned;
    vrmRetarget::TargetJoint stray;
    stray.token = "Missing/Child";
    orphaned.AddJoint(stray);
    orphaned.ResolveParentsFromTokens();
    assert(orphaned.GetJoints()[0].parent
           == vrmRetarget::TargetSkeleton::kNoParent);
}

void
TestHumanoidMapReportsGapsAndCollisions()
{
    const vrmRetarget::TargetSkeleton skeleton = DesignAvatar();
    vrmRetarget::HumanoidMap map = DesignMap(skeleton);

    assert(map.GetMappedCount() == 3);
    assert(map.IsMapped(motion::HumanBone::Hips));
    assert(!map.IsMapped(motion::HumanBone::Head));
    assert(map.GetJointIndex(motion::HumanBone::Head)
           == vrmRetarget::HumanoidMap::kUnmapped);

    // An unknown token leaves the bone unmapped instead of guessing.
    assert(!map.SetJointToken(motion::HumanBone::Head, "NoSuchJoint", skeleton));
    assert(!map.IsMapped(motion::HumanBone::Head));

    const std::vector<motion::HumanBone> missing =
        map.FindMissingRequiredBones();
    assert(!missing.empty());
    assert(std::find(missing.begin(), missing.end(), motion::HumanBone::Head)
           != missing.end());
    assert(std::find(missing.begin(), missing.end(), motion::HumanBone::Hips)
           == missing.end());

    assert(map.FindDuplicateJointIndices().empty());
    assert(map.SetJointToken(motion::HumanBone::UpperChest,
                             "Root/Pelvis/SpineA/ChestA", skeleton));
    const std::vector<int> duplicates = map.FindDuplicateJointIndices();
    assert(duplicates.size() == 1 && duplicates[0] == 3);
}

void
TestIdentityRestPosesPassRotationsThrough()
{
    const vrmRetarget::TargetSkeleton skeleton = DesignAvatar();
    const vrmRetarget::HumanoidMap map = DesignMap(skeleton);
    const vrmRetarget::RestPoseCorrection correction =
        vrmRetarget::ComputeRestPoseCorrection(DesignSourceRest(), skeleton,
                                               map);

    const pxr::GfQuatf sample = Rotation(kAxisY, 90.0f);
    assert(SameOrientation(correction.Apply(motion::HumanBone::Hips, sample),
                           sample));
    assert(correction.identity[static_cast<std::size_t>(motion::HumanBone::Hips)]);
}

// The correction's contract is that the bone's world rotation *away from its
// own rest* survives the change of rig. Verify it directly rather than
// trusting the closed form.
void
TestRestPoseCorrectionPreservesTheWorldDelta()
{
    const pxr::GfQuatf sourceParentRest = Rotation(kAxisX, 20.0f);
    const pxr::GfQuatf sourceRest = Rotation(kAxisZ, -35.0f);
    const pxr::GfQuatf targetParentRest = Rotation(kAxisY, 50.0f);
    const pxr::GfQuatf targetRest = Rotation(kAxisX, 15.0f);

    vrmRetarget::TargetSkeleton skeleton;
    vrmRetarget::TargetJoint parent;
    parent.token = "Hips";
    parent.restRotation = targetParentRest;
    skeleton.AddJoint(parent);
    vrmRetarget::TargetJoint child;
    child.token = "Hips/Spine";
    child.restRotation = targetRest;
    skeleton.AddJoint(child);
    skeleton.ResolveParentsFromTokens();

    vrmRetarget::SourceRestPose sourceRestPose;
    sourceRestPose.localRotations[static_cast<std::size_t>(
        motion::HumanBone::Hips)] = sourceParentRest;
    sourceRestPose.localRotations[static_cast<std::size_t>(
        motion::HumanBone::Spine)] = sourceRest;
    sourceRestPose.SetParent(motion::HumanBone::Spine, motion::HumanBone::Hips);

    vrmRetarget::HumanoidMap map;
    map.SetJointToken(motion::HumanBone::Hips, "Hips", skeleton);
    map.SetJointToken(motion::HumanBone::Spine, "Hips/Spine", skeleton);

    const vrmRetarget::RestPoseCorrection correction =
        vrmRetarget::ComputeRestPoseCorrection(sourceRestPose, skeleton, map);
    assert(!correction.identity[static_cast<std::size_t>(
        motion::HumanBone::Spine)]);

    const pxr::GfQuatf animated = Rotation(kAxisY, 42.0f) * sourceRest;
    const pxr::GfQuatf retargeted =
        correction.Apply(motion::HumanBone::Spine, animated);

    // World delta = worldAnimated * worldRest^-1, with world = parent * local
    // (OpenUSD composition: `a * b` applies `b` first).
    const pxr::GfQuatf sourceDelta =
        (sourceParentRest * animated)
        * (sourceParentRest * sourceRest).GetInverse();
    const pxr::GfQuatf targetDelta =
        (targetParentRest * retargeted)
        * (targetParentRest * targetRest).GetInverse();
    assert(SameOrientation(sourceDelta, targetDelta));

    // A sample sitting at the source rest must land exactly on the target rest.
    assert(SameOrientation(
        correction.Apply(motion::HumanBone::Spine, sourceRest), targetRest));
}

void
TestRootMotionModes()
{
    const pxr::GfVec3f sourceRest(0.0f, 1.0f, 0.0f);
    const pxr::GfVec3f sourceNow(0.0f, 1.2f, 0.5f);
    const pxr::GfVec3f targetRest(0.0f, 1.6f, 0.0f);

    vrmRetarget::RootMotionOptions options;
    options.mode = vrmRetarget::RootMotionMode::Ignore;
    assert(NearlyEqual(vrmRetarget::ResolveRootTranslation(
                           options, sourceNow, sourceRest, targetRest),
                       targetRest));

    // The delta carries, not the absolute height: a 1.0 m rig drives a 1.6 m
    // one without the avatar snapping to the source's hip height.
    options.mode = vrmRetarget::RootMotionMode::Hips;
    assert(NearlyEqual(
        vrmRetarget::ResolveRootTranslation(options, sourceNow, sourceRest,
                                            targetRest),
        pxr::GfVec3f(0.0f, 1.8f, 0.5f)));

    options.translationScale = 2.0f;
    assert(NearlyEqual(
        vrmRetarget::ResolveRootTranslation(options, sourceNow, sourceRest,
                                            targetRest),
        pxr::GfVec3f(0.0f, 2.0f, 1.0f)));

    options.translationScale = 1.0f;
    options.preserveTargetHeight = true;
    assert(NearlyEqual(
        vrmRetarget::ResolveRootTranslation(options, sourceNow, sourceRest,
                                            targetRest),
        pxr::GfVec3f(0.0f, 1.6f, 0.5f)));
}

motion::HumanoidAnimation
DesignClip()
{
    motion::HumanoidAnimation animation;
    animation.startTime = 0.0;
    animation.endTime = 1.0;
    animation.nominalFrameRate = 30.0;

    const auto hips = static_cast<std::size_t>(motion::HumanBone::Hips);
    const auto spine = static_cast<std::size_t>(motion::HumanBone::Spine);
    const auto chest = static_cast<std::size_t>(motion::HumanBone::Chest);

    motion::HumanoidPose first;
    first.timestamp = 0.0;
    first.validRotations.set(hips);
    first.validRotations.set(spine);
    first.validRotations.set(chest);
    first.root.worldPosition = pxr::GfVec3f(0.0f, 1.0f, 0.0f);
    first.root.hasPosition = true;
    animation.samples.push_back(first);

    motion::HumanoidPose last;
    last.timestamp = 1.0;
    last.localRotations[hips] = Rotation(kAxisY, 90.0f);
    last.localRotations[chest] = Rotation(kAxisX, 90.0f);
    last.validRotations.set(hips);
    last.validRotations.set(spine);
    last.validRotations.set(chest);
    last.root.worldPosition = pxr::GfVec3f(0.0f, 1.0f, 0.5f);
    last.root.hasPosition = true;
    animation.samples.push_back(last);

    return animation;
}

// Reproduces docs/design/fixtures/motion/expected_retargeted.usda from
// canonical_walk.usda's values against avatar.usda's rig.
void
TestDesignTripletHandOff()
{
    const vrmRetarget::TargetSkeleton skeleton = DesignAvatar();
    const vrmRetarget::PoseRetargeter retargeter(skeleton, DesignMap(skeleton),
                                                 DesignSourceRest());

    vrmRetarget::RetargetDiagnostics diagnostics;
    const vrmRetarget::RetargetedAnimation result =
        retargeter.Retarget(DesignClip(), &diagnostics);

    assert(result.joints.size() == 4);
    assert(result.joints[0] == "Root");
    assert(result.joints[3] == "Root/Pelvis/SpineA/ChestA");
    assert(result.samples.size() == 2);

    const vrmRetarget::RetargetedPose& first = result.samples.front();
    for (const pxr::GfQuatf& rotation : first.rotations) {
        assert(SameOrientation(rotation, pxr::GfQuatf(1.0f, pxr::GfVec3f(0.0f))));
    }
    assert(NearlyEqual(first.translations[0], pxr::GfVec3f(0.0f)));
    assert(NearlyEqual(first.translations[1], pxr::GfVec3f(0.0f, 1.0f, 0.0f)));
    assert(NearlyEqual(first.translations[2], pxr::GfVec3f(0.0f, 0.5f, 0.0f)));
    assert(NearlyEqual(first.translations[3], pxr::GfVec3f(0.0f, 0.5f, 0.0f)));

    const vrmRetarget::RetargetedPose& last = result.samples.back();
    // Root is unmapped: it holds its rest pose while Pelvis takes the hips.
    assert(SameOrientation(last.rotations[0], pxr::GfQuatf(1.0f, pxr::GfVec3f(0.0f))));
    assert(SameOrientation(last.rotations[1], Rotation(kAxisY, 90.0f)));
    assert(SameOrientation(last.rotations[2], pxr::GfQuatf(1.0f, pxr::GfVec3f(0.0f))));
    assert(SameOrientation(last.rotations[3], Rotation(kAxisX, 90.0f)));
    assert(NearlyEqual(last.translations[0], pxr::GfVec3f(0.0f)));
    assert(NearlyEqual(last.translations[1], pxr::GfVec3f(0.0f, 1.0f, 0.5f)));
    assert(NearlyEqual(last.translations[2], pxr::GfVec3f(0.0f, 0.5f, 0.0f)));
    assert(NearlyEqual(last.translations[3], pxr::GfVec3f(0.0f, 0.5f, 0.0f)));

    // The rig maps three bones, so the required-bone gap must be reported.
    assert(!diagnostics.IsClean());
    assert(!diagnostics.missingRequiredBones.empty());
    assert(diagnostics.unmappedSourceBones.empty());
}

void
TestUnmappedJointsStayAtRestAndAreReported()
{
    vrmRetarget::TargetSkeleton skeleton = DesignAvatar();
    vrmRetarget::HumanoidMap map;
    // Bind hips only: the clip also drives spine and chest.
    map.SetJointToken(motion::HumanBone::Hips, "Root/Pelvis", skeleton);

    const vrmRetarget::PoseRetargeter retargeter(skeleton, map,
                                                 DesignSourceRest());
    vrmRetarget::RetargetDiagnostics diagnostics;
    const vrmRetarget::RetargetedAnimation result =
        retargeter.Retarget(DesignClip(), &diagnostics);

    const vrmRetarget::RetargetedPose& last = result.samples.back();
    // SpineA and ChestA keep their rest transforms rather than collapsing.
    assert(SameOrientation(last.rotations[2], pxr::GfQuatf(1.0f, pxr::GfVec3f(0.0f))));
    assert(SameOrientation(last.rotations[3], pxr::GfQuatf(1.0f, pxr::GfVec3f(0.0f))));
    assert(NearlyEqual(last.translations[3], pxr::GfVec3f(0.0f, 0.5f, 0.0f)));

    assert(diagnostics.unmappedSourceBones.size() == 2);
    assert(!diagnostics.warnings.empty());
}

void
TestResampleOptionDrivesSampleCount()
{
    const vrmRetarget::TargetSkeleton skeleton = DesignAvatar();
    vrmRetarget::RetargetOptions options;
    options.resampleRate = 4.0;
    const vrmRetarget::PoseRetargeter retargeter(
        skeleton, DesignMap(skeleton), DesignSourceRest(), options);

    const vrmRetarget::RetargetedAnimation result =
        retargeter.Retarget(DesignClip());
    assert(result.samples.size() == 5);
    assert(NearlyEqual(static_cast<float>(result.frameRate), 4.0f));
    assert(NearlyEqual(static_cast<float>(result.endTime), 1.0f));
    // Midpoint of a 0 -> 0.5 m hips advance.
    assert(NearlyEqual(result.samples[2].translations[1],
                       pxr::GfVec3f(0.0f, 1.0f, 0.25f)));
}

void
TestRootJointModeMovesTheReceiver()
{
    const vrmRetarget::TargetSkeleton skeleton = DesignAvatar();
    vrmRetarget::RetargetOptions options;
    options.rootMotion.mode = vrmRetarget::RootMotionMode::RootJoint;
    options.rootMotion.rootJointIndex = 0;
    const vrmRetarget::PoseRetargeter retargeter(
        skeleton, DesignMap(skeleton), DesignSourceRest(), options);

    const vrmRetarget::RetargetedAnimation result =
        retargeter.Retarget(DesignClip());
    const vrmRetarget::RetargetedPose& last = result.samples.back();
    // Root takes the delta; Pelvis stays at its rest translation.
    assert(NearlyEqual(last.translations[0], pxr::GfVec3f(0.0f, 0.0f, 0.5f)));
    assert(NearlyEqual(last.translations[1], pxr::GfVec3f(0.0f, 1.0f, 0.0f)));

    // An invalid root index degrades to "author nothing" and says so.
    options.rootMotion.rootJointIndex = -1;
    const vrmRetarget::PoseRetargeter degraded(
        skeleton, DesignMap(skeleton), DesignSourceRest(), options);
    vrmRetarget::RetargetDiagnostics diagnostics;
    const vrmRetarget::RetargetedAnimation fallback =
        degraded.Retarget(DesignClip(), &diagnostics);
    assert(NearlyEqual(fallback.samples.back().translations[1],
                       pxr::GfVec3f(0.0f, 1.0f, 0.0f)));
    assert(!diagnostics.warnings.empty());
}

} // namespace

int
main()
{
    TestSkeletonParentsComeFromJointPaths();
    TestHumanoidMapReportsGapsAndCollisions();
    TestIdentityRestPosesPassRotationsThrough();
    TestRestPoseCorrectionPreservesTheWorldDelta();
    TestRootMotionModes();
    TestDesignTripletHandOff();
    TestUnmappedJointsStayAtRestAndAreReported();
    TestResampleOptionDrivesSampleCount();
    TestRootJointModeMovesTheReceiver();
    std::puts("vrmRetarget unit tests passed");
    return 0;
}
