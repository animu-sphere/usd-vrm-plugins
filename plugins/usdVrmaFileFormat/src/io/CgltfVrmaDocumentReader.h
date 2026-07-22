// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "pxr/pxr.h"
#include "io/VrmaDocumentReader.h"

PXR_NAMESPACE_OPEN_SCOPE

class CgltfVrmaDocumentReader final : public VrmaDocumentReader {
public:
    bool Read(const std::string& resolvedPath,
              const std::vector<std::byte>& bytes,
              VrmaCanonicalDocument* outDocument,
              std::string* outError) override;
};

PXR_NAMESPACE_CLOSE_SCOPE
