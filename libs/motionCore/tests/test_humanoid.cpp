// SPDX-License-Identifier: Apache-2.0
#include "motionCore/Humanoid.h"

#include <cassert>
#include <string_view>

int
main()
{
    using motion::FindHumanBone;
    using motion::HumanBone;
    using motion::HumanBoneCount;
    using motion::HumanBoneName;

    static_assert(HumanBoneCount == 55, "VRM 1.0 humanoid vocabulary changed");

    assert(HumanBoneName(HumanBone::Hips) == "hips");
    assert(HumanBoneName(HumanBone::RightLittleDistal) == "rightLittleDistal");
    assert(!HumanBoneName(HumanBone::Count).empty() == false);
    assert(FindHumanBone("leftUpperArm") == HumanBone::LeftUpperArm);
    assert(!FindHumanBone("LeftUpperArm"));

    motion::HumanoidPose pose;
    assert(!pose.validRotations.any());
    for (const pxr::GfQuatf& rotation : pose.localRotations) {
        assert(rotation.GetReal() == 1.0f);
        assert(rotation.GetImaginary() == pxr::GfVec3f(0.0f));
    }
    assert(!pose.root.hasPosition);
    assert(!pose.root.hasOrientation);

    // A fresh pose reports no expressions, which is not the same as reporting
    // them all at zero.
    assert(pose.expressions.IsEmpty());
    assert(pose.expressions.Find("happy") == nullptr);

    motion::ExpressionWeights weights;
    assert(weights.Set("happy", 0.5f));
    assert(weights.Set("aa", 0.25f));
    assert(weights.Set("blink", 1.0f));
    // Sorted by name whatever order they arrived in -- the invariant that makes
    // two producers' identical weights compare equal and a trace round-trip to
    // the same bytes.
    assert(weights.entries.size() == 3);
    assert(weights.entries[0].name == "aa");
    assert(weights.entries[1].name == "blink");
    assert(weights.entries[2].name == "happy");

    // A repeat replaces and says so, because only the caller knows whether that
    // is a duplicate delivery to refuse or an update to accept.
    assert(!weights.Set("blink", 0.0f));
    assert(weights.entries.size() == 3);
    assert(weights.Find("blink") != nullptr && *weights.Find("blink") == 0.0f);

    // Reported-and-zero is reachable; unreported answers with no pointer at
    // all, so nothing can read one as the other.
    assert(weights.Find("sad") == nullptr);

    motion::MotionConstraintSet constraints;
    constraints.textPrompt = std::string("walk forward");
    motion::JointRotationConstraint head;
    head.common.joint = HumanBone::Head;
    head.common.coordinateSpace = motion::CoordinateSpace::JointLocal;
    head.common.hard = true;
    constraints.jointRotations.push_back(head);
    assert(constraints.jointRotations.size() == 1);
    assert(constraints.jointRotations[0].common.joint == HumanBone::Head);
    return 0;
}
