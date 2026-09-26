// SPDX-License-Identifier: Apache-2.0
//
// The export operation (docs/design/VRM_EXPORT_POLICY.md §6-§8).
//
// Three stages, in order: materialize the imported stage's root layer into a
// new layer (§6.1), localize every asset path it holds into `textures/`
// (§6.2), and write the result -- as a loose layer, or packaged into a
// `.usdz` by OpenUSD's own packaging (§6.3). `check` then reopens what was
// written (§8).
#include "VrmExport.h"

#include "pxr/base/arch/fileSystem.h"
#include "pxr/base/tf/diagnosticMgr.h"
#include "pxr/base/tf/errorMark.h"
#include "pxr/base/tf/fileUtils.h"
#include "pxr/base/tf/pathUtils.h"
#include "pxr/base/tf/stringUtils.h"
#include "pxr/base/tf/token.h"
#include "pxr/usd/ar/asset.h"
#include "pxr/usd/ar/packageUtils.h"
#include "pxr/usd/ar/resolver.h"
#include "pxr/usd/ar/resolverContextBinder.h"
#include "pxr/usd/ar/writableAsset.h"
#include "pxr/usd/sdf/assetPath.h"
#include "pxr/usd/sdf/attributeSpec.h"
#include "pxr/usd/sdf/fileFormat.h"
#include "pxr/usd/sdf/layer.h"
#include "pxr/usd/sdf/layerUtils.h"
#include "pxr/usd/usd/prim.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usdUtils/dependencies.h"
#include "pxr/usd/usdUtils/usdzPackage.h"
#include "pxr/usdValidation/usdValidation/context.h"
#include "pxr/usdValidation/usdValidation/error.h"
#include "pxr/usdValidation/usdValidation/registry.h"

#include <cstdio>
#include <map>
#include <memory>
#include <set>
#include <utility>

PXR_NAMESPACE_USING_DIRECTIVE

