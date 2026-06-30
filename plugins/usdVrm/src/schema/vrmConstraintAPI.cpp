//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "./vrmConstraintAPI.h"
#include "pxr/usd/usd/schemaRegistry.h"
#include "pxr/usd/usd/typed.h"

#include "pxr/usd/sdf/types.h"
#include "pxr/usd/sdf/assetPath.h"

PXR_NAMESPACE_OPEN_SCOPE

// Register the schema with the TfType system.
TF_REGISTRY_FUNCTION(TfType)
{
    TfType::Define<UsdVrmConstraintAPI,
        TfType::Bases< UsdAPISchemaBase > >();
    
}

/* virtual */
UsdVrmConstraintAPI::~UsdVrmConstraintAPI()
{
}

/* static */
UsdVrmConstraintAPI
UsdVrmConstraintAPI::Get(const UsdStagePtr &stage, const SdfPath &path)
{
    if (!stage) {
        TF_CODING_ERROR("Invalid stage");
        return UsdVrmConstraintAPI();
    }
    return UsdVrmConstraintAPI(stage->GetPrimAtPath(path));
}


/* virtual */
UsdSchemaKind UsdVrmConstraintAPI::_GetSchemaKind() const
{
    return UsdVrmConstraintAPI::schemaKind;
}

/* static */
bool
UsdVrmConstraintAPI::CanApply(
    const UsdPrim &prim, std::string *whyNot)
{
    return prim.CanApplyAPI<UsdVrmConstraintAPI>(whyNot);
}

/* static */
UsdVrmConstraintAPI
UsdVrmConstraintAPI::Apply(const UsdPrim &prim)
{
    if (prim.ApplyAPI<UsdVrmConstraintAPI>()) {
        return UsdVrmConstraintAPI(prim);
    }
    return UsdVrmConstraintAPI();
}

/* static */
const TfType &
UsdVrmConstraintAPI::_GetStaticTfType()
{
    static TfType tfType = TfType::Find<UsdVrmConstraintAPI>();
    return tfType;
}

/* static */
bool 
UsdVrmConstraintAPI::_IsTypedSchema()
{
    static bool isTyped = _GetStaticTfType().IsA<UsdTyped>();
    return isTyped;
}

/* virtual */
const TfType &
UsdVrmConstraintAPI::_GetTfType() const
{
    return _GetStaticTfType();
}

UsdAttribute
UsdVrmConstraintAPI::GetVrmTypeAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->vrmType);
}

UsdAttribute
UsdVrmConstraintAPI::CreateVrmTypeAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdVrmTokens->vrmType,
                       SdfValueTypeNames->Token,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdVrmConstraintAPI::GetVrmConstrainedAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->vrmConstrained);
}

UsdAttribute
UsdVrmConstraintAPI::CreateVrmConstrainedAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdVrmTokens->vrmConstrained,
                       SdfValueTypeNames->Token,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdVrmConstraintAPI::GetVrmSourceAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->vrmSource);
}

UsdAttribute
UsdVrmConstraintAPI::CreateVrmSourceAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdVrmTokens->vrmSource,
                       SdfValueTypeNames->Token,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdVrmConstraintAPI::GetVrmAxisAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->vrmAxis);
}

UsdAttribute
UsdVrmConstraintAPI::CreateVrmAxisAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdVrmTokens->vrmAxis,
                       SdfValueTypeNames->Token,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdVrmConstraintAPI::GetVrmWeightAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->vrmWeight);
}

UsdAttribute
UsdVrmConstraintAPI::CreateVrmWeightAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdVrmTokens->vrmWeight,
                       SdfValueTypeNames->Float,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
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
UsdVrmConstraintAPI::GetSchemaAttributeNames(bool includeInherited)
{
    static TfTokenVector localNames = {
        UsdVrmTokens->vrmType,
        UsdVrmTokens->vrmConstrained,
        UsdVrmTokens->vrmSource,
        UsdVrmTokens->vrmAxis,
        UsdVrmTokens->vrmWeight,
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
