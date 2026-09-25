// SPDX-License-Identifier: Apache-2.0
#include "usd/MtlxRealization.h"

#include "usd/UvTransform.h"

#include "pxr/base/gf/vec2f.h"
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

// The MaterialX version whose node semantics this generator targets. A literal,
// not the linked MaterialX version: the graph we write must mean the same thing
// on every runtime, and deriving it would make the frozen baselines depend on
// whichever MaterialX a platform happens to ship.
const char* const _kMtlxVersion = "1.39";

const VrmTextureRef*
_Texture(const VrmMaterialSemantics& s, const char* role)
{
    const auto it = s.textures.find(role);
    return (it != s.textures.end() && it->second.present) ? &it->second : nullptr;
}

// glTF sampler wrap -> MaterialX addressmode. The vocabularies differ by one
// word and the enum is not validated at author time, so "repeat" would sail
// through and mean nothing. The input is typed `string`, not `token`, so this
// never needs to become a TfToken.
const char*
_MtlxAddressMode(const std::string& wrap)
{
    if (wrap == "clamp")
        return "clamp";
    if (wrap == "mirror")
        return "mirror";
    return "periodic"; // glTF "repeat"
}

// The base colour factor's alpha, under glTF's alpha-coverage rule: OPAQUE
// "the alpha value is ignored and the rendered output is fully opaque", so a
// material with alphaMode OPAQUE and a factor alpha of 0.3 is opaque, not 30%
// transparent. MaterialX's gltf_pbr enforces it inside its own graph, and
// /preview applies the same rule (PreviewRealization.cpp) -- leaving either to
// pass the factor through would make the two disagree about one material.
float
_GltfOpacity(const VrmMaterialSemantics& s)
{
    return s.alphaMode == "OPAQUE" ? 1.0f : s.baseColorAlphaFactor;
}

