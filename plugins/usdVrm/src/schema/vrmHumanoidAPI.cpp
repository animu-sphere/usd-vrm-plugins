//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "./vrmHumanoidAPI.h"
#include "pxr/usd/usd/schemaRegistry.h"
#include "pxr/usd/usd/typed.h"

#include "pxr/usd/sdf/types.h"
#include "pxr/usd/sdf/assetPath.h"

PXR_NAMESPACE_OPEN_SCOPE

// Register the schema with the TfType system.
TF_REGISTRY_FUNCTION(TfType)
{
    TfType::Define<UsdVrmHumanoidAPI,
        TfType::Bases< UsdAPISchemaBase > >();
    
}

/* virtual */
UsdVrmHumanoidAPI::~UsdVrmHumanoidAPI()
{
}

/* static */
UsdVrmHumanoidAPI
UsdVrmHumanoidAPI::Get(const UsdStagePtr &stage, const SdfPath &path)
{
    if (!stage) {
        TF_CODING_ERROR("Invalid stage");
        return UsdVrmHumanoidAPI();
    }
    return UsdVrmHumanoidAPI(stage->GetPrimAtPath(path));
}


/* virtual */
UsdSchemaKind UsdVrmHumanoidAPI::_GetSchemaKind() const
{
    return UsdVrmHumanoidAPI::schemaKind;
}

/* static */
bool
UsdVrmHumanoidAPI::CanApply(
    const UsdPrim &prim, std::string *whyNot)
{
    return prim.CanApplyAPI<UsdVrmHumanoidAPI>(whyNot);
}

/* static */
UsdVrmHumanoidAPI
UsdVrmHumanoidAPI::Apply(const UsdPrim &prim)
{
    if (prim.ApplyAPI<UsdVrmHumanoidAPI>()) {
        return UsdVrmHumanoidAPI(prim);
    }
    return UsdVrmHumanoidAPI();
}

/* static */
const TfType &
UsdVrmHumanoidAPI::_GetStaticTfType()
{
    static TfType tfType = TfType::Find<UsdVrmHumanoidAPI>();
    return tfType;
}

/* static */
bool 
UsdVrmHumanoidAPI::_IsTypedSchema()
{
    static bool isTyped = _GetStaticTfType().IsA<UsdTyped>();
    return isTyped;
}

/* virtual */
const TfType &
UsdVrmHumanoidAPI::_GetTfType() const
{
    return _GetStaticTfType();
}

UsdAttribute
UsdVrmHumanoidAPI::GetVrmHumanBonesHipsAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->vrmHumanBonesHips);
}

UsdAttribute
UsdVrmHumanoidAPI::CreateVrmHumanBonesHipsAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdVrmTokens->vrmHumanBonesHips,
                       SdfValueTypeNames->Token,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdVrmHumanoidAPI::GetVrmHumanBonesSpineAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->vrmHumanBonesSpine);
}

UsdAttribute
UsdVrmHumanoidAPI::CreateVrmHumanBonesSpineAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdVrmTokens->vrmHumanBonesSpine,
                       SdfValueTypeNames->Token,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdVrmHumanoidAPI::GetVrmHumanBonesChestAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->vrmHumanBonesChest);
}

UsdAttribute
UsdVrmHumanoidAPI::CreateVrmHumanBonesChestAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdVrmTokens->vrmHumanBonesChest,
                       SdfValueTypeNames->Token,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdVrmHumanoidAPI::GetVrmHumanBonesUpperChestAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->vrmHumanBonesUpperChest);
}

UsdAttribute
UsdVrmHumanoidAPI::CreateVrmHumanBonesUpperChestAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdVrmTokens->vrmHumanBonesUpperChest,
                       SdfValueTypeNames->Token,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdVrmHumanoidAPI::GetVrmHumanBonesNeckAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->vrmHumanBonesNeck);
}

UsdAttribute
UsdVrmHumanoidAPI::CreateVrmHumanBonesNeckAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdVrmTokens->vrmHumanBonesNeck,
                       SdfValueTypeNames->Token,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdVrmHumanoidAPI::GetVrmHumanBonesHeadAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->vrmHumanBonesHead);
}

UsdAttribute
UsdVrmHumanoidAPI::CreateVrmHumanBonesHeadAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdVrmTokens->vrmHumanBonesHead,
                       SdfValueTypeNames->Token,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdVrmHumanoidAPI::GetVrmHumanBonesLeftEyeAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->vrmHumanBonesLeftEye);
}

