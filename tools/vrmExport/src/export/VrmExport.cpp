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
#include "MotionBake.h"

#include "pxr/base/arch/fileSystem.h"
#include "pxr/base/js/json.h"
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
#include "pxr/usd/sdf/primSpec.h"
#include "pxr/usd/sdf/reference.h"
#include "pxr/usd/usd/prim.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usdUtils/dependencies.h"
#include "pxr/usd/usdUtils/usdzPackage.h"
#include "pxr/usdValidation/usdValidation/context.h"
#include "pxr/usdValidation/usdValidation/error.h"
#include "pxr/usdValidation/usdValidation/registry.h"

#include <cstdint>
#include <cstdio>
#include <ctime>
#include <map>
#include <memory>
#include <set>
#include <utility>

#if defined(ARCH_OS_WINDOWS)
#include <sys/utime.h>
#else
#include <utime.h>
#endif

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
    // OpenUSD commentary and file names can contain line breaks. Keep one
    // diagnostic on one line without losing the distinction in a file name.
    message = TfStringReplace(message, "\r", "\\r");
    message = TfStringReplace(message, "\n", "\\n");
    result.message = TfStringReplace(message, "\t", "\\t");
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

// Locate a failed dependency in the authored stage, including asset arrays
// and time samples. Only needed on failure; do not change the traversal that
// determines export bytes or dependency order.
std::string
DescribeAssetUse(const SdfLayerHandle& layer, const std::string& assetPath)
{
    auto contains = [&assetPath](const VtValue& value)
    {
        if (value.IsHolding<SdfAssetPath>())
        {
            return value.UncheckedGet<SdfAssetPath>().GetAuthoredPath() == assetPath;
        }
        if (value.IsHolding<VtArray<SdfAssetPath>>())
        {
            for (const SdfAssetPath& path : value.UncheckedGet<VtArray<SdfAssetPath>>())
            {
                if (path.GetAuthoredPath() == assetPath)
                {
                    return true;
                }
            }
        }
        return false;
    };
    std::set<std::string> properties;
    layer->Traverse(SdfPath::AbsoluteRootPath(),
                    [&](const SdfPath& path)
                    {
                        if (!path.IsPropertyPath())
                        {
                            return;
                        }
                        const SdfAttributeSpecHandle attribute = layer->GetAttributeAtPath(path);
                        if (!attribute)
                        {
                            return;
                        }
                        if (contains(attribute->GetDefaultValue()))
                        {
                            properties.insert(path.GetString());
                        }
                        for (double time : layer->ListTimeSamplesForPath(path))
                        {
                            VtValue sample;
                            if (layer->QueryTimeSample(path, time, &sample) && contains(sample))
                            {
                                properties.insert(path.GetString());
                            }
                        }
                    });
    return properties.empty() ? "" : " (referenced by " + TfStringJoin(properties, ", ") + ")";
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

    UsdUtilsModifyAssetPaths(
        layer,
        [&](const std::string& assetPath) -> std::string
        {
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
                *result =
                    Fail(Status::ExportFailure, "the layer depends on another layer (" + assetPath +
                                                    "), which this export does not localize" +
                                                    DescribeAssetUse(layer, assetPath));
                failed = true;
                return assetPath;
            }

            const ArResolvedPath resolved = resolver.Resolve(anchored);
            std::shared_ptr<ArAsset> asset = resolved ? resolver.OpenAsset(resolved) : nullptr;
            std::shared_ptr<const char> bytes = asset ? asset->GetBuffer() : nullptr;
            if (!bytes && !(asset && asset->GetSize() == 0))
            {
                std::string message = "cannot read the dependency '" + assetPath + "'" +
                                      DescribeAssetUse(layer, assetPath) + "; anchored path '" +
                                      anchored + "'";
                if (PointsIntoVrm(anchored))
                {
                    message +=
                        "; a texture inside a .vrm needs usdVrmPackageResolver registered "
                        "on PXR_PLUGINPATH_NAME; if it is registered, check the embedded image";
                }
                else
                {
                    message += "; check that the external image exists and is readable relative "
                               "to the input .vrm";
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
                    *result = Fail(
                        Status::ExportFailure,
                        "cannot create texture directory '" + texturesDir + "' for dependency '" +
                            assetPath + "'" + DescribeAssetUse(layer, assetPath) +
                            "; check write permissions and whether a file blocks the directory");
                    failed = true;
                    return assetPath;
                }
                if (!WriteBytes(TfStringCatPaths(texturesDir, name), bytes.get(), size, &error))
                {
                    *result = Fail(Status::ExportFailure,
                                   error + "; localizing dependency '" + assetPath + "'" +
                                       DescribeAssetUse(layer, assetPath) +
                                       "; check destination permissions and available space");
                    failed = true;
                    return assetPath;
                }
            }
            localized.emplace(anchored, outputPath);
            result->assets.push_back({assetPath, outputPath, size});
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
    const std::string&
    path() const
    {
        return _path;
    }

  private:
    std::string _path;
};

// Gives every file under `dir` the modification time that is 2000-01-01
// 00:00:00 in this host's local time. SdfZipFileWriter stamps each entry with
// its file's modification time, converted with localtime() (policy §2), so a
// time pinned in local terms encodes the same in every time zone: a package's
// bytes then depend on its contents alone (policy §9).
bool
PinModificationTimes(const std::string& dir, std::string* error)
{
    std::tm local = {};
    local.tm_year = 2000 - 1900;
    local.tm_mon = 0;
    local.tm_mday = 1;
    local.tm_isdst = -1;
    const std::time_t pinned = std::mktime(&local);
    if (pinned == static_cast<std::time_t>(-1))
    {
        *error = "cannot compute the time a package's entries are stamped with";
        return false;
    }

    bool ok = true;
    TfWalkDirs(dir,
               [&](const std::string& dirPath, std::vector<std::string>*,
                   const std::vector<std::string>& fileNames)
               {
                   for (const std::string& name : fileNames)
                   {
                       const std::string path = TfStringCatPaths(dirPath, name);
#if defined(ARCH_OS_WINDOWS)
                       __utimbuf64 times = {pinned, pinned};
                       const bool pinnedThis =
                           _wutime64(ArchWindowsUtf8ToUtf16(path).c_str(), &times) == 0;
#else
            utimbuf times = {pinned, pinned};
            const bool pinnedThis = utime(path.c_str(), &times) == 0;
#endif
                       if (!pinnedThis)
                       {
                           *error = "cannot set the modification time of " + path;
                           ok = false;
                           return false;
                       }
                   }
                   return true;
               });
    return ok;
}

// Every asset path authored in `layer`, as authored: attribute defaults and
// time samples, single values and arrays.
std::vector<std::string>
AuthoredAssetPaths(const SdfLayerHandle& layer)
{
    std::vector<std::string> paths;
    auto collect = [&paths](const VtValue& value)
    {
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
    layer->Traverse(SdfPath::AbsoluteRootPath(),
                    [&](const SdfPath& path)
                    {
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

std::string
CreateDependencyReport(const std::string& outputPath, const ExportResult& result)
{
    if (result.status != Status::Success)
    {
        return {};
    }

    // Inventory the bytes localization wrote, rather than reading paths back
    // from a package through the source resolver. Several source images may
    // share one localized path, and traversal order is not report order.
    std::map<std::string, std::size_t> distinct;
    for (const LocalizedAsset& asset : result.assets)
    {
        distinct.emplace(asset.outputPath, asset.sizeBytes);
    }
    JsArray dependencies;
    uint64_t total = 0;
    for (const auto& entry : distinct)
    {
        const auto size = static_cast<uint64_t>(entry.second);
        dependencies.emplace_back(
            JsObject{{"path", JsValue(entry.first)}, {"sizeBytes", JsValue(size)}});
        total += size;
    }
    const std::string rootLayer = DetectOutputFormat(outputPath) == OutputFormat::Usdz
                                      ? kPackageRootLayer
                                      : TfGetBaseName(outputPath);
    return JsWriteToString(JsValue(JsObject{
        {"reportVersion", JsValue(1)},
        {"format", JsValue(LowerExtension(outputPath))},
        {"rootLayer", JsValue(rootLayer)},
        {"dependencies", JsValue(std::move(dependencies))},
        {"totalDependencyBytes", JsValue(total)},
        {"validation", JsValue(result.checks.empty() ? "notRequested" : "passed")},
    }));
}

ExportResult
ExportVrm(const std::string& inputPath, const std::string& outputPath, const ExportOptions& options)
{
    // The arguments first, and nothing written until all of them hold.
    const OutputFormat format = DetectOutputFormat(outputPath);
    if (format == OutputFormat::Unknown)
    {
        return Fail(Status::InvalidArguments, "cannot tell the output format of " + outputPath +
                                                  " (the extension must be .usda, .usdc or .usdz)");
    }
    if (LowerExtension(inputPath) != "vrm")
    {
        return Fail(Status::InvalidArguments, "the input must be a .vrm file: " + inputPath);
    }
    if (!options.motionPath.empty() && LowerExtension(options.motionPath) != "vrma")
    {
        return Fail(Status::InvalidArguments,
                    "--motion must name a .vrma file: " + options.motionPath);
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
                    "usdVrmFileFormat registered on PXR_PLUGINPATH_NAME");
    }

    TfErrorMark mark;
    UsdStageRefPtr stage = UsdStage::Open(inputPath);
    if (!stage)
    {
        const std::string errors = TakeErrors(&mark);
        return Fail(Status::InputOpenFailure,
                    "OpenUSD could not open " + inputPath +
                        "; check that it is a readable, valid VRM/GLB file" +
                        (errors.empty() ? "" : ": " + errors));
    }

    // The existing CLI owns the complete VRM bake (body, expressions, gaze).
    // Keep its binding layer separate from the avatar until materialization.
    std::unique_ptr<TemporaryDirectory> motionStaging;
    SdfLayerRefPtr bakedLayer;
    if (!options.motionPath.empty())
    {
        if (!TfIsFile(options.motionPath))
            return Fail(Status::InputOpenFailure, "no .vrma file at " + options.motionPath);
        if (!SdfFileFormat::FindByExtension("vrma"))
            return Fail(Status::InputOpenFailure,
                        "--motion needs usdVrmaFileFormat registered on PXR_PLUGINPATH_NAME");
        if (!UsdStage::Open(options.motionPath))
            return Fail(Status::InputOpenFailure,
                        "cannot open motion '" + options.motionPath + "': " + TakeErrors(&mark));
        motionStaging = std::make_unique<TemporaryDirectory>();
        if (motionStaging->path().empty())
            return Fail(Status::ExportFailure,
                        "cannot create a temporary directory for motion assembly");
        const std::string bakedPath = TfStringCatPaths(motionStaging->path(), "motion.usda");
        std::string error;
        if (!BakeMotion(inputPath, options.motionPath, bakedPath, &error))
            return Fail(Status::ExportFailure, error);
        bakedLayer = SdfLayer::FindOrOpen(bakedPath);
        if (!bakedLayer)
            return Fail(Status::ExportFailure,
                        "cannot read motion_retarget output: " + TakeErrors(&mark));
        if (options.flatten)
        {
            stage = UsdStage::Open(bakedLayer);
            if (!stage)
                return Fail(Status::ExportFailure,
                            "cannot compose the baked motion: " + TakeErrors(&mark));
        }
    }

    // Materialize (policy §6.1): the root layer's content into a new layer, so
    // the source layer is never edited. The session layer is not the asset.
    // Flattened only when asked, and then with no source-file comment, which
    // would name the input's absolute path (policy §7).
    const SdfLayerHandle source = stage->GetRootLayer();
    SdfLayerRefPtr layer;
    if (options.flatten)
    {
        layer = stage->Flatten(/* addSourceFileComment = */ false);
    }
    else
    {
        layer = SdfLayer::CreateAnonymous("vrm_export.usda");
        if (layer)
        {
            layer->TransferContent(source);
        }
    }
    if (!layer)
    {
        const std::string errors = TakeErrors(&mark);
        return Fail(Status::ExportFailure,
                    "cannot create a layer to export into" + (errors.empty() ? "" : ": " + errors));
    }

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
            return Fail(
                Status::ExportFailure,
                "cannot create a temporary directory for '" + outputPath +
                    "'; check the system temporary directory's permissions and available space");
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
            const std::string errors = TakeErrors(&mark);
            return Fail(Status::ExportFailure,
                        "cannot create output directory '" + dir + "' for '" + outputPath +
                            "'; check write permissions and whether a file blocks the directory" +
                            (errors.empty() ? "" : ": " + errors));
        }
    }

    if (!LocalizeDependencies(layer, source, rootDir, &result))
    {
        const std::string errors = TakeErrors(&mark);
        result = Fail(result.status, result.message + (errors.empty() ? "" : ": " + errors));
        return result;
    }

    if (bakedLayer && !options.flatten)
    {
        // A native avatar plus the bake's stronger opinions. Content-named
        // avatars let multiple loose exports share a directory safely.
        const std::string avatarPath = TfStringCatPaths(motionStaging->path(), "avatar.usdc");
        if (!layer->Export(avatarPath))
            return Fail(Status::ExportFailure,
                        "cannot materialize the motion avatar: " + TakeErrors(&mark));
        const std::shared_ptr<ArAsset> avatar =
            ArGetResolver().OpenAsset(ArResolvedPath(avatarPath));
        const std::shared_ptr<const char> bytes = avatar ? avatar->GetBuffer() : nullptr;
        if (!bytes)
            return Fail(Status::ExportFailure, "cannot read the materialized motion avatar");
        const std::size_t size = avatar->GetSize();
        const std::string name = "avatar_" + HexName(HashBytes(bytes.get(), size)) + ".usdc";
        std::string outputAbsolute = TfNormPath(TfAbsPath(outputPath));
        std::string dependencyAbsolute = TfNormPath(TfStringCatPaths(rootDir, name));
#if defined(ARCH_OS_WINDOWS)
        outputAbsolute = TfStringToLower(outputAbsolute);
        dependencyAbsolute = TfStringToLower(dependencyAbsolute);
#endif
        if (outputAbsolute == dependencyAbsolute)
            return Fail(
                Status::InvalidArguments,
                "the output names the native avatar dependency; choose another output name");
        std::string error;
        if (!WriteBytes(TfStringCatPaths(rootDir, name), bytes.get(), size, &error))
            return Fail(Status::ExportFailure, error);
        result.assets.push_back({inputPath, "./" + name, size});

        layer = SdfLayer::CreateAnonymous("vrm_motion.usda");
        layer->TransferContent(bakedLayer);
        const SdfPrimSpecHandle root =
            layer->GetPrimAtPath(SdfPath::AbsoluteRootPath().AppendChild(layer->GetDefaultPrim()));
        if (!root)
            return Fail(Status::ExportFailure, "motion_retarget output has no default prim");
        root->GetReferenceList().ClearEdits();
        root->GetReferenceList().Add(SdfReference("./" + name));
    }

    if (!layer->Export(layerPath))
    {
        const std::string errors = TakeErrors(&mark);
        return Fail(Status::ExportFailure,
                    "cannot write '" + layerPath + "' for output '" + outputPath +
                        "'; check destination permissions and available space" +
                        (errors.empty() ? "" : ": " + errors));
    }

    if (format == OutputFormat::Usdz)
    {
        std::string error;
        if (!PinModificationTimes(rootDir, &error))
        {
            return Fail(Status::PackagingFailure, error);
        }
        const ArResolverContextBinder binder(
            ArGetResolver().CreateDefaultContextForAsset(layerPath));
        if (!UsdUtilsCreateNewUsdzPackage(SdfAssetPath(layerPath), outputPath))
        {
            const std::string errors = TakeErrors(&mark);
            return Fail(Status::PackagingFailure,
                        "cannot package '" + outputPath +
                            "'; check destination permissions and available space" +
                            (errors.empty() ? "" : ": " + errors));
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
        return Fail(Status::ValidationFailure,
                    "check: cannot reopen " + outputPath + (errors.empty() ? "" : ": " + errors));
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
        return Fail(
            Status::ValidationFailure,
            "check: " + std::to_string(unresolved.size()) +
                " dependency(ies) do not resolve: " + TfStringJoin(unresolved, ", ") +
                "; restore the dependencies beside the output or re-export the source .vrm");
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
                            "check: " + layer->GetIdentifier() + " still names " + path +
                                DescribeAssetUse(layer, path) +
                                "; re-export the source .vrm to localize its dependencies");
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
                            std::to_string(names.size()) +
                            " found); use an OpenUSD runtime with usdUtilsValidators registered");
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
