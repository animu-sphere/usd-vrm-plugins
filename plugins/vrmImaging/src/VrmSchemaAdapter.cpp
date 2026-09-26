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
// A multiple-apply schema is asked once per applied instance, and each
// instance contributes `vrm/<group>/<instance>`; the same overlay merges them
// under one `<group>` (policy §29).
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

bool _StartsWith(const std::string& s, const std::string& prefix)
{
    return s.compare(0, prefix.size(), prefix) == 0;
}

} // namespace

struct UsdVrmImagingSchemaAdapter::Schema
{
    TfToken name;
    TfToken group;
    // Asked once per applied instance, each under its own locator.
    bool multipleApply = false;
    // `inputs:vrm:<group>:`, or `inputs:vrm:<group>:__INSTANCE_NAME__:` for a
    // multiple-apply schema: the part of a property name before its field.
    std::string prefixTemplate;
    // The definition's fields, sorted. A field may be namespaced
    // (`transform:offset`).
    TfTokenVector fields;
    // Fields the schema documents a default for and cannot carry one
    // (VrmMaterialAPI's alphaMode), with that default.
    std::vector<std::pair<TfToken, TfToken>> documentedDefaults;

    bool IsField(const TfToken& field) const
    {
        return std::binary_search(fields.begin(), fields.end(), field);
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

    // Whether this adapter answers for `instance` at all: an instance name on
    // exactly the schemas that have one, and only one the schema allows --
    // read from the registry, which has the schema's own list. A role outside
    // it is not canonical data, and an adapter that exposed it would make it
    // look like some.
    bool Answers(const TfToken& instance) const
    {
        if (!multipleApply) {
            return instance.IsEmpty();
        }
        return !instance.IsEmpty() &&
               UsdSchemaRegistry::IsAllowedAPISchemaInstanceName(name, instance);
    }
};

namespace {

using Schema = UsdVrmImagingSchemaAdapter::Schema;

// One applied instance of a schema -- the only one, for a single-apply
// schema: where its attributes are read from and where its locators are.
struct _Binding
{
    std::shared_ptr<const Schema> schema;
    // `inputs:vrm:<group>:` or `inputs:vrm:<group>:<instance>:`
    std::string prefix;
    // `vrm/<group>` or `vrm/<group>/<instance>`
    HdDataSourceLocator root;

    // One locator element per namespace element of the field.
    HdDataSourceLocator Locator(const TfToken& field) const
    {
        HdDataSourceLocator result = root;
        for (const std::string& element : TfStringSplit(field.GetString(), ":")) {
            result = result.Append(TfToken(element));
        }
        return result;
    }
};

std::shared_ptr<const _Binding>
_Bind(const std::shared_ptr<const Schema>& schema, const TfToken& instance)
{
    auto binding = std::make_shared<_Binding>();
    binding->schema = schema;
    if (schema->multipleApply) {
        binding->prefix = UsdSchemaRegistry::MakeMultipleApplyNameInstance(
            schema->prefixTemplate, instance).GetString();
        binding->root = HdDataSourceLocator(_VrmToken(), schema->group, instance);
    } else {
        binding->prefix = schema->prefixTemplate;
        binding->root = HdDataSourceLocator(_VrmToken(), schema->group);
    }
    return binding;
}

// One named child, which is all the outer levels (`vrm`, `<group>`, an
// instance) are. HdRetainedContainerDataSource would do, but
// hd/retainedDataSource.h does not compile as C++20 under GCC 13 -- OpenUSD
// 26.08 declares a constructor there as
// `HdRetainedTypedSampledDataSource<bool>(...)`, a template-id C++20 no longer
// accepts in that position -- and `ost`'s toolchain compiles C++20. MSVC
// accepts it, so only the Linux lane shows it (policy §27 item 11).
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

// The fields under one namespace of one binding -- the root namespace, or a
// nested one such as `transform:` -- each the attribute's resolved value at
// the scene index's time, the schema fallback included, so a consumer never
// has to know the schema's defaults. Built lazily, and sampled rather than
// copied, so a time-sampled attribute stays time-sampled (policy §10).
//
// A name is listed only when it resolves to something (policy §28.1 item 3):
// a field with no authored value, no fallback and no documented default -- a
// texture's `file`, unauthored -- is absent rather than empty, and so is a
// namespace none of whose fields resolves.
class _FieldsDataSource : public HdContainerDataSource
{
public:
    HD_DECLARE_DATASOURCE(_FieldsDataSource);

    TfTokenVector GetNames() override
    {
        TfTokenVector names;
        for (const TfToken& field : _binding->schema->fields) {
            const std::string& full = field.GetString();
            if (!_StartsWith(full, _namespace)) {
                continue;
            }
            const std::string rest = full.substr(_namespace.size());
            const TfToken name(rest.substr(0, rest.find(':')));
            if (std::find(names.begin(), names.end(), name) == names.end() &&
                _Resolves(name)) {
                names.push_back(name);
            }
        }
        return names;
    }

    HdDataSourceBaseHandle Get(const TfToken& name) override
    {
        const TfToken field(_namespace + name.GetString());
        if (_binding->schema->IsField(field)) {
            return _Leaf(field);
        }
        if (_Resolves(name)) {
            return _FieldsDataSource::New(
                _binding, _prim, _stageGlobals, field.GetString() + ":");
        }
        return nullptr;
    }

private:
    _FieldsDataSource(const std::shared_ptr<const _Binding>& binding,
                      const UsdPrim& prim,
                      const UsdImagingDataSourceStageGlobals& stageGlobals,
                      std::string ns)
        : _binding(binding), _prim(prim), _stageGlobals(stageGlobals),
          _namespace(std::move(ns))
    {
    }

