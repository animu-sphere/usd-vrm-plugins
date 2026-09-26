// SPDX-License-Identifier: Apache-2.0
//
// vrmImaging Step I0 (VRM_IMAGING_POLICY.md §20, §25): a `VrmMToonAPI` value
// reaches a Hydra consumer through UsdImaging's own stage scene index, and an
// edit of it dirties the one locator it feeds.
//
// The consumer here is the scene index a Hydra renderer is handed. It reads
// prims and data sources and observes notices; it never touches the stage. The
// plugin is not linked: it is found through PXR_PLUGINPATH_NAME and the staged
// plugInfo.json, and loaded by UsdImaging's adapter registry when the first prim
// with `VrmMToonAPI` is populated -- the path a packaged plugin takes.
//
// What is measured:
//
//   * discovery: the plugin is registered and UsdImaging knows an adapter for
//     `VrmMToonAPI` (§17.1);
//   * canonical → Hydra: authored values and schema fallbacks under
//     `vrm/mtoon/<field>`, the field set being the schema's and nothing else,
//     beside a `material` container the adapter leaves alone (§6.1, §17.2);
//   * invalidation: an edit dirties exactly `vrm/mtoon/<field>` of the VRM
//     contribution, and a property the schema does not define dirties nothing
//     under `vrm` (§9, §17.4). UsdImaging's own material adapter dirties the
//     whole `material` locator beside it on every such edit -- see
//     TestAnEditDirtiesItsOwnLocator;
//   * time samples: moving the scene index's time dirties the sampled field
//     and nothing else, and the value follows (§10, §17.5);
//   * with `--without-schema`, under an environment that registers no
//     vrmSchema: no contribution at all, which is the plugin's one runtime
//     requirement on that bundle, measured by leaving it unmet.

#include "imaging_test_support.h"

#include "pxr/base/gf/vec3f.h"
#include "pxr/base/plug/plugin.h"
#include "pxr/base/plug/registry.h"
#include "pxr/imaging/hd/materialSchema.h"
#include "pxr/imaging/hd/tokens.h"
#include "pxr/usd/usd/primDefinition.h"
#include "pxr/usd/usd/schemaRegistry.h"
#include "pxr/usdImaging/usdImaging/adapterRegistry.h"

PXR_NAMESPACE_USING_DIRECTIVE
using namespace vrm_imaging_test;