UsdAttribute
UsdVrmHumanoidAPI::CreateVrmHumanBonesLeftEyeAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdVrmTokens->vrmHumanBonesLeftEye,
                       SdfValueTypeNames->Token,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdVrmHumanoidAPI::GetVrmHumanBonesRightEyeAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->vrmHumanBonesRightEye);
}

UsdAttribute
UsdVrmHumanoidAPI::CreateVrmHumanBonesRightEyeAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdVrmTokens->vrmHumanBonesRightEye,
                       SdfValueTypeNames->Token,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdVrmHumanoidAPI::GetVrmHumanBonesJawAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->vrmHumanBonesJaw);
}

UsdAttribute
UsdVrmHumanoidAPI::CreateVrmHumanBonesJawAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdVrmTokens->vrmHumanBonesJaw,
                       SdfValueTypeNames->Token,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdVrmHumanoidAPI::GetVrmHumanBonesLeftUpperLegAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->vrmHumanBonesLeftUpperLeg);
}

UsdAttribute
UsdVrmHumanoidAPI::CreateVrmHumanBonesLeftUpperLegAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdVrmTokens->vrmHumanBonesLeftUpperLeg,
                       SdfValueTypeNames->Token,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdVrmHumanoidAPI::GetVrmHumanBonesLeftLowerLegAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->vrmHumanBonesLeftLowerLeg);
}

UsdAttribute
UsdVrmHumanoidAPI::CreateVrmHumanBonesLeftLowerLegAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdVrmTokens->vrmHumanBonesLeftLowerLeg,
                       SdfValueTypeNames->Token,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdVrmHumanoidAPI::GetVrmHumanBonesLeftFootAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->vrmHumanBonesLeftFoot);
}

UsdAttribute
UsdVrmHumanoidAPI::CreateVrmHumanBonesLeftFootAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdVrmTokens->vrmHumanBonesLeftFoot,
                       SdfValueTypeNames->Token,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdVrmHumanoidAPI::GetVrmHumanBonesLeftToesAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->vrmHumanBonesLeftToes);
}

UsdAttribute
UsdVrmHumanoidAPI::CreateVrmHumanBonesLeftToesAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdVrmTokens->vrmHumanBonesLeftToes,
                       SdfValueTypeNames->Token,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdVrmHumanoidAPI::GetVrmHumanBonesRightUpperLegAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->vrmHumanBonesRightUpperLeg);
}

UsdAttribute
UsdVrmHumanoidAPI::CreateVrmHumanBonesRightUpperLegAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdVrmTokens->vrmHumanBonesRightUpperLeg,
                       SdfValueTypeNames->Token,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdVrmHumanoidAPI::GetVrmHumanBonesRightLowerLegAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->vrmHumanBonesRightLowerLeg);
}

UsdAttribute
UsdVrmHumanoidAPI::CreateVrmHumanBonesRightLowerLegAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdVrmTokens->vrmHumanBonesRightLowerLeg,
                       SdfValueTypeNames->Token,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdVrmHumanoidAPI::GetVrmHumanBonesRightFootAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->vrmHumanBonesRightFoot);
}

UsdAttribute
UsdVrmHumanoidAPI::CreateVrmHumanBonesRightFootAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdVrmTokens->vrmHumanBonesRightFoot,
                       SdfValueTypeNames->Token,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdVrmHumanoidAPI::GetVrmHumanBonesRightToesAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->vrmHumanBonesRightToes);
}

UsdAttribute
UsdVrmHumanoidAPI::CreateVrmHumanBonesRightToesAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdVrmTokens->vrmHumanBonesRightToes,
                       SdfValueTypeNames->Token,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdVrmHumanoidAPI::GetVrmHumanBonesLeftShoulderAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->vrmHumanBonesLeftShoulder);
}

UsdAttribute
UsdVrmHumanoidAPI::CreateVrmHumanBonesLeftShoulderAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdVrmTokens->vrmHumanBonesLeftShoulder,
                       SdfValueTypeNames->Token,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdVrmHumanoidAPI::GetVrmHumanBonesLeftUpperArmAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->vrmHumanBonesLeftUpperArm);
}

UsdAttribute
UsdVrmHumanoidAPI::CreateVrmHumanBonesLeftUpperArmAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdVrmTokens->vrmHumanBonesLeftUpperArm,
                       SdfValueTypeNames->Token,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdVrmHumanoidAPI::GetVrmHumanBonesLeftLowerArmAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->vrmHumanBonesLeftLowerArm);
}

