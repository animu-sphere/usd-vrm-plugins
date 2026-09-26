// SPDX-License-Identifier: Apache-2.0
//
// `VrmMToonAPI` as Hydra data: Step I0 of VRM_IMAGING_POLICY.md, on the
// shared adapter since Step I1. The fields are the registered definition's
// `inputs:vrm:mtoon:*`; nothing here names one.
#include "VrmMToonAPIAdapter.h"

#include <pxr/base/tf/type.h>

PXR_NAMESPACE_OPEN_SCOPE

TF_REGISTRY_FUNCTION(TfType)
{
    using Adapter = UsdVrmImagingMToonAPIAdapter;
    TfType t = TfType::Define<Adapter, TfType::Bases<Adapter::BaseAdapter>>();
    t.SetFactory<UsdImagingAPISchemaAdapterFactory<Adapter>>();
}

UsdVrmImagingMToonAPIAdapter::UsdVrmImagingMToonAPIAdapter()
    : UsdVrmImagingSchemaAdapter(TfToken("VrmMToonAPI"), TfToken("mtoon"))
{
}

PXR_NAMESPACE_CLOSE_SCOPE
