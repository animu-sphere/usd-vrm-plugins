//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "./vrmSpringBoneAPI.h"
#include "pxr/usd/usd/schemaRegistry.h"
#include "pxr/usd/usd/typed.h"

#include "pxr/usd/sdf/types.h"
#include "pxr/usd/sdf/assetPath.h"

PXR_NAMESPACE_OPEN_SCOPE

// Register the schema with the TfType system.
TF_REGISTRY_FUNCTION(TfType)
{
    TfType::Define<UsdVrmSpringBoneAPI,
        TfType::Bases< UsdAPISchemaBase > >();
    
}

/* virtual */
UsdVrmSpringBoneAPI::~UsdVrmSpringBoneAPI()
{
}

/* static */
UsdVrmSpringBoneAPI
UsdVrmSpringBoneAPI::Get(const UsdStagePtr &stage, const SdfPath &path)
{
    if (!stage) {
        TF_CODING_ERROR("Invalid stage");
        return UsdVrmSpringBoneAPI();
    }
    return UsdVrmSpringBoneAPI(stage->GetPrimAtPath(path));
}


/* virtual */
UsdSchemaKind UsdVrmSpringBoneAPI::_GetSchemaKind() const
{
    return UsdVrmSpringBoneAPI::schemaKind;
}

/* static */
bool
UsdVrmSpringBoneAPI::CanApply(
    const UsdPrim &prim, std::string *whyNot)
{
    return prim.CanApplyAPI<UsdVrmSpringBoneAPI>(whyNot);
}

/* static */
UsdVrmSpringBoneAPI
UsdVrmSpringBoneAPI::Apply(const UsdPrim &prim)
{
    if (prim.ApplyAPI<UsdVrmSpringBoneAPI>()) {
        return UsdVrmSpringBoneAPI(prim);
    }
    return UsdVrmSpringBoneAPI();
}

/* static */
const TfType &
UsdVrmSpringBoneAPI::_GetStaticTfType()
{
    static TfType tfType = TfType::Find<UsdVrmSpringBoneAPI>();
    return tfType;
}

/* static */
bool 
UsdVrmSpringBoneAPI::_IsTypedSchema()
{
    static bool isTyped = _GetStaticTfType().IsA<UsdTyped>();
    return isTyped;
}

/* virtual */
const TfType &
UsdVrmSpringBoneAPI::_GetTfType() const
{
    return _GetStaticTfType();
}

UsdAttribute
UsdVrmSpringBoneAPI::GetVrmJointsAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->vrmJoints);
}

UsdAttribute
UsdVrmSpringBoneAPI::CreateVrmJointsAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdVrmTokens->vrmJoints,
                       SdfValueTypeNames->TokenArray,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdVrmSpringBoneAPI::GetVrmStiffnessAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->vrmStiffness);
}

UsdAttribute
UsdVrmSpringBoneAPI::CreateVrmStiffnessAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdVrmTokens->vrmStiffness,
                       SdfValueTypeNames->FloatArray,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdVrmSpringBoneAPI::GetVrmGravityPowerAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->vrmGravityPower);
}

UsdAttribute
UsdVrmSpringBoneAPI::CreateVrmGravityPowerAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdVrmTokens->vrmGravityPower,
                       SdfValueTypeNames->FloatArray,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdVrmSpringBoneAPI::GetVrmDragForceAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->vrmDragForce);
}

UsdAttribute
UsdVrmSpringBoneAPI::CreateVrmDragForceAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdVrmTokens->vrmDragForce,
                       SdfValueTypeNames->FloatArray,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdVrmSpringBoneAPI::GetVrmHitRadiusAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->vrmHitRadius);
}

UsdAttribute
UsdVrmSpringBoneAPI::CreateVrmHitRadiusAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdVrmTokens->vrmHitRadius,
                       SdfValueTypeNames->FloatArray,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdVrmSpringBoneAPI::GetVrmGravityDirAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->vrmGravityDir);
}

UsdAttribute
UsdVrmSpringBoneAPI::CreateVrmGravityDirAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdVrmTokens->vrmGravityDir,
                       SdfValueTypeNames->Float3Array,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdVrmSpringBoneAPI::GetVrmCenterAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->vrmCenter);
}

UsdAttribute
UsdVrmSpringBoneAPI::CreateVrmCenterAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdVrmTokens->vrmCenter,
                       SdfValueTypeNames->Token,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdRelationship
UsdVrmSpringBoneAPI::GetVrmColliderGroupsRel() const
{
    return GetPrim().GetRelationship(UsdVrmTokens->vrmColliderGroups);
}

UsdRelationship
UsdVrmSpringBoneAPI::CreateVrmColliderGroupsRel() const
{
    return GetPrim().CreateRelationship(UsdVrmTokens->vrmColliderGroups,
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
UsdVrmSpringBoneAPI::GetSchemaAttributeNames(bool includeInherited)
{
    static TfTokenVector localNames = {
        UsdVrmTokens->vrmJoints,
        UsdVrmTokens->vrmStiffness,
        UsdVrmTokens->vrmGravityPower,
        UsdVrmTokens->vrmDragForce,
        UsdVrmTokens->vrmHitRadius,
        UsdVrmTokens->vrmGravityDir,
        UsdVrmTokens->vrmCenter,
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
