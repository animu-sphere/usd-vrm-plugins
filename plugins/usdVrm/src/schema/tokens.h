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
///     gprim.GetMyTokenValuedAttr().Set(UsdVrmTokens->vrmCenter);
/// \endcode
struct UsdVrmTokensType {
    USDVRM_API UsdVrmTokensType();
    /// \brief "vrm:center"
    /// 
    /// UsdVrmSpringBoneAPI
    const TfToken vrmCenter;
    /// \brief "vrm:colliderGroups"
    /// 
    /// UsdVrmSpringBoneAPI
    const TfToken vrmColliderGroups;
    /// \brief "vrm:dragForce"
    /// 
    /// UsdVrmSpringBoneAPI
    const TfToken vrmDragForce;
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
    /// UsdVrmLookAtAPI
    const TfToken vrmType;
    /// \brief "VrmColliderAPI"
    /// 
    /// Schema identifer and family for UsdVrmColliderAPI
    const TfToken VrmColliderAPI;
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
    /// \brief "VrmSpringBoneAPI"
    /// 
    /// Schema identifer and family for UsdVrmSpringBoneAPI
    const TfToken VrmSpringBoneAPI;
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
