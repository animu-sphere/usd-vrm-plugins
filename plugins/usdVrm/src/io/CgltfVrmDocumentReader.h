// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "pxr/pxr.h"

#include "io/VrmDocumentReader.h"

PXR_NAMESPACE_OPEN_SCOPE

// cgltf-backed reader: GLB container, accessors, meshes, materials, skin, and the
// VRM(C_vrm) extension JSON (parsed with pxr/base/js, normalized across 0.x/1.0).
class CgltfVrmDocumentReader final : public VrmDocumentReader {
public:
    bool Read(const std::string& resolvedPath,
              const std::vector<std::byte>& bytes,
              VrmCanonicalDocument* outDoc,
              std::string* outError) override;
};

PXR_NAMESPACE_CLOSE_SCOPE
