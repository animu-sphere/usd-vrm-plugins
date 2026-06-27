// SPDX-License-Identifier: Apache-2.0
//
// Authors a USD scene description from the canonical VRM document. Consumes only
// VrmCanonicalDocument; emits a USDA string the file-format plugin imports into
// the layer.
#pragma once

#include "pxr/pxr.h"

#include "model/VrmCanonicalDocument.h"

#include <string>
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

class UsdVrmAuthorer {
public:
    // Build the USD scene for `doc` and serialize it to `outUsda`. Authoring-time
    // diagnostics are appended to `outWarnings`. Returns false only on an
    // unrecoverable authoring/serialization error.
    bool WriteToString(const VrmCanonicalDocument& doc,
                       std::string* outUsda,
                       std::vector<std::string>* outWarnings) const;
};

PXR_NAMESPACE_CLOSE_SCOPE
