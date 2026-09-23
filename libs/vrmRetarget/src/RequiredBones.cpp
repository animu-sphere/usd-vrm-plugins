// SPDX-License-Identifier: Apache-2.0
#include "vrmRetarget/RequiredBones.h"

namespace vrmRetarget
{

const std::vector<openstrata::motion::HumanJoint>&
GetRequiredBones()
{
    using openstrata::motion::HumanJoint;
    static const std::vector<HumanJoint> required = {
        HumanJoint::Hips,          HumanJoint::Spine,         HumanJoint::Chest,
        HumanJoint::Neck,          HumanJoint::Head,          HumanJoint::LeftUpperLeg,
        HumanJoint::LeftLowerLeg,  HumanJoint::LeftFoot,      HumanJoint::RightUpperLeg,
        HumanJoint::RightLowerLeg, HumanJoint::RightFoot,     HumanJoint::LeftUpperArm,
        HumanJoint::LeftLowerArm,  HumanJoint::LeftHand,      HumanJoint::RightUpperArm,
        HumanJoint::RightLowerArm, HumanJoint::RightHand,
    };
    return required;
}

} // namespace vrmRetarget
