// SPDX-License-Identifier: Apache-2.0
#include "Options.h"

#include <cstddef>
#include <exception>
#include <stdexcept>
#include <string>

namespace motionRetargetTool
{
namespace
{

bool
TakeValue(const std::vector<std::string>& arguments, std::size_t* index,
          const std::string& flag, std::string* value, std::string* error)
{
    if (*index + 1 >= arguments.size()) {
        *error = flag + " requires a value";
        return false;
    }
    ++(*index);
    *value = arguments[*index];
    return true;
}

bool
TakeDouble(const std::vector<std::string>& arguments, std::size_t* index,
           const std::string& flag, double* value, std::string* error)
{
    std::string text;
    if (!TakeValue(arguments, index, flag, &text, error)) {
        return false;
    }
    try {
        std::size_t consumed = 0;
        const double parsed = std::stod(text, &consumed);
        if (consumed != text.size()) {
            throw std::invalid_argument("trailing characters");
        }
        *value = parsed;
    } catch (const std::exception&) {
        *error = flag + " expects a number, got '" + text + "'";
        return false;
    }
    return true;
}

} // namespace

const char*
GetUsage()
{
    return
        "motion_retarget - bake a semantic humanoid clip onto a target rig\n"
        "\n"
        "Usage:\n"
        "  motion_retarget --avatar <avatar.vrm|.usd*> \\\n"
        "                  --animation <clip.vrma|.usd*> \\\n"
        "                  --output <out.usda> [options]\n"
        "\n"
        "Required:\n"
        "  --avatar PATH          Target rig. Any layer OpenUSD can open; a\n"
        "                         .vrm needs usdVrmFileFormat registered.\n"
        "  --animation PATH       Source clip. A .vrma needs\n"
        "                         usdVrmaFileFormat registered.\n"
        "  --output PATH          Output .usda/.usdc layer to author.\n"
        "\n"
        "Mapping:\n"
        "  --humanoid-map PATH    JSON object of humanBone -> target joint\n"
        "                         token, e.g. {\"hips\": \"Root/Pelvis\"}.\n"
        "                         Required when the avatar carries no\n"
        "                         VrmHumanoidAPI; merged over it when it does.\n"
        "  --skeleton PRIMPATH    Target UsdSkelSkeleton, when auto-detection\n"
        "                         finds none or the wrong one.\n"
        "  --clip-skeleton PRIMPATH  Source semantic UsdSkelSkeleton.\n"
        "\n"
        "Root motion:\n"
        "  --root-motion MODE     hips (default) | root | ignore.\n"
        "  --root-joint TOKEN     Joint that receives motion under 'root'.\n"
        "  --translation-scale N  Scale on the root translation delta.\n"
        "  --preserve-target-height  Keep the target's rest height; take only\n"
        "                         the horizontal delta.\n"
        "\n"
        "Output:\n"
        "  --animation-name NAME  Prim name for the authored UsdSkelAnimation\n"
        "                         (default RetargetedAnimation).\n"
        "  --resample HZ          Resample onto a uniform timeline first.\n"
        "  --no-expressions       Bake the body only; do not resolve the\n"
        "                         clip's expression weights onto the avatar's\n"
        "                         blend shapes.\n"
        "  --no-look-at           Bake without a gaze; do not evaluate the\n"
        "                         clip's look-at target against the\n"
        "                         avatar's eyes.\n"
        "  --quiet                Suppress diagnostics on stderr.\n"
        "  -h, --help             Show this message.\n";
}

bool
ParseOptions(const std::vector<std::string>& arguments, Options* options,
             bool* showHelp, std::string* error)
{
    *showHelp = false;
    for (std::size_t i = 0; i < arguments.size(); ++i) {
        const std::string& argument = arguments[i];
        if (argument == "-h" || argument == "--help") {
            *showHelp = true;
            return true;
        } else if (argument == "--avatar") {
            if (!TakeValue(arguments, &i, argument, &options->avatarPath, error)) {
                return false;
            }
        } else if (argument == "--animation") {
            if (!TakeValue(arguments, &i, argument, &options->animationPath,
                           error)) {
                return false;
            }
        } else if (argument == "--output") {
            if (!TakeValue(arguments, &i, argument, &options->outputPath, error)) {
                return false;
            }
        } else if (argument == "--humanoid-map") {
            if (!TakeValue(arguments, &i, argument, &options->humanoidMapPath,
                           error)) {
                return false;
            }
        } else if (argument == "--skeleton") {
            if (!TakeValue(arguments, &i, argument, &options->targetSkeletonPath,
                           error)) {
                return false;
            }
        } else if (argument == "--clip-skeleton") {
            if (!TakeValue(arguments, &i, argument, &options->clipSkeletonPath,
                           error)) {
                return false;
            }
        } else if (argument == "--animation-name") {
            if (!TakeValue(arguments, &i, argument, &options->animationName,
                           error)) {
                return false;
            }
        } else if (argument == "--root-motion") {
            std::string mode;
            if (!TakeValue(arguments, &i, argument, &mode, error)) {
                return false;
            }
            if (mode == "hips") {
                options->rootMotion.mode = vrmRetarget::RootMotionMode::Hips;
            } else if (mode == "root") {
                options->rootMotion.mode = vrmRetarget::RootMotionMode::RootJoint;
            } else if (mode == "ignore") {
                options->rootMotion.mode = vrmRetarget::RootMotionMode::Ignore;
            } else {
                *error = "--root-motion expects hips, root, or ignore; got '"
                    + mode + "'";
                return false;
            }
        } else if (argument == "--root-joint") {
            if (!TakeValue(arguments, &i, argument, &options->rootJointToken,
                           error)) {
                return false;
            }
        } else if (argument == "--translation-scale") {
            double scale = 1.0;
            if (!TakeDouble(arguments, &i, argument, &scale, error)) {
                return false;
            }
            options->rootMotion.translationScale = static_cast<float>(scale);
        } else if (argument == "--preserve-target-height") {
            options->rootMotion.preserveTargetHeight = true;
        } else if (argument == "--resample") {
            if (!TakeDouble(arguments, &i, argument, &options->resampleRate,
                            error)) {
                return false;
            }
            if (options->resampleRate <= 0.0) {
                *error = "--resample expects a positive rate";
                return false;
            }
        } else if (argument == "--no-expressions") {
            options->expressions = false;
        } else if (argument == "--no-look-at") {
            options->lookAt = false;
        } else if (argument == "--quiet") {
            options->quiet = true;
        } else {
            *error = "unknown argument '" + argument + "'";
            return false;
        }
    }

    if (options->avatarPath.empty()) {
        *error = "--avatar is required";
        return false;
    }
    if (options->animationPath.empty()) {
        *error = "--animation is required";
        return false;
    }
    if (options->outputPath.empty()) {
        *error = "--output is required";
        return false;
    }
    if (options->rootMotion.mode == vrmRetarget::RootMotionMode::RootJoint
        && options->rootJointToken.empty()) {
        *error = "--root-motion root also needs --root-joint";
        return false;
    }
    return true;
}

} // namespace motionRetargetTool
