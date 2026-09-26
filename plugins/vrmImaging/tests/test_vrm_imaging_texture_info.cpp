// SPDX-License-Identifier: Apache-2.0
//
// vrmImaging Step I2 (VRM_IMAGING_POLICY.md §20, §29): `VrmTextureInfoAPI`,
// the multiple-apply texture schema, reaches a Hydra consumer through
// UsdImaging's own stage scene index, one role per applied instance, with
// enough to reconstruct the texture record and no image read.
//
// What is measured:
//
//   * discovery: UsdImaging knows an adapter for `VrmTextureInfoAPI` (§17.1);
//   * the record: every field of a role under
//     `vrm/textureInfo/<role>/<field>`, `transform:*` nested under
//     `transform`, authored values and schema fallbacks, and `file` as an
//     asset path both authored and resolved -- or absent when unauthored,
//     since it has no fallback (§28.1 item 3, §29);
//   * roles: all eleven the schema allows, each with its own image, so a role
//     is never read under another's name (§17.6); the literal list here is
//     checked against the schema's; a role the schema does not allow is not
//     exposed;
//   * invalidation: an authored edit dirties exactly its role's field -- and,
//     from UsdImaging, the whole `material` (§27 item 6) -- including a
//     nested field and a `file` appearing and disappearing;
//   * time: a time-sampled UV rotation dirties its one locator, and never
//     the whole `material` (§28.3).

#include "imaging_test_support.h"

#include "pxr/base/gf/vec2f.h"
#include "pxr/base/js/value.h"
#include "pxr/base/plug/registry.h"
#include "pxr/base/tf/stringUtils.h"
#include "pxr/base/tf/type.h"
#include "pxr/imaging/hd/materialSchema.h"
#include "pxr/usd/sdf/assetPath.h"
#include "pxr/usd/usd/primDefinition.h"
#include "pxr/usd/usd/schemaRegistry.h"
#include "pxr/usdImaging/usdImaging/adapterRegistry.h"

#include <algorithm>
#include <iterator>
#include <string>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE
using namespace vrm_imaging_test;

