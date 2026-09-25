// SPDX-License-Identifier: Apache-2.0
//
// /preview is a function of a material's canonical attributes (material
// policy §6.5, P5 Step 5): delete it from an imported stage and regenerate it
// from the Material's VrmMaterialAPI / VrmTextureInfoAPI attributes alone, and
// the graph comes back as the importer wrote it.
//
// The stage is flattened first, so nothing of the source .vrm, the file format
// or the importer is reachable when the graph is regenerated: only the stage.
// A second check changes a canonical value and regenerates, so a generator that
// ignored its input and reproduced a remembered graph would fail.
//
// Usage: test_preview_regenerate <dir-with-.vrm> [<dir> ...]
// Needs the usdVrmFileFormat plugin (and vrmSchema) on PXR_PLUGINPATH_NAME to
// open the .vrm inputs.

#include "usd/MaterialSemantics.h"
#include "usd/PreviewRealization.h"

#include <vrmSchema/vrmMaterialAPI.h>

#include "pxr/base/gf/vec3f.h"
#include "pxr/usd/sdf/copyUtils.h"
#include "pxr/usd/sdf/layer.h"
#include "pxr/usd/usd/primRange.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usdShade/material.h"
#include "pxr/usd/usdShade/shader.h"

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

namespace
{

int g_failures = 0;

void
Fail(const std::string& what)
{
    std::fprintf(stderr, "FAIL: %s\n", what.c_str());
    ++g_failures;
}

// The /preview subtree as text, copied under a fixed root so the comparison
// sees the graph and not where it lives; plus the material's terminal.
std::string
Describe(const UsdShadeMaterial& material)
{
    const SdfPath preview = material.GetPath().AppendChild(TfToken("preview"));
    SdfLayerRefPtr out = SdfLayer::CreateAnonymous(".usda");
    const SdfLayerHandle src = material.GetPrim().GetStage()->GetRootLayer();
    if (!src->GetPrimAtPath(preview))
        return "<no /preview>";
    if (!SdfCopySpec(src, preview, out, SdfPath("/preview")))
        return "<copy failed>";
    std::string text;
    out->ExportToString(&text);
    text += "\nterminal:";
    SdfPathVector sources;
    material.GetSurfaceOutput().GetAttr().GetConnections(&sources);
    for (const SdfPath& p : sources)
        text += " " + p.GetString();
    return text;
}

void
Regenerate(const UsdShadeMaterial& material)
{
    material.GetPrim().GetStage()->RemovePrim(
        material.GetPath().AppendChild(TfToken("preview")));
    material.GetSurfaceOutput().ClearSources();
    UsdVrmAuthorPreview(material, UsdVrmReadMaterialSemantics(material));
}

std::vector<UsdShadeMaterial>
Materials(const UsdStageRefPtr& stage)
{
    std::vector<UsdShadeMaterial> out;
    for (const UsdPrim& prim : stage->Traverse())
    {
        if (prim.IsA<UsdShadeMaterial>())
            out.emplace_back(prim);
    }
    return out;
}

} // namespace

int
main(int argc, char** argv)
{
    int stages = 0, materials = 0;
    UsdStageRefPtr mutationStage;

    for (int a = 1; a < argc; ++a)
    {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(argv[a]))
        {
            if (entry.path().extension() != ".vrm")
                continue;
            const std::string path = entry.path().generic_string();
            UsdStageRefPtr source = UsdStage::Open(path);
            if (!source)
                continue; // the fixtures that must not open
            // Everything below sees an anonymous flattened layer, nothing else.
            UsdStageRefPtr stage = UsdStage::Open(source->Flatten());
            ++stages;
            for (const UsdShadeMaterial& material : Materials(stage))
            {
                const std::string before = Describe(material);
                Regenerate(material);
                const std::string after = Describe(material);
                if (before != after)
                {
                    Fail(path + " " + material.GetPath().GetString() +
                         ": regenerated /preview differs\n--- imported\n" + before +
                         "\n--- regenerated\n" + after);
                }
                ++materials;
            }
            if (entry.path().filename() == "materials.vrm")
                mutationStage = stage;
        }
    }

    // The generator reads its input: move a canonical value, regenerate, and
    // the realization follows. Glass is lit and untextured, so its base colour
    // lands on diffuseColor as a value.
    if (!mutationStage)
    {
        Fail("materials.vrm not found");
    }
    else
    {
        const UsdShadeMaterial glass(mutationStage->GetPrimAtPath(SdfPath("/Asset/mtl/Glass")));
        const GfVec3f moved(0.25f, 0.5f, 0.75f);
        UsdVrmMaterialAPI(glass.GetPrim()).GetBaseColorFactorAttr().Set(moved);
        Regenerate(glass);
        GfVec3f diffuse(0.0f);
        const UsdShadeShader surface(
            mutationStage->GetPrimAtPath(SdfPath("/Asset/mtl/Glass/preview/surface")));
        if (!surface || !surface.GetInput(TfToken("diffuseColor")).Get(&diffuse) ||
            diffuse != moved)
        {
            Fail("Glass: /preview did not follow a changed baseColorFactor");
        }
    }

    // Not a vacuous pass: the fixtures and the vendored corpus.
    if (materials < 60)
        Fail("only " + std::to_string(materials) + " materials checked");

    std::printf("test_preview_regenerate: %d stage(s), %d material(s), %d failure(s)\n", stages,
                materials, g_failures);
    return g_failures == 0 ? 0 : 1;
}
