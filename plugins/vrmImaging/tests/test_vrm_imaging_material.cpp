// SPDX-License-Identifier: Apache-2.0
//
// vrmImaging Step I1 (VRM_IMAGING_POLICY.md §20, §28): `VrmMaterialAPI`
// reaches a Hydra consumer through UsdImaging's own stage scene index beside
// `VrmMToonAPI`, and what the whole-material dirtying of §27 item 6 costs is
// measured rather than assumed.
//
// What is measured:
//
//   * discovery: UsdImaging knows an adapter for `VrmMaterialAPI` (§17.1);
//   * canonical → Hydra: authored values of each type the schema uses and
//     schema fallbacks under `vrm/material/<field>`, the field set being the
//     schema's; `alphaMode`, the one field without a fallback, reads as the
//     schema's documented 'OPAQUE' (§28);
//   * one `vrm` container: both schemas' groups side by side on one prim;
//   * invalidation: an authored edit dirties exactly `vrm/material/<field>` of
//     the VRM contribution -- and, from UsdImaging, the whole `material`
//     locator, whether a network reads the input or not (§27 item 6);
//   * time: moving the scene index's time dirties the sampled field and never
//     the whole `material`. A network that reads the input is dirtied at that
//     one parameter; one that does not, not at all. This is the measurement
//     §28's decision on the whole-material dirtying rests on.

#include "imaging_test_support.h"

#include "pxr/base/gf/vec3f.h"
#include "pxr/imaging/hd/materialNetworkSchema.h"
#include "pxr/imaging/hd/materialNodeParameterSchema.h"
#include "pxr/imaging/hd/materialNodeSchema.h"
#include "pxr/imaging/hd/materialSchema.h"
#include "pxr/usd/usd/primDefinition.h"
#include "pxr/usd/usd/schemaRegistry.h"
#include "pxr/usdImaging/usdImaging/adapterRegistry.h"

#include <algorithm>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE
using namespace vrm_imaging_test;

