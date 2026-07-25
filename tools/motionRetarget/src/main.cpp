// SPDX-License-Identifier: Apache-2.0
//
// motion_retarget — Motion Phase C's bake tool.
//
// It is the composition point, not the algorithm: `vrmRetarget` does the
// retargeting over plain values, `StageIo` does everything that touches a
// stage, and this file wires the two together and reports what happened.
#include "Options.h"
#include "StageIo.h"

#include "vrmRetarget/PoseRetargeter.h"

#include <cstdio>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace
{

void
ReportWarnings(const std::vector<std::string>& warnings, bool quiet)
{
    if (quiet) {
        return;
    }
    for (const std::string& warning : warnings) {
        std::cerr << "motion_retarget: warning: " << warning << "\n";
    }
}

std::string
JoinBones(const std::vector<motion::HumanBone>& bones)
{
    std::string joined;
    for (const motion::HumanBone bone : bones) {
        if (!joined.empty()) {
            joined += ", ";
        }
        joined += std::string(motion::HumanBoneName(bone));
    }
    return joined;
}

} // namespace

int
main(int argc, char** argv)
{
    const std::vector<std::string> arguments(argv + 1, argv + argc);

    motionRetargetTool::Options options;
    bool showHelp = false;
    std::string error;
    if (!motionRetargetTool::ParseOptions(arguments, &options, &showHelp,
                                          &error)) {
        std::cerr << "motion_retarget: " << error << "\n\n"
                  << motionRetargetTool::GetUsage();
        return 2;
    }
    if (showHelp) {
        std::fputs(motionRetargetTool::GetUsage(), stdout);
        return 0;
    }

    std::map<std::string, std::string> extraMappings;
    if (!options.humanoidMapPath.empty()
        && !motionRetargetTool::ReadHumanoidMapFile(options.humanoidMapPath,
                                                    &extraMappings, &error)) {
        std::cerr << "motion_retarget: " << error << "\n";
        return 1;
    }

    motionRetargetTool::Avatar avatar;
    if (!motionRetargetTool::ReadAvatar(options.avatarPath,
                                        options.targetSkeletonPath,
                                        extraMappings, &avatar, &error)) {
        std::cerr << "motion_retarget: " << error << "\n";
        return 1;
    }
    ReportWarnings(avatar.warnings, options.quiet);

    motionRetargetTool::Clip clip;
    if (!motionRetargetTool::ReadClip(options.animationPath,
                                      options.clipSkeletonPath, &clip, &error)) {
        std::cerr << "motion_retarget: " << error << "\n";
        return 1;
    }
    ReportWarnings(clip.warnings, options.quiet);

    vrmRetarget::RetargetOptions retargetOptions;
    retargetOptions.rootMotion = options.rootMotion;
    retargetOptions.resampleRate = options.resampleRate;
    if (retargetOptions.rootMotion.mode
        == vrmRetarget::RootMotionMode::RootJoint) {
        const int index = avatar.skeleton.FindJoint(options.rootJointToken);
        if (index < 0) {
            std::cerr << "motion_retarget: --root-joint '"
                      << options.rootJointToken
                      << "' is not a joint of the target skeleton\n";
            return 1;
        }
        retargetOptions.rootMotion.rootJointIndex = index;
    }

    const vrmRetarget::PoseRetargeter retargeter(avatar.skeleton, avatar.map,
                                                 clip.restPose,
                                                 retargetOptions);
    vrmRetarget::RetargetDiagnostics diagnostics;
    const vrmRetarget::RetargetedAnimation retargeted =
        retargeter.Retarget(clip.animation, &diagnostics);

    if (!options.quiet) {
        if (!diagnostics.missingRequiredBones.empty()) {
            std::cerr << "motion_retarget: warning: the target rig maps no "
                         "joint for required bones: "
                      << JoinBones(diagnostics.missingRequiredBones) << "\n";
        }
        ReportWarnings(diagnostics.warnings, options.quiet);
    }

    if (!motionRetargetTool::WriteRetargetedAnimation(
            options.outputPath, avatar, clip, retargeted,
            options.animationName, &error)) {
        std::cerr << "motion_retarget: " << error << "\n";
        return 1;
    }

    if (!options.quiet) {
        std::cout << "motion_retarget: wrote " << options.outputPath << " ("
                  << retargeted.samples.size() << " samples over "
                  << retargeted.joints.size() << " joints, "
                  << avatar.map.GetMappedCount() << " humanoid bones bound)\n";
    }
    return 0;
}
