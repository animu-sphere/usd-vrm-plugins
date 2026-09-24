//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "./vrmMToonAPI.h"
#include "pxr/usd/usd/schemaRegistry.h"
#include "pxr/usd/usd/typed.h"

#include "pxr/usd/sdf/types.h"
#include "pxr/usd/sdf/assetPath.h"

PXR_NAMESPACE_OPEN_SCOPE

// Register the schema with the TfType system.
TF_REGISTRY_FUNCTION(TfType)
{
    TfType::Define<UsdVrmMToonAPI, TfType::Bases<UsdAPISchemaBase>>();
}

/* virtual */
UsdVrmMToonAPI::~UsdVrmMToonAPI()
{
}

/* static */
UsdVrmMToonAPI
UsdVrmMToonAPI::Get(const UsdStagePtr& stage, const SdfPath& path)
{
    if (!stage)
    {
        TF_CODING_ERROR("Invalid stage");
        return UsdVrmMToonAPI();
    }
    return UsdVrmMToonAPI(stage->GetPrimAtPath(path));
}

/* virtual */
UsdSchemaKind
UsdVrmMToonAPI::_GetSchemaKind() const
{
    return UsdVrmMToonAPI::schemaKind;
}

/* static */
bool
UsdVrmMToonAPI::CanApply(const UsdPrim& prim, std::string* whyNot)
{
    return prim.CanApplyAPI<UsdVrmMToonAPI>(whyNot);
}

/* static */
UsdVrmMToonAPI
UsdVrmMToonAPI::Apply(const UsdPrim& prim)
{
    if (prim.ApplyAPI<UsdVrmMToonAPI>())
    {
        return UsdVrmMToonAPI(prim);
    }
    return UsdVrmMToonAPI();
}

/* static */
const TfType&
UsdVrmMToonAPI::_GetStaticTfType()
{
    static TfType tfType = TfType::Find<UsdVrmMToonAPI>();
    return tfType;
}

/* static */
bool
UsdVrmMToonAPI::_IsTypedSchema()
{
    static bool isTyped = _GetStaticTfType().IsA<UsdTyped>();
    return isTyped;
}

/* virtual */
const TfType&
UsdVrmMToonAPI::_GetTfType() const
{
    return _GetStaticTfType();
}

UsdAttribute
UsdVrmMToonAPI::GetSpecVersionAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->inputsVrmMtoonSpecVersion);
}

UsdAttribute
UsdVrmMToonAPI::CreateSpecVersionAttr(VtValue const& defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(
        UsdVrmTokens->inputsVrmMtoonSpecVersion, SdfValueTypeNames->Token,
        /* custom = */ false, SdfVariabilityUniform, defaultValue, writeSparsely);
}

UsdAttribute
UsdVrmMToonAPI::GetTransparentWithZWriteAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->inputsVrmMtoonTransparentWithZWrite);
}

UsdAttribute
UsdVrmMToonAPI::CreateTransparentWithZWriteAttr(VtValue const& defaultValue,
                                                bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(
        UsdVrmTokens->inputsVrmMtoonTransparentWithZWrite, SdfValueTypeNames->Bool,
        /* custom = */ false, SdfVariabilityUniform, defaultValue, writeSparsely);
}

UsdAttribute
UsdVrmMToonAPI::GetRenderQueueOffsetNumberAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->inputsVrmMtoonRenderQueueOffsetNumber);
}

UsdAttribute
UsdVrmMToonAPI::CreateRenderQueueOffsetNumberAttr(VtValue const& defaultValue,
                                                  bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(
        UsdVrmTokens->inputsVrmMtoonRenderQueueOffsetNumber, SdfValueTypeNames->Int,
        /* custom = */ false, SdfVariabilityUniform, defaultValue, writeSparsely);
}

UsdAttribute
UsdVrmMToonAPI::GetShadeColorFactorAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->inputsVrmMtoonShadeColorFactor);
}

UsdAttribute
UsdVrmMToonAPI::CreateShadeColorFactorAttr(VtValue const& defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(
        UsdVrmTokens->inputsVrmMtoonShadeColorFactor, SdfValueTypeNames->Color3f,
        /* custom = */ false, SdfVariabilityVarying, defaultValue, writeSparsely);
}

UsdAttribute
UsdVrmMToonAPI::GetShadingShiftFactorAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->inputsVrmMtoonShadingShiftFactor);
}

UsdAttribute
UsdVrmMToonAPI::CreateShadingShiftFactorAttr(VtValue const& defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(
        UsdVrmTokens->inputsVrmMtoonShadingShiftFactor, SdfValueTypeNames->Float,
        /* custom = */ false, SdfVariabilityVarying, defaultValue, writeSparsely);
}

UsdAttribute
UsdVrmMToonAPI::GetShadingToonyFactorAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->inputsVrmMtoonShadingToonyFactor);
}

