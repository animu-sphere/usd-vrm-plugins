// SPDX-License-Identifier: Apache-2.0
//
// vrmImaging Step I4 (VRM_IMAGING_POLICY.md §10, §17.5, §28.3): an expression
// bake reaches Hydra as time.
//
// The stage is `motion_retarget`'s output over the importer's `expressions.vrm`
// fixture: its `happy` expression binds `emissionColor` of `Face_Mat` to red,
// and the bake writes that slot as time samples on the Material's canonical
// `inputs:vrm:material:emissiveFactor` (Product P5 Step 7). Nothing here bakes
// or imports; the root CMakeLists.txt runs the tool and hands this suite the
// layer it wrote, with usdVrmFileFormat, vrmSchema and vrmImaging registered.
//
// What is measured:
//
//   * values: at each baked time the `vrm/material/emissiveFactor` data source
//     is the bake's sample, and so is every network parameter that reads the
//     slot -- `/preview`'s and `/mtlx`'s, which connect to it;
//   * invalidation: a time move dirties that `vrm` locator and those reading
//     parameters, and nothing else of the material -- never the whole
//     `material`, which only an authored edit costs (§28.3) -- and no locator
//     of the mesh the material is bound to.
//
// That is §28.3's condition for living with the whole-material dirtying met by
// the one producer of per-frame material values the workspace has: they arrive
// as time samples, not as per-frame authored edits.

#include "imaging_test_support.h"

#include "pxr/base/gf/vec3f.h"
#include "pxr/imaging/hd/materialNodeParameterSchema.h"
#include "pxr/imaging/hd/materialNodeSchema.h"

#include <string>
#include <utility>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE
using namespace vrm_imaging_test;

