// SPDX-License-Identifier: Apache-2.0
//
// The UsdImaging API-schema adapter for `VrmTextureInfoAPI:<role>`: each
// applied role's fields under `vrm/textureInfo/<role>/<field>` of the material
// prim (VRM_IMAGING_POLICY.md §28, §29).
#ifndef USD_VRM_IMAGING_TEXTURE_INFO_API_ADAPTER_H
#define USD_VRM_IMAGING_TEXTURE_INFO_API_ADAPTER_H

#include "VrmSchemaAdapter.h"

PXR_NAMESPACE_OPEN_SCOPE

class UsdVrmImagingTextureInfoAPIAdapter : public UsdVrmImagingSchemaAdapter
{
public:
    UsdVrmImagingTextureInfoAPIAdapter();
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
