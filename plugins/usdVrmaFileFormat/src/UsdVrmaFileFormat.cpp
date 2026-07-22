// SPDX-License-Identifier: Apache-2.0
#include "UsdVrmaFileFormat.h"

#include "io/CgltfVrmaDocumentReader.h"
#include "model/VrmaCanonicalDocument.h"
#include "usd/UsdVrmaAuthorer.h"

#include <vrmContainer/GlbContainer.h>

#include "pxr/base/tf/diagnostic.h"
#include "pxr/base/tf/registryManager.h"
#include "pxr/base/tf/type.h"
#include "pxr/usd/sdf/layer.h"

#include <cstddef>
#include <fstream>
#include <future>
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
    std::ifstream input(file, std::ios::binary);
    if (!input) return false;
    std::byte magic[4] = {};
    input.read(reinterpret_cast<char*>(magic), sizeof(magic));
    return input.gcount() == sizeof(magic) &&
        vrmContainer::HasGlbMagic({magic, sizeof(magic)});
}

bool
UsdVrmaFileFormat::Read(SdfLayer* layer, const std::string& resolvedPath,
                        bool metadataOnly) const
{
    (void)metadataOnly;
    std::ifstream input(resolvedPath, std::ios::binary | std::ios::ate);
    if (!input) {
        TF_RUNTIME_ERROR("usdVrmaFileFormat: could not open '%s'", resolvedPath.c_str());
        return false;
    }
    const std::streamsize size = input.tellg();
    input.seekg(0, std::ios::beg);
    std::vector<std::byte> bytes(size > 0 ? static_cast<std::size_t>(size) : 0);
    if (size <= 0 || !input.read(reinterpret_cast<char*>(bytes.data()), size)) {
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
