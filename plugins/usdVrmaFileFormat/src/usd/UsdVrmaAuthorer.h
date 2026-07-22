// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "pxr/pxr.h"
#include "model/VrmaCanonicalDocument.h"

#include <string>

PXR_NAMESPACE_OPEN_SCOPE

class UsdVrmaAuthorer {
public:
    bool WriteToString(const VrmaCanonicalDocument& document,
                       std::string* outUsda) const;
};

PXR_NAMESPACE_CLOSE_SCOPE