UsdAttribute
UsdVrmHumanoidAPI::CreateVrmHumanBonesLeftLowerArmAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdVrmTokens->vrmHumanBonesLeftLowerArm,
                       SdfValueTypeNames->Token,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdVrmHumanoidAPI::GetVrmHumanBonesLeftHandAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->vrmHumanBonesLeftHand);
}

UsdAttribute
UsdVrmHumanoidAPI::CreateVrmHumanBonesLeftHandAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdVrmTokens->vrmHumanBonesLeftHand,
                       SdfValueTypeNames->Token,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdVrmHumanoidAPI::GetVrmHumanBonesRightShoulderAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->vrmHumanBonesRightShoulder);
}

UsdAttribute
UsdVrmHumanoidAPI::CreateVrmHumanBonesRightShoulderAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdVrmTokens->vrmHumanBonesRightShoulder,
                       SdfValueTypeNames->Token,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdVrmHumanoidAPI::GetVrmHumanBonesRightUpperArmAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->vrmHumanBonesRightUpperArm);
}

UsdAttribute
UsdVrmHumanoidAPI::CreateVrmHumanBonesRightUpperArmAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdVrmTokens->vrmHumanBonesRightUpperArm,
                       SdfValueTypeNames->Token,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdVrmHumanoidAPI::GetVrmHumanBonesRightLowerArmAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->vrmHumanBonesRightLowerArm);
}

UsdAttribute
UsdVrmHumanoidAPI::CreateVrmHumanBonesRightLowerArmAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdVrmTokens->vrmHumanBonesRightLowerArm,
                       SdfValueTypeNames->Token,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdVrmHumanoidAPI::GetVrmHumanBonesRightHandAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->vrmHumanBonesRightHand);
}

UsdAttribute
UsdVrmHumanoidAPI::CreateVrmHumanBonesRightHandAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdVrmTokens->vrmHumanBonesRightHand,
                       SdfValueTypeNames->Token,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdVrmHumanoidAPI::GetVrmHumanBonesLeftThumbMetacarpalAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->vrmHumanBonesLeftThumbMetacarpal);
}

UsdAttribute
UsdVrmHumanoidAPI::CreateVrmHumanBonesLeftThumbMetacarpalAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdVrmTokens->vrmHumanBonesLeftThumbMetacarpal,
                       SdfValueTypeNames->Token,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdVrmHumanoidAPI::GetVrmHumanBonesLeftThumbProximalAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->vrmHumanBonesLeftThumbProximal);
}

UsdAttribute
UsdVrmHumanoidAPI::CreateVrmHumanBonesLeftThumbProximalAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdVrmTokens->vrmHumanBonesLeftThumbProximal,
                       SdfValueTypeNames->Token,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdVrmHumanoidAPI::GetVrmHumanBonesLeftThumbDistalAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->vrmHumanBonesLeftThumbDistal);
}

UsdAttribute
UsdVrmHumanoidAPI::CreateVrmHumanBonesLeftThumbDistalAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdVrmTokens->vrmHumanBonesLeftThumbDistal,
                       SdfValueTypeNames->Token,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdVrmHumanoidAPI::GetVrmHumanBonesLeftIndexProximalAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->vrmHumanBonesLeftIndexProximal);
}

UsdAttribute
UsdVrmHumanoidAPI::CreateVrmHumanBonesLeftIndexProximalAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdVrmTokens->vrmHumanBonesLeftIndexProximal,
                       SdfValueTypeNames->Token,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdVrmHumanoidAPI::GetVrmHumanBonesLeftIndexIntermediateAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->vrmHumanBonesLeftIndexIntermediate);
}

UsdAttribute
UsdVrmHumanoidAPI::CreateVrmHumanBonesLeftIndexIntermediateAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdVrmTokens->vrmHumanBonesLeftIndexIntermediate,
                       SdfValueTypeNames->Token,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdVrmHumanoidAPI::GetVrmHumanBonesLeftIndexDistalAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->vrmHumanBonesLeftIndexDistal);
}

UsdAttribute
UsdVrmHumanoidAPI::CreateVrmHumanBonesLeftIndexDistalAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdVrmTokens->vrmHumanBonesLeftIndexDistal,
                       SdfValueTypeNames->Token,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdVrmHumanoidAPI::GetVrmHumanBonesLeftMiddleProximalAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->vrmHumanBonesLeftMiddleProximal);
}