UsdAttribute
UsdVrmMToonAPI::CreateShadingToonyFactorAttr(VtValue const& defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(
        UsdVrmTokens->inputsVrmMtoonShadingToonyFactor, SdfValueTypeNames->Float,
        /* custom = */ false, SdfVariabilityVarying, defaultValue, writeSparsely);
}

UsdAttribute
UsdVrmMToonAPI::GetGiEqualizationFactorAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->inputsVrmMtoonGiEqualizationFactor);
}

UsdAttribute
UsdVrmMToonAPI::CreateGiEqualizationFactorAttr(VtValue const& defaultValue,
                                               bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(
        UsdVrmTokens->inputsVrmMtoonGiEqualizationFactor, SdfValueTypeNames->Float,
        /* custom = */ false, SdfVariabilityVarying, defaultValue, writeSparsely);
}

UsdAttribute
UsdVrmMToonAPI::GetMatcapFactorAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->inputsVrmMtoonMatcapFactor);
}

UsdAttribute
UsdVrmMToonAPI::CreateMatcapFactorAttr(VtValue const& defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(
        UsdVrmTokens->inputsVrmMtoonMatcapFactor, SdfValueTypeNames->Color3f,
        /* custom = */ false, SdfVariabilityVarying, defaultValue, writeSparsely);
}

UsdAttribute
UsdVrmMToonAPI::GetParametricRimColorFactorAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->inputsVrmMtoonParametricRimColorFactor);
}

UsdAttribute
UsdVrmMToonAPI::CreateParametricRimColorFactorAttr(VtValue const& defaultValue,
                                                   bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(
        UsdVrmTokens->inputsVrmMtoonParametricRimColorFactor, SdfValueTypeNames->Color3f,
        /* custom = */ false, SdfVariabilityVarying, defaultValue, writeSparsely);
}

UsdAttribute
UsdVrmMToonAPI::GetParametricRimFresnelPowerFactorAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->inputsVrmMtoonParametricRimFresnelPowerFactor);
}

UsdAttribute
UsdVrmMToonAPI::CreateParametricRimFresnelPowerFactorAttr(VtValue const& defaultValue,
                                                          bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(
        UsdVrmTokens->inputsVrmMtoonParametricRimFresnelPowerFactor, SdfValueTypeNames->Float,
        /* custom = */ false, SdfVariabilityVarying, defaultValue, writeSparsely);
}

UsdAttribute
UsdVrmMToonAPI::GetParametricRimLiftFactorAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->inputsVrmMtoonParametricRimLiftFactor);
}

UsdAttribute
UsdVrmMToonAPI::CreateParametricRimLiftFactorAttr(VtValue const& defaultValue,
                                                  bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(
        UsdVrmTokens->inputsVrmMtoonParametricRimLiftFactor, SdfValueTypeNames->Float,
        /* custom = */ false, SdfVariabilityVarying, defaultValue, writeSparsely);
}

UsdAttribute
UsdVrmMToonAPI::GetRimLightingMixFactorAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->inputsVrmMtoonRimLightingMixFactor);
}

UsdAttribute
UsdVrmMToonAPI::CreateRimLightingMixFactorAttr(VtValue const& defaultValue,
                                               bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(
        UsdVrmTokens->inputsVrmMtoonRimLightingMixFactor, SdfValueTypeNames->Float,
        /* custom = */ false, SdfVariabilityVarying, defaultValue, writeSparsely);
}

UsdAttribute
UsdVrmMToonAPI::GetOutlineWidthModeAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->inputsVrmMtoonOutlineWidthMode);
}

UsdAttribute
UsdVrmMToonAPI::CreateOutlineWidthModeAttr(VtValue const& defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(
        UsdVrmTokens->inputsVrmMtoonOutlineWidthMode, SdfValueTypeNames->Token,
        /* custom = */ false, SdfVariabilityUniform, defaultValue, writeSparsely);
}

UsdAttribute
UsdVrmMToonAPI::GetOutlineWidthFactorAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->inputsVrmMtoonOutlineWidthFactor);
}

UsdAttribute
UsdVrmMToonAPI::CreateOutlineWidthFactorAttr(VtValue const& defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(
        UsdVrmTokens->inputsVrmMtoonOutlineWidthFactor, SdfValueTypeNames->Float,
        /* custom = */ false, SdfVariabilityVarying, defaultValue, writeSparsely);
}

UsdAttribute
UsdVrmMToonAPI::GetOutlineColorFactorAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->inputsVrmMtoonOutlineColorFactor);
}

UsdAttribute
UsdVrmMToonAPI::CreateOutlineColorFactorAttr(VtValue const& defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(
        UsdVrmTokens->inputsVrmMtoonOutlineColorFactor, SdfValueTypeNames->Color3f,
        /* custom = */ false, SdfVariabilityVarying, defaultValue, writeSparsely);
}

UsdAttribute
UsdVrmMToonAPI::GetOutlineLightingMixFactorAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->inputsVrmMtoonOutlineLightingMixFactor);
}

