// SPDX-License-Identifier: Apache-2.0
//
// vrm_export -- an imported `.vrm` written as native `.usda`, `.usdc` or
// `.usdz` (docs/design/VRM_EXPORT_POLICY.md).
//
// This file is the command line and nothing else: it parses the arguments,
// runs the export operation (export/VrmExport.h) and turns its result into
// output and an exit code (policy §5.3).
#include "Options.h"
#include "export/VrmExport.h"

#include <exception>
#include <iostream>
#include <string>
#include <vector>

#ifndef VRM_EXPORT_VERSION
#define VRM_EXPORT_VERSION "unknown"
#endif

int
main(int argc, char** argv)
{
    using vrmExport::Status;

    std::vector<std::string> arguments(argv + 1, argv + argc);
    vrmExport::Options options;
    std::string error;
    if (!vrmExport::ParseOptions(arguments, &options, &error))
    {
        std::cerr << "vrm_export: " << error << "\n\n" << vrmExport::GetUsage();
        return static_cast<int>(Status::InvalidArguments);
    }
    if (options.showHelp)
    {
        std::cout << vrmExport::GetUsage();
        return 0;
    }
    if (options.showVersion)
    {
        std::cout << "vrm_export " << VRM_EXPORT_VERSION << "\n";
        return 0;
    }

    vrmExport::ExportOptions exportOptions;
    exportOptions.check = options.check;
    exportOptions.overwrite = options.overwrite;

    vrmExport::ExportResult result;
    try
    {
        result = vrmExport::ExportVrm(options.inputPath, options.outputPath, exportOptions);
    }
    catch (const std::exception& exception)
    {
        std::cerr << "vrm_export: unexpected failure: " << exception.what() << "\n";
        return static_cast<int>(Status::UnexpectedFailure);
    }

    if (options.verbose)
    {
        for (const vrmExport::LocalizedAsset& asset : result.assets)
        {
            std::cerr << "vrm_export: localized " << asset.sourcePath << " -> "
                      << asset.outputPath << "\n";
        }
        for (const std::string& check : result.checks)
        {
            std::cerr << "vrm_export: check: " << check << "\n";
        }
    }
    if (result.status != Status::Success)
    {
        std::cerr << "vrm_export: " << result.message << "\n";
        return static_cast<int>(result.status);
    }
    if (options.verbose)
    {
        std::cerr << "vrm_export: wrote " << options.outputPath << "\n";
    }
    return 0;
}
