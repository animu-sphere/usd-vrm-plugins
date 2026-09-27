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
// A second check animates each expression colour slot on the Material and
// requires the realizations' node inputs to resolve to it with nothing
// regenerated (P5 Step 7), and a third changes a value /preview can only hold
// folded and regenerates, so a generator that ignored its input would fail.
//
// Usage: test_realization_regenerate <dir-with-.vrm> [<dir> ...]
// Needs the usdVrmFileFormat plugin (and vrmSchema) on PXR_PLUGINPATH_NAME to
// open the .vrm inputs.

#include "usd/MaterialSemantics.h"
#include "usd/MtlxRealization.h"
#include "usd/PreviewRealization.h"

#include <vrmSchema/vrmMaterialAPI.h>

#include "pxr/base/gf/vec3f.h"
#include "pxr/base/gf/vec4f.h"
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
        if (!SdfCopySpec(src, graph, out,
                         SdfPath::AbsoluteRootPath().AppendChild(TfToken(r.graph))))
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
    UsdStageRefPtr mutationStage, mtoonStage;

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
                             r.graph + " differs\n--- imported\n" + before + "\n--- regenerated\n" +
                             after);
                    }
                }
                if (material.GetPrim().GetChild(TfToken("mtlx")))
                    ++mtlxGraphs;
                ++materials;
            }
            if (entry.path().filename() == "materials.vrm")
                mutationStage = stage;
            if (entry.path().filename() == "mtoon_vrm1.vrm")
                mtoonStage = stage;
        }
    }

    // Every expression colour slot a realization uses is read from the
    // Material, not copied into the graph (material policy §11 q12): animate
    // the canonical value and the node input follows through the graph's
    // interface, with nothing regenerated. Each case names the node input and
    // the canonical attribute its value must come from.
    struct Follows
    {
        UsdStageRefPtr stage;
        const char* material;
        const char* node; // relative to the material: graph/node
        const char* input;
        const char* canonical;
    };
    const Follows follows[] = {
        // materials.vrm: Glass is lit and untextured, Unlit unlit and untextured.
        {mutationStage, "Glass", "preview/surface", "diffuseColor",
         "inputs:vrm:material:baseColorFactor"},
        {mutationStage, "Glass", "preview/surface", "opacity",
         "inputs:vrm:material:baseColorAlphaFactor"},
        {mutationStage, "Glass", "mtlx/surface", "base_color",
         "inputs:vrm:material:baseColorFactor"},
        {mutationStage, "Unlit", "preview/surface", "emissiveColor",
         "inputs:vrm:material:baseColorFactor"},
        {mutationStage, "Unlit", "mtlx/surface", "emissive", "inputs:vrm:material:baseColorFactor"},
        // mtoon_vrm1.vrm: Hair states every realized MToon term, textured;
        // Veil has no shade texture and a black rim.
        {mtoonStage, "Hair", "mtlx/baseColorFactorRgba", "in1",
         "inputs:vrm:material:baseColorFactor"},
        {mtoonStage, "Hair", "mtlx/baseColorFactorRgba", "in2",
         "inputs:vrm:material:baseColorAlphaFactor"},
        {mtoonStage, "Hair", "mtlx/shadeColor", "in2", "inputs:vrm:mtoon:shadeColorFactor"},
        {mtoonStage, "Hair", "mtlx/matcap", "in2", "inputs:vrm:mtoon:matcapFactor"},
        {mtoonStage, "Hair", "mtlx/parametricRim", "in1",
         "inputs:vrm:mtoon:parametricRimColorFactor"},
        {mtoonStage, "Hair", "mtlx/emissiveFactor", "in2", "inputs:vrm:material:emissiveFactor"},
        {mtoonStage, "Veil", "mtlx/toon", "bg", "inputs:vrm:mtoon:shadeColorFactor"},
        {mtoonStage, "Veil", "mtlx/parametricRim", "in1",
         "inputs:vrm:mtoon:parametricRimColorFactor"},
    };
    const UsdTimeCode frame(12.0);
    for (const Follows& f : follows)
    {
        const std::string where = std::string(f.material) + "/" + f.node + "." + f.input;
        if (!f.stage)
        {
            Fail(where + ": fixture not found");
            continue;
        }
        const SdfPath matPath = SdfPath("/Asset/mtl").AppendChild(TfToken(f.material));
        const UsdShadeShader node(f.stage->GetPrimAtPath(matPath.AppendPath(SdfPath(f.node))));
        const UsdShadeInput input = node ? node.GetInput(TfToken(f.input)) : UsdShadeInput();
        if (!input)
        {
            Fail(where + ": no such input");
            continue;
        }
        // The value-producing attribute: the one the renderer reads.
        const UsdShadeAttributeVector producers = input.GetValueProducingAttributes();
        const SdfPath expected = matPath.AppendProperty(TfToken(f.canonical));
        if (producers.size() != 1 || producers[0].GetPath() != expected)
        {
            Fail(where + ": its value does not come from " + expected.GetString());
            continue;
        }
        // A time sample on the Material -- what an expression bake authors --
        // is what the input resolves to at that frame.
        const UsdAttribute canonical = f.stage->GetAttributeAtPath(expected);
        if (canonical.GetTypeName() == SdfValueTypeNames->Float)
        {
            float value = 0.0f;
            canonical.Set(0.375f, frame);
            if (!producers[0].Get(&value, frame) || value != 0.375f)
                Fail(where + ": did not follow an animated " + f.canonical);
        }
        else
        {
            const GfVec3f moved(0.25f, 0.5f, 0.75f);
            GfVec3f value(0.0f);
            canonical.Set(moved, frame);
            if (!producers[0].Get(&value, frame) || value != moved)
                Fail(where + ": did not follow an animated " + f.canonical);
        }
        canonical.ClearAtTime(frame);
    }

    // What /preview can only take folded -- Hair's base colour is factor *
    // texture in UsdUVTexture.scale -- follows by regenerating, which is the
    // generator still reading its input.
    if (mtoonStage)
    {
        const SdfPath hairPath("/Asset/mtl/Hair");
        const UsdShadeMaterial hair(mtoonStage->GetPrimAtPath(hairPath));
        const GfVec3f moved(0.25f, 0.5f, 0.75f);
        UsdVrmMaterialAPI(hair.GetPrim()).GetBaseColorFactorAttr().Set(moved);
        Regenerate(hair, kRealizations[0]);
        GfVec4f scale(0.0f);
        const UsdShadeShader tex(
            mtoonStage->GetPrimAtPath(hairPath.AppendPath(SdfPath("preview/baseColorTexture"))));
        if (!tex || !tex.GetInput(TfToken("scale")).Get(&scale) ||
            GfVec3f(scale[0], scale[1], scale[2]) != moved)
        {
            Fail("Hair: /preview did not follow a changed baseColorFactor on regenerating");
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
