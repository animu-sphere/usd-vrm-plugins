// SPDX-License-Identifier: Apache-2.0
//
// The stage half of the retarget tool.
//
// Everything that knows about UsdStage lives here; `vrmRetarget` itself takes
// and returns plain values (WORKSPACE.md §2). This file reads the target rig
// and the source clip off stages and writes the result back to one.
#pragma once

#include "vrmRetarget/HumanoidMap.h"
#include "vrmRetarget/PoseRetargeter.h"
#include "vrmRetarget/RestPose.h"
#include "vrmRetarget/TargetSkeleton.h"

#include "motionCore/Humanoid.h"

#include "pxr/usd/usd/stage.h"

#include <map>
#include <string>
#include <vector>

namespace motionRetargetTool
{

struct Avatar
{
    pxr::UsdStageRefPtr stage;
    pxr::SdfPath skeletonPath;
    // The prim the output layer references — the stage's default prim, which
    // must be an ancestor of the skeleton for the binding override to compose.
    pxr::SdfPath defaultPrimPath;
    vrmRetarget::TargetSkeleton skeleton;
    vrmRetarget::HumanoidMap map;
    std::vector<std::string> warnings;
};

struct Clip
{
    pxr::UsdStageRefPtr stage;
    pxr::SdfPath skeletonPath;
    motion::HumanoidAnimation animation;
    vrmRetarget::SourceRestPose restPose;
    double timeCodesPerSecond = 30.0;
    std::vector<std::string> warnings;
};

// Reads a `humanBone -> joint token` JSON object, e.g.
// `{"hips": "Root/Pelvis", "spine": "Root/Pelvis/SpineA"}`.
bool ReadHumanoidMapFile(const std::string& path,
                         std::map<std::string, std::string>* entries,
                         std::string* error);

// Opens the avatar and resolves its target skeleton plus humanoid mapping.
// `skeletonPathOverride` and `extraMappings` may be empty; extra mappings are
// applied over anything found on the stage.
bool ReadAvatar(const std::string& path,
                const std::string& skeletonPathOverride,
                const std::map<std::string, std::string>& extraMappings,
                Avatar* avatar, std::string* error);

// Opens the clip and reads its semantic humanoid animation.
bool ReadClip(const std::string& path, const std::string& skeletonPathOverride,
              Clip* clip, std::string* error);

// Authors `animation` into a new layer that references the avatar and binds the
// result to the target skeleton.
bool WriteRetargetedAnimation(
    const std::string& outputPath, const Avatar& avatar, const Clip& clip,
    const vrmRetarget::RetargetedAnimation& animation,
    const std::string& animationName, std::string* error);

} // namespace motionRetargetTool
