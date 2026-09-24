//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "./vrmMaterialAPI.h"
#include "pxr/usd/usd/schemaRegistry.h"
#include "pxr/usd/usd/typed.h"

#include "pxr/usd/sdf/types.h"
#include "pxr/usd/sdf/assetPath.h"

PXR_NAMESPACE_OPEN_SCOPE

// Register the schema with the TfType system.
TF_REGISTRY_FUNCTION(TfType)
{
    TfType::Define<UsdVrmMaterialAPI, TfType::Bases<UsdAPISchemaBase>>();
}

/* virtual */
UsdVrmMaterialAPI::~UsdVrmMaterialAPI()
{
}

/* static */
UsdVrmMaterialAPI
UsdVrmMaterialAPI::Get(const UsdStagePtr& stage, const SdfPath& path)
{
    if (!stage)
    {
        TF_CODING_ERROR("Invalid stage");
        return UsdVrmMaterialAPI();
    }
    return UsdVrmMaterialAPI(stage->GetPrimAtPath(path));
}

/* virtual */
UsdSchemaKind
UsdVrmMaterialAPI::_GetSchemaKind() const
{
    return UsdVrmMaterialAPI::schemaKind;
}

/* static */
bool
UsdVrmMaterialAPI::CanApply(const UsdPrim& prim, std::string* whyNot)
{
    return prim.CanApplyAPI<UsdVrmMaterialAPI>(whyNot);
}

/* static */
UsdVrmMaterialAPI
UsdVrmMaterialAPI::Apply(const UsdPrim& prim)
{
    if (prim.ApplyAPI<UsdVrmMaterialAPI>())
    {
        return UsdVrmMaterialAPI(prim);
    }
    return UsdVrmMaterialAPI();
}

/* static */
const TfType&
UsdVrmMaterialAPI::_GetStaticTfType()
{
    static TfType tfType = TfType::Find<UsdVrmMaterialAPI>();
    return tfType;
}

/* static */
bool
UsdVrmMaterialAPI::_IsTypedSchema()
{
    static bool isTyped = _GetStaticTfType().IsA<UsdTyped>();
    return isTyped;
}

/* virtual */
const TfType&
UsdVrmMaterialAPI::_GetTfType() const
{
    return _GetStaticTfType();
}

UsdAttribute
UsdVrmMaterialAPI::GetBaseColorFactorAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->inputsVrmMaterialBaseColorFactor);
}

UsdAttribute
UsdVrmMaterialAPI::CreateBaseColorFactorAttr(VtValue const& defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(
        UsdVrmTokens->inputsVrmMaterialBaseColorFactor, SdfValueTypeNames->Color3f,
        /* custom = */ false, SdfVariabilityVarying, defaultValue, writeSparsely);
}

UsdAttribute
UsdVrmMaterialAPI::GetBaseColorAlphaFactorAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->inputsVrmMaterialBaseColorAlphaFactor);
}

UsdAttribute
UsdVrmMaterialAPI::CreateBaseColorAlphaFactorAttr(VtValue const& defaultValue,
                                                  bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(
        UsdVrmTokens->inputsVrmMaterialBaseColorAlphaFactor, SdfValueTypeNames->Float,
        /* custom = */ false, SdfVariabilityVarying, defaultValue, writeSparsely);
}

UsdAttribute
UsdVrmMaterialAPI::GetMetallicFactorAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->inputsVrmMaterialMetallicFactor);
}

UsdAttribute
UsdVrmMaterialAPI::CreateMetallicFactorAttr(VtValue const& defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(
        UsdVrmTokens->inputsVrmMaterialMetallicFactor, SdfValueTypeNames->Float,
        /* custom = */ false, SdfVariabilityVarying, defaultValue, writeSparsely);
}

UsdAttribute
UsdVrmMaterialAPI::GetRoughnessFactorAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->inputsVrmMaterialRoughnessFactor);
}

