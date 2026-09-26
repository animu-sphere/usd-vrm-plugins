// SPDX-License-Identifier: Apache-2.0
//
// `VrmMaterialAPI` as Hydra data: Step I1 of VRM_IMAGING_POLICY.md. The
// fields are the registered definition's `inputs:vrm:material:*`; the one
// field named here is the one whose default the schema cannot carry.
#include "VrmMaterialAPIAdapter.h"

#include <pxr/base/tf/type.h>

PXR_NAMESPACE_OPEN_SCOPE

TF_REGISTRY_FUNCTION(TfType)
{
    using Adapter = UsdVrmImagingMaterialAPIAdapter;
    TfType t = TfType::Define<Adapter, TfType::Bases<Adapter::BaseAdapter>>();
    t.SetFactory<UsdImagingAPISchemaAdapterFactory<Adapter>>();
}

// `alphaMode` has no schema fallback: usdGenSchema would name its C++ token
// `OPAQUE`, a wingdi.h macro. The schema documents unauthored as 'OPAQUE', as
// glTF does, and a consumer is owed that value rather than an absence it would
// have to interpret (policy §28). Should the schema gain the fallback, the
// definition's value is used instead and this entry does nothing.
UsdVrmImagingMaterialAPIAdapter::UsdVrmImagingMaterialAPIAdapter()
    : UsdVrmImagingSchemaAdapter(
          TfToken("VrmMaterialAPI"), TfToken("material"),
          {{TfToken("alphaMode"), TfToken("OPAQUE")}})
{
}

PXR_NAMESPACE_CLOSE_SCOPE
