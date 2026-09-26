// SPDX-License-Identifier: Apache-2.0
//
// vrm_export's command line (docs/design/VRM_EXPORT_POLICY.md §5).
#pragma once

#include <string>
#include <vector>

namespace vrmExport
{

struct Options
{
    std::string inputPath;
    std::string outputPath;
    bool check = false;
    bool overwrite = false;
    bool verbose = false;
    // Print and exit 0, with no other argument required.
    bool showHelp = false;
    bool showVersion = false;
};

// Parses the arguments after the program name. On failure `error` says why
// and the result is false; the caller exits with InvalidArguments.
bool ParseOptions(const std::vector<std::string>& arguments, Options* options,
                  std::string* error);

const char* GetUsage();

} // namespace vrmExport