namespace vrmExport
{
namespace
{

// Where localized dependencies go, relative to the written root layer. The
// leading `./` is load-bearing: `textures/x.png` is a search path to USD, and
// OpenUSD's packaging files each one under `0/` instead (policy §2).
constexpr const char* kTexturesDir = "textures";

// The root layer's name inside a `.usdz`, whatever the output is called. ASCII
// on purpose: SdfZipFileWriter writes an entry name's UTF-8 bytes without the
// ZIP UTF-8 flag, so any other ZIP reader decodes a non-ASCII name as CP437
// (policy §6.3). Fixed, so a package's contents do not depend on its file name.
constexpr const char* kPackageRootLayer = "defaultLayer.usdc";

ExportResult
Fail(Status status, std::string message)
{
    ExportResult result;
    result.status = status;
    result.message = std::move(message);
    return result;
}

// The errors OpenUSD raised since `mark`, as one line, and cleared so they do
// not reach the diagnostic manager's own report a second time.
std::string
TakeErrors(TfErrorMark* mark)
{
    std::string text;
    for (auto it = mark->GetBegin(); it != TfDiagnosticMgr::GetInstance().GetErrorEnd(); ++it)
    {
        if (!text.empty())
        {
            text += "; ";
        }
        text += it->GetCommentary();
    }
    mark->Clear();
    return text;
}

std::string
LowerExtension(const std::string& path)
{
    return TfStringToLower(TfGetExtension(path));
}

// The innermost path a package-relative path names: `images/x.png` for
// `avatar.vrm[images/x.png]`, the path itself otherwise.
std::string
InnermostPath(const std::string& path)
{
    std::string inner = path;
    while (ArIsPackageRelativePath(inner))
    {
        inner = ArSplitPackageRelativePathInner(inner).second;
    }
    return inner;
}

// The package a path points into, when it points into a `.vrm`: the one case
// where an unreadable dependency means a missing plugin rather than a missing
// file.
bool
PointsIntoVrm(const std::string& path)
{
    if (!ArIsPackageRelativePath(path))
    {
        return false;
    }
    const std::string outer = ArSplitPackageRelativePathOuter(path).first;
    return LowerExtension(outer) == "vrm";
}

std::string
HexName(unsigned long long hash)
{
    char text[17];
    std::snprintf(text, sizeof(text), "%016llx", hash);
    return text;
}

bool
WriteBytes(const std::string& path, const char* data, std::size_t size, std::string* error)
{
    std::shared_ptr<ArWritableAsset> asset =
        ArGetResolver().OpenAssetForWrite(ArResolvedPath(path), ArResolver::WriteMode::Replace);
    if (!asset)
    {
        *error = "cannot open " + path + " for writing";
        return false;
    }
    if (size != 0 && asset->Write(data, size, 0) != size)
    {
        *error = "cannot write " + path;
        return false;
    }
    if (!asset->Close())
    {
        *error = "cannot finish writing " + path;
        return false;
    }
    return true;
}

// Copies every dependency of `layer` into `rootDir`/textures and points the
// layer at the copies. `source` anchors a relative path the way the layer it
// came from would have. Returns false with `result` filled on the first
// dependency that cannot be read or written: an output that silently lost a
// texture is what this tool exists to prevent (policy §6.2).
bool
LocalizeDependencies(const SdfLayerHandle& layer, const SdfLayerHandle& source,
                     const std::string& rootDir, ExportResult* result)
{
    ArResolver& resolver = ArGetResolver();
    const std::string texturesDir = TfStringCatPaths(rootDir, kTexturesDir);

    std::map<std::string, std::string> localized;
    std::set<std::string> written;
    bool failed = false;

    UsdUtilsModifyAssetPaths(layer, [&](const std::string& assetPath) -> std::string {
        if (failed || assetPath.empty())
        {
            return assetPath;
        }
        const std::string anchored = SdfComputeAssetPathRelativeToLayer(source, assetPath);
        const auto known = localized.find(anchored);
        if (known != localized.end())
        {
            return known->second;
        }

        // Step 1's input is a `.vrm`, whose layer has no composition arcs
        // (policy §2). A layer dependency would need its own localization, so
        // it is refused rather than copied as if it were an image.
        const std::string extension = LowerExtension(InnermostPath(anchored));
        if (!extension.empty() && SdfFileFormat::FindByExtension(extension))
        {
            *result = Fail(Status::ExportFailure,
                           "the layer depends on another layer (" + assetPath +
                               "), which this export does not localize");
            failed = true;
            return assetPath;
        }

        const ArResolvedPath resolved = resolver.Resolve(anchored);
        std::shared_ptr<ArAsset> asset = resolved ? resolver.OpenAsset(resolved) : nullptr;
        std::shared_ptr<const char> bytes = asset ? asset->GetBuffer() : nullptr;
        if (!bytes && !(asset && asset->GetSize() == 0))
        {
            std::string message = "cannot read the dependency " + assetPath;
            if (PointsIntoVrm(anchored))
            {
                message += "; a texture inside a .vrm needs usdVrmPackageResolver registered";
            }
            *result = Fail(Status::ExportFailure, message);
            failed = true;
            return assetPath;
        }

        const std::size_t size = asset->GetSize();
        std::string name = HexName(HashBytes(bytes.get(), size));
        if (!extension.empty())
        {
            name += "." + extension;
        }
        const std::string outputPath = std::string("./") + kTexturesDir + "/" + name;

        if (written.insert(name).second)
        {
            std::string error;
            if (!TfIsDir(texturesDir) && !TfMakeDirs(texturesDir, -1, /* existOk = */ true))
            {
                *result = Fail(Status::ExportFailure, "cannot create " + texturesDir);
                failed = true;
                return assetPath;
            }
            if (!WriteBytes(TfStringCatPaths(texturesDir, name), bytes.get(), size, &error))
            {
                *result = Fail(Status::ExportFailure, error);
                failed = true;
                return assetPath;
            }
        }
        localized.emplace(anchored, outputPath);
        result->assets.push_back({assetPath, outputPath});
        return outputPath;
    });
    return !failed;
}

// Removes a temporary directory when the export leaves, whichever way.
class TemporaryDirectory
{
  public:
    TemporaryDirectory() : _path(ArchMakeTmpSubdir(ArchGetTmpDir(), "vrm_export_"))
    {
    }
    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;
    ~TemporaryDirectory()
    {
        if (!_path.empty())
        {
            TfRmTree(_path, [](const std::string&, const std::string&) {});
        }
    }
    const std::string& path() const
    {
        return _path;
    }

