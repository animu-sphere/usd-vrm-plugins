// SPDX-License-Identifier: Apache-2.0
//
// `VrmTextureInfoAPI:<role>` as Hydra data: Step I2 of VRM_IMAGING_POLICY.md.
// The schema is multiple-apply, the instance name is the role, and the roles
// and fields are the registered definition's; nothing here names one. The
// image is exposed as an asset path, authored and resolved, and never read
// (policy §11).
#include "VrmTextureInfoAPIAdapter.h"

#include <pxr/base/tf/type.h>

PXR_NAMESPACE_OPEN_SCOPE

TF_REGISTRY_FUNCTION(TfType)
{
    using Adapter = UsdVrmImagingTextureInfoAPIAdapter;
    TfType t = TfType::Define<Adapter, TfType::Bases<Adapter::BaseAdapter>>();
    t.SetFactory<UsdImagingAPISchemaAdapterFactory<Adapter>>();
}

UsdVrmImagingTextureInfoAPIAdapter::UsdVrmImagingTextureInfoAPIAdapter()
    : UsdVrmImagingSchemaAdapter(
          TfToken("VrmTextureInfoAPI"), TfToken("textureInfo"))
{
}

PXR_NAMESPACE_CLOSE_SCOPE
