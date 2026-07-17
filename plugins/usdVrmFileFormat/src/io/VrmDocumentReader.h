// SPDX-License-Identifier: Apache-2.0
//
// Reader abstraction. The authoring layer depends only on this interface and the
// canonical document, never on the concrete parser (cgltf today, possibly
// fastgltf later for compression/perf). Per the plan, this keeps the OpenUSD ABI
// decoupled from any third-party C/C++ parser ABI.
#pragma once

#include "pxr/pxr.h"

#include "model/VrmCanonicalDocument.h"

#include <cstddef>
#include <string>
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

class VrmDocumentReader {
public:
    virtual ~VrmDocumentReader() = default;

    // Parse `bytes` (a whole .vrm/GLB file) into `outDoc`. `resolvedPath` is used
    // only for diagnostics and external-resource base resolution. Returns false
    // and fills `outError` on a fatal error; non-fatal issues are appended to
    // outDoc->warnings.
    virtual bool Read(const std::string& resolvedPath,
                      const std::vector<std::byte>& bytes,
                      VrmCanonicalDocument* outDoc,
                      std::string* outError) = 0;
};

PXR_NAMESPACE_CLOSE_SCOPE
