// SPDX-License-Identifier: Apache-2.0
//
// vrmImaging Step I5 (VRM_IMAGING_POLICY.md §16, §17.1): the plugin is found
// and its adapters constructed from `plugInfo.json` alone.
//
// Nothing here links the plugin or registers anything: the session names the
// staged `plugInfo.json` on PXR_PLUGINPATH_NAME, as a runtime composition does,
// and the suite asks OpenUSD what it found, in the order a Hydra host meets it:
//
//   * PlugRegistry: a plugin named `VrmImaging` is registered and not loaded,
//     and it declares exactly the three adapter types, each derived from
//     `UsdImagingAPISchemaAdapter` and naming one registered `Vrm*API`;
//   * UsdImaging's adapter registry: one adapter per schema -- no other plugin
//     in the session claims the same `apiSchemaName` -- known before the
//     library is loaded;
//   * construction: the registry builds each adapter, of the declared type,
//     which is what loads the library.
//
// The per-schema suites go on to what each adapter contributes; this one is
// the part of that path they take for granted.

#include "pxr/pxr.h"

#include "pxr/base/js/value.h"
#include "pxr/base/plug/plugin.h"
#include "pxr/base/plug/registry.h"
#include "pxr/base/tf/token.h"
#include "pxr/base/tf/type.h"
#include "pxr/usd/usd/schemaRegistry.h"
#include "pxr/usdImaging/usdImaging/adapterRegistry.h"
#include "pxr/usdImaging/usdImaging/apiSchemaAdapter.h"

#include <cassert>
#include <cstdio>
#include <map>
#include <set>
#include <string>

PXR_NAMESPACE_USING_DIRECTIVE

namespace {

const char* const kPlugin = "VrmImaging";

// The schema each adapter serves, as plugInfo.json declares it.
const std::map<std::string, std::string> kAdapters = {
    {"VrmMaterialAPI", "UsdVrmImagingMaterialAPIAdapter"},
    {"VrmMToonAPI", "UsdVrmImagingMToonAPIAdapter"},
    {"VrmTextureInfoAPI", "UsdVrmImagingTextureInfoAPIAdapter"},
};

std::string
ApiSchemaName(const TfType& type)
{
    const JsValue name = PlugRegistry::GetInstance().GetDataFromPluginMetaData(
        type, "apiSchemaName");
    return name.IsString() ? name.GetString() : std::string();
}

PlugPluginPtr
ThePlugin()
{
    const PlugPluginPtr plugin =
        PlugRegistry::GetInstance().GetPluginWithName(kPlugin);
    if (!plugin) {
        std::fprintf(stderr, "no plugin named %s: is its plugInfo.json on "
                             "PXR_PLUGINPATH_NAME?\n", kPlugin);
    }
    assert(plugin && "vrmImaging is not discovered");
    return plugin;
}

// ---------------------------------------------------------------------------
// PlugRegistry: the plugin and the types it declares
// ---------------------------------------------------------------------------
void
TestThePluginDeclaresOneAdapterPerSchema()
{
    const PlugPluginPtr plugin = ThePlugin();
    assert(!plugin->IsLoaded() && "something loaded the library before "
                                  "anything asked for an adapter");

    std::set<TfType> adapters;
    PlugRegistry::GetAllDerivedTypes<UsdImagingAPISchemaAdapter>(&adapters);

    std::map<std::string, std::string> declared;
    std::map<std::string, int> claims;
    for (const TfType& type : adapters) {
        const std::string schema = ApiSchemaName(type);
        if (kAdapters.count(schema)) {
            ++claims[schema];
        }
        if (PlugRegistry::GetInstance().GetPluginForType(type) == plugin) {
            declared[schema] = type.GetTypeName();
        }
    }
    for (const auto& [schema, type] : declared) {
        std::printf("  %s -> %s\n", schema.c_str(), type.c_str());
    }
    assert(declared == kAdapters &&
           "the plugin does not declare exactly one adapter per Vrm*API");
    for (const auto& [schema, count] : claims) {
        if (count != 1) {
            std::fprintf(stderr, "%d adapters claim %s\n", count, schema.c_str());
        }
        assert(count == 1 && "another plugin claims a Vrm*API");
    }

    // Each names a schema the session has registered: vrmSchema's, which
    // this plugin does not link. VrmTextureInfoAPI is multiple-apply.
    for (const auto& [schema, type] : kAdapters) {
        const TfType schemaType =
            UsdSchemaRegistry::GetTypeFromSchemaTypeName(TfToken(schema));
        if (schemaType.IsUnknown()) {
            std::fprintf(stderr, "%s is not a registered schema\n", schema.c_str());
        }
        assert(!schemaType.IsUnknown() && "an adapter names no registered schema");
        assert(UsdSchemaRegistry::IsAppliedAPISchema(schemaType));
        assert(UsdSchemaRegistry::IsMultipleApplyAPISchema(schemaType) ==
               (schema == "VrmTextureInfoAPI"));
    }
}

// ---------------------------------------------------------------------------
// UsdImaging: known from the metadata, constructed on demand
// ---------------------------------------------------------------------------
void
TestUsdImagingConstructsEachAdapter()
{
    UsdImagingAdapterRegistry& registry = UsdImagingAdapterRegistry::GetInstance();
    for (const auto& [schema, type] : kAdapters) {
        assert(registry.HasAPISchemaAdapter(TfToken(schema)) &&
               "UsdImaging knows no adapter for a Vrm*API");
    }
    // Knowing them costs nothing: the registry read plugInfo.json only.
    assert(!ThePlugin()->IsLoaded() &&
           "the adapter registry loaded the library to learn its keys");

    for (const auto& [schema, type] : kAdapters) {
        const UsdImagingAPISchemaAdapterSharedPtr adapter =
            registry.ConstructAPISchemaAdapter(TfToken(schema));
        assert(adapter && "UsdImaging did not construct the adapter");
        const TfType built = TfType::Find(*adapter);
        if (built.GetTypeName() != type) {
            std::fprintf(stderr, "%s built a %s, not a %s\n", schema.c_str(),
                         built.GetTypeName().c_str(), type.c_str());
        }
        assert(built.GetTypeName() == type &&
               "the adapter is not the declared type");
    }
    assert(ThePlugin()->IsLoaded() && "constructing an adapter loads the library");
}

} // namespace

int
main()
{
    TestThePluginDeclaresOneAdapterPerSchema();
    TestUsdImagingConstructsEachAdapter();
    std::printf("vrmImaging_discovery: passed\n");
    return 0;
}
