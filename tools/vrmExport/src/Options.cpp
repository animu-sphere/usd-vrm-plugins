// SPDX-License-Identifier: Apache-2.0
#include "Options.h"

namespace vrmExport
{

const char*
GetUsage()
{
    return "usage: vrm_export <input.vrm> -o <output.{usda,usdc,usdz}> [options]\n"
           "\n"
           "Writes an imported .vrm as native USD that opens with no VRM plugin\n"
           "installed. The output format is the output path's extension. Textures\n"
           "are copied into textures/ next to a .usda or .usdc, and into the\n"
           "package for a .usdz.\n"
           "\n"
           "options:\n"
           "  -o, --output <path>  the output file (required)\n"
           "  --check              reopen and validate the output\n"
           "  --overwrite          replace an existing output file\n"
           "  --verbose            report each localized texture and each check\n"
           "  --help               print this and exit\n"
           "  --version            print the version and exit\n"
           "\n"
           "exit codes: 0 success, 1 unexpected failure, 2 invalid arguments,\n"
           "3 input open failure, 4 export failure, 5 packaging failure,\n"
           "6 validation failure\n";
}

bool
ParseOptions(const std::vector<std::string>& arguments, Options* options, std::string* error)
{
    std::vector<std::string> positional;
    for (std::size_t i = 0; i < arguments.size(); ++i)
    {
        const std::string& argument = arguments[i];
        if (argument == "-h" || argument == "--help")
        {
            options->showHelp = true;
            return true;
        }
        if (argument == "--version")
        {
            options->showVersion = true;
            return true;
        }
        if (argument == "-o" || argument == "--output")
        {
            if (i + 1 == arguments.size())
            {
                *error = argument + " needs a path";
                return false;
            }
            if (!options->outputPath.empty())
            {
                *error = "the output is given twice";
                return false;
            }
            options->outputPath = arguments[++i];
        }
        else if (argument == "--check")
        {
            options->check = true;
        }
        else if (argument == "--overwrite")
        {
            options->overwrite = true;
        }
        else if (argument == "--verbose")
        {
            options->verbose = true;
        }
        else if (argument.size() > 1 && argument[0] == '-')
        {
            *error = "unknown option " + argument;
            return false;
        }
        else
        {
            positional.push_back(argument);
        }
    }

    // One spelling for the output: `-o`, never a second positional
    // argument (policy §5.1).
    if (positional.empty())
    {
        *error = "no input .vrm given";
        return false;
    }
    if (positional.size() > 1)
    {
        *error = "one input only; name the output with -o";
        return false;
    }
    if (options->outputPath.empty())
    {
        *error = "no output given; name it with -o";
        return false;
    }
    options->inputPath = positional.front();
    return true;
}

} // namespace vrmExport
