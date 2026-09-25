// SPDX-License-Identifier: Apache-2.0
#include "usd/MaterialSemantics.h"

#include <vrmSchema/vrmMToonAPI.h>
#include <vrmSchema/vrmMaterialAPI.h>
#include <vrmSchema/vrmTextureInfoAPI.h>

#include "pxr/base/tf/token.h"
#include "pxr/usd/sdf/assetPath.h"
#include "pxr/usd/usd/prim.h"

PXR_NAMESPACE_OPEN_SCOPE

namespace
{

// The model keeps a sampler wrap in UsdUVTexture's words, which /preview
// authors as-is; the canonical attribute is glTF's word (schema contract).
const char*
_GltfWrap(const std::string& wrap)
{
    if (wrap == "clamp")
        return "clampToEdge";
    if (wrap == "mirror")
        return "mirroredRepeat";
    return "repeat";
}

std::string
_ModelWrap(const TfToken& wrap)
{
    if (wrap == "clampToEdge")
        return "clamp";
    if (wrap == "mirroredRepeat")
        return "mirror";
    return "repeat";
}

// The contribution scalar a role carries, and under which name: glTF's normal
// scale and MToon's shading-shift scale are `scale`, glTF's occlusion strength
// is `strength`. The model carries all three in VrmTextureRef::scale.
enum class _Contribution
{
    None,
    Scale,
    Strength,
};

_Contribution
_ContributionOf(const std::string& role)
{
    if (role == "normal" || role == "shadingShift")
        return _Contribution::Scale;
    if (role == "occlusion")
        return _Contribution::Strength;
    return _Contribution::None;
}

template <typename T>
void
_Get(const UsdAttribute& attr, T* value)
{
    if (attr)
        attr.Get(value);
}

} // namespace

