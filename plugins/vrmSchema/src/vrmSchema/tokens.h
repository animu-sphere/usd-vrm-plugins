//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef USDVRM_TOKENS_H
#define USDVRM_TOKENS_H

/// \file usdVrm/tokens.h

// XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
//
// This is an automatically generated file (by usdGenSchema.py).
// Do not hand-edit!
//
// XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX

#include "pxr/pxr.h"
#include "./api.h"
#include "pxr/base/tf/staticData.h"
#include "pxr/base/tf/token.h"
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

/// \class UsdVrmTokensType
///
/// \link UsdVrmTokens \endlink provides static, efficient
/// \link TfToken TfTokens\endlink for use in all public USD API.
///
/// These tokens are auto-generated from the module's schema, representing
/// property names, for when you need to fetch an attribute or relationship
/// directly by name, e.g. UsdPrim::GetAttribute(), in the most efficient
/// manner, and allow the compiler to verify that you spelled the name
/// correctly.
///
/// UsdVrmTokens also contains all of the \em allowedTokens values
/// declared for schema builtin attributes of 'token' scene description type.
/// Use UsdVrmTokens like so:
///
/// \code
///     gprim.GetMyTokenValuedAttr().Set(UsdVrmTokens->_1_0);
/// \endcode
struct UsdVrmTokensType
{
    USDVRM_API UsdVrmTokensType();
    /// \brief "1.0"
    ///
    /// Fallback value for UsdVrmMToonAPI::GetSpecVersionAttr()
    const TfToken _1_0;
    /// \brief "inputs:vrm:material:alphaCutoff"
    ///
    /// UsdVrmMaterialAPI
    const TfToken inputsVrmMaterialAlphaCutoff;
    /// \brief "inputs:vrm:material:alphaMode"
    ///
    /// UsdVrmMaterialAPI
    const TfToken inputsVrmMaterialAlphaMode;
    /// \brief "inputs:vrm:material:baseColorAlphaFactor"
    ///
    /// UsdVrmMaterialAPI
    const TfToken inputsVrmMaterialBaseColorAlphaFactor;
    /// \brief "inputs:vrm:material:baseColorFactor"
    ///
    /// UsdVrmMaterialAPI
    const TfToken inputsVrmMaterialBaseColorFactor;
    /// \brief "inputs:vrm:material:doubleSided"
    ///
    /// UsdVrmMaterialAPI
    const TfToken inputsVrmMaterialDoubleSided;
    /// \brief "inputs:vrm:material:emissiveFactor"
    ///
    /// UsdVrmMaterialAPI
    const TfToken inputsVrmMaterialEmissiveFactor;
    /// \brief "inputs:vrm:material:emissiveStrength"
    ///
    /// UsdVrmMaterialAPI
    const TfToken inputsVrmMaterialEmissiveStrength;
    /// \brief "inputs:vrm:material:metallicFactor"
    ///
    /// UsdVrmMaterialAPI
    const TfToken inputsVrmMaterialMetallicFactor;
    /// \brief "inputs:vrm:material:roughnessFactor"
    ///
    /// UsdVrmMaterialAPI
    const TfToken inputsVrmMaterialRoughnessFactor;
    /// \brief "inputs:vrm:material:unlit"
    ///
    /// UsdVrmMaterialAPI
    const TfToken inputsVrmMaterialUnlit;
    /// \brief "inputs:vrm:mtoon:giEqualizationFactor"
    ///
    /// UsdVrmMToonAPI
    const TfToken inputsVrmMtoonGiEqualizationFactor;
    /// \brief "inputs:vrm:mtoon:matcapFactor"
    ///
    /// UsdVrmMToonAPI
    const TfToken inputsVrmMtoonMatcapFactor;
    /// \brief "inputs:vrm:mtoon:outlineColorFactor"
    ///
    /// UsdVrmMToonAPI
    const TfToken inputsVrmMtoonOutlineColorFactor;
    /// \brief "inputs:vrm:mtoon:outlineLightingMixFactor"
    ///
    /// UsdVrmMToonAPI
    const TfToken inputsVrmMtoonOutlineLightingMixFactor;
    /// \brief "inputs:vrm:mtoon:outlineWidthFactor"
    ///
    /// UsdVrmMToonAPI
    const TfToken inputsVrmMtoonOutlineWidthFactor;
    /// \brief "inputs:vrm:mtoon:outlineWidthMode"
    ///
    /// UsdVrmMToonAPI
    const TfToken inputsVrmMtoonOutlineWidthMode;
    /// \brief "inputs:vrm:mtoon:parametricRimColorFactor"
    ///
    /// UsdVrmMToonAPI
    const TfToken inputsVrmMtoonParametricRimColorFactor;
    /// \brief "inputs:vrm:mtoon:parametricRimFresnelPowerFactor"
    ///
    /// UsdVrmMToonAPI
    const TfToken inputsVrmMtoonParametricRimFresnelPowerFactor;
    /// \brief "inputs:vrm:mtoon:parametricRimLiftFactor"
    ///
    /// UsdVrmMToonAPI
    const TfToken inputsVrmMtoonParametricRimLiftFactor;
    /// \brief "inputs:vrm:mtoon:renderQueueOffsetNumber"
    ///
    /// UsdVrmMToonAPI
    const TfToken inputsVrmMtoonRenderQueueOffsetNumber;
    /// \brief "inputs:vrm:mtoon:rimLightingMixFactor"
    ///
    /// UsdVrmMToonAPI
    const TfToken inputsVrmMtoonRimLightingMixFactor;
    /// \brief "inputs:vrm:mtoon:shadeColorFactor"
    ///
    /// UsdVrmMToonAPI
    const TfToken inputsVrmMtoonShadeColorFactor;
    /// \brief "inputs:vrm:mtoon:shadingShiftFactor"
    ///
    /// UsdVrmMToonAPI
    const TfToken inputsVrmMtoonShadingShiftFactor;
    /// \brief "inputs:vrm:mtoon:shadingToonyFactor"
    ///
    /// UsdVrmMToonAPI
    const TfToken inputsVrmMtoonShadingToonyFactor;
    /// \brief "inputs:vrm:mtoon:specVersion"
    ///
    /// UsdVrmMToonAPI
    const TfToken inputsVrmMtoonSpecVersion;
    /// \brief "inputs:vrm:mtoon:transparentWithZWrite"
    ///
    /// UsdVrmMToonAPI
    const TfToken inputsVrmMtoonTransparentWithZWrite;
    /// \brief "inputs:vrm:mtoon:uvAnimationRotationSpeedFactor"
    ///
    /// UsdVrmMToonAPI
    const TfToken inputsVrmMtoonUvAnimationRotationSpeedFactor;
    /// \brief "inputs:vrm:mtoon:uvAnimationScrollXSpeedFactor"
    ///
    /// UsdVrmMToonAPI
    const TfToken inputsVrmMtoonUvAnimationScrollXSpeedFactor;
    /// \brief "inputs:vrm:mtoon:uvAnimationScrollYSpeedFactor"
    ///
    /// UsdVrmMToonAPI
    const TfToken inputsVrmMtoonUvAnimationScrollYSpeedFactor;
    /// \brief "inputs:vrm:textureInfo"
    ///
    /// Property namespace prefix for the UsdVrmTextureInfoAPI schema.
    const TfToken inputsVrmTextureInfo;
    /// \brief "inputs:vrm:textureInfo:__INSTANCE_NAME__:file"
    ///
    /// UsdVrmTextureInfoAPI
    const TfToken inputsVrmTextureInfo_MultipleApplyTemplate_File;
    /// \brief "inputs:vrm:textureInfo:__INSTANCE_NAME__:scale"
    ///
    /// UsdVrmTextureInfoAPI
    const TfToken inputsVrmTextureInfo_MultipleApplyTemplate_Scale;
    /// \brief "inputs:vrm:textureInfo:__INSTANCE_NAME__:strength"
    ///
    /// UsdVrmTextureInfoAPI
    const TfToken inputsVrmTextureInfo_MultipleApplyTemplate_Strength;
    /// \brief "inputs:vrm:textureInfo:__INSTANCE_NAME__:texCoord"
    ///
    /// UsdVrmTextureInfoAPI
    const TfToken inputsVrmTextureInfo_MultipleApplyTemplate_TexCoord;
    /// \brief "inputs:vrm:textureInfo:__INSTANCE_NAME__:transform:offset"
    ///
    /// UsdVrmTextureInfoAPI
    const TfToken inputsVrmTextureInfo_MultipleApplyTemplate_TransformOffset;
    /// \brief "inputs:vrm:textureInfo:__INSTANCE_NAME__:transform:rotation"
    ///
    /// UsdVrmTextureInfoAPI
    const TfToken inputsVrmTextureInfo_MultipleApplyTemplate_TransformRotation;
    /// \brief "inputs:vrm:textureInfo:__INSTANCE_NAME__:transform:scale"
    ///
    /// UsdVrmTextureInfoAPI
    const TfToken inputsVrmTextureInfo_MultipleApplyTemplate_TransformScale;
    /// \brief "inputs:vrm:textureInfo:__INSTANCE_NAME__:wrapS"
    ///
    /// UsdVrmTextureInfoAPI
    const TfToken inputsVrmTextureInfo_MultipleApplyTemplate_WrapS;
    /// \brief "inputs:vrm:textureInfo:__INSTANCE_NAME__:wrapT"
    ///
    /// UsdVrmTextureInfoAPI
    const TfToken inputsVrmTextureInfo_MultipleApplyTemplate_WrapT;
    /// \brief "none"
    ///
    /// Fallback value for UsdVrmMToonAPI::GetOutlineWidthModeAttr()
    const TfToken none;
    /// \brief "repeat"
    ///
    /// Fallback value for UsdVrmTextureInfoAPI::GetWrapSAttr(), Fallback value for UsdVrmTextureInfoAPI::GetWrapTAttr()
    const TfToken repeat;
    /// \brief "vrm:axis"
    ///
    /// UsdVrmConstraintAPI
    const TfToken vrmAxis;
    /// \brief "vrm:center"
    ///
    /// UsdVrmSpringBoneAPI
    const TfToken vrmCenter;
    /// \brief "vrm:colliderGroups"
    ///
    /// UsdVrmSpringBoneAPI
    const TfToken vrmColliderGroups;
    /// \brief "vrm:constrained"
    ///
    /// UsdVrmConstraintAPI
    const TfToken vrmConstrained;
    /// \brief "vrm:dragForce"
    ///
    /// UsdVrmSpringBoneAPI
    const TfToken vrmDragForce;
    /// \brief "vrm:expressionName"
    ///
    /// UsdVrmExpressionAPI
    const TfToken vrmExpressionName;
    /// \brief "vrm:expressionType"
    ///
    /// UsdVrmExpressionAPI
    const TfToken vrmExpressionType;
    /// \brief "vrm:gravityDir"
    ///
    /// UsdVrmSpringBoneAPI
    const TfToken vrmGravityDir;
    /// \brief "vrm:gravityPower"
    ///
    /// UsdVrmSpringBoneAPI
    const TfToken vrmGravityPower;
    /// \brief "vrm:hitRadius"
    ///
    /// UsdVrmSpringBoneAPI
    const TfToken vrmHitRadius;
    /// \brief "vrm:humanBones:chest"
    ///
    /// UsdVrmHumanoidAPI
    const TfToken vrmHumanBonesChest;
    /// \brief "vrm:humanBones:head"
    ///
    /// UsdVrmHumanoidAPI
    const TfToken vrmHumanBonesHead;
    /// \brief "vrm:humanBones:hips"
    ///
    /// UsdVrmHumanoidAPI
    const TfToken vrmHumanBonesHips;
    /// \brief "vrm:humanBones:jaw"
    ///
    /// UsdVrmHumanoidAPI
    const TfToken vrmHumanBonesJaw;
    /// \brief "vrm:humanBones:leftEye"
    ///
    /// UsdVrmHumanoidAPI
    const TfToken vrmHumanBonesLeftEye;
    /// \brief "vrm:humanBones:leftFoot"
    ///
    /// UsdVrmHumanoidAPI
    const TfToken vrmHumanBonesLeftFoot;
    /// \brief "vrm:humanBones:leftHand"
    ///
    /// UsdVrmHumanoidAPI
    const TfToken vrmHumanBonesLeftHand;
    /// \brief "vrm:humanBones:leftIndexDistal"
    ///
    /// UsdVrmHumanoidAPI
    const TfToken vrmHumanBonesLeftIndexDistal;
    /// \brief "vrm:humanBones:leftIndexIntermediate"
    ///
    /// UsdVrmHumanoidAPI
    const TfToken vrmHumanBonesLeftIndexIntermediate;
    /// \brief "vrm:humanBones:leftIndexProximal"
    ///
    /// UsdVrmHumanoidAPI
    const TfToken vrmHumanBonesLeftIndexProximal;
    /// \brief "vrm:humanBones:leftLittleDistal"
    ///
    /// UsdVrmHumanoidAPI
    const TfToken vrmHumanBonesLeftLittleDistal;
    /// \brief "vrm:humanBones:leftLittleIntermediate"
    ///
    /// UsdVrmHumanoidAPI
    const TfToken vrmHumanBonesLeftLittleIntermediate;
    /// \brief "vrm:humanBones:leftLittleProximal"
    ///
    /// UsdVrmHumanoidAPI
    const TfToken vrmHumanBonesLeftLittleProximal;
    /// \brief "vrm:humanBones:leftLowerArm"
    ///
    /// UsdVrmHumanoidAPI
    const TfToken vrmHumanBonesLeftLowerArm;
    /// \brief "vrm:humanBones:leftLowerLeg"
    ///
    /// UsdVrmHumanoidAPI
    const TfToken vrmHumanBonesLeftLowerLeg;
    /// \brief "vrm:humanBones:leftMiddleDistal"
    ///
    /// UsdVrmHumanoidAPI
    const TfToken vrmHumanBonesLeftMiddleDistal;
    /// \brief "vrm:humanBones:leftMiddleIntermediate"
    ///
    /// UsdVrmHumanoidAPI
    const TfToken vrmHumanBonesLeftMiddleIntermediate;
    /// \brief "vrm:humanBones:leftMiddleProximal"
    ///
    /// UsdVrmHumanoidAPI
    const TfToken vrmHumanBonesLeftMiddleProximal;
    /// \brief "vrm:humanBones:leftRingDistal"
    ///
    /// UsdVrmHumanoidAPI
    const TfToken vrmHumanBonesLeftRingDistal;
    /// \brief "vrm:humanBones:leftRingIntermediate"
    ///
    /// UsdVrmHumanoidAPI
    const TfToken vrmHumanBonesLeftRingIntermediate;
    /// \brief "vrm:humanBones:leftRingProximal"
    ///
    /// UsdVrmHumanoidAPI
    const TfToken vrmHumanBonesLeftRingProximal;
    /// \brief "vrm:humanBones:leftShoulder"
    ///
    /// UsdVrmHumanoidAPI
    const TfToken vrmHumanBonesLeftShoulder;
    /// \brief "vrm:humanBones:leftThumbDistal"
    ///
    /// UsdVrmHumanoidAPI
    const TfToken vrmHumanBonesLeftThumbDistal;
    /// \brief "vrm:humanBones:leftThumbMetacarpal"
    ///
    /// UsdVrmHumanoidAPI
    const TfToken vrmHumanBonesLeftThumbMetacarpal;
    /// \brief "vrm:humanBones:leftThumbProximal"
    ///
    /// UsdVrmHumanoidAPI
    const TfToken vrmHumanBonesLeftThumbProximal;
    /// \brief "vrm:humanBones:leftToes"
    ///
    /// UsdVrmHumanoidAPI
    const TfToken vrmHumanBonesLeftToes;
    /// \brief "vrm:humanBones:leftUpperArm"
    ///
    /// UsdVrmHumanoidAPI
    const TfToken vrmHumanBonesLeftUpperArm;
    /// \brief "vrm:humanBones:leftUpperLeg"
    ///
    /// UsdVrmHumanoidAPI
    const TfToken vrmHumanBonesLeftUpperLeg;
    /// \brief "vrm:humanBones:neck"
    ///
    /// UsdVrmHumanoidAPI
    const TfToken vrmHumanBonesNeck;
    /// \brief "vrm:humanBones:rightEye"
    ///
    /// UsdVrmHumanoidAPI
    const TfToken vrmHumanBonesRightEye;
    /// \brief "vrm:humanBones:rightFoot"
    ///
    /// UsdVrmHumanoidAPI
    const TfToken vrmHumanBonesRightFoot;
    /// \brief "vrm:humanBones:rightHand"
    ///
    /// UsdVrmHumanoidAPI
    const TfToken vrmHumanBonesRightHand;
    /// \brief "vrm:humanBones:rightIndexDistal"
    ///
    /// UsdVrmHumanoidAPI
    const TfToken vrmHumanBonesRightIndexDistal;
    /// \brief "vrm:humanBones:rightIndexIntermediate"
    ///
    /// UsdVrmHumanoidAPI
    const TfToken vrmHumanBonesRightIndexIntermediate;
    /// \brief "vrm:humanBones:rightIndexProximal"
    ///
    /// UsdVrmHumanoidAPI
    const TfToken vrmHumanBonesRightIndexProximal;
    /// \brief "vrm:humanBones:rightLittleDistal"
    ///
    /// UsdVrmHumanoidAPI
    const TfToken vrmHumanBonesRightLittleDistal;
    /// \brief "vrm:humanBones:rightLittleIntermediate"
    ///
    /// UsdVrmHumanoidAPI
    const TfToken vrmHumanBonesRightLittleIntermediate;
    /// \brief "vrm:humanBones:rightLittleProximal"
    ///
    /// UsdVrmHumanoidAPI
    const TfToken vrmHumanBonesRightLittleProximal;
    /// \brief "vrm:humanBones:rightLowerArm"
    ///
    /// UsdVrmHumanoidAPI
    const TfToken vrmHumanBonesRightLowerArm;
    /// \brief "vrm:humanBones:rightLowerLeg"
    ///
    /// UsdVrmHumanoidAPI
    const TfToken vrmHumanBonesRightLowerLeg;
    /// \brief "vrm:humanBones:rightMiddleDistal"
    ///
    /// UsdVrmHumanoidAPI
    const TfToken vrmHumanBonesRightMiddleDistal;
    /// \brief "vrm:humanBones:rightMiddleIntermediate"
    ///
    /// UsdVrmHumanoidAPI
    const TfToken vrmHumanBonesRightMiddleIntermediate;
    /// \brief "vrm:humanBones:rightMiddleProximal"
    ///
    /// UsdVrmHumanoidAPI
    const TfToken vrmHumanBonesRightMiddleProximal;
    /// \brief "vrm:humanBones:rightRingDistal"
    ///
    /// UsdVrmHumanoidAPI
    const TfToken vrmHumanBonesRightRingDistal;
    /// \brief "vrm:humanBones:rightRingIntermediate"
    ///
    /// UsdVrmHumanoidAPI
    const TfToken vrmHumanBonesRightRingIntermediate;
    /// \brief "vrm:humanBones:rightRingProximal"
    ///
    /// UsdVrmHumanoidAPI
    const TfToken vrmHumanBonesRightRingProximal;
    /// \brief "vrm:humanBones:rightShoulder"
    ///
    /// UsdVrmHumanoidAPI
    const TfToken vrmHumanBonesRightShoulder;
    /// \brief "vrm:humanBones:rightThumbDistal"
    ///
    /// UsdVrmHumanoidAPI
    const TfToken vrmHumanBonesRightThumbDistal;
    /// \brief "vrm:humanBones:rightThumbMetacarpal"
    ///
    /// UsdVrmHumanoidAPI
    const TfToken vrmHumanBonesRightThumbMetacarpal;
    /// \brief "vrm:humanBones:rightThumbProximal"
    ///
    /// UsdVrmHumanoidAPI
    const TfToken vrmHumanBonesRightThumbProximal;
    /// \brief "vrm:humanBones:rightToes"
    ///
    /// UsdVrmHumanoidAPI
    const TfToken vrmHumanBonesRightToes;
    /// \brief "vrm:humanBones:rightUpperArm"
    ///
    /// UsdVrmHumanoidAPI
    const TfToken vrmHumanBonesRightUpperArm;
    /// \brief "vrm:humanBones:rightUpperLeg"
    ///
    /// UsdVrmHumanoidAPI
    const TfToken vrmHumanBonesRightUpperLeg;
    /// \brief "vrm:humanBones:spine"
    ///
    /// UsdVrmHumanoidAPI
    const TfToken vrmHumanBonesSpine;
    /// \brief "vrm:humanBones:upperChest"
    ///
    /// UsdVrmHumanoidAPI
    const TfToken vrmHumanBonesUpperChest;
    /// \brief "vrm:isBinary"
    ///
    /// UsdVrmExpressionAPI
    const TfToken vrmIsBinary;
    /// \brief "vrm:joints"
    ///
    /// UsdVrmSpringBoneAPI
    const TfToken vrmJoints;
    /// \brief "vrm:leftEye"
    ///
    /// UsdVrmLookAtAPI
    const TfToken vrmLeftEye;
    /// \brief "vrm:materialColorTargets"
    ///
    /// UsdVrmExpressionAPI
    const TfToken vrmMaterialColorTargets;
    /// \brief "vrm:materialColorTypes"
    ///
    /// UsdVrmExpressionAPI
    const TfToken vrmMaterialColorTypes;
    /// \brief "vrm:materialColorValues"
    ///
    /// UsdVrmExpressionAPI
    const TfToken vrmMaterialColorValues;
    /// \brief "vrm:morphTargets"
    ///
    /// UsdVrmExpressionAPI
    const TfToken vrmMorphTargets;
    /// \brief "vrm:morphTargetWeights"
    ///
    /// UsdVrmExpressionAPI
    const TfToken vrmMorphTargetWeights;
    /// \brief "vrm:node"
    ///
    /// UsdVrmColliderAPI
    const TfToken vrmNode;
    /// \brief "vrm:offset"
    ///
    /// UsdVrmColliderAPI
    const TfToken vrmOffset;
    /// \brief "vrm:overrideBlink"
    ///
    /// UsdVrmExpressionAPI
    const TfToken vrmOverrideBlink;
    /// \brief "vrm:overrideLookAt"
    ///
    /// UsdVrmExpressionAPI
    const TfToken vrmOverrideLookAt;
    /// \brief "vrm:overrideMouth"
    ///
    /// UsdVrmExpressionAPI
    const TfToken vrmOverrideMouth;
    /// \brief "vrm:radius"
    ///
    /// UsdVrmColliderAPI
    const TfToken vrmRadius;
    /// \brief "vrm:rightEye"
    ///
    /// UsdVrmLookAtAPI
    const TfToken vrmRightEye;
    /// \brief "vrm:shape"
    ///
    /// UsdVrmColliderAPI
    const TfToken vrmShape;
    /// \brief "vrm:skeleton"
    ///
    /// UsdVrmHumanoidAPI, UsdVrmLookAtAPI
    const TfToken vrmSkeleton;
    /// \brief "vrm:source"
    ///
    /// UsdVrmConstraintAPI
    const TfToken vrmSource;
    /// \brief "vrm:stiffness"
    ///
    /// UsdVrmSpringBoneAPI
    const TfToken vrmStiffness;
    /// \brief "vrm:tail"
    ///
    /// UsdVrmColliderAPI
    const TfToken vrmTail;
    /// \brief "vrm:type"
    ///
    /// UsdVrmLookAtAPI, UsdVrmConstraintAPI
    const TfToken vrmType;
    /// \brief "vrm:weight"
    ///
    /// UsdVrmConstraintAPI
    const TfToken vrmWeight;
    /// \brief "VrmColliderAPI"
    ///
    /// Schema identifer and family for UsdVrmColliderAPI
    const TfToken VrmColliderAPI;
    /// \brief "VrmConstraintAPI"
    ///
    /// Schema identifer and family for UsdVrmConstraintAPI
    const TfToken VrmConstraintAPI;
    /// \brief "VrmExpressionAPI"
    ///
    /// Schema identifer and family for UsdVrmExpressionAPI
    const TfToken VrmExpressionAPI;
    /// \brief "VrmHumanoidAPI"
    ///
    /// Schema identifer and family for UsdVrmHumanoidAPI
    const TfToken VrmHumanoidAPI;
    /// \brief "VrmLookAtAPI"
    ///
    /// Schema identifer and family for UsdVrmLookAtAPI
    const TfToken VrmLookAtAPI;
    /// \brief "VrmMaterialAPI"
    ///
    /// Schema identifer and family for UsdVrmMaterialAPI
    const TfToken VrmMaterialAPI;
    /// \brief "VrmMToonAPI"
    ///
    /// Schema identifer and family for UsdVrmMToonAPI
    const TfToken VrmMToonAPI;
    /// \brief "VrmSpringBoneAPI"
    ///
    /// Schema identifer and family for UsdVrmSpringBoneAPI
    const TfToken VrmSpringBoneAPI;
    /// \brief "VrmTextureInfoAPI"
    ///
    /// Schema identifer and family for UsdVrmTextureInfoAPI
    const TfToken VrmTextureInfoAPI;
    /// A vector of all of the tokens listed above.
    const std::vector<TfToken> allTokens;
};

/// \var UsdVrmTokens
///
/// A global variable with static, efficient \link TfToken TfTokens\endlink
/// for use in all public USD API.  \sa UsdVrmTokensType
extern USDVRM_API TfStaticData<UsdVrmTokensType> UsdVrmTokens;

PXR_NAMESPACE_CLOSE_SCOPE

#endif
