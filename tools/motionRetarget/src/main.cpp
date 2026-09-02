// SPDX-License-Identifier: Apache-2.0
//
// motion_retarget — Motion Phase C's bake tool.
//
// It is the composition point, not the algorithm: `vrmRetarget` does the
// retargeting over plain values, `StageIo` does everything that touches a
// stage, and this file wires the two together and reports what happened.
#include "Options.h"
#include "StageIo.h"

#include "vrmRetarget/ExpressionResolver.h"
#include "vrmRetarget/PoseRetargeter.h"

#include "motionRuntime/Resample.h"

#include <cstdio>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <utility>
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

std::string
JoinNames(const std::vector<std::string>& names)
{
    std::string joined;
    for (const std::string& name : names) {
        if (!joined.empty()) {
            joined += ", ";
        }
        joined += "'" + name + "'";
    }
    return joined;
}

// The distinct material colour slots a clip drives on this rig.
//
// They are resolved and deliberately not authored: a colour slot is a material
// input, and the material layer owns what an MToon or a UsdPreviewSurface calls
// it. Reporting the count is what keeps that a stated boundary rather than a
// silent omission -- an operator whose clip turns a face red sees why it did
// not.
std::size_t
CountMaterialColors(
    const std::vector<vrmRetarget::ResolvedExpressions>& expressions)
{
    std::set<std::pair<std::string, std::string>> slots;
    for (const vrmRetarget::ResolvedExpressions& sample : expressions) {
        for (const vrmRetarget::ResolvedMaterialColor& color :
             sample.materialColors) {
            slots.emplace(color.material, color.colorType);
        }
    }
    return slots.size();
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

    // Resample once, here, rather than inside the retargeter. The body and the
    // face are two expansions of the same samples, and what keeps them on one
    // timeline is that they expand the same list: a retargeter-side resample
    // would move the joints onto a uniform timeline while the expressions
    // stayed on the clip's key times, and the two would meet at neither.
    motion::HumanoidAnimation resampled;
    const motion::HumanoidAnimation* source = &clip.animation;
    if (options.resampleRate > 0.0) {
        resampled = motion::Resample(clip.animation, options.resampleRate);
        source = &resampled;
    }

    vrmRetarget::RetargetOptions retargetOptions;
    retargetOptions.rootMotion = options.rootMotion;
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
        retargeter.Retarget(*source, &diagnostics);

    if (!options.quiet) {
        if (!diagnostics.missingRequiredBones.empty()) {
            std::cerr << "motion_retarget: warning: the target rig maps no "
                         "joint for required bones: "
                      << JoinBones(diagnostics.missingRequiredBones) << "\n";
        }
        ReportWarnings(diagnostics.warnings, options.quiet);
    }

    // The face half of the same samples.
    //
    // A rig that declares no expression at all is resolved against anyway,
    // rather than short-circuited: it resolves nothing, and the point is that
    // it *says* so. That is the total-loss case -- every name the clip animates
    // going missing at once -- and it is the one a bake must not pass over in
    // silence, so the empty rig takes the same path as a rig missing one name.
    std::vector<vrmRetarget::ResolvedExpressions> expressions;
    vrmRetarget::ExpressionDiagnostics expressionDiagnostics;
    if (options.expressions) {
        const vrmRetarget::ExpressionResolver resolver(avatar.expressionRig);
        expressions.reserve(source->samples.size());
        for (const motion::HumanoidPose& pose : source->samples) {
            expressions.push_back(resolver.Resolve(pose,
                                                   &expressionDiagnostics));
        }
    }

    if (!options.quiet) {
        if (!expressionDiagnostics.unresolvedNames.empty()) {
            std::cerr << "motion_retarget: warning: the clip animates "
                         "expressions the avatar does not declare: "
                      << JoinNames(expressionDiagnostics.unresolvedNames)
                      << "\n";
        }
        if (!expressionDiagnostics.clampedNames.empty()) {
            std::cerr << "motion_retarget: warning: expression weights "
                         "outside [0, 1] were clamped: "
                      << JoinNames(expressionDiagnostics.clampedNames) << "\n";
        }
        ReportWarnings(expressionDiagnostics.warnings, options.quiet);
        const std::size_t materialColors = CountMaterialColors(expressions);
        if (materialColors != 0) {
            std::cerr << "motion_retarget: warning: the clip drives "
                      << materialColors
                      << " material colour slot(s) of this rig; "
                         "motion_retarget authors blend-shape weights only, "
                         "so they are not written\n";
        }
    }

    motionRetargetTool::WriteResult written;
    if (!motionRetargetTool::WriteRetargetedAnimation(
            options.outputPath, avatar, clip, retargeted, expressions,
            options.animationName, &written, &error)) {
        std::cerr << "motion_retarget: " << error << "\n";
        return 1;
    }
    ReportWarnings(written.warnings, options.quiet);

    if (!options.quiet) {
        std::cout << "motion_retarget: wrote " << options.outputPath << " ("
                  << retargeted.samples.size() << " samples over "
                  << retargeted.joints.size() << " joints, "
                  << avatar.map.GetMappedCount() << " humanoid bones bound";
        if (written.blendShapesAuthored != 0) {
            std::cout << ", " << written.blendShapesAuthored
                      << " blend shapes driven";
        }
        std::cout << ")\n";
    }
    return 0;
}
