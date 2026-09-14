// SPDX-License-Identifier: Apache-2.0
#include "UsdVrmaFileFormat.h"

#include "io/CgltfVrmaDocumentReader.h"
#include "model/VrmaCanonicalDocument.h"
#include "usd/UsdVrmaAuthorer.h"

#include <vrmContainer/GlbContainer.h>

#include "pxr/base/tf/diagnostic.h"
#include "pxr/base/tf/registryManager.h"
#include "pxr/base/tf/type.h"
#include "pxr/usd/ar/asset.h"
#include "pxr/usd/ar/resolvedPath.h"
#include "pxr/usd/ar/resolver.h"
#include "pxr/usd/sdf/layer.h"

#include <cstddef>
#include <future>
#include <memory>
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PUBLIC_TOKENS(UsdVrmaFileFormatTokens, USDVRMA_FILE_FORMAT_TOKENS);

TF_REGISTRY_FUNCTION(TfType)
{
    SDF_DEFINE_FILE_FORMAT(UsdVrmaFileFormat, SdfFileFormat);
}

UsdVrmaFileFormat::UsdVrmaFileFormat()
    : SdfFileFormat(UsdVrmaFileFormatTokens->Id,
                    UsdVrmaFileFormatTokens->Version,
                    UsdVrmaFileFormatTokens->Target,
                    UsdVrmaFileFormatTokens->Extension)
{
}

UsdVrmaFileFormat::~UsdVrmaFileFormat() = default;

bool
UsdVrmaFileFormat::CanRead(const std::string& file) const
{
    if (SdfFileFormat::GetFileExtension(file) != "vrma") return false;
    // Through Ar, the way OpenUSD's own formats read: the path is UTF-8 on
    // every platform, and a narrow std::ifstream on Windows reads it in the
    // host process's code page, so a clip under a non-ASCII directory never
    // opened.
    const std::shared_ptr<ArAsset> asset =
        ArGetResolver().OpenAsset(ArResolvedPath(file));
    if (!asset) return false;
    std::byte magic[4] = {};
    return asset->Read(magic, sizeof(magic), 0) == sizeof(magic) &&
        vrmContainer::HasGlbMagic({magic, sizeof(magic)});
}

bool
UsdVrmaFileFormat::Read(SdfLayer* layer, const std::string& resolvedPath,
                        bool metadataOnly) const
{
    (void)metadataOnly;
    const std::shared_ptr<ArAsset> asset =
        ArGetResolver().OpenAsset(ArResolvedPath(resolvedPath));
    if (!asset) {
        TF_RUNTIME_ERROR("usdVrmaFileFormat: could not open '%s'", resolvedPath.c_str());
        return false;
    }
    const std::size_t size = asset->GetSize();
    std::vector<std::byte> bytes(size);
    if (size == 0 || asset->Read(bytes.data(), size, 0) != size) {
        TF_RUNTIME_ERROR("usdVrmaFileFormat: could not read '%s'", resolvedPath.c_str());
        return false;
    }

    VrmaCanonicalDocument document;
    std::string error;
    CgltfVrmaDocumentReader reader;
    if (!reader.Read(resolvedPath, bytes, &document, &error)) {
        TF_RUNTIME_ERROR("usdVrmaFileFormat: %s", error.c_str());
        return false;
    }
    for (const std::string& warning : document.warnings) {
        TF_WARN("usdVrmaFileFormat: %s", warning.c_str());
    }

    std::string usda;
    UsdVrmaAuthorer authorer;
    auto task = std::async(std::launch::async, [&]() {
        return authorer.WriteToString(document, &usda);
    });
    if (!task.get()) {
        TF_RUNTIME_ERROR("usdVrmaFileFormat: failed to author USD for '%s'",
                         resolvedPath.c_str());
        return false;
    }

    const SdfFileFormatConstPtr usdaFormat = SdfFileFormat::FindByExtension("usda");
    const SdfLayerRefPtr generated = SdfLayer::CreateAnonymous(
        "usdVrmaFileFormat.generated.usda", usdaFormat);
    if (!generated || !generated->ImportFromString(usda)) {
        TF_RUNTIME_ERROR("usdVrmaFileFormat: generated USD for '%s' could not be parsed",
                         resolvedPath.c_str());
        return false;
    }
    layer->TransferContent(generated);
    return true;
}

bool
UsdVrmaFileFormat::WriteToString(const SdfLayer& layer, std::string* string,
                                 const std::string& comment) const
{
    const SdfFileFormatConstPtr usda = SdfFileFormat::FindByExtension("usda");
    return usda ? usda->WriteToString(layer, string, comment) : layer.ExportToString(string);
}

PXR_NAMESPACE_CLOSE_SCOPE
