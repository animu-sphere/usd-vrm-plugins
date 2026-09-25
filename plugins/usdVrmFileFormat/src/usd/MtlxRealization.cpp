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

#include <algorithm>

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


// One /mtlx graph under construction: defines its nodes, and shares one
// texcoord node between every texture that samples it.
class _Graph
{
public:
    _Graph(const UsdStagePtr& stage, const SdfPath& path) : _stage(stage), _path(path) {}

    UsdShadeShader Node(const std::string& name, const char* id) const
    {
        UsdShadeShader n = UsdShadeShader::Define(_stage, _path.AppendChild(TfToken(name)));
        n.CreateIdAttr(VtValue(TfToken(id)));
        return n;
    }

    // Sample `ref` for texture role `role`: an `ND_image_<type>` node fed by
    // the shared texcoord, through a place2d when the source states a
    // KHR_texture_transform. `srgb` declares the file's colour space; the
    // data textures (vector3) have none. A texture that is not addressed by
    // the mesh's UVs (MToon's MatCap) passes its own `uv` and takes no
    // transform.
    UsdShadeOutput Sample(const std::string& role, const VrmTextureRef& ref, const char* id,
                          const SdfValueTypeName& type, bool srgb, UsdShadeOutput uv = {})
    {
        const bool meshUv = !uv;
        if (meshUv)
            uv = _Texcoord();

        // KHR_texture_transform. place2d is not UsdTransform2d spelled
        // differently: its SRT form computes rotate2d(uv / scale, rotate) -
        // offset, dividing where UsdTransform2d multiplies, subtracting where
        // it adds, and not negating the rotation the way UsdTransform2d does.
        // The shared st-space map is therefore inverted into this node's
        // vocabulary rather than passed through.
        if (meshUv && ref.hasTransform)
        {
            const VrmStTransform t = VrmGltfStTransform(ref);
            UsdShadeShader place = Node(role + "Place", "ND_place2d_vector2");
            place.CreateInput(TfToken("texcoord"), SdfValueTypeNames->Float2).ConnectToSource(uv);
            place.CreateInput(TfToken("scale"), SdfValueTypeNames->Float2)
                .Set(GfVec2f(1.0f / t.scale[0], 1.0f / t.scale[1]));
            place.CreateInput(TfToken("rotate"), SdfValueTypeNames->Float).Set(-t.rotationDegrees);
            place.CreateInput(TfToken("offset"), SdfValueTypeNames->Float2).Set(-t.translation);
            uv = place.CreateOutput(TfToken("out"), SdfValueTypeNames->Float2);
        }

        UsdShadeShader image = Node(role + "Image", id);
        UsdShadeInput file = image.CreateInput(TfToken("file"), SdfValueTypeNames->Asset);
        file.Set(SdfAssetPath(ref.filePath));
        // sRGB is metadata on the asset here, not an input as in /preview.
        if (srgb)
            file.GetAttr().SetColorSpace(TfToken("srgb_texture"));
        // The color4 node's own default is transparent black, so a texture
        // that fails to resolve would take the alpha to zero and erase the
        // material instead of drawing it flat. Match UsdUVTexture's opaque
        // black, which is what /preview falls back to and what reads as a
        // texture problem. The other types' zero default already matches it
        // (and a zero normal sample is the flat normal to normalmap).
        if (type == SdfValueTypeNames->Color4f)
        {
            image.CreateInput(TfToken("default"), SdfValueTypeNames->Color4f)
                .Set(GfVec4f(0.0f, 0.0f, 0.0f, 1.0f));
        }
        image.CreateInput(TfToken("texcoord"), SdfValueTypeNames->Float2).ConnectToSource(uv);
        image.CreateInput(TfToken("uaddressmode"), SdfValueTypeNames->String)
            .Set(std::string(_MtlxAddressMode(ref.wrapS)));
        image.CreateInput(TfToken("vaddressmode"), SdfValueTypeNames->String)
            .Set(std::string(_MtlxAddressMode(ref.wrapT)));
        return image.CreateOutput(TfToken("out"), type);
    }

private:
    UsdShadeOutput _Texcoord()
    {
        if (!_st)
        {
            UsdShadeShader st = Node("st", "ND_texcoord_vector2");
            // Index 0, not ref.uvSet: the importer authors exactly one UV
            // primvar, `st` from TEXCOORD_0, and warns when a material asked
            // for another (VRM121). Naming a set the geometry does not carry
            // would turn that warning into a renderer sampling an undefined
            // stream.
            st.CreateInput(TfToken("index"), SdfValueTypeNames->Int).Set(0);
            _st = st.CreateOutput(TfToken("out"), SdfValueTypeNames->Float2);
        }
        return _st;
    }

