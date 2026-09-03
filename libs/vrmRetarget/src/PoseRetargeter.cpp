// SPDX-License-Identifier: Apache-2.0
#include "vrmRetarget/PoseRetargeter.h"

#include "motionRuntime/Resample.h"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace vrmRetarget
{
namespace
{

std::string
Describe(motion::HumanBone bone)
{
    return std::string(motion::HumanBoneName(bone));
}

} // namespace

bool
GetJointWorldTransform(const TargetSkeleton& skeleton,
                       const RetargetedPose& pose, int jointIndex,
                       pxr::GfQuatf* orientation, pxr::GfVec3f* position)
{
    const std::vector<TargetJoint>& joints = skeleton.GetJoints();
    const std::size_t count = joints.size();
    if (jointIndex < 0 || static_cast<std::size_t>(jointIndex) >= count
        || pose.rotations.size() != count || pose.translations.size() != count) {
        return false;
    }

    // Root-first, because composing a chain means applying the outermost
    // ancestor's transform last. The bound is the joint count: a chain longer
    // than that has revisited a joint, which is a cycle -- and a rig whose
    // parents cycle is one this refuses rather than walks forever.
    std::vector<int> chain;
    chain.reserve(count);
    for (int walk = jointIndex; walk != TargetSkeleton::kNoParent;
         walk = joints[static_cast<std::size_t>(walk)].parent) {
        if (walk < 0 || static_cast<std::size_t>(walk) >= count
            || chain.size() == count) {
            return false;
        }
        chain.push_back(walk);
    }

    pxr::GfQuatf composedRotation(1.0f);
    pxr::GfVec3f composedPosition(0.0f);
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        const auto slot = static_cast<std::size_t>(*it);
        composedPosition
            += composedRotation.Transform(pose.translations[slot]);
        composedRotation
            = (composedRotation * pose.rotations[slot]).GetNormalized();
    }

    if (orientation) {
        *orientation = composedRotation;
    }
    if (position) {
        *position = composedPosition;
    }
    return true;
}

PoseRetargeter::PoseRetargeter(TargetSkeleton skeleton, HumanoidMap map,
                               SourceRestPose sourceRest,
                               RetargetOptions options)
    : _skeleton(std::move(skeleton))
    , _map(std::move(map))
    , _sourceRest(std::move(sourceRest))
    , _options(std::move(options))
    , _correction(ComputeRestPoseCorrection(_sourceRest, _skeleton, _map))
{
}

RetargetedPose
PoseRetargeter::_RestPose() const
{
    RetargetedPose rest;
    const std::vector<TargetJoint>& joints = _skeleton.GetJoints();
    rest.rotations.reserve(joints.size());
    rest.translations.reserve(joints.size());
    for (const TargetJoint& joint : joints) {
        rest.rotations.push_back(joint.restRotation);
        rest.translations.push_back(joint.restTranslation);
    }
    return rest;
}