  private:
    std::string _path;
};

// Every asset path authored in `layer`, as authored: attribute defaults and
// time samples, single values and arrays.
std::vector<std::string>
AuthoredAssetPaths(const SdfLayerHandle& layer)
{
    std::vector<std::string> paths;
    auto collect = [&paths](const VtValue& value) {
        if (value.IsHolding<SdfAssetPath>())
        {
            paths.push_back(value.UncheckedGet<SdfAssetPath>().GetAuthoredPath());
        }
        else if (value.IsHolding<VtArray<SdfAssetPath>>())
        {
            for (const SdfAssetPath& path : value.UncheckedGet<VtArray<SdfAssetPath>>())
            {
                paths.push_back(path.GetAuthoredPath());
            }
        }
    };
    layer->Traverse(SdfPath::AbsoluteRootPath(), [&](const SdfPath& path) {
        if (!path.IsPropertyPath())
        {
            return;
        }
        const SdfAttributeSpecHandle attribute = layer->GetAttributeAtPath(path);
        if (!attribute)
        {
            return;
        }
        collect(attribute->GetDefaultValue());
        for (const double time : layer->ListTimeSamplesForPath(path))
        {
            VtValue sample;
            if (layer->QueryTimeSample(path, time, &sample))
            {
                collect(sample);
            }
        }
    });
    return paths;
}

// The validators `check` runs on a `.usdz` (policy §8).
const TfTokenVector&
UsdzValidatorNames()
{
    static const TfTokenVector names = {
        TfToken("usdUtilsValidators:PackageEncapsulationValidator"),
        TfToken("usdUtilsValidators:MissingReferenceValidator"),
        TfToken("usdUtilsValidators:RootPackageValidator"),
        TfToken("usdUtilsValidators:UsdzPackageValidator"),
        TfToken("usdUtilsValidators:FileExtensionValidator"),
    };
    return names;
}

} // namespace

unsigned long long
HashBytes(const char* data, std::size_t size)
{
    // vrmContainer::HashBytes' basis, not FNV-1a's published one (VrmExport.h).
    unsigned long long hash = 1469598103934665603ull;
    const auto* bytes = reinterpret_cast<const unsigned char*>(data);
    for (std::size_t i = 0; i < size; ++i)
    {
        hash ^= bytes[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

OutputFormat
DetectOutputFormat(const std::string& outputPath)
{
    const std::string extension = LowerExtension(outputPath);
    if (extension == "usda")
    {
        return OutputFormat::Usda;
    }
    if (extension == "usdc")
    {
        return OutputFormat::Usdc;
    }
    if (extension == "usdz")
    {
        return OutputFormat::Usdz;
    }
    return OutputFormat::Unknown;
}

ExportResult
ExportVrm(const std::string& inputPath, const std::string& outputPath,
          const ExportOptions& options)
{
    // The arguments first, and nothing written until all of them hold.
    const OutputFormat format = DetectOutputFormat(outputPath);
    if (format == OutputFormat::Unknown)
    {
        return Fail(Status::InvalidArguments,
                    "cannot tell the output format of " + outputPath +
                        " (the extension must be .usda, .usdc or .usdz)");
    }
    if (LowerExtension(inputPath) != "vrm")
    {
        return Fail(Status::InvalidArguments, "the input must be a .vrm file: " + inputPath);
    }
    if (TfIsDir(outputPath, /* resolveSymlinks = */ true))
    {
        return Fail(Status::InvalidArguments, outputPath + " is a directory");
    }
    if (TfPathExists(outputPath, /* resolveSymlinks = */ true) && !options.overwrite)
    {
        return Fail(Status::InvalidArguments,
                    outputPath + " already exists (pass --overwrite to replace it)");
    }
    if (!TfIsFile(inputPath, /* resolveSymlinks = */ true))
    {
        return Fail(Status::InputOpenFailure, "no .vrm file at " + inputPath);
    }
    if (!SdfFileFormat::FindByExtension("vrm"))
    {
        return Fail(Status::InputOpenFailure,
                    "OpenUSD has no file format for .vrm files; a .vrm needs "
                    "usdVrmFileFormat registered");
    }

    TfErrorMark mark;
    UsdStageRefPtr stage = UsdStage::Open(inputPath);
    if (!stage)
    {
        const std::string errors = TakeErrors(&mark);
        return Fail(Status::InputOpenFailure, "OpenUSD could not open " + inputPath +
                                                  (errors.empty() ? "" : ": " + errors));
    }

    // Materialize (policy §6.1): the root layer's content into a new layer, so
    // the source layer is never edited. The session layer is not the asset.
    const SdfLayerHandle source = stage->GetRootLayer();
    SdfLayerRefPtr layer = SdfLayer::CreateAnonymous("vrm_export.usda");
    if (!layer)
    {
        return Fail(Status::ExportFailure, "cannot create a layer to export into");
    }
    layer->TransferContent(source);

    ExportResult result;

    // A loose layer's textures go next to it, a package's into the directory
    // it is assembled in before OpenUSD packages it (policy §6.2, §6.3).
    std::unique_ptr<TemporaryDirectory> staging;
    std::string rootDir;
    std::string layerPath;
    if (format == OutputFormat::Usdz)
    {
        staging = std::make_unique<TemporaryDirectory>();
        if (staging->path().empty())
        {
            return Fail(Status::ExportFailure, "cannot create a temporary directory");
        }
        rootDir = staging->path();
        layerPath = TfStringCatPaths(rootDir, kPackageRootLayer);
    }
    else
    {
        rootDir = TfGetPathName(TfAbsPath(outputPath));
        layerPath = outputPath;
    }
    // The output's directory, whichever format: a package is assembled
    // elsewhere and still written here.
    const std::string outputDir = TfGetPathName(TfAbsPath(outputPath));
    for (const std::string& dir : {rootDir, outputDir})
    {
        if (!dir.empty() && !TfIsDir(dir) && !TfMakeDirs(dir, -1, /* existOk = */ true))
        {
            return Fail(Status::ExportFailure, "cannot create " + dir);
        }
    }

    if (!LocalizeDependencies(layer, source, rootDir, &result))
    {
        return result;
    }

    if (!layer->Export(layerPath))
    {
        const std::string errors = TakeErrors(&mark);
        return Fail(Status::ExportFailure,
                    "cannot write " + layerPath + (errors.empty() ? "" : ": " + errors));
    }

    if (format == OutputFormat::Usdz)
    {
        const ArResolverContextBinder binder(
            ArGetResolver().CreateDefaultContextForAsset(layerPath));
        if (!UsdUtilsCreateNewUsdzPackage(SdfAssetPath(layerPath), outputPath))
        {
            const std::string errors = TakeErrors(&mark);
            return Fail(Status::PackagingFailure,
                        "cannot package " + outputPath + (errors.empty() ? "" : ": " + errors));
        }
    }

    if (options.check)
    {
        ExportResult checked = CheckOutput(outputPath);
        if (checked.status != Status::Success)
        {
            checked.assets = std::move(result.assets);
            return checked;
        }
        result.checks = std::move(checked.checks);
    }
    return result;
}

ExportResult
CheckOutput(const std::string& outputPath)
{
    // A fresh parse: nothing in this process opened `outputPath` before, so the
    // layer registry has no in-memory copy to hand back in its place.
    TfErrorMark mark;
    UsdStageRefPtr stage = UsdStage::Open(outputPath);
    if (!stage)
    {
        const std::string errors = TakeErrors(&mark);
        return Fail(Status::ValidationFailure, "check: cannot reopen " + outputPath +
                                                   (errors.empty() ? "" : ": " + errors));
    }
    ExportResult result;
    const UsdPrim defaultPrim = stage->GetDefaultPrim();
    if (!defaultPrim)
    {
        return Fail(Status::ValidationFailure, "check: " + outputPath + " has no defaultPrim");
    }
    result.checks.push_back("reopened, defaultPrim " + defaultPrim.GetPath().GetString());

    std::vector<SdfLayerRefPtr> layers;
    std::vector<std::string> assets;
    std::vector<std::string> unresolved;
    {
        const ArResolverContextBinder binder(
            ArGetResolver().CreateDefaultContextForAsset(outputPath));
        UsdUtilsComputeAllDependencies(SdfAssetPath(outputPath), &layers, &assets, &unresolved);
    }
    if (!unresolved.empty())
    {
        return Fail(Status::ValidationFailure,
                    "check: " + std::to_string(unresolved.size()) +
                        " dependency(ies) do not resolve, the first " + unresolved.front());
    }
    result.checks.push_back(std::to_string(layers.size()) + " layer(s) and " +
                            std::to_string(assets.size()) + " asset(s), all resolved");

    std::size_t authored = 0;
    for (const SdfLayerRefPtr& layer : layers)
    {
        for (const std::string& path : AuthoredAssetPaths(layer))
        {
            if (path.empty())
            {
                continue;
            }
            ++authored;
            const std::string outer =
                ArIsPackageRelativePath(path) ? ArSplitPackageRelativePathOuter(path).first : path;
            if (!TfIsRelativePath(outer) || PointsIntoVrm(path))
            {
                return Fail(Status::ValidationFailure,
                            "check: " + layer->GetIdentifier() + " still names " + path);
            }
        }
    }
    result.checks.push_back(std::to_string(authored) +
                            " authored asset path(s), none absolute or into a .vrm");

    if (DetectOutputFormat(outputPath) == OutputFormat::Usdz)
    {
        const TfTokenVector& names = UsdzValidatorNames();
        const std::vector<const UsdValidationValidator*> validators =
            UsdValidationRegistry::GetInstance().GetOrLoadValidatorsByName(names);
        if (validators.size() != names.size())
        {
            return Fail(Status::ValidationFailure,
                        "check: OpenUSD's usdUtilsValidators are not all registered (" +
                            std::to_string(validators.size()) + " of " +
                            std::to_string(names.size()) + " found)");
        }
        const UsdValidationContext context(validators);
        std::size_t warnings = 0;
        for (const UsdValidationError& error : context.Validate(stage))
        {
            if (error.GetType() == UsdValidationErrorType::Error)
            {
                return Fail(Status::ValidationFailure, "check: " + error.GetErrorAsString());
            }
            if (error.GetType() == UsdValidationErrorType::Warn)
            {
                ++warnings;
                result.checks.push_back("warning: " + error.GetErrorAsString());
            }
        }
        result.checks.push_back(std::to_string(validators.size()) +
                                " usdUtilsValidators, no error" +
                                (warnings ? ", " + std::to_string(warnings) + " warning(s)" : ""));
    }
    return result;
}

} // namespace vrmExport
