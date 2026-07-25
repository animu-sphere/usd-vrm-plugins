// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "vrmRetarget/api.h"

#include "vrmRetarget/TargetSkeleton.h"

#include "motionCore/Humanoid.h"

#include <array>
#include <bitset>
#include <string>
#include <vector>

namespace vrmRetarget
{

// Which target joint each VRM human bone drives.
//
// The source of the mapping is the caller's problem: a `.vrm` avatar carries it
// as `vrm:humanBones:<bone>` under VrmHumanoidAPI, a hand-written rig supplies
// it as a side file. Either way vrmRetarget receives resolved indices and never
// guesses from joint names — name heuristics are exactly the kind of silent
// mis-retarget this contract exists to prevent.
class VRMRETARGET_API HumanoidMap
{
public:
    static constexpr int kUnmapped = -1;

    HumanoidMap();

    // Binds `bone` to a target joint index. Returns false — leaving the bone
    // unmapped — when either the bone or the joint index is out of range, so a
    // rejected binding is never mistaken for a successful one.
    bool SetJointIndex(motion::HumanBone bone, int jointIndex,
                       std::size_t jointCount);

    // Resolves `token` against `skeleton` and binds it. Returns false when the
    // skeleton has no such joint, leaving the bone unmapped.
    bool SetJointToken(motion::HumanBone bone, const std::string& token,
                       const TargetSkeleton& skeleton);

    void Clear();

    // kUnmapped when the bone does not drive a joint of this rig.
    int GetJointIndex(motion::HumanBone bone) const;
    bool IsMapped(motion::HumanBone bone) const;
    std::size_t GetMappedCount() const noexcept { return _mapped.count(); }

    // The bones a VRM 1.0 avatar must define. A rig missing one of these can
    // still be retargeted onto, but the caller should say so.
    static const std::vector<motion::HumanBone>& GetRequiredBones();

    // Required bones with no binding, in vocabulary order.
    std::vector<motion::HumanBone> FindMissingRequiredBones() const;

    // True when two bones resolve to the same joint — always a mapping bug,
    // because the second binding would silently overwrite the first.
    std::vector<int> FindDuplicateJointIndices() const;

private:
    std::array<int, motion::HumanBoneCount> _jointIndices;
    std::bitset<motion::HumanBoneCount> _mapped;
};

} // namespace vrmRetarget