UsdAttribute
UsdVrmMaterialAPI::CreateRoughnessFactorAttr(VtValue const& defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(
        UsdVrmTokens->inputsVrmMaterialRoughnessFactor, SdfValueTypeNames->Float,
        /* custom = */ false, SdfVariabilityVarying, defaultValue, writeSparsely);
}

UsdAttribute
UsdVrmMaterialAPI::GetEmissiveFactorAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->inputsVrmMaterialEmissiveFactor);
}

UsdAttribute
UsdVrmMaterialAPI::CreateEmissiveFactorAttr(VtValue const& defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(
        UsdVrmTokens->inputsVrmMaterialEmissiveFactor, SdfValueTypeNames->Color3f,
        /* custom = */ false, SdfVariabilityVarying, defaultValue, writeSparsely);
}

UsdAttribute
UsdVrmMaterialAPI::GetEmissiveStrengthAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->inputsVrmMaterialEmissiveStrength);
}

UsdAttribute
UsdVrmMaterialAPI::CreateEmissiveStrengthAttr(VtValue const& defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(
        UsdVrmTokens->inputsVrmMaterialEmissiveStrength, SdfValueTypeNames->Float,
        /* custom = */ false, SdfVariabilityVarying, defaultValue, writeSparsely);
}

UsdAttribute
UsdVrmMaterialAPI::GetAlphaModeAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->inputsVrmMaterialAlphaMode);
}

UsdAttribute
UsdVrmMaterialAPI::CreateAlphaModeAttr(VtValue const& defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(
        UsdVrmTokens->inputsVrmMaterialAlphaMode, SdfValueTypeNames->Token,
        /* custom = */ false, SdfVariabilityUniform, defaultValue, writeSparsely);
}

UsdAttribute
UsdVrmMaterialAPI::GetAlphaCutoffAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->inputsVrmMaterialAlphaCutoff);
}

UsdAttribute
UsdVrmMaterialAPI::CreateAlphaCutoffAttr(VtValue const& defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(
        UsdVrmTokens->inputsVrmMaterialAlphaCutoff, SdfValueTypeNames->Float,
        /* custom = */ false, SdfVariabilityVarying, defaultValue, writeSparsely);
}

UsdAttribute
UsdVrmMaterialAPI::GetDoubleSidedAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->inputsVrmMaterialDoubleSided);
}

UsdAttribute
UsdVrmMaterialAPI::CreateDoubleSidedAttr(VtValue const& defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(
        UsdVrmTokens->inputsVrmMaterialDoubleSided, SdfValueTypeNames->Bool,
        /* custom = */ false, SdfVariabilityUniform, defaultValue, writeSparsely);
}

UsdAttribute
UsdVrmMaterialAPI::GetUnlitAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->inputsVrmMaterialUnlit);
}

UsdAttribute
UsdVrmMaterialAPI::CreateUnlitAttr(VtValue const& defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdVrmTokens->inputsVrmMaterialUnlit, SdfValueTypeNames->Bool,
                                      /* custom = */ false, SdfVariabilityUniform, defaultValue,
                                      writeSparsely);
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
UsdVrmMaterialAPI::GetSchemaAttributeNames(bool includeInherited)
{
    static TfTokenVector localNames = {
        UsdVrmTokens->inputsVrmMaterialBaseColorFactor,
        UsdVrmTokens->inputsVrmMaterialBaseColorAlphaFactor,
        UsdVrmTokens->inputsVrmMaterialMetallicFactor,
        UsdVrmTokens->inputsVrmMaterialRoughnessFactor,
        UsdVrmTokens->inputsVrmMaterialEmissiveFactor,
        UsdVrmTokens->inputsVrmMaterialEmissiveStrength,
        UsdVrmTokens->inputsVrmMaterialAlphaMode,
        UsdVrmTokens->inputsVrmMaterialAlphaCutoff,
        UsdVrmTokens->inputsVrmMaterialDoubleSided,
        UsdVrmTokens->inputsVrmMaterialUnlit,
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