namespace {

const TfToken kTextureInfo("textureInfo");
const TfToken kSchema("VrmTextureInfoAPI");

// The eleven roles, spelled as a consumer spells them (§28.1 item 6) and
// checked against the schema's own list below.
const char* const kRoles[] = {
    "baseColor", "metallicRoughness", "normal", "occlusion", "emissive",
    "shadeMultiply", "shadingShift", "matcap", "rimMultiply",
    "outlineWidthMultiply", "uvAnimationMask",
};

HdDataSourceLocator
RoleLocator(const char* role)
{
    return HdDataSourceLocator(kVrm, kTextureInfo, TfToken(role));
}

HdDataSourceLocator
FieldLocator(const char* role, const char* field)
{
    HdDataSourceLocator locator = RoleLocator(role);
    for (const std::string& element : TfStringSplit(field, ":")) {
        locator = locator.Append(TfToken(element));
    }
    return locator;
}

TfTokenVector
SortedNames(const Session& session, const char* path,
            const HdDataSourceLocator& locator)
{
    const HdContainerDataSourceHandle container = HdContainerDataSource::Cast(
        HdContainerDataSource::Get(PrimData(session, path), locator));
    assert(container && "no container at the locator");
    TfTokenVector names = container->GetNames();
    std::sort(names.begin(), names.end());
    return names;
}

TfTokenVector
Tokens(std::vector<const char*> names)
{
    TfTokenVector result;
    for (const char* name : names) {
        result.emplace_back(name);
    }
    std::sort(result.begin(), result.end());
    return result;
}

SdfAssetPath
AssetAt(const Session& session, const char* path, const char* role)
{
    const auto file = HdTypedSampledDataSource<SdfAssetPath>::Cast(
        HdContainerDataSource::Get(PrimData(session, path),
                                   FieldLocator(role, "file")));
    assert(file && "no typed asset-path data source at the role's file");
    return file->GetTypedValue(0.0f);
}

const UsdPrimDefinition&
Definition()
{
    const UsdPrimDefinition* definition =
        UsdSchemaRegistry::GetInstance().FindAppliedAPIPrimDefinition(kSchema);
    assert(definition && "vrmSchema is not registered in this session");
    return *definition;
}

// The schema fallback of one field, which every role shares: the definition
// holds the multiple-apply template, not an instance.
VtValue
Fallback(const char* field)
{
    VtValue fallback;
    Definition().GetAttributeFallbackValue(
        UsdSchemaRegistry::MakeMultipleApplyNameTemplate(
            "inputs:vrm:textureInfo", field),
        &fallback);
    return fallback;
}

// ---------------------------------------------------------------------------
// §17.1 discovery
// ---------------------------------------------------------------------------
void
TestTheAdapterIsDiscovered()
{
    assert(UsdImagingAdapterRegistry::GetInstance().HasAPISchemaAdapter(kSchema) &&
           "UsdImaging registered no adapter for VrmTextureInfoAPI");
}

// ---------------------------------------------------------------------------
// §17.2 the texture record
// ---------------------------------------------------------------------------
void
TestTheRecordReachesHydra(const Session& session)
{
    // The roles applied, and only they.
    assert((SortedNames(session, "/mtl/Textured",
                        HdDataSourceLocator(kVrm, kTextureInfo)) ==
            Tokens({"baseColor", "matcap", "normal", "occlusion",
                    "shadeMultiply"})));
    // Beside the other groups, in one `vrm` container.
    assert((SortedNames(session, "/mtl/Textured", HdDataSourceLocator(kVrm)) ==
            Tokens({"material", "textureInfo"})));

    // Every field of the schema, authored on baseColor, `transform:*` nested.
    assert((SortedNames(session, "/mtl/Textured", RoleLocator("baseColor")) ==
            Tokens({"file", "scale", "strength", "texCoord", "transform",
                    "wrapS", "wrapT"})));
    assert((SortedNames(session, "/mtl/Textured",
                        FieldLocator("baseColor", "transform")) ==
            Tokens({"offset", "rotation", "scale"})));

    const SdfAssetPath base = AssetAt(session, "/mtl/Textured", "baseColor");
    assert(base.GetAssetPath() == "./textures/base.png");
    // Resolved by the session's resolver, and nothing more: no image is read.
    assert(TfStringEndsWith(base.GetResolvedPath(), "base.png") &&
           "an image that exists did not resolve");
    assert(ValueAt(session, "/mtl/Textured", FieldLocator("baseColor", "texCoord")) ==
           VtValue(1));
    assert(ValueAt(session, "/mtl/Textured", FieldLocator("baseColor", "wrapS")) ==
           VtValue(TfToken("clampToEdge")));
    assert(ValueAt(session, "/mtl/Textured", FieldLocator("baseColor", "wrapT")) ==
           VtValue(TfToken("mirroredRepeat")));
    assert(ValueAt(session, "/mtl/Textured",
                   FieldLocator("baseColor", "transform:offset")) ==
           VtValue(GfVec2f(0.25f, 0.125f)));
    assert(Near(FloatAt(session, "/mtl/Textured",
                        FieldLocator("baseColor", "transform:rotation")), 1.5f));
    assert(ValueAt(session, "/mtl/Textured",
                   FieldLocator("baseColor", "transform:scale")) ==
           VtValue(GfVec2f(2.0f, 4.0f)));

    // The contribution scale is its own field, apart from the UV scale.
    assert(Near(FloatAt(session, "/mtl/Textured", FieldLocator("normal", "scale")),
                0.5f));
    assert(ValueAt(session, "/mtl/Textured", FieldLocator("normal", "transform:scale")) ==
           Fallback("transform:scale"));

    // An image and nothing else: every other field is the schema's fallback.
    for (const char* field : {"texCoord", "wrapS", "wrapT", "scale", "strength",
                              "transform:offset", "transform:rotation",
                              "transform:scale"}) {
        const VtValue fallback = Fallback(field);
        assert(!fallback.IsEmpty());
        assert(ValueAt(session, "/mtl/Textured",
                       FieldLocator("shadeMultiply", field)) == fallback);
    }

    // An image that does not resolve is still the canonical path.
    const SdfAssetPath missing = AssetAt(session, "/mtl/Textured", "matcap");
    assert(missing.GetAssetPath() == "./textures/missing.png");
    assert(missing.GetResolvedPath().empty());

    // No image: `file` has no fallback, so it is absent, not empty.
    assert((SortedNames(session, "/mtl/Textured", RoleLocator("occlusion")) ==
            Tokens({"scale", "strength", "texCoord", "transform", "wrapS",
                    "wrapT"})));
    assert(!HdContainerDataSource::Get(PrimData(session, "/mtl/Textured"),
                                       FieldLocator("occlusion", "file")));
    assert(Near(FloatAt(session, "/mtl/Textured",
                        FieldLocator("occlusion", "strength")), 0.75f));

    // No texture, no group.
    assert((SortedNames(session, "/mtl/Untextured", HdDataSourceLocator(kVrm)) ==
            Tokens({"material"})));

    std::printf("the record: every field of a role, file as an asset path\n");
}

// ---------------------------------------------------------------------------
// §17.6 every allowed role, and only those
// ---------------------------------------------------------------------------
void
TestEveryRoleKeepsItsIdentity(const Session& session)
{
    // The literal list is the schema's.
    const TfType type = TfType::FindByName("UsdVrmTextureInfoAPI");
    const JsValue allowed = PlugRegistry::GetInstance().GetDataFromPluginMetaData(
        type, "apiSchemaAllowedInstanceNames");
    assert(allowed.IsArrayOf<std::string>());
    std::vector<std::string> schemaRoles = allowed.GetArrayOf<std::string>();
    std::vector<std::string> literalRoles(std::begin(kRoles), std::end(kRoles));
    std::sort(schemaRoles.begin(), schemaRoles.end());
    std::sort(literalRoles.begin(), literalRoles.end());
    assert(schemaRoles == literalRoles && "the schema's roles changed");

    std::vector<const char*> roles(std::begin(kRoles), std::end(kRoles));
    assert(SortedNames(session, "/mtl/AllRoles",
                       HdDataSourceLocator(kVrm, kTextureInfo)) == Tokens(roles));
    for (const char* role : kRoles) {
        assert(UsdSchemaRegistry::IsAllowedAPISchemaInstanceName(kSchema,
                                                                 TfToken(role)));
        const SdfAssetPath file = AssetAt(session, "/mtl/AllRoles", role);
        assert(file.GetAssetPath() == std::string("./textures/") + role + ".png" &&
               "a role was read under another role's name");
    }

    // A role the schema does not allow is not canonical data.
    assert(!UsdSchemaRegistry::IsAllowedAPISchemaInstanceName(kSchema,
                                                              TfToken("bogus")));
    assert((SortedNames(session, "/mtl/Disallowed",
                        HdDataSourceLocator(kVrm, kTextureInfo)) ==
            Tokens({"baseColor"})));

    std::printf("roles: all %zu allowed roles, each under its own name\n",
                roles.size());
}

// ---------------------------------------------------------------------------
// §17.4 invalidation of an authored edit
// ---------------------------------------------------------------------------
void
ExpectEditDirties(Session& session, const char* role, const char* field)
{
    HdDataSourceLocatorSet expected;
    expected.insert(FieldLocator(role, field));
    expected.insert(HdMaterialSchema::GetDefaultLocator());
    const HdDataSourceLocatorSet got = session.recorder.At("/mtl/Textured");
    if (got != expected) {
        PrintLocators(field, got);
    }
    assert(got == expected);
    assert(session.recorder.dirtied.size() == 1);
}

void
TestAnEditDirtiesItsOwnLocator(Session& session)
{
    const struct {
        const char* role;
        const char* field;
        VtValue value;
    } edits[] = {
        {"baseColor", "wrapS", VtValue(TfToken("repeat"))},
        {"baseColor", "transform:offset", VtValue(GfVec2f(0.5f, 0.5f))},
        {"normal", "scale", VtValue(0.25f)},
        // A field left to its fallback, authored for the first time.
        {"shadeMultiply", "texCoord", VtValue(2)},
    };
    for (const auto& edit : edits) {
        const std::string name = std::string("inputs:vrm:textureInfo:") +
                                 edit.role + ":" + edit.field;
        session.recorder.Clear();
        assert(Attribute(session, "/mtl/Textured", name.c_str()).Set(edit.value));
        session.sceneIndex->ApplyPendingUpdates();
        ExpectEditDirties(session, edit.role, edit.field);
        assert(ValueAt(session, "/mtl/Textured",
                       FieldLocator(edit.role, edit.field)) == edit.value);
    }

    // A file appearing on a role that had none: its one locator, and the name
    // is listed from then on.
    const UsdPrim prim =
        session.stage->GetPrimAtPath(SdfPath("/mtl/Textured"));
    const UsdAttribute occlusionFile = prim.GetAttribute(
        TfToken("inputs:vrm:textureInfo:occlusion:file"));
    assert(occlusionFile && !occlusionFile.HasValue());
    session.recorder.Clear();
    assert(occlusionFile.Set(SdfAssetPath("./textures/base.png")));
    session.sceneIndex->ApplyPendingUpdates();
    ExpectEditDirties(session, "occlusion", "file");
    assert(AssetAt(session, "/mtl/Textured", "occlusion").GetAssetPath() ==
           "./textures/base.png");

    // And disappearing again.
    session.recorder.Clear();
    assert(occlusionFile.Clear());
    session.sceneIndex->ApplyPendingUpdates();
    ExpectEditDirties(session, "occlusion", "file");
    assert(!HdContainerDataSource::Get(PrimData(session, "/mtl/Textured"),
                                       FieldLocator("occlusion", "file")));

    std::printf("invalidation: one role field per edit, plus UsdImaging's material\n");
}

// ---------------------------------------------------------------------------
// §17.5 time
// ---------------------------------------------------------------------------
void
TestTimeDirtiesTheOneField(Session& session)
{
    // Read first: a data source is recorded as time-varying when it is built
    // (§27 item 4).
    assert(Near(FloatAt(session, "/mtl/Animated",
                        FieldLocator("uvAnimationMask", "transform:rotation")),
                0.0f));

    session.recorder.Clear();
    session.sceneIndex->SetTime(UsdTimeCode(10.0));

    HdDataSourceLocatorSet expected;
    expected.insert(FieldLocator("uvAnimationMask", "transform:rotation"));
    const HdDataSourceLocatorSet got = session.recorder.At("/mtl/Animated");
    if (got != expected) {
        PrintLocators("time on /mtl/Animated", got);
    }
    assert(got == expected);
    assert(session.recorder.At("/mtl/Textured").IsEmpty());
    assert(Near(FloatAt(session, "/mtl/Animated",
                        FieldLocator("uvAnimationMask", "transform:rotation")),
                3.0f));
    std::printf("time: the one nested field, never the whole material\n");
}

} // namespace

int
main(int argc, char** argv)
{
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <texture_info.usda>\n", argv[0]);
        return 2;
    }
    TestTheAdapterIsDiscovered();
    Session session;
    Open(session, argv[1]);
    TestTheRecordReachesHydra(session);
    TestEveryRoleKeepsItsIdentity(session);
    TestAnEditDirtiesItsOwnLocator(session);
    TestTimeDirtiesTheOneField(session);
    std::printf("vrmImaging_texture_info: passed\n");
    return 0;
}
