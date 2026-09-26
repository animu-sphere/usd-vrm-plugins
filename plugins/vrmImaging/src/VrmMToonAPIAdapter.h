// SPDX-License-Identifier: Apache-2.0
//
// The UsdImaging API-schema adapter for `VrmMToonAPI`: its fields under
// `vrm/mtoon/<field>` of the material prim (VRM_IMAGING_POLICY.md §28).
#ifndef USD_VRM_IMAGING_MTOON_API_ADAPTER_H
#define USD_VRM_IMAGING_MTOON_API_ADAPTER_H

#include "VrmSchemaAdapter.h"

PXR_NAMESPACE_OPEN_SCOPE

class UsdVrmImagingMToonAPIAdapter : public UsdVrmImagingSchemaAdapter
{
public:
    UsdVrmImagingMToonAPIAdapter();
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
