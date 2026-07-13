//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "./vrmLookAtAPI.h"
#include "pxr/usd/usd/schemaRegistry.h"
#include "pxr/usd/usd/typed.h"

#include "pxr/usd/sdf/types.h"
#include "pxr/usd/sdf/assetPath.h"

PXR_NAMESPACE_OPEN_SCOPE

// Register the schema with the TfType system.
TF_REGISTRY_FUNCTION(TfType)
{
    TfType::Define<UsdVrmLookAtAPI,
        TfType::Bases< UsdAPISchemaBase > >();
    
}

/* virtual */
UsdVrmLookAtAPI::~UsdVrmLookAtAPI()
{
}

/* static */
UsdVrmLookAtAPI
UsdVrmLookAtAPI::Get(const UsdStagePtr &stage, const SdfPath &path)
{
    if (!stage) {
        TF_CODING_ERROR("Invalid stage");
        return UsdVrmLookAtAPI();
    }
    return UsdVrmLookAtAPI(stage->GetPrimAtPath(path));
}


/* virtual */
UsdSchemaKind UsdVrmLookAtAPI::_GetSchemaKind() const
{
    return UsdVrmLookAtAPI::schemaKind;
}

/* static */
bool
UsdVrmLookAtAPI::CanApply(
    const UsdPrim &prim, std::string *whyNot)
{
    return prim.CanApplyAPI<UsdVrmLookAtAPI>(whyNot);
}

/* static */
UsdVrmLookAtAPI
UsdVrmLookAtAPI::Apply(const UsdPrim &prim)
{
    if (prim.ApplyAPI<UsdVrmLookAtAPI>()) {
        return UsdVrmLookAtAPI(prim);
    }
    return UsdVrmLookAtAPI();
}

/* static */
const TfType &
UsdVrmLookAtAPI::_GetStaticTfType()
{
    static TfType tfType = TfType::Find<UsdVrmLookAtAPI>();
    return tfType;
}

/* static */
bool 
UsdVrmLookAtAPI::_IsTypedSchema()
{
    static bool isTyped = _GetStaticTfType().IsA<UsdTyped>();
    return isTyped;
}

/* virtual */
const TfType &
UsdVrmLookAtAPI::_GetTfType() const
{
    return _GetStaticTfType();
}

UsdAttribute
UsdVrmLookAtAPI::GetVrmTypeAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->vrmType);
}

UsdAttribute
UsdVrmLookAtAPI::CreateVrmTypeAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdVrmTokens->vrmType,
                       SdfValueTypeNames->Token,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdVrmLookAtAPI::GetVrmLeftEyeAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->vrmLeftEye);
}

UsdAttribute
UsdVrmLookAtAPI::CreateVrmLeftEyeAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdVrmTokens->vrmLeftEye,
                       SdfValueTypeNames->Token,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdVrmLookAtAPI::GetVrmRightEyeAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->vrmRightEye);
}

UsdAttribute
UsdVrmLookAtAPI::CreateVrmRightEyeAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdVrmTokens->vrmRightEye,
                       SdfValueTypeNames->Token,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdRelationship
UsdVrmLookAtAPI::GetVrmSkeletonRel() const
{
    return GetPrim().GetRelationship(UsdVrmTokens->vrmSkeleton);
}

UsdRelationship
UsdVrmLookAtAPI::CreateVrmSkeletonRel() const
{
    return GetPrim().CreateRelationship(UsdVrmTokens->vrmSkeleton,
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
UsdVrmLookAtAPI::GetSchemaAttributeNames(bool includeInherited)
{
    static TfTokenVector localNames = {
        UsdVrmTokens->vrmType,
        UsdVrmTokens->vrmLeftEye,
        UsdVrmTokens->vrmRightEye,
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
