// SPDX-License-Identifier: Apache-2.0
#include "ExecMotionPose.h"

#include <cstddef>
#include <string_view>

namespace execmotion {

std::optional<motion::HumanBone>
BoneForJointPath(const std::string& jointPath)
{
    // A joint path is `parent/child/leaf`; the bone is the leaf. A path with no
    // separator is already a leaf, which is what a flat rig authors.
    const std::size_t slash = jointPath.rfind('/');
    const std::string_view leaf =
        slash == std::string::npos
            ? std::string_view(jointPath)
            : std::string_view(jointPath).substr(slash + 1);
    if (leaf.empty()) {
        return std::nullopt;
    }
    return motion::FindHumanBone(leaf);
}

motion::HumanoidPose
IdentityPoseForJoints(const std::vector<std::string>& jointPaths)
{
    motion::HumanoidPose pose;
    // HumanoidPose's default constructor already fills localRotations with the
    // identity quaternion and clears validRotations, so this loop only says
    // which bones the clip named -- it authors no rotation at all.
    for (const std::string& jointPath : jointPaths) {
        if (const std::optional<motion::HumanBone> bone =
                BoneForJointPath(jointPath)) {
            pose.validRotations.set(static_cast<std::size_t>(*bone));
        }
    }
    return pose;
}

std::optional<motion::HumanoidPose>
PoseFromClipSample(const ClipSample& sample)
{
    if (!(sample.timeCodesPerSecond > 0.0)) {
        return std::nullopt;
    }

    motion::HumanoidPose pose;

    // The default time code is not frame zero, and this is the one place the
    // difference does not produce a wrong number: a pose resolved outside a
    // timeline carries no second, `timestamp` has no absent state, and frame
    // zero converts to 0.0 at every rate -- so "no time" and "the first frame"
    // are the same value here whatever the clip's rate is. What differs between
    // the two is which values USD resolved, and that happened before this call.
    if (sample.hasTimeCode) {
        pose.timestamp = sample.timeCode / sample.timeCodesPerSecond;
    }

    const std::size_t jointCount = sample.jointPaths.size();
    const bool rotationsUsable = sample.rotations.size() == jointCount;
    const bool translationsUsable = sample.translations.size() == jointCount;

    for (std::size_t i = 0; i < jointCount; ++i) {
        const std::optional<motion::HumanBone> bone =
            BoneForJointPath(sample.jointPaths[i]);
        if (!bone) {
            continue;
        }
        const auto slot = static_cast<std::size_t>(*bone);

        if (rotationsUsable) {
            // Normalized on the way in, like the offline reader: a clip may
            // author a quaternion that has drifted off the unit sphere, and
            // every consumer of a canonical pose is entitled to a rotation.
            pose.localRotations[slot] = sample.rotations[i].GetNormalized();
            pose.validRotations.set(slot);
        }

        // Only the hips carry body translation. A `translations` array states
        // one per joint, but the rest of it is the rest pose the source rig was
        // authored with, which a retargeter re-derives for the rig it is aiming
        // at (motion contract; tools/motionRetarget reads a clip the same way).
        if (translationsUsable && *bone == motion::HumanBone::Hips) {
            pose.root.worldPosition = sample.translations[i];
            pose.root.hasPosition = true;
        }
    }

    return pose;
}

motion::HumanoidPose
FilteredPose(const motion::HumanoidPose& prior,
             const motion::HumanoidPose& pose,
             const FilterPolicy& policy)
{
    // The library's defaults, then whatever the clip actually stated. Each
    // field is overwritten independently, so a clip that authors a cutoff and
    // nothing else keeps the library's answer for the other two.
    motion::PoseFilter::Options options;
    if (policy.cutoffHz) {
        options.cutoffHz = *policy.cutoffHz;
    }
    if (policy.filterRootPosition) {
        options.filterRootPosition = *policy.filterRootPosition;
    }
    if (policy.filterRootOrientation) {
        options.filterRootOrientation = *policy.filterRootOrientation;
    }

    // The whole node, and it is a wrapper: a filter constructed here, seeded
    // with the prior pose, stepped once. The first Apply is the seed -- a
    // PoseFilter with no state returns its argument and keeps it -- and the
    // second is the step whose weight the two timestamps decide.
    //
    // The filter is a local rather than a member of anything: it lives for one
    // call, sees exactly the two poses it was given, and is destroyed. That is
    // what makes this callable from a computation at all.
    motion::PoseFilter filter(options);
    filter.Apply(prior);
    return filter.Apply(pose);
}

std::optional<motion::RootMotionIntake>
RootIntakeForToken(std::string_view token)
{
    // Three spellings and no synonyms. A table rather than a chain of ifs
    // because the set is closed: it is `motion::RootMotionIntake`, and a fourth
    // policy is a change to the library that has to reach this list.
    if (token == "passthrough") {
        return motion::RootMotionIntake::Passthrough;
    }
    if (token == "ignore") {
        return motion::RootMotionIntake::Ignore;
    }
    if (token == "deriveVelocity") {
        return motion::RootMotionIntake::DeriveVelocity;
    }
    return std::nullopt;
}

motion::RootMotion
RootMotionFrom(const motion::HumanoidPose& prior,
               const motion::HumanoidPose& pose,
               const RootPolicy& policy)
{
    // The library's default, read from the library. `LiveCaptureConfig` is what
    // every other caller of this rule is configured with, so a clip that states
    // nothing gets exactly what a live session that states nothing gets --
    // including on the day that default changes.
    const motion::RootMotionIntake intake =
        policy.intake ? *policy.intake
                      : motion::LiveCaptureConfig{}.rootMotion;

    if (intake == motion::RootMotionIntake::Ignore) {
        return motion::RootMotion();
    }

    motion::RootMotion root = pose.root;
    if (intake != motion::RootMotionIntake::DeriveVelocity) {
        return root;
    }

    // Four conditions, and each is the library's: a velocity is derived only
    // where there is a position to differentiate, no velocity the source
    // already reported, a previous position to differentiate against, and time
    // between the two. A pose that fails any of them keeps whatever the clip
    // stated -- nothing is invented, which is the same rule the rest of this
    // bundle keeps for a value nobody measured.
    if (!root.hasPosition || root.hasLinearVelocity || !prior.root.hasPosition) {
        return root;
    }
    const double elapsed = pose.timestamp - prior.timestamp;
    if (elapsed > 0.0) {
        root.linearVelocity =
            (root.worldPosition - prior.root.worldPosition)
            / static_cast<float>(elapsed);
        root.hasLinearVelocity = true;
    }
    return root;
}

} // namespace execmotion
