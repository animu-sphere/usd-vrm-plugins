// SPDX-License-Identifier: Apache-2.0
//
// Reads the canonical material semantics of a hand-authored stage through the
// generated C++ API -- not through JSON, and not through attribute names typed
// at the call site (material policy §7.3; the "done when" of Product P5
// Step 3). The stage is tests/fixtures/basic.usda, whose /Asset/mtl/Hair
// applies VrmMaterialAPI, VrmMToonAPI and two VrmTextureInfoAPI instances.
//
// The Python test beside this one owns the registry-level contract (field
// names, fallbacks, instance names, connectability). What only C++ can show is
// that the generated classes a consumer links -- hydra-toon, the importer --
// resolve the same properties: a multiple-apply accessor that built the wrong
// instance-qualified name would pass every Python check and read nothing here.
//
// Usage: vrmschema_material_api <path/to/basic.usda>
#include <vrmSchema/tokens.h>
#include <vrmSchema/vrmMToonAPI.h>
#include <vrmSchema/vrmMaterialAPI.h>
#include <vrmSchema/vrmTextureInfoAPI.h>

#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/usd/usd/stage.h>

#include <cmath>
#include <cstdio>
#include <string>

PXR_NAMESPACE_USING_DIRECTIVE

namespace
{

int g_failures = 0;

void
_Check(bool ok, const char* what)
{
    std::printf("[%s] %s\n", ok ? "ok" : "FAIL", what);
    if (!ok)
        ++g_failures;
}

bool
_Near(float a, float b)
{
    return std::fabs(a - b) < 1e-6f;
}

bool
_Near(const GfVec3f& a, const GfVec3f& b)
{
    return _Near(a[0], b[0]) && _Near(a[1], b[1]) && _Near(a[2], b[2]);
}

} // namespace

int
main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::fprintf(stderr, "usage: %s <basic.usda>\n", argv[0]);
        return 2;
    }
    UsdStageRefPtr stage = UsdStage::Open(argv[1]);
    if (!stage)
    {
        std::fprintf(stderr, "cannot open %s\n", argv[1]);
        return 2;
    }
    const UsdPrim hair = stage->GetPrimAtPath(SdfPath("/Asset/mtl/Hair"));
    _Check(hair.IsValid(), "/Asset/mtl/Hair exists");

    // VrmMToonAPI: authored values, and a fallback that is the
    // VRMC_materials_mtoon 1.0 default.
    const UsdVrmMToonAPI mtoon(hair);
    _Check(hair.HasAPI<UsdVrmMToonAPI>(), "Hair has VrmMToonAPI");
    float toony = 0.0f;
    _Check(mtoon.GetShadingToonyFactorAttr().Get(&toony) && _Near(toony, 0.95f),
           "shadingToonyFactor reads 0.95 through the typed API");
    GfVec3f shade(0.0f);
    _Check(mtoon.GetShadeColorFactorAttr().Get(&shade) && _Near(shade, GfVec3f(0.6f, 0.5f, 0.55f)),
           "shadeColorFactor reads through the typed API");
    TfToken widthMode;
    _Check(mtoon.GetOutlineWidthModeAttr().Get(&widthMode) &&
               widthMode == TfToken("worldCoordinates"),
           "outlineWidthMode reads 'worldCoordinates'");
    float giEq = 0.0f;
    _Check(!mtoon.GetGiEqualizationFactorAttr().HasAuthoredValue() &&
               mtoon.GetGiEqualizationFactorAttr().Get(&giEq) && _Near(giEq, 0.9f),
           "unauthored giEqualizationFactor falls back to the spec default 0.9");
    _Check(mtoon.GetShadeColorFactorAttr().GetName() ==
               UsdVrmTokens->inputsVrmMtoonShadeColorFactor,
           "shadeColorFactor is spelled inputs:vrm:mtoon:shadeColorFactor");

    // VrmMaterialAPI.
    const UsdVrmMaterialAPI material(hair);
    _Check(hair.HasAPI<UsdVrmMaterialAPI>(), "Hair has VrmMaterialAPI");
    TfToken alphaMode;
    _Check(material.GetAlphaModeAttr().Get(&alphaMode) && alphaMode == TfToken("MASK"),
           "alphaMode reads 'MASK'");
    bool unlit = false;
    _Check(material.GetUnlitAttr().Get(&unlit) && unlit, "unlit reads true");

    // VrmTextureInfoAPI: one instance per role, each resolving its own
    // instance-qualified properties.
    const UsdVrmTextureInfoAPI baseColor(hair, TfToken("baseColor"));
    const UsdVrmTextureInfoAPI shadeMultiply(hair, TfToken("shadeMultiply"));
    _Check(hair.HasAPI<UsdVrmTextureInfoAPI>(TfToken("baseColor")),
           "Hair has VrmTextureInfoAPI:baseColor");
    GfVec2f uvScale(0.0f);
    _Check(baseColor.GetTransformScaleAttr().Get(&uvScale) && uvScale == GfVec2f(2.0f, 1.0f),
           "baseColor transform:scale reads (2, 1)");
    _Check(baseColor.GetTransformScaleAttr().GetName() ==
               TfToken("inputs:vrm:textureInfo:baseColor:transform:scale"),
           "the instance property carries the role in its name");
    TfToken wrapS;
    _Check(shadeMultiply.GetWrapSAttr().Get(&wrapS) && wrapS == TfToken("clampToEdge"),
           "shadeMultiply wrapS reads 'clampToEdge'");
    _Check(UsdVrmTextureInfoAPI::GetAll(hair).size() == 2, "GetAll finds the two applied roles");

    // The apply rules the schema declares.
    std::string whyNot;
    const UsdPrim scope = stage->GetPrimAtPath(SdfPath("/Asset/rig"));
    _Check(!UsdVrmMToonAPI::CanApply(scope, &whyNot), "VrmMToonAPI cannot apply to a non-Material");
    _Check(!UsdVrmTextureInfoAPI::CanApply(hair, TfToken("outlineWidth"), &whyNot),
           "outlineWidth is not an allowed texture role");
    _Check(UsdVrmTextureInfoAPI::CanApply(hair, TfToken("outlineWidthMultiply"), &whyNot),
           "outlineWidthMultiply is an allowed texture role");

    if (g_failures)
    {
        std::printf("\n%d check(s) FAILED\n", g_failures);
        return 1;
    }
    std::printf("\nall checks passed\n");
    return 0;
}
