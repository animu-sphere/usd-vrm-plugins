// SPDX-License-Identifier: Apache-2.0
#include "ConvertOptions.h"

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

// Whole numbers only, and no negatives, for `Options.cpp`'s reason: `stoull`
// accepts "-1" and wraps it to 18446744073709551615, which would turn a typo
// into a limit nothing can exceed.
bool
TakeSize(const std::vector<std::string>& arguments, std::size_t* index,
         const std::string& flag, std::size_t* value, std::string* error)
{
    std::string text;
    if (!TakeValue(arguments, index, flag, &text, error)) {
        return false;
    }
    if (text.empty()
        || text.find_first_not_of("0123456789") != std::string::npos) {
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
GetConvertUsage()
{
    return
        "motion_bvh_convert - a BVH file and a named profile to a semantic "
        "clip\n"
        "\n"
        "Reads a BVH file, reads it the way the named producer profile says to,\n"
        "and writes the avatar-independent semantic humanoid clip\n"
        "`motion_retarget` consumes. It never binds to a target avatar: source\n"
        "rest to target rest is the retargeter's correction, and a converter\n"
        "that applied it would be a second one.\n"
        "\n"
        "There is no default profile and no automatic fallback. A BVH file\n"
        "carries no reliable statement of who wrote it, and a near-miss profile\n"
        "produces motion that is subtly misassembled rather than absent - which\n"
        "is worse than a refusal because it looks like a result.\n"
        "\n"
        "Usage:\n"
        "  motion_bvh_convert <file.bvh> --profile <id> --output <clip.usda>\n"
        "\n"
        "Required:\n"
        "  --profile ID|PATH      The producer profile to read the file as: an\n"
        "                         id looked up in the profile search path, or a\n"
        "                         path to a profile file.\n"
        "  --output PATH          The semantic clip to write.\n"
        "\n"
        "Options:\n"
        "  --profile-dir DIR      Search DIR for profiles before the built-in\n"
        "                         locations. Repeatable, in the order given.\n"
        "  --clip-name NAME       Prim name for the authored UsdSkelAnimation\n"
        "                         (default SourceAnimation).\n"
        "  --quiet                Write the clip and print nothing.\n"
        "\n"
        "Limits (refusals of the pathological case; raise one for a file that\n"
        "genuinely needs it):\n"
        "  --max-depth N          Hierarchy levels (default 64).\n"
        "  --max-joints N         Joints (default 4096).\n"
        "  --max-frames N         Motion rows (default 1000000).\n"
        "\n"
        "  --                     Everything after this is the file, however it\n"
        "                         is spelled.\n"
        "  -h, --help             Show this message.\n"
        "\n"
        "Exit status: 0 the clip was written, 1 the conversion produced no\n"
        "clip, 2 the command or what it named was wrong.\n";
}

bool
ParseConvertOptions(const std::vector<std::string>& arguments,
                    ConvertOptions* options, bool* showHelp,
                    std::string* error)
{
    *showHelp = false;
    for (std::size_t i = 0; i < arguments.size(); ++i) {
        const std::string& argument = arguments[i];
        if (argument == "-h" || argument == "--help") {
            *showHelp = true;
            return true;
        } else if (argument == "--profile") {
            if (!TakeValue(arguments, &i, argument, &options->profile, error)) {
                return false;
            }
        } else if (argument == "--profile-dir") {
            std::string directory;
            if (!TakeValue(arguments, &i, argument, &directory, error)) {
                return false;
            }
            options->profileDirs.push_back(directory);
        } else if (argument == "--output") {
            if (!TakeValue(arguments, &i, argument, &options->outputPath,
                           error)) {
                return false;
            }
        } else if (argument == "--clip-name") {
            if (!TakeValue(arguments, &i, argument, &options->clipName,
                           error)) {
                return false;
            }
        } else if (argument == "--quiet") {
            options->quiet = true;
        } else if (argument == "--max-depth") {
            if (!TakePositive(arguments, &i, argument,
                              &options->limits.maxHierarchyDepth, error)) {
                return false;
            }
        } else if (argument == "--max-joints") {
            if (!TakePositive(arguments, &i, argument, &options->limits.maxJoints,
                              error)) {
                return false;
            }
        } else if (argument == "--max-frames") {
            // TakeSize rather than TakePositive, as the inspect tool takes it:
            // `--max-frames 0` means "refuse any file carrying a motion row",
            // which is a coherent thing to ask of a format where `Frames: 0` is
            // legal.
            if (!TakeSize(arguments, &i, argument, &options->limits.maxFrames,
                          error)) {
                return false;
            }
        } else if (argument == "--") {
            if (i + 1 >= arguments.size()) {
                *error = "-- must be followed by a file";
                return false;
            }
            ++i;
            if (!options->inputPath.empty()) {
                *error = "this tool converts one file at a time; '"
                    + arguments[i] + "' is a second";
                return false;
            }
            options->inputPath = arguments[i];
            if (i + 1 < arguments.size()) {
                *error = "this tool converts one file at a time; '"
                    + arguments[i + 1] + "' is a second";
                return false;
            }
        } else if (!argument.empty() && argument[0] == '-' && argument != "-") {
            // Before the positional check, so a mistyped flag is reported as an
            // unknown flag rather than accepted as a second file name.
            *error = "unknown argument '" + argument + "'";
            return false;
        } else if (options->inputPath.empty()) {
            options->inputPath = argument;
        } else {
            *error = "this tool converts one file at a time; '" + argument
                + "' is a second";
            return false;
        }
    }

    if (options->inputPath.empty()) {
        *error = "a BVH file to convert is required";
        return false;
    }
    if (options->outputPath.empty()) {
        *error = "--output is required; this tool writes a clip and does not "
                 "print one";
        return false;
    }
    if (options->clipName.empty()) {
        *error = "--clip-name requires a name";
        return false;
    }
    // `--profile` is checked by main, not here. See ConvertOptions::profile.
    return true;
}

} // namespace motionBvhTool