UsdAttribute
UsdVrmMToonAPI::CreateOutlineLightingMixFactorAttr(VtValue const& defaultValue,
                                                   bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(
        UsdVrmTokens->inputsVrmMtoonOutlineLightingMixFactor, SdfValueTypeNames->Float,
        /* custom = */ false, SdfVariabilityVarying, defaultValue, writeSparsely);
}

UsdAttribute
UsdVrmMToonAPI::GetUvAnimationScrollXSpeedFactorAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->inputsVrmMtoonUvAnimationScrollXSpeedFactor);
}

UsdAttribute
UsdVrmMToonAPI::CreateUvAnimationScrollXSpeedFactorAttr(VtValue const& defaultValue,
                                                        bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(
        UsdVrmTokens->inputsVrmMtoonUvAnimationScrollXSpeedFactor, SdfValueTypeNames->Float,
        /* custom = */ false, SdfVariabilityVarying, defaultValue, writeSparsely);
}

UsdAttribute
UsdVrmMToonAPI::GetUvAnimationScrollYSpeedFactorAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->inputsVrmMtoonUvAnimationScrollYSpeedFactor);
}

UsdAttribute
UsdVrmMToonAPI::CreateUvAnimationScrollYSpeedFactorAttr(VtValue const& defaultValue,
                                                        bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(
        UsdVrmTokens->inputsVrmMtoonUvAnimationScrollYSpeedFactor, SdfValueTypeNames->Float,
        /* custom = */ false, SdfVariabilityVarying, defaultValue, writeSparsely);
}

UsdAttribute
UsdVrmMToonAPI::GetUvAnimationRotationSpeedFactorAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->inputsVrmMtoonUvAnimationRotationSpeedFactor);
}

UsdAttribute
UsdVrmMToonAPI::CreateUvAnimationRotationSpeedFactorAttr(VtValue const& defaultValue,
                                                         bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(
        UsdVrmTokens->inputsVrmMtoonUvAnimationRotationSpeedFactor, SdfValueTypeNames->Float,
        /* custom = */ false, SdfVariabilityVarying, defaultValue, writeSparsely);
}

namespace
{
static inline TfTokenVector
_ConcatenateAttributeNames(const TfTokenVector& left, const TfTokenVector& right)
{
    TfTokenVector result;
    result.reserve(left.size() + right.size());
    result.insert(result.end(), left.begin(), left.end());
    result.insert(result.end(), right.begin(), right.end());
    return result;
}
} // namespace

/*static*/
const TfTokenVector&
UsdVrmMToonAPI::GetSchemaAttributeNames(bool includeInherited)
{
    static TfTokenVector localNames = {
        UsdVrmTokens->inputsVrmMtoonSpecVersion,
        UsdVrmTokens->inputsVrmMtoonTransparentWithZWrite,
        UsdVrmTokens->inputsVrmMtoonRenderQueueOffsetNumber,
        UsdVrmTokens->inputsVrmMtoonShadeColorFactor,
        UsdVrmTokens->inputsVrmMtoonShadingShiftFactor,
        UsdVrmTokens->inputsVrmMtoonShadingToonyFactor,
        UsdVrmTokens->inputsVrmMtoonGiEqualizationFactor,
        UsdVrmTokens->inputsVrmMtoonMatcapFactor,
        UsdVrmTokens->inputsVrmMtoonParametricRimColorFactor,
        UsdVrmTokens->inputsVrmMtoonParametricRimFresnelPowerFactor,
        UsdVrmTokens->inputsVrmMtoonParametricRimLiftFactor,
        UsdVrmTokens->inputsVrmMtoonRimLightingMixFactor,
        UsdVrmTokens->inputsVrmMtoonOutlineWidthMode,
        UsdVrmTokens->inputsVrmMtoonOutlineWidthFactor,
        UsdVrmTokens->inputsVrmMtoonOutlineColorFactor,
        UsdVrmTokens->inputsVrmMtoonOutlineLightingMixFactor,
        UsdVrmTokens->inputsVrmMtoonUvAnimationScrollXSpeedFactor,
        UsdVrmTokens->inputsVrmMtoonUvAnimationScrollYSpeedFactor,
        UsdVrmTokens->inputsVrmMtoonUvAnimationRotationSpeedFactor,
    };
    static TfTokenVector allNames =
        _ConcatenateAttributeNames(UsdAPISchemaBase::GetSchemaAttributeNames(true), localNames);

    if (includeInherited)
        return allNames;
    else
        return localNames;
}

PXR_NAMESPACE_CLOSE_SCOPE

// ===================================================================== //
// Feel free to add custom code below this line. It will be preserved by
// the code generator.
//
// Just remember to wrap code in the appropriate delimiters:
// 'PXR_NAMESPACE_OPEN_SCOPE', 'PXR_NAMESPACE_CLOSE_SCOPE'.
// ===================================================================== //
// --(BEGIN CUSTOM CODE)--
