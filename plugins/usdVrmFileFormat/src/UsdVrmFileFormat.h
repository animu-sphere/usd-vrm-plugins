// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "pxr/pxr.h"
#include "pxr/base/tf/staticTokens.h"
#include "pxr/usd/sdf/fileFormat.h"

PXR_NAMESPACE_OPEN_SCOPE

// The tokens that identify this file format to USD's Sdf layer registry.
#define USDVRM_FILE_FORMAT_TOKENS \
    ((Id, "vrm"))                 \
    ((Version, "1.0"))            \
    ((Target, "usd"))             \
    ((Extension, "vrm"))

TF_DECLARE_PUBLIC_TOKENS(UsdVrmFileFormatTokens, USDVRM_FILE_FORMAT_TOKENS);

/// SdfFileFormat that imports `.vrm` (VRM 0.x / 1.0) avatars as a normalized
/// USD stage. See README.md for the produced hierarchy and design rationale.
class UsdVrmFileFormat : public SdfFileFormat {
public:
    bool CanRead(const std::string& file) const override;
    bool Read(SdfLayer* layer, const std::string& resolvedPath, bool metadataOnly) const override;
    bool WriteToString(
        const SdfLayer& layer,
        std::string* str,
        const std::string& comment = std::string()) const override;

protected:
    SDF_FILE_FORMAT_FACTORY_ACCESS;

    UsdVrmFileFormat();
    ~UsdVrmFileFormat() override;
};

PXR_NAMESPACE_CLOSE_SCOPE