// The unlit realization (material policy §5.2).
//
// The terminal is `gltf_pbr` with every lit response zeroed — base colour black,
// specular off — and the material's colour carried on `emissive`. That reads
// like the PreviewSurface workaround §5.2 forbids propagating here, so the
// reason it is not is worth recording: MaterialX's two direct ways to say unlit
// do not survive hdSt in OpenUSD 26.08. Measured against the pinned runtime:
//
//   ND_surface_unlit               fails to compile (GLSL references undeclared
//   ND_convert_color4_surfaceshader  `u_env*` uniforms); the prim then draws as
//                                    hdSt's flat grey fallback
//   ND_surface + EDF, no BSDF      compiles, renders, but `opacity` has no
//                                    effect at all — VRM hair and eyelashes
//                                    come out solid
//   ND_surface + EDF + black BSDF  works, including opacity
//   ND_gltf_pbr_surfaceshader      works, and is the only one whose alpha the
//                                    renderer reads as glTF defines it
//
// The common factor in the broken cases is a surface with no BSDF. Between the
// two that work, glTF's own model wins: `alpha_mode` and `alpha_cutoff` are
// native, so MASK becomes a real cutout rather than an `ifgreater` emulated
// against a translucent draw, and lit materials want the same terminal. There
// is also no fallback to lean on — a material carrying an unrenderable
// `outputs:mtlx:surface` does not revert to the universal terminal, it renders
// as an untextured grey.
//
// Base colour follows glTF: factor * texture, multiplied after the sRGB decode
// that the file's `colorSpace` metadata requests — the same order /preview gets
// from folding the factor into UsdUVTexture.scale, so the two realizations
// agree on the value while disagreeing on the shading model.
void
_AuthorUnlit(const UsdStagePtr& stage, const SdfPath& mtlxPath, UsdShadeShader surface,
             const VrmMaterialSemantics& s)
{
    auto node = [&](const char* name, const char* id)
    {
        UsdShadeShader n = UsdShadeShader::Define(stage, mtlxPath.AppendChild(TfToken(name)));
        n.CreateIdAttr(VtValue(TfToken(id)));
        return n;
    };

    // No lit response: no diffuse albedo and no specular lobe. Leaving specular
    // at its default puts a highlight on a toon face.
    surface.CreateInput(TfToken("base_color"), SdfValueTypeNames->Color3f).Set(GfVec3f(0.0f));
    surface.CreateInput(TfToken("metallic"), SdfValueTypeNames->Float).Set(0.0f);
    surface.CreateInput(TfToken("roughness"), SdfValueTypeNames->Float).Set(1.0f);
    surface.CreateInput(TfToken("specular"), SdfValueTypeNames->Float).Set(0.0f);

    // Emitted colour and alpha, resolved below to either a constant or the tail
    // of the texture chain.
    UsdShadeInput emissionColor =
        surface.CreateInput(TfToken("emissive"), SdfValueTypeNames->Color3f);
    UsdShadeInput opacity = surface.CreateInput(TfToken("alpha"), SdfValueTypeNames->Float);

    // glTF alpha coverage, verbatim: 0 OPAQUE, 1 MASK, 2 BLEND. The renderer
    // reads this to choose opaque / cutout / blended drawing, so it is authored
    // even when it is the default.
    const int alphaMode = s.alphaMode == "MASK" ? 1 : (s.alphaMode == "BLEND" ? 2 : 0);
    surface.CreateInput(TfToken("alpha_mode"), SdfValueTypeNames->Int).Set(alphaMode);
    if (alphaMode == 1)
    {
        surface.CreateInput(TfToken("alpha_cutoff"), SdfValueTypeNames->Float).Set(s.alphaCutoff);
    }

    if (const VrmTextureRef* baseColorTex = _Texture(s, "baseColor"))
    {
        const VrmTextureRef& ref = *baseColorTex;

        UsdShadeShader st = node("st", "ND_texcoord_vector2");
        // Index 0, not ref.uvSet: the importer authors exactly one UV primvar,
        // `st` from TEXCOORD_0, and warns when a material asked for another
        // (VRM121). Naming a set the geometry does not carry would turn that
        // warning into a renderer sampling an undefined stream.
        st.CreateInput(TfToken("index"), SdfValueTypeNames->Int).Set(0);
        UsdShadeOutput uv = st.CreateOutput(TfToken("out"), SdfValueTypeNames->Float2);

        // KHR_texture_transform. place2d is not UsdTransform2d spelled
        // differently: its SRT form computes rotate2d(uv / scale, rotate) -
        // offset, dividing where UsdTransform2d multiplies, subtracting where
        // it adds, and not negating the rotation the way UsdTransform2d does.
        // The shared st-space map is therefore inverted into this node's
        // vocabulary rather than passed through.
        if (ref.hasTransform)
        {
            const VrmStTransform t = VrmGltfStTransform(ref);
            UsdShadeShader place = node("baseColorPlace", "ND_place2d_vector2");
            place.CreateInput(TfToken("texcoord"), SdfValueTypeNames->Float2).ConnectToSource(uv);
            place.CreateInput(TfToken("scale"), SdfValueTypeNames->Float2)
                .Set(GfVec2f(1.0f / t.scale[0], 1.0f / t.scale[1]));
            place.CreateInput(TfToken("rotate"), SdfValueTypeNames->Float).Set(-t.rotationDegrees);
            place.CreateInput(TfToken("offset"), SdfValueTypeNames->Float2).Set(-t.translation);
            uv = place.CreateOutput(TfToken("out"), SdfValueTypeNames->Float2);
        }

        // color4 in one fetch: the alpha has to come from the same sample as
        // the colour, and MaterialX has no multi-output image node.
        UsdShadeShader image = node("baseColorImage", "ND_image_color4");
        UsdShadeInput file = image.CreateInput(TfToken("file"), SdfValueTypeNames->Asset);
        file.Set(SdfAssetPath(ref.filePath));
        // sRGB is metadata on the asset here, not an input as in /preview.
        file.GetAttr().SetColorSpace(TfToken("srgb_texture"));
        // The node's own default is transparent black, so a texture that fails
        // to resolve would take the alpha to zero and erase the material
        // instead of drawing it flat. Match UsdUVTexture's opaque black, which
        // is what /preview falls back to and what reads as a texture problem.
        image.CreateInput(TfToken("default"), SdfValueTypeNames->Color4f)
            .Set(GfVec4f(0.0f, 0.0f, 0.0f, 1.0f));
        image.CreateInput(TfToken("texcoord"), SdfValueTypeNames->Float2).ConnectToSource(uv);
        image.CreateInput(TfToken("uaddressmode"), SdfValueTypeNames->String)
            .Set(std::string(_MtlxAddressMode(ref.wrapS)));
        image.CreateInput(TfToken("vaddressmode"), SdfValueTypeNames->String)
            .Set(std::string(_MtlxAddressMode(ref.wrapT)));

        UsdShadeShader factor = node("baseColorFactor", "ND_multiply_color4");
        factor.CreateInput(TfToken("in1"), SdfValueTypeNames->Color4f)
            .ConnectToSource(image.CreateOutput(TfToken("out"), SdfValueTypeNames->Color4f));
        const GfVec3f& f = s.baseColorFactor;
        factor.CreateInput(TfToken("in2"), SdfValueTypeNames->Color4f)
            .Set(GfVec4f(f[0], f[1], f[2], s.baseColorAlphaFactor));

        UsdShadeShader split = node("baseColorSplit", "ND_separate4_color4");
        split.CreateInput(TfToken("in"), SdfValueTypeNames->Color4f)
            .ConnectToSource(factor.CreateOutput(TfToken("out"), SdfValueTypeNames->Color4f));
        UsdShadeOutput r = split.CreateOutput(TfToken("outr"), SdfValueTypeNames->Float);
        UsdShadeOutput g = split.CreateOutput(TfToken("outg"), SdfValueTypeNames->Float);
        UsdShadeOutput b = split.CreateOutput(TfToken("outb"), SdfValueTypeNames->Float);
        UsdShadeOutput a = split.CreateOutput(TfToken("outa"), SdfValueTypeNames->Float);

        UsdShadeShader rgb = node("baseColorRgb", "ND_combine3_color3");
        rgb.CreateInput(TfToken("in1"), SdfValueTypeNames->Float).ConnectToSource(r);
        rgb.CreateInput(TfToken("in2"), SdfValueTypeNames->Float).ConnectToSource(g);
        rgb.CreateInput(TfToken("in3"), SdfValueTypeNames->Float).ConnectToSource(b);
        emissionColor.ConnectToSource(rgb.CreateOutput(TfToken("out"), SdfValueTypeNames->Color3f));

        // The sampled alpha only reaches the surface where glTF says it counts;
        // OPAQUE ignores it, exactly as /preview leaves opacity unconnected.
        if (alphaMode == 0)
        {
            opacity.Set(_GltfOpacity(s));
        }
        else
        {
            opacity.ConnectToSource(a);
        }
    }
    else
    {
        emissionColor.Set(s.baseColorFactor);
        opacity.Set(_GltfOpacity(s));
    }
}

} // namespace

