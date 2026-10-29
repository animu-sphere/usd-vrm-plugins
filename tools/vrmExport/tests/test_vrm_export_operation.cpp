// SPDX-License-Identifier: Apache-2.0
//
// The export operation's unit tests: format detection, the command line, the
// refusals that come before any stage is opened, and `check` on outputs
// written by hand. They run with no VRM plugin registered, which is what makes
// the one `.vrm` case here a test of the missing-plugin refusal.
//
// Checks with `assert()`, which NDEBUG compiles away; the CMake target
// undefines it, so a Release build does not pass every check vacuously.
#include "Options.h"
#include "export/VrmExport.h"

#include "pxr/base/arch/fileSystem.h"
#include "pxr/base/js/json.h"
#include "pxr/base/tf/fileUtils.h"
#include "pxr/base/tf/pathUtils.h"
#include "pxr/base/tf/stringUtils.h"

#include <cassert>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

using vrmExport::DetectOutputFormat;
using vrmExport::ExportOptions;
using vrmExport::ExportResult;
using vrmExport::OutputFormat;
using vrmExport::Status;

namespace
{

void
WriteFile(const std::string& path, const std::string& text)
{
    std::ofstream out(path, std::ios::binary);
    out << text;
    assert(out.good());
}

bool
Contains(const std::string& text, const std::string& part)
{
    return text.find(part) != std::string::npos;
}

void
TestDetectOutputFormat()
{
    assert(DetectOutputFormat("a.usda") == OutputFormat::Usda);
    assert(DetectOutputFormat("a.usdc") == OutputFormat::Usdc);
    assert(DetectOutputFormat("a.usdz") == OutputFormat::Usdz);
    assert(DetectOutputFormat("dir/A.USDZ") == OutputFormat::Usdz);
    // `.usd` names no single format, so it is not a choice this tool makes.
    assert(DetectOutputFormat("a.usd") == OutputFormat::Unknown);
    assert(DetectOutputFormat("a.vrm") == OutputFormat::Unknown);
    assert(DetectOutputFormat("usda") == OutputFormat::Unknown);
    assert(DetectOutputFormat("") == OutputFormat::Unknown);
}

void
TestHashBytes()
{
    // vrmContainer's offset basis, which is FNV-1a 64's with its last digit
    // missing (14695981039346656037). It is kept so a localized embedded image
    // keeps the name the importer gave it; the published FNV-1a vector for "a"
    // (0xaf63dc4c8601ec8c) therefore does not hold here, on purpose.
    assert(vrmExport::HashBytes("", 0) == 1469598103934665603ull);
    assert(vrmExport::HashBytes("a", 1) == 0x44bd8ad473cd9906ull);
}

bool
Parse(const std::vector<std::string>& arguments, vrmExport::Options* options, std::string* error)
{
    *options = vrmExport::Options();
    error->clear();
    return vrmExport::ParseOptions(arguments, options, error);
}

void
TestParseOptions()
{
    vrmExport::Options options;
    std::string error;

    assert(
        Parse({"a.vrm", "-o", "a.usdz", "--check", "--overwrite", "--verbose"}, &options, &error));
    assert(options.inputPath == "a.vrm" && options.outputPath == "a.usdz");
    assert(options.check && options.overwrite && options.verbose);

    assert(Parse({"--output", "b.usda", "b.vrm"}, &options, &error));
    assert(options.inputPath == "b.vrm" && options.outputPath == "b.usda");
    assert(!options.check && !options.overwrite);

    assert(Parse({"--help"}, &options, &error) && options.showHelp);
    assert(Parse({"--version"}, &options, &error) && options.showVersion);

    assert(!Parse({}, &options, &error) && Contains(error, "no input"));
    assert(!Parse({"a.vrm"}, &options, &error) && Contains(error, "-o"));
    // One spelling for the output (policy §5.1).
    assert(!Parse({"a.vrm", "a.usdz"}, &options, &error) && Contains(error, "-o"));
    assert(!Parse({"a.vrm", "-o"}, &options, &error) && Contains(error, "needs a path"));
    assert(!Parse({"a.vrm", "-o", "x.usda", "-o", "y.usda"}, &options, &error));
    assert(Parse({"a.vrm", "-o", "x.usda", "--flatten"}, &options, &error) && options.flatten);
    assert(Parse({"a.vrm", "-o", "x.usda"}, &options, &error) && !options.flatten);
    assert(Parse({"a.vrm", "-o", "x.usda", "--dependency-report"}, &options, &error) &&
           options.dependencyReport);
    assert(Parse({"a.vrm", "-o", "x.usda"}, &options, &error) && !options.dependencyReport);
    assert(!Parse({"a.vrm", "-o", "x.usda", "--motion"}, &options, &error) &&
           Contains(error, "--motion"));
}

void
TestDependencyReport()
{
    ExportResult result;
    auto report = JsParseString(vrmExport::CreateDependencyReport("/private/avatar.usda", result))
                      .GetJsObject();
    assert(report.at("rootLayer").GetString() == "avatar.usda");
    assert(report.at("dependencies").GetJsArray().empty());
    assert(report.at("totalDependencyBytes").GetUInt64() == 0);
    assert(report.at("validation").GetString() == "notRequested");

    // Distinct sources sharing a file count once. Sorting, JSON escaping and
    // sizes beyond 32 bits must survive serialization, without source paths.
    const std::size_t large = static_cast<std::size_t>(1ull << 33);
    result.assets = {{"/private/b.png", "./textures/b.png", large},
                     {"/private/a.png", "./textures/a.png", 0},
                     {"/private/duplicate.png", "./textures/b.png", large}};
    result.checks = {"passed"};
    const std::string output = "/private/avatar-\"quoted\"-\n.usda";
    const std::string text = vrmExport::CreateDependencyReport(output, result);
    assert(!Contains(text, "/private"));
    report = JsParseString(text).GetJsObject();
    assert(report.at("rootLayer").GetString() == "avatar-\"quoted\"-\n.usda");
    assert(report.at("totalDependencyBytes").GetUInt64() == large);
    assert(report.at("validation").GetString() == "passed");
    const JsArray& dependencies = report.at("dependencies").GetJsArray();
    assert(dependencies.size() == 2);
    assert(dependencies[0].GetJsObject().at("path").GetString() == "./textures/a.png");
    assert(dependencies[1].GetJsObject().at("sizeBytes").GetUInt64() == large);

    report =
        JsParseString(vrmExport::CreateDependencyReport("/private/A.USDZ", result)).GetJsObject();
    assert(report.at("rootLayer").GetString() == "defaultLayer.usdc");
    assert(report.at("format").GetString() == "usdz");
    result.status = Status::ExportFailure;
    assert(vrmExport::CreateDependencyReport(output, result).empty());
}

// Everything refused before a stage is opened, in the order the export
// checks it, so each case isolates one refusal.
void
TestRefusals(const std::string& scratch, const std::string& vrmFixture)
{
    const ExportOptions options;
    const std::string output = TfStringCatPaths(scratch, "out.usda");

    ExportResult result =
        vrmExport::ExportVrm("a.vrm", TfStringCatPaths(scratch, "out.obj"), options);
    assert(result.status == Status::InvalidArguments);
    assert(Contains(result.message, ".usda, .usdc or .usdz"));

    result = vrmExport::ExportVrm("a.vrm", "line\nbreak\r\t.obj", options);
    assert(result.message.find_first_of("\r\n\t") == std::string::npos);
    assert(Contains(result.message, "line\\nbreak\\r\\t.obj"));

    result = vrmExport::ExportVrm("a.usda", output, options);
    assert(result.status == Status::InvalidArguments);
    assert(Contains(result.message, ".vrm"));

    result = vrmExport::ExportVrm("a.vrm", TfStringCatPaths(scratch, "dir.usda"), options);
    assert(result.status == Status::InvalidArguments);
    assert(Contains(result.message, "directory"));

    const std::string existing = TfStringCatPaths(scratch, "existing.usdz");
    WriteFile(existing, "not a package");
    result = vrmExport::ExportVrm("a.vrm", existing, options);
    assert(result.status == Status::InvalidArguments);
    assert(Contains(result.message, "--overwrite"));

    result = vrmExport::ExportVrm(TfStringCatPaths(scratch, "missing.vrm"), output, options);
    assert(result.status == Status::InputOpenFailure);
    assert(Contains(result.message, "no .vrm file"));
    assert(!TfPathExists(output));

    // A real `.vrm`, in a process with no VRM plugin: the refusal names the
    // bundle to register rather than failing somewhere inside OpenUSD.
    if (!vrmFixture.empty())
    {
        result = vrmExport::ExportVrm(vrmFixture, output, options);
        assert(result.status == Status::InputOpenFailure);
        assert(Contains(result.message, "usdVrmFileFormat"));
        assert(!TfPathExists(output));
    }
}

// `check` on outputs written by hand: one that holds, and one for each way
// an output can fail it.
void
TestCheckOutput(const std::string& scratch)
{
    WriteFile(TfStringCatPaths(scratch, "tex.png"), "not really a png");
    const std::string absoluteTexture =
        TfStringReplace(TfAbsPath(TfStringCatPaths(scratch, "tex.png")), "\\", "/");

    const std::string good = TfStringCatPaths(scratch, "good.usda");
    WriteFile(good, "#usda 1.0\n(\n    defaultPrim = \"A\"\n)\n\n"
                    "def \"A\"\n{\n    asset inputs:file = @./tex.png@\n}\n");
    ExportResult result = vrmExport::CheckOutput(good);
    assert(result.status == Status::Success);
    assert(!result.checks.empty());

    const std::string absolute = TfStringCatPaths(scratch, "absolute.usda");
    WriteFile(absolute, "#usda 1.0\n(\n    defaultPrim = \"A\"\n)\n\n"
                        "def \"A\"\n{\n    asset inputs:file = @" +
                            absoluteTexture + "@\n}\n");
    result = vrmExport::CheckOutput(absolute);
    assert(result.status == Status::ValidationFailure);
    assert(Contains(result.message, "still names"));
    assert(Contains(result.message, "/A.inputs:file"));
    assert(Contains(result.message, "re-export"));

    // A time-sampled array also needs the offending property in its message.
    const std::string timed = TfStringCatPaths(scratch, "timed.usda");
    WriteFile(timed, "#usda 1.0\n(\n    defaultPrim = \"A\"\n)\n\n"
                     "def \"A\"\n{\n    asset[] inputs:files.timeSamples = {\n"
                     "        1: [@" +
                         absoluteTexture + "@],\n    }\n}\n");
    result = vrmExport::CheckOutput(timed);
    assert(result.status == Status::ValidationFailure);
    assert(Contains(result.message, "/A.inputs:files"));

    const std::string missing = TfStringCatPaths(scratch, "missing_dependency.usda");
    WriteFile(missing, "#usda 1.0\n(\n    defaultPrim = \"A\"\n)\n\n"
                       "def \"A\"\n{\n    asset[] inputs:files = "
                       "[@./nothing.png@, @./also_missing.png@]\n}\n");
    result = vrmExport::CheckOutput(missing);
    assert(result.status == Status::ValidationFailure);
    assert(Contains(result.message, "do not resolve"));
    assert(Contains(result.message, "nothing.png"));
    assert(Contains(result.message, "also_missing.png"));
    assert(Contains(result.message, "restore the dependencies"));

    const std::string noDefault = TfStringCatPaths(scratch, "no_default.usda");
    WriteFile(noDefault, "#usda 1.0\n\ndef \"A\"\n{\n}\n");
    result = vrmExport::CheckOutput(noDefault);
    assert(result.status == Status::ValidationFailure);
    assert(Contains(result.message, "defaultPrim"));

    result = vrmExport::CheckOutput(TfStringCatPaths(scratch, "absent.usda"));
    assert(result.status == Status::ValidationFailure);
}

} // namespace

int
main(int argc, char** argv)
{
    // An optional `.vrm` fixture, for the missing-plugin refusal.
    const std::string vrmFixture = argc > 1 ? argv[1] : "";

    const std::string scratch = ArchMakeTmpSubdir(ArchGetTmpDir(), "vrm_export_tests_");
    assert(!scratch.empty());
    TfMakeDirs(TfStringCatPaths(scratch, "dir.usda"), -1, true);

    TestDetectOutputFormat();
    TestHashBytes();
    TestParseOptions();
    TestDependencyReport();
    TestRefusals(scratch, vrmFixture);
    TestCheckOutput(scratch);

    TfRmTree(scratch, [](const std::string&, const std::string&) {});
    std::cout << "vrm_export operation tests passed\n";
    return 0;
}
