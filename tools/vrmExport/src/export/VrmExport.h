// SPDX-License-Identifier: Apache-2.0
//
// vrm_export's export operation: an imported `.vrm` written as a native
// `.usda`, `.usdc` or `.usdz` that opens with no VRM plugin installed
// (docs/design/VRM_EXPORT_POLICY.md).
//
// It is a target of its own inside the tool, apart from the command line, and
// not a library identity: the CLI is its only consumer (export policy §4). It
// reaches a `.vrm` the way any OpenUSD client does -- through the plugin
// registry -- and links no VRM identity, so a change to what the importer
// authors is carried through here as data.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace vrmExport
{

// What an export ends with, and what the process exits with (policy §5.3).
enum class Status : int
{
    Success = 0,
    UnexpectedFailure = 1,
    // An unsupported extension, an input that is not a `.vrm`, an existing
    // output without `overwrite`. Fixed by changing the arguments.
    InvalidArguments = 2,
    // The input could not be opened as a stage, including no `.vrm` file
    // format being registered. Fixed in the input or the plugin path.
    InputOpenFailure = 3,
    // A dependency could not be read, or a layer could not be written.
    ExportFailure = 4,
    // `.usdz` packaging failed.
    PackagingFailure = 5,
    // `check` found the written output invalid.
    ValidationFailure = 6,
};

enum class OutputFormat
{
    Unknown,
    Usda,
    Usdc,
    Usdz,
};

// The output format named by a path's extension, case-insensitively. There is
// no other way to choose one (policy §5.1).
OutputFormat DetectOutputFormat(const std::string& outputPath);

struct ExportOptions
{
    // Reopen and validate the written output (policy §8).
    bool check = false;
    // Replace an existing output file. Without it, one is refused.
    bool overwrite = false;
};

// One dependency copied into the output: where it was read from, as the
// source layer authored it, and the path the output references it by.
struct LocalizedAsset
{
    std::string sourcePath;
    std::string outputPath;
};

struct ExportResult
{
    Status status = Status::Success;
    // Why, when `status` is not Success. One line.
    std::string message;
    // Every dependency localized, in the order the layer was visited;
    // several source paths may share one output path.
    std::vector<LocalizedAsset> assets;
    // What `check` verified, one line each, when it ran and passed.
    std::vector<std::string> checks;
};

// Exports `inputPath` (a `.vrm`) to `outputPath`, in the format its extension
// names. Every failure is reported in the result; nothing throws.
ExportResult ExportVrm(const std::string& inputPath, const std::string& outputPath,
                       const ExportOptions& options);

// The validation `check` runs, on an output already written. Exposed so a
// test can run it on an output it corrupted.
ExportResult CheckOutput(const std::string& outputPath);

// The name a localized file is given (policy §6.2): FNV-1a 64-bit over
// `size` bytes, with vrmContainer's offset basis -- 1469598103934665603, which
// is the published basis 14695981039346656037 with its last digit missing.
// Kept so an embedded image keeps the name the importer gave it inside the
// `.vrm`; the resolver's package paths are frozen on that constant.
unsigned long long HashBytes(const char* data, std::size_t size);

} // namespace vrmExport
