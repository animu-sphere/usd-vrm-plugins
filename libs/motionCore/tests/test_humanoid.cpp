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
