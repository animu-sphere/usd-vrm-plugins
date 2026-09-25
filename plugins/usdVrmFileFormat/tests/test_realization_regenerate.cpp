// SPDX-License-Identifier: Apache-2.0
//
// Each rendering realization is a function of a material's canonical
// attributes (material policy §6.5, P5 Steps 5-6): delete /preview or /mtlx
// from an imported stage and regenerate it from the Material's
// VrmMaterialAPI / VrmMToonAPI / VrmTextureInfoAPI attributes alone, and the
// graph comes back as the importer wrote it.
//
// The stage is flattened first, so nothing of the source .vrm, the file format
// or the importer is reachable when a graph is regenerated: only the stage.
// A second check changes a canonical value and regenerates, so a generator that
// ignored its input and reproduced a remembered graph would fail.
//
// Usage: test_realization_regenerate <dir-with-.vrm> [<dir> ...]
// Needs the usdVrmFileFormat plugin (and vrmSchema) on PXR_PLUGINPATH_NAME to
// open the .vrm inputs.

#include "usd/MaterialSemantics.h"
#include "usd/MtlxRealization.h"
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

// One realization: the material-local graph it owns, the render context of
// the terminal it connects ("" is the universal one), and its generator.
struct Realization
{
    const char* graph;
    const char* context;
    void (*author)(const UsdShadeMaterial&, const VrmMaterialSemantics&);
};

const Realization kRealizations[] = {
    {"preview", "", &UsdVrmAuthorPreview},
    {"mtlx", "mtlx", &UsdVrmAuthorMtlx},
};

// The graph's subtree as text, copied under a fixed root so the comparison
// sees the graph and not where it lives; plus the material's terminal and, for
// /mtlx, the MaterialX version it declares.
std::string
Describe(const UsdShadeMaterial& material, const Realization& r)
{
    const SdfPath graph = material.GetPath().AppendChild(TfToken(r.graph));
    std::string text;
    const SdfLayerHandle src = material.GetPrim().GetStage()->GetRootLayer();
    if (src->GetPrimAtPath(graph))
    {
        SdfLayerRefPtr out = SdfLayer::CreateAnonymous(".usda");
        if (!SdfCopySpec(src, graph, out, SdfPath::AbsoluteRootPath().AppendChild(TfToken(r.graph))))
            return "<copy failed>";
        out->ExportToString(&text);
    }
    else
    {
        text = std::string("<no /") + r.graph + ">";
    }
    text += "\nterminal:";
    SdfPathVector sources;
    if (const UsdShadeOutput terminal = material.GetSurfaceOutput(TfToken(r.context)))
        terminal.GetAttr().GetConnections(&sources);
    for (const SdfPath& p : sources)
        text += " " + p.GetString();
    std::string version;
    if (material.GetPrim().GetAttribute(TfToken("config:mtlx:version")).Get(&version))
        text += "\nconfig:mtlx:version: " + version;
    return text;
}

void
Regenerate(const UsdShadeMaterial& material, const Realization& r)
{
    material.GetPrim().GetStage()->RemovePrim(material.GetPath().AppendChild(TfToken(r.graph)));
    if (const UsdShadeOutput terminal = material.GetSurfaceOutput(TfToken(r.context)))
        terminal.ClearSources();
    r.author(material, UsdVrmReadMaterialSemantics(material));
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
    int stages = 0, materials = 0, mtlxGraphs = 0;
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
                for (const Realization& r : kRealizations)
                {
                    const std::string before = Describe(material, r);
                    Regenerate(material, r);
                    const std::string after = Describe(material, r);
                    if (before != after)
                    {
                        Fail(path + " " + material.GetPath().GetString() + ": regenerated /" +
                             r.graph + " differs\n--- imported\n" + before +
                             "\n--- regenerated\n" + after);
                    }
                }
                if (material.GetPrim().GetChild(TfToken("mtlx")))
                    ++mtlxGraphs;
                ++materials;
            }
            if (entry.path().filename() == "materials.vrm")
                mutationStage = stage;
        }
    }

    // Each generator reads its input: move a canonical value, regenerate, and
    // the realization follows. Glass is lit and untextured, so its base colour
    // lands on /preview's diffuseColor and /mtlx's base_color as a value;
    // Unlit is unlit and untextured, so it lands on /mtlx's emissive.
    if (!mutationStage)
    {
        Fail("materials.vrm not found");
    }
    else
    {
        struct Mutation
        {
            const char* material;
            const Realization& realization;
            const char* shaderInput;
        };
        const Mutation mutations[] = {
            {"Glass", kRealizations[0], "diffuseColor"},
            {"Unlit", kRealizations[1], "emissive"},
            {"Glass", kRealizations[1], "base_color"},
        };
        const GfVec3f moved(0.25f, 0.5f, 0.75f);
        for (const Mutation& m : mutations)
        {
            const SdfPath matPath = SdfPath("/Asset/mtl").AppendChild(TfToken(m.material));
            const UsdShadeMaterial material(mutationStage->GetPrimAtPath(matPath));
            UsdVrmMaterialAPI(material.GetPrim()).GetBaseColorFactorAttr().Set(moved);
            Regenerate(material, m.realization);
            GfVec3f value(0.0f);
            const UsdShadeShader surface(mutationStage->GetPrimAtPath(
                matPath.AppendChild(TfToken(m.realization.graph)).AppendChild(TfToken("surface"))));
            if (!surface || !surface.GetInput(TfToken(m.shaderInput)).Get(&value) ||
                value != moved)
            {
                Fail(std::string(m.material) + ": /" + m.realization.graph +
                     " did not follow a changed baseColorFactor");
            }
        }
    }

    // Not a vacuous pass: the fixtures and the vendored corpus.
    if (materials < 60)
        Fail("only " + std::to_string(materials) + " materials checked");
    if (mtlxGraphs == 0)
        Fail("no /mtlx graph checked");

    std::printf("test_realization_regenerate: %d stage(s), %d material(s), %d with /mtlx, "
                "%d failure(s)\n",
                stages, materials, mtlxGraphs, g_failures);
    return g_failures == 0 ? 0 : 1;
}
