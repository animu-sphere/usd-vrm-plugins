// SPDX-License-Identifier: Apache-2.0
#include "UsdVrmFileFormat.h"

#include "io/CgltfVrmDocumentReader.h"
#include "model/VrmCanonicalDocument.h"
#include "model/VrmDiagnostics.h"
#include "usd/UsdVrmAuthorer.h"

#include <vrmContainer/GlbContainer.h>

#include "pxr/base/tf/diagnostic.h"
#include "pxr/base/tf/registryManager.h"
#include "pxr/base/tf/type.h"
#include "pxr/usd/ar/asset.h"
#include "pxr/usd/ar/resolvedPath.h"
#include "pxr/usd/ar/resolver.h"
#include "pxr/usd/sdf/layer.h"

#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <future>
#include <memory>
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PUBLIC_TOKENS(UsdVrmFileFormatTokens, USDVRM_FILE_FORMAT_TOKENS);

// Register the format with USD's type system so the plug system can find it.
TF_REGISTRY_FUNCTION(TfType)
{
    SDF_DEFINE_FILE_FORMAT(UsdVrmFileFormat, SdfFileFormat);
}

UsdVrmFileFormat::UsdVrmFileFormat()
    : SdfFileFormat(
          UsdVrmFileFormatTokens->Id,
          UsdVrmFileFormatTokens->Version,
          UsdVrmFileFormatTokens->Target,
          UsdVrmFileFormatTokens->Extension)
{
}

UsdVrmFileFormat::~UsdVrmFileFormat() = default;

bool
UsdVrmFileFormat::CanRead(const std::string& file) const
{
    if (SdfFileFormat::GetFileExtension(file) != "vrm") {
        return false;
    }
    // .vrm is a GLB container; sniff the 4-byte GLB header magic "glTF".
    //
    // Through Ar, the way OpenUSD's own formats read. The path is UTF-8 on
    // every platform, and a narrow std::ifstream on Windows reads it in the
    // host process's code page instead -- so a .vrm under a non-ASCII directory
    // opened in no host at all, usdview and Python included.
    const std::shared_ptr<ArAsset> asset =
        ArGetResolver().OpenAsset(ArResolvedPath(file));
    if (!asset) {
        return false;
    }
    std::byte magic[4] = {};
    return asset->Read(magic, sizeof(magic), 0) == sizeof(magic) &&
        vrmContainer::HasGlbMagic({magic, sizeof(magic)});
}

bool
UsdVrmFileFormat::Read(
    SdfLayer* layer,
    const std::string& resolvedPath,
    bool metadataOnly) const
{
    (void)metadataOnly;

    // Slurp the whole .vrm/GLB into memory; cgltf parses from the buffer and
    // keeps the embedded bin chunk alive for the duration of Read(). Through
    // Ar for the reason CanRead gives.
    const std::shared_ptr<ArAsset> asset =
        ArGetResolver().OpenAsset(ArResolvedPath(resolvedPath));
    if (!asset) {
        TF_RUNTIME_ERROR("usdVrmFileFormat: could not open '%s'", resolvedPath.c_str());
        return false;
    }
    const size_t size = asset->GetSize();
    std::vector<std::byte> bytes(size);
    if (size > 0 && asset->Read(bytes.data(), size, 0) != size) {
        TF_RUNTIME_ERROR("usdVrmFileFormat: could not read '%s'", resolvedPath.c_str());
        return false;
    }

    VrmCanonicalDocument document;
    std::string error;
    CgltfVrmDocumentReader reader;
    if (!reader.Read(resolvedPath, bytes, &document, &error)) {
        TF_RUNTIME_ERROR("usdVrmFileFormat: %s", VrmDiagMsg(VrmDiag::ContainerUnreadable,
            "failed to read '" + resolvedPath + "': " + error).c_str());
        return false;
    }
    for (const std::string& w : document.warnings) {
        TF_WARN("usdVrmFileFormat: %s", w.c_str());
    }

    // SdfLayer::Reload reads file formats under an outer SdfChangeBlock, and a
    // UsdStage authored on the calling thread can't observe newly authored prims
    // until that block closes. Author on a separate thread so the detached stage
    // is outside the reload thread's change block.
    std::string usda;
    std::vector<std::string> writerWarnings;
    UsdVrmAuthorer authorer;
    auto task = std::async(std::launch::async,
        [&authorer, &document, &usda, &writerWarnings]() {
            return authorer.WriteToString(document, &usda, &writerWarnings);
        });
    if (!task.get()) {
        TF_RUNTIME_ERROR("usdVrmFileFormat: failed to author USD for '%s'",
            resolvedPath.c_str());
        return false;
    }
    for (const std::string& w : writerWarnings) {
        TF_WARN("usdVrmFileFormat: %s", w.c_str());
    }

    SdfFileFormatConstPtr usdaFormat = SdfFileFormat::FindByExtension("usda");
    SdfLayerRefPtr generated = SdfLayer::CreateAnonymous(
        "usdVrmFileFormat.generated.usda",
        usdaFormat ? usdaFormat : SdfFileFormatConstPtr());
    if (!generated || !generated->ImportFromString(usda)) {
        if (const char* dumpPath = std::getenv("USDVRM_DUMP_GENERATED_USDA")) {
            std::ofstream dump(dumpPath, std::ios::binary);
            dump << usda;
        }
        TF_RUNTIME_ERROR("usdVrmFileFormat: generated USD for '%s' could not be parsed",
            resolvedPath.c_str());
        return false;
    }

    layer->TransferContent(generated);
    return true;
}

bool
UsdVrmFileFormat::WriteToString(
    const SdfLayer& layer,
    std::string* str,
    const std::string& comment) const
{
    SdfFileFormatConstPtr usda = SdfFileFormat::FindByExtension("usda");
    if (usda) {
        return usda->WriteToString(layer, str, comment);
    }
    return layer.ExportToString(str);
}

PXR_NAMESPACE_CLOSE_SCOPE
