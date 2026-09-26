// SPDX-License-Identifier: Apache-2.0
//
// The shared half of every vrmImaging adapter (VRM_IMAGING_POLICY.md §28).
//
// A canonical schema reaches Hydra as data on the material prim, under
// `vrm/<group>/<field>`, with no stage query on the consumer's side and no
// change to the material network beside it (policy §6.1). Each adapter
// contributes its own `vrm/<group>` branch; UsdImaging overlays the
// contributions of every API schema on a prim, and the overlay merges the
// `vrm` containers, so a material with `VrmMaterialAPI` and `VrmMToonAPI` has
// one `vrm` container holding both groups.
//
// vrmSchema is not linked. The definition is read from the schema registry by
// the schema's name, which is what vrmSchema's plugInfo.json registers; an
// adapter is only ever called for a prim whose definition includes its
// schema, so it cannot run without that registration anyway.
#include "VrmSchemaAdapter.h"

#include <pxr/base/tf/stringUtils.h>
#include <pxr/base/vt/value.h>
#include <pxr/imaging/hd/dataSource.h>
#include <pxr/usd/usd/primDefinition.h>
#include <pxr/usd/usd/schemaRegistry.h>
#include <pxr/usdImaging/usdImaging/dataSourceAttribute.h>
#include <pxr/usdImaging/usdImaging/dataSourceStageGlobals.h>

#include <algorithm>
#include <string>

PXR_NAMESPACE_OPEN_SCOPE

namespace {

const TfToken& _VrmToken()
{
    static const TfToken vrm("vrm");
    return vrm;
}

} // namespace

struct UsdVrmImagingSchemaAdapter::Schema
{
    TfToken group;
    // `inputs:vrm:<group>:`
    std::string prefix;
    // The definition's fields, sorted.
    TfTokenVector fields;
    // Fields the schema documents a default for and cannot carry one
    // (VrmMaterialAPI's alphaMode), with that default.
    std::vector<std::pair<TfToken, TfToken>> documentedDefaults;

    bool IsField(const TfToken& field) const
    {
        return std::binary_search(fields.begin(), fields.end(), field);
    }

    HdDataSourceLocator Locator(const TfToken& field) const
    {
        return HdDataSourceLocator(_VrmToken(), group, field);
    }

    const TfToken* DocumentedDefault(const TfToken& field) const
    {
        for (const auto& entry : documentedDefaults) {
            if (entry.first == field) {
                return &entry.second;
            }
        }
        return nullptr;
    }
};

namespace {

using Schema = UsdVrmImagingSchemaAdapter::Schema;

// One named child, which is all the two outer levels (`vrm`, `<group>`) are.
// HdRetainedContainerDataSource would do, but hd/retainedDataSource.h does not
// compile as C++20 under GCC 13 -- OpenUSD 26.08 declares a constructor there
// as `HdRetainedTypedSampledDataSource<bool>(...)`, a template-id C++20 no
// longer accepts in that position -- and `ost`'s toolchain compiles C++20.
// MSVC accepts it, so only the Linux lane shows it (policy §27 item 11).
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

// A token that does not vary: a documented default standing in for an
// unauthored field whose schema cannot carry a fallback. Typed, so a consumer
// casts it exactly as it casts the authored attribute's data source.
class _ConstantToken : public HdTypedSampledDataSource<TfToken>
{
public:
    HD_DECLARE_DATASOURCE(_ConstantToken);

    VtValue GetValue(Time) override { return VtValue(_value); }
    TfToken GetTypedValue(Time) override { return _value; }

    bool GetContributingSampleTimesForInterval(
        Time, Time, std::vector<Time>*) override
    {
        return false;
    }

private:
    explicit _ConstantToken(const TfToken& value) : _value(value) {}

    TfToken _value;
};

// One field per name, each the attribute's resolved value at the scene
// index's time -- the schema fallback included, so a consumer never has to
// know the schema's defaults. Built lazily, and sampled rather than copied, so
// a time-sampled attribute stays time-sampled (policy §10).
class _FieldsDataSource : public HdContainerDataSource
{
public:
    HD_DECLARE_DATASOURCE(_FieldsDataSource);

