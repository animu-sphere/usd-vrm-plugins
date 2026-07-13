//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "./vrmColliderAPI.h"
#include "pxr/usd/usd/schemaRegistry.h"
#include "pxr/usd/usd/typed.h"

#include "pxr/usd/sdf/types.h"
#include "pxr/usd/sdf/assetPath.h"

PXR_NAMESPACE_OPEN_SCOPE

// Register the schema with the TfType system.
TF_REGISTRY_FUNCTION(TfType)
{
    TfType::Define<UsdVrmColliderAPI,
        TfType::Bases< UsdAPISchemaBase > >();
    
}

/* virtual */
UsdVrmColliderAPI::~UsdVrmColliderAPI()
{
}

/* static */
UsdVrmColliderAPI
UsdVrmColliderAPI::Get(const UsdStagePtr &stage, const SdfPath &path)
{
    if (!stage) {
        TF_CODING_ERROR("Invalid stage");
        return UsdVrmColliderAPI();
    }
    return UsdVrmColliderAPI(stage->GetPrimAtPath(path));
}


/* virtual */
UsdSchemaKind UsdVrmColliderAPI::_GetSchemaKind() const
{
    return UsdVrmColliderAPI::schemaKind;
}

/* static */
bool
UsdVrmColliderAPI::CanApply(
    const UsdPrim &prim, std::string *whyNot)
{
    return prim.CanApplyAPI<UsdVrmColliderAPI>(whyNot);
}

/* static */
UsdVrmColliderAPI
UsdVrmColliderAPI::Apply(const UsdPrim &prim)
{
    if (prim.ApplyAPI<UsdVrmColliderAPI>()) {
        return UsdVrmColliderAPI(prim);
    }
    return UsdVrmColliderAPI();
}

/* static */
const TfType &
UsdVrmColliderAPI::_GetStaticTfType()
{
    static TfType tfType = TfType::Find<UsdVrmColliderAPI>();
    return tfType;
}

/* static */
bool 
UsdVrmColliderAPI::_IsTypedSchema()
{
    static bool isTyped = _GetStaticTfType().IsA<UsdTyped>();
    return isTyped;
}

/* virtual */
const TfType &
UsdVrmColliderAPI::_GetTfType() const
{
    return _GetStaticTfType();
}

UsdAttribute
UsdVrmColliderAPI::GetVrmShapeAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->vrmShape);
}

UsdAttribute
UsdVrmColliderAPI::CreateVrmShapeAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdVrmTokens->vrmShape,
                       SdfValueTypeNames->Token,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdVrmColliderAPI::GetVrmNodeAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->vrmNode);
}

UsdAttribute
UsdVrmColliderAPI::CreateVrmNodeAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdVrmTokens->vrmNode,
                       SdfValueTypeNames->Token,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdVrmColliderAPI::GetVrmOffsetAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->vrmOffset);
}

UsdAttribute
UsdVrmColliderAPI::CreateVrmOffsetAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdVrmTokens->vrmOffset,
                       SdfValueTypeNames->Float3,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdVrmColliderAPI::GetVrmRadiusAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->vrmRadius);
}

UsdAttribute
UsdVrmColliderAPI::CreateVrmRadiusAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdVrmTokens->vrmRadius,
                       SdfValueTypeNames->Float,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdVrmColliderAPI::GetVrmTailAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->vrmTail);
}

UsdAttribute
UsdVrmColliderAPI::CreateVrmTailAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdVrmTokens->vrmTail,
                       SdfValueTypeNames->Float3,
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
UsdVrmColliderAPI::GetSchemaAttributeNames(bool includeInherited)
{
    static TfTokenVector localNames = {
        UsdVrmTokens->vrmShape,
        UsdVrmTokens->vrmNode,
        UsdVrmTokens->vrmOffset,
        UsdVrmTokens->vrmRadius,
        UsdVrmTokens->vrmTail,
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
