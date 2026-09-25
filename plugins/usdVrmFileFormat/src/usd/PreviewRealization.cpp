// SPDX-License-Identifier: Apache-2.0
#include "usd/PreviewRealization.h"

#include "usd/UvTransform.h"

#include "pxr/base/gf/vec3f.h"
#include "pxr/base/gf/vec4f.h"
#include "pxr/base/tf/token.h"
#include "pxr/usd/sdf/assetPath.h"
#include "pxr/usd/sdf/path.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usdShade/nodeGraph.h"
#include "pxr/usd/usdShade/shader.h"

PXR_NAMESPACE_OPEN_SCOPE

namespace
{

const VrmTextureRef*
_Texture(const VrmMaterialSemantics& s, const char* role)
{
    const auto it = s.textures.find(role);
    return (it != s.textures.end() && it->second.present) ? &it->second : nullptr;
}

} // namespace

void
UsdVrmAuthorPreview(const UsdShadeMaterial& material, const VrmMaterialSemantics& s)
{
    const UsdStagePtr stage = material.GetPrim().GetStage();

    // Material policy §4: the Material prim is identity, binding and canonical
    // VRM semantics; each rendering realization is one material-local
    // UsdShadeNodeGraph. `preview` holds the UsdPreviewSurface fallback. The
    // graph name and its surface output are the authored contract; the node
    // names *inside* it are realization-local and are deliberately not (§4.3),
    // so the graph can be rewritten without a fixture migration.
    const SdfPath previewPath = material.GetPath().AppendChild(TfToken("preview"));
    UsdShadeNodeGraph preview = UsdShadeNodeGraph::Define(stage, previewPath);

    UsdShadeShader shader =
        UsdShadeShader::Define(stage, previewPath.AppendChild(TfToken("surface")));
    shader.CreateIdAttr(VtValue(TfToken("UsdPreviewSurface")));
    // VRM materials are unlit (KHR_materials_unlit) / toon. Render unlit as
    // base color through emissive with no lit response, so scene lights
    // don't carve facets into the low-poly surface (the "polygonal" look).
    const bool unlit = s.unlit;
    shader.CreateInput(TfToken("diffuseColor"), SdfValueTypeNames->Color3f)
        .Set(unlit ? GfVec3f(0.0f) : s.baseColorFactor);
    shader.CreateInput(TfToken("emissiveColor"), SdfValueTypeNames->Color3f)
        .Set(unlit ? s.baseColorFactor : s.emissiveFactor);
    shader.CreateInput(TfToken("metallic"), SdfValueTypeNames->Float)
        .Set(unlit ? 0.0f : s.metallicFactor);
    shader.CreateInput(TfToken("roughness"), SdfValueTypeNames->Float)
        .Set(unlit ? 1.0f : s.roughnessFactor);
    // glTF's alpha-coverage rule: OPAQUE "the alpha value is ignored and the
    // rendered output is fully opaque", so a factor alpha of 0.3 on an OPAQUE
    // material is opaque, not 30% transparent.
    shader.CreateInput(TfToken("opacity"), SdfValueTypeNames->Float)
        .Set(s.alphaMode == "OPAQUE" ? 1.0f : s.baseColorAlphaFactor);
    if (s.alphaMode == "MASK")
    {
        shader.CreateInput(TfToken("opacityThreshold"), SdfValueTypeNames->Float)
            .Set(s.alphaCutoff);
    }
    // Terminals connect material -> graph -> internal shader, never material
    // -> an internal shader (material policy §4.1). Material *bindings* keep
    // targeting the material prim and nothing below it.
    UsdShadeOutput surfaceOut = shader.CreateOutput(TfToken("surface"), SdfValueTypeNames->Token);
    UsdShadeOutput previewOut = preview.CreateOutput(TfToken("surface"), SdfValueTypeNames->Token);
    previewOut.ConnectToSource(surfaceOut);
    material.CreateSurfaceOutput().ConnectToSource(previewOut);

    // Textures: the five glTF core roles. A single UsdPrimvarReader_float2
    // feeds every UsdUVTexture's st; each role becomes one UsdUVTexture wired
    // into the matching UsdPreviewSurface input.
    const VrmTextureRef* baseColorTex = _Texture(s, "baseColor");
    const VrmTextureRef* metallicRoughnessTex = _Texture(s, "metallicRoughness");
    const VrmTextureRef* normalTex = _Texture(s, "normal");
    const VrmTextureRef* emissiveTex = _Texture(s, "emissive");
    const VrmTextureRef* occlusionTex = _Texture(s, "occlusion");
    const bool anyTex =
        baseColorTex || metallicRoughnessTex || normalTex || emissiveTex || occlusionTex;
    UsdShadeShader stReader;
    if (anyTex)
    {
        stReader = UsdShadeShader::Define(stage, previewPath.AppendChild(TfToken("stReader")));
        stReader.CreateIdAttr(VtValue(TfToken("UsdPrimvarReader_float2")));
        stReader.CreateInput(TfToken("varname"), SdfValueTypeNames->Token).Set(TfToken("st"));
        stReader.CreateOutput(TfToken("result"), SdfValueTypeNames->Float2);
    }

    auto makeTexture = [&](const VrmTextureRef& ref, const char* nodeName,
                           bool color) -> UsdShadeShader
    {
        UsdShadeShader tex =
            UsdShadeShader::Define(stage, previewPath.AppendChild(TfToken(nodeName)));
        tex.CreateIdAttr(VtValue(TfToken("UsdUVTexture")));
        tex.CreateInput(TfToken("file"), SdfValueTypeNames->Asset).Set(SdfAssetPath(ref.filePath));
        tex.CreateInput(TfToken("wrapS"), SdfValueTypeNames->Token).Set(TfToken(ref.wrapS));
        tex.CreateInput(TfToken("wrapT"), SdfValueTypeNames->Token).Set(TfToken(ref.wrapT));
        tex.CreateInput(TfToken("sourceColorSpace"), SdfValueTypeNames->Token)
            .Set(TfToken(color ? "sRGB" : "raw"));
        UsdShadeInput st = tex.CreateInput(TfToken("st"), SdfValueTypeNames->Float2);
        // KHR_texture_transform -> UsdTransform2d between the reader and st.
        // The node computes rotate2d(in * scale, -rotation) + translation,
        // and rotate2d turns clockwise, so the two negations cancel and the
        // shared st-space rotation is authored as-is.
        if (ref.hasTransform)
        {
            const VrmStTransform uv = VrmGltfStTransform(ref);
            UsdShadeShader xf = UsdShadeShader::Define(
                stage, previewPath.AppendChild(TfToken(std::string(nodeName) + "_xf")));
            xf.CreateIdAttr(VtValue(TfToken("UsdTransform2d")));
            xf.CreateInput(TfToken("in"), SdfValueTypeNames->Float2)
                .ConnectToSource(stReader.GetOutput(TfToken("result")));
            xf.CreateInput(TfToken("translation"), SdfValueTypeNames->Float2).Set(uv.translation);
            xf.CreateInput(TfToken("scale"), SdfValueTypeNames->Float2).Set(uv.scale);
            xf.CreateInput(TfToken("rotation"), SdfValueTypeNames->Float).Set(uv.rotationDegrees);
            st.ConnectToSource(xf.CreateOutput(TfToken("result"), SdfValueTypeNames->Float2));
        }
        else
        {
            st.ConnectToSource(stReader.GetOutput(TfToken("result")));
        }
        tex.CreateOutput(TfToken("rgb"), SdfValueTypeNames->Float3);
        tex.CreateOutput(TfToken("r"), SdfValueTypeNames->Float);
        tex.CreateOutput(TfToken("g"), SdfValueTypeNames->Float);
        tex.CreateOutput(TfToken("b"), SdfValueTypeNames->Float);
        tex.CreateOutput(TfToken("a"), SdfValueTypeNames->Float);
        return tex;
    };

    if (baseColorTex)
    {
        UsdShadeShader t = makeTexture(*baseColorTex, "baseColorTexture", true);
        // glTF defines base color as factor * texture. UsdUVTexture's
        // scale input preserves that relation without an extra shader node.
        const GfVec3f& f = s.baseColorFactor;
        t.CreateInput(TfToken("scale"), SdfValueTypeNames->Float4)
            .Set(GfVec4f(f[0], f[1], f[2], s.baseColorAlphaFactor));
        // Unlit routes base color to emissive (flat); lit routes to diffuse.
        shader.GetInput(TfToken(unlit ? "emissiveColor" : "diffuseColor"))
            .ConnectToSource(t.GetOutput(TfToken("rgb")));
        if (s.alphaMode != "OPAQUE")
        {
            shader.GetInput(TfToken("opacity")).ConnectToSource(t.GetOutput(TfToken("a")));
        }
    }
    // Lit-only slots (metallicRoughness / emissive / occlusion / normal) are
    // ignored by KHR_materials_unlit, so skip them on an unlit surface. This
    // also keeps the emissive texture from clobbering the base-color->emissive
    // connection authored above (a single UsdShade input takes one source).
    if (!unlit && metallicRoughnessTex)
    {
        UsdShadeShader t = makeTexture(*metallicRoughnessTex, "metallicRoughnessTexture", false);
        // glTF packs roughness in G, metalness in B.
        shader.GetInput(TfToken("roughness")).ConnectToSource(t.GetOutput(TfToken("g")));
        shader.GetInput(TfToken("metallic")).ConnectToSource(t.GetOutput(TfToken("b")));
    }
    if (!unlit && emissiveTex)
    {
        UsdShadeShader t = makeTexture(*emissiveTex, "emissiveTexture", true);
        shader.GetInput(TfToken("emissiveColor")).ConnectToSource(t.GetOutput(TfToken("rgb")));
    }
    if (!unlit && occlusionTex)
    {
        UsdShadeShader t = makeTexture(*occlusionTex, "occlusionTexture", false);
        // glTF occlusion strength: ao = 1 + strength * (sampled - 1), i.e.
        // out.r = sampled*strength + (1 - strength). Fold into the texture
        // scale/bias so the strength is honored, not dropped.
        const float os = occlusionTex->scale;
        t.CreateInput(TfToken("scale"), SdfValueTypeNames->Float4).Set(GfVec4f(os, os, os, os));
        t.CreateInput(TfToken("bias"), SdfValueTypeNames->Float4)
            .Set(GfVec4f(1.0f - os, 1.0f - os, 1.0f - os, 1.0f - os));
        shader.CreateInput(TfToken("occlusion"), SdfValueTypeNames->Float)
            .ConnectToSource(t.GetOutput(TfToken("r")));
    }
    if (!unlit && normalTex)
    {
        UsdShadeShader t = makeTexture(*normalTex, "normalTexture", false);
        // Decode tangent-space normals ([0,1] -> [-1,1]) and fold in glTF's
        // normalTexture.scale, which scales only the X/Y components:
        //   x,y = (2c - 1) * scale ;  z = 2c - 1
        const float ns = normalTex->scale;
        t.CreateInput(TfToken("scale"), SdfValueTypeNames->Float4)
            .Set(GfVec4f(2.0f * ns, 2.0f * ns, 2.0f, 2.0f));
        t.CreateInput(TfToken("bias"), SdfValueTypeNames->Float4)
            .Set(GfVec4f(-ns, -ns, -1.0f, -1.0f));
        shader.CreateInput(TfToken("normal"), SdfValueTypeNames->Normal3f)
            .ConnectToSource(t.GetOutput(TfToken("rgb")));
    }
}

PXR_NAMESPACE_CLOSE_SCOPE
