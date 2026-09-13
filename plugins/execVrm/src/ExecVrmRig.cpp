// SPDX-License-Identifier: Apache-2.0
#include "ExecVrmRig.h"

#include "pxr/base/gf/quatd.h"
#include "pxr/base/gf/quatf.h"
#include "pxr/base/gf/vec3d.h"
#include "pxr/base/gf/vec3f.h"

#include <algorithm>
#include <array>
#include <optional>
#include <set>
#include <string_view>

namespace execvrm {

namespace {

pxr::GfQuatf ToQuatf(const pxr::GfQuatd& q)
{
    return pxr::GfQuatf(static_cast<float>(q.GetReal()),
                        pxr::GfVec3f(static_cast<float>(q.GetImaginary()[0]),
                                     static_cast<float>(q.GetImaginary()[1]),
                                     static_cast<float>(q.GetImaginary()[2])));
}

pxr::GfVec3f ToVec3f(const pxr::GfVec3d& v)
{
    return pxr::GfVec3f(static_cast<float>(v[0]), static_cast<float>(v[1]),
                        static_cast<float>(v[2]));
}

// The bone a semantic joint path names: its leaf, looked up in the vocabulary.
// tools/motionRetarget's `FindHumanBone(LeafToken(path))`, and execMotion's
// `BoneForJointPath` -- a path with no separator is already a leaf.
std::optional<motion::HumanBone> BoneForLeaf(const std::string& jointPath)
{
    const std::size_t separator = jointPath.rfind('/');
    const std::string_view leaf =
        separator == std::string::npos
            ? std::string_view(jointPath)
            : std::string_view(jointPath).substr(separator + 1);
    return motion::FindHumanBone(leaf);
}

} // namespace

const std::array<pxr::TfToken, motion::HumanBoneCount>& HumanBoneAttributeNames()
{
    // Immutable once built, so it is not the mutable global state a callback
    // may not read: the same list at every evaluation, derived from nothing a
    // stage can change.
    static const std::array<pxr::TfToken, motion::HumanBoneCount> names = [] {
        std::array<pxr::TfToken, motion::HumanBoneCount> result;
        for (std::size_t slot = 0; slot < motion::HumanBoneCount; ++slot) {
            result[slot] = pxr::TfToken(
                "vrm:humanBones:"
                + std::string(motion::HumanBoneName(
                    static_cast<motion::HumanBone>(slot))));
        }
        return result;
    }();
    return names;
}

SkeletonOutcome TargetSkeletonFromRest(const SkeletonRest& rest)
{
    SkeletonOutcome outcome;
    for (const std::string& joint : rest.joints) {
        if (joint.empty()) {
            outcome.refusal = SkeletonRefusal::EmptyJointToken;
            return outcome;
        }
    }
    // No joints is the empty skeleton, whatever `restTransforms` says: there is
    // no joint for a rest transform to belong to, so none can become a number.
    // It is also the one reading under which `joints = []` beside an unauthored
    // `restTransforms` -- which arrives as one fallback matrix -- is answered
    // for what the stage states rather than refused over a count it did not.
    if (rest.joints.empty()) {
        outcome.skeleton = vrmRetarget::TargetSkeleton();
        return outcome;
    }
    if (rest.restTransforms.size() != rest.joints.size()) {
        outcome.refusal = SkeletonRefusal::RestTransformCount;
        return outcome;
    }

    std::vector<vrmRetarget::TargetJoint> joints;
    joints.reserve(rest.joints.size());
    for (std::size_t i = 0; i < rest.joints.size(); ++i) {
        vrmRetarget::TargetJoint joint;
        joint.token = rest.joints[i];
        // tools/motionRetarget's DecomposeRest, line for line: translation
        // straight off the matrix, rotation from what is left once scale and
        // shear are removed. Kept identical on purpose -- two decompositions
        // that differ in a normalization step are a parity difference P0-6
        // would have to explain rather than measure.
        const pxr::GfMatrix4d& matrix = rest.restTransforms[i];
        joint.restTranslation = ToVec3f(matrix.ExtractTranslation());
        joint.restRotation =
            ToQuatf(matrix.RemoveScaleShear().ExtractRotationQuat())
                .GetNormalized();
        joints.push_back(std::move(joint));
    }

    vrmRetarget::TargetSkeleton skeleton(std::move(joints));
    skeleton.ResolveParentsFromTokens();
    outcome.skeleton = std::move(skeleton);
    return outcome;
}

MapOutcome HumanoidMapFor(const HumanoidInputs& inputs)
{
    MapOutcome outcome;
    if (inputs.skeletonTargetCount == 0) {
        outcome.refusal = MapRefusal::NoSkeleton;
        return outcome;
    }
    if (inputs.skeletonTargetCount > 1) {
        outcome.refusal = MapRefusal::SeveralSkeletons;
        return outcome;
    }
    if (inputs.skeletons.size() != 1) {
        outcome.refusal = MapRefusal::SkeletonUnanswered;
        return outcome;
    }
    const vrmRetarget::TargetSkeleton& skeleton = inputs.skeletons.front();

    vrmRetarget::HumanoidMap map;
    for (const auto& [bone, token] : inputs.bindings) {
        if (token.empty()) {
            continue;
        }
        if (!map.SetJointToken(bone, token, skeleton)) {
            outcome.offending.emplace_back(bone, token);
        }
    }
    if (!outcome.offending.empty()) {
        outcome.refusal = MapRefusal::UnknownJoint;
        return outcome;
    }

    const std::vector<int> duplicates = map.FindDuplicateJointIndices();
    if (!duplicates.empty()) {
        // Name every bone on a shared joint, not only the second: the library
        // cannot say which binding the humanoid meant, and neither can this.
        const std::set<int> shared(duplicates.begin(), duplicates.end());
        for (const auto& [bone, token] : inputs.bindings) {
            if (map.IsMapped(bone) && shared.count(map.GetJointIndex(bone))) {
                outcome.offending.emplace_back(bone, token);
            }
        }
        outcome.refusal = MapRefusal::DuplicateJoint;
        return outcome;
    }

    outcome.map = std::move(map);
    return outcome;
}

SourceRestOutcome SourceRestFromSkeleton(
    const vrmRetarget::TargetSkeleton& skeleton)
{
    SourceRestOutcome outcome;
    vrmRetarget::SourceRestPose rest;

    // Which joint first named each bone, so a second naming can report both.
    std::array<const std::string*, motion::HumanBoneCount> namedBy{};
    std::size_t recognized = 0;

    for (const vrmRetarget::TargetJoint& joint : skeleton.GetJoints()) {
        const std::optional<motion::HumanBone> bone = BoneForLeaf(joint.token);
        if (!bone) {
            continue;
        }
        const auto slot = static_cast<std::size_t>(*bone);
        if (namedBy[slot]) {
            // Report the first naming once, then every later one.
            const bool firstReported = std::any_of(
                outcome.offending.begin(), outcome.offending.end(),
                [&](const auto& named) { return named.first == *bone; });
            if (!firstReported) {
                outcome.offending.emplace_back(*bone, *namedBy[slot]);
            }
            outcome.offending.emplace_back(*bone, joint.token);
            continue;
        }
        namedBy[slot] = &joint.token;
        ++recognized;

        // tools/motionRetarget's ReadClip, line for line: the joint's own
        // decomposed rest fills its bone's slot, and the semantic parent is the
        // bone its parent PATH's leaf names -- the path, whether or not a joint
        // of this skeleton resolves it.
        rest.localRotations[slot] = joint.restRotation;
        rest.localTranslations[slot] = joint.restTranslation;
        const std::size_t separator = joint.token.rfind('/');
        if (separator == std::string::npos) {
            continue;
        }
        if (const std::optional<motion::HumanBone> parent =
                BoneForLeaf(joint.token.substr(0, separator))) {
            rest.SetParent(*bone, *parent);
        }
    }

    if (!outcome.offending.empty()) {
        outcome.refusal = SourceRestRefusal::DuplicateBone;
        return outcome;
    }
    if (recognized == 0) {
        outcome.refusal = SourceRestRefusal::NoHumanBone;
        return outcome;
    }
    outcome.rest = std::move(rest);
    return outcome;
}

CorrectionOutcome RestPoseCorrectionFor(const CorrectionInputs& inputs)
{
    CorrectionOutcome outcome;
    if (!inputs.map || inputs.targets.size() != 1) {
        outcome.refusal = CorrectionRefusal::RigUnanswered;
        return outcome;
    }
    if (inputs.sourceTargetCount == 0) {
        outcome.refusal = CorrectionRefusal::NoSource;
        return outcome;
    }
    if (inputs.sourceTargetCount > 1) {
        outcome.refusal = CorrectionRefusal::SeveralSources;
        return outcome;
    }
    if (inputs.sources.size() != 1) {
        outcome.refusal = CorrectionRefusal::SourceUnanswered;
        return outcome;
    }

    SourceRestOutcome source = SourceRestFromSkeleton(inputs.sources.front());
    if (!source.rest) {
        outcome.refusal = CorrectionRefusal::SourceRest;
        outcome.sourceRefusal = source.refusal;
        outcome.offending = std::move(source.offending);
        return outcome;
    }

    // The whole node, and it is a wrapper.
    outcome.correction = vrmRetarget::ComputeRestPoseCorrection(
        *source.rest, inputs.targets.front(), *inputs.map);
    return outcome;
}

} // namespace execvrm
