// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "pxr/pxr.h"
#include "model/VrmaCanonicalDocument.h"

#include <cstddef>
#include <string>
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

class VrmaDocumentReader {
public:
    virtual ~VrmaDocumentReader() = default;
    virtual bool Read(const std::string& resolvedPath,
                      const std::vector<std::byte>& bytes,
                      VrmaCanonicalDocument* outDocument,
                      std::string* outError) = 0;
};

PXR_NAMESPACE_CLOSE_SCOPE
