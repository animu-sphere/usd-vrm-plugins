// SPDX-License-Identifier: Apache-2.0
//
// The human bones a VRM 1.0 avatar must define.
//
// The retarget itself is usd-motion-plugins' `motionRetarget` now, and it
// requires no bone of its own accord: the joint vocabulary carries no
// required-bone rule, so which bones a target needs is its caller's statement
// (`RetargetOptions::requiredBones`, `RetargetMap::FindMissingRequiredBones`).
// This is that statement for a VRM target. Every caller here that retargets
// onto a VRM avatar passes it, which is what keeps a VRM rig that lacks one of
// these bones reported exactly as it was before the retarget left.
#pragma once

#include "vrmRetarget/api.h"

#include "motionCore/MotionPose.h"

#include <vector>

namespace vrmRetarget
{

// VRM 1.0's required humanoid bones, hips first. Eyes, jaw, toes, shoulders,
// fingers and upperChest are optional in VRM 1.0 and deliberately absent.
//
// Hips first because `motionRetarget` requires the hips under `Hips` root
// motion whether or not the caller lists them, and reports them first when it
// adds them: a set that already starts with them reports every missing bone in
// the order this list states.
VRMRETARGET_API const std::vector<openstrata::motion::HumanJoint>& GetRequiredBones();

} // namespace vrmRetarget