UsdAttribute
UsdVrmHumanoidAPI::CreateVrmHumanBonesLeftMiddleProximalAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdVrmTokens->vrmHumanBonesLeftMiddleProximal,
                       SdfValueTypeNames->Token,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdVrmHumanoidAPI::GetVrmHumanBonesLeftMiddleIntermediateAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->vrmHumanBonesLeftMiddleIntermediate);
}

UsdAttribute
UsdVrmHumanoidAPI::CreateVrmHumanBonesLeftMiddleIntermediateAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdVrmTokens->vrmHumanBonesLeftMiddleIntermediate,
                       SdfValueTypeNames->Token,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdVrmHumanoidAPI::GetVrmHumanBonesLeftMiddleDistalAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->vrmHumanBonesLeftMiddleDistal);
}

UsdAttribute
UsdVrmHumanoidAPI::CreateVrmHumanBonesLeftMiddleDistalAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdVrmTokens->vrmHumanBonesLeftMiddleDistal,
                       SdfValueTypeNames->Token,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdVrmHumanoidAPI::GetVrmHumanBonesLeftRingProximalAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->vrmHumanBonesLeftRingProximal);
}

UsdAttribute
UsdVrmHumanoidAPI::CreateVrmHumanBonesLeftRingProximalAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdVrmTokens->vrmHumanBonesLeftRingProximal,
                       SdfValueTypeNames->Token,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdVrmHumanoidAPI::GetVrmHumanBonesLeftRingIntermediateAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->vrmHumanBonesLeftRingIntermediate);
}

UsdAttribute
UsdVrmHumanoidAPI::CreateVrmHumanBonesLeftRingIntermediateAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdVrmTokens->vrmHumanBonesLeftRingIntermediate,
                       SdfValueTypeNames->Token,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdVrmHumanoidAPI::GetVrmHumanBonesLeftRingDistalAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->vrmHumanBonesLeftRingDistal);
}

UsdAttribute
UsdVrmHumanoidAPI::CreateVrmHumanBonesLeftRingDistalAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdVrmTokens->vrmHumanBonesLeftRingDistal,
                       SdfValueTypeNames->Token,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdVrmHumanoidAPI::GetVrmHumanBonesLeftLittleProximalAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->vrmHumanBonesLeftLittleProximal);
}

UsdAttribute
UsdVrmHumanoidAPI::CreateVrmHumanBonesLeftLittleProximalAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdVrmTokens->vrmHumanBonesLeftLittleProximal,
                       SdfValueTypeNames->Token,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdVrmHumanoidAPI::GetVrmHumanBonesLeftLittleIntermediateAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->vrmHumanBonesLeftLittleIntermediate);
}

UsdAttribute
UsdVrmHumanoidAPI::CreateVrmHumanBonesLeftLittleIntermediateAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdVrmTokens->vrmHumanBonesLeftLittleIntermediate,
                       SdfValueTypeNames->Token,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdVrmHumanoidAPI::GetVrmHumanBonesLeftLittleDistalAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->vrmHumanBonesLeftLittleDistal);
}

UsdAttribute
UsdVrmHumanoidAPI::CreateVrmHumanBonesLeftLittleDistalAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdVrmTokens->vrmHumanBonesLeftLittleDistal,
                       SdfValueTypeNames->Token,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdVrmHumanoidAPI::GetVrmHumanBonesRightThumbMetacarpalAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->vrmHumanBonesRightThumbMetacarpal);
}

UsdAttribute
UsdVrmHumanoidAPI::CreateVrmHumanBonesRightThumbMetacarpalAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdVrmTokens->vrmHumanBonesRightThumbMetacarpal,
                       SdfValueTypeNames->Token,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdVrmHumanoidAPI::GetVrmHumanBonesRightThumbProximalAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->vrmHumanBonesRightThumbProximal);
}

UsdAttribute
UsdVrmHumanoidAPI::CreateVrmHumanBonesRightThumbProximalAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdVrmTokens->vrmHumanBonesRightThumbProximal,
                       SdfValueTypeNames->Token,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdVrmHumanoidAPI::GetVrmHumanBonesRightThumbDistalAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->vrmHumanBonesRightThumbDistal);
}

UsdAttribute
UsdVrmHumanoidAPI::CreateVrmHumanBonesRightThumbDistalAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdVrmTokens->vrmHumanBonesRightThumbDistal,
                       SdfValueTypeNames->Token,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdVrmHumanoidAPI::GetVrmHumanBonesRightIndexProximalAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->vrmHumanBonesRightIndexProximal);
}

