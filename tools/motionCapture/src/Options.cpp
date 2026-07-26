// SPDX-License-Identifier: Apache-2.0
#include "Options.h"

#include <cstddef>
#include <exception>
#include <stdexcept>
#include <string>

namespace motionCaptureTool
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
        "motion_capture - replay a recorded capture session into a semantic "
        "humanoid clip\n"
        "\n"
        "Motion Phase D. The trace is pushed frame by frame into a generic\n"
        "LiveCaptureSource, evaluated on a fixed tick, and recorded back into a\n"
        "clip in exactly the form usdVrmaFileFormat produces - so motion_retarget\n"
        "bakes a live session onto an avatar with no changes at all.\n"
        "\n"
        "Usage:\n"
        "  motion_capture --trace <in.trace> --output <clip.usda> [options]\n"
        "  motion_capture --trace <in.trace> --normalize <out.trace>\n"
        "\n"
        "Required:\n"
        "  --trace PATH           Recorded capture trace to replay.\n"
        "  --output PATH          Semantic clip (.usda/.usdc) to author.\n"
        "\n"
        "Timing:\n"
        "  --rate HZ              Evaluation tick rate (default: the trace's\n"
        "                         nominal frame rate).\n"
        "  --delivery-lag S       Seconds each frame arrives behind the tick it\n"
        "                         belongs to (default 0). Raise it to make the\n"
        "                         consumer hold and extrapolate as it would when\n"
        "                         a real transport falls behind.\n"
        "  --extrapolation S      Root lead budget past the newest frame\n"
        "                         (default 0.1). Zero holds instead.\n"
        "  --stale-after S        Frames further behind the head than this are\n"
        "                         counted stale rather than out-of-order.\n"
        "  --buffer-capacity N    Pose history depth (default 120).\n"
        "\n"
        "Intake:\n"
        "  --confidence-floor F   Gate bones reporting less than F in [0, 1].\n"
        "                         Frames carrying no confidence are never gated.\n"
        "  --missing-bones MODE   hold (default) | unbound.\n"
        "  --root-motion MODE     derive (default) | passthrough | ignore.\n"
        "  --smoothing HZ         Filter cutoff; 0 (default) disables it.\n"
        "\n"
        "Output:\n"
        "  --clip-name NAME       Prim name for the UsdSkelAnimation\n"
        "                         (default BodyAnimation).\n"
        "  --normalize PATH       Rewrite the trace canonically and exit.\n"
        "  --dry-run              Replay and report, author nothing.\n"
        "  --report               Print intake and evaluation statistics.\n"
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
        } else if (argument == "--trace") {
            if (!TakeValue(arguments, &i, argument, &options->tracePath,
                           error)) {
                return false;
            }
        } else if (argument == "--output") {
            if (!TakeValue(arguments, &i, argument, &options->outputPath,
                           error)) {
                return false;
            }
        } else if (argument == "--normalize") {
            if (!TakeValue(arguments, &i, argument, &options->normalizePath,
                           error)) {
                return false;
            }
        } else if (argument == "--clip-name") {
            if (!TakeValue(arguments, &i, argument, &options->clipName,
                           error)) {
                return false;
            }
        } else if (argument == "--rate") {
            if (!TakeDouble(arguments, &i, argument, &options->evaluationRate,
                            error)) {
                return false;
            }
            if (options->evaluationRate <= 0.0) {
                *error = "--rate expects a positive frequency";
                return false;
            }
        } else if (argument == "--delivery-lag") {
            if (!TakeDouble(arguments, &i, argument, &options->deliveryLag,
                            error)) {
                return false;
            }
            if (options->deliveryLag < 0.0) {
                *error = "--delivery-lag cannot be negative";
                return false;
            }
        } else if (argument == "--extrapolation") {
            double seconds = 0.0;
            if (!TakeDouble(arguments, &i, argument, &seconds, error)) {
                return false;
            }
            if (seconds < 0.0) {
                *error = "--extrapolation cannot be negative";
                return false;
            }
            options->capture.maxExtrapolationSeconds = seconds;
        } else if (argument == "--stale-after") {
            double seconds = 0.0;
            if (!TakeDouble(arguments, &i, argument, &seconds, error)) {
                return false;
            }
            if (seconds < 0.0) {
                *error = "--stale-after cannot be negative";
                return false;
            }
            options->capture.staleFrameSeconds = seconds;
        } else if (argument == "--buffer-capacity") {
            double capacity = 0.0;
            if (!TakeDouble(arguments, &i, argument, &capacity, error)) {
                return false;
            }
            if (capacity < 1.0) {
                *error = "--buffer-capacity expects at least 1";
                return false;
            }
            options->capture.bufferCapacity =
                static_cast<std::size_t>(capacity);
        } else if (argument == "--confidence-floor") {
            double floor = 0.0;
            if (!TakeDouble(arguments, &i, argument, &floor, error)) {
                return false;
            }
            if (floor < 0.0 || floor > 1.0) {
                *error = "--confidence-floor expects a value in [0, 1]";
                return false;
            }
            options->capture.confidenceFloor = static_cast<float>(floor);
        } else if (argument == "--smoothing") {
            double cutoff = 0.0;
            if (!TakeDouble(arguments, &i, argument, &cutoff, error)) {
                return false;
            }
            if (cutoff < 0.0) {
                *error = "--smoothing cannot be negative";
                return false;
            }
            options->capture.smoothingCutoffHz = static_cast<float>(cutoff);
        } else if (argument == "--missing-bones") {
            std::string mode;
            if (!TakeValue(arguments, &i, argument, &mode, error)) {
                return false;
            }
            if (mode == "hold") {
                options->capture.missingBones =
                    motion::MissingBonePolicy::HoldLast;
            } else if (mode == "unbound") {
                options->capture.missingBones =
                    motion::MissingBonePolicy::LeaveUnbound;
            } else {
                *error = "--missing-bones expects hold or unbound, got '" + mode
                    + "'";
                return false;
            }
        } else if (argument == "--root-motion") {
            std::string mode;
            if (!TakeValue(arguments, &i, argument, &mode, error)) {
                return false;
            }
            if (mode == "derive") {
                options->capture.rootMotion =
                    motion::RootMotionIntake::DeriveVelocity;
            } else if (mode == "passthrough") {
                options->capture.rootMotion =
                    motion::RootMotionIntake::Passthrough;
            } else if (mode == "ignore") {
                options->capture.rootMotion = motion::RootMotionIntake::Ignore;
            } else {
                *error = "--root-motion expects derive, passthrough or ignore, "
                         "got '" + mode + "'";
                return false;
            }
        } else if (argument == "--report") {
            options->report = true;
        } else if (argument == "--dry-run") {
            options->dryRun = true;
        } else if (argument == "--quiet") {
            options->quiet = true;
        } else {
            *error = "unknown argument '" + argument + "'";
            return false;
        }
    }

    if (options->tracePath.empty()) {
        *error = "--trace is required";
        return false;
    }
    if (options->outputPath.empty() && options->normalizePath.empty()
        && !options->dryRun) {
        *error = "--output is required (or use --normalize / --dry-run)";
        return false;
    }
    return true;
}

} // namespace motionCaptureTool
