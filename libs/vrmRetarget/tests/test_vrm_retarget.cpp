// SPDX-License-Identifier: Apache-2.0
#include "vrmRetarget/ExpressionResolver.h"
#include "vrmRetarget/HumanoidMap.h"
#include "vrmRetarget/LookAtEvaluator.h"
#include "vrmRetarget/PoseRetargeter.h"
#include "vrmRetarget/RestPose.h"
#include "vrmRetarget/RootMotionPolicy.h"
#include "vrmRetarget/TargetSkeleton.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

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

bool
NearlyEqual(const pxr::GfVec4f& a, const pxr::GfVec4f& b)
{
    return NearlyEqual(a[0], b[0]) && NearlyEqual(a[1], b[1])
        && NearlyEqual(a[2], b[2]) && NearlyEqual(a[3], b[3]);
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

// The same invariant one level deeper. A grandparent's rest rotation reaches
// the bone only through the accumulated chain, so a correction built from each
// parent's own local rotation passes the two-level test above and fails here.
void
TestRestPoseCorrectionAccountsForTheWholeAncestorChain()
{
    const pxr::GfQuatf targetHipsRest = Rotation(kAxisY, 25.0f);
    const pxr::GfQuatf targetSpineRest = Rotation(kAxisX, -15.0f);
    const pxr::GfQuatf targetChestRest = Rotation(kAxisZ, 40.0f);
    const pxr::GfQuatf sourceHipsRest = Rotation(kAxisZ, -10.0f);
    const pxr::GfQuatf sourceSpineRest = Rotation(kAxisY, 30.0f);
    const pxr::GfQuatf sourceChestRest = Rotation(kAxisX, 55.0f);

    vrmRetarget::TargetSkeleton skeleton;
    vrmRetarget::TargetJoint hips;
    hips.token = "Hips";
    hips.restRotation = targetHipsRest;
    skeleton.AddJoint(hips);
    vrmRetarget::TargetJoint spine;
    spine.token = "Hips/Spine";
    spine.restRotation = targetSpineRest;
    skeleton.AddJoint(spine);
    vrmRetarget::TargetJoint chest;
    chest.token = "Hips/Spine/Chest";
    chest.restRotation = targetChestRest;
    skeleton.AddJoint(chest);
    skeleton.ResolveParentsFromTokens();

    vrmRetarget::SourceRestPose sourceRest;
    sourceRest.localRotations[static_cast<std::size_t>(motion::HumanBone::Hips)] =
        sourceHipsRest;
    sourceRest.localRotations[static_cast<std::size_t>(motion::HumanBone::Spine)] =
        sourceSpineRest;
    sourceRest.localRotations[static_cast<std::size_t>(motion::HumanBone::Chest)] =
        sourceChestRest;
    sourceRest.SetParent(motion::HumanBone::Spine, motion::HumanBone::Hips);
    sourceRest.SetParent(motion::HumanBone::Chest, motion::HumanBone::Spine);

    // Both accumulators compose root-first.
    assert(SameOrientation(skeleton.GetWorldRestRotation(2),
                           targetHipsRest * targetSpineRest * targetChestRest));
    assert(SameOrientation(
        sourceRest.GetWorldRestRotation(motion::HumanBone::Chest),
        sourceHipsRest * sourceSpineRest * sourceChestRest));
    // A root joint's absent parent contributes identity, not a dangling index.
    assert(SameOrientation(
        skeleton.GetWorldRestRotation(vrmRetarget::TargetSkeleton::kNoParent),
        pxr::GfQuatf(1.0f, pxr::GfVec3f(0.0f))));

    vrmRetarget::HumanoidMap map;
    map.SetJointToken(motion::HumanBone::Hips, "Hips", skeleton);
    map.SetJointToken(motion::HumanBone::Spine, "Hips/Spine", skeleton);
    map.SetJointToken(motion::HumanBone::Chest, "Hips/Spine/Chest", skeleton);

    const vrmRetarget::RestPoseCorrection correction =
        vrmRetarget::ComputeRestPoseCorrection(sourceRest, skeleton, map);

    const pxr::GfQuatf animated = Rotation(kAxisY, 42.0f) * sourceChestRest;
    const pxr::GfQuatf retargeted =
        correction.Apply(motion::HumanBone::Chest, animated);

    const pxr::GfQuatf sourceParentWorld = sourceHipsRest * sourceSpineRest;
    const pxr::GfQuatf targetParentWorld = targetHipsRest * targetSpineRest;
    const pxr::GfQuatf sourceDelta =
        (sourceParentWorld * animated)
        * (sourceParentWorld * sourceChestRest).GetInverse();
    const pxr::GfQuatf targetDelta =
        (targetParentWorld * retargeted)
        * (targetParentWorld * targetChestRest).GetInverse();
    assert(SameOrientation(sourceDelta, targetDelta));

    // And the rest pose itself still maps onto the target's rest pose.
    assert(SameOrientation(
        correction.Apply(motion::HumanBone::Chest, sourceChestRest),
        targetChestRest));
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


// ---------------------------------------------------------------------------
// ExpressionResolve: a producer reports a name and a weight, the avatar carries
// the binds, and the join key is the verbatim name on both sides.
// ---------------------------------------------------------------------------

// An avatar with two expressions. `happy` drives two morph targets across two
// meshes plus a material colour -- the N-across-M shape the resolve exists for
// -- and `blink` drives one target of one mesh and is binary.
vrmRetarget::ExpressionRig
DesignExpressionRig()
{
    vrmRetarget::ExpressionRig rig;

    vrmRetarget::ExpressionDefinition happy;
    happy.name = "happy";
    happy.morphTargets.push_back({"/Asset/Meshes/Face/Smile", 1.0f});
    happy.morphTargets.push_back({"/Asset/Meshes/Brows/Raise", 0.5f});
    happy.materialColors.push_back(
        {"/Asset/Materials/Face", "color", pxr::GfVec4f(1.0f, 0.0f, 0.0f, 1.0f)});
    rig.Add(happy);

    vrmRetarget::ExpressionDefinition blink;
    blink.name = "blink";
    blink.isBinary = true;
    blink.morphTargets.push_back({"/Asset/Meshes/Face/EyeClose", 1.0f});
    rig.Add(blink);

    return rig;
}

motion::ExpressionWeights
Weights(std::initializer_list<std::pair<const char*, float>> entries)
{
    motion::ExpressionWeights weights;
    for (const auto& entry : entries) {
        weights.Set(entry.first, entry.second);
    }
    return weights;
}

void
TestExpressionRigDeclaresANameOnce()
{
    vrmRetarget::ExpressionRig rig = DesignExpressionRig();
    assert(rig.GetSize() == 2);

    // A second declaration of a declared name is refused rather than shadowing
    // the first -- the join key has to be unique or it is not a key.
    vrmRetarget::ExpressionDefinition duplicate;
    duplicate.name = "happy";
    duplicate.morphTargets.push_back({"/Asset/Meshes/Face/Other", 1.0f});
    assert(!rig.Add(duplicate));
    assert(rig.GetSize() == 2);
    assert(rig.Find("happy")->morphTargets[0].target
           == "/Asset/Meshes/Face/Smile");

    // A nameless expression cannot be joined on, so it is not a definition.
    vrmRetarget::ExpressionDefinition nameless;
    assert(!rig.Add(nameless));

    // Sorted by name, whatever order they arrived in.
    assert(rig.GetExpressions()[0].name == "blink");
    assert(rig.Find("relaxed") == nullptr);
}

void
TestOneWeightExpandsOntoEveryBind()
{
    const vrmRetarget::ExpressionResolver resolver(DesignExpressionRig());

    vrmRetarget::ExpressionDiagnostics diagnostics;
    const vrmRetarget::ResolvedExpressions resolved =
        resolver.Resolve(Weights({{"happy", 0.5f}}), &diagnostics);

    assert(diagnostics.IsClean());
    // Two targets, sorted by target: Brows/Raise before Face/Smile.
    assert(resolved.morphTargets.size() == 2);
    assert(resolved.morphTargets[0].target == "/Asset/Meshes/Brows/Raise");
    // The bind's own 0.5 is the avatar's, and it multiplies the clip's.
    assert(NearlyEqual(resolved.morphTargets[0].weight, 0.25f));
    assert(resolved.morphTargets[1].target == "/Asset/Meshes/Face/Smile");
    assert(NearlyEqual(resolved.morphTargets[1].weight, 0.5f));

    // The colour is carried as (total weight, weighted target) so the material's
    // own base value never has to reach this library: Apply is the lerp.
    assert(resolved.materialColors.size() == 1);
    const vrmRetarget::ResolvedMaterialColor& color = resolved.materialColors[0];
    assert(color.material == "/Asset/Materials/Face");
    assert(color.colorType == "color");
    assert(NearlyEqual(color.totalWeight, 0.5f));
    const pxr::GfVec4f base(1.0f, 1.0f, 1.0f, 1.0f);
    assert(NearlyEqual(color.Apply(base), pxr::GfVec4f(1.0f, 0.5f, 0.5f, 1.0f)));
}

void
TestReportedZeroIsAuthoredAndUnreportedIsAbsent()
{
    const vrmRetarget::ExpressionResolver resolver(DesignExpressionRig());

    // A reported zero is a statement -- "this expression is off now" -- so its
    // targets are authored at zero. Dropping them would leave the previous
    // sample's weight standing on the rig.
    const vrmRetarget::ResolvedExpressions off =
        resolver.Resolve(Weights({{"happy", 0.0f}}));
    assert(off.morphTargets.size() == 2);
    assert(NearlyEqual(off.morphTargets[0].weight, 0.0f));
    assert(off.materialColors.size() == 1);
    assert(NearlyEqual(off.materialColors[0].totalWeight, 0.0f));
    // Apply with no weight is the material's own value, untouched.
    const pxr::GfVec4f base(0.25f, 0.5f, 0.75f, 1.0f);
    assert(NearlyEqual(off.materialColors[0].Apply(base), base));

    // `blink` was not reported, so its target is absent rather than zero: an
    // unreported name is not a zero weight, and this layer does not invent one
    // for the binds behind it either.
    for (const vrmRetarget::ResolvedMorphTarget& target : off.morphTargets) {
        assert(target.target != "/Asset/Meshes/Face/EyeClose");
    }

    // Nothing reported resolves to nothing at all.
    assert(resolver.Resolve(motion::ExpressionWeights()).IsEmpty());
}

void
TestBinaryRoundsAndOutOfRangeIsClamped()
{
    const vrmRetarget::ExpressionResolver resolver(DesignExpressionRig());

    float weight = -1.0f;
    assert(resolver.ResolveWeight("blink", 0.4f, &weight));
    assert(NearlyEqual(weight, 0.0f));
    assert(resolver.ResolveWeight("blink", 0.5f, &weight));
    assert(NearlyEqual(weight, 1.0f));
    // "Does not resolve" is distinguishable from "resolves to zero", and the
    // sentinel differs from the reported weight so that an implementation
    // writing through the pointer before the early return fails here.
    weight = -7.0f;
    assert(!resolver.ResolveWeight("relaxed", 1.0f, &weight));
    assert(NearlyEqual(weight, -7.0f));

    // The clip reader carries a weight outside [0, 1] verbatim and leaves the
    // clamp to whoever applies it to a rig, which is here -- and the operator
    // is told which name it was.
    vrmRetarget::ExpressionDiagnostics diagnostics;
    const vrmRetarget::ResolvedExpressions resolved =
        resolver.Resolve(Weights({{"happy", 1.5f}, {"blink", -0.2f}}),
                         &diagnostics);
    assert(NearlyEqual(resolved.morphTargets[2].weight, 1.0f));
    assert(diagnostics.clampedNames.size() == 2);
    assert(diagnostics.clampedNames[0] == "blink");
    assert(diagnostics.unresolvedNames.empty());

    // A binary expression clamped to 0 still authors its target at 0.
    assert(resolved.morphTargets[1].target == "/Asset/Meshes/Face/EyeClose");
    assert(NearlyEqual(resolved.morphTargets[1].weight, 0.0f));

    // The rounding has to happen on the way to the binds and not only in
    // ResolveWeight: a partly-open eyelid is exactly what `isBinary` says this
    // rig cannot show, so 0.4 reaches the target as 0 and 0.6 as 1. Written
    // because the suite passed once with this line deleted from Resolve.
    const vrmRetarget::ResolvedExpressions ajar =
        resolver.Resolve(Weights({{"blink", 0.4f}}));
    assert(ajar.morphTargets.size() == 1);
    assert(ajar.morphTargets[0].target == "/Asset/Meshes/Face/EyeClose");
    assert(NearlyEqual(ajar.morphTargets[0].weight, 0.0f));
    assert(NearlyEqual(
        resolver.Resolve(Weights({{"blink", 0.6f}})).morphTargets[0].weight,
        1.0f));

    // Turning the clamp off resolves what the producer actually said.
    vrmRetarget::ExpressionResolveOptions verbatim;
    verbatim.clampWeights = false;
    const vrmRetarget::ExpressionResolver unclamped(DesignExpressionRig(),
                                                    verbatim);
    assert(NearlyEqual(
        unclamped.Resolve(Weights({{"happy", 1.5f}})).morphTargets[1].weight,
        1.5f));
}

void
TestExpressionsAccumulateOnOneTarget()
{
    // Two expressions of the same rig driving one target is a rig that can sum
    // past 1, and the sum is carried through rather than corrected.
    vrmRetarget::ExpressionRig rig;
    vrmRetarget::ExpressionDefinition happy;
    happy.name = "happy";
    happy.morphTargets.push_back({"/Asset/Meshes/Face/Smile", 0.8f});
    happy.materialColors.push_back(
        {"/Asset/Materials/Face", "color", pxr::GfVec4f(1.0f, 0.0f, 0.0f, 1.0f)});
    rig.Add(happy);
    vrmRetarget::ExpressionDefinition aa;
    aa.name = "aa";
    aa.morphTargets.push_back({"/Asset/Meshes/Face/Smile", 0.8f});
    aa.materialColors.push_back(
        {"/Asset/Materials/Face", "color", pxr::GfVec4f(0.0f, 0.0f, 1.0f, 1.0f)});
    rig.Add(aa);

    const vrmRetarget::ExpressionResolver resolver(rig);
    vrmRetarget::ExpressionDiagnostics diagnostics;
    const vrmRetarget::ResolvedExpressions resolved =
        resolver.Resolve(Weights({{"happy", 1.0f}, {"aa", 1.0f}}),
                         &diagnostics);

    assert(resolved.morphTargets.size() == 1);
    assert(NearlyEqual(resolved.morphTargets[0].weight, 1.6f));
    assert(resolved.materialColors.size() == 1);
    assert(NearlyEqual(resolved.materialColors[0].totalWeight, 2.0f));
    // Two warnings, one per over-driven channel, and neither is an error: the
    // rig said it, so the operator hears it.
    assert(diagnostics.warnings.size() == 2);

    // Half of each stays inside 1 and says nothing.
    vrmRetarget::ExpressionDiagnostics quiet;
    const vrmRetarget::ResolvedExpressions half =
        resolver.Resolve(Weights({{"happy", 0.5f}, {"aa", 0.5f}}), &quiet);
    assert(quiet.IsClean());
    assert(NearlyEqual(half.morphTargets[0].weight, 0.8f));
    // The colour lerps toward both targets at once: base is pushed out entirely
    // and the two weighted targets are what is left.
    assert(NearlyEqual(half.materialColors[0].Apply(pxr::GfVec4f(1.0f)),
                       pxr::GfVec4f(0.5f, 0.0f, 0.5f, 1.0f)));
}

void
TestAnUnresolvedNameIsNamedOnceForAWholeClip()
{
    const vrmRetarget::ExpressionResolver resolver(DesignExpressionRig());

    motion::HumanoidPose pose;
    pose.timestamp = 0.5;
    pose.expressions.Set("happy", 0.25f);
    // A custom name this avatar does not declare. The clip is not wrong -- it
    // was authored against no avatar in particular -- but the loss is named.
    pose.expressions.Set("照れ", 1.0f);

    vrmRetarget::ExpressionDiagnostics diagnostics;
    for (int sample = 0; sample < 3; ++sample) {
        const vrmRetarget::ResolvedExpressions resolved =
            resolver.Resolve(pose, &diagnostics);
        // The pose overload carries the sample's own time through.
        assert(NearlyEqual(static_cast<float>(resolved.timestamp), 0.5f));
        assert(resolved.morphTargets.size() == 2);
    }
    // Three samples, one line: diagnostics accumulate without repeating.
    assert(diagnostics.unresolvedNames.size() == 1);
    assert(diagnostics.unresolvedNames[0] == "照れ");
    assert(diagnostics.clampedNames.empty());
    assert(diagnostics.warnings.empty());
}

void
TestANonNumberIsNotAWeight()
{
    const vrmRetarget::ExpressionResolver resolver(DesignExpressionRig());
    const float notANumber = std::nanf("");

    // Every comparison against NaN is false, so a range test written as
    // `weight < 0 || weight > 1` calls it "already inside [0, 1]" and lets it
    // through to the binds, the totals and Apply() with nothing reported. It
    // clamps to 0 -- the only value that leaves the rig where it was -- and is
    // named beside the ordinary out-of-range weights.
    vrmRetarget::ExpressionDiagnostics diagnostics;
    const vrmRetarget::ResolvedExpressions resolved =
        resolver.Resolve(Weights({{"happy", notANumber}}), &diagnostics);
    assert(resolved.morphTargets.size() == 2);
    assert(NearlyEqual(resolved.morphTargets[0].weight, 0.0f));
    assert(NearlyEqual(resolved.morphTargets[1].weight, 0.0f));
    assert(NearlyEqual(resolved.materialColors[0].totalWeight, 0.0f));
    assert(diagnostics.clampedNames.size() == 1);
    assert(diagnostics.clampedNames[0] == "happy");
    assert(!diagnostics.IsClean());

    float weight = -7.0f;
    assert(resolver.ResolveWeight("happy", notANumber, &weight));
    assert(NearlyEqual(weight, 0.0f));

    // An infinity is the same question with an answer the comparisons already
    // gave; it is here so the two cannot drift apart.
    vrmRetarget::ExpressionDiagnostics infinite;
    assert(NearlyEqual(
        resolver.Resolve(Weights({{"happy", HUGE_VALF}}), &infinite)
            .morphTargets[1].weight,
        1.0f));
    assert(infinite.clampedNames.size() == 1);

    // With clamping off the value reaches the binds, because that mode
    // resolves what the producer said -- but a resolve that carried a NaN into
    // an avatar must not read as a clean one.
    vrmRetarget::ExpressionResolveOptions verbatim;
    verbatim.clampWeights = false;
    const vrmRetarget::ExpressionResolver unclamped(DesignExpressionRig(),
                                                    verbatim);
    vrmRetarget::ExpressionDiagnostics carried;
    const vrmRetarget::ResolvedExpressions raw =
        unclamped.Resolve(Weights({{"happy", notANumber}}), &carried);
    assert(std::isnan(raw.morphTargets[0].weight));
    // One warning for the expression, and one for each of the three channels
    // its NaN reached -- a count that says how far the value got.
    assert(carried.warnings.size() == 4);
    assert(!carried.IsClean());
}

void
TestABindWithNoIdentifierIsSkippedAndNamed()
{
    // Half a bind is not a bind: a morph target with no path, a colour with no
    // material, and a colour with no slot. The last one is the subtle one --
    // the slot is half the accumulator's key, so an empty one would merge two
    // binds of one material and hand back a colour nothing can map to a shader
    // input.
    vrmRetarget::ExpressionRig rig;
    vrmRetarget::ExpressionDefinition broken;
    broken.name = "happy";
    broken.morphTargets.push_back({"", 1.0f});
    broken.morphTargets.push_back({"/Asset/Meshes/Face/Smile", 1.0f});
    broken.materialColors.push_back(
        {"", "color", pxr::GfVec4f(1.0f, 0.0f, 0.0f, 1.0f)});
    broken.materialColors.push_back(
        {"/Asset/Materials/Face", "", pxr::GfVec4f(1.0f, 0.0f, 0.0f, 1.0f)});
    broken.materialColors.push_back(
        {"/Asset/Materials/Face", "", pxr::GfVec4f(0.0f, 1.0f, 0.0f, 1.0f)});
    broken.materialColors.push_back(
        {"/Asset/Materials/Face", "emissionColor",
         pxr::GfVec4f(0.0f, 0.0f, 1.0f, 1.0f)});
    rig.Add(broken);

    const vrmRetarget::ExpressionResolver resolver(rig);
    vrmRetarget::ExpressionDiagnostics diagnostics;
    const vrmRetarget::ResolvedExpressions resolved =
        resolver.Resolve(Weights({{"happy", 1.0f}}), &diagnostics);

    // The bind that could be resolved was, and only it.
    assert(resolved.morphTargets.size() == 1);
    assert(resolved.morphTargets[0].target == "/Asset/Meshes/Face/Smile");
    assert(resolved.materialColors.size() == 1);
    assert(resolved.materialColors[0].colorType == "emissionColor");
    // One line per shape, and the two slotless binds did not merge into a
    // single accumulator on their way to being refused.
    assert(diagnostics.warnings.size() == 3);
}

void
TestATargetDrivenBelowZeroIsReportedToo()
{
    // A negative bind weight is a rig this library does not validate, so a
    // fully-on expression can drive a target below 0. It extrapolates, exactly
    // as driving one past 1 does, and a report that named only the upper side
    // would leave this looking clean.
    vrmRetarget::ExpressionRig rig;
    vrmRetarget::ExpressionDefinition frown;
    frown.name = "sad";
    frown.morphTargets.push_back({"/Asset/Meshes/Face/Smile", -1.0f});
    rig.Add(frown);

    const vrmRetarget::ExpressionResolver resolver(rig);
    vrmRetarget::ExpressionDiagnostics diagnostics;
    const vrmRetarget::ResolvedExpressions resolved =
        resolver.Resolve(Weights({{"sad", 1.0f}}), &diagnostics);
    assert(NearlyEqual(resolved.morphTargets[0].weight, -1.0f));
    assert(diagnostics.warnings.size() == 1);
    assert(!diagnostics.IsClean());

    // The same in the other mode: a negative report with clamping off drives
    // every bind of the expression negative.
    vrmRetarget::ExpressionResolveOptions verbatim;
    verbatim.clampWeights = false;
    const vrmRetarget::ExpressionResolver unclamped(DesignExpressionRig(),
                                                    verbatim);
    vrmRetarget::ExpressionDiagnostics carried;
    const vrmRetarget::ResolvedExpressions raw =
        unclamped.Resolve(Weights({{"happy", -0.5f}}), &carried);
    assert(NearlyEqual(raw.morphTargets[1].weight, -0.5f));
    assert(NearlyEqual(raw.materialColors[0].totalWeight, -0.5f));
    // Apply extrapolates past the material's own value rather than toward the
    // bind's target, which is the thing worth being told about.
    assert(NearlyEqual(raw.materialColors[0].Apply(pxr::GfVec4f(1.0f)),
                       pxr::GfVec4f(1.0f, 1.5f, 1.5f, 1.0f)));
    // Three channels outside the range -- two morph targets and one colour
    // slot -- one warning each, and nothing clamped.
    assert(carried.warnings.size() == 3);
    assert(carried.clampedNames.empty());
}

void
TestAJointsWorldTransformComposesItsWholeChain()
{
    // LookAtEvaluator needs to know where the head *is*, and a RetargetedPose
    // states only where each joint sits relative to its parent. Composing that
    // is the piece between them, and the translation is the half that is easy
    // to get wrong: a parent's rotation has to turn the child's offset before
    // it is added, so a rotated spine moves the chest sideways rather than
    // further up.
    const vrmRetarget::TargetSkeleton skeleton = DesignAvatar();
    vrmRetarget::RetargetedPose pose;
    for (const vrmRetarget::TargetJoint& joint : skeleton.GetJoints()) {
        pose.rotations.push_back(joint.restRotation);
        pose.translations.push_back(joint.restTranslation);
    }

    const int chest = skeleton.FindJoint("Root/Pelvis/SpineA/ChestA");
    assert(chest >= 0);
    pxr::GfQuatf orientation(1.0f);
    pxr::GfVec3f position(0.0f);
    assert(vrmRetarget::GetJointWorldTransform(skeleton, pose, chest,
                                               &orientation, &position));
    // 1 m to the pelvis plus two half-metre segments, all straight up.
    assert(NearlyEqual(position, pxr::GfVec3f(0.0f, 2.0f, 0.0f)));
    assert(SameOrientation(orientation, pxr::GfQuatf(1.0f)));

    // Tip the pelvis a quarter turn about +X: everything above it swings from
    // vertical onto +Z, and the pelvis's own metre of height stays.
    const int pelvis = skeleton.FindJoint("Root/Pelvis");
    pose.rotations[static_cast<std::size_t>(pelvis)] = Rotation(kAxisX, 90.0f);
    assert(vrmRetarget::GetJointWorldTransform(skeleton, pose, chest,
                                               &orientation, &position));
    assert(NearlyEqual(position, pxr::GfVec3f(0.0f, 1.0f, 1.0f)));
    assert(SameOrientation(orientation, Rotation(kAxisX, 90.0f)));

    // A pose that is not the skeleton's own width answers nothing rather than
    // reading past the end of it, and neither output is touched.
    vrmRetarget::RetargetedPose truncated = pose;
    truncated.rotations.pop_back();
    assert(!vrmRetarget::GetJointWorldTransform(skeleton, truncated, chest,
                                                &orientation, &position));
    assert(!vrmRetarget::GetJointWorldTransform(skeleton, pose, -1,
                                                &orientation, &position));
    assert(NearlyEqual(position, pxr::GfVec3f(0.0f, 1.0f, 1.0f)));
}

// ---------------------------------------------------------------------------
// LookAtEvaluator
// ---------------------------------------------------------------------------

// A range map that is the identity over [0, 90] degrees, so a resolved eye
// rotation is the aim itself and the test measures the geometry rather than a
// curve on top of it.
vrmRetarget::LookAtRangeMap
IdentityMap()
{
    vrmRetarget::LookAtRangeMap map;
    map.inputMaxValue = 90.0f;
    map.outputScale = 90.0f;
    return map;
}

vrmRetarget::LookAtRig
IdentityBoneRig()
{
    vrmRetarget::LookAtRig rig;
    rig.type = vrmRetarget::LookAtType::Bone;
    rig.leftEyeJoint = "Root/Hips/Spine/Head/LeftEye";
    rig.rightEyeJoint = "Root/Hips/Spine/Head/RightEye";
    rig.horizontalInner = IdentityMap();
    rig.horizontalOuter = IdentityMap();
    rig.verticalDown = IdentityMap();
    rig.verticalUp = IdentityMap();
    // An explicit zero: this rig states that its eyes sit on the head joint,
    // which is a measurement rather than the absence of one. The tests that are
    // about the absence clear it.
    rig.offsetFromHeadBone = pxr::GfVec3f(0.0f);
    return rig;
}

// The same rig driven through the face instead. Every map is the identity onto
// a unit weight, so a gaze at the limit of a range is a weight of exactly 1.
vrmRetarget::LookAtRig
IdentityExpressionRig()
{
    vrmRetarget::LookAtRig rig = IdentityBoneRig();
    rig.type = vrmRetarget::LookAtType::Expression;
    rig.horizontalInner.outputScale = 1.0f;
    rig.horizontalOuter.outputScale = 1.0f;
    rig.verticalDown.outputScale = 1.0f;
    rig.verticalUp.outputScale = 1.0f;
    return rig;
}

vrmRetarget::LookAtInput
GazeAt(const pxr::GfVec3f& target)
{
    vrmRetarget::LookAtInput input;
    input.target = target;
    return input;
}

void
TestAnIdentityRangeMapReproducesTheAim()
{
    // The composition order is the claim: yaw about +Y, then pitch about +X
    // negated, because a positive right-handed rotation about +X takes the
    // forward axis down. Get either wrong and a rig whose maps do nothing --
    // 90 degrees of input onto 90 degrees of output -- still fails to point at
    // the thing it is aiming at. So this measures the round trip rather than
    // asserting the two angles.
    const vrmRetarget::LookAtEvaluator evaluator(IdentityBoneRig());
    vrmRetarget::LookAtDiagnostics diagnostics;
    const pxr::GfVec3f target(1.0f, 1.0f, 1.0f);
    const vrmRetarget::ResolvedLookAt resolved =
        evaluator.Evaluate(GazeAt(target), &diagnostics);

    assert(resolved.hasGaze);
    assert(NearlyEqual(resolved.yawDegrees, 45.0f));
    assert(NearlyEqual(resolved.pitchDegrees, 35.26439f));
    assert(resolved.eyeRotations.size() == 2);
    for (const vrmRetarget::LookAtEyeRotation& eye : resolved.eyeRotations) {
        assert(NearlyEqual(eye.rotation.Transform(kAxisZ),
                           target.GetNormalized()));
    }
    // An absent target is the only thing this counts as a sample without one,
    // and there was none.
    assert(diagnostics.samplesEvaluated == 1);
    assert(diagnostics.samplesWithoutTarget == 0);

    // +X is the character's own left in the basis VRM inherits from glTF, so a
    // target on that side is a positive yaw. The mirror image is the same
    // magnitude with the other sign, which is what makes the inner/outer choice
    // below a choice about a side rather than about a formula.
    const vrmRetarget::ResolvedLookAt mirrored =
        evaluator.Evaluate(GazeAt(pxr::GfVec3f(-1.0f, 1.0f, 1.0f)));
    assert(NearlyEqual(mirrored.yawDegrees, -45.0f));
    assert(NearlyEqual(mirrored.pitchDegrees, 35.26439f));
}

void
TestTheOffsetPlacesTheGazeOrigin()
{
    // The gaze starts at the eyes, not at the head joint, and the offset that
    // says where they are is stated in the head's own space -- so it has to be
    // rotated by the head before it is added. A rig that added it in world
    // space would agree with this test at an identity head orientation and
    // disagree the moment the character turned, which is why the second half
    // turns the head.
    vrmRetarget::LookAtRig rig = IdentityBoneRig();
    rig.offsetFromHeadBone = pxr::GfVec3f(0.0f, 0.06f, 0.0f);
    const vrmRetarget::LookAtEvaluator evaluator(rig);

    // Straight ahead of the eyes is a level gaze...
    const vrmRetarget::ResolvedLookAt level =
        evaluator.Evaluate(GazeAt(pxr::GfVec3f(0.0f, 0.06f, 1.0f)));
    assert(level.hasGaze);
    assert(NearlyEqual(level.yawDegrees, 0.0f));
    assert(NearlyEqual(level.pitchDegrees, 0.0f));

    // ...and straight ahead of the *joint* is 6 cm below them.
    const vrmRetarget::ResolvedLookAt below =
        evaluator.Evaluate(GazeAt(pxr::GfVec3f(0.0f, 0.0f, 1.0f)));
    assert(NearlyEqual(below.pitchDegrees, -3.43363f));

    // With the head turned a quarter turn to the character's left, the eyes
    // move with it: the target that was level ahead is now off to the right by
    // exactly that quarter turn, and the offset -- which is along the head's
    // own up axis, unchanged by a yaw -- still puts the origin at the eyes.
    vrmRetarget::LookAtInput turned = GazeAt(pxr::GfVec3f(0.0f, 0.06f, 1.0f));
    turned.head.orientation = Rotation(kAxisY, 90.0f);
    const vrmRetarget::ResolvedLookAt aside = evaluator.Evaluate(turned);
    assert(NearlyEqual(aside.yawDegrees, -90.0f));
    assert(NearlyEqual(aside.pitchDegrees, 0.0f));

    // A head that has moved carries its eyes with it too.
    vrmRetarget::LookAtInput walked = GazeAt(pxr::GfVec3f(0.0f, 1.56f, 1.0f));
    walked.head.position = pxr::GfVec3f(0.0f, 1.5f, 0.0f);
    const vrmRetarget::ResolvedLookAt ahead = evaluator.Evaluate(walked);
    assert(NearlyEqual(ahead.pitchDegrees, 0.0f));
}

void
TestInnerAndOuterAreChosenBySide()
{
    // Two eyes converge: the one on the side the gaze goes to turns outward,
    // away from the nose, and the other turns inward. The two maps are given
    // different scales so a resolve that read one map for both eyes -- or read
    // them the other way round -- cannot pass.
    vrmRetarget::LookAtRig rig = IdentityBoneRig();
    rig.horizontalInner.outputScale = 5.0f;
    rig.horizontalOuter.outputScale = 10.0f;
    rig.verticalUp.outputScale = 0.0f;
    rig.verticalDown.outputScale = 0.0f;
    const vrmRetarget::LookAtEvaluator evaluator(rig);

    const vrmRetarget::ResolvedLookAt left =
        evaluator.Evaluate(GazeAt(pxr::GfVec3f(1.0f, 0.0f, 0.0f)));
    assert(NearlyEqual(left.yawDegrees, 90.0f));
    assert(left.eyeRotations[0].joint == rig.leftEyeJoint);
    assert(SameOrientation(left.eyeRotations[0].rotation,
                           Rotation(kAxisY, 10.0f)));
    assert(SameOrientation(left.eyeRotations[1].rotation,
                           Rotation(kAxisY, 5.0f)));

    const vrmRetarget::ResolvedLookAt right =
        evaluator.Evaluate(GazeAt(pxr::GfVec3f(-1.0f, 0.0f, 0.0f)));
    assert(NearlyEqual(right.yawDegrees, -90.0f));
    assert(SameOrientation(right.eyeRotations[0].rotation,
                           Rotation(kAxisY, -5.0f)));
    assert(SameOrientation(right.eyeRotations[1].rotation,
                           Rotation(kAxisY, -10.0f)));

    // Past the map's input range the eye stops rather than extrapolating: a
    // target behind the character's shoulder is 135 degrees of aim and still
    // ten degrees of eye.
    const vrmRetarget::ResolvedLookAt behind =
        evaluator.Evaluate(GazeAt(pxr::GfVec3f(1.0f, 0.0f, -1.0f)));
    assert(NearlyEqual(behind.yawDegrees, 135.0f));
    assert(SameOrientation(behind.eyeRotations[0].rotation,
                           Rotation(kAxisY, 10.0f)));
}

void
TestVerticalIsSharedAndSignedByDirection()
{
    // Up and down are two maps because a face is not symmetric about the
    // horizon -- an eye rolls further up than down -- but both eyes share them,
    // so this is the one place the two rotations agree.
    vrmRetarget::LookAtRig rig = IdentityBoneRig();
    rig.horizontalInner.outputScale = 0.0f;
    rig.horizontalOuter.outputScale = 0.0f;
    rig.verticalUp.outputScale = 12.0f;
    rig.verticalDown.outputScale = 6.0f;
    const vrmRetarget::LookAtEvaluator evaluator(rig);

    const vrmRetarget::ResolvedLookAt up =
        evaluator.Evaluate(GazeAt(pxr::GfVec3f(0.0f, 1.0f, 0.0f)));
    assert(NearlyEqual(up.pitchDegrees, 90.0f));
    assert(SameOrientation(up.eyeRotations[0].rotation,
                           Rotation(kAxisX, -12.0f)));
    assert(SameOrientation(up.eyeRotations[1].rotation,
                           Rotation(kAxisX, -12.0f)));

    const vrmRetarget::ResolvedLookAt down =
        evaluator.Evaluate(GazeAt(pxr::GfVec3f(0.0f, -1.0f, 0.0f)));
    assert(NearlyEqual(down.pitchDegrees, -90.0f));
    assert(SameOrientation(down.eyeRotations[0].rotation,
                           Rotation(kAxisX, 6.0f)));
}

void
TestAGazeNobodyNamedIsNotAGazeForward()
{
    // The rule an unreported expression name is under, one field over: a clip
    // that said nothing about where the character is looking did not say the
    // character is looking straight ahead. Nothing resolves, and it is not a
    // warning -- a clip with no look-at track is an ordinary clip.
    const vrmRetarget::LookAtEvaluator evaluator(IdentityBoneRig());
    vrmRetarget::LookAtDiagnostics diagnostics;
    const vrmRetarget::ResolvedLookAt resolved =
        evaluator.Evaluate(vrmRetarget::LookAtInput(), &diagnostics);

    assert(!resolved.hasGaze);
    assert(resolved.eyeRotations.empty());
    assert(resolved.expressions.IsEmpty());
    assert(NearlyEqual(resolved.yawDegrees, 0.0f));
    assert(diagnostics.samplesEvaluated == 1);
    assert(diagnostics.samplesWithoutTarget == 1);
    assert(diagnostics.IsClean());

    float yaw = 7.0f;
    float pitch = 7.0f;
    assert(!evaluator.Aim(vrmRetarget::LookAtInput(), &yaw, &pitch));
    // Untouched, so a caller cannot mistake a refusal for a level gaze.
    assert(NearlyEqual(yaw, 7.0f) && NearlyEqual(pitch, 7.0f));
}

void
TestATargetOnTheEyesNamesNoDirection()
{
    // A target at the eye origin has no direction to normalize, and one a
    // micrometre away has one that is numerically meaningless. Both are the
    // same defect and both answer "no gaze" rather than inventing forward --
    // but unlike an absent target, this one is a rig or a clip going wrong, so
    // it is reported.
    vrmRetarget::LookAtRig rig = IdentityBoneRig();
    rig.offsetFromHeadBone = pxr::GfVec3f(0.0f, 0.06f, 0.0f);
    const vrmRetarget::LookAtEvaluator evaluator(rig);
    vrmRetarget::LookAtDiagnostics diagnostics;

    const vrmRetarget::ResolvedLookAt resolved =
        evaluator.Evaluate(GazeAt(pxr::GfVec3f(0.0f, 0.06f, 0.0f)),
                           &diagnostics);
    assert(!resolved.hasGaze);
    assert(diagnostics.samplesWithoutTarget == 1);
    assert(diagnostics.warnings.size() == 1);

    // A second sample with the same defect is the same fact about the clip, so
    // the report does not grow.
    evaluator.Evaluate(GazeAt(pxr::GfVec3f(0.0f, 0.06f, 0.0f)), &diagnostics);
    assert(diagnostics.warnings.size() == 1);
    assert(diagnostics.samplesEvaluated == 2);
}

void
TestTheClipsOffsetIsTheFallbackAndIsReported()
{
    // A VRM 0.x rig states no offsetFromHeadBone at all. The clip's is the only
    // measurement left, and using it assumes the two rigs' eyes sit at the same
    // height -- an assumption an operator should see, so it is a warning rather
    // than a default.
    vrmRetarget::LookAtRig unmeasuredRig = IdentityBoneRig();
    unmeasuredRig.offsetFromHeadBone.reset();

    vrmRetarget::LookAtEvaluateOptions options;
    options.clipOffsetFromHeadBone = pxr::GfVec3f(0.0f, 0.06f, 0.0f);
    const vrmRetarget::LookAtEvaluator borrowed(unmeasuredRig, options);
    vrmRetarget::LookAtDiagnostics diagnostics;
    const vrmRetarget::ResolvedLookAt resolved =
        borrowed.Evaluate(GazeAt(pxr::GfVec3f(0.0f, 0.0f, 1.0f)), &diagnostics);
    assert(NearlyEqual(resolved.pitchDegrees, -3.43363f));
    assert(diagnostics.warnings.size() == 1);

    // The avatar's own offset wins when it has one, and then there is nothing
    // to report.
    vrmRetarget::LookAtRig own = IdentityBoneRig();
    own.offsetFromHeadBone = pxr::GfVec3f(0.0f, 0.12f, 0.0f);
    const vrmRetarget::LookAtEvaluator preferred(own, options);
    vrmRetarget::LookAtDiagnostics clean;
    const vrmRetarget::ResolvedLookAt higher =
        preferred.Evaluate(GazeAt(pxr::GfVec3f(0.0f, 0.0f, 1.0f)), &clean);
    assert(NearlyEqual(higher.pitchDegrees, -6.84277f));
    assert(clean.IsClean());

    // Neither side stating one is a third case, and it is not silent either:
    // the gaze then starts at the head joint, which is inside the skull.
    const vrmRetarget::LookAtEvaluator bare(unmeasuredRig);
    vrmRetarget::LookAtDiagnostics unmeasured;
    bare.Evaluate(GazeAt(pxr::GfVec3f(0.0f, 0.0f, 1.0f)), &unmeasured);
    assert(unmeasured.warnings.size() == 1);
}

void
TestAnExpressionRigReportsAllFourNames()
{
    // One weight drives both eyes, so an expression rig has no inner eye and
    // the horizontal curve is the outer one. What matters more is that all four
    // names are reported every sample: a gaze that swings left after a sample
    // that looked right has to say `lookRight` is now 0, or the earlier weight
    // stands on the rig -- the same rule ExpressionResolver states for a
    // reported zero.
    const vrmRetarget::LookAtEvaluator evaluator(IdentityExpressionRig());

    vrmRetarget::LookAtDiagnostics diagnostics;
    const vrmRetarget::ResolvedLookAt left =
        evaluator.Evaluate(GazeAt(pxr::GfVec3f(1.0f, 0.0f, 0.0f)),
                           &diagnostics);
    assert(left.hasGaze);
    // No eye is rotated: an expression rig drives its gaze through the face.
    assert(left.eyeRotations.empty());
    assert(left.expressions.entries.size() == 4);
    assert(NearlyEqual(*left.expressions.Find("lookLeft"), 1.0f));
    assert(NearlyEqual(*left.expressions.Find("lookRight"), 0.0f));
    assert(NearlyEqual(*left.expressions.Find("lookUp"), 0.0f));
    assert(NearlyEqual(*left.expressions.Find("lookDown"), 0.0f));

    const vrmRetarget::ResolvedLookAt down =
        evaluator.Evaluate(GazeAt(pxr::GfVec3f(0.0f, -1.0f, 0.0f)),
                           &diagnostics);
    assert(NearlyEqual(*down.expressions.Find("lookDown"), 1.0f));
    assert(NearlyEqual(*down.expressions.Find("lookUp"), 0.0f));
    // A downward gaze is straight down, so its horizontal weights are both
    // zero -- and both are still stated.
    assert(NearlyEqual(*down.expressions.Find("lookLeft"), 0.0f));
    assert(NearlyEqual(*down.expressions.Find("lookRight"), 0.0f));
    assert(diagnostics.IsClean());

    // The value the expression half hands back is exactly what
    // ExpressionResolver consumes, so a gaze reaches the avatar's binds through
    // the path the face already uses rather than through a second one.
    vrmRetarget::ExpressionRig binds;
    vrmRetarget::ExpressionDefinition lookDown;
    lookDown.name = "lookDown";
    lookDown.morphTargets.push_back({"/Asset/Meshes/Face/EyesDown", 1.0f});
    binds.Add(lookDown);
    const vrmRetarget::ExpressionResolver resolver(binds);
    const vrmRetarget::ResolvedExpressions applied =
        resolver.Resolve(down.expressions);
    assert(applied.morphTargets.size() == 1);
    assert(NearlyEqual(applied.morphTargets[0].weight, 1.0f));
}

void
TestAnExpressionWeightOutsideTheRangeIsClampedAndNamed()
{
    // A VRM 0.x BlendShape rig states its output range in the same key a Bone
    // rig states degrees in, and nothing in the block distinguishes them. So a
    // weight of 10 is clamped rather than being rescaled by a factor guessed
    // from the rig's type, and the operator is told which name it happened to.
    vrmRetarget::LookAtRig rig = IdentityExpressionRig();
    // Both horizontal maps, so the only thing this sample can be told about is
    // the weight -- an expression rig that states a *different* inner map is a
    // separate report, and it is the case below.
    rig.horizontalInner.outputScale = 10.0f;
    rig.horizontalOuter.outputScale = 10.0f;
    const vrmRetarget::LookAtEvaluator evaluator(rig);

    vrmRetarget::LookAtDiagnostics diagnostics;
    const vrmRetarget::ResolvedLookAt clamped =
        evaluator.Evaluate(GazeAt(pxr::GfVec3f(1.0f, 0.0f, 0.0f)),
                           &diagnostics);
    assert(NearlyEqual(*clamped.expressions.Find("lookLeft"), 1.0f));
    assert(diagnostics.warnings.size() == 1);

    vrmRetarget::LookAtEvaluateOptions verbatim;
    verbatim.clampExpressionWeights = false;
    const vrmRetarget::LookAtEvaluator unclamped(rig, verbatim);
    vrmRetarget::LookAtDiagnostics carried;
    const vrmRetarget::ResolvedLookAt raw =
        unclamped.Evaluate(GazeAt(pxr::GfVec3f(1.0f, 0.0f, 0.0f)), &carried);
    assert(NearlyEqual(*raw.expressions.Find("lookLeft"), 10.0f));
    assert(carried.warnings.size() == 1);
    assert(carried.warnings[0] != diagnostics.warnings[0]);
}

void
TestABoneRigWithHalfItsEyesDrivesTheOneItNamed()
{
    vrmRetarget::LookAtRig half = IdentityBoneRig();
    half.rightEyeJoint.clear();
    const vrmRetarget::LookAtEvaluator one(half);
    vrmRetarget::LookAtDiagnostics diagnostics;
    const vrmRetarget::ResolvedLookAt resolved =
        one.Evaluate(GazeAt(pxr::GfVec3f(1.0f, 0.0f, 1.0f)), &diagnostics);
    assert(resolved.hasGaze);
    assert(resolved.eyeRotations.size() == 1);
    assert(resolved.eyeRotations[0].joint == half.leftEyeJoint);
    assert(diagnostics.warnings.size() == 1);

    // A bone rig naming neither eye resolves to nothing at all, and the aim is
    // still measured -- which is what lets a caller report the gaze it could
    // not apply.
    vrmRetarget::LookAtRig blind = IdentityBoneRig();
    blind.leftEyeJoint.clear();
    blind.rightEyeJoint.clear();
    const vrmRetarget::LookAtEvaluator none(blind);
    vrmRetarget::LookAtDiagnostics blindReport;
    const vrmRetarget::ResolvedLookAt aimed =
        none.Evaluate(GazeAt(pxr::GfVec3f(1.0f, 0.0f, 1.0f)), &blindReport);
    assert(aimed.hasGaze);
    assert(NearlyEqual(aimed.yawDegrees, 45.0f));
    assert(aimed.eyeRotations.empty());
    assert(blindReport.warnings.size() == 1);
}

void
TestThePoseOverloadCarriesTheSampleThrough()
{
    const vrmRetarget::LookAtEvaluator evaluator(IdentityBoneRig());
    motion::HumanoidPose pose;
    pose.timestamp = 1.25;
    assert(!pose.lookAtTarget);

    vrmRetarget::LookAtHead head;
    head.position = pxr::GfVec3f(0.0f, 1.5f, 0.0f);
    const vrmRetarget::ResolvedLookAt silent =
        evaluator.Evaluate(pose, head);
    assert(!silent.hasGaze);
    assert(silent.timestamp == 1.25);

    pose.lookAtTarget = pxr::GfVec3f(0.0f, 2.5f, 1.0f);
    const vrmRetarget::ResolvedLookAt gazing = evaluator.Evaluate(pose, head);
    assert(gazing.hasGaze);
    assert(gazing.timestamp == 1.25);
    // One metre up and one metre ahead of a head that is itself 1.5 m up.
    assert(NearlyEqual(gazing.pitchDegrees, 45.0f));
}

void
TestBothVrmSpellingsParseToOneValue()
{
    // The 1.0 range map and the 0.x curve say the same thing in two shapes, and
    // the reader's job is that a consumer never learns which shape a rig came
    // from. The linear default is where the two have to agree exactly rather
    // than within a tolerance: 0.x's `[0,0,0,1, 1,1,1,0]` is the Hermite basis
    // over one unit segment with both tangents 1, which reduces to `t`.
    vrmRetarget::LookAtRig one;
    std::vector<std::string> warnings;
    assert(vrmRetarget::ParseLookAtRangeMaps(
        R"({"type":"bone","offsetFromHeadBone":[0.0,0.06,0.0],)"
        R"("rangeMapHorizontalInner":{"inputMaxValue":90,"outputScale":5.0},)"
        R"("rangeMapHorizontalOuter":{"inputMaxValue":90,"outputScale":10.0}})",
        &one, &warnings));
    assert(warnings.empty());
    assert(one.type == vrmRetarget::LookAtType::Bone);
    assert(one.offsetFromHeadBone
           && NearlyEqual(*one.offsetFromHeadBone,
                          pxr::GfVec3f(0.0f, 0.06f, 0.0f)));
    assert(NearlyEqual(one.horizontalInner.outputScale, 5.0f));
    assert(NearlyEqual(one.horizontalOuter.Map(45.0f), 5.0f));
    assert(one.horizontalOuter.curve.empty());
    // A map the block never mentioned keeps its default rather than collapsing
    // to zero: an incomplete block is not four broken curves.
    assert(NearlyEqual(one.verticalUp.inputMaxValue, 90.0f));

    vrmRetarget::LookAtRig zero;
    assert(vrmRetarget::ParseLookAtRangeMaps(
        R"({"lookAtTypeName":"BlendShape",)"
        R"("lookAtHorizontalOuter":{"curve":[0,0,0,1,1,1,1,0],)"
        R"("xRange":90,"yRange":1.0}})",
        &zero, &warnings));
    assert(warnings.empty());
    // "BlendShape" is the name 0.x gives the rig 1.0 calls `expression`, and
    // the rest of this library speaks the newer one.
    assert(zero.type == vrmRetarget::LookAtType::Expression);
    assert(zero.horizontalOuter.curve.size() == 2);
    assert(NearlyEqual(zero.horizontalOuter.Map(45.0f), 0.5f));

    vrmRetarget::LookAtRangeMap implicitlyLinear = zero.horizontalOuter;
    implicitlyLinear.curve.clear();
    for (const float degrees : {0.0f, 12.5f, 45.0f, 71.25f, 90.0f, 180.0f}) {
        assert(NearlyEqual(zero.horizontalOuter.Map(degrees),
                           implicitlyLinear.Map(degrees)));
    }

    // A curve that is not the linear default is read as the curve it is.
    vrmRetarget::LookAtRig curved;
    assert(vrmRetarget::ParseLookAtRangeMaps(
        R"({"lookAtVerticalUp":{"curve":[0,0,0,0,1,1,0,0],)"
        R"("xRange":90,"yRange":10}})",
        &curved, &warnings));
    // Flat tangents at both ends: the smoothstep, which is 0.5 at the middle
    // and visibly not `t` on either side of it.
    assert(NearlyEqual(curved.verticalUp.Map(45.0f), 5.0f));
    assert(NearlyEqual(curved.verticalUp.Map(22.5f), 1.5625f));
}

void
TestAnUnreadableLookAtBlockLeavesTheDefaultsStanding()
{
    vrmRetarget::LookAtRig rig;
    std::vector<std::string> warnings;
    // The empty string is what a rig with no preserved curves carries, and it
    // is not a defect -- there is nothing to warn about in a file that said
    // nothing.
    assert(!vrmRetarget::ParseLookAtRangeMaps("", &rig, &warnings));
    assert(warnings.empty());

    // A block that is not an object is a defect, and the defaults stand.
    assert(!vrmRetarget::ParseLookAtRangeMaps("[90, 10]", &rig, &warnings));
    assert(warnings.size() == 1);
    assert(NearlyEqual(rig.horizontalOuter.inputMaxValue, 90.0f));

    // An input range of zero would be a division by it. It maps everything to
    // nothing instead, and says so.
    warnings.clear();
    assert(vrmRetarget::ParseLookAtRangeMaps(
        R"({"rangeMapVerticalUp":{"inputMaxValue":0,"outputScale":10}})", &rig,
        &warnings));
    assert(warnings.size() == 1);
    assert(rig.verticalUp.Map(45.0f) == 0.0f);

    // A curve that is not a whole number of four-float keys is not a curve.
    // Reading the keys it does hold would silently rescale the rest of it, so
    // it falls back to linear and is named.
    warnings.clear();
    vrmRetarget::LookAtRig ragged;
    assert(vrmRetarget::ParseLookAtRangeMaps(
        R"({"lookAtVerticalDown":{"curve":[0,0,0,1,1],"xRange":90,)"
        R"("yRange":10}})",
        &ragged, &warnings));
    assert(warnings.size() == 1);
    assert(ragged.verticalDown.curve.empty());
    assert(NearlyEqual(ragged.verticalDown.Map(45.0f), 5.0f));
}

} // namespace

int
main()
{
    TestSkeletonParentsComeFromJointPaths();
    TestHumanoidMapReportsGapsAndCollisions();
    TestIdentityRestPosesPassRotationsThrough();
    TestRestPoseCorrectionPreservesTheWorldDelta();
    TestRestPoseCorrectionAccountsForTheWholeAncestorChain();
    TestRootMotionModes();
    TestDesignTripletHandOff();
    TestUnmappedJointsStayAtRestAndAreReported();
    TestResampleOptionDrivesSampleCount();
    TestRootJointModeMovesTheReceiver();
    TestExpressionRigDeclaresANameOnce();
    TestOneWeightExpandsOntoEveryBind();
    TestReportedZeroIsAuthoredAndUnreportedIsAbsent();
    TestBinaryRoundsAndOutOfRangeIsClamped();
    TestExpressionsAccumulateOnOneTarget();
    TestAnUnresolvedNameIsNamedOnceForAWholeClip();
    TestANonNumberIsNotAWeight();
    TestABindWithNoIdentifierIsSkippedAndNamed();
    TestATargetDrivenBelowZeroIsReportedToo();
    TestAJointsWorldTransformComposesItsWholeChain();
    TestAnIdentityRangeMapReproducesTheAim();
    TestTheOffsetPlacesTheGazeOrigin();
    TestInnerAndOuterAreChosenBySide();
    TestVerticalIsSharedAndSignedByDirection();
    TestAGazeNobodyNamedIsNotAGazeForward();
    TestATargetOnTheEyesNamesNoDirection();
    TestTheClipsOffsetIsTheFallbackAndIsReported();
    TestAnExpressionRigReportsAllFourNames();
    TestAnExpressionWeightOutsideTheRangeIsClampedAndNamed();
    TestABoneRigWithHalfItsEyesDrivesTheOneItNamed();
    TestThePoseOverloadCarriesTheSampleThrough();
    TestBothVrmSpellingsParseToOneValue();
    TestAnUnreadableLookAtBlockLeavesTheDefaultsStanding();
    std::puts("vrmRetarget unit tests passed");
    return 0;
}
