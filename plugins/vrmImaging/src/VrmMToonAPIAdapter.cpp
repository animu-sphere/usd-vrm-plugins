// SPDX-License-Identifier: Apache-2.0
//
// Phase I0 of VRM_IMAGING_POLICY.md: `VrmMToonAPI` reaches Hydra as data on
// the material prim, under `vrm/mtoon/<field>`, with no stage query on the
// consumer's side and no change to the material network beside it.
//
// The Hydra field names are the schema's own, derived mechanically: every
// property of the applied schema's definition named `inputs:vrm:mtoon:<field>`
// becomes `<field>` (policy §7). Nothing here names a field, so a field the
// schema gains reaches Hydra without an edit, and a property authored on the
// prim that the schema does not define never does.
//
// vrmSchema is not linked. The definition is read from the schema registry by
// the schema's name, which is what vrmSchema's plugInfo.json registers; the
// adapter is only ever called for a prim whose definition includes the schema,
// so it cannot run without that registration anyway.
#include "VrmMToonAPIAdapter.h"

#include <pxr/base/tf/staticTokens.h>
#include <pxr/base/tf/stringUtils.h>
#include <pxr/base/tf/type.h>
#include <pxr/imaging/hd/dataSource.h>
#include <pxr/usd/usd/primDefinition.h>
#include <pxr/usd/usd/schemaRegistry.h>
#include <pxr/usdImaging/usdImaging/dataSourceAttribute.h>
#include <pxr/usdImaging/usdImaging/dataSourceStageGlobals.h>

#include <algorithm>
#include <string>

PXR_NAMESPACE_OPEN_SCOPE

// Not a public header: the Hydra-facing names are not frozen until the
// experiment has been inspected (policy §6.1, §25).
TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    (vrm)
    (mtoon)
    (VrmMToonAPI)
);

TF_REGISTRY_FUNCTION(TfType)
{
    using Adapter = UsdVrmImagingMToonAPIAdapter;
    TfType t = TfType::Define<Adapter, TfType::Bases<Adapter::BaseAdapter>>();
    t.SetFactory<UsdImagingAPISchemaAdapterFactory<Adapter>>();
}

namespace {

const std::string& _AttributePrefix()
{
    static const std::string prefix("inputs:vrm:mtoon:");
    return prefix;
}

// The schema's fields, sorted, read once from the registered definition.
const TfTokenVector& _Fields()
{
    static const TfTokenVector fields = [] {
        TfTokenVector result;
        const UsdPrimDefinition* definition =
            UsdSchemaRegistry::GetInstance().FindAppliedAPIPrimDefinition(
                _tokens->VrmMToonAPI);
        if (!definition) {
            return result;
        }
        const std::string& prefix = _AttributePrefix();
        for (const TfToken& name : definition->GetPropertyNames()) {
            if (TfStringStartsWith(name.GetString(), prefix)) {
                result.emplace_back(name.GetString().substr(prefix.size()));
            }
        }
        std::sort(result.begin(), result.end());
        return result;
    }();
    return fields;
}

bool _IsField(const TfToken& field)
{
    const TfTokenVector& fields = _Fields();
    return std::binary_search(fields.begin(), fields.end(), field);
}

// One named child, which is all the two outer levels (`vrm`, `mtoon`) are.
// HdRetainedContainerDataSource would do, but hd/retainedDataSource.h does not
// compile as C++20 under GCC 13 -- OpenUSD 26.08 declares a constructor there
// as `HdRetainedTypedSampledDataSource<bool>(...)`, a template-id C++20 no
// longer accepts in that position -- and `ost`'s toolchain compiles C++20.
// MSVC accepts it, so only the Linux lane shows it.
class _ChildContainer : public HdContainerDataSource
{
public:
    HD_DECLARE_DATASOURCE(_ChildContainer);

    TfTokenVector GetNames() override { return {_name}; }

    HdDataSourceBaseHandle Get(const TfToken& name) override
    {
        return name == _name ? _child : nullptr;
    }

private:
    _ChildContainer(const TfToken& name, const HdDataSourceBaseHandle& child)
        : _name(name), _child(child)
    {
    }

    TfToken _name;
    HdDataSourceBaseHandle _child;
};

// One field per name, each the attribute's resolved value at the scene
// index's time -- the schema fallback included, so a consumer never has to
// know the schema's defaults. Built lazily, and sampled rather than copied, so
// a time-sampled attribute stays time-sampled (policy §10).
class _MToonDataSource : public HdContainerDataSource
{
public:
    HD_DECLARE_DATASOURCE(_MToonDataSource);

    TfTokenVector GetNames() override { return _Fields(); }

    HdDataSourceBaseHandle Get(const TfToken& name) override
    {
        if (!_IsField(name)) {
            return nullptr;
        }
        const UsdAttribute attribute = _prim.GetAttribute(
            TfToken(_AttributePrefix() + name.GetString()));
        if (!attribute || !attribute.HasValue()) {
            return nullptr;
        }
        // The locator a time-varying attribute is dirtied at when the scene
        // index's time moves: the same one an edit dirties.
        return UsdImagingDataSourceAttributeNew(
            attribute, _stageGlobals, _prim.GetPath(),
            HdDataSourceLocator(_tokens->vrm, _tokens->mtoon, name));
    }

private:
    _MToonDataSource(const UsdPrim& prim,
                     const UsdImagingDataSourceStageGlobals& stageGlobals)
        : _prim(prim), _stageGlobals(stageGlobals)
    {
    }

    UsdPrim _prim;
    const UsdImagingDataSourceStageGlobals& _stageGlobals;
};

} // namespace

HdContainerDataSourceHandle
UsdVrmImagingMToonAPIAdapter::GetImagingSubprimData(
    UsdPrim const& prim,
    TfToken const& subprim,
    TfToken const& appliedInstanceName,
    const UsdImagingDataSourceStageGlobals& stageGlobals)
{
    // A contribution to the material prim itself: no child Hydra prim, and
    // `VrmMToonAPI` is single-apply.
    if (!subprim.IsEmpty() || !appliedInstanceName.IsEmpty()) {
        return nullptr;
    }
    // Beside the prim adapter's `material` container, never inside it: the
    // portable networks stay as the realizations authored them (policy §6.1).
    return _ChildContainer::New(
        _tokens->vrm,
        _ChildContainer::New(
            _tokens->mtoon, _MToonDataSource::New(prim, stageGlobals)));
}

HdDataSourceLocatorSet
UsdVrmImagingMToonAPIAdapter::InvalidateImagingSubprim(
    UsdPrim const& prim,
    TfToken const& subprim,
    TfToken const& appliedInstanceName,
    TfTokenVector const& properties,
    const UsdImagingPropertyInvalidationType invalidationType)
{
    HdDataSourceLocatorSet result;
    if (!subprim.IsEmpty() || !appliedInstanceName.IsEmpty()) {
        return result;
    }
    // One locator per changed field, never the whole contribution (policy §9).
    // An authored value appearing or disappearing (a resync) dirties the same
    // locator as a value changing: the name set is the schema's either way.
    const std::string& prefix = _AttributePrefix();
    for (const TfToken& property : properties) {
        if (!TfStringStartsWith(property.GetString(), prefix)) {
            continue;
        }
        const TfToken field(property.GetString().substr(prefix.size()));
        if (_IsField(field)) {
            result.insert(
                HdDataSourceLocator(_tokens->vrm, _tokens->mtoon, field));
        }
    }
    return result;
}

PXR_NAMESPACE_CLOSE_SCOPE
