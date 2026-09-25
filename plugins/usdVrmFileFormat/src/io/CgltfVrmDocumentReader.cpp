// SPDX-License-Identifier: Apache-2.0
#include "io/CgltfVrmDocumentReader.h"

#include "model/VrmDiagnostics.h"
#include "util/PathUtil.h"
#include "util/TransformUtil.h"

#include <vrmContainer/GlbContainer.h>

#include "pxr/base/gf/quatd.h"
#include "pxr/base/gf/transform.h"
#include "pxr/base/js/json.h"
#include "pxr/base/js/value.h"
#include "pxr/base/tf/pathUtils.h"
#include "pxr/base/tf/stringUtils.h"
#include "pxr/usd/ar/packageUtils.h"

#include "cgltf.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <set>
#include <unordered_map>
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

namespace
{

// VRM 0.x names its expression presets with the `BlendShapePreset` enum, which
// is a *different vocabulary* from VRM 1.0's -- "joy" where 1.0 says "happy",
// "a" where 1.0 says "aa". `vrm:expressionName` is the key a `.vrma` clip joins
// an avatar's binds through, and a clip is a VRM 1.0-era file that only ever
// spells the 1.0 names, so a 0.x avatar carrying its own vocabulary could never
// be driven by one: of the seventeen presets only `neutral`, `angry` and
// `blink` are spelled the same on both sides.
//
// So a 0.x preset is migrated to the 1.0 name, which is what the importer
// already does with everything else 0.x (weights 0..100 -> 0..1,
// BlendShapeGroup -> Expression, SecondaryAnimation -> SpringBone). The raw 0.x
// block is preserved verbatim at `/Asset.customData.vrm:rawExtension`, so
// nothing is lost -- the mapping decides the canonical identity, not the record.
// A `presetName` outside the enum is left alone: it is not a preset this table
// knows, and inventing a name for it would be worse than carrying it through.
const std::string&
_Vrm0PresetToVrm1(const std::string& presetName)
{
    static const std::map<std::string, std::string> kMigration = {
        {"neutral", "neutral"},
        {"a", "aa"},
        {"i", "ih"},
        {"u", "ou"},
        {"e", "ee"},
        {"o", "oh"},
        {"blink", "blink"},
        {"blink_l", "blinkLeft"},
        {"blink_r", "blinkRight"},
        {"joy", "happy"},
        {"angry", "angry"},
        {"sorrow", "sad"},
        {"fun", "relaxed"},
        {"lookup", "lookUp"},
        {"lookdown", "lookDown"},
        {"lookleft", "lookLeft"},
        {"lookright", "lookRight"},
    };
    // The enum is serialized lowercase by UniVRM, but the field is free text in
    // the file: fold case before the lookup rather than miss on "Joy".
    std::string key = presetName;
    std::transform(key.begin(), key.end(), key.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const auto it = kMigration.find(key);
    return it == kMigration.end() ? presetName : it->second;
}

// ---------------------------------------------------------------------------
// Small JSON helpers over pxr/base/js (VRM extension blocks are plain JSON).
// ---------------------------------------------------------------------------
const JsValue*
_Find(const JsObject& obj, const char* key)
{
    auto it = obj.find(key);
    return it == obj.end() ? nullptr : &it->second;
}

const JsObject*
_AsObject(const JsValue* v)
{
    return (v && v->IsObject()) ? &v->GetJsObject() : nullptr;
}

const JsArray*
_AsArray(const JsValue* v)
{
    return (v && v->IsArray()) ? &v->GetJsArray() : nullptr;
}

int
_AsInt(const JsValue* v, int fallback = -1)
{
    return (v && v->IsInt()) ? v->GetInt() : fallback;
}

float
_AsFloat(const JsValue* v, float fallback)
{
    if (v && v->IsReal())
        return static_cast<float>(v->GetReal());
    if (v && v->IsInt())
        return static_cast<float>(v->GetInt());
    return fallback;
}

bool
_AsBool(const JsValue* v, bool fallback)
{
    return (v && v->IsBool()) ? v->GetBool() : fallback;
}

// The first `N` numbers of a JSON array; `fallback` unless the array has them all.
template <size_t N>
std::array<float, N>
_AsFloats(const JsValue* v, const std::array<float, N>& fallback)
{
    const JsArray* a = _AsArray(v);
    if (!a || a->size() < N)
        return fallback;
    std::array<float, N> out;
    for (size_t i = 0; i < N; ++i)
        out[i] = _AsFloat(&(*a)[i], fallback[i]);
    return out;
}

GfVec3f
_AsVec3(const JsValue* v, const GfVec3f& fallback)
{
    const auto a = _AsFloats<3>(v, {fallback[0], fallback[1], fallback[2]});
    return GfVec3f(a[0], a[1], a[2]);
}

// ---------------------------------------------------------------------------
// Canonical material semantics (material policy §6; P5 Step 4)
// ---------------------------------------------------------------------------

// Resolves a glTF texture index and an effective TEXCOORD set to a texture.
using _TextureResolver = std::function<VrmTextureRef(int textureIndex, int texCoord)>;

// A glTF textureInfo object written in JSON, as VRMC_materials_mtoon writes
// its textures: index, texCoord, the contribution `scale` a shading-shift
// texture carries, and KHR_texture_transform (whose texCoord overrides).
VrmTextureRef
_TextureInfoFromJson(const JsObject& info, const _TextureResolver& resolve)
{
    const JsObject* exts = _AsObject(_Find(info, "extensions"));
    const JsObject* xf = exts ? _AsObject(_Find(*exts, "KHR_texture_transform")) : nullptr;
    int texCoord = _AsInt(_Find(info, "texCoord"), 0);
    if (xf)
        texCoord = _AsInt(_Find(*xf, "texCoord"), texCoord);
    VrmTextureRef ref = resolve(_AsInt(_Find(info, "index")), texCoord);
    if (!ref.present)
        return ref;
    ref.scale = _AsFloat(_Find(info, "scale"), 1.0f);
    if (xf)
    {
        const auto offset = _AsFloats<2>(_Find(*xf, "offset"), {0.0f, 0.0f});
        const auto scale = _AsFloats<2>(_Find(*xf, "scale"), {1.0f, 1.0f});
        ref.hasTransform = true;
        ref.uvOffset = GfVec2f(offset[0], offset[1]);
        ref.uvScale = GfVec2f(scale[0], scale[1]);
        ref.uvRotation = _AsFloat(_Find(*xf, "rotation"), 0.0f);
    }
    return ref;
}

// The six VRMC_materials_mtoon textures: specification name -> texture role
// (VrmTextureInfoAPI instance name, the name without `Texture`).
const std::pair<const char*, const char*> _kMToonTextures[] = {
    {"shadeMultiplyTexture", "shadeMultiply"},
    {"shadingShiftTexture", "shadingShift"},
    {"matcapTexture", "matcap"},
    {"rimMultiplyTexture", "rimMultiply"},
    {"outlineWidthMultiplyTexture", "outlineWidthMultiply"},
    {"uvAnimationMaskTexture", "uvAnimationMask"},
};

// VRMC_materials_mtoon 1.0 -> canonical: a rename, field for field. An absent
// field keeps the specification default VrmMToonSemantics starts from.
void
_ReadMToon1(const JsObject& ext, const _TextureResolver& resolve, VrmMaterialSemantics* sem)
{
    VrmMToonSemantics& t = sem->mtoon;
    if (const JsValue* v = _Find(ext, "specVersion"); v && v->IsString())
        t.specVersion = v->GetString();
    t.transparentWithZWrite = _AsBool(_Find(ext, "transparentWithZWrite"), t.transparentWithZWrite);
    t.renderQueueOffsetNumber =
        _AsInt(_Find(ext, "renderQueueOffsetNumber"), t.renderQueueOffsetNumber);
    t.shadeColorFactor = _AsVec3(_Find(ext, "shadeColorFactor"), t.shadeColorFactor);
    t.shadingShiftFactor = _AsFloat(_Find(ext, "shadingShiftFactor"), t.shadingShiftFactor);
    t.shadingToonyFactor = _AsFloat(_Find(ext, "shadingToonyFactor"), t.shadingToonyFactor);
    t.giEqualizationFactor = _AsFloat(_Find(ext, "giEqualizationFactor"), t.giEqualizationFactor);
    t.matcapFactor = _AsVec3(_Find(ext, "matcapFactor"), t.matcapFactor);
    t.parametricRimColorFactor =
        _AsVec3(_Find(ext, "parametricRimColorFactor"), t.parametricRimColorFactor);
    t.parametricRimFresnelPowerFactor =
        _AsFloat(_Find(ext, "parametricRimFresnelPowerFactor"), t.parametricRimFresnelPowerFactor);
    t.parametricRimLiftFactor =
        _AsFloat(_Find(ext, "parametricRimLiftFactor"), t.parametricRimLiftFactor);
    t.rimLightingMixFactor = _AsFloat(_Find(ext, "rimLightingMixFactor"), t.rimLightingMixFactor);
    if (const JsValue* v = _Find(ext, "outlineWidthMode"); v && v->IsString())
        t.outlineWidthMode = v->GetString();
    t.outlineWidthFactor = _AsFloat(_Find(ext, "outlineWidthFactor"), t.outlineWidthFactor);
    t.outlineColorFactor = _AsVec3(_Find(ext, "outlineColorFactor"), t.outlineColorFactor);
    t.outlineLightingMixFactor =
        _AsFloat(_Find(ext, "outlineLightingMixFactor"), t.outlineLightingMixFactor);
    t.uvAnimationScrollXSpeedFactor =
        _AsFloat(_Find(ext, "uvAnimationScrollXSpeedFactor"), t.uvAnimationScrollXSpeedFactor);
    t.uvAnimationScrollYSpeedFactor =
        _AsFloat(_Find(ext, "uvAnimationScrollYSpeedFactor"), t.uvAnimationScrollYSpeedFactor);
    t.uvAnimationRotationSpeedFactor =
        _AsFloat(_Find(ext, "uvAnimationRotationSpeedFactor"), t.uvAnimationRotationSpeedFactor);

    for (const auto& [key, role] : _kMToonTextures)
    {
        if (const JsObject* info = _AsObject(_Find(ext, key)))
        {
            VrmTextureRef ref = _TextureInfoFromJson(*info, resolve);
            if (ref.present)
                sem->textures[role] = std::move(ref);
        }
    }
}

// ---- VRM 0.x MToon ---------------------------------------------------------
//
// VRM 0.x MToon is `materialProperties[i]`: Unity shader property names in
// three maps (floatProperties, vectorProperties, textureProperties) plus the
// material's renderQueue. It is normalized into the 1.0 model exactly as
// UniVRM's own migration does (Packages/VRM10/Runtime/Migration/Materials/
// MigrationMToonMaterial.cs and MToon10Migrator.cs, vrm-c/UniVRM d3665db), so
// a 0.x avatar and the 1.0 file UniVRM migrates it to author the same
// canonical values. Every conversion that is not a rename, and the two
// destructive ones UniVRM makes on purpose, are the schema contract's VRM 0.x
// table, each with its fidelity class (material policy §11 q10). The one
// departure: a property absent from the file takes the MToon 0.x shader's
// default, where UniVRM would take C#'s zero.

// `_BlendMode`.
enum class _MToon0RenderMode
{
    Opaque = 0,
    Cutout = 1,
    Transparent = 2,
    TransparentWithZWrite = 3,
};

// Unity's Color.linear, which UniVRM applies to every 0.x colour but emission:
// the sRGB transfer function, per channel.
float
_SrgbToLinear(float c)
{
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

GfVec3f
_SrgbToLinear(const GfVec3f& c)
{
    return GfVec3f(_SrgbToLinear(c[0]), _SrgbToLinear(c[1]), _SrgbToLinear(c[2]));
}

// Read access to one materialProperties entry, defaulting to the MToon 0.x
// shader's own property defaults.
struct _MToon0Props
{
    const JsObject* floats = nullptr;
    const JsObject* vectors = nullptr;
    const JsObject* textures = nullptr;

    explicit _MToon0Props(const JsObject& mp)
        : floats(_AsObject(_Find(mp, "floatProperties")))
        , vectors(_AsObject(_Find(mp, "vectorProperties")))
        , textures(_AsObject(_Find(mp, "textureProperties")))
    {
    }

    float Float(const char* key, float fallback) const
    {
        return floats ? _AsFloat(_Find(*floats, key), fallback) : fallback;
    }
    GfVec4f Vec4(const char* key, const GfVec4f& fallback) const
    {
        const auto a = _AsFloats<4>(vectors ? _Find(*vectors, key) : nullptr,
                                    {fallback[0], fallback[1], fallback[2], fallback[3]});
        return GfVec4f(a[0], a[1], a[2], a[3]);
    }
    GfVec3f Rgb(const char* key, const GfVec3f& fallback) const
    {
        const GfVec4f v = Vec4(key, GfVec4f(fallback[0], fallback[1], fallback[2], 1.0f));
        return GfVec3f(v[0], v[1], v[2]);
    }
    bool HasVec4(const char* key) const
    {
        const JsArray* a = vectors ? _AsArray(_Find(*vectors, key)) : nullptr;
        return a && a->size() >= 4;
    }
    // The glTF texture index a property names, or -1.
    int Texture(const char* key) const
    {
        return textures ? _AsInt(_Find(*textures, key)) : -1;
    }
    _MToon0RenderMode RenderMode() const
    {
        const int mode = static_cast<int>(Float("_BlendMode", 0.0f));
        return (mode >= 1 && mode <= 3) ? static_cast<_MToon0RenderMode>(mode)
                                        : _MToon0RenderMode::Opaque;
    }
};

// The material's renderQueue relative to its render mode's default queue
// (UniVRM's Vrm0XMToonValue). Only the two transparent modes use it, and only
// for its order among the file's materials of the same mode.
int
_MToon0RawQueueOffset(const JsObject& mp, _MToon0RenderMode mode)
{
    const int defaultQueue = mode == _MToon0RenderMode::Transparent             ? 3000
                             : mode == _MToon0RenderMode::TransparentWithZWrite ? 2501
                             : mode == _MToon0RenderMode::Cutout                ? 2450
                                                                                : 2000;
    return _AsInt(_Find(mp, "renderQueue"), defaultQueue) - defaultQueue;
}

// One VRM 0.x MToon material -> the canonical semantics. `sem` arrives holding
// the glTF core, which is kept wherever UniVRM keeps it (metallic, roughness,
// emissive strength, the metallicRoughness and occlusion textures, and any core
// texture the 0.x block does not name); `renderQueueOffsetNumber` is the
// cross-material ranking the caller computed.
void
_ReadMToon0(const JsObject& mp, int renderQueueOffsetNumber, const _TextureResolver& resolve,
            VrmMaterialSemantics* sem)
{
    const _MToon0Props p(mp);
    VrmMToonSemantics& t = sem->mtoon;
    sem->hasMToon = true;
    t.specVersion = "1.0";

    // Every texture but MatCap samples through _MainTex's tiling and offset
    // (Unity's _ST, bottom-left origin), carried over as KHR_texture_transform.
    const GfVec4f st = p.Vec4("_MainTex", GfVec4f(0.0f, 0.0f, 1.0f, 1.0f));
    const bool hasSt = p.Texture("_MainTex") >= 0 && p.HasVec4("_MainTex");
    auto texture = [&](const char* key, const char* role, bool applySt) -> bool
    {
        const int index = p.Texture(key);
        if (index < 0)
            return false;
        VrmTextureRef ref = resolve(index, 0);
        if (!ref.present)
            return false;
        if (applySt && hasSt)
        {
            ref.hasTransform = true;
            ref.uvOffset = GfVec2f(st[0], 1.0f - st[1] - st[3]);
            ref.uvScale = GfVec2f(st[2], st[3]);
            ref.uvRotation = 0.0f;
        }
        sem->textures[role] = std::move(ref);
        return true;
    };

    // Rendering.
    switch (p.RenderMode())
    {
    case _MToon0RenderMode::Opaque:
        sem->alphaMode = "OPAQUE";
        sem->alphaCutoff = 0.5f;
        t.transparentWithZWrite = false;
        break;
    case _MToon0RenderMode::Cutout:
        sem->alphaMode = "MASK";
        sem->alphaCutoff = p.Float("_Cutoff", 0.5f);
        t.transparentWithZWrite = false;
        break;
    case _MToon0RenderMode::Transparent:
        sem->alphaMode = "BLEND";
        sem->alphaCutoff = 0.5f;
        t.transparentWithZWrite = false;
        break;
    case _MToon0RenderMode::TransparentWithZWrite:
        sem->alphaMode = "BLEND";
        sem->alphaCutoff = 0.5f;
        t.transparentWithZWrite = true;
        break;
    }
    t.renderQueueOffsetNumber = renderQueueOffsetNumber;
    // `_CullMode`: 0 Off, 1 Front, 2 Back. glTF has no front-face culling, so
    // Front becomes double-sided.
    const int cull = static_cast<int>(p.Float("_CullMode", 2.0f));
    sem->doubleSided = cull == 0 || cull == 1;
    // UniVRM's migration marks every MToon material KHR_materials_unlit.
    sem->unlit = true;

    // Lit colour.
    const GfVec4f color = p.Vec4("_Color", GfVec4f(1.0f));
    sem->baseColorFactor = _SrgbToLinear(GfVec3f(color[0], color[1], color[2]));
    sem->baseColorAlphaFactor = color[3];
    texture("_MainTex", "baseColor", true);

    // Shade. A lit texture with no shade texture becomes the shade texture as
    // well: destructive, and UniVRM's choice (MToon 0.x's GI let a missing
    // shade texture pass unnoticed).
    t.shadeColorFactor = _SrgbToLinear(p.Rgb("_ShadeColor", GfVec3f(0.97f, 0.81f, 0.86f)));
    if (!texture("_ShadeTexture", "shadeMultiply", true))
        texture("_MainTex", "shadeMultiply", true);

    if (texture("_BumpMap", "normal", true))
        sem->textures["normal"].scale = p.Float("_BumpScale", 1.0f);

    // Shading shift / toony: 0.x states the lit-to-shade ramp as a shift and a
    // toony that together bound it; 1.0 as the ramp's centre and its margin.
    {
        const float toony0 = p.Float("_ShadeToony", 0.9f);
        const float shift0 = p.Float("_ShadeShift", 0.0f);
        const float rangeMin = shift0;
        const float rangeMax = 1.0f + (shift0 - 1.0f) * toony0; // lerp(1, shift0, toony0)
        t.shadingToonyFactor = std::clamp((2.0f - (rangeMax - rangeMin)) * 0.5f, 0.0f, 1.0f);
        t.shadingShiftFactor = std::clamp((rangeMax + rangeMin) * 0.5f * -1.0f, -1.0f, 1.0f);
    }
    t.giEqualizationFactor =
        std::clamp(1.0f - p.Float("_IndirectLightIntensity", 0.1f), 0.0f, 1.0f);

    // Emission: already linear (an HDR colour in Unity).
    sem->emissiveFactor = p.Rgb("_EmissionColor", GfVec3f(0.0f));
    texture("_EmissionMap", "emissive", true);

    // MatCap: 0.x adds the sphere texture; 1.0 multiplies a factor into it, so
    // the factor is white exactly when there is a texture. No _ST.
    t.matcapFactor = texture("_SphereAdd", "matcap", false) ? GfVec3f(1.0f) : GfVec3f(0.0f);

    // Rim. rimLightingMixFactor is 1 whatever `_RimLightingMix` said:
    // destructive, and UniVRM's choice (1.0 merges rim with MatCap).
    t.parametricRimColorFactor = _SrgbToLinear(p.Rgb("_RimColor", GfVec3f(0.0f)));
    t.parametricRimFresnelPowerFactor = p.Float("_RimFresnelPower", 1.0f);
    t.parametricRimLiftFactor = p.Float("_RimLift", 0.0f);
    texture("_RimTexture", "rimMultiply", true);
    t.rimLightingMixFactor = 1.0f;

    // Outline. World width is in centimetres in 0.x and metres in 1.0; screen
    // width is a percentage of half the screen height in 0.x and a fraction of
    // the whole height in 1.0.
    int widthMode = static_cast<int>(p.Float("_OutlineWidthMode", 0.0f));
    if (widthMode < 0 || widthMode > 2)
        widthMode = 0;
    const float width0 = p.Float("_OutlineWidth", 0.5f);
    t.outlineWidthMode = widthMode == 1 ? "worldCoordinates"
                         : widthMode == 2 ? "screenCoordinates"
                                          : "none";
    t.outlineWidthFactor = widthMode == 1 ? width0 * 0.01f
                           : widthMode == 2 ? width0 * 0.01f * 0.5f
                                            : 0.0f;
    texture("_OutlineWidthTexture", "outlineWidthMultiply", true);
    t.outlineColorFactor = _SrgbToLinear(p.Rgb("_OutlineColor", GfVec3f(0.0f)));
    // `_OutlineColorMode`: 0 FixedColor (no lighting), 1 MixedLighting.
    t.outlineLightingMixFactor = static_cast<int>(p.Float("_OutlineColorMode", 0.0f)) == 1
                                     ? p.Float("_OutlineLightingMix", 1.0f)
                                     : 0.0f;

    // UV animation. 0.x rotates in turns per second and scrolls V in Unity's
    // bottom-up direction; 1.0 in radians per second and glTF's top-down V.
    texture("_UvAnimMaskTexture", "uvAnimationMask", true);
    t.uvAnimationScrollXSpeedFactor = p.Float("_UvAnimScrollX", 0.0f);
    // Negated by subtraction: `x * -1` turns a still 0 into -0.
    t.uvAnimationScrollYSpeedFactor = 0.0f - p.Float("_UvAnimScrollY", 0.0f);
    t.uvAnimationRotationSpeedFactor =
        p.Float("_UvAnimRotation", 0.0f) * 2.0f * 3.14159265358979f;
}

// ---------------------------------------------------------------------------
// cgltf node helpers
// ---------------------------------------------------------------------------
GfMatrix4d
_NodeLocal(const cgltf_node& n)
{
    if (n.has_matrix)
    {
        return VrmConvertGltfMatrix(n.matrix);
    }
    float t[3] = {0, 0, 0}, r[4] = {0, 0, 0, 1}, s[3] = {1, 1, 1};
    if (n.has_translation)
        std::memcpy(t, n.translation, sizeof(t));
    if (n.has_rotation)
        std::memcpy(r, n.rotation, sizeof(r));
    if (n.has_scale)
        std::memcpy(s, n.scale, sizeof(s));
    return VrmComposeTrs(t, r, s);
}

// World transform in USD row-vector convention: leaf-to-root left multiply.
GfMatrix4d
_NodeWorld(const cgltf_node* n)
{
    GfMatrix4d m(1.0);
    for (const cgltf_node* cur = n; cur; cur = cur->parent)
    {
        m = m * _NodeLocal(*cur);
    }
    return m;
}

template <typename T>
int
_IndexOf(const T* element, const T* base)
{
    return element ? static_cast<int>(element - base) : -1;
}

// ---------------------------------------------------------------------------
// Accessor readers
// ---------------------------------------------------------------------------
std::vector<GfVec3f>
_ReadVec3(const cgltf_accessor* acc)
{
    std::vector<GfVec3f> out;
    if (!acc)
        return out;
    out.resize(acc->count);
    for (cgltf_size i = 0; i < acc->count; ++i)
    {
        float v[3] = {0, 0, 0};
        cgltf_accessor_read_float(acc, i, v, 3);
        out[i] = GfVec3f(v[0], v[1], v[2]);
    }
    return out;
}

std::vector<GfVec2f>
_ReadVec2(const cgltf_accessor* acc)
{
    std::vector<GfVec2f> out;
    if (!acc)
        return out;
    out.resize(acc->count);
    for (cgltf_size i = 0; i < acc->count; ++i)
    {
        float v[2] = {0, 0};
        cgltf_accessor_read_float(acc, i, v, 2);
        out[i] = GfVec2f(v[0], v[1]);
    }
    return out;
}

const char*
_WrapStr(cgltf_wrap_mode w)
{
    switch (w)
    {
    case cgltf_wrap_mode_clamp_to_edge:
        return "clamp";
    case cgltf_wrap_mode_mirrored_repeat:
        return "mirror";
    default:
        return "repeat";
    }
}

// Supported embedded image formats (USD's image plugins read png/jpg). KTX2 /
// WebP / Basis are out of scope for Phase 2.
const char*
_ImageExt(const cgltf_image* img)
{
    if (img->mime_type)
    {
        if (std::strcmp(img->mime_type, "image/png") == 0)
            return "png";
        if (std::strcmp(img->mime_type, "image/jpeg") == 0)
            return "jpg";
        return nullptr;
    }
    return nullptr;
}

bool
_BufferViewBytes(const cgltf_buffer_view* view, vrmContainer::ByteView* out)
{
    if (!view || !out)
        return false;
    if (view->data)
    {
        *out = {static_cast<const std::byte*>(view->data), view->size};
        return true;
    }
    if (!view->buffer || !view->buffer->data)
        return false;
    const vrmContainer::ByteView buffer(static_cast<const std::byte*>(view->buffer->data),
                                        view->buffer->size);
    return vrmContainer::MakeBufferView(buffer, view->offset, view->size, out);
}

} // namespace

bool
CgltfVrmDocumentReader::Read(const std::string& resolvedPath, const std::vector<std::byte>& bytes,
                             VrmCanonicalDocument* outDoc, std::string* outError)
{
    auto fail = [&](const std::string& msg)
    {
        if (outError)
            *outError = msg;
        return false;
    };

    if (bytes.empty())
    {
        return fail("empty file");
    }

    vrmContainer::GlbView glb;
    vrmContainer::Error containerError;
    const vrmContainer::ByteView containerBytes(bytes.data(), bytes.size());
    if (!vrmContainer::ParseGlb(containerBytes, &glb, &containerError))
    {
        return fail(std::string("vrmContainer: ") +
                    vrmContainer::ErrorMessage(containerError.code) + " at byte " +
                    std::to_string(containerError.offset));
    }

    cgltf_options options = {};
    cgltf_data* data = nullptr;
    cgltf_result res = cgltf_parse(&options, bytes.data(), bytes.size(), &data);
    if (res != cgltf_result_success || !data)
    {
        return fail("cgltf_parse failed (not a valid glTF/GLB container)");
    }

    // Load buffer data. VRM embeds its bin chunk in the GLB, but pass the file
    // path so any external resources still resolve.
    if (cgltf_load_buffers(&options, data, resolvedPath.c_str()) != cgltf_result_success)
    {
        cgltf_free(data);
        return fail("cgltf_load_buffers failed (missing or unreadable buffers)");
    }

    // -----------------------------------------------------------------------
    // VRM version + raw extension JSON (cgltf hands us unparsed extension data).
    // -----------------------------------------------------------------------
    std::string vrm1Json, vrm0Json, springBone1Json;
    for (cgltf_size i = 0; i < data->data_extensions_count; ++i)
    {
        const cgltf_extension& ext = data->data_extensions[i];
        if (!ext.name || !ext.data)
            continue;
        if (std::strcmp(ext.name, "VRMC_vrm") == 0)
            vrm1Json = ext.data;
        else if (std::strcmp(ext.name, "VRM") == 0)
            vrm0Json = ext.data;
        // VRM 1.0 SpringBone is its own top-level extension (not under VRMC_vrm).
        else if (std::strcmp(ext.name, "VRMC_springBone") == 0)
            springBone1Json = ext.data;
    }

    if (!vrm1Json.empty())
    {
        outDoc->version = VrmVersion::Vrm1;
        outDoc->rawVrmExtensionJson = vrm1Json;
    }
    else if (!vrm0Json.empty())
    {
        outDoc->version = VrmVersion::Vrm0;
        outDoc->rawVrmExtensionJson = vrm0Json;
    }
    else
    {
        outDoc->version = VrmVersion::Unknown;
        outDoc->warnings.push_back(
            VrmDiagMsg(VrmDiag::NoVrmExtension,
                       "no VRM or VRMC_vrm extension found; importing as plain glTF"));
    }

    // -----------------------------------------------------------------------
    // Texture paths. Embedded images are addressed as package-relative assets
    // inside the .vrm container; UsdVrmPackageResolver serves those bytes to Hio
    // without a temp-dir extraction dependency.
    // -----------------------------------------------------------------------
    //
    // Both paths stay the UTF-8 string USD handed us. A std::filesystem::path
    // built from a narrow string reads it in the process's code page on
    // Windows, so the generic_string() these used to be round-tripped a
    // non-ASCII directory into a package path no resolver could find.
    std::string packagePath = resolvedPath;
#if defined(ARCH_OS_WINDOWS)
    std::replace(packagePath.begin(), packagePath.end(), '\\', '/');
#endif
    const std::string sourceDir = TfGetPathName(packagePath);
    std::unordered_map<const cgltf_image*, std::string> imageCache;

    auto extractImage = [&](const cgltf_image* img) -> std::string
    {
        if (!img)
            return {};
        auto cached = imageCache.find(img);
        if (cached != imageCache.end())
            return cached->second;

        std::string result;
        if (img->buffer_view)
        {
            const cgltf_buffer_view* bv = img->buffer_view;
            vrmContainer::ByteView imageBytes;
            if (!_BufferViewBytes(bv, &imageBytes))
            {
                outDoc->warnings.push_back(
                    VrmDiagMsg(VrmDiag::TextureFormatUnsupported,
                               "embedded image buffer view is out of range; texture skipped"));
                imageCache[img] = result;
                return result;
            }
            const auto* base = reinterpret_cast<const unsigned char*>(imageBytes.data());
            // Sniff the magic bytes (more reliable than the declared mimeType,
            // which some exporters/parsers drop); fall back to the mime hint.
            const char* ext = nullptr;
            if (imageBytes.size() >= 4 && base[0] == 0x89 && base[1] == 'P' && base[2] == 'N' &&
                base[3] == 'G')
            {
                ext = "png";
            }
            else if (imageBytes.size() >= 3 && base[0] == 0xFF && base[1] == 0xD8 &&
                     base[2] == 0xFF)
            {
                ext = "jpg";
            }
            else
            {
                ext = _ImageExt(img);
            }
            if (!ext)
            {
                outDoc->warnings.push_back(
                    VrmDiagMsg(VrmDiag::TextureFormatUnsupported,
                               "unsupported embedded image format (not PNG/JPEG); "
                               "texture skipped"));
            }
            else
            {
                result = ArJoinPackageRelativePath(
                    packagePath, vrmContainer::MakeEmbeddedResourcePath(imageBytes, ext));
            }
        }
        else if (img->uri && std::strncmp(img->uri, "data:", 5) != 0)
        {
            // External file reference, resolved relative to the source.
            result = img->uri;
#if defined(ARCH_OS_WINDOWS)
            std::replace(result.begin(), result.end(), '\\', '/');
#endif
            if (TfIsRelativePath(result))
            {
                result = sourceDir + result;
            }
        }
        else
        {
            outDoc->warnings.push_back(
                VrmDiagMsg(VrmDiag::TextureDataUriUnsupported,
                           "data-URI image not supported in Phase 2; texture skipped"));
        }
        imageCache[img] = result;
        return result;
    };

    // One texture: its image, sampler and the UV set it samples. `texCoord` is
    // the effective set, a KHR_texture_transform override already folded in.
    auto texRefFromTexture = [&](const cgltf_texture* texture, int texCoord) -> VrmTextureRef
    {
        VrmTextureRef r;
        if (!texture || !texture->image)
            return r;
        std::string path = extractImage(texture->image);
        if (path.empty())
            return r;
        r.present = true;
        r.filePath = path;
        r.uvSet = texCoord;
        if (texCoord != 0)
        {
            outDoc->warnings.push_back(
                VrmDiagMsg(VrmDiag::TextureTexcoordUnsupported,
                           "texture uses TEXCOORD_" + std::to_string(texCoord) +
                               "; only UV set 0 is wired in Phase 2 (sampling may be wrong)"));
        }
        if (texture->sampler)
        {
            r.wrapS = _WrapStr(texture->sampler->wrap_s);
            r.wrapT = _WrapStr(texture->sampler->wrap_t);
        }
        return r;
    };

    auto makeTexRef = [&](const cgltf_texture_view& tv) -> VrmTextureRef
    {
        const int texCoord =
            (tv.has_transform && tv.transform.has_texcoord) ? tv.transform.texcoord : tv.texcoord;
        VrmTextureRef r = texRefFromTexture(tv.texture, texCoord);
        if (!r.present)
            return r;
        r.scale = tv.scale;
        if (tv.has_transform)
        {
            r.hasTransform = true;
            r.uvOffset = GfVec2f(tv.transform.offset[0], tv.transform.offset[1]);
            r.uvScale = GfVec2f(tv.transform.scale[0], tv.transform.scale[1]);
            r.uvRotation = tv.transform.rotation;
        }
        return r;
    };

    // A texture a VRM extension block names by glTF texture index -- a
    // VRMC_materials_mtoon textureInfo, a VRM 0.x textureProperties entry --
    // which cgltf does not resolve. An index out of range names no texture.
    auto texRefFromIndex = [&](int textureIndex, int texCoord) -> VrmTextureRef
    {
        if (textureIndex < 0 || textureIndex >= static_cast<int>(data->textures_count))
            return {};
        return texRefFromTexture(&data->textures[textureIndex], texCoord);
    };

    // -----------------------------------------------------------------------
    // Materials
    // -----------------------------------------------------------------------
    std::vector<std::string> rawMatNames;
    rawMatNames.reserve(data->materials_count);
    for (cgltf_size i = 0; i < data->materials_count; ++i)
    {
        const cgltf_material& m = data->materials[i];
        rawMatNames.push_back(m.name ? m.name : "");
    }
    std::vector<std::string> matNames = VrmMakeUniqueNames(rawMatNames, "Material");

    outDoc->materials.resize(data->materials_count);
    for (cgltf_size i = 0; i < data->materials_count; ++i)
    {
        const cgltf_material& m = data->materials[i];
        VrmMaterial& vm = outDoc->materials[i];
        vm.name = matNames[i];
        vm.sourceMaterialIndex = static_cast<int>(i);
        if (m.has_pbr_metallic_roughness)
        {
            const auto& pbr = m.pbr_metallic_roughness;
            vm.baseColor = GfVec3f(pbr.base_color_factor[0], pbr.base_color_factor[1],
                                   pbr.base_color_factor[2]);
            vm.opacity = pbr.base_color_factor[3];
            vm.metallic = pbr.metallic_factor;
            vm.roughness = pbr.roughness_factor;
            vm.baseColorTex = makeTexRef(pbr.base_color_texture);
            vm.metallicRoughnessTex = makeTexRef(pbr.metallic_roughness_texture);
        }
        vm.emissiveColor =
            GfVec3f(m.emissive_factor[0], m.emissive_factor[1], m.emissive_factor[2]);
        vm.unlit = m.unlit; // KHR_materials_unlit
        vm.normalTex = makeTexRef(m.normal_texture);
        vm.occlusionTex = makeTexRef(m.occlusion_texture);
        vm.emissiveTex = makeTexRef(m.emissive_texture);
        vm.doubleSided = m.double_sided;
        vm.alphaMode = (m.alpha_mode == cgltf_alpha_mode_mask)    ? "MASK"
                       : (m.alpha_mode == cgltf_alpha_mode_blend) ? "BLEND"
                                                                  : "OPAQUE";
        vm.alphaCutoff = m.alpha_cutoff;

        // Canonical semantics start as the glTF core, for every material.
        VrmMaterialSemantics& sem = vm.semantics;
        sem.baseColorFactor = vm.baseColor;
        sem.baseColorAlphaFactor = vm.opacity;
        sem.metallicFactor = vm.metallic;
        sem.roughnessFactor = vm.roughness;
        sem.emissiveFactor = vm.emissiveColor;
        if (m.has_emissive_strength)
            sem.emissiveStrength = m.emissive_strength.emissive_strength;
        sem.alphaMode = vm.alphaMode;
        sem.alphaCutoff = vm.alphaCutoff;
        sem.doubleSided = vm.doubleSided;
        sem.unlit = vm.unlit;
        const std::pair<const char*, const VrmTextureRef*> coreTextures[] = {
            {"baseColor", &vm.baseColorTex}, {"metallicRoughness", &vm.metallicRoughnessTex},
            {"normal", &vm.normalTex},       {"occlusion", &vm.occlusionTex},
            {"emissive", &vm.emissiveTex},
        };
        for (const auto& [role, ref] : coreTextures)
        {
            if (ref->present)
                sem.textures[role] = *ref;
        }

        // MToon (VRM 1.0): typed into the canonical semantics, and the block
        // itself preserved verbatim as the lossless fallback. (VRM 0.x MToon
        // lives in VRM.materialProperties and is handled in the extension pass.)
        for (cgltf_size e = 0; e < m.extensions_count; ++e)
        {
            if (m.extensions[e].name &&
                std::strcmp(m.extensions[e].name, "VRMC_materials_mtoon") == 0)
            {
                vm.isMToon = true;
                if (m.extensions[e].data)
                {
                    vm.rawShaderJson = m.extensions[e].data;
                    JsParseError perr;
                    const JsValue ext = JsParseString(vm.rawShaderJson, &perr);
                    if (const JsObject* obj = _AsObject(&ext))
                    {
                        sem.hasMToon = true;
                        _ReadMToon1(*obj, texRefFromIndex, &sem);
                    }
                }
            }
        }
    }

    // -----------------------------------------------------------------------
    // Skeleton: unify the joints of *all* skins into a single skeleton.
    //
    // VRM avatars are routinely split across several glTF skins that each
    // reference a subset of a shared joint hierarchy. Importing only the first
    // skin (as a naive importer would) yields a partial skeleton and breaks the
    // humanoid mapping, so we take the union of all skin joints in a
    // deterministic encounter order and remap every mesh into it.
    // -----------------------------------------------------------------------
    std::unordered_map<const cgltf_node*, int> nodeToJoint;
    std::vector<const cgltf_node*> jointNodes; // union, in encounter order
    for (cgltf_size s = 0; s < data->skins_count; ++s)
    {
        const cgltf_skin& skin = data->skins[s];
        for (cgltf_size j = 0; j < skin.joints_count; ++j)
        {
            const cgltf_node* jn = skin.joints[j];
            if (jn && nodeToJoint.find(jn) == nodeToJoint.end())
            {
                nodeToJoint[jn] = static_cast<int>(jointNodes.size());
                jointNodes.push_back(jn);
            }
        }
    }

    if (!jointNodes.empty())
    {
        // Pre-compute world transforms once (used for both bind and rest).
        std::vector<GfMatrix4d> world(jointNodes.size());
        std::vector<std::string> rawJointNames(jointNodes.size());
        for (size_t j = 0; j < jointNodes.size(); ++j)
        {
            world[j] = _NodeWorld(jointNodes[j]);
            rawJointNames[j] = jointNodes[j]->name ? jointNodes[j]->name : "";
        }
        std::vector<std::string> jointNames = VrmMakeUniqueNames(rawJointNames, "Joint");

        outDoc->joints.resize(jointNodes.size());
        for (size_t j = 0; j < jointNodes.size(); ++j)
        {
            const cgltf_node* jn = jointNodes[j];
            VrmJoint& vj = outDoc->joints[j];
            vj.name = jointNames[j];
            vj.sourceNodeIndex = _IndexOf(jn, data->nodes);

            // Parent = nearest ancestor node that is itself a joint.
            int parent = -1;
            for (const cgltf_node* a = jn->parent; a; a = a->parent)
            {
                auto it = nodeToJoint.find(a);
                if (it != nodeToJoint.end())
                {
                    parent = it->second;
                    break;
                }
            }
            vj.parentJointIndex = parent;

            // rest = transform relative to the parent joint (robust even when
            // intermediate non-joint nodes are skipped). bind defaults to the
            // node world transform, then is overridden by the inverse bind
            // matrix below when the skin supplies one.
            vj.bindTransform = world[j];
            vj.restTransform = (parent >= 0) ? world[j] * world[parent].GetInverse() : world[j];
        }

        // World-space bind transforms come from each skin's inverse bind
        // matrices (bind = inverse(IBM)); the node world transform is only a
        // fallback. A node shared by several skins should agree across them.
        std::vector<bool> bindFromIbm(jointNodes.size(), false);
        bool warnedIbmConflict = false;
        for (cgltf_size s = 0; s < data->skins_count; ++s)
        {
            const cgltf_skin& skin = data->skins[s];
            if (!skin.inverse_bind_matrices)
                continue;
            for (cgltf_size j = 0; j < skin.joints_count; ++j)
            {
                auto it = nodeToJoint.find(skin.joints[j]);
                if (it == nodeToJoint.end())
                    continue;
                float ibm[16];
                if (!cgltf_accessor_read_float(skin.inverse_bind_matrices, j, ibm, 16))
                {
                    continue;
                }
                GfMatrix4d bind = VrmConvertGltfMatrix(ibm).GetInverse();
                const int u = it->second;
                if (bindFromIbm[u])
                {
                    if (!warnedIbmConflict &&
                        !GfIsClose(outDoc->joints[u].bindTransform, bind, 1e-4))
                    {
                        outDoc->warnings.push_back(
                            VrmDiagMsg(VrmDiag::SkinIbmConflict,
                                       "joint '" + outDoc->joints[u].name +
                                           "' has conflicting inverse bind matrices across "
                                           "skins; keeping the first"));
                        warnedIbmConflict = true;
                    }
                    continue;
                }
                outDoc->joints[u].bindTransform = bind;
                bindFromIbm[u] = true;
            }
        }

        // UsdSkel requires the joints array to be topologically ordered: every
        // parent must precede its children. The skin encounter order does not
        // guarantee that, so reorder by depth (a stable sort keeps sibling order
        // and, since depth(parent) < depth(child), yields a valid topo order),
        // then remap parent links, the node->joint map (used by the mesh and
        // humanoid passes below), so all downstream joint indices stay correct.
        const int nJoints = static_cast<int>(outDoc->joints.size());
        std::vector<int> depth(nJoints, -1);
        std::function<int(int)> getDepth = [&](int i) -> int
        {
            if (depth[i] >= 0)
                return depth[i];
            int p = outDoc->joints[i].parentJointIndex;
            depth[i] = (p < 0) ? 0 : getDepth(p) + 1;
            return depth[i];
        };
        for (int i = 0; i < nJoints; ++i)
            getDepth(i);

        std::vector<int> order(nJoints);
        for (int i = 0; i < nJoints; ++i)
            order[i] = i;
        std::stable_sort(order.begin(), order.end(),
                         [&](int a, int b) { return depth[a] < depth[b]; });

        std::vector<int> oldToNew(nJoints);
        for (int newIdx = 0; newIdx < nJoints; ++newIdx)
            oldToNew[order[newIdx]] = newIdx;

        std::vector<VrmJoint> reordered(nJoints);
        for (int newIdx = 0; newIdx < nJoints; ++newIdx)
        {
            reordered[newIdx] = outDoc->joints[order[newIdx]];
            int p = reordered[newIdx].parentJointIndex;
            reordered[newIdx].parentJointIndex = (p < 0) ? -1 : oldToNew[p];
        }
        outDoc->joints = std::move(reordered);
        for (auto& kv : nodeToJoint)
            kv.second = oldToNew[kv.second];
    }

    // -----------------------------------------------------------------------
    // Meshes (one USD Mesh per glTF primitive).
    // -----------------------------------------------------------------------
    // First pass: collect raw names so we can uniquify across all primitives.
    std::vector<std::string> rawMeshNames;
    for (cgltf_size n = 0; n < data->nodes_count; ++n)
    {
        const cgltf_node& node = data->nodes[n];
        if (!node.mesh)
            continue;
        const cgltf_mesh& mesh = *node.mesh;
        for (cgltf_size p = 0; p < mesh.primitives_count; ++p)
        {
            std::string base = mesh.name ? mesh.name : (node.name ? node.name : "Mesh");
            if (mesh.primitives_count > 1)
            {
                base += "_" + std::to_string(p);
            }
            rawMeshNames.push_back(base);
        }
    }
    std::vector<std::string> meshNames = VrmMakeUniqueNames(rawMeshNames, "Mesh");

    size_t meshNameCursor = 0;
    for (cgltf_size n = 0; n < data->nodes_count; ++n)
    {
        const cgltf_node& node = data->nodes[n];
        if (!node.mesh)
            continue;
        const cgltf_mesh& mesh = *node.mesh;
        const bool nodeSkinned = (node.skin != nullptr) && !outDoc->joints.empty();
        const GfMatrix4d nodeWorld = _NodeWorld(&node);

        for (cgltf_size p = 0; p < mesh.primitives_count; ++p)
        {
            const cgltf_primitive& prim = mesh.primitives[p];
            VrmMeshPrimitive out;
            out.name = meshNames[meshNameCursor++];
            out.sourceMeshIndex = _IndexOf(&mesh, data->meshes);
            out.sourcePrimitiveIndex = static_cast<int>(p);
            out.sourceNodeIndex = static_cast<int>(n);
            out.sourceNodeName = node.name ? node.name : "";
            out.materialIndex = _IndexOf(prim.material, data->materials);
            out.nodeWorldTransform = nodeWorld;

            if (prim.type != cgltf_primitive_type_triangles)
            {
                outDoc->warnings.push_back(
                    VrmDiagMsg(VrmDiag::PrimitiveNotTriangles,
                               "primitive '" + out.name + "' is not a triangle list; skipped"));
                continue;
            }

            const cgltf_accessor* jointsAcc = nullptr;
            const cgltf_accessor* weightsAcc = nullptr;
            for (cgltf_size a = 0; a < prim.attributes_count; ++a)
            {
                const cgltf_attribute& attr = prim.attributes[a];
                switch (attr.type)
                {
                case cgltf_attribute_type_position:
                    out.points = _ReadVec3(attr.data);
                    break;
                case cgltf_attribute_type_normal:
                    out.normals = _ReadVec3(attr.data);
                    break;
                case cgltf_attribute_type_texcoord:
                    if (attr.index == 0)
                    {
                        out.uvs = _ReadVec2(attr.data);
                        for (auto& uv : out.uvs)
                            uv = VrmConvertUv(uv);
                    }
                    break;
                case cgltf_attribute_type_joints:
                    if (attr.index == 0)
                        jointsAcc = attr.data;
                    break;
                case cgltf_attribute_type_weights:
                    if (attr.index == 0)
                        weightsAcc = attr.data;
                    break;
                default:
                    break;
                }
            }

            if (out.points.empty())
            {
                outDoc->warnings.push_back(
                    VrmDiagMsg(VrmDiag::PrimitiveNoPosition,
                               "primitive '" + out.name + "' has no POSITION; skipped"));
                continue;
            }

            // Indices -> triangle face topology.
            if (prim.indices)
            {
                const cgltf_size ic = prim.indices->count;
                out.faceVertexIndices.resize(ic);
                for (cgltf_size i = 0; i < ic; ++i)
                {
                    out.faceVertexIndices[i] =
                        static_cast<int>(cgltf_accessor_read_index(prim.indices, i));
                }
            }
            else
            {
                out.faceVertexIndices.resize(out.points.size());
                for (size_t i = 0; i < out.points.size(); ++i)
                {
                    out.faceVertexIndices[i] = static_cast<int>(i);
                }
            }
            out.faceVertexCounts.assign(out.faceVertexIndices.size() / 3, 3);

            // Skinning. JOINTS_0 indexes into this mesh's skin's joint list;
            // remap each into the unified skeleton order.
            if (nodeSkinned && jointsAcc && weightsAcc)
            {
                const cgltf_skin* skin = node.skin;
                out.skinned = true;
                // glTF skinned vertices already live in the common space the
                // inverse bind matrices map from (the scene/skel root); the mesh
                // node transform is ignored. So the geom bind is identity —
                // setting it to the node world would double-apply a transform
                // glTF discards and displace the mesh at bind pose.
                out.geomBindTransform = GfMatrix4d(1.0);
                const cgltf_size vc = out.points.size();
                out.jointWeights.resize(vc);
                out.jointIndices.resize(vc * 4);
                bool jointIndexOutOfRange = false;
                for (cgltf_size i = 0; i < vc; ++i)
                {
                    cgltf_uint ji[4] = {0, 0, 0, 0};
                    float jw[4] = {0, 0, 0, 0};
                    cgltf_accessor_read_uint(jointsAcc, i, ji, 4);
                    cgltf_accessor_read_float(weightsAcc, i, jw, 4);
                    for (int k = 0; k < 4; ++k)
                    {
                        int unified = 0;
                        if (ji[k] < skin->joints_count)
                        {
                            auto it = nodeToJoint.find(skin->joints[ji[k]]);
                            if (it != nodeToJoint.end())
                                unified = it->second;
                        }
                        else
                        {
                            jointIndexOutOfRange = true;
                        }
                        out.jointIndices[i * 4 + k] = unified;
                    }
                    out.jointWeights[i] = GfVec4f(jw[0], jw[1], jw[2], jw[3]);
                }
                if (jointIndexOutOfRange)
                {
                    outDoc->warnings.push_back(VrmDiagMsg(VrmDiag::SkinJointIndexOutOfRange,
                                                          "mesh '" + out.name +
                                                              "': JOINTS_0 references a skin joint "
                                                              "outside [0," +
                                                              std::to_string(skin->joints_count) +
                                                              "); clamped to the skeleton root"));
                }
            }

            // Morph targets (deltas only; expression binding handled separately).
            for (cgltf_size t = 0; t < prim.targets_count; ++t)
            {
                const cgltf_morph_target& target = prim.targets[t];
                VrmMorphTarget vt;
                if (t < mesh.target_names_count && mesh.target_names[t])
                {
                    vt.name = mesh.target_names[t];
                }
                for (cgltf_size a = 0; a < target.attributes_count; ++a)
                {
                    const cgltf_attribute& attr = target.attributes[a];
                    if (attr.type == cgltf_attribute_type_position)
                        vt.positionDeltas = _ReadVec3(attr.data);
                    else if (attr.type == cgltf_attribute_type_normal)
                        vt.normalDeltas = _ReadVec3(attr.data);
                }
                out.morphTargets.push_back(std::move(vt));
            }

            // Compact to the vertices this primitive actually references. glTF
            // primitives within one mesh may share a single vertex accessor (each
            // using a sub-range), so the full-accessor arrays read above can be
            // larger than the topology uses — which Hydra rejects ("Vertex
            // primvar has N elements, topology references only up to ...") and
            // then drops the primvars (incl. normals -> flat shading).
            {
                std::unordered_map<int, int> remap;
                std::vector<int> used; // newIndex -> oldIndex
                used.reserve(out.points.size());
                for (int& idx : out.faceVertexIndices)
                {
                    auto it = remap.find(idx);
                    if (it != remap.end())
                    {
                        idx = it->second;
                    }
                    else
                    {
                        int n = static_cast<int>(used.size());
                        remap.emplace(idx, n);
                        used.push_back(idx);
                        idx = n;
                    }
                }
                bool identity = used.size() == out.points.size();
                for (size_t i = 0; identity && i < used.size(); ++i)
                    identity = used[i] == static_cast<int>(i);
                if (!identity)
                {
                    auto gather3 = [&](std::vector<GfVec3f>& v)
                    {
                        if (v.empty())
                            return;
                        std::vector<GfVec3f> r;
                        r.reserve(used.size());
                        for (int o : used)
                            r.push_back(o < static_cast<int>(v.size()) ? v[o] : GfVec3f(0));
                        v.swap(r);
                    };
                    gather3(out.points);
                    gather3(out.normals);
                    if (!out.uvs.empty())
                    {
                        std::vector<GfVec2f> r;
                        r.reserve(used.size());
                        for (int o : used)
                            r.push_back(o < static_cast<int>(out.uvs.size()) ? out.uvs[o]
                                                                             : GfVec2f(0));
                        out.uvs.swap(r);
                    }
                    if (out.skinned)
                    {
                        const int wc = static_cast<int>(out.jointWeights.size());
                        std::vector<GfVec4f> w;
                        std::vector<int> ji;
                        w.reserve(used.size());
                        ji.reserve(used.size() * 4);
                        for (int o : used)
                        {
                            const bool ok = o < wc;
                            w.push_back(ok ? out.jointWeights[o] : GfVec4f(0));
                            for (int k = 0; k < 4; ++k)
                                ji.push_back(ok ? out.jointIndices[o * 4 + k] : 0);
                        }
                        out.jointWeights.swap(w);
                        out.jointIndices.swap(ji);
                    }
                    for (auto& mt : out.morphTargets)
                    {
                        gather3(mt.positionDeltas);
                        gather3(mt.normalDeltas);
                    }
                }
            }

            outDoc->meshes.push_back(std::move(out));
        }
    }

    // -----------------------------------------------------------------------
    // VRM extension JSON: humanoid mapping + meta (normalized across 0.x/1.0).
    // -----------------------------------------------------------------------
    if (!outDoc->rawVrmExtensionJson.empty())
    {
        JsParseError perr;
        JsValue root = JsParseString(outDoc->rawVrmExtensionJson, &perr);
        const JsObject* rootObj = _AsObject(&root);
        if (!rootObj)
        {
            outDoc->warnings.push_back(
                VrmDiagMsg(VrmDiag::VrmJsonParseFailed, "VRM extension JSON could not be parsed"));
        }
        else
        {
            auto nodeIndexToJoint = [&](int nodeIndex) -> int
            {
                if (nodeIndex < 0 || nodeIndex >= static_cast<int>(data->nodes_count))
                {
                    return -1;
                }
                auto it = nodeToJoint.find(&data->nodes[nodeIndex]);
                return it != nodeToJoint.end() ? it->second : -1;
            };

            // A glTF node / mesh may expand into several canonical primitives;
            // expression binds reference a node (VRM 1.0) or mesh (VRM 0.x) plus
            // a morph-target index, which we fan out to the matching primitives.
            std::unordered_map<int, std::vector<int>> nodeToPrims, meshToPrims;
            for (int mi = 0; mi < static_cast<int>(outDoc->meshes.size()); ++mi)
            {
                nodeToPrims[outDoc->meshes[mi].sourceNodeIndex].push_back(mi);
                meshToPrims[outDoc->meshes[mi].sourceMeshIndex].push_back(mi);
            }
            auto readWeight = [&](const JsValue* wv, float fallback) -> float
            {
                if (wv && wv->IsReal())
                    return static_cast<float>(wv->GetReal());
                if (wv && wv->IsInt())
                    return static_cast<float>(wv->GetInt());
                return fallback;
            };
            auto addBinds = [&](VrmExpression& expr, const std::vector<int>* prims, int morphIndex,
                                float weight)
            {
                if (!prims)
                    return;
                bool sawPrim = false, bound = false;
                for (int mi : *prims)
                {
                    sawPrim = true;
                    if (morphIndex >= 0 &&
                        morphIndex < static_cast<int>(outDoc->meshes[mi].morphTargets.size()))
                    {
                        expr.morphBinds.push_back({mi, morphIndex, weight});
                        bound = true;
                    }
                }
                // A bind that resolved to a mesh but names a morph index the mesh
                // doesn't have is a lossy drop, not a silent no-op.
                if (sawPrim && !bound && morphIndex >= 0)
                {
                    outDoc->warnings.push_back(
                        VrmDiagMsg(VrmDiag::ExpressionMorphIndexOutOfRange,
                                   "expression '" + expr.name + "': morph target index " +
                                       std::to_string(morphIndex) +
                                       " is out of range for its mesh; bind skipped"));
                }
            };

            if (outDoc->version == VrmVersion::Vrm1)
            {
                outDoc->specVersion = _Find(*rootObj, "specVersion")
                                          ? _Find(*rootObj, "specVersion")->GetString()
                                          : "1.0";
                if (const JsObject* meta = _AsObject(_Find(*rootObj, "meta")))
                {
                    outDoc->metaJson = JsWriteToString(JsValue(*meta));
                }
                // humanoid.humanBones: { "hips": { "node": N }, ... }
                if (const JsObject* hum = _AsObject(_Find(*rootObj, "humanoid")))
                {
                    if (const JsObject* bones = _AsObject(_Find(*hum, "humanBones")))
                    {
                        for (const auto& kv : *bones)
                        {
                            const JsObject* b = _AsObject(&kv.second);
                            int nodeIndex = b ? _AsInt(_Find(*b, "node")) : -1;
                            int joint = nodeIndexToJoint(nodeIndex);
                            if (joint >= 0)
                            {
                                outDoc->humanoidBones.push_back({kv.first, joint});
                            }
                            else
                            {
                                outDoc->warnings.push_back(
                                    VrmDiagMsg(VrmDiag::HumanoidBoneUnmapped,
                                               "humanoid bone '" + kv.first + "' references node " +
                                                   std::to_string(nodeIndex) +
                                                   " with no skeleton joint; skipped"));
                            }
                        }
                    }
                }
                // expressions.preset.<name> and expressions.custom.<name>,
                // each with morphTargetBinds: [{ node, index, weight(0..1) }].
                if (const JsObject* exprs = _AsObject(_Find(*rootObj, "expressions")))
                {
                    // `vrm:expressionName` is the only key a clip can resolve
                    // an avatar's binds through, so two expressions carrying
                    // the same one are not two expressions -- a resolver would
                    // silently bind whichever it reached first. A JSON object
                    // cannot repeat a key, but `preset` and `custom` are two
                    // objects and nothing stops both declaring "happy". Keep
                    // the first and say so; `usdVrmaFileFormat` applies the
                    // same rule to a clip (VRMA107).
                    std::set<std::string> claimedExpressionNames;
                    for (const char* group : {"preset", "custom"})
                    {
                        const JsObject* g = _AsObject(_Find(*exprs, group));
                        if (!g)
                            continue;
                        const bool preset = std::strcmp(group, "preset") == 0;
                        for (const auto& kv : *g)
                        {
                            const JsObject* e = _AsObject(&kv.second);
                            if (!e)
                                continue;
                            VrmExpression expr;
                            expr.name = kv.first;
                            expr.isPreset = preset;
                            if (!claimedExpressionNames.insert(expr.name).second)
                            {
                                outDoc->warnings.push_back(
                                    VrmDiagMsg(VrmDiag::ExpressionDuplicateName,
                                               "expression '" + expr.name +
                                                   "' is declared "
                                                   "more than once; the first declaration is kept "
                                                   "and the rest are preserved in "
                                                   "vrm:rawExtension only"));
                                continue;
                            }
                            const JsValue* ib = _Find(*e, "isBinary");
                            expr.isBinary = ib && ib->IsBool() && ib->GetBool();
                            // overrideBlink / overrideLookAt / overrideMouth.
                            // Carried, never applied: which expressions a value
                            // suppresses is a question about the whole sample
                            // and belongs to the consumer that has one.
                            //
                            // Three cases, and the difference between them is
                            // the whole point of the diagnostic. A field the
                            // file does not have says nothing, and nothing is
                            // authored. An unknown *token* is kept as the file
                            // spelled it -- dropping it would lose the only
                            // evidence of what the author meant -- and is
                            // reported. And a value that is not a token at all
                            // (a number, null, an empty string) cannot be
                            // authored onto a token attribute, so it survives
                            // in vrm:rawExtension alone; that one is reported
                            // *too*, because the file did state an arbitration
                            // and the stage will not, which is exactly the
                            // silent downgrade this code exists to prevent.
                            const auto readOverride = [&](const char* field) -> std::string
                            {
                                const JsValue* v = _Find(*e, field);
                                if (!v)
                                    return std::string();
                                const bool isToken = v->IsString() && !v->GetString().empty();
                                if (!isToken)
                                {
                                    outDoc->warnings.push_back(VrmDiagMsg(
                                        VrmDiag::ExpressionOverrideUnknown,
                                        "expression '" + expr.name + "' declares " + field +
                                            " with a value that is not a "
                                            "token; nothing is authored for it and it "
                                            "survives in vrm:rawExtension only"));
                                    return std::string();
                                }
                                const std::string token = v->GetString();
                                if (token != "none" && token != "block" && token != "blend")
                                {
                                    outDoc->warnings.push_back(
                                        VrmDiagMsg(VrmDiag::ExpressionOverrideUnknown,
                                                   "expression '" + expr.name + "' declares " +
                                                       field + " '" + token +
                                                       "', which is not "
                                                       "none, block or blend; it is carried "
                                                       "verbatim and no consumer will act on it"));
                                }
                                return token;
                            };
                            expr.overrideBlink = readOverride("overrideBlink");
                            expr.overrideLookAt = readOverride("overrideLookAt");
                            expr.overrideMouth = readOverride("overrideMouth");
                            if (const JsArray* binds = _AsArray(_Find(*e, "morphTargetBinds")))
                            {
                                for (const JsValue& bv : *binds)
                                {
                                    const JsObject* b = _AsObject(&bv);
                                    if (!b)
                                        continue;
                                    int node = _AsInt(_Find(*b, "node"));
                                    int index = _AsInt(_Find(*b, "index"));
                                    float w = readWeight(_Find(*b, "weight"), 1.0f);
                                    auto it = nodeToPrims.find(node);
                                    addBinds(expr, it != nodeToPrims.end() ? &it->second : nullptr,
                                             index, w);
                                }
                            }
                            // materialColorBinds: drive a material color slot to a
                            // target RGBA. material is a direct glTF material index.
                            if (const JsArray* cbinds = _AsArray(_Find(*e, "materialColorBinds")))
                            {
                                for (const JsValue& bv : *cbinds)
                                {
                                    const JsObject* b = _AsObject(&bv);
                                    if (!b)
                                        continue;
                                    int mat = _AsInt(_Find(*b, "material"));
                                    if (mat < 0 ||
                                        mat >= static_cast<int>(outDoc->materials.size()))
                                    {
                                        continue;
                                    }
                                    VrmExpression::MaterialColorBind mb;
                                    mb.materialIndex = mat;
                                    const JsValue* tv = _Find(*b, "type");
                                    mb.type = (tv && tv->IsString()) ? tv->GetString() : "color";
                                    GfVec4f c(1.0f);
                                    if (const JsArray* val = _AsArray(_Find(*b, "targetValue")))
                                    {
                                        for (size_t k = 0; k < val->size() && k < 4; ++k)
                                            c[k] = readWeight(&(*val)[k], c[k]);
                                    }
                                    mb.targetValue = c;
                                    expr.materialColorBinds.push_back(std::move(mb));
                                }
                            }
                            outDoc->expressions.push_back(std::move(expr));
                        }
                    }
                }
                // lookAt: { type: "bone"|"expression", rangeMap*… }. Eyes come
                // from the humanoid leftEye/rightEye bones (resolved below).
                if (const JsObject* la = _AsObject(_Find(*rootObj, "lookAt")))
                {
                    outDoc->lookAt.present = true;
                    const JsValue* ty = _Find(*la, "type");
                    outDoc->lookAt.type = (ty && ty->IsString()) ? ty->GetString() : "bone";
                    outDoc->lookAt.rawJson = JsWriteToString(JsValue(*la));
                }
            }
            else if (outDoc->version == VrmVersion::Vrm0)
            {
                outDoc->specVersion = _Find(*rootObj, "specVersion")
                                          ? _Find(*rootObj, "specVersion")->GetString()
                                          : "0.0";
                if (const JsObject* meta = _AsObject(_Find(*rootObj, "meta")))
                {
                    outDoc->metaJson = JsWriteToString(JsValue(*meta));
                }
                // humanoid.humanBones: [ { "bone": "hips", "node": N }, ... ]
                if (const JsObject* hum = _AsObject(_Find(*rootObj, "humanoid")))
                {
                    if (const JsArray* bones = _AsArray(_Find(*hum, "humanBones")))
                    {
                        std::set<std::string> seenBones;
                        for (const JsValue& e : *bones)
                        {
                            const JsObject* b = _AsObject(&e);
                            if (!b)
                                continue;
                            const JsValue* boneName = _Find(*b, "bone");
                            int nodeIndex = _AsInt(_Find(*b, "node"));
                            int joint = nodeIndexToJoint(nodeIndex);
                            if (boneName && boneName->IsString() &&
                                !seenBones.insert(boneName->GetString()).second)
                            {
                                outDoc->warnings.push_back(VrmDiagMsg(
                                    VrmDiag::HumanoidBoneDuplicate,
                                    "duplicate humanoid bone '" + boneName->GetString() +
                                        "'; keeping the first mapping and ignoring the rest"));
                                continue;
                            }
                            if (boneName && boneName->IsString() && joint >= 0)
                            {
                                outDoc->humanoidBones.push_back({boneName->GetString(), joint});
                            }
                            else if (boneName && boneName->IsString())
                            {
                                outDoc->warnings.push_back(VrmDiagMsg(
                                    VrmDiag::HumanoidBoneUnmapped,
                                    "humanoid bone '" + boneName->GetString() +
                                        "' references node " + std::to_string(nodeIndex) +
                                        " with no skeleton joint; skipped"));
                            }
                        }
                    }
                }
                // blendShapeMaster.blendShapeGroups: [{ name, presetName,
                // isBinary, binds: [{ mesh, index, weight(0..100) }] }].
                if (const JsObject* bsm = _AsObject(_Find(*rootObj, "blendShapeMaster")))
                {
                    if (const JsArray* groups = _AsArray(_Find(*bsm, "blendShapeGroups")))
                    {
                        // Same rule as the 1.0 branch above, and reachable more
                        // easily here: `blendShapeGroups` is an *array*, so a
                        // file can declare the same presetName twice outright.
                        std::set<std::string> claimedExpressionNames;
                        for (const JsValue& gv : *groups)
                        {
                            const JsObject* g = _AsObject(&gv);
                            if (!g)
                                continue;
                            const JsValue* nm = _Find(*g, "name");
                            const JsValue* pn = _Find(*g, "presetName");
                            std::string preset = (pn && pn->IsString()) ? pn->GetString() : "";
                            const bool isPreset = !preset.empty() && preset != "unknown";
                            VrmExpression expr;
                            expr.name =
                                isPreset ? _Vrm0PresetToVrm1(preset)
                                         : (nm && nm->IsString() ? nm->GetString() : std::string());
                            expr.isPreset = isPreset;
                            if (!claimedExpressionNames.insert(expr.name).second)
                            {
                                outDoc->warnings.push_back(
                                    VrmDiagMsg(VrmDiag::ExpressionDuplicateName,
                                               "expression '" + expr.name +
                                                   "' is declared "
                                                   "more than once; the first declaration is kept "
                                                   "and the rest are preserved in "
                                                   "vrm:rawExtension only"));
                                continue;
                            }
                            const JsValue* ib = _Find(*g, "isBinary");
                            expr.isBinary = ib && ib->IsBool() && ib->GetBool();
                            if (const JsArray* binds = _AsArray(_Find(*g, "binds")))
                            {
                                for (const JsValue& bv : *binds)
                                {
                                    const JsObject* b = _AsObject(&bv);
                                    if (!b)
                                        continue;
                                    int meshIdx = _AsInt(_Find(*b, "mesh"));
                                    int index = _AsInt(_Find(*b, "index"));
                                    // VRM 0.x weights are 0..100.
                                    float w = readWeight(_Find(*b, "weight"), 100.0f) / 100.0f;
                                    auto it = meshToPrims.find(meshIdx);
                                    addBinds(expr, it != meshToPrims.end() ? &it->second : nullptr,
                                             index, w);
                                }
                            }
                            // VRM 0.x material-value binds (MToon _Color etc.) are
                            // not mapped to USD; they remain in vrm:rawExtension.
                            if (_AsArray(_Find(*g, "materialValues")))
                            {
                                outDoc->warnings.push_back(VrmDiagMsg(
                                    VrmDiag::ExpressionVrm0MaterialValues,
                                    "expression '" + expr.name +
                                        "' has VRM 0.x materialValues binds; preserved in "
                                        "vrm:rawExtension only (not mapped to USD)"));
                            }
                            outDoc->expressions.push_back(std::move(expr));
                        }
                    }
                }
                // materialProperties[]: VRM 0.x MToon, aligned with glTF
                // materials by index. Typed into the canonical semantics (the
                // same fields a VRM 1.0 material lands in) and preserved
                // verbatim; the realizations still read the glTF core.
                if (const JsArray* mprops = _AsArray(_Find(*rootObj, "materialProperties")))
                {
                    std::vector<const JsObject*> mtoon(outDoc->materials.size(), nullptr);
                    for (size_t i = 0; i < mprops->size() && i < outDoc->materials.size(); ++i)
                    {
                        const JsObject* mp = _AsObject(&(*mprops)[i]);
                        if (!mp)
                            continue;
                        const JsValue* shader = _Find(*mp, "shader");
                        if (shader && shader->IsString() &&
                            shader->GetString().find("MToon") != std::string::npos)
                        {
                            mtoon[i] = mp;
                            outDoc->materials[i].isMToon = true;
                            // Only MToon blocks are surfaced (vrm:mtoon:raw); skip
                            // the serialization for Unlit/standard properties.
                            outDoc->materials[i].rawShaderJson = JsWriteToString(JsValue(*mp));
                        }
                    }

                    // 1.0 bounds renderQueueOffsetNumber to -9..0 (transparent)
                    // and 0..+9 (with z-write) where 0.x had a free queue, so
                    // the queues are ranked across the file's materials,
                    // keeping their order, as UniVRM's migration does: the
                    // highest transparent queue takes 0 and each lower one the
                    // next offset down; the lowest z-write queue takes 0 and
                    // each higher one the next offset up.
                    std::set<int> transparentQueues, zWriteQueues;
                    for (const JsObject* mp : mtoon)
                    {
                        if (!mp)
                            continue;
                        const _MToon0RenderMode mode = _MToon0Props(*mp).RenderMode();
                        if (mode == _MToon0RenderMode::Transparent)
                            transparentQueues.insert(_MToon0RawQueueOffset(*mp, mode));
                        else if (mode == _MToon0RenderMode::TransparentWithZWrite)
                            zWriteQueues.insert(_MToon0RawQueueOffset(*mp, mode));
                    }
                    std::map<int, int> transparentRank, zWriteRank;
                    int rank = 0;
                    for (auto it = transparentQueues.rbegin(); it != transparentQueues.rend(); ++it)
                        transparentRank[*it] = std::clamp(rank--, -9, 0);
                    rank = 0;
                    for (int q : zWriteQueues)
                        zWriteRank[q] = std::clamp(rank++, 0, 9);

                    for (size_t i = 0; i < mtoon.size(); ++i)
                    {
                        if (!mtoon[i])
                            continue;
                        const _MToon0RenderMode mode = _MToon0Props(*mtoon[i]).RenderMode();
                        int queueOffset = 0;
                        if (mode == _MToon0RenderMode::Transparent)
                            queueOffset = transparentRank[_MToon0RawQueueOffset(*mtoon[i], mode)];
                        else if (mode == _MToon0RenderMode::TransparentWithZWrite)
                            queueOffset = zWriteRank[_MToon0RawQueueOffset(*mtoon[i], mode)];
                        _ReadMToon0(*mtoon[i], queueOffset, texRefFromIndex,
                                    &outDoc->materials[i].semantics);
                    }
                }
                // firstPerson.lookAtTypeName ("Bone" | "BlendShape"). The 0.x
                // lookAt config lives directly under firstPerson alongside
                // unrelated first-person data (firstPersonBone, meshAnnotations),
                // so preserve only the lookAt-related keys (matching the 1.0
                // lookAt block) rather than the whole firstPerson object.
                if (const JsObject* fp = _AsObject(_Find(*rootObj, "firstPerson")))
                {
                    outDoc->lookAt.present = true;
                    const JsValue* tn = _Find(*fp, "lookAtTypeName");
                    std::string t = (tn && tn->IsString()) ? tn->GetString() : "Bone";
                    outDoc->lookAt.type = (t == "BlendShape") ? "expression" : "bone";
                    JsObject lookAtRaw;
                    for (const char* key :
                         {"lookAtTypeName", "lookAtHorizontalInner", "lookAtHorizontalOuter",
                          "lookAtVerticalDown", "lookAtVerticalUp"})
                    {
                        if (const JsValue* v = _Find(*fp, key))
                            lookAtRaw[key] = *v;
                    }
                    outDoc->lookAt.rawJson = JsWriteToString(JsValue(lookAtRaw));
                }
            }
            // Resolve lookAt eyes from the humanoid leftEye/rightEye bones
            // (both VRM versions express the eyes via the humanoid, not lookAt).
            if (outDoc->lookAt.present)
            {
                for (const VrmHumanoidBone& hb : outDoc->humanoidBones)
                {
                    if (hb.semanticName == "leftEye")
                        outDoc->lookAt.leftEyeJoint = hb.jointIndex;
                    else if (hb.semanticName == "rightEye")
                        outDoc->lookAt.rightEyeJoint = hb.jointIndex;
                }
            }
        }
    }

    // -----------------------------------------------------------------------
    // Skeletal animation. VRM files rarely embed animation, but an imported
    // .glb may. Each glTF clip's joint TRS channels are resampled onto the union
    // of their key times so the result maps straight onto UsdSkelAnimation.
    // (Morph-weight animation is a tracked follow-up.)
    // -----------------------------------------------------------------------
    if (data->animations_count > 0 && !outDoc->joints.empty())
    {
        // Read one key's `comp` floats, honoring the cubic-spline layout (which
        // stores in-tangent / value / out-tangent — we take the value vertex).
        auto readKey = [](const cgltf_animation_sampler* s, size_t key, int comp, float* out)
        {
            size_t idx =
                (s->interpolation == cgltf_interpolation_type_cubic_spline) ? key * 3 + 1 : key;
            cgltf_accessor_read_float(s->output, idx, out, comp);
        };
        // Sample a comp-vector channel at time t (STEP / LINEAR; cubic spline is
        // approximated as linear between value vertices). comp==4 => quaternion.
        // `keys` is the sampler's input times, pre-read once by the caller, so
        // the interval is found by binary search instead of rescanning the
        // accessor on every (time x joint x channel) sample.
        auto sample = [&](const cgltf_animation_sampler* s, const std::vector<float>& keys, float t,
                          int comp, float* out)
        {
            const size_t n = keys.size();
            if (n == 0)
                return;
            if (t <= keys.front() || n == 1)
            {
                readKey(s, 0, comp, out);
                return;
            }
            if (t >= keys.back())
            {
                readKey(s, n - 1, comp, out);
                return;
            }
            // keys[i] <= t < keys[hi]; upper_bound is strict so an exact key time
            // lands on that key (fixes STEP returning the previous key at t==key).
            const size_t hi =
                static_cast<size_t>(std::upper_bound(keys.begin(), keys.end(), t) - keys.begin());
            const size_t i = hi - 1;
            if (s->interpolation == cgltf_interpolation_type_step)
            {
                readKey(s, i, comp, out); // right-continuous hold on [keys[i], keys[hi])
                return;
            }
            const float a = keys[i], b = keys[hi];
            const float f = (b > a) ? (t - a) / (b - a) : 0.0f;
            float va[4], vb[4];
            readKey(s, i, comp, va);
            readKey(s, hi, comp, vb);
            if (comp == 4)
            { // quaternion (xyzw) -> slerp
                GfQuatd q = GfSlerp(f, GfQuatd(va[3], va[0], va[1], va[2]),
                                    GfQuatd(vb[3], vb[0], vb[1], vb[2]));
                out[0] = static_cast<float>(q.GetImaginary()[0]);
                out[1] = static_cast<float>(q.GetImaginary()[1]);
                out[2] = static_cast<float>(q.GetImaginary()[2]);
                out[3] = static_cast<float>(q.GetReal());
            }
            else
            {
                for (int c = 0; c < comp; ++c)
                    out[c] = va[c] + (vb[c] - va[c]) * f;
            }
        };

        // Each sampler's input times, read once and shared by the union-time
        // collection and the resampling below.
        std::unordered_map<const cgltf_animation_sampler*, std::vector<float>> samplerKeys;

        std::vector<std::string> rawAnimNames(data->animations_count);
        for (cgltf_size a = 0; a < data->animations_count; ++a)
        {
            rawAnimNames[a] = data->animations[a].name ? data->animations[a].name : "";
        }
        std::vector<std::string> animNames = VrmMakeUniqueNames(rawAnimNames, "Clip");

        for (cgltf_size a = 0; a < data->animations_count; ++a)
        {
            const cgltf_animation& ga = data->animations[a];
            std::set<float> timeSet;
            std::map<int, const cgltf_animation_sampler*> chT, chR, chS;
            bool cubicWarned = false;

            for (cgltf_size c = 0; c < ga.channels_count; ++c)
            {
                const cgltf_animation_channel& ch = ga.channels[c];
                if (!ch.target_node || !ch.sampler)
                    continue;
                auto jit = nodeToJoint.find(ch.target_node);
                if (jit == nodeToJoint.end())
                    continue; // non-joint channel
                std::map<int, const cgltf_animation_sampler*>* dst = nullptr;
                if (ch.target_path == cgltf_animation_path_type_translation)
                    dst = &chT;
                else if (ch.target_path == cgltf_animation_path_type_rotation)
                    dst = &chR;
                else if (ch.target_path == cgltf_animation_path_type_scale)
                    dst = &chS;
                else
                    continue;
                (*dst)[jit->second] = ch.sampler;
                if (ch.sampler->interpolation == cgltf_interpolation_type_cubic_spline &&
                    !cubicWarned)
                {
                    outDoc->warnings.push_back(
                        VrmDiagMsg(VrmDiag::AnimationCubicSpline,
                                   "animation '" + animNames[a] +
                                       "' uses CUBICSPLINE; approximated as linear"));
                    cubicWarned = true;
                }
                std::vector<float>& keys = samplerKeys[ch.sampler];
                if (keys.empty())
                { // read this sampler's input times once
                    const cgltf_accessor* in = ch.sampler->input;
                    keys.resize(in->count);
                    for (cgltf_size i = 0; i < in->count; ++i)
                        cgltf_accessor_read_float(in, i, &keys[i], 1);
                }
                timeSet.insert(keys.begin(), keys.end());
            }
            if (timeSet.empty())
                continue;

            VrmAnimation clip;
            clip.name = animNames[a];
            clip.times.assign(timeSet.begin(), timeSet.end());

            std::set<int> jointSet;
            for (auto& kv : chT)
                jointSet.insert(kv.first);
            for (auto& kv : chR)
                jointSet.insert(kv.first);
            for (auto& kv : chS)
                jointSet.insert(kv.first);
            clip.jointIndices.assign(jointSet.begin(), jointSet.end());

            // Rest TRS fills components a joint doesn't animate.
            const size_t nj = clip.jointIndices.size();
            std::vector<GfVec3f> restT(nj), restS(nj);
            std::vector<GfQuatf> restR(nj);
            for (size_t k = 0; k < nj; ++k)
            {
                GfTransform xf(outDoc->joints[clip.jointIndices[k]].restTransform);
                GfVec3d tr = xf.GetTranslation();
                restT[k] = GfVec3f(tr[0], tr[1], tr[2]);
                GfQuaternion q = xf.GetRotation().GetQuaternion();
                restR[k] =
                    GfQuatf(static_cast<float>(q.GetReal()),
                            GfVec3f(q.GetImaginary()[0], q.GetImaginary()[1], q.GetImaginary()[2]));
                GfVec3d sc = xf.GetScale();
                restS[k] = GfVec3f(sc[0], sc[1], sc[2]);
            }

            const size_t T = clip.times.size();
            clip.translations.resize(T);
            clip.rotations.resize(T);
            clip.scales.resize(T);
            for (size_t ti = 0; ti < T; ++ti)
            {
                const float t = clip.times[ti];
                clip.translations[ti].resize(nj);
                clip.rotations[ti].resize(nj);
                clip.scales[ti].resize(nj);
                for (size_t k = 0; k < nj; ++k)
                {
                    const int j = clip.jointIndices[k];
                    float v[4] = {0, 0, 0, 0};
                    auto itT = chT.find(j);
                    if (itT != chT.end())
                    {
                        sample(itT->second, samplerKeys[itT->second], t, 3, v);
                        clip.translations[ti][k] = GfVec3f(v[0], v[1], v[2]);
                    }
                    else
                    {
                        clip.translations[ti][k] = restT[k];
                    }
                    auto itR = chR.find(j);
                    if (itR != chR.end())
                    {
                        sample(itR->second, samplerKeys[itR->second], t, 4, v);
                        clip.rotations[ti][k] = GfQuatf(v[3], GfVec3f(v[0], v[1], v[2]));
                    }
                    else
                    {
                        clip.rotations[ti][k] = restR[k];
                    }
                    auto itS = chS.find(j);
                    if (itS != chS.end())
                    {
                        sample(itS->second, samplerKeys[itS->second], t, 3, v);
                        clip.scales[ti][k] = GfVec3f(v[0], v[1], v[2]);
                    }
                    else
                    {
                        clip.scales[ti][k] = restS[k];
                    }
                }
            }
            outDoc->animations.push_back(std::move(clip));
        }
    }

    // -----------------------------------------------------------------------
    // Secondary motion (SpringBone). VRM 1.0: top-level VRMC_springBone. VRM
    // 0.x: VRM.secondaryAnimation. Imported as data only (no simulation). Node
    // refs resolve to skeleton joints where possible; the source node index/name
    // (and the raw block) are kept for whatever can't be resolved.
    // -----------------------------------------------------------------------
    {
        VrmSecondaryMotion& sm = outDoc->secondaryMotion;
        auto nodeJoint = [&](int ni) -> int
        {
            if (ni < 0 || ni >= static_cast<int>(data->nodes_count))
                return -1;
            auto it = nodeToJoint.find(&data->nodes[ni]);
            return it != nodeToJoint.end() ? it->second : -1;
        };
        auto nodeName = [&](int ni) -> std::string
        {
            if (ni < 0 || ni >= static_cast<int>(data->nodes_count))
                return {};
            return data->nodes[ni].name ? data->nodes[ni].name : std::string();
        };
        auto fval = [&](const JsValue* v, float fb) -> float
        {
            if (v && v->IsReal())
                return static_cast<float>(v->GetReal());
            if (v && v->IsInt())
                return static_cast<float>(v->GetInt());
            return fb;
        };
        auto vec3 = [&](const JsValue* v, GfVec3f fb) -> GfVec3f
        {
            if (const JsArray* a = _AsArray(v))
            {
                if (a->size() >= 3)
                    return GfVec3f(fval(&(*a)[0], fb[0]), fval(&(*a)[1], fb[1]),
                                   fval(&(*a)[2], fb[2]));
            }
            if (const JsObject* o = _AsObject(v))
            { // VRM 0.x {x,y,z}
                return GfVec3f(fval(_Find(*o, "x"), fb[0]), fval(_Find(*o, "y"), fb[1]),
                               fval(_Find(*o, "z"), fb[2]));
            }
            return fb;
        };

        if (outDoc->version == VrmVersion::Vrm1 && !springBone1Json.empty())
        {
            JsParseError perr;
            JsValue root = JsParseString(springBone1Json, &perr);
            if (const JsObject* obj = _AsObject(&root))
            {
                sm.present = true;
                sm.rawJson = springBone1Json;
                if (const JsArray* cols = _AsArray(_Find(*obj, "colliders")))
                {
                    for (const JsValue& cv : *cols)
                    {
                        const JsObject* co = _AsObject(&cv);
                        if (!co)
                            continue;
                        VrmCollider c;
                        int ni = _AsInt(_Find(*co, "node"));
                        c.sourceNodeIndex = ni;
                        c.sourceNodeName = nodeName(ni);
                        c.jointIndex = nodeJoint(ni);
                        const JsObject* shape = _AsObject(_Find(*co, "shape"));
                        const JsObject* sph = shape ? _AsObject(_Find(*shape, "sphere")) : nullptr;
                        const JsObject* cap = shape ? _AsObject(_Find(*shape, "capsule")) : nullptr;
                        if (sph)
                        {
                            c.shape = "sphere";
                            c.offset = vec3(_Find(*sph, "offset"), GfVec3f(0));
                            c.radius = fval(_Find(*sph, "radius"), 0);
                        }
                        else if (cap)
                        {
                            c.shape = "capsule";
                            c.offset = vec3(_Find(*cap, "offset"), GfVec3f(0));
                            c.radius = fval(_Find(*cap, "radius"), 0);
                            c.tail = vec3(_Find(*cap, "tail"), GfVec3f(0));
                        }
                        sm.colliders.push_back(c);
                    }
                }
                if (const JsArray* grps = _AsArray(_Find(*obj, "colliderGroups")))
                {
                    for (const JsValue& gv : *grps)
                    {
                        const JsObject* g = _AsObject(&gv);
                        if (!g)
                            continue;
                        VrmColliderGroup grp;
                        const JsValue* nm = _Find(*g, "name");
                        grp.name =
                            (nm && nm->IsString())
                                ? nm->GetString()
                                : "ColliderGroup_" + std::to_string(sm.colliderGroups.size());
                        if (const JsArray* ci = _AsArray(_Find(*g, "colliders")))
                            for (const JsValue& iv : *ci)
                                grp.colliderIndices.push_back(_AsInt(&iv));
                        sm.colliderGroups.push_back(std::move(grp));
                    }
                }
                if (const JsArray* springs = _AsArray(_Find(*obj, "springs")))
                {
                    for (const JsValue& sv : *springs)
                    {
                        const JsObject* s = _AsObject(&sv);
                        if (!s)
                            continue;
                        VrmSpring spr;
                        const JsValue* nm = _Find(*s, "name");
                        spr.name = (nm && nm->IsString())
                                       ? nm->GetString()
                                       : "Spring_" + std::to_string(sm.springs.size());
                        int centerNode = _AsInt(_Find(*s, "center"));
                        spr.centerSourceNodeIndex = centerNode;
                        spr.centerSourceNodeName = nodeName(centerNode);
                        spr.centerJoint = nodeJoint(centerNode);
                        if (const JsArray* cg = _AsArray(_Find(*s, "colliderGroups")))
                            for (const JsValue& iv : *cg)
                            {
                                int gi = _AsInt(&iv);
                                if (gi < 0 || gi >= static_cast<int>(sm.colliderGroups.size()))
                                {
                                    outDoc->warnings.push_back(VrmDiagMsg(
                                        VrmDiag::SpringColliderGroupOutOfRange,
                                        "spring '" + spr.name + "': collider-group index " +
                                            std::to_string(gi) + " is out of range; dropped"));
                                    continue;
                                }
                                spr.colliderGroupIndices.push_back(gi);
                            }
                        if (const JsArray* joints = _AsArray(_Find(*s, "joints")))
                        {
                            for (const JsValue& jv : *joints)
                            {
                                const JsObject* j = _AsObject(&jv);
                                if (!j)
                                    continue;
                                VrmSpringJoint sj;
                                int ni = _AsInt(_Find(*j, "node"));
                                sj.sourceNodeIndex = ni;
                                sj.sourceNodeName = nodeName(ni);
                                sj.jointIndex = nodeJoint(ni);
                                sj.hitRadius = fval(_Find(*j, "hitRadius"), 0);
                                sj.stiffness = fval(_Find(*j, "stiffness"), 1);
                                sj.gravityPower = fval(_Find(*j, "gravityPower"), 0);
                                sj.dragForce = fval(_Find(*j, "dragForce"), 0.4f);
                                sj.gravityDir = vec3(_Find(*j, "gravityDir"), GfVec3f(0, -1, 0));
                                spr.joints.push_back(sj);
                            }
                        }
                        sm.springs.push_back(std::move(spr));
                    }
                }
            }
        }
        else if (outDoc->version == VrmVersion::Vrm0 && !outDoc->rawVrmExtensionJson.empty())
        {
            JsParseError perr;
            JsValue root = JsParseString(outDoc->rawVrmExtensionJson, &perr);
            const JsObject* vrm = _AsObject(&root);
            const JsObject* sa = vrm ? _AsObject(_Find(*vrm, "secondaryAnimation")) : nullptr;
            if (sa)
            {
                sm.present = true;
                sm.rawJson = JsWriteToString(JsValue(*sa));
                // colliderGroups: [{ node, colliders: [{ offset, radius }] }].
                if (const JsArray* grps = _AsArray(_Find(*sa, "colliderGroups")))
                {
                    for (const JsValue& gv : *grps)
                    {
                        const JsObject* g = _AsObject(&gv);
                        if (!g)
                            continue;
                        int ni = _AsInt(_Find(*g, "node"));
                        VrmColliderGroup grp;
                        grp.name = "ColliderGroup_" + std::to_string(sm.colliderGroups.size());
                        if (const JsArray* cs = _AsArray(_Find(*g, "colliders")))
                        {
                            for (const JsValue& cv : *cs)
                            {
                                const JsObject* co = _AsObject(&cv);
                                if (!co)
                                    continue;
                                VrmCollider c;
                                c.shape = "sphere"; // VRM 0.x is spheres only
                                c.sourceNodeIndex = ni;
                                c.sourceNodeName = nodeName(ni);
                                c.jointIndex = nodeJoint(ni);
                                c.offset = vec3(_Find(*co, "offset"), GfVec3f(0));
                                c.radius = fval(_Find(*co, "radius"), 0);
                                grp.colliderIndices.push_back(
                                    static_cast<int>(sm.colliders.size()));
                                sm.colliders.push_back(c);
                            }
                        }
                        sm.colliderGroups.push_back(std::move(grp));
                    }
                }
                // boneGroups: per-group params replicated onto each bone joint.
                if (const JsArray* bgs = _AsArray(_Find(*sa, "boneGroups")))
                {
                    for (const JsValue& bv : *bgs)
                    {
                        const JsObject* bg = _AsObject(&bv);
                        if (!bg)
                            continue;
                        VrmSpring spr;
                        const JsValue* cm = _Find(*bg, "comment");
                        spr.name = (cm && cm->IsString() && !cm->GetString().empty())
                                       ? cm->GetString()
                                       : "Spring_" + std::to_string(sm.springs.size());
                        int centerNode = _AsInt(_Find(*bg, "center"));
                        spr.centerSourceNodeIndex = centerNode;
                        spr.centerSourceNodeName = nodeName(centerNode);
                        spr.centerJoint = nodeJoint(centerNode);
                        // VRM 0.x spells stiffness "stiffiness".
                        float stiff = fval(_Find(*bg, "stiffiness"), 1);
                        float gp = fval(_Find(*bg, "gravityPower"), 0);
                        float df = fval(_Find(*bg, "dragForce"), 0.4f);
                        float hr = fval(_Find(*bg, "hitRadius"), 0);
                        GfVec3f gd = vec3(_Find(*bg, "gravityDir"), GfVec3f(0, -1, 0));
                        if (const JsArray* cg = _AsArray(_Find(*bg, "colliderGroups")))
                            for (const JsValue& iv : *cg)
                            {
                                int gi = _AsInt(&iv);
                                if (gi < 0 || gi >= static_cast<int>(sm.colliderGroups.size()))
                                {
                                    outDoc->warnings.push_back(VrmDiagMsg(
                                        VrmDiag::SpringColliderGroupOutOfRange,
                                        "spring '" + spr.name + "': collider-group index " +
                                            std::to_string(gi) + " is out of range; dropped"));
                                    continue;
                                }
                                spr.colliderGroupIndices.push_back(gi);
                            }
                        if (const JsArray* bones = _AsArray(_Find(*bg, "bones")))
                        {
                            for (const JsValue& nv : *bones)
                            {
                                int ni = _AsInt(&nv);
                                VrmSpringJoint sj;
                                sj.sourceNodeIndex = ni;
                                sj.sourceNodeName = nodeName(ni);
                                sj.jointIndex = nodeJoint(ni);
                                sj.stiffness = stiff;
                                sj.gravityPower = gp;
                                sj.dragForce = df;
                                sj.hitRadius = hr;
                                sj.gravityDir = gd;
                                spr.joints.push_back(sj);
                            }
                        }
                        sm.springs.push_back(std::move(spr));
                    }
                }
            }
        }
    }

    // -----------------------------------------------------------------------
    // Node constraints (VRMC_node_constraint, VRM 1.0 only). Each constrained
    // node carries the extension and names a source node; imported as data only.
    // -----------------------------------------------------------------------
    if (outDoc->version == VrmVersion::Vrm1)
    {
        auto nodeJoint = [&](int ni) -> int
        {
            if (ni < 0 || ni >= static_cast<int>(data->nodes_count))
                return -1;
            auto it = nodeToJoint.find(&data->nodes[ni]);
            return it != nodeToJoint.end() ? it->second : -1;
        };
        auto nodeName = [&](int ni) -> std::string
        {
            if (ni < 0 || ni >= static_cast<int>(data->nodes_count))
                return {};
            return data->nodes[ni].name ? data->nodes[ni].name : std::string();
        };
        for (cgltf_size ni = 0; ni < data->nodes_count; ++ni)
        {
            const cgltf_node& node = data->nodes[ni];
            for (cgltf_size e = 0; e < node.extensions_count; ++e)
            {
                if (!node.extensions[e].name || !node.extensions[e].data ||
                    std::strcmp(node.extensions[e].name, "VRMC_node_constraint") != 0)
                {
                    continue;
                }
                JsParseError perr;
                JsValue root = JsParseString(node.extensions[e].data, &perr);
                const JsObject* obj = _AsObject(&root);
                const JsObject* c = obj ? _AsObject(_Find(*obj, "constraint")) : nullptr;
                if (!c)
                    continue;

                const JsObject* spec = nullptr;
                const char* axisKey = nullptr;
                std::string type;
                if ((spec = _AsObject(_Find(*c, "roll"))))
                {
                    type = "roll";
                    axisKey = "rollAxis";
                }
                else if ((spec = _AsObject(_Find(*c, "aim"))))
                {
                    type = "aim";
                    axisKey = "aimAxis";
                }
                else if ((spec = _AsObject(_Find(*c, "rotation"))))
                {
                    type = "rotation";
                }
                if (!spec)
                    continue;

                VrmConstraint con;
                con.type = type;
                con.rawJson = node.extensions[e].data;
                con.constrainedNodeIndex = static_cast<int>(ni);
                con.constrainedNodeName = nodeName(static_cast<int>(ni));
                con.constrainedJoint = nodeJoint(static_cast<int>(ni));
                int src = _AsInt(_Find(*spec, "source"));
                if (src < 0 || src >= static_cast<int>(data->nodes_count))
                {
                    outDoc->warnings.push_back(
                        VrmDiagMsg(VrmDiag::ConstraintNoSource,
                                   "node constraint on '" + con.constrainedNodeName + "' (" + type +
                                       ") has no valid source node; skipped"));
                    continue;
                }
                con.sourceNodeIndex = src;
                con.sourceNodeName = nodeName(src);
                con.sourceJoint = nodeJoint(src);
                if (axisKey)
                {
                    const JsValue* ax = _Find(*spec, axisKey);
                    if (ax && ax->IsString())
                        con.axis = ax->GetString();
                }
                const JsValue* w = _Find(*spec, "weight");
                con.weight = (w && w->IsReal())  ? static_cast<float>(w->GetReal())
                             : (w && w->IsInt()) ? static_cast<float>(w->GetInt())
                                                 : 1.0f;
                outDoc->constraints.push_back(std::move(con));
            }
        }
    }

    cgltf_free(data);
    return true;
}

PXR_NAMESPACE_CLOSE_SCOPE
