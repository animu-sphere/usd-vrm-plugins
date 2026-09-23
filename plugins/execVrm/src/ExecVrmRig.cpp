// SPDX-License-Identifier: Apache-2.0
#include "ExecVrmRig.h"

#include <vrmRig/RequiredBones.h>

#include "pxr/base/gf/quatd.h"
#include "pxr/base/gf/quatf.h"
#include "pxr/base/gf/vec3d.h"
#include "pxr/base/gf/vec3f.h"
#include "pxr/base/gf/vec3h.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <set>
#include <string_view>
#include <utility>

namespace execvrm
{

const std::array<pxr::TfToken, openstrata::motion::HumanJointCount>&
HumanBoneAttributeNames()
{
    // Immutable once built, so it is not the mutable global state a callback
    // may not read: the same list at every evaluation, derived from nothing a
    // stage can change.
    static const std::array<pxr::TfToken, openstrata::motion::HumanJointCount> names = []
    {
        std::array<pxr::TfToken, openstrata::motion::HumanJointCount> result;
        for (std::size_t slot = 0; slot < openstrata::motion::HumanJointCount; ++slot)
        {
            result[slot] = pxr::TfToken(
                "vrm:humanBones:" +
                std::string(openstrata::motion::HumanJointName(static_cast<openstrata::motion::HumanJoint>(slot))));
        }
        return result;
    }();
    return names;
}

SkeletonOutcome
TargetSkeletonFromRest(const SkeletonRest& rest)
{
    // The library's builder, which `motion_retarget` calls too: two readings
    // that differ in a normalization step are a parity difference P0-6 would
    // have to explain rather than measure. What is left here is naming its
    // answer in this bundle's refusal vocabulary.
    openstrata::motion::SkeletonDescriptorResult built =
        openstrata::motion::BuildSkeletonDescriptor(rest.joints, rest.restTransforms);
    SkeletonOutcome outcome;
    switch (built.error)
    {
    case openstrata::motion::SkeletonDescriptorError::None:
        outcome.skeleton = std::move(built.skeleton);
        break;
    case openstrata::motion::SkeletonDescriptorError::RestTransformCount:
        outcome.refusal = SkeletonRefusal::RestTransformCount;
        break;
    case openstrata::motion::SkeletonDescriptorError::EmptyJointToken:
        outcome.refusal = SkeletonRefusal::EmptyJointToken;
        break;
    }
    return outcome;
}

MapOutcome
HumanoidMapFor(const HumanoidInputs& inputs)
{
    MapOutcome outcome;
    if (inputs.skeletonTargetCount == 0)
    {
        outcome.refusal = MapRefusal::NoSkeleton;
        return outcome;
    }
    if (inputs.skeletonTargetCount > 1)
    {
        outcome.refusal = MapRefusal::SeveralSkeletons;
        return outcome;
    }
    if (inputs.skeletons.size() != 1)
    {
        outcome.refusal = MapRefusal::SkeletonUnanswered;
        return outcome;
    }
    const openstrata::motion::SkeletonDescriptor& skeleton = inputs.skeletons.front();

    openstrata::motion::RetargetMap map;
    for (const auto& [bone, token] : inputs.bindings)
    {
        if (token.empty())
        {
            continue;
        }
        if (!map.SetJointToken(bone, token, skeleton))
        {
            outcome.offending.emplace_back(bone, token);
        }
    }
    if (!outcome.offending.empty())
    {
        outcome.refusal = MapRefusal::UnknownJoint;
        return outcome;
    }

    const std::vector<int> duplicates = map.FindDuplicateJointIndices();
    if (!duplicates.empty())
    {
        // Name every bone on a shared joint, not only the second: the library
        // cannot say which binding the humanoid meant, and neither can this.
        const std::set<int> shared(duplicates.begin(), duplicates.end());
        for (const auto& [bone, token] : inputs.bindings)
        {
            if (map.IsMapped(bone) && shared.count(map.GetJointIndex(bone)))
            {
                outcome.offending.emplace_back(bone, token);
            }
        }
        outcome.refusal = MapRefusal::DuplicateJoint;
        return outcome;
    }

    outcome.map = std::move(map);
    return outcome;
}

SourceRestOutcome
SourceRestFromSkeleton(const openstrata::motion::SkeletonDescriptor& skeleton)
{
    // The library's builder, which `motion_retarget` calls too, and the one
    // leaf rule for a semantic skeleton (RETARGETING_POLICY.md §10 there).
    openstrata::motion::SourceRestPoseResult built =
        openstrata::motion::BuildSourceRestPose(skeleton);
    SourceRestOutcome outcome;
    switch (built.error)
    {
    case openstrata::motion::SourceRestPoseError::None:
        outcome.rest = std::move(built.rest);
        break;
    case openstrata::motion::SourceRestPoseError::NoHumanBone:
        outcome.refusal = SourceRestRefusal::NoHumanBone;
        break;
    case openstrata::motion::SourceRestPoseError::DuplicateBone:
        outcome.refusal = SourceRestRefusal::DuplicateBone;
        outcome.offending = std::move(built.offending);
        break;
    }
    return outcome;
}

namespace
{

// What `vrm:retarget:sourceSkeleton` reached, judged once for both nodes that
// read it -- the correction and the retarget refuse it identically, because
// they read it identically.
enum class SourceCheck
{
    Answered,
    NoSource,
    SeveralSources,
    SourceUnanswered,
    SourceRest,
};

struct SourceOutcome
{
    SourceCheck check = SourceCheck::NoSource;
    SourceRestOutcome rest;
};

SourceOutcome
SourceFor(std::size_t count, const std::vector<openstrata::motion::SkeletonDescriptor>& sources)
{
    SourceOutcome outcome;
    if (count == 0)
    {
        outcome.check = SourceCheck::NoSource;
        return outcome;
    }
    if (count > 1)
    {
        outcome.check = SourceCheck::SeveralSources;
        return outcome;
    }
    if (sources.size() != 1)
    {
        outcome.check = SourceCheck::SourceUnanswered;
        return outcome;
    }
    outcome.rest = SourceRestFromSkeleton(sources.front());
    outcome.check = outcome.rest.rest ? SourceCheck::Answered : SourceCheck::SourceRest;
    return outcome;
}

} // namespace

CorrectionOutcome
RestPoseCorrectionFor(const CorrectionInputs& inputs)
{
    CorrectionOutcome outcome;
    if (!inputs.map || inputs.targets.size() != 1)
    {
        outcome.refusal = CorrectionRefusal::RigUnanswered;
        return outcome;
    }

    SourceOutcome source = SourceFor(inputs.sourceTargetCount, inputs.sources);
    switch (source.check)
    {
    case SourceCheck::Answered:
        break;
    case SourceCheck::NoSource:
        outcome.refusal = CorrectionRefusal::NoSource;
        return outcome;
    case SourceCheck::SeveralSources:
        outcome.refusal = CorrectionRefusal::SeveralSources;
        return outcome;
    case SourceCheck::SourceUnanswered:
        outcome.refusal = CorrectionRefusal::SourceUnanswered;
        return outcome;
    case SourceCheck::SourceRest:
        outcome.refusal = CorrectionRefusal::SourceRest;
        outcome.sourceRefusal = source.rest.refusal;
        outcome.offending = std::move(source.rest.offending);
        return outcome;
    }

    // The whole node, and it is a wrapper.
    outcome.correction = openstrata::motion::ComputeRestPoseCorrection(
        *source.rest.rest, inputs.targets.front(), *inputs.map);
    return outcome;
}

BoundPoseOutcome
BoundPoseFor(const BoundPoseInputs& inputs)
{
    BoundPoseOutcome outcome;
    if (inputs.animationTargetCount == 0)
    {
        // Nothing bound here: what an ancestor binds, UsdSkel's inheritance.
        if (inputs.inherited)
        {
            outcome.pose = *inputs.inherited;
            return outcome;
        }
        outcome.refusal = BoundPoseRefusal::NoAnimation;
        return outcome;
    }
    if (inputs.animationTargetCount > 1)
    {
        outcome.refusal = BoundPoseRefusal::SeveralAnimations;
        return outcome;
    }
    if (inputs.poses.size() != 1)
    {
        outcome.refusal = BoundPoseRefusal::AnimationUnanswered;
        return outcome;
    }
    outcome.pose = inputs.poses.front();
    return outcome;
}

RootMotionOutcome
RootMotionOptionsFor(const RootMotionStatements& statements,
                     const openstrata::motion::SkeletonDescriptor& target)
{
    RootMotionOutcome outcome;
    openstrata::motion::RootMotionOptions options;

    // motion_retarget's `--root-motion`, word for word (tools/motionRetarget's
    // Options.cpp).
    if (statements.mode)
    {
        const std::string& mode = *statements.mode;
        if (mode == "hips")
        {
            options.mode = openstrata::motion::RootMotionMode::Hips;
        }
        else if (mode == "root")
        {
            options.mode = openstrata::motion::RootMotionMode::RootJoint;
        }
        else if (mode == "ignore")
        {
            options.mode = openstrata::motion::RootMotionMode::Ignore;
        }
        else
        {
            outcome.refusal = RootMotionRefusal::UnknownMode;
            return outcome;
        }
    }

    // And its `--root-joint`, resolved the way main.cpp resolves it: exactly,
    // on the full joint path, and only under `root`.
    if (options.mode == openstrata::motion::RootMotionMode::RootJoint)
    {
        if (!statements.rootJoint || statements.rootJoint->empty())
        {
            outcome.refusal = RootMotionRefusal::NoRootJoint;
            return outcome;
        }
        options.rootJointIndex = target.FindJoint(*statements.rootJoint);
        if (options.rootJointIndex < 0)
        {
            outcome.refusal = RootMotionRefusal::UnknownRootJoint;
            return outcome;
        }
    }

    if (statements.translationScale)
    {
        if (!std::isfinite(*statements.translationScale))
        {
            outcome.refusal = RootMotionRefusal::TranslationScale;
            return outcome;
        }
        options.translationScale = *statements.translationScale;
    }
    if (statements.preserveTargetHeight)
    {
        options.preserveTargetHeight = *statements.preserveTargetHeight;
    }

    outcome.options = options;
    return outcome;
}

RetargetOutcome
HumanoidRetargetFor(const RetargetInputs& inputs,
                    openstrata::motion::RetargetDiagnostics* diagnostics)
{
    RetargetOutcome outcome;
    if (!inputs.map || inputs.targets.size() != 1)
    {
        outcome.refusal = RetargetRefusal::RigUnanswered;
        return outcome;
    }
    const openstrata::motion::SkeletonDescriptor& target = inputs.targets.front();

    const RootMotionOutcome rootMotion = RootMotionOptionsFor(inputs.rootMotion, target);
    if (!rootMotion.options)
    {
        outcome.refusal = RetargetRefusal::RootMotion;
        outcome.rootMotionRefusal = rootMotion.refusal;
        return outcome;
    }

    SourceOutcome source = SourceFor(inputs.sourceTargetCount, inputs.sources);
    switch (source.check)
    {
    case SourceCheck::Answered:
        break;
    case SourceCheck::NoSource:
        outcome.refusal = RetargetRefusal::NoSource;
        return outcome;
    case SourceCheck::SeveralSources:
        outcome.refusal = RetargetRefusal::SeveralSources;
        return outcome;
    case SourceCheck::SourceUnanswered:
        outcome.refusal = RetargetRefusal::SourceUnanswered;
        return outcome;
    case SourceCheck::SourceRest:
        outcome.refusal = RetargetRefusal::SourceRest;
        outcome.sourceRefusal = source.rest.refusal;
        outcome.offending = std::move(source.rest.offending);
        return outcome;
    }

    // The count above covers this relationship for both reads of it, so a pose
    // missing here is the bound pose refusing, not a second object.
    if (inputs.poses.size() != 1)
    {
        outcome.refusal = RetargetRefusal::PoseUnanswered;
        return outcome;
    }
    if (!inputs.hasInstant)
    {
        outcome.refusal = RetargetRefusal::NoInstant;
        return outcome;
    }

    // The whole node, and it is a wrapper -- motion_retarget's call, with its
    // four arguments. Constructing the retargeter is where the correction is
    // computed, which is the cost this node reports rather than hides.
    openstrata::motion::RetargetOptions options;
    options.rootMotion = *rootMotion.options;
    options.requiredBones = vrmRig::GetRequiredBones();
    const openstrata::motion::PoseRetargeter retargeter(target, *inputs.map, *source.rest.rest,
                                                        options);
    outcome.pose = retargeter.Retarget(inputs.poses.front(), diagnostics);
    return outcome;
}

RigDiagnosticsOutcome
RigDiagnosticsFor(const RigDiagnosticsInputs& inputs)
{
    RigDiagnosticsOutcome outcome;
    if (!inputs.map || inputs.targets.size() != 1)
    {
        outcome.refusal = RetargetRefusal::RigUnanswered;
        return outcome;
    }
    const openstrata::motion::SkeletonDescriptor& target = inputs.targets.front();

    const RootMotionOutcome rootMotion = RootMotionOptionsFor(inputs.rootMotion, target);
    if (!rootMotion.options)
    {
        outcome.refusal = RetargetRefusal::RootMotion;
        outcome.rootMotionRefusal = rootMotion.refusal;
        return outcome;
    }

    // The whole node, and it is a wrapper. The required bones are VRM 1.0's:
    // the retarget holds no set of its own, and this rig is a VRM avatar's.
    openstrata::motion::RetargetOptions options;
    options.rootMotion = *rootMotion.options;
    options.requiredBones = vrmRig::GetRequiredBones();
    outcome.diagnostics = openstrata::motion::DiagnoseRig(target, *inputs.map, options);
    return outcome;
}

RetargetDiagnosticsOutcome
RetargetDiagnosticsFor(const RetargetInputs& inputs,
                       const openstrata::motion::RetargetDiagnostics* rig)
{
    RetargetDiagnosticsOutcome outcome;

    // Seeded with the rig's list, so the pose's report lands behind it and a
    // code the rig already raised -- a missing hips under root-motion mode
    // 'hips', which every sample raises again -- stays where the rig put it:
    // the clip overload's order, exactly.
    openstrata::motion::RetargetDiagnostics diagnostics;
    if (rig)
    {
        diagnostics = *rig;
    }
    outcome.retarget = HumanoidRetargetFor(inputs, &diagnostics);
    if (!outcome.retarget.pose)
    {
        return outcome;
    }
    // Answered: the pose is the retarget node's to answer, not this one's.
    outcome.retarget.pose.reset();
    if (!rig)
    {
        outcome.rigUnanswered = true;
        return outcome;
    }
    outcome.diagnostics = std::move(diagnostics);
    return outcome;
}

JointTransformsOutcome
JointLocalTransformsFor(const JointTransformsInputs& inputs)
{
    JointTransformsOutcome outcome;
    if (!inputs.pose)
    {
        outcome.refusal = JointTransformsRefusal::PoseUnanswered;
        return outcome;
    }
    if (inputs.targets.size() != 1)
    {
        outcome.refusal = JointTransformsRefusal::RigUnanswered;
        return outcome;
    }
    const openstrata::motion::SkeletonDescriptor& target = inputs.targets.front();
    const openstrata::motion::RetargetedPose& pose = *inputs.pose;
    if (pose.rotations.size() != target.GetSize() || pose.translations.size() != target.GetSize())
    {
        outcome.refusal = JointTransformsRefusal::JointCount;
        outcome.joints = target.GetSize();
        outcome.rotations = pose.rotations.size();
        outcome.translations = pose.translations.size();
        return outcome;
    }

    // What `motion_retarget`'s WriteAnimation authors, per sample: the rig's
    // tokens as `joints`, the pose's arrays unchanged, and each joint's rest
    // scale (the scale policy).
    openstrata::motion::JointLocalTransforms sample;
    sample.timestamp = pose.timestamp;
    sample.joints.reserve(target.GetSize());
    for (const openstrata::motion::SkeletonJoint& joint : target.GetJoints())
    {
        sample.joints.push_back(joint.token);
    }
    sample.translations = pose.translations;
    sample.rotations = pose.rotations;
    sample.scales.reserve(target.GetSize());
    for (const openstrata::motion::SkeletonJoint& joint : target.GetJoints())
    {
        sample.scales.emplace_back(joint.restScale);
    }
    outcome.sample = std::move(sample);
    return outcome;
}

} // namespace execvrm