UsdAttribute
UsdVrmHumanoidAPI::CreateVrmHumanBonesRightIndexProximalAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdVrmTokens->vrmHumanBonesRightIndexProximal,
                       SdfValueTypeNames->Token,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdVrmHumanoidAPI::GetVrmHumanBonesRightIndexIntermediateAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->vrmHumanBonesRightIndexIntermediate);
}

UsdAttribute
UsdVrmHumanoidAPI::CreateVrmHumanBonesRightIndexIntermediateAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdVrmTokens->vrmHumanBonesRightIndexIntermediate,
                       SdfValueTypeNames->Token,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdVrmHumanoidAPI::GetVrmHumanBonesRightIndexDistalAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->vrmHumanBonesRightIndexDistal);
}

UsdAttribute
UsdVrmHumanoidAPI::CreateVrmHumanBonesRightIndexDistalAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdVrmTokens->vrmHumanBonesRightIndexDistal,
                       SdfValueTypeNames->Token,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdVrmHumanoidAPI::GetVrmHumanBonesRightMiddleProximalAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->vrmHumanBonesRightMiddleProximal);
}

UsdAttribute
UsdVrmHumanoidAPI::CreateVrmHumanBonesRightMiddleProximalAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdVrmTokens->vrmHumanBonesRightMiddleProximal,
                       SdfValueTypeNames->Token,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdVrmHumanoidAPI::GetVrmHumanBonesRightMiddleIntermediateAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->vrmHumanBonesRightMiddleIntermediate);
}

UsdAttribute
UsdVrmHumanoidAPI::CreateVrmHumanBonesRightMiddleIntermediateAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdVrmTokens->vrmHumanBonesRightMiddleIntermediate,
                       SdfValueTypeNames->Token,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdVrmHumanoidAPI::GetVrmHumanBonesRightMiddleDistalAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->vrmHumanBonesRightMiddleDistal);
}

UsdAttribute
UsdVrmHumanoidAPI::CreateVrmHumanBonesRightMiddleDistalAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdVrmTokens->vrmHumanBonesRightMiddleDistal,
                       SdfValueTypeNames->Token,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdVrmHumanoidAPI::GetVrmHumanBonesRightRingProximalAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->vrmHumanBonesRightRingProximal);
}

UsdAttribute
UsdVrmHumanoidAPI::CreateVrmHumanBonesRightRingProximalAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdVrmTokens->vrmHumanBonesRightRingProximal,
                       SdfValueTypeNames->Token,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdVrmHumanoidAPI::GetVrmHumanBonesRightRingIntermediateAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->vrmHumanBonesRightRingIntermediate);
}

UsdAttribute
UsdVrmHumanoidAPI::CreateVrmHumanBonesRightRingIntermediateAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdVrmTokens->vrmHumanBonesRightRingIntermediate,
                       SdfValueTypeNames->Token,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdVrmHumanoidAPI::GetVrmHumanBonesRightRingDistalAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->vrmHumanBonesRightRingDistal);
}

UsdAttribute
UsdVrmHumanoidAPI::CreateVrmHumanBonesRightRingDistalAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdVrmTokens->vrmHumanBonesRightRingDistal,
                       SdfValueTypeNames->Token,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdVrmHumanoidAPI::GetVrmHumanBonesRightLittleProximalAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->vrmHumanBonesRightLittleProximal);
}

UsdAttribute
UsdVrmHumanoidAPI::CreateVrmHumanBonesRightLittleProximalAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdVrmTokens->vrmHumanBonesRightLittleProximal,
                       SdfValueTypeNames->Token,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdVrmHumanoidAPI::GetVrmHumanBonesRightLittleIntermediateAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->vrmHumanBonesRightLittleIntermediate);
}

UsdAttribute
UsdVrmHumanoidAPI::CreateVrmHumanBonesRightLittleIntermediateAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdVrmTokens->vrmHumanBonesRightLittleIntermediate,
                       SdfValueTypeNames->Token,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdAttribute
UsdVrmHumanoidAPI::GetVrmHumanBonesRightLittleDistalAttr() const
{
    return GetPrim().GetAttribute(UsdVrmTokens->vrmHumanBonesRightLittleDistal);
}

UsdAttribute
UsdVrmHumanoidAPI::CreateVrmHumanBonesRightLittleDistalAttr(VtValue const &defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(UsdVrmTokens->vrmHumanBonesRightLittleDistal,
                       SdfValueTypeNames->Token,
                       /* custom = */ false,
                       SdfVariabilityUniform,
                       defaultValue,
                       writeSparsely);
}

