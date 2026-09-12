// SPDX-License-Identifier: Apache-2.0
#include "ExecVrmRig.h"

#include "pxr/base/gf/quatd.h"
#include "pxr/base/gf/quatf.h"
#include "pxr/base/gf/vec3d.h"
#include "pxr/base/gf/vec3f.h"

#include <set>

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

} // namespace execvrm
