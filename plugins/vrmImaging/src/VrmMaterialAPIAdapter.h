// SPDX-License-Identifier: Apache-2.0
//
// The UsdImaging API-schema adapter for `VrmMaterialAPI`: its fields under
// `vrm/material/<field>` of the material prim (VRM_IMAGING_POLICY.md §28).
#ifndef USD_VRM_IMAGING_MATERIAL_API_ADAPTER_H
#define USD_VRM_IMAGING_MATERIAL_API_ADAPTER_H

#include "VrmSchemaAdapter.h"

PXR_NAMESPACE_OPEN_SCOPE

class UsdVrmImagingMaterialAPIAdapter : public UsdVrmImagingSchemaAdapter
{
public:
    UsdVrmImagingMaterialAPIAdapter();
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