void
UsdVrmAuthorMtlx(const UsdShadeMaterial& material, const VrmMaterialSemantics& s)
{
    // Unlit only for now: that is where MaterialX says something
    // PreviewSurface cannot, and where every VRM character material lands.
    if (!s.unlit)
        return;

    const UsdStagePtr stage = material.GetPrim().GetStage();
    const SdfPath mtlxPath = material.GetPath().AppendChild(TfToken("mtlx"));
    UsdShadeNodeGraph graph = UsdShadeNodeGraph::Define(stage, mtlxPath);

    UsdShadeShader surface =
        UsdShadeShader::Define(stage, mtlxPath.AppendChild(TfToken("surface")));
    surface.CreateIdAttr(VtValue(TfToken("ND_gltf_pbr_surfaceshader")));

    _AuthorUnlit(stage, mtlxPath, surface, s);

    // Terminal: material -> graph -> internal shader, as for /preview (§4.1).
    // The render-context name is what a MaterialX-aware renderer looks for, and
    // it wins over the universal terminal wherever both exist.
    UsdShadeOutput graphOut = graph.CreateOutput(TfToken("surface"), SdfValueTypeNames->Token);
    graphOut.ConnectToSource(surface.CreateOutput(TfToken("surface"), SdfValueTypeNames->Token));
    material.CreateSurfaceOutput(TfToken("mtlx")).ConnectToSource(graphOut);

    // Say which MaterialX the graph was written against, the way UsdMtlx does
    // for documents it reads. The schema comes from the usdMtlx plugin rather
    // than from anything this bundle links, so an apply that fails means the
    // runtime is missing it — author nothing rather than leave an undeclared
    // builtin behind on a prim that has no such schema.
    const UsdPrim prim = material.GetPrim();
    if (prim.ApplyAPI(TfToken("MaterialXConfigAPI")))
    {
        prim.CreateAttribute(TfToken("config:mtlx:version"), SdfValueTypeNames->String,
                             /*custom=*/false)
            .Set(std::string(_kMtlxVersion));
    }
}

PXR_NAMESPACE_CLOSE_SCOPE