    TfTokenVector GetNames() override { return _schema->fields; }

    HdDataSourceBaseHandle Get(const TfToken& name) override
    {
        if (!_schema->IsField(name)) {
            return nullptr;
        }
        const UsdAttribute attribute =
            _prim.GetAttribute(TfToken(_schema->prefix + name.GetString()));
        if (!attribute || !attribute.HasValue()) {
            if (const TfToken* fallback = _schema->DocumentedDefault(name)) {
                return _ConstantToken::New(*fallback);
            }
            return nullptr;
        }
        // The locator a time-varying attribute is dirtied at when the scene
        // index's time moves: the same one an edit dirties.
        return UsdImagingDataSourceAttributeNew(
            attribute, _stageGlobals, _prim.GetPath(), _schema->Locator(name));
    }

private:
    _FieldsDataSource(const std::shared_ptr<const Schema>& schema,
                      const UsdPrim& prim,
                      const UsdImagingDataSourceStageGlobals& stageGlobals)
        : _schema(schema), _prim(prim), _stageGlobals(stageGlobals)
    {
    }

    std::shared_ptr<const Schema> _schema;
    UsdPrim _prim;
    const UsdImagingDataSourceStageGlobals& _stageGlobals;
};

std::shared_ptr<const Schema>
_ReadSchema(const TfToken& schemaName, const TfToken& group,
            std::vector<std::pair<TfToken, TfToken>> documentedDefaults)
{
    auto schema = std::make_shared<Schema>();
    schema->group = group;
    schema->prefix = "inputs:vrm:" + group.GetString() + ":";
    const UsdPrimDefinition* definition =
        UsdSchemaRegistry::GetInstance().FindAppliedAPIPrimDefinition(
            schemaName);
    if (definition) {
        for (const TfToken& name : definition->GetPropertyNames()) {
            if (TfStringStartsWith(name.GetString(), schema->prefix)) {
                schema->fields.emplace_back(
                    name.GetString().substr(schema->prefix.size()));
            }
        }
        // A documented default applies only while the schema has no fallback
        // of its own: the day it gains one, the definition's value wins.
        for (auto& entry : documentedDefaults) {
            VtValue fallback;
            const TfToken property(schema->prefix + entry.first.GetString());
            if (!definition->GetAttributeFallbackValue(property, &fallback)) {
                schema->documentedDefaults.push_back(std::move(entry));
            }
        }
    }
    std::sort(schema->fields.begin(), schema->fields.end());
    return schema;
}

} // namespace

UsdVrmImagingSchemaAdapter::UsdVrmImagingSchemaAdapter(
    const TfToken& schemaName, const TfToken& group,
    std::vector<std::pair<TfToken, TfToken>> documentedDefaults)
    : _schema(_ReadSchema(schemaName, group, std::move(documentedDefaults)))
{
}

HdContainerDataSourceHandle
UsdVrmImagingSchemaAdapter::GetImagingSubprimData(
    UsdPrim const& prim,
    TfToken const& subprim,
    TfToken const& appliedInstanceName,
    const UsdImagingDataSourceStageGlobals& stageGlobals)
{
    // A contribution to the material prim itself: no child Hydra prim, and
    // the schema is single-apply.
    if (!subprim.IsEmpty() || !appliedInstanceName.IsEmpty()) {
        return nullptr;
    }
    // Beside the prim adapter's `material` container, never inside it: the
    // portable networks stay as the realizations authored them (policy §6.1).
    return _ChildContainer::New(
        _VrmToken(),
        _ChildContainer::New(
            _schema->group,
            _FieldsDataSource::New(_schema, prim, stageGlobals)));
}

HdDataSourceLocatorSet
UsdVrmImagingSchemaAdapter::InvalidateImagingSubprim(
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
    for (const TfToken& property : properties) {
        if (!TfStringStartsWith(property.GetString(), _schema->prefix)) {
            continue;
        }
        const TfToken field(property.GetString().substr(_schema->prefix.size()));
        if (_schema->IsField(field)) {
            result.insert(_schema->Locator(field));
        }
    }
    return result;
}

PXR_NAMESPACE_CLOSE_SCOPE