namespace {

// The importer's paths for the fixture, under the bake's `Asset` root.
const char* const kMaterial = "/Asset/mtl/Face_Mat";
const char* const kMesh = "/Asset/geo/Face";

// `expressive_clip.usda` keys `happy` at 0, 0.5 and 1.5 at 0, 15 and 30; the
// bake clamps the last to 1. The slot's rest value is the fixture's black
// emission, so the sample is the weight times the bind's red.
const std::pair<double, GfVec3f> kBakedSamples[] = {
    {0.0, GfVec3f(0.0f, 0.0f, 0.0f)},
    {15.0, GfVec3f(0.5f, 0.0f, 0.0f)},
    {30.0, GfVec3f(1.0f, 0.0f, 0.0f)},
};

// The shader inputs each realization drives from the slot.
const TfToken kReadingParameters[] = {
    TfToken("emissiveColor"), // UsdPreviewSurface, /preview
    TfToken("emissive"),      // ND_standard_surface_surfaceshader, /mtlx
};

using Sampled = std::vector<std::pair<HdDataSourceLocator, VtValue>>;

// Every sampled data source under `at`, which builds each one: a data source
// is recorded as time-varying when it is built (§27 item 4), so a renderer
// that has drawn the prim once has done at least this much.
void
ReadAll(const HdDataSourceBaseHandle& source, const HdDataSourceLocator& at,
        Sampled* out)
{
    if (const auto container = HdContainerDataSource::Cast(source)) {
        for (const TfToken& name : container->GetNames()) {
            ReadAll(container->Get(name), at.Append(name), out);
        }
    } else if (const auto vector = HdVectorDataSource::Cast(source)) {
        for (size_t i = 0; i < vector->GetNumElements(); ++i) {
            ReadAll(vector->GetElement(i), at.Append(TfToken(std::to_string(i))),
                    out);
        }
    } else if (const auto sampled = HdSampledDataSource::Cast(source)) {
        out->emplace_back(at, sampled->GetValue(0.0f));
    }
}

Sampled
ReadAll(const Session& session, const char* path)
{
    Sampled result;
    ReadAll(PrimData(session, path), HdDataSourceLocator(), &result);
    assert(!result.empty() && "the prim has no data");
    return result;
}

bool
IsReadingParameter(const HdDataSourceLocator& locator)
{
    // material/<context>/nodes/<node>/parameters/<name>/value
    const size_t n = locator.GetElementCount();
    if (n < 7 ||
        locator.GetFirstElement() != HdMaterialSchema::GetSchemaToken() ||
        locator.GetElement(n - 1) != HdMaterialNodeParameterSchemaTokens->value ||
        locator.GetElement(n - 3) != HdMaterialNodeSchemaTokens->parameters) {
        return false;
    }
    for (const TfToken& name : kReadingParameters) {
        if (locator.GetElement(n - 2) == name) {
            return true;
        }
    }
    return false;
}

GfVec3f
Color(const VtValue& value, const char* what)
{
    if (!value.IsHolding<GfVec3f>()) {
        std::fprintf(stderr, "%s is %s, not a color3f\n", what,
                     value.GetTypeName().c_str());
    }
    assert(value.IsHolding<GfVec3f>());
    return value.UncheckedGet<GfVec3f>();
}

bool
Near(const GfVec3f& a, const GfVec3f& b)
{
    return vrm_imaging_test::Near(a[0], b[0]) &&
           vrm_imaging_test::Near(a[1], b[1]) &&
           vrm_imaging_test::Near(a[2], b[2]);
}

void
CheckValuesAt(const Session& session, const GfVec3f& want,
              const std::vector<HdDataSourceLocator>& readers, double time)
{
    const GfVec3f slot = Color(
        ValueAt(session, kMaterial, VrmLocator("material", "emissiveFactor")),
        "vrm/material/emissiveFactor");
    if (!Near(slot, want)) {
        std::fprintf(stderr, "at %g: vrm/material/emissiveFactor (%g, %g, %g)\n",
                     time, slot[0], slot[1], slot[2]);
    }
    assert(Near(slot, want) && "the slot is not the bake's sample");
    for (const HdDataSourceLocator& reader : readers) {
        const GfVec3f value =
            Color(ValueAt(session, kMaterial, reader), reader.GetString().c_str());
        if (!Near(value, want)) {
            std::fprintf(stderr, "at %g: %s (%g, %g, %g)\n", time,
                         reader.GetString().c_str(), value[0], value[1], value[2]);
        }
        assert(Near(value, want) && "a realization does not follow the slot");
    }
}

// ---------------------------------------------------------------------------
// §17.5: the bake, through the scene index, at each of its times
// ---------------------------------------------------------------------------
void
TestTheBakeReachesHydraAsTime(Session& session)
{
    // Draw the material and the mesh once, as a renderer would before any
    // frame moves the time.
    const Sampled material = ReadAll(session, kMaterial);
    ReadAll(session, kMesh);

    std::vector<HdDataSourceLocator> readers;
    bool preview = false;
    bool mtlx = false;
    for (const auto& [locator, value] : material) {
        if (IsReadingParameter(locator)) {
            readers.push_back(locator);
            preview |= locator.GetElement(locator.GetElementCount() - 2) ==
                       kReadingParameters[0];
            mtlx |= locator.GetElement(locator.GetElementCount() - 2) ==
                    kReadingParameters[1];
        }
    }
    // Both realizations read the slot; a bake that reached one of them would
    // be a bake the other renderer never sees.
    assert(preview && "no /preview parameter reads the emission slot");
    assert(mtlx && "no /mtlx parameter reads the emission slot");

    CheckValuesAt(session, kBakedSamples[0].second, readers, kBakedSamples[0].first);

    for (size_t i = 1; i < std::size(kBakedSamples); ++i) {
        const auto& [time, want] = kBakedSamples[i];
        session.recorder.Clear();
        session.sceneIndex->SetTime(UsdTimeCode(time));

        HdDataSourceLocatorSet expected;
        expected.insert(VrmLocator("material", "emissiveFactor"));
        for (const HdDataSourceLocator& reader : readers) {
            expected.insert(reader);
        }
        const HdDataSourceLocatorSet got = session.recorder.At(kMaterial);
        if (got != expected) {
            PrintLocators("time on the baked material", got);
        }
        assert(got == expected);
        for (const HdDataSourceLocator& locator : got) {
            assert(locator != HdMaterialSchema::GetDefaultLocator() &&
                   "a time move dirtied the whole material");
        }

        const HdDataSourceLocatorSet mesh = session.recorder.At(kMesh);
        if (!mesh.IsEmpty()) {
            PrintLocators("time on the bound mesh", mesh);
        }
        assert(mesh.IsEmpty() && "a material time move dirtied the mesh");

        CheckValuesAt(session, want, readers, time);
    }

    std::printf("expression bake -> Hydra: %zu samples; a time move dirties\n",
                std::size(kBakedSamples));
    std::printf("  vrm/material/emissiveFactor\n");
    for (const HdDataSourceLocator& reader : readers) {
        std::printf("  %s\n", reader.GetString().c_str());
    }
}

} // namespace

int
main(int argc, char** argv)
{
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <expression_bake.usda>\n", argv[0]);
        return 2;
    }
    Session session;
    Open(session, argv[1]);
    TestTheBakeReachesHydraAsTime(session);
    std::printf("vrmImaging_expression_bake: passed\n");
    return 0;
}
