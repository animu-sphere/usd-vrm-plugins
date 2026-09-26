// SPDX-License-Identifier: Apache-2.0
//
// What every vrmImaging adapter does, given a canonical schema and the
// namespace group its attributes live under: the schema's attributes as a
// Hydra data-source contribution to the material prim they are authored on,
// and a changed attribute as the one locator it feeds
// (VRM_IMAGING_POLICY.md §5, §9, §28).
//
// The Hydra names are derived, never listed: `inputs:vrm:<group>:<field>` of
// the registered definition is `vrm/<group>/<field>`, and a multiple-apply
// schema's `inputs:vrm:<group>:<instance>:<field>` is
// `vrm/<group>/<instance>/<field>`. A namespaced field nests, one locator
// element per namespace element (policy §7, §28). A concrete adapter names its
// schema and its group and nothing else.
#ifndef USD_VRM_IMAGING_SCHEMA_ADAPTER_H
#define USD_VRM_IMAGING_SCHEMA_ADAPTER_H

#include <pxr/pxr.h>
#include <pxr/base/tf/token.h>
#include <pxr/usdImaging/usdImaging/apiSchemaAdapter.h>

#include <memory>
#include <utility>
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

class UsdVrmImagingSchemaAdapter : public UsdImagingAPISchemaAdapter
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

    // The schema's fields and the names they are read from, shared with every
    // data source the adapter hands out, which may outlive it.
    struct Schema;

protected:
    // `documentedDefaults`: fields whose schema documents a default it cannot
    // carry as a fallback, and that default. Used only while the registered
    // definition has no fallback for the field.
    UsdVrmImagingSchemaAdapter(
        const TfToken& schemaName, const TfToken& group,
        std::vector<std::pair<TfToken, TfToken>> documentedDefaults = {});

private:
    std::shared_ptr<const Schema> _schema;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
