// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "vrmRetarget/RootMotionPolicy.h"

#include <string>
#include <vector>

namespace motionRetargetTool
{

struct Options
{
    std::string avatarPath;
    std::string animationPath;
    std::string outputPath;

    // Explicit human bone -> target joint token map, for a rig that carries no
    // VrmHumanoidAPI. Merged over anything read from the avatar stage.
    std::string humanoidMapPath;

    // Prim paths, when auto-detection picks the wrong prim or finds none.
    std::string targetSkeletonPath;
    std::string clipSkeletonPath;

    // Prim name for the authored UsdSkelAnimation.
    std::string animationName = "RetargetedAnimation";

    vrmRetarget::RootMotionOptions rootMotion;
    // Joint token that receives root motion under --root-motion root.
    std::string rootJointToken;

    double resampleRate = 0.0;

    // Resolve the clip's expression weights onto the avatar's binds and author
    // the result. On by default, because a rig and a clip that both carry
    // expressions mean the face to move; --no-expressions bakes the body alone,
    // which is what a pipeline that drives the face from elsewhere wants.
    bool expressions = true;

    bool quiet = false;
};

// Parses argv. On failure `error` explains why and the result is false; on
// --help `showHelp` is set and the caller should print usage and exit 0.
bool ParseOptions(const std::vector<std::string>& arguments, Options* options,
                  bool* showHelp, std::string* error);

const char* GetUsage();

} // namespace motionRetargetTool