    UsdAttribute _Attribute(const TfToken& field) const
    {
        return _prim.GetAttribute(TfToken(_binding->prefix + field.GetString()));
    }

    bool _FieldResolves(const TfToken& field) const
    {
        const UsdAttribute attribute = _Attribute(field);
        return (attribute && attribute.HasValue()) ||
               _binding->schema->DocumentedDefault(field);
    }

    // A leaf that resolves, or a namespace holding one.
    bool _Resolves(const TfToken& name) const
    {
        const TfToken field(_namespace + name.GetString());
        if (_binding->schema->IsField(field)) {
            return _FieldResolves(field);
        }
        const std::string nested = field.GetString() + ":";
        for (const TfToken& candidate : _binding->schema->fields) {
            if (_StartsWith(candidate.GetString(), nested) &&
                _FieldResolves(candidate)) {
                return true;
            }
        }
        return false;
    }

    HdDataSourceBaseHandle _Leaf(const TfToken& field) const
    {
        const UsdAttribute attribute = _Attribute(field);
        if (!attribute || !attribute.HasValue()) {
            if (const TfToken* fallback =
                    _binding->schema->DocumentedDefault(field)) {
                return _ConstantToken::New(*fallback);
            }
            return nullptr;
        }
        // The locator a time-varying attribute is dirtied at when the scene
        // index's time moves: the same one an edit dirties. An asset-valued
        // field comes back as UsdImaging's asset-path data source, authored
        // path and resolved path both, with nothing loaded (policy §11).
        return UsdImagingDataSourceAttributeNew(
            attribute, _stageGlobals, _prim.GetPath(), _binding->Locator(field));
    }

    std::shared_ptr<const _Binding> _binding;
    UsdPrim _prim;
    const UsdImagingDataSourceStageGlobals& _stageGlobals;
    // `` or `<namespace>:`, relative to the binding's prefix.
    std::string _namespace;
};

std::shared_ptr<const Schema>
_ReadSchema(const TfToken& schemaName, const TfToken& group,
            std::vector<std::pair<TfToken, TfToken>> documentedDefaults)
{
    auto schema = std::make_shared<Schema>();
    schema->name = schemaName;
    schema->group = group;
    const std::string groupNamespace = "inputs:vrm:" + group.GetString();
    const UsdSchemaRegistry::SchemaInfo* info =
        UsdSchemaRegistry::FindSchemaInfo(schemaName);
    schema->multipleApply =
        info && info->kind == UsdSchemaKind::MultipleApplyAPI;
    schema->prefixTemplate =
        (schema->multipleApply
             ? UsdSchemaRegistry::MakeMultipleApplyNameTemplate(
                   groupNamespace, std::string()).GetString()
             : groupNamespace) +
        ":";

    const UsdPrimDefinition* definition =
        UsdSchemaRegistry::GetInstance().FindAppliedAPIPrimDefinition(
            schemaName);
    if (definition) {
        for (const TfToken& name : definition->GetPropertyNames()) {
            if (_StartsWith(name.GetString(), schema->prefixTemplate)) {
                schema->fields.emplace_back(
                    name.GetString().substr(schema->prefixTemplate.size()));
            }
        }
        // A documented default applies only while the schema has no fallback
        // of its own: the day it gains one, the definition's value wins.
        for (auto& entry : documentedDefaults) {
            VtValue fallback;
            const TfToken property(
                schema->prefixTemplate + entry.first.GetString());
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
    // A contribution to the material prim itself: no child Hydra prim.
    if (!subprim.IsEmpty() || !_schema->Answers(appliedInstanceName)) {
        return nullptr;
    }
    const std::shared_ptr<const _Binding> binding =
        _Bind(_schema, appliedInstanceName);
    HdContainerDataSourceHandle fields =
        _FieldsDataSource::New(binding, prim, stageGlobals, std::string());
    if (_schema->multipleApply) {
        fields = _ChildContainer::New(appliedInstanceName, fields);
    }
    // Beside the prim adapter's `material` container, never inside it: the
    // portable networks stay as the realizations authored them (policy §6.1).
    return _ChildContainer::New(
        _VrmToken(), _ChildContainer::New(_schema->group, fields));
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
    if (!subprim.IsEmpty() || !_schema->Answers(appliedInstanceName)) {
        return result;
    }
    const std::shared_ptr<const _Binding> binding =
        _Bind(_schema, appliedInstanceName);
    // One locator per changed field, never the whole contribution (policy §9).
    // An authored value appearing or disappearing (a resync) dirties the same
    // locator as a value changing: whether a name is listed follows from its
    // value, so the one locator covers both. Another instance's properties
    // share the group prefix but not this instance's, and are its own call's.
    for (const TfToken& property : properties) {
        if (!_StartsWith(property.GetString(), binding->prefix)) {
            continue;
        }
        const TfToken field(property.GetString().substr(binding->prefix.size()));
        if (_schema->IsField(field)) {
            result.insert(binding->Locator(field));
        }
    }
    return result;
}

PXR_NAMESPACE_CLOSE_SCOPE