RetargetedPose
PoseRetargeter::Retarget(const motion::HumanoidPose& pose,
                         RetargetDiagnostics* diagnostics) const
{
    // Joints the clip does not drive stay at rest rather than collapsing to
    // identity, so a partial clip leaves the rest of the rig alone.
    RetargetedPose result = _RestPose();
    result.timestamp = pose.timestamp;
    const std::size_t jointCount = _skeleton.GetSize();

    for (std::size_t slot = 0; slot < motion::HumanBoneCount; ++slot) {
        if (!pose.validRotations.test(slot)) {
            continue;
        }
        const auto bone = static_cast<motion::HumanBone>(slot);
        const int jointIndex = _map.GetJointIndex(bone);
        if (jointIndex < 0 || static_cast<std::size_t>(jointIndex) >= jointCount) {
            if (diagnostics) {
                diagnostics->unmappedSourceBones.push_back(bone);
            }
            continue;
        }
        result.rotations[static_cast<std::size_t>(jointIndex)] =
            _correction.Apply(bone, pose.localRotations[slot]);
    }

    // Root motion. The clip carries body translation on the hips (motion
    // contract, "Coordinates and time"), so the hips slot is the source
    // regardless of which joint finally receives it.
    const RootMotionOptions& rootOptions = _options.rootMotion;
    if (rootOptions.mode != RootMotionMode::Ignore) {
        const auto hipsSlot =
            static_cast<std::size_t>(motion::HumanBone::Hips);
        const int hipsJoint = _map.GetJointIndex(motion::HumanBone::Hips);

        int receiver = hipsJoint;
        if (rootOptions.mode == RootMotionMode::RootJoint) {
            receiver = rootOptions.rootJointIndex;
            if (receiver < 0 || static_cast<std::size_t>(receiver) >= jointCount) {
                if (diagnostics) {
                    diagnostics->warnings.emplace_back(
                        "root-motion mode 'root' has no valid root joint index; "
                        "no root translation was authored");
                }
                receiver = TargetSkeleton::kNoParent;
            }
        }

        if (receiver >= 0 && static_cast<std::size_t>(receiver) < jointCount) {
            pxr::GfVec3f sourceTranslation =
                _sourceRest.localTranslations[hipsSlot];
            bool haveSource = false;
            if (pose.root.hasPosition) {
                sourceTranslation = pose.root.worldPosition;
                haveSource = true;
            }
            if (haveSource) {
                const std::vector<TargetJoint>& joints = _skeleton.GetJoints();
                result.translations[static_cast<std::size_t>(receiver)] =
                    ResolveRootTranslation(
                        rootOptions, sourceTranslation,
                        _sourceRest.localTranslations[hipsSlot],
                        joints[static_cast<std::size_t>(receiver)]
                            .restTranslation);
            }
        } else if (hipsJoint < 0 && diagnostics) {
            diagnostics->warnings.emplace_back(
                "no target joint is bound to 'hips'; root motion was dropped");
        }
    }

    return result;
}

RetargetedAnimation
PoseRetargeter::Retarget(const motion::HumanoidAnimation& animation,
                         RetargetDiagnostics* diagnostics) const
{
    RetargetedAnimation result;
    for (const TargetJoint& joint : _skeleton.GetJoints()) {
        result.joints.push_back(joint.token);
    }
    result.startTime = animation.startTime;
    result.endTime = animation.endTime;
    result.frameRate = animation.nominalFrameRate;
    result.source = animation.source;

    if (diagnostics) {
        for (const motion::HumanBone bone : _map.FindMissingRequiredBones()) {
            diagnostics->missingRequiredBones.push_back(bone);
        }
        for (const int duplicate : _map.FindDuplicateJointIndices()) {
            diagnostics->warnings.push_back(
                "two human bones resolve to target joint index "
                + std::to_string(duplicate)
                + "; the later binding wins and the earlier one is lost");
        }
        if (!_skeleton.IsTopologicallyOrdered()) {
            diagnostics->warnings.emplace_back(
                "target skeleton joints are not in parent-before-child order");
        }
    }

    const motion::HumanoidAnimation* source = &animation;
    motion::HumanoidAnimation resampled;
    if (_options.resampleRate > 0.0) {
        resampled = motion::Resample(animation, _options.resampleRate);
        result.frameRate = _options.resampleRate;
        source = &resampled;
    }

    result.samples.reserve(source->samples.size());
    // Per-sample bone diagnostics would repeat once per frame; collect them
    // from the first sample only and let the caller report each bone once.
    RetargetDiagnostics firstSample;
    bool first = true;
    for (const motion::HumanoidPose& pose : source->samples) {
        result.samples.push_back(
            Retarget(pose, first ? &firstSample : nullptr));
        first = false;
    }
    if (diagnostics) {
        for (const motion::HumanBone bone : firstSample.unmappedSourceBones) {
            diagnostics->unmappedSourceBones.push_back(bone);
        }
        for (const std::string& warning : firstSample.warnings) {
            diagnostics->warnings.push_back(warning);
        }
        if (!firstSample.unmappedSourceBones.empty()) {
            std::string names;
            for (const motion::HumanBone bone : firstSample.unmappedSourceBones) {
                if (!names.empty()) {
                    names += ", ";
                }
                names += Describe(bone);
            }
            diagnostics->warnings.push_back(
                "the clip drives bones the target rig does not map: " + names);
        }
    }

    if (!result.samples.empty()) {
        result.startTime = result.samples.front().timestamp;
        result.endTime = result.samples.back().timestamp;
    }
    return result;
}

} // namespace vrmRetarget
