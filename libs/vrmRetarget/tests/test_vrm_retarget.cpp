// SPDX-License-Identifier: Apache-2.0
#include "vrmRetarget/ExpressionResolver.h"
#include "vrmRetarget/HumanoidMap.h"
#include "vrmRetarget/PoseRetargeter.h"
#include "vrmRetarget/RestPose.h"
#include "vrmRetarget/RootMotionPolicy.h"
#include "vrmRetarget/TargetSkeleton.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <utility>

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
    // "Does not resolve" is distinguishable from "resolves to zero".
    assert(!resolver.ResolveWeight("relaxed", 1.0f, &weight));
    assert(NearlyEqual(weight, 1.0f));

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
    std::puts("vrmRetarget unit tests passed");
    return 0;
}
