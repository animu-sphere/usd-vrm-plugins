// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "motionBvh/BvhParser.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace motionBvhTool
{

struct Options
{
    // Positional. The plan writes the command as `motion_bvh_inspect input.bvh`
    // (roadmap/recorded-motion-sources.md §5), and the file is the subject of
    // every run rather than one setting among others.
    std::string inputPath;

    // Sections. The summary always prints; each of these adds one block, in the
    // fixed order main.cpp emits them.
    bool hierarchy = false;
    bool channelMap = false;
    bool ranges = false;

    // One motion row, 0-based. Optional rather than a -1 sentinel: row 0 is a
    // legitimate request and would collide with any in-band "none".
    std::optional<std::size_t> frame;

    // Forwarded to the parser verbatim. BvhParser.h calls its limits refusals
    // of the pathological case and says a caller with a genuine reason can
    // raise any of them — this is that caller. A reader meeting a file nobody
    // predicted should not have to rebuild the tool to look at it, and a test
    // can lower a limit to see the refusal without committing a pathological
    // fixture.
    motionBvh::BvhParseLimits limits;
};

// Parses argv. On failure `error` explains why and the result is false; on
// --help `showHelp` is set and the caller should print usage and exit 0.
bool ParseOptions(const std::vector<std::string>& arguments, Options* options,
                  bool* showHelp, std::string* error);

const char* GetUsage();

} // namespace motionBvhTool