// Every value is authored, the specification defaults included. A reader would
// get a default from the schema fallback, but a realization connected to a
// canonical input would not -- UsdShade resolves an interface connection to an
// authored value only (material policy §6.4.1).
//
// The one exception is a texture's `transform:*`, authored only when the
// source states a KHR_texture_transform. Its fallback is the identity, so a
// reader sees the same UV mapping either way; what the difference keeps is
// whether the source said it, which a realization reproduces (an identity
// transform stated in the source is a node in /preview, as it always was).
void
UsdVrmAuthorMaterialSemantics(const UsdShadeMaterial& material, const VrmMaterialSemantics& s)
{
    const UsdPrim prim = material.GetPrim();

    UsdVrmMaterialAPI core = UsdVrmMaterialAPI::Apply(prim);
    core.CreateBaseColorFactorAttr().Set(s.baseColorFactor);
    core.CreateBaseColorAlphaFactorAttr().Set(s.baseColorAlphaFactor);
    core.CreateMetallicFactorAttr().Set(s.metallicFactor);
    core.CreateRoughnessFactorAttr().Set(s.roughnessFactor);
    core.CreateEmissiveFactorAttr().Set(s.emissiveFactor);
    core.CreateEmissiveStrengthAttr().Set(s.emissiveStrength);
    core.CreateAlphaModeAttr().Set(TfToken(s.alphaMode));
    core.CreateAlphaCutoffAttr().Set(s.alphaCutoff);
    core.CreateDoubleSidedAttr().Set(s.doubleSided);
    core.CreateUnlitAttr().Set(s.unlit);

    if (s.hasMToon)
    {
        const VrmMToonSemantics& t = s.mtoon;
        UsdVrmMToonAPI mtoon = UsdVrmMToonAPI::Apply(prim);
        mtoon.CreateSpecVersionAttr().Set(TfToken(t.specVersion));
        mtoon.CreateTransparentWithZWriteAttr().Set(t.transparentWithZWrite);
        mtoon.CreateRenderQueueOffsetNumberAttr().Set(t.renderQueueOffsetNumber);
        mtoon.CreateShadeColorFactorAttr().Set(t.shadeColorFactor);
        mtoon.CreateShadingShiftFactorAttr().Set(t.shadingShiftFactor);
        mtoon.CreateShadingToonyFactorAttr().Set(t.shadingToonyFactor);
        mtoon.CreateGiEqualizationFactorAttr().Set(t.giEqualizationFactor);
        mtoon.CreateMatcapFactorAttr().Set(t.matcapFactor);
        mtoon.CreateParametricRimColorFactorAttr().Set(t.parametricRimColorFactor);
        mtoon.CreateParametricRimFresnelPowerFactorAttr().Set(t.parametricRimFresnelPowerFactor);
        mtoon.CreateParametricRimLiftFactorAttr().Set(t.parametricRimLiftFactor);
        mtoon.CreateRimLightingMixFactorAttr().Set(t.rimLightingMixFactor);
        mtoon.CreateOutlineWidthModeAttr().Set(TfToken(t.outlineWidthMode));
        mtoon.CreateOutlineWidthFactorAttr().Set(t.outlineWidthFactor);
        mtoon.CreateOutlineColorFactorAttr().Set(t.outlineColorFactor);
        mtoon.CreateOutlineLightingMixFactorAttr().Set(t.outlineLightingMixFactor);
        mtoon.CreateUvAnimationScrollXSpeedFactorAttr().Set(t.uvAnimationScrollXSpeedFactor);
        mtoon.CreateUvAnimationScrollYSpeedFactorAttr().Set(t.uvAnimationScrollYSpeedFactor);
        mtoon.CreateUvAnimationRotationSpeedFactorAttr().Set(t.uvAnimationRotationSpeedFactor);
    }

    for (const auto& [role, ref] : s.textures)
    {
        UsdVrmTextureInfoAPI info = UsdVrmTextureInfoAPI::Apply(prim, TfToken(role));
        info.CreateFileAttr().Set(SdfAssetPath(ref.filePath));
        info.CreateTexCoordAttr().Set(ref.uvSet);
        info.CreateWrapSAttr().Set(TfToken(_GltfWrap(ref.wrapS)));
        info.CreateWrapTAttr().Set(TfToken(_GltfWrap(ref.wrapT)));
        switch (_ContributionOf(role))
        {
        case _Contribution::Scale:
            info.CreateScaleAttr().Set(ref.scale);
            break;
        case _Contribution::Strength:
            info.CreateStrengthAttr().Set(ref.scale);
            break;
        case _Contribution::None:
            break;
        }
        if (ref.hasTransform)
        {
            info.CreateTransformOffsetAttr().Set(ref.uvOffset);
            info.CreateTransformRotationAttr().Set(ref.uvRotation);
            info.CreateTransformScaleAttr().Set(ref.uvScale);
        }
    }
}

