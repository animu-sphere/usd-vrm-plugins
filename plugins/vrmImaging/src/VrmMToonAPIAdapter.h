// SPDX-License-Identifier: Apache-2.0
//
// The UsdImaging API-schema adapter for `VrmMToonAPI`: the applied schema's
// canonical attributes as a Hydra data-source contribution to the material
// prim they are authored on, and a changed attribute as the one locator it
// feeds (VRM_IMAGING_POLICY.md §5, §9).
#ifndef USD_VRM_IMAGING_MTOON_API_ADAPTER_H
#define USD_VRM_IMAGING_MTOON_API_ADAPTER_H

#include <pxr/pxr.h>
#include <pxr/usdImaging/usdImaging/apiSchemaAdapter.h>

PXR_NAMESPACE_OPEN_SCOPE

class UsdVrmImagingMToonAPIAdapter : public UsdImagingAPISchemaAdapter
{
public:
    using BaseAdapter = UsdImagingAPISchemaAdapter;

    HdContainerDataSourceHandle GetImagingSubprimData(
        UsdPrim const& prim,
        TfToken const& subprim,
        TfToken const& appliedInstanceName,
        const UsdImagingDataSourceStageGlobals& stageGlobals) override;

    HdDataSourceLocatorSet InvalidateImagingSubprim(
        UsdPrim const& prim,
        TfToken const& subprim,
        TfToken const& appliedInstanceName,
        TfTokenVector const& properties,
        UsdImagingPropertyInvalidationType invalidationType) override;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
