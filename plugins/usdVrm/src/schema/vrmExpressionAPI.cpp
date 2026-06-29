//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "./vrmExpressionAPI.h"
#include "pxr/usd/usd/schemaRegistry.h"
#include "pxr/usd/usd/typed.h"

#include "pxr/usd/sdf/types.h"
#include "pxr/usd/sdf/assetPath.h"

PXR_NAMESPACE_OPEN_SCOPE

// Register the schema with the TfType system.
TF_REGISTRY_FUNCTION(TfType)
{
    TfType::Define<UsdVrmExpressionAPI,
        TfType::Bases< UsdAPISchemaBase > >();
    
}

/* virtual */
UsdVrmExpressionAPI::~UsdVrmExpressionAPI()
{
}

/* static */
UsdVrmExpressionAPI
UsdVrmExpressionAPI::Get(const UsdStagePtr &stage, const SdfPath &path)
{
    if (!stage) {
        TF_CODING_ERROR("Invalid stage");
        return UsdVrmExpressionAPI();
    }
    return UsdVrmExpressionAPI(stage->GetPrimAtPath(path));
}


/* virtual */
UsdSchemaKind UsdVrmExpressionAPI::_GetSchemaKind() const
{
    return UsdVrmExpressionAPI::schemaKind;
}

/* static */
bool
UsdVrmExpressionAPI::CanApply(
    const UsdPrim &prim, std::string *whyNot)
{
    return prim.CanApplyAPI<UsdVrmExpressionAPI>(whyNot);
}

/* static */
UsdVrmExpressionAPI
UsdVrmExpressionAPI::Apply(const UsdPrim &prim)
{
    if (prim.ApplyAPI<UsdVrmExpressionAPI>()) {
        return UsdVrmExpressionAPI(prim);
    }
    return UsdVrmExpressionAPI();
}

/* static */
const TfType &
UsdVrmExpressionAPI::_GetStaticTfType()
{
    static TfType tfType = TfType::Find<UsdVrmExpressionAPI>();
    return tfType;
}

/* static */
bool 
UsdVrmExpressionAPI::_IsTypedSchema()
{
    static bool isTyped = _GetStaticTfType().IsA<UsdTyped>();
    return isTyped;
}

/* virtual */
const TfType &
UsdVrmExpressionAPI::_GetTfType() const
{
    return _GetStaticTfType();
}

UsdAttribute
UsdVrmExpressionAPI::GetVrmExpressionTypeAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->vrmExpressionType);
}

UsdAttribute
UsdVrmExpressionAPI::CreateVrmExpressionTypeAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdVrmTokens->vrmExpressionType,
                       SdfValueTypeNames->Token,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdVrmExpressionAPI::GetVrmIsBinaryAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->vrmIsBinary);
}

UsdAttribute
UsdVrmExpressionAPI::CreateVrmIsBinaryAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdVrmTokens->vrmIsBinary,
                       SdfValueTypeNames->Bool,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdVrmExpressionAPI::GetVrmMorphTargetWeightsAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->vrmMorphTargetWeights);
}

UsdAttribute
UsdVrmExpressionAPI::CreateVrmMorphTargetWeightsAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdVrmTokens->vrmMorphTargetWeights,
                       SdfValueTypeNames->FloatArray,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdVrmExpressionAPI::GetVrmMaterialColorTypesAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->vrmMaterialColorTypes);
}

UsdAttribute
UsdVrmExpressionAPI::CreateVrmMaterialColorTypesAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdVrmTokens->vrmMaterialColorTypes,
                       SdfValueTypeNames->TokenArray,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdVrmExpressionAPI::GetVrmMaterialColorValuesAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->vrmMaterialColorValues);
}

UsdAttribute
UsdVrmExpressionAPI::CreateVrmMaterialColorValuesAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdVrmTokens->vrmMaterialColorValues,
                       SdfValueTypeNames->Float4Array,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdRelationship
UsdVrmExpressionAPI::GetVrmMorphTargetsRel() const
{
    return GetPrim().GetRelationship(UsdVrmTokens->vrmMorphTargets);
}

UsdRelationship
UsdVrmExpressionAPI::CreateVrmMorphTargetsRel() const
{
    return GetPrim().CreateRelationship(UsdVrmTokens->vrmMorphTargets,
                       /* custom = */ false);
}

UsdRelationship
UsdVrmExpressionAPI::GetVrmMaterialColorTargetsRel() const
{
    return GetPrim().GetRelationship(UsdVrmTokens->vrmMaterialColorTargets);
}

UsdRelationship
UsdVrmExpressionAPI::CreateVrmMaterialColorTargetsRel() const
{
    return GetPrim().CreateRelationship(UsdVrmTokens->vrmMaterialColorTargets,
                       /* custom = */ false);
}

namespace {
static inline TfTokenVector
_ConcatenateAttributeNames(const TfTokenVector& left,const TfTokenVector& right)
{
    TfTokenVector result;
    result.reserve(left.size() + right.size());
    result.insert(result.end(), left.begin(), left.end());
    result.insert(result.end(), right.begin(), right.end());
    return result;
}
}

/*static*/
const TfTokenVector&
UsdVrmExpressionAPI::GetSchemaAttributeNames(bool includeInherited)
{
    static TfTokenVector localNames = {
        UsdVrmTokens->vrmExpressionType,
        UsdVrmTokens->vrmIsBinary,
        UsdVrmTokens->vrmMorphTargetWeights,
        UsdVrmTokens->vrmMaterialColorTypes,
        UsdVrmTokens->vrmMaterialColorValues,
    };
    static TfTokenVector allNames =
        _ConcatenateAttributeNames(
            UsdAPISchemaBase::GetSchemaAttributeNames(true),
            localNames);

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
