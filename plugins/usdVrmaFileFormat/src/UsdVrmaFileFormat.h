// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "pxr/pxr.h"
#include "pxr/base/tf/staticTokens.h"
#include "pxr/usd/sdf/fileFormat.h"

PXR_NAMESPACE_OPEN_SCOPE

#define USDVRMA_FILE_FORMAT_TOKENS \
    ((Id, "vrma"))                  \
    ((Version, "1.0"))              \
    ((Target, "usd"))               \
    ((Extension, "vrma"))

TF_DECLARE_PUBLIC_TOKENS(UsdVrmaFileFormatTokens, USDVRMA_FILE_FORMAT_TOKENS);

/// SdfFileFormat that imports a VRM Animation (`.vrma`) GLB into an
/// avatar-independent semantic humanoid skeleton plus UsdSkelAnimation.
class UsdVrmaFileFormat : public SdfFileFormat {
public:
    bool CanRead(const std::string& file) const override;
    bool Read(SdfLayer* layer, const std::string& resolvedPath, bool metadataOnly) const override;
    bool WriteToString(
        const SdfLayer& layer,
        std::string* str,
        const std::string& comment = std::string()) const override;

protected:
    SDF_FILE_FORMAT_FACTORY_ACCESS;

    UsdVrmaFileFormat();
    ~UsdVrmaFileFormat() override;
};

PXR_NAMESPACE_CLOSE_SCOPE