VrmMaterialSemantics
UsdVrmReadMaterialSemantics(const UsdShadeMaterial& material)
{
    const UsdPrim prim = material.GetPrim();
    VrmMaterialSemantics s;

    const UsdVrmMaterialAPI core(prim);
    _Get(core.GetBaseColorFactorAttr(), &s.baseColorFactor);
    _Get(core.GetBaseColorAlphaFactorAttr(), &s.baseColorAlphaFactor);
    _Get(core.GetMetallicFactorAttr(), &s.metallicFactor);
    _Get(core.GetRoughnessFactorAttr(), &s.roughnessFactor);
    _Get(core.GetEmissiveFactorAttr(), &s.emissiveFactor);
    _Get(core.GetEmissiveStrengthAttr(), &s.emissiveStrength);
    TfToken alphaMode; // no schema fallback: unauthored is OPAQUE, as in glTF
    if (core.GetAlphaModeAttr() && core.GetAlphaModeAttr().Get(&alphaMode) && !alphaMode.IsEmpty())
        s.alphaMode = alphaMode.GetString();
    _Get(core.GetAlphaCutoffAttr(), &s.alphaCutoff);
    _Get(core.GetDoubleSidedAttr(), &s.doubleSided);
    _Get(core.GetUnlitAttr(), &s.unlit);

    s.hasMToon = prim.HasAPI<UsdVrmMToonAPI>();
    if (s.hasMToon)
    {
        const UsdVrmMToonAPI mtoon(prim);
        VrmMToonSemantics& t = s.mtoon;
        TfToken token;
        if (mtoon.GetSpecVersionAttr().Get(&token))
            t.specVersion = token.GetString();
        _Get(mtoon.GetTransparentWithZWriteAttr(), &t.transparentWithZWrite);
        _Get(mtoon.GetRenderQueueOffsetNumberAttr(), &t.renderQueueOffsetNumber);
        _Get(mtoon.GetShadeColorFactorAttr(), &t.shadeColorFactor);
        _Get(mtoon.GetShadingShiftFactorAttr(), &t.shadingShiftFactor);
        _Get(mtoon.GetShadingToonyFactorAttr(), &t.shadingToonyFactor);
        _Get(mtoon.GetGiEqualizationFactorAttr(), &t.giEqualizationFactor);
        _Get(mtoon.GetMatcapFactorAttr(), &t.matcapFactor);
        _Get(mtoon.GetParametricRimColorFactorAttr(), &t.parametricRimColorFactor);
        _Get(mtoon.GetParametricRimFresnelPowerFactorAttr(), &t.parametricRimFresnelPowerFactor);
        _Get(mtoon.GetParametricRimLiftFactorAttr(), &t.parametricRimLiftFactor);
        _Get(mtoon.GetRimLightingMixFactorAttr(), &t.rimLightingMixFactor);
        if (mtoon.GetOutlineWidthModeAttr().Get(&token))
            t.outlineWidthMode = token.GetString();
        _Get(mtoon.GetOutlineWidthFactorAttr(), &t.outlineWidthFactor);
        _Get(mtoon.GetOutlineColorFactorAttr(), &t.outlineColorFactor);
        _Get(mtoon.GetOutlineLightingMixFactorAttr(), &t.outlineLightingMixFactor);
        _Get(mtoon.GetUvAnimationScrollXSpeedFactorAttr(), &t.uvAnimationScrollXSpeedFactor);
        _Get(mtoon.GetUvAnimationScrollYSpeedFactorAttr(), &t.uvAnimationScrollYSpeedFactor);
        _Get(mtoon.GetUvAnimationRotationSpeedFactorAttr(), &t.uvAnimationRotationSpeedFactor);
    }

    for (const UsdVrmTextureInfoAPI& info : UsdVrmTextureInfoAPI::GetAll(prim))
    {
        const std::string role = info.GetName().GetString();
        VrmTextureRef ref;
        SdfAssetPath file;
        if (!info.GetFileAttr().Get(&file) || file.GetAssetPath().empty())
            continue;
        ref.present = true;
        ref.filePath = file.GetAssetPath();
        _Get(info.GetTexCoordAttr(), &ref.uvSet);
        TfToken wrap;
        if (info.GetWrapSAttr().Get(&wrap))
            ref.wrapS = _ModelWrap(wrap);
        if (info.GetWrapTAttr().Get(&wrap))
            ref.wrapT = _ModelWrap(wrap);
        switch (_ContributionOf(role))
        {
        case _Contribution::Scale:
            _Get(info.GetScaleAttr(), &ref.scale);
            break;
        case _Contribution::Strength:
            _Get(info.GetStrengthAttr(), &ref.scale);
            break;
        case _Contribution::None:
            break;
        }
        ref.hasTransform = info.GetTransformOffsetAttr().HasAuthoredValue() ||
                           info.GetTransformRotationAttr().HasAuthoredValue() ||
                           info.GetTransformScaleAttr().HasAuthoredValue();
        _Get(info.GetTransformOffsetAttr(), &ref.uvOffset);
        _Get(info.GetTransformRotationAttr(), &ref.uvRotation);
        _Get(info.GetTransformScaleAttr(), &ref.uvScale);
        s.textures[role] = std::move(ref);
    }
    return s;
}

PXR_NAMESPACE_CLOSE_SCOPE
