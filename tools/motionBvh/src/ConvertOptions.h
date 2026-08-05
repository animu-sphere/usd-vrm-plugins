// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "motionBvh/BvhParser.h"

#include <string>
#include <vector>

namespace motionBvhTool
{

// `motion_bvh_convert`'s arguments. A separate type from the inspect tool's
// `Options` rather than a superset of it: the two commands share a directory
// and a library, and nothing else. An inspection has no output and no profile,
// a conversion has no `--frame`, and one struct carrying both would make every
// field's presence a question about which executable is reading it.
struct ConvertOptions
{
    // Positional, as `motion_bvh_inspect` takes it and for the same reason: the
    // file is the subject of the run rather than one setting among others
    // (roadmap/recorded-motion-sources.md §5).
    std::string inputPath;

    // A profile id, or a path to a profile file. Empty is the state a
    // conversion refuses from, and it is deliberately **not** an argument error
    // here: the frozen diagnostic set names that event
    // `VRM_BVH_PROFILE_REQUIRED`, so main raises it with its code rather than
    // as a generic complaint about argv. There is no default and no fallback
    // (roadmap §3.1).
    std::string profile;

    // Directories searched before every built-in location, in the order given.
    // A workspace running out of a build tree, and a test that must name the
    // profile it is about rather than whichever one is installed, both need
    // this; see ProfileLocator.h for the whole search order.
    std::vector<std::string> profileDirs;

    std::string outputPath;

    // Prim name for the authored UsdSkelAnimation, as `motion_capture` spells
    // it. The default differs from that tool's `BodyAnimation` because these
    // clips are named after where they came from and a recorded file is not a
    // session.
    std::string clipName = "SourceAnimation";

    bool quiet = false;

    // Forwarded to the parser verbatim, exactly as the inspect tool forwards
    // them: a reader who had to raise a limit to look at a file should not have
    // to rebuild the tool to convert the same file.
    motionBvh::BvhParseLimits limits;
};

// Parses argv. On failure `error` explains why and the result is false; on
// --help `showHelp` is set and the caller should print usage and exit 0.
//
// A missing `--profile` is not a failure here. See the field's note.
bool ParseConvertOptions(const std::vector<std::string>& arguments,
                         ConvertOptions* options, bool* showHelp,
                         std::string* error);

const char* GetConvertUsage();

} // namespace motionBvhTool