namespace {

const TfToken kMaterialGroup("material");
const TfToken kMToonGroup("mtoon");

HdDataSourceLocator
MaterialLocator(const char* field)
{
    return VrmLocator("material", field);
}

const UsdPrimDefinition&
Definition()
{
    const UsdPrimDefinition* definition =
        UsdSchemaRegistry::GetInstance().FindAppliedAPIPrimDefinition(
            TfToken("VrmMaterialAPI"));
    assert(definition && "vrmSchema is not registered in this session");
    return *definition;
}

// Every `<field>` of the definition's `inputs:vrm:material:<field>`.
TfTokenVector
SchemaFields()
{
    const std::string prefix = "inputs:vrm:material:";
    TfTokenVector fields;
    for (const TfToken& name : Definition().GetPropertyNames()) {
        if (name.GetString().compare(0, prefix.size(), prefix) == 0) {
            fields.emplace_back(name.GetString().substr(prefix.size()));
        }
    }
    std::sort(fields.begin(), fields.end());
    return fields;
}

// Every `diffuseColor` parameter of every network on the prim, by its locator,
// read -- which is what builds its data source and so lets time dirty it.
std::vector<std::pair<HdDataSourceLocator, VtValue>>
ReadDiffuseColorParameters(const Session& session, const char* path)
{
    std::vector<std::pair<HdDataSourceLocator, VtValue>> result;
    const HdDataSourceLocator root = HdMaterialSchema::GetDefaultLocator();
    const HdContainerDataSourceHandle material = HdContainerDataSource::Cast(
        HdContainerDataSource::Get(PrimData(session, path), root));
    assert(material && "the material network is gone");
    for (const TfToken& context : material->GetNames()) {
        const HdDataSourceLocator nodesLocator =
            root.Append(context).Append(HdMaterialNetworkSchemaTokens->nodes);
        const HdContainerDataSourceHandle nodes = HdContainerDataSource::Cast(
            HdContainerDataSource::Get(PrimData(session, path), nodesLocator));
        if (!nodes) {
            continue;
        }
        for (const TfToken& node : nodes->GetNames()) {
            const HdDataSourceLocator locator =
                nodesLocator.Append(node)
                    .Append(HdMaterialNodeSchemaTokens->parameters)
                    .Append(TfToken("diffuseColor"))
                    .Append(HdMaterialNodeParameterSchemaTokens->value);
            const VtValue value = ValueAt(session, path, locator);
            if (!value.IsEmpty()) {
                result.emplace_back(locator, value);
            }
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// §17.1 discovery
// ---------------------------------------------------------------------------
void
TestTheAdapterIsDiscovered()
{
    assert(UsdImagingAdapterRegistry::GetInstance().HasAPISchemaAdapter(
               TfToken("VrmMaterialAPI")) &&
           "UsdImaging registered no adapter for VrmMaterialAPI");
}

// ---------------------------------------------------------------------------
// §17.2 canonical → Hydra
// ---------------------------------------------------------------------------
void
TestCanonicalValuesReachHydra(const Session& session)
{
    // Authored, of each value type the schema uses.
    const VtValue base =
        ValueAt(session, "/mtl/Body", MaterialLocator("baseColorFactor"));
    assert(base.IsHolding<GfVec3f>() &&
           base.UncheckedGet<GfVec3f>() == GfVec3f(0.8f, 0.7f, 0.6f));
    assert(Near(FloatAt(session, "/mtl/Body", MaterialLocator("metallicFactor")),
                0.0f));
    assert(Near(FloatAt(session, "/mtl/Body", MaterialLocator("alphaCutoff")),
                0.25f));
    const VtValue mode =
        ValueAt(session, "/mtl/Body", MaterialLocator("alphaMode"));
    assert(mode.IsHolding<TfToken>() &&
           mode.UncheckedGet<TfToken>() == TfToken("MASK"));
    const VtValue doubleSided =
        ValueAt(session, "/mtl/Body", MaterialLocator("doubleSided"));
    assert(doubleSided.IsHolding<bool>() && doubleSided.UncheckedGet<bool>());

    // The field set is the schema's, named as the schema names it.
    const TfTokenVector fields = SchemaFields();
    assert(fields.size() == 10);
    const HdContainerDataSourceHandle group = HdContainerDataSource::Cast(
        HdContainerDataSource::Get(PrimData(session, "/mtl/Defaults"),
                                   HdDataSourceLocator(kVrm, kMaterialGroup)));
    assert(group && "no vrm/material container on a material with VrmMaterialAPI");
    TfTokenVector names = group->GetNames();
    std::sort(names.begin(), names.end());
    assert(names == fields);

    // Unauthored: the schema fallback, read from the definition rather than
    // written here. Every field has a value.
    for (const TfToken& field : fields) {
        const VtValue value = ValueAt(
            session, "/mtl/Defaults",
            HdDataSourceLocator(kVrm, kMaterialGroup, field));
        assert(!value.IsEmpty() && "an unauthored field has no value");
        VtValue fallback;
        if (Definition().GetAttributeFallbackValue(
                TfToken("inputs:vrm:material:" + field.GetString()),
                &fallback)) {
            assert(value == fallback);
        }
    }

    // alphaMode: no fallback in the definition (usdGenSchema would name its
    // token `OPAQUE`, a wingdi.h macro), so the value is the schema's
    // documented one, and typed as the authored attribute's is. The day the
    // schema carries the fallback, the loop above checks it instead.
    VtValue alphaModeFallback;
    if (!Definition().GetAttributeFallbackValue(
            TfToken("inputs:vrm:material:alphaMode"), &alphaModeFallback)) {
        const auto opaque = HdTypedSampledDataSource<TfToken>::Cast(
            HdContainerDataSource::Get(PrimData(session, "/mtl/Defaults"),
                                       MaterialLocator("alphaMode")));
        assert(opaque && opaque->GetTypedValue(0.0f) == TfToken("OPAQUE"));
    }

    // One `vrm` container, holding each applied schema's group: UsdImaging
    // overlays the two adapters' contributions.
    const HdContainerDataSourceHandle vrm = HdContainerDataSource::Cast(
        HdContainerDataSource::Get(PrimData(session, "/mtl/Body"),
                                   HdDataSourceLocator(kVrm)));
    assert(vrm);
    TfTokenVector groups = vrm->GetNames();
    std::sort(groups.begin(), groups.end());
    assert((groups == TfTokenVector{kMaterialGroup, kMToonGroup}));
    assert(Near(FloatAt(session, "/mtl/Body", VrmLocator("mtoon", "shadingToonyFactor")),
                0.35f));
    const HdContainerDataSourceHandle only = HdContainerDataSource::Cast(
        HdContainerDataSource::Get(PrimData(session, "/mtl/Defaults"),
                                   HdDataSourceLocator(kVrm)));
    assert((only->GetNames() == TfTokenVector{kMaterialGroup}));

    std::printf("canonical -> Hydra: %zu material fields beside mtoon\n",
                fields.size());
}

// ---------------------------------------------------------------------------
// §17.4 invalidation of an authored edit
// ---------------------------------------------------------------------------
void
TestAnEditDirtiesItsOwnLocator(Session& session)
{
    const struct {
        const char* field;
        VtValue value;
    } edits[] = {
        // Read by /mtl/Body's network.
        {"baseColorFactor", VtValue(GfVec3f(0.1f, 0.2f, 0.3f))},
        // Read by no network.
        {"metallicFactor", VtValue(0.5f)},
        {"alphaMode", VtValue(TfToken("BLEND"))},
        {"doubleSided", VtValue(false)},
    };
    for (const auto& edit : edits) {
        const std::string name = std::string("inputs:vrm:material:") + edit.field;
        session.recorder.Clear();
        assert(Attribute(session, "/mtl/Body", name.c_str()).Set(edit.value));
        session.sceneIndex->ApplyPendingUpdates();

        // The one VRM locator, and the whole `material` from UsdImaging
        // whether a network reads the input (baseColorFactor) or not (the
        // rest): §27 item 6, pinned so a runtime that narrows it is noticed.
        HdDataSourceLocatorSet expected;
        expected.insert(MaterialLocator(edit.field));
        expected.insert(HdMaterialSchema::GetDefaultLocator());
        const HdDataSourceLocatorSet got = session.recorder.At("/mtl/Body");
        if (got != expected) {
            PrintLocators(edit.field, got);
        }
        assert(got == expected);
        assert(session.recorder.dirtied.size() == 1);
        assert(ValueAt(session, "/mtl/Body", MaterialLocator(edit.field)) ==
               edit.value);
    }

    // Clearing the one field without a fallback returns its documented value
    // and dirties the same locator.
    session.recorder.Clear();
    assert(Attribute(session, "/mtl/Body", "inputs:vrm:material:alphaMode").Clear());
    session.sceneIndex->ApplyPendingUpdates();
    assert(session.recorder.At("/mtl/Body").Contains(MaterialLocator("alphaMode")));
    assert(ValueAt(session, "/mtl/Body", MaterialLocator("alphaMode")) ==
           VtValue(TfToken("OPAQUE")));

    std::printf("invalidation: one vrm locator per edit, plus UsdImaging's material\n");
}

// ---------------------------------------------------------------------------
// §17.5 time, and what the whole-material dirtying costs (§28)
// ---------------------------------------------------------------------------
void
TestTimeNeverDirtiesTheWholeMaterial(Session& session)
{
    // Read first: a data source is recorded as time-varying when it is built
    // (§27 item 4). A renderer reads what it draws before the time moves.
    for (const char* path : {"/mtl/Unread", "/mtl/Read"}) {
        const VtValue value =
            ValueAt(session, path, MaterialLocator("baseColorFactor"));
        assert(value == VtValue(GfVec3f(1.0f, 0.0f, 0.0f)));
    }
    const auto unreadParameters = ReadDiffuseColorParameters(session, "/mtl/Unread");
    const auto readParameters = ReadDiffuseColorParameters(session, "/mtl/Read");
    assert(!unreadParameters.empty() && !readParameters.empty());
    for (const auto& parameter : unreadParameters) {
        assert(parameter.second == VtValue(GfVec3f(0.5f, 0.5f, 0.5f)));
    }
    for (const auto& parameter : readParameters) {
        assert(parameter.second == VtValue(GfVec3f(1.0f, 0.0f, 0.0f)) &&
               "the network does not read the canonical input");
    }

    session.recorder.Clear();
    session.sceneIndex->SetTime(UsdTimeCode(10.0));

    // No network reads it: the VRM locator alone.
    HdDataSourceLocatorSet unread;
    unread.insert(MaterialLocator("baseColorFactor"));
    if (session.recorder.At("/mtl/Unread") != unread) {
        PrintLocators("time on /mtl/Unread", session.recorder.At("/mtl/Unread"));
    }
    assert(session.recorder.At("/mtl/Unread") == unread);

    // A network reads it: the VRM locator and that one parameter, never the
    // whole `material`.
    HdDataSourceLocatorSet read;
    read.insert(MaterialLocator("baseColorFactor"));
    for (const auto& parameter : readParameters) {
        read.insert(parameter.first);
    }
    const HdDataSourceLocatorSet got = session.recorder.At("/mtl/Read");
    if (got != read) {
        PrintLocators("time on /mtl/Read", got);
    }
    assert(got == read);
    assert(session.recorder.At("/mtl/Body").IsEmpty());

    for (const char* path : {"/mtl/Unread", "/mtl/Read"}) {
        assert(ValueAt(session, path, MaterialLocator("baseColorFactor")) ==
               VtValue(GfVec3f(0.0f, 0.0f, 1.0f)));
    }
    for (const auto& parameter : ReadDiffuseColorParameters(session, "/mtl/Read")) {
        assert(parameter.second == VtValue(GfVec3f(0.0f, 0.0f, 1.0f)));
    }
    std::printf("time: never the whole material; the network is dirtied at\n");
    for (const auto& parameter : readParameters) {
        std::printf("  %s\n", parameter.first.GetString().c_str());
    }
}

} // namespace

int
main(int argc, char** argv)
{
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <material_core.usda>\n", argv[0]);
        return 2;
    }
    TestTheAdapterIsDiscovered();
    Session session;
    Open(session, argv[1]);
    TestCanonicalValuesReachHydra(session);
    TestAnEditDirtiesItsOwnLocator(session);
    TestTimeNeverDirtiesTheWholeMaterial(session);
    std::printf("vrmImaging_material: passed\n");
    return 0;
}
