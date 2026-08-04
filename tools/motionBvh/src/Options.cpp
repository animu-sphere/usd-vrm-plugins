// SPDX-License-Identifier: Apache-2.0
#include "Options.h"

#include <cstddef>
#include <exception>
#include <stdexcept>
#include <string>

namespace motionBvhTool
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

// Whole numbers only, and no negatives: every count this tool takes is an index
// or a limit. `stoull` accepts "-1" and wraps it to 18446744073709551615, which
// would turn a typo into a limit nothing can exceed.
bool
TakeSize(const std::vector<std::string>& arguments, std::size_t* index,
         const std::string& flag, std::size_t* value, std::string* error)
{
    std::string text;
    if (!TakeValue(arguments, index, flag, &text, error)) {
        return false;
    }
    if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos) {
        *error = flag + " expects a whole number, got '" + text + "'";
        return false;
    }
    try {
        std::size_t consumed = 0;
        const unsigned long long parsed = std::stoull(text, &consumed);
        if (consumed != text.size()) {
            throw std::invalid_argument("trailing characters");
        }
        *value = static_cast<std::size_t>(parsed);
    } catch (const std::exception&) {
        *error = flag + " expects a whole number, got '" + text + "'";
        return false;
    }
    return true;
}

bool
TakePositive(const std::vector<std::string>& arguments, std::size_t* index,
             const std::string& flag, std::size_t* value, std::string* error)
{
    if (!TakeSize(arguments, index, flag, value, error)) {
        return false;
    }
    if (*value == 0) {
        *error = flag + " expects a value of at least 1";
        return false;
    }
    return true;
}

} // namespace

const char*
GetUsage()
{
    return
        "motion_bvh_inspect - report what a BVH file contains\n"
        "\n"
        "Reads BVH syntax and prints it back: the hierarchy, the channels in\n"
        "declaration order, the frame count, the frame time, and the values.\n"
        "It reports no unit, no up axis, no handedness, no rotation order and no\n"
        "humanoid bone, because a BVH file states none of them - those are facts\n"
        "about the application that wrote it, and they live in a producer\n"
        "profile one layer up.\n"
        "\n"
        "Usage:\n"
        "  motion_bvh_inspect <file.bvh> [options]\n"
        "\n"
        "Sections (the summary always prints):\n"
        "  --hierarchy            Joints in declaration order, with offsets,\n"
        "                         channels and row columns.\n"
        "  --channel-map          Which joint and channel each row column is.\n"
        "  --frame N              Print motion row N, 0-based.\n"
        "  --ranges               Per-column smallest and largest value over\n"
        "                         every frame.\n"
        "  --all                  --hierarchy --channel-map --ranges.\n"
        "\n"
        "Limits (refusals of the pathological case; raise one for a file that\n"
        "genuinely needs it):\n"
        "  --max-depth N          Hierarchy levels (default 64).\n"
        "  --max-joints N         Joints (default 4096).\n"
        "  --max-frames N         Motion rows (default 1000000).\n"
        "\n"
        "  -h, --help             Show this message.\n"
        "\n"
        "Exit status: 0 the file was read, 1 the file was refused, 2 the command\n"
        "was wrong.\n";
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
        } else if (argument == "--hierarchy") {
            options->hierarchy = true;
        } else if (argument == "--channel-map") {
            options->channelMap = true;
        } else if (argument == "--ranges") {
            options->ranges = true;
        } else if (argument == "--all") {
            options->hierarchy = true;
            options->channelMap = true;
            options->ranges = true;
        } else if (argument == "--frame") {
            std::size_t frame = 0;
            if (!TakeSize(arguments, &i, argument, &frame, error)) {
                return false;
            }
            options->frame = frame;
        } else if (argument == "--max-depth") {
            if (!TakePositive(arguments, &i, argument,
                              &options->limits.maxHierarchyDepth, error)) {
                return false;
            }
        } else if (argument == "--max-joints") {
            if (!TakePositive(arguments, &i, argument,
                              &options->limits.maxJoints, error)) {
                return false;
            }
        } else if (argument == "--max-frames") {
            if (!TakeSize(arguments, &i, argument, &options->limits.maxFrames,
                          error)) {
                return false;
            }
        } else if (!argument.empty() && argument[0] == '-'
                   && argument != "-") {
            // Before the positional check, so a mistyped flag is reported as an
            // unknown flag rather than accepted as a second file name.
            *error = "unknown argument '" + argument + "'";
            return false;
        } else if (options->inputPath.empty()) {
            options->inputPath = argument;
        } else {
            *error = "this tool reads one file at a time; '" + argument
                + "' is a second";
            return false;
        }
    }

    if (options->inputPath.empty()) {
        *error = "a BVH file to inspect is required";
        return false;
    }
    return true;
}

} // namespace motionBvhTool