namespace {

const TfToken kMToon("mtoon");

HdDataSourceLocator
MToonLocator(const char* field)
{
    return VrmLocator("mtoon", field);
}

float
FloatAt(const Session& session, const char* path, const char* field)
{
    return vrm_imaging_test::FloatAt(session, path, MToonLocator(field));
}

// ---------------------------------------------------------------------------
// §17.1 discovery
// ---------------------------------------------------------------------------
void
TestTheAdapterIsDiscovered()
{
    const PlugPluginPtr plugin =
        PlugRegistry::GetInstance().GetPluginWithName("VrmImaging");
    assert(plugin && "PXR_PLUGINPATH_NAME does not reach vrmImaging's plugInfo.json");
    assert(UsdImagingAdapterRegistry::GetInstance().HasAPISchemaAdapter(
               TfToken("VrmMToonAPI")) &&
           "UsdImaging registered no adapter for VrmMToonAPI");
}

// ---------------------------------------------------------------------------
// §17.2 canonical → Hydra
// ---------------------------------------------------------------------------
void
TestCanonicalValuesReachHydra(const Session& session)
{
    const HdSceneIndexPrim hair =
        session.sceneIndex->GetPrim(SdfPath("/mtl/Hair"));
    assert(hair.primType == HdPrimTypeTokens->material);

    // Authored, of each value type the schema uses.
    assert(Near(FloatAt(session, "/mtl/Hair", "shadingToonyFactor"), 0.35f));
    const VtValue shade =
        ValueAt(session, "/mtl/Hair", MToonLocator("shadeColorFactor"));
    assert(shade.IsHolding<GfVec3f>() &&
           shade.UncheckedGet<GfVec3f>() == GfVec3f(0.6f, 0.5f, 0.55f));
    const VtValue mode =
        ValueAt(session, "/mtl/Hair", MToonLocator("outlineWidthMode"));
    assert(mode.IsHolding<TfToken>() &&
           mode.UncheckedGet<TfToken>() == TfToken("worldCoordinates"));

    // Unauthored: the schema fallback, so a consumer never restates the
    // schema's defaults. Read from the definition rather than written here.
    const UsdPrimDefinition* definition =
        UsdSchemaRegistry::GetInstance().FindAppliedAPIPrimDefinition(
            TfToken("VrmMToonAPI"));
    assert(definition);
    VtValue fallback;
    assert(definition->GetAttributeFallbackValue(
        TfToken("inputs:vrm:mtoon:giEqualizationFactor"), &fallback));
    assert(Near(FloatAt(session, "/mtl/Hair", "giEqualizationFactor"),
                fallback.Get<float>()));

    // The field set is the schema's, named as the schema names it: every
    // `inputs:vrm:mtoon:<field>` of the definition, and no property the prim
    // merely authors under that prefix.
    const HdContainerDataSourceHandle mtoon = HdContainerDataSource::Cast(
        HdContainerDataSource::Get(hair.dataSource,
                                   HdDataSourceLocator(kVrm, kMToon)));
    assert(mtoon && "no vrm/mtoon container on a material with VrmMToonAPI");
    size_t schemaFields = 0;
    for (const TfToken& name : definition->GetPropertyNames()) {
        const std::string prefix = "inputs:vrm:mtoon:";
        if (name.GetString().compare(0, prefix.size(), prefix) == 0) {
            ++schemaFields;
            const TfToken field(name.GetString().substr(prefix.size()));
            assert(mtoon->Get(field) && "a schema field is not exposed");
        }
    }
    const TfTokenVector names = mtoon->GetNames();
    assert(names.size() == schemaFields && schemaFields == 19);
    assert(!mtoon->Get(TfToken("notASchemaField")));

    // The realization beside it is UsdImaging's, untouched.
    assert(HdContainerDataSource::Get(hair.dataSource,
                                      HdMaterialSchema::GetDefaultLocator()) &&
           "the material network is gone");

    // A material without the schema carries no VRM contribution.
    assert(!HdContainerDataSource::Get(PrimData(session, "/mtl/Plain"),
                                       HdDataSourceLocator(kVrm)));
    std::printf("canonical -> Hydra: %zu fields, authored and fallback\n",
                names.size());
}

// ---------------------------------------------------------------------------
// §17.4 invalidation
// ---------------------------------------------------------------------------
void
TestAnEditDirtiesItsOwnLocator(Session& session)
{
    const struct {
        const char* field;
        VtValue value;
    } edits[] = {
        {"shadingToonyFactor", VtValue(0.5f)},
        {"shadeColorFactor", VtValue(GfVec3f(0.1f, 0.2f, 0.3f))},
        {"outlineWidthFactor", VtValue(0.004f)},
    };
    for (const auto& edit : edits) {
        const std::string name = std::string("inputs:vrm:mtoon:") + edit.field;
        session.recorder.Clear();
        assert(Attribute(session, "/mtl/Hair", name.c_str()).Set(edit.value));
        session.sceneIndex->ApplyPendingUpdates();

        // The one VRM locator -- not `vrm/mtoon`, not `vrm` -- and the whole
        // `material`, which is not this plugin's. OpenUSD 26.08's material
        // prim dirties its entire network whenever any interface input
        // changes, connected or not (UsdImagingDataSourceMaterialPrim::
        // Invalidate: "TODO, invalidate specifically connected node
        // parameters. FOR NOW: just dirty the whole material"), and every
        // canonical attribute is an interface input (material policy
        // §6.4.1). Pinned here so a runtime that narrows it is noticed: the
        // network on this prim reads no canonical input, so the narrow answer
        // is the VRM locator alone.
        HdDataSourceLocatorSet expected;
        expected.insert(MToonLocator(edit.field));
        expected.insert(HdMaterialSchema::GetDefaultLocator());
        const HdDataSourceLocatorSet got = session.recorder.At("/mtl/Hair");
        if (got != expected) {
            PrintLocators(edit.field, got);
        }
        assert(got == expected);
        assert(session.recorder.dirtied.size() == 1);
        assert(ValueAt(session, "/mtl/Hair", MToonLocator(edit.field)) ==
               edit.value);
    }

    // Under the prefix, but not a field of the schema.
    session.recorder.Clear();
    assert(Attribute(session, "/mtl/Hair", "inputs:vrm:mtoon:notASchemaField")
               .Set(2.0f));
    session.sceneIndex->ApplyPendingUpdates();
    assert(!session.recorder.At("/mtl/Hair").Intersects(
        HdDataSourceLocator(kVrm)));

    std::printf("invalidation: one locator per edit\n");
}

// ---------------------------------------------------------------------------
// §17.5 time samples
// ---------------------------------------------------------------------------
void
TestATimeSampledFieldFollowsTime(Session& session)
{
    // Read first: UsdImaging records a field as time-varying when its data
    // source is built, so a field no consumer has pulled is never dirtied by
    // time. A renderer reads what it draws before the time moves.
    assert(Near(FloatAt(session, "/mtl/Animated", "shadingShiftFactor"), -0.2f));
    assert(Near(FloatAt(session, "/mtl/Hair", "shadingToonyFactor"), 0.5f));

    session.recorder.Clear();
    session.sceneIndex->SetTime(UsdTimeCode(10.0));
    HdDataSourceLocatorSet expected;
    expected.insert(MToonLocator("shadingShiftFactor"));
    assert(session.recorder.At("/mtl/Animated") == expected);
    assert(session.recorder.At("/mtl/Hair").IsEmpty() &&
           "time dirtied a field that has no time samples");
    assert(Near(FloatAt(session, "/mtl/Animated", "shadingShiftFactor"), 0.4f));

    session.sceneIndex->SetTime(UsdTimeCode(5.0));
    assert(Near(FloatAt(session, "/mtl/Animated", "shadingShiftFactor"), 0.1f));
    std::printf("time samples: sampled, not copied\n");
}

// ---------------------------------------------------------------------------
// --without-schema: vrmSchema is not in the session
// ---------------------------------------------------------------------------
void
TestWithoutTheSchemaThereIsNoContribution(const std::string& fixture)
{
    assert(!UsdSchemaRegistry::GetInstance().FindAppliedAPIPrimDefinition(
               TfToken("VrmMToonAPI")) &&
           "vrmSchema is registered after all, so this run measures nothing");
    // The adapter is registered; it is simply never asked, because no prim's
    // definition includes an API schema the registry does not know.
    TestTheAdapterIsDiscovered();

    Session session;
    Open(session, fixture);
    assert(!HdContainerDataSource::Get(PrimData(session, "/mtl/Hair"),
                                       HdDataSourceLocator(kVrm)));
    assert(HdContainerDataSource::Get(PrimData(session, "/mtl/Hair"),
                                      HdMaterialSchema::GetDefaultLocator()));
    std::printf("without vrmSchema: no vrm contribution\n");
}

} // namespace

int
main(int argc, char** argv)
{
    if (argc < 2) {
        std::fprintf(stderr,
                     "usage: %s <mtoon_materials.usda> [--without-schema]\n",
                     argv[0]);
        return 2;
    }
    const std::string fixture = argv[1];
    if (argc > 2 && std::string(argv[2]) == "--without-schema") {
        TestWithoutTheSchemaThereIsNoContribution(fixture);
        std::printf("vrmImaging_mtoon_without_schema: passed\n");
        return 0;
    }

    TestTheAdapterIsDiscovered();
    Session session;
    Open(session, fixture);
    TestCanonicalValuesReachHydra(session);
    TestAnEditDirtiesItsOwnLocator(session);
    TestATimeSampledFieldFollowsTime(session);
    std::printf("vrmImaging_mtoon: passed\n");
    return 0;
}