UsdRelationship
UsdVrmHumanoidAPI::GetVrmSkeletonRel() const
{
    return GetPrim().GetRelationship(UsdVrmTokens->vrmSkeleton);
}

UsdRelationship
UsdVrmHumanoidAPI::CreateVrmSkeletonRel() const
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
UsdVrmHumanoidAPI::GetSchemaAttributeNames(bool includeInherited)
{
    static TfTokenVector localNames = {
        UsdVrmTokens->vrmHumanBonesHips,
        UsdVrmTokens->vrmHumanBonesSpine,
        UsdVrmTokens->vrmHumanBonesChest,
        UsdVrmTokens->vrmHumanBonesUpperChest,
        UsdVrmTokens->vrmHumanBonesNeck,
        UsdVrmTokens->vrmHumanBonesHead,
        UsdVrmTokens->vrmHumanBonesLeftEye,
        UsdVrmTokens->vrmHumanBonesRightEye,
        UsdVrmTokens->vrmHumanBonesJaw,
        UsdVrmTokens->vrmHumanBonesLeftUpperLeg,
        UsdVrmTokens->vrmHumanBonesLeftLowerLeg,
        UsdVrmTokens->vrmHumanBonesLeftFoot,
        UsdVrmTokens->vrmHumanBonesLeftToes,
        UsdVrmTokens->vrmHumanBonesRightUpperLeg,
        UsdVrmTokens->vrmHumanBonesRightLowerLeg,
        UsdVrmTokens->vrmHumanBonesRightFoot,
        UsdVrmTokens->vrmHumanBonesRightToes,
        UsdVrmTokens->vrmHumanBonesLeftShoulder,
        UsdVrmTokens->vrmHumanBonesLeftUpperArm,
        UsdVrmTokens->vrmHumanBonesLeftLowerArm,
        UsdVrmTokens->vrmHumanBonesLeftHand,
        UsdVrmTokens->vrmHumanBonesRightShoulder,
        UsdVrmTokens->vrmHumanBonesRightUpperArm,
        UsdVrmTokens->vrmHumanBonesRightLowerArm,
        UsdVrmTokens->vrmHumanBonesRightHand,
        UsdVrmTokens->vrmHumanBonesLeftThumbMetacarpal,
        UsdVrmTokens->vrmHumanBonesLeftThumbProximal,
        UsdVrmTokens->vrmHumanBonesLeftThumbDistal,
        UsdVrmTokens->vrmHumanBonesLeftIndexProximal,
        UsdVrmTokens->vrmHumanBonesLeftIndexIntermediate,
        UsdVrmTokens->vrmHumanBonesLeftIndexDistal,
        UsdVrmTokens->vrmHumanBonesLeftMiddleProximal,
        UsdVrmTokens->vrmHumanBonesLeftMiddleIntermediate,
        UsdVrmTokens->vrmHumanBonesLeftMiddleDistal,
        UsdVrmTokens->vrmHumanBonesLeftRingProximal,
        UsdVrmTokens->vrmHumanBonesLeftRingIntermediate,
        UsdVrmTokens->vrmHumanBonesLeftRingDistal,
        UsdVrmTokens->vrmHumanBonesLeftLittleProximal,
        UsdVrmTokens->vrmHumanBonesLeftLittleIntermediate,
        UsdVrmTokens->vrmHumanBonesLeftLittleDistal,
        UsdVrmTokens->vrmHumanBonesRightThumbMetacarpal,
        UsdVrmTokens->vrmHumanBonesRightThumbProximal,
        UsdVrmTokens->vrmHumanBonesRightThumbDistal,
        UsdVrmTokens->vrmHumanBonesRightIndexProximal,
        UsdVrmTokens->vrmHumanBonesRightIndexIntermediate,
        UsdVrmTokens->vrmHumanBonesRightIndexDistal,
        UsdVrmTokens->vrmHumanBonesRightMiddleProximal,
        UsdVrmTokens->vrmHumanBonesRightMiddleIntermediate,
        UsdVrmTokens->vrmHumanBonesRightMiddleDistal,
        UsdVrmTokens->vrmHumanBonesRightRingProximal,
        UsdVrmTokens->vrmHumanBonesRightRingIntermediate,
        UsdVrmTokens->vrmHumanBonesRightRingDistal,
        UsdVrmTokens->vrmHumanBonesRightLittleProximal,
        UsdVrmTokens->vrmHumanBonesRightLittleIntermediate,
        UsdVrmTokens->vrmHumanBonesRightLittleDistal,
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