    UsdStagePtr _stage;
    SdfPath _path;
    UsdShadeOutput _st;
};

// glTF's base colour and alpha, onto `color` and `opacity`, with the coverage
// inputs they need.
//
// Base colour follows glTF: factor * texture, multiplied after the sRGB decode
// that the file's `colorSpace` metadata requests — the same order /preview gets
// from folding the factor into UsdUVTexture.scale, so the two realizations
// agree on the value while disagreeing on the shading model.
void
_AuthorBaseColor(_Graph& g, UsdShadeShader surface, UsdShadeInput color,
                 const VrmMaterialSemantics& s)
{
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

    const VrmTextureRef* tex = _Texture(s, "baseColor");
    if (!tex)
    {
        color.Set(s.baseColorFactor);
        opacity.Set(_GltfOpacity(s));
        return;
    }

    // color4 in one fetch: the alpha has to come from the same sample as the
    // colour, and MaterialX has no multi-output image node.
    const UsdShadeOutput sample =
        g.Sample("baseColor", *tex, "ND_image_color4", SdfValueTypeNames->Color4f, true);

    UsdShadeShader factor = g.Node("baseColorFactor", "ND_multiply_color4");
    factor.CreateInput(TfToken("in1"), SdfValueTypeNames->Color4f).ConnectToSource(sample);
    const GfVec3f& f = s.baseColorFactor;
    factor.CreateInput(TfToken("in2"), SdfValueTypeNames->Color4f)
        .Set(GfVec4f(f[0], f[1], f[2], s.baseColorAlphaFactor));

    UsdShadeShader split = g.Node("baseColorSplit", "ND_separate4_color4");
    split.CreateInput(TfToken("in"), SdfValueTypeNames->Color4f)
        .ConnectToSource(factor.CreateOutput(TfToken("out"), SdfValueTypeNames->Color4f));
    UsdShadeOutput r = split.CreateOutput(TfToken("outr"), SdfValueTypeNames->Float);
    UsdShadeOutput gr = split.CreateOutput(TfToken("outg"), SdfValueTypeNames->Float);
    UsdShadeOutput b = split.CreateOutput(TfToken("outb"), SdfValueTypeNames->Float);
    UsdShadeOutput a = split.CreateOutput(TfToken("outa"), SdfValueTypeNames->Float);

    UsdShadeShader rgb = g.Node("baseColorRgb", "ND_combine3_color3");
    rgb.CreateInput(TfToken("in1"), SdfValueTypeNames->Float).ConnectToSource(r);
    rgb.CreateInput(TfToken("in2"), SdfValueTypeNames->Float).ConnectToSource(gr);
    rgb.CreateInput(TfToken("in3"), SdfValueTypeNames->Float).ConnectToSource(b);
    color.ConnectToSource(rgb.CreateOutput(TfToken("out"), SdfValueTypeNames->Color3f));

    // The sampled alpha only reaches the surface where glTF says it counts;
    // OPAQUE ignores it, exactly as /preview leaves opacity unconnected.
    if (alphaMode == 0)
        opacity.Set(_GltfOpacity(s));
    else
        opacity.ConnectToSource(a);
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
// against a translucent draw, and lit materials use the same terminal. There
// is also no fallback to lean on — a material carrying an unrenderable
// `outputs:mtlx:surface` does not revert to the universal terminal, it renders
// as an untextured grey.
//
// KHR_materials_unlit ignores every lit-only slot (metallic-roughness,
// normal, occlusion, emissive), so none is read.
void
_AuthorUnlit(_Graph& g, UsdShadeShader surface, const VrmMaterialSemantics& s)
{
    // No lit response: no diffuse albedo and no specular lobe. Leaving specular
    // at its default puts a highlight on a toon face.
    surface.CreateInput(TfToken("base_color"), SdfValueTypeNames->Color3f).Set(GfVec3f(0.0f));
    surface.CreateInput(TfToken("metallic"), SdfValueTypeNames->Float).Set(0.0f);
    surface.CreateInput(TfToken("roughness"), SdfValueTypeNames->Float).Set(1.0f);
    surface.CreateInput(TfToken("specular"), SdfValueTypeNames->Float).Set(0.0f);

    _AuthorBaseColor(g, surface,
                     surface.CreateInput(TfToken("emissive"), SdfValueTypeNames->Color3f), s);
}

// A glTF factor times one channel of a data texture: `factor` when the
// texture is absent, `factor * channel` through `ND_multiply_float` when not.
void
_AuthorScaledChannel(_Graph& g, UsdShadeInput input, float factor, const UsdShadeOutput& channel,
                     const std::string& nodeName)
{
    if (!channel)
    {
        input.Set(factor);
        return;
    }
    UsdShadeShader mul = g.Node(nodeName, "ND_multiply_float");
    mul.CreateInput(TfToken("in1"), SdfValueTypeNames->Float).ConnectToSource(channel);
    mul.CreateInput(TfToken("in2"), SdfValueTypeNames->Float).Set(factor);
    input.ConnectToSource(mul.CreateOutput(TfToken("out"), SdfValueTypeNames->Float));
}

// glTF's normal texture as a world-space shading normal, or an invalid output
// when the material has none. glTF's scale applies to X and Y only, which is
// what `normalmap` does with its `scale`.
UsdShadeOutput
_AuthorNormalMap(_Graph& g, const VrmMaterialSemantics& s)
{
    const VrmTextureRef* tex = _Texture(s, "normal");
    if (!tex)
        return {};
    UsdShadeShader map = g.Node("normalMap", "ND_normalmap_float");
    map.CreateInput(TfToken("in"), SdfValueTypeNames->Vector3f)
        .ConnectToSource(
            g.Sample("normal", *tex, "ND_image_vector3", SdfValueTypeNames->Vector3f, false));
    map.CreateInput(TfToken("scale"), SdfValueTypeNames->Float).Set(tex->scale);
    return map.CreateOutput(TfToken("out"), SdfValueTypeNames->Vector3f);
}

// glTF emission onto `input`: emissiveFactor * emissiveTexture, the factor
// scaled by `factorScale`.
void
_AuthorEmission(_Graph& g, UsdShadeInput input, const VrmMaterialSemantics& s,
                float factorScale)
{
    const GfVec3f factor = s.emissiveFactor * factorScale;
    if (const VrmTextureRef* tex = _Texture(s, "emissive"))
    {
        UsdShadeShader mul = g.Node("emissiveFactor", "ND_multiply_color3");
        mul.CreateInput(TfToken("in1"), SdfValueTypeNames->Color3f)
            .ConnectToSource(
                g.Sample("emissive", *tex, "ND_image_color3", SdfValueTypeNames->Color3f, true));
        mul.CreateInput(TfToken("in2"), SdfValueTypeNames->Color3f).Set(factor);
        input.ConnectToSource(mul.CreateOutput(TfToken("out"), SdfValueTypeNames->Color3f));
    }
    else
    {
        input.Set(factor);
    }
}

// The lit realization: glTF metallic-roughness PBR, through the glTF shading
// model's own MaterialX node, one texture role to one input.
//
// Every glTF relation that /preview has to fold into UsdUVTexture's scale and
// bias is a node here, because MaterialX can compute: factor * texture for
// base colour, metallic, roughness and emission; occlusion strength as
// mix(1, sample, strength); the normal texture's scale on X and Y only, which
// is what `normalmap` does with its `scale`. Emissive strength is gltf_pbr's
// own `emissive_strength`.
void
_AuthorLit(_Graph& g, UsdShadeShader surface, const VrmMaterialSemantics& s)
{
    _AuthorBaseColor(g, surface,
                     surface.CreateInput(TfToken("base_color"), SdfValueTypeNames->Color3f), s);

    // glTF packs roughness in G and metalness in B. Raw data: vector3 has no
    // colour space to decode.
    UsdShadeOutput metallicChannel, roughnessChannel;
    if (const VrmTextureRef* tex = _Texture(s, "metallicRoughness"))
    {
        UsdShadeShader split = g.Node("metallicRoughnessSplit", "ND_separate3_vector3");
        split.CreateInput(TfToken("in"), SdfValueTypeNames->Vector3f)
            .ConnectToSource(g.Sample("metallicRoughness", *tex, "ND_image_vector3",
                                      SdfValueTypeNames->Vector3f, false));
        roughnessChannel = split.CreateOutput(TfToken("outy"), SdfValueTypeNames->Float);
        metallicChannel = split.CreateOutput(TfToken("outz"), SdfValueTypeNames->Float);
    }
    _AuthorScaledChannel(g, surface.CreateInput(TfToken("metallic"), SdfValueTypeNames->Float),
                         s.metallicFactor, metallicChannel, "metallic");
    _AuthorScaledChannel(g, surface.CreateInput(TfToken("roughness"), SdfValueTypeNames->Float),
                         s.roughnessFactor, roughnessChannel, "roughness");

    if (const UsdShadeOutput normal = _AuthorNormalMap(g, s))
    {
        surface.CreateInput(TfToken("normal"), SdfValueTypeNames->Vector3f)
            .ConnectToSource(normal);
    }

    // glTF occlusion: ao = 1 + strength * (sample.r - 1) = mix(1, sample.r, strength).
    if (const VrmTextureRef* tex = _Texture(s, "occlusion"))
    {
        UsdShadeShader split = g.Node("occlusionSplit", "ND_separate3_vector3");
        split.CreateInput(TfToken("in"), SdfValueTypeNames->Vector3f)
            .ConnectToSource(g.Sample("occlusion", *tex, "ND_image_vector3",
                                      SdfValueTypeNames->Vector3f, false));
        UsdShadeShader mix = g.Node("occlusion", "ND_mix_float");
        mix.CreateInput(TfToken("fg"), SdfValueTypeNames->Float)
            .ConnectToSource(split.CreateOutput(TfToken("outx"), SdfValueTypeNames->Float));
        mix.CreateInput(TfToken("bg"), SdfValueTypeNames->Float).Set(1.0f);
        mix.CreateInput(TfToken("mix"), SdfValueTypeNames->Float).Set(tex->scale);
        surface.CreateInput(TfToken("occlusion"), SdfValueTypeNames->Float)
            .ConnectToSource(mix.CreateOutput(TfToken("out"), SdfValueTypeNames->Float));
    }

    // glTF emission: emissiveFactor * emissiveTexture, times
    // KHR_materials_emissive_strength on gltf_pbr's own input.
    _AuthorEmission(g, surface.CreateInput(TfToken("emissive"), SdfValueTypeNames->Color3f), s,
                    1.0f);
    surface.CreateInput(TfToken("emissive_strength"), SdfValueTypeNames->Float)
        .Set(s.emissiveStrength);
}
// The MToon realization: VRMC_materials_mtoon 1.0's lighting and rim, as the
// specification's pseudocode states them, approximated in standard MaterialX
// nodes (material policy §5.2) and carried on `emissive` with the lit response
// zeroed, as the unlit realization is.
//
// The light is a headlight (policy §11 q13): standard nodes reach scene lights
// only inside a BSDF, so the graph has no light of its own, and it takes the
// direction to the camera as the light direction, white at intensity 1. Then:
//
//   shading = linearstep(-1 + toony, 1 - toony,
//                        N.V + shadingShiftFactor + shadingShiftTexture.r * scale)
//   color   = lerp(shadeColorFactor * shadeMultiplyTexture,
//                  baseColorFactor * baseColorTexture, shading)
//   rim     = (matcapFactor * matcapTexture(matcapUv)
//              + parametricRimColorFactor
//                * pow(saturate(1 - N.V + parametricRimLiftFactor), max(power, eps)))
//             * rimMultiplyTexture
//   emissive = color + rim + glTF emission
//
// What the approximation leaves out, each for a stated reason:
//   - global illumination, and with it giEqualizationFactor: the graph has no
//     environment to equalize;
//   - rimLightingMixFactor: it blends the rim towards the lighting, which is
//     white at intensity 1 here, so every value gives the same rim;
//   - outline, render-queue offset and transparentWithZWrite: canonical
//     semantics no single-pass graph reproduces (§6.2, §5.3.2);
//   - UV animation: a function of time, which a material graph is not given.
//
// A missing texture takes the value MToon's reference shader defaults it to:
// white for the shade-multiply and rim-multiply textures (so the factor alone
// applies), black for MatCap (so no MatCap), zero shift for shading shift.
void
_AuthorMToon(_Graph& g, UsdShadeShader surface, const VrmMaterialSemantics& s)
{
    const VrmMToonSemantics& m = s.mtoon;
    auto in = [](UsdShadeShader n, const char* name, const SdfValueTypeName& type)
    { return n.CreateInput(TfToken(name), type); };
    auto out = [](UsdShadeShader n, const char* name, const SdfValueTypeName& type)
    { return n.CreateOutput(TfToken(name), type); };

    // No lit response, as for unlit: the toon colour is emitted.
    surface.CreateInput(TfToken("base_color"), SdfValueTypeNames->Color3f).Set(GfVec3f(0.0f));
    surface.CreateInput(TfToken("metallic"), SdfValueTypeNames->Float).Set(0.0f);
    surface.CreateInput(TfToken("roughness"), SdfValueTypeNames->Float).Set(1.0f);
    surface.CreateInput(TfToken("specular"), SdfValueTypeNames->Float).Set(0.0f);

    const UsdShadeOutput normal = _AuthorNormalMap(g, s);

    // N.L, with L the direction to the camera. facingratio is
    // -dot(viewdirection, N), viewdirection pointing from the camera to the
    // surface; with faceforward off it keeps the sign MToon's dot product has.
    UsdShadeShader nDotV = g.Node("nDotV", "ND_facingratio_float");
    in(nDotV, "faceforward", SdfValueTypeNames->Bool).Set(false);
    if (normal)
        in(nDotV, "normal", SdfValueTypeNames->Vector3f).ConnectToSource(normal);
    const UsdShadeOutput nDotVOut = out(nDotV, "out", SdfValueTypeNames->Float);

    // shading = N.L + shadingShiftFactor + shadingShiftTexture.r * scale.
    UsdShadeShader shading = g.Node("shading", "ND_add_float");
    in(shading, "in1", SdfValueTypeNames->Float).ConnectToSource(nDotVOut);
    UsdShadeInput shift = in(shading, "in2", SdfValueTypeNames->Float);
    if (const VrmTextureRef* tex = _Texture(s, "shadingShift"))
    {
        UsdShadeShader split = g.Node("shadingShiftSplit", "ND_separate3_vector3");
        in(split, "in", SdfValueTypeNames->Vector3f)
            .ConnectToSource(g.Sample("shadingShift", *tex, "ND_image_vector3",
                                      SdfValueTypeNames->Vector3f, false));
        UsdShadeShader scaled = g.Node("shadingShiftScaled", "ND_multiply_float");
        in(scaled, "in1", SdfValueTypeNames->Float)
            .ConnectToSource(out(split, "outx", SdfValueTypeNames->Float));
        in(scaled, "in2", SdfValueTypeNames->Float).Set(tex->scale);
        UsdShadeShader offset = g.Node("shadingShiftOffset", "ND_add_float");
        in(offset, "in1", SdfValueTypeNames->Float)
            .ConnectToSource(out(scaled, "out", SdfValueTypeNames->Float));
        in(offset, "in2", SdfValueTypeNames->Float).Set(m.shadingShiftFactor);
        shift.ConnectToSource(out(offset, "out", SdfValueTypeNames->Float));
    }
    else
    {
        shift.Set(m.shadingShiftFactor);
    }

    // linearstep(a, b, t) = saturate((t - a) / (b - a)), a = -1 + toony and
    // b = 1 - toony. At toony = 1 the width is zero and the specification
    // divides by it; the step is kept a step by a minimum width instead.
    const float low = -1.0f + m.shadingToonyFactor;
    const float width = std::max(2.0f - 2.0f * m.shadingToonyFactor, 1e-5f);
    UsdShadeShader rampOffset = g.Node("rampOffset", "ND_subtract_float");
    in(rampOffset, "in1", SdfValueTypeNames->Float)
        .ConnectToSource(out(shading, "out", SdfValueTypeNames->Float));
    in(rampOffset, "in2", SdfValueTypeNames->Float).Set(low);
    UsdShadeShader rampScale = g.Node("rampScale", "ND_multiply_float");
    in(rampScale, "in1", SdfValueTypeNames->Float)
        .ConnectToSource(out(rampOffset, "out", SdfValueTypeNames->Float));
    in(rampScale, "in2", SdfValueTypeNames->Float).Set(1.0f / width);
    UsdShadeShader ramp = g.Node("ramp", "ND_clamp_float");
    in(ramp, "in", SdfValueTypeNames->Float)
        .ConnectToSource(out(rampScale, "out", SdfValueTypeNames->Float));
    in(ramp, "low", SdfValueTypeNames->Float).Set(0.0f);
    in(ramp, "high", SdfValueTypeNames->Float).Set(1.0f);

    // color = lerp(shade term, lit term, shading). The lit term is the base
    // colour chain the other models use, alpha and coverage included.
    UsdShadeShader toon = g.Node("toon", "ND_mix_color3");
    _AuthorBaseColor(g, surface, in(toon, "fg", SdfValueTypeNames->Color3f), s);
    UsdShadeInput shade = in(toon, "bg", SdfValueTypeNames->Color3f);
    if (const VrmTextureRef* tex = _Texture(s, "shadeMultiply"))
    {
        UsdShadeShader mul = g.Node("shadeColor", "ND_multiply_color3");
        in(mul, "in1", SdfValueTypeNames->Color3f)
            .ConnectToSource(g.Sample("shadeMultiply", *tex, "ND_image_color3",
                                      SdfValueTypeNames->Color3f, true));
        in(mul, "in2", SdfValueTypeNames->Color3f).Set(m.shadeColorFactor);
        shade.ConnectToSource(out(mul, "out", SdfValueTypeNames->Color3f));
    }
    else
    {
        shade.Set(m.shadeColorFactor);
    }
    in(toon, "mix", SdfValueTypeNames->Float)
        .ConnectToSource(out(ramp, "out", SdfValueTypeNames->Float));
    UsdShadeOutput color = out(toon, "out", SdfValueTypeNames->Color3f);

    // Rim: MatCap, then parametric rim, then the rim-multiply mask.
    UsdShadeOutput rim;
    if (const VrmTextureRef* tex = _Texture(s, "matcap"))
    {
        // matcapUv = (dot(X, N), dot(Y, N)) * 0.495 + 0.5, with V the
        // direction to the camera, X = normalize(V.z, 0, -V.x) and
        // Y = cross(V, X). Sampled as is: MaterialX, like the reference
        // shader, puts v = 1 at the top of the image, so a normal facing up
        // reads the top of the MatCap.
        UsdShadeShader view = g.Node("view", "ND_viewdirection_vector3");
        UsdShadeShader toCamera = g.Node("viewToCamera", "ND_multiply_vector3FA");
        in(toCamera, "in1", SdfValueTypeNames->Vector3f)
            .ConnectToSource(out(view, "out", SdfValueTypeNames->Vector3f));
        in(toCamera, "in2", SdfValueTypeNames->Float).Set(-1.0f);
        const UsdShadeOutput v = out(toCamera, "out", SdfValueTypeNames->Vector3f);

        UsdShadeShader split = g.Node("viewSplit", "ND_separate3_vector3");
        in(split, "in", SdfValueTypeNames->Vector3f).ConnectToSource(v);
        UsdShadeShader negX = g.Node("viewNegX", "ND_multiply_float");
        in(negX, "in1", SdfValueTypeNames->Float)
            .ConnectToSource(out(split, "outx", SdfValueTypeNames->Float));
        in(negX, "in2", SdfValueTypeNames->Float).Set(-1.0f);
        UsdShadeShader xRaw = g.Node("viewXRaw", "ND_combine3_vector3");
        in(xRaw, "in1", SdfValueTypeNames->Float)
            .ConnectToSource(out(split, "outz", SdfValueTypeNames->Float));
        in(xRaw, "in2", SdfValueTypeNames->Float).Set(0.0f);
        in(xRaw, "in3", SdfValueTypeNames->Float)
            .ConnectToSource(out(negX, "out", SdfValueTypeNames->Float));
        UsdShadeShader x = g.Node("viewX", "ND_normalize_vector3");
        in(x, "in", SdfValueTypeNames->Vector3f)
            .ConnectToSource(out(xRaw, "out", SdfValueTypeNames->Vector3f));
        const UsdShadeOutput xOut = out(x, "out", SdfValueTypeNames->Vector3f);
        UsdShadeShader y = g.Node("viewY", "ND_crossproduct_vector3");
        in(y, "in1", SdfValueTypeNames->Vector3f).ConnectToSource(v);
        in(y, "in2", SdfValueTypeNames->Vector3f).ConnectToSource(xOut);

        UsdShadeOutput n = normal;
        if (!n)
        {
            UsdShadeShader worldNormal = g.Node("worldNormal", "ND_normal_vector3");
            in(worldNormal, "space", SdfValueTypeNames->String).Set(std::string("world"));
            n = out(worldNormal, "out", SdfValueTypeNames->Vector3f);
        }
        UsdShadeShader u = g.Node("matcapU", "ND_dotproduct_vector3");
        in(u, "in1", SdfValueTypeNames->Vector3f).ConnectToSource(xOut);
        in(u, "in2", SdfValueTypeNames->Vector3f).ConnectToSource(n);
        UsdShadeShader vv = g.Node("matcapV", "ND_dotproduct_vector3");
        in(vv, "in1", SdfValueTypeNames->Vector3f)
            .ConnectToSource(out(y, "out", SdfValueTypeNames->Vector3f));
        in(vv, "in2", SdfValueTypeNames->Vector3f).ConnectToSource(n);
        UsdShadeShader uvRaw = g.Node("matcapUvRaw", "ND_combine2_vector2");
        in(uvRaw, "in1", SdfValueTypeNames->Float)
            .ConnectToSource(out(u, "out", SdfValueTypeNames->Float));
        in(uvRaw, "in2", SdfValueTypeNames->Float)
            .ConnectToSource(out(vv, "out", SdfValueTypeNames->Float));
        UsdShadeShader uvScale = g.Node("matcapUvScale", "ND_multiply_vector2FA");
        in(uvScale, "in1", SdfValueTypeNames->Float2)
            .ConnectToSource(out(uvRaw, "out", SdfValueTypeNames->Float2));
        in(uvScale, "in2", SdfValueTypeNames->Float).Set(0.495f);
        UsdShadeShader uv = g.Node("matcapUv", "ND_add_vector2FA");
        in(uv, "in1", SdfValueTypeNames->Float2)
            .ConnectToSource(out(uvScale, "out", SdfValueTypeNames->Float2));
        in(uv, "in2", SdfValueTypeNames->Float).Set(0.5f);

        UsdShadeShader matcap = g.Node("matcap", "ND_multiply_color3");
        in(matcap, "in1", SdfValueTypeNames->Color3f)
            .ConnectToSource(g.Sample("matcap", *tex, "ND_image_color3",
                                      SdfValueTypeNames->Color3f, true,
                                      out(uv, "out", SdfValueTypeNames->Float2)));
        in(matcap, "in2", SdfValueTypeNames->Color3f).Set(m.matcapFactor);
        rim = out(matcap, "out", SdfValueTypeNames->Color3f);
    }

    // A black parametric rim adds nothing, so none is authored.
    if (m.parametricRimColorFactor != GfVec3f(0.0f))
    {
        UsdShadeShader base = g.Node("rimFresnelBase", "ND_subtract_float");
        in(base, "in1", SdfValueTypeNames->Float).Set(1.0f + m.parametricRimLiftFactor);
        in(base, "in2", SdfValueTypeNames->Float).ConnectToSource(nDotVOut);
        UsdShadeShader sat = g.Node("rimFresnelClamp", "ND_clamp_float");
        in(sat, "in", SdfValueTypeNames->Float)
            .ConnectToSource(out(base, "out", SdfValueTypeNames->Float));
        in(sat, "low", SdfValueTypeNames->Float).Set(0.0f);
        in(sat, "high", SdfValueTypeNames->Float).Set(1.0f);
        UsdShadeShader fresnel = g.Node("rimFresnel", "ND_power_float");
        in(fresnel, "in1", SdfValueTypeNames->Float)
            .ConnectToSource(out(sat, "out", SdfValueTypeNames->Float));
        in(fresnel, "in2", SdfValueTypeNames->Float)
            .Set(std::max(m.parametricRimFresnelPowerFactor, 1e-5f));
        UsdShadeShader parametric = g.Node("parametricRim", "ND_multiply_color3FA");
        in(parametric, "in1", SdfValueTypeNames->Color3f).Set(m.parametricRimColorFactor);
        in(parametric, "in2", SdfValueTypeNames->Float)
            .ConnectToSource(out(fresnel, "out", SdfValueTypeNames->Float));
        const UsdShadeOutput p = out(parametric, "out", SdfValueTypeNames->Color3f);
        if (rim)
        {
            UsdShadeShader sum = g.Node("rim", "ND_add_color3");
            in(sum, "in1", SdfValueTypeNames->Color3f).ConnectToSource(rim);
            in(sum, "in2", SdfValueTypeNames->Color3f).ConnectToSource(p);
            rim = out(sum, "out", SdfValueTypeNames->Color3f);
        }
        else
        {
            rim = p;
        }
    }

    if (rim)
    {
        if (const VrmTextureRef* tex = _Texture(s, "rimMultiply"))
        {
            UsdShadeShader mask = g.Node("rimMasked", "ND_multiply_color3");
            in(mask, "in1", SdfValueTypeNames->Color3f).ConnectToSource(rim);
            in(mask, "in2", SdfValueTypeNames->Color3f)
                .ConnectToSource(g.Sample("rimMultiply", *tex, "ND_image_color3",
                                          SdfValueTypeNames->Color3f, true));
            rim = out(mask, "out", SdfValueTypeNames->Color3f);
        }
        UsdShadeShader withRim = g.Node("withRim", "ND_add_color3");
        in(withRim, "in1", SdfValueTypeNames->Color3f).ConnectToSource(color);
        in(withRim, "in2", SdfValueTypeNames->Color3f).ConnectToSource(rim);
        color = out(withRim, "out", SdfValueTypeNames->Color3f);
    }

    // MToon adds glTF's emission. The strength is folded into the factor:
    // `emissive_strength` would scale the toon colour on the same input too.
    if (_Texture(s, "emissive") || s.emissiveFactor * s.emissiveStrength != GfVec3f(0.0f))
    {
        UsdShadeShader withEmission = g.Node("withEmission", "ND_add_color3");
        in(withEmission, "in1", SdfValueTypeNames->Color3f).ConnectToSource(color);
        _AuthorEmission(g, in(withEmission, "in2", SdfValueTypeNames->Color3f), s,
                        s.emissiveStrength);
        color = out(withEmission, "out", SdfValueTypeNames->Color3f);
    }

    surface.CreateInput(TfToken("emissive"), SdfValueTypeNames->Color3f).ConnectToSource(color);
}

} // namespace

void
UsdVrmAuthorMtlx(const UsdShadeMaterial& material, const VrmMaterialSemantics& s)
{
    const UsdStagePtr stage = material.GetPrim().GetStage();
    const SdfPath mtlxPath = material.GetPath().AppendChild(TfToken("mtlx"));
    UsdShadeNodeGraph graph = UsdShadeNodeGraph::Define(stage, mtlxPath);
    _Graph g(stage, mtlxPath);

    // Every shading model ends in the glTF surface (material policy §5.2.1).
    // MToon is the material's shading model wherever it is stated, whatever
    // the glTF core beside it says about unlit.
    UsdShadeShader surface = g.Node("surface", "ND_gltf_pbr_surfaceshader");
    if (s.hasMToon)
        _AuthorMToon(g, surface, s);
    else if (s.unlit)
        _AuthorUnlit(g, surface, s);
    else
        _AuthorLit(g, surface, s);

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
