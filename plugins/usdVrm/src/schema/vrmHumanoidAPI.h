//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef USDVRM_GENERATED_VRMHUMANOIDAPI_H
#define USDVRM_GENERATED_VRMHUMANOIDAPI_H

/// \file usdVrm/vrmHumanoidAPI.h

#include "pxr/pxr.h"
#include "./api.h"
#include "pxr/usd/usd/apiSchemaBase.h"
#include "pxr/usd/usd/prim.h"
#include "pxr/usd/usd/stage.h"
#include "./tokens.h"

#include "pxr/base/vt/value.h"

#include "pxr/base/gf/vec3d.h"
#include "pxr/base/gf/vec3f.h"
#include "pxr/base/gf/matrix4d.h"

#include "pxr/base/tf/token.h"
#include "pxr/base/tf/type.h"

PXR_NAMESPACE_OPEN_SCOPE

class SdfAssetPath;

// -------------------------------------------------------------------------- //
// VRMHUMANOIDAPI                                                             //
// -------------------------------------------------------------------------- //

/// \class UsdVrmHumanoidAPI
///
/// Maps VRM humanoid bones to USD skeleton joints. Apply to the
/// /Asset/rig/Humanoid prim. `vrm:skeleton` resolves to the UsdSkelSkeleton; each
/// `vrm:humanBones:<bone>` token holds the joint's path token within
/// Skeleton.joints. Bones absent from the source VRM are simply not authored;
/// non-standard bones outside this set are preserved as custom attributes.
///
/// For any described attribute \em Fallback \em Value or \em Allowed \em Values below
/// that are text/tokens, the actual token is published and defined in \ref UsdVrmTokens.
/// So to set an attribute to the value "rightHanded", use UsdVrmTokens->rightHanded
/// as the value.
///
class UsdVrmHumanoidAPI : public UsdAPISchemaBase
{
public:
    /// Compile time constant representing what kind of schema this class is.
    ///
    /// \sa UsdSchemaKind
    static const UsdSchemaKind schemaKind = UsdSchemaKind::SingleApplyAPI;

    /// Construct a UsdVrmHumanoidAPI on UsdPrim \p prim .
    /// Equivalent to UsdVrmHumanoidAPI::Get(prim.GetStage(), prim.GetPath())
    /// for a \em valid \p prim, but will not immediately throw an error for
    /// an invalid \p prim
    explicit UsdVrmHumanoidAPI(const UsdPrim& prim=UsdPrim())
        : UsdAPISchemaBase(prim)
    {
    }

    /// Construct a UsdVrmHumanoidAPI on the prim held by \p schemaObj .
    /// Should be preferred over UsdVrmHumanoidAPI(schemaObj.GetPrim()),
    /// as it preserves SchemaBase state.
    explicit UsdVrmHumanoidAPI(const UsdSchemaBase& schemaObj)
        : UsdAPISchemaBase(schemaObj)
    {
    }

    /// Destructor.
    USDVRM_API
    virtual ~UsdVrmHumanoidAPI();

    /// Return a vector of names of all pre-declared attributes for this schema
    /// class and all its ancestor classes.  Does not include attributes that
    /// may be authored by custom/extended methods of the schemas involved.
    USDVRM_API
    static const TfTokenVector &
    GetSchemaAttributeNames(bool includeInherited=true);

    /// Return a UsdVrmHumanoidAPI holding the prim adhering to this
    /// schema at \p path on \p stage.  If no prim exists at \p path on
    /// \p stage, or if the prim at that path does not adhere to this schema,
    /// return an invalid schema object.  This is shorthand for the following:
    ///
    /// \code
    /// UsdVrmHumanoidAPI(stage->GetPrimAtPath(path));
    /// \endcode
    ///
    USDVRM_API
    static UsdVrmHumanoidAPI
    Get(const UsdStagePtr &stage, const SdfPath &path);


    /// Returns true if this <b>single-apply</b> API schema can be applied to 
    /// the given \p prim. If this schema can not be a applied to the prim, 
    /// this returns false and, if provided, populates \p whyNot with the 
    /// reason it can not be applied.
    /// 
    /// Note that if CanApply returns false, that does not necessarily imply
    /// that calling Apply will fail. Callers are expected to call CanApply
    /// before calling Apply if they want to ensure that it is valid to 
    /// apply a schema.
    /// 
    /// \sa UsdPrim::GetAppliedSchemas()
    /// \sa UsdPrim::HasAPI()
    /// \sa UsdPrim::CanApplyAPI()
    /// \sa UsdPrim::ApplyAPI()
    /// \sa UsdPrim::RemoveAPI()
    ///
    USDVRM_API
    static bool 
    CanApply(const UsdPrim &prim, std::string *whyNot=nullptr);

    /// Applies this <b>single-apply</b> API schema to the given \p prim.
    /// This information is stored by adding "VrmHumanoidAPI" to the 
    /// token-valued, listOp metadata \em apiSchemas on the prim.
    /// 
    /// \return A valid UsdVrmHumanoidAPI object is returned upon success. 
    /// An invalid (or empty) UsdVrmHumanoidAPI object is returned upon 
    /// failure. See \ref UsdPrim::ApplyAPI() for conditions 
    /// resulting in failure. 
    /// 
    /// \sa UsdPrim::GetAppliedSchemas()
    /// \sa UsdPrim::HasAPI()
    /// \sa UsdPrim::CanApplyAPI()
    /// \sa UsdPrim::ApplyAPI()
    /// \sa UsdPrim::RemoveAPI()
    ///
    USDVRM_API
    static UsdVrmHumanoidAPI 
    Apply(const UsdPrim &prim);

protected:
    /// Returns the kind of schema this class belongs to.
    ///
    /// \sa UsdSchemaKind
    USDVRM_API
    UsdSchemaKind _GetSchemaKind() const override;

private:
    // needs to invoke _GetStaticTfType.
    friend class UsdSchemaRegistry;
    USDVRM_API
    static const TfType &_GetStaticTfType();

    static bool _IsTypedSchema();

    // override SchemaBase virtuals.
    USDVRM_API
    const TfType &_GetTfType() const override;

public:
    // --------------------------------------------------------------------- //
    // VRMHUMANBONESHIPS 
    // --------------------------------------------------------------------- //
    /// VRM human bone 'hips'.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform token vrm:humanBones:hips` |
    /// | C++ Type | TfToken |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Token |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDVRM_API
    UsdAttribute GetVrmHumanBonesHipsAttr() const;

    /// See GetVrmHumanBonesHipsAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateVrmHumanBonesHipsAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // VRMHUMANBONESSPINE 
    // --------------------------------------------------------------------- //
    /// VRM human bone 'spine'.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform token vrm:humanBones:spine` |
    /// | C++ Type | TfToken |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Token |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDVRM_API
    UsdAttribute GetVrmHumanBonesSpineAttr() const;

    /// See GetVrmHumanBonesSpineAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateVrmHumanBonesSpineAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // VRMHUMANBONESCHEST 
    // --------------------------------------------------------------------- //
    /// VRM human bone 'chest'.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform token vrm:humanBones:chest` |
    /// | C++ Type | TfToken |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Token |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDVRM_API
    UsdAttribute GetVrmHumanBonesChestAttr() const;

    /// See GetVrmHumanBonesChestAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateVrmHumanBonesChestAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // VRMHUMANBONESUPPERCHEST 
    // --------------------------------------------------------------------- //
    /// VRM human bone 'upperChest'.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform token vrm:humanBones:upperChest` |
    /// | C++ Type | TfToken |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Token |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDVRM_API
    UsdAttribute GetVrmHumanBonesUpperChestAttr() const;

    /// See GetVrmHumanBonesUpperChestAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateVrmHumanBonesUpperChestAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // VRMHUMANBONESNECK 
    // --------------------------------------------------------------------- //
    /// VRM human bone 'neck'.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform token vrm:humanBones:neck` |
    /// | C++ Type | TfToken |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Token |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDVRM_API
    UsdAttribute GetVrmHumanBonesNeckAttr() const;

    /// See GetVrmHumanBonesNeckAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateVrmHumanBonesNeckAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // VRMHUMANBONESHEAD 
    // --------------------------------------------------------------------- //
    /// VRM human bone 'head'.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform token vrm:humanBones:head` |
    /// | C++ Type | TfToken |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Token |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDVRM_API
    UsdAttribute GetVrmHumanBonesHeadAttr() const;

    /// See GetVrmHumanBonesHeadAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateVrmHumanBonesHeadAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // VRMHUMANBONESLEFTEYE 
    // --------------------------------------------------------------------- //
    /// VRM human bone 'leftEye'.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform token vrm:humanBones:leftEye` |
    /// | C++ Type | TfToken |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Token |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDVRM_API
    UsdAttribute GetVrmHumanBonesLeftEyeAttr() const;

    /// See GetVrmHumanBonesLeftEyeAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateVrmHumanBonesLeftEyeAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // VRMHUMANBONESRIGHTEYE 
    // --------------------------------------------------------------------- //
    /// VRM human bone 'rightEye'.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform token vrm:humanBones:rightEye` |
    /// | C++ Type | TfToken |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Token |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDVRM_API
    UsdAttribute GetVrmHumanBonesRightEyeAttr() const;

    /// See GetVrmHumanBonesRightEyeAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateVrmHumanBonesRightEyeAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // VRMHUMANBONESJAW 
    // --------------------------------------------------------------------- //
    /// VRM human bone 'jaw'.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform token vrm:humanBones:jaw` |
    /// | C++ Type | TfToken |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Token |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDVRM_API
    UsdAttribute GetVrmHumanBonesJawAttr() const;

    /// See GetVrmHumanBonesJawAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateVrmHumanBonesJawAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // VRMHUMANBONESLEFTUPPERLEG 
    // --------------------------------------------------------------------- //
    /// VRM human bone 'leftUpperLeg'.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform token vrm:humanBones:leftUpperLeg` |
    /// | C++ Type | TfToken |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Token |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDVRM_API
    UsdAttribute GetVrmHumanBonesLeftUpperLegAttr() const;

    /// See GetVrmHumanBonesLeftUpperLegAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateVrmHumanBonesLeftUpperLegAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // VRMHUMANBONESLEFTLOWERLEG 
    // --------------------------------------------------------------------- //
    /// VRM human bone 'leftLowerLeg'.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform token vrm:humanBones:leftLowerLeg` |
    /// | C++ Type | TfToken |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Token |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDVRM_API
    UsdAttribute GetVrmHumanBonesLeftLowerLegAttr() const;

    /// See GetVrmHumanBonesLeftLowerLegAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateVrmHumanBonesLeftLowerLegAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // VRMHUMANBONESLEFTFOOT 
    // --------------------------------------------------------------------- //
    /// VRM human bone 'leftFoot'.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform token vrm:humanBones:leftFoot` |
    /// | C++ Type | TfToken |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Token |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDVRM_API
    UsdAttribute GetVrmHumanBonesLeftFootAttr() const;

    /// See GetVrmHumanBonesLeftFootAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateVrmHumanBonesLeftFootAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // VRMHUMANBONESLEFTTOES 
    // --------------------------------------------------------------------- //
    /// VRM human bone 'leftToes'.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform token vrm:humanBones:leftToes` |
    /// | C++ Type | TfToken |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Token |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDVRM_API
    UsdAttribute GetVrmHumanBonesLeftToesAttr() const;

    /// See GetVrmHumanBonesLeftToesAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateVrmHumanBonesLeftToesAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // VRMHUMANBONESRIGHTUPPERLEG 
    // --------------------------------------------------------------------- //
    /// VRM human bone 'rightUpperLeg'.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform token vrm:humanBones:rightUpperLeg` |
    /// | C++ Type | TfToken |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Token |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDVRM_API
    UsdAttribute GetVrmHumanBonesRightUpperLegAttr() const;

    /// See GetVrmHumanBonesRightUpperLegAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateVrmHumanBonesRightUpperLegAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // VRMHUMANBONESRIGHTLOWERLEG 
    // --------------------------------------------------------------------- //
    /// VRM human bone 'rightLowerLeg'.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform token vrm:humanBones:rightLowerLeg` |
    /// | C++ Type | TfToken |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Token |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDVRM_API
    UsdAttribute GetVrmHumanBonesRightLowerLegAttr() const;

    /// See GetVrmHumanBonesRightLowerLegAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateVrmHumanBonesRightLowerLegAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // VRMHUMANBONESRIGHTFOOT 
    // --------------------------------------------------------------------- //
    /// VRM human bone 'rightFoot'.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform token vrm:humanBones:rightFoot` |
    /// | C++ Type | TfToken |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Token |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDVRM_API
    UsdAttribute GetVrmHumanBonesRightFootAttr() const;

    /// See GetVrmHumanBonesRightFootAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateVrmHumanBonesRightFootAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // VRMHUMANBONESRIGHTTOES 
    // --------------------------------------------------------------------- //
    /// VRM human bone 'rightToes'.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform token vrm:humanBones:rightToes` |
    /// | C++ Type | TfToken |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Token |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDVRM_API
    UsdAttribute GetVrmHumanBonesRightToesAttr() const;

    /// See GetVrmHumanBonesRightToesAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateVrmHumanBonesRightToesAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // VRMHUMANBONESLEFTSHOULDER 
    // --------------------------------------------------------------------- //
    /// VRM human bone 'leftShoulder'.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform token vrm:humanBones:leftShoulder` |
    /// | C++ Type | TfToken |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Token |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDVRM_API
    UsdAttribute GetVrmHumanBonesLeftShoulderAttr() const;

    /// See GetVrmHumanBonesLeftShoulderAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateVrmHumanBonesLeftShoulderAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // VRMHUMANBONESLEFTUPPERARM 
    // --------------------------------------------------------------------- //
    /// VRM human bone 'leftUpperArm'.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform token vrm:humanBones:leftUpperArm` |
    /// | C++ Type | TfToken |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Token |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDVRM_API
    UsdAttribute GetVrmHumanBonesLeftUpperArmAttr() const;

    /// See GetVrmHumanBonesLeftUpperArmAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateVrmHumanBonesLeftUpperArmAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // VRMHUMANBONESLEFTLOWERARM 
    // --------------------------------------------------------------------- //
    /// VRM human bone 'leftLowerArm'.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform token vrm:humanBones:leftLowerArm` |
    /// | C++ Type | TfToken |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Token |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDVRM_API
    UsdAttribute GetVrmHumanBonesLeftLowerArmAttr() const;

    /// See GetVrmHumanBonesLeftLowerArmAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateVrmHumanBonesLeftLowerArmAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // VRMHUMANBONESLEFTHAND 
    // --------------------------------------------------------------------- //
    /// VRM human bone 'leftHand'.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform token vrm:humanBones:leftHand` |
    /// | C++ Type | TfToken |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Token |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDVRM_API
    UsdAttribute GetVrmHumanBonesLeftHandAttr() const;

    /// See GetVrmHumanBonesLeftHandAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateVrmHumanBonesLeftHandAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // VRMHUMANBONESRIGHTSHOULDER 
    // --------------------------------------------------------------------- //
    /// VRM human bone 'rightShoulder'.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform token vrm:humanBones:rightShoulder` |
    /// | C++ Type | TfToken |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Token |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDVRM_API
    UsdAttribute GetVrmHumanBonesRightShoulderAttr() const;

    /// See GetVrmHumanBonesRightShoulderAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateVrmHumanBonesRightShoulderAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // VRMHUMANBONESRIGHTUPPERARM 
    // --------------------------------------------------------------------- //
    /// VRM human bone 'rightUpperArm'.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform token vrm:humanBones:rightUpperArm` |
    /// | C++ Type | TfToken |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Token |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDVRM_API
    UsdAttribute GetVrmHumanBonesRightUpperArmAttr() const;

    /// See GetVrmHumanBonesRightUpperArmAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateVrmHumanBonesRightUpperArmAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // VRMHUMANBONESRIGHTLOWERARM 
    // --------------------------------------------------------------------- //
    /// VRM human bone 'rightLowerArm'.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform token vrm:humanBones:rightLowerArm` |
    /// | C++ Type | TfToken |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Token |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDVRM_API
    UsdAttribute GetVrmHumanBonesRightLowerArmAttr() const;

    /// See GetVrmHumanBonesRightLowerArmAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateVrmHumanBonesRightLowerArmAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // VRMHUMANBONESRIGHTHAND 
    // --------------------------------------------------------------------- //
    /// VRM human bone 'rightHand'.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform token vrm:humanBones:rightHand` |
    /// | C++ Type | TfToken |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Token |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDVRM_API
    UsdAttribute GetVrmHumanBonesRightHandAttr() const;

    /// See GetVrmHumanBonesRightHandAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateVrmHumanBonesRightHandAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // VRMHUMANBONESLEFTTHUMBMETACARPAL 
    // --------------------------------------------------------------------- //
    /// VRM 1.0 human bone 'leftThumbMetacarpal'.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform token vrm:humanBones:leftThumbMetacarpal` |
    /// | C++ Type | TfToken |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Token |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDVRM_API
    UsdAttribute GetVrmHumanBonesLeftThumbMetacarpalAttr() const;

    /// See GetVrmHumanBonesLeftThumbMetacarpalAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateVrmHumanBonesLeftThumbMetacarpalAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // VRMHUMANBONESLEFTTHUMBPROXIMAL 
    // --------------------------------------------------------------------- //
    /// VRM human bone 'leftThumbProximal'.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform token vrm:humanBones:leftThumbProximal` |
    /// | C++ Type | TfToken |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Token |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDVRM_API
    UsdAttribute GetVrmHumanBonesLeftThumbProximalAttr() const;

    /// See GetVrmHumanBonesLeftThumbProximalAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateVrmHumanBonesLeftThumbProximalAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // VRMHUMANBONESLEFTTHUMBDISTAL 
    // --------------------------------------------------------------------- //
    /// VRM human bone 'leftThumbDistal'.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform token vrm:humanBones:leftThumbDistal` |
    /// | C++ Type | TfToken |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Token |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDVRM_API
    UsdAttribute GetVrmHumanBonesLeftThumbDistalAttr() const;

    /// See GetVrmHumanBonesLeftThumbDistalAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateVrmHumanBonesLeftThumbDistalAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // VRMHUMANBONESLEFTINDEXPROXIMAL 
    // --------------------------------------------------------------------- //
    /// VRM human bone 'leftIndexProximal'.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform token vrm:humanBones:leftIndexProximal` |
    /// | C++ Type | TfToken |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Token |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDVRM_API
    UsdAttribute GetVrmHumanBonesLeftIndexProximalAttr() const;

    /// See GetVrmHumanBonesLeftIndexProximalAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateVrmHumanBonesLeftIndexProximalAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // VRMHUMANBONESLEFTINDEXINTERMEDIATE 
    // --------------------------------------------------------------------- //
    /// VRM human bone 'leftIndexIntermediate'.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform token vrm:humanBones:leftIndexIntermediate` |
    /// | C++ Type | TfToken |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Token |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDVRM_API
    UsdAttribute GetVrmHumanBonesLeftIndexIntermediateAttr() const;

    /// See GetVrmHumanBonesLeftIndexIntermediateAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateVrmHumanBonesLeftIndexIntermediateAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // VRMHUMANBONESLEFTINDEXDISTAL 
    // --------------------------------------------------------------------- //
    /// VRM human bone 'leftIndexDistal'.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform token vrm:humanBones:leftIndexDistal` |
    /// | C++ Type | TfToken |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Token |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDVRM_API
    UsdAttribute GetVrmHumanBonesLeftIndexDistalAttr() const;

    /// See GetVrmHumanBonesLeftIndexDistalAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateVrmHumanBonesLeftIndexDistalAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // VRMHUMANBONESLEFTMIDDLEPROXIMAL 
    // --------------------------------------------------------------------- //
    /// VRM human bone 'leftMiddleProximal'.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform token vrm:humanBones:leftMiddleProximal` |
    /// | C++ Type | TfToken |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Token |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDVRM_API
    UsdAttribute GetVrmHumanBonesLeftMiddleProximalAttr() const;

    /// See GetVrmHumanBonesLeftMiddleProximalAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateVrmHumanBonesLeftMiddleProximalAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // VRMHUMANBONESLEFTMIDDLEINTERMEDIATE 
    // --------------------------------------------------------------------- //
    /// VRM human bone 'leftMiddleIntermediate'.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform token vrm:humanBones:leftMiddleIntermediate` |
    /// | C++ Type | TfToken |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Token |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDVRM_API
    UsdAttribute GetVrmHumanBonesLeftMiddleIntermediateAttr() const;

    /// See GetVrmHumanBonesLeftMiddleIntermediateAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateVrmHumanBonesLeftMiddleIntermediateAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // VRMHUMANBONESLEFTMIDDLEDISTAL 
    // --------------------------------------------------------------------- //
    /// VRM human bone 'leftMiddleDistal'.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform token vrm:humanBones:leftMiddleDistal` |
    /// | C++ Type | TfToken |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Token |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDVRM_API
    UsdAttribute GetVrmHumanBonesLeftMiddleDistalAttr() const;

    /// See GetVrmHumanBonesLeftMiddleDistalAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateVrmHumanBonesLeftMiddleDistalAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // VRMHUMANBONESLEFTRINGPROXIMAL 
    // --------------------------------------------------------------------- //
    /// VRM human bone 'leftRingProximal'.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform token vrm:humanBones:leftRingProximal` |
    /// | C++ Type | TfToken |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Token |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDVRM_API
    UsdAttribute GetVrmHumanBonesLeftRingProximalAttr() const;

    /// See GetVrmHumanBonesLeftRingProximalAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateVrmHumanBonesLeftRingProximalAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // VRMHUMANBONESLEFTRINGINTERMEDIATE 
    // --------------------------------------------------------------------- //
    /// VRM human bone 'leftRingIntermediate'.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform token vrm:humanBones:leftRingIntermediate` |
    /// | C++ Type | TfToken |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Token |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDVRM_API
    UsdAttribute GetVrmHumanBonesLeftRingIntermediateAttr() const;

    /// See GetVrmHumanBonesLeftRingIntermediateAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateVrmHumanBonesLeftRingIntermediateAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // VRMHUMANBONESLEFTRINGDISTAL 
    // --------------------------------------------------------------------- //
    /// VRM human bone 'leftRingDistal'.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform token vrm:humanBones:leftRingDistal` |
    /// | C++ Type | TfToken |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Token |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDVRM_API
    UsdAttribute GetVrmHumanBonesLeftRingDistalAttr() const;

    /// See GetVrmHumanBonesLeftRingDistalAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateVrmHumanBonesLeftRingDistalAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // VRMHUMANBONESLEFTLITTLEPROXIMAL 
    // --------------------------------------------------------------------- //
    /// VRM human bone 'leftLittleProximal'.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform token vrm:humanBones:leftLittleProximal` |
    /// | C++ Type | TfToken |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Token |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDVRM_API
    UsdAttribute GetVrmHumanBonesLeftLittleProximalAttr() const;

    /// See GetVrmHumanBonesLeftLittleProximalAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateVrmHumanBonesLeftLittleProximalAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // VRMHUMANBONESLEFTLITTLEINTERMEDIATE 
    // --------------------------------------------------------------------- //
    /// VRM human bone 'leftLittleIntermediate'.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform token vrm:humanBones:leftLittleIntermediate` |
    /// | C++ Type | TfToken |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Token |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDVRM_API
    UsdAttribute GetVrmHumanBonesLeftLittleIntermediateAttr() const;

    /// See GetVrmHumanBonesLeftLittleIntermediateAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateVrmHumanBonesLeftLittleIntermediateAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // VRMHUMANBONESLEFTLITTLEDISTAL 
    // --------------------------------------------------------------------- //
    /// VRM human bone 'leftLittleDistal'.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform token vrm:humanBones:leftLittleDistal` |
    /// | C++ Type | TfToken |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Token |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDVRM_API
    UsdAttribute GetVrmHumanBonesLeftLittleDistalAttr() const;

    /// See GetVrmHumanBonesLeftLittleDistalAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateVrmHumanBonesLeftLittleDistalAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // VRMHUMANBONESRIGHTTHUMBMETACARPAL 
    // --------------------------------------------------------------------- //
    /// VRM 1.0 human bone 'rightThumbMetacarpal'.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform token vrm:humanBones:rightThumbMetacarpal` |
    /// | C++ Type | TfToken |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Token |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDVRM_API
    UsdAttribute GetVrmHumanBonesRightThumbMetacarpalAttr() const;

    /// See GetVrmHumanBonesRightThumbMetacarpalAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateVrmHumanBonesRightThumbMetacarpalAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // VRMHUMANBONESRIGHTTHUMBPROXIMAL 
    // --------------------------------------------------------------------- //
    /// VRM human bone 'rightThumbProximal'.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform token vrm:humanBones:rightThumbProximal` |
    /// | C++ Type | TfToken |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Token |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDVRM_API
    UsdAttribute GetVrmHumanBonesRightThumbProximalAttr() const;

    /// See GetVrmHumanBonesRightThumbProximalAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateVrmHumanBonesRightThumbProximalAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // VRMHUMANBONESRIGHTTHUMBDISTAL 
    // --------------------------------------------------------------------- //
    /// VRM human bone 'rightThumbDistal'.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform token vrm:humanBones:rightThumbDistal` |
    /// | C++ Type | TfToken |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Token |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDVRM_API
    UsdAttribute GetVrmHumanBonesRightThumbDistalAttr() const;

    /// See GetVrmHumanBonesRightThumbDistalAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateVrmHumanBonesRightThumbDistalAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // VRMHUMANBONESRIGHTINDEXPROXIMAL 
    // --------------------------------------------------------------------- //
    /// VRM human bone 'rightIndexProximal'.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform token vrm:humanBones:rightIndexProximal` |
    /// | C++ Type | TfToken |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Token |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDVRM_API
    UsdAttribute GetVrmHumanBonesRightIndexProximalAttr() const;

    /// See GetVrmHumanBonesRightIndexProximalAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateVrmHumanBonesRightIndexProximalAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // VRMHUMANBONESRIGHTINDEXINTERMEDIATE 
    // --------------------------------------------------------------------- //
    /// VRM human bone 'rightIndexIntermediate'.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform token vrm:humanBones:rightIndexIntermediate` |
    /// | C++ Type | TfToken |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Token |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDVRM_API
    UsdAttribute GetVrmHumanBonesRightIndexIntermediateAttr() const;

    /// See GetVrmHumanBonesRightIndexIntermediateAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateVrmHumanBonesRightIndexIntermediateAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // VRMHUMANBONESRIGHTINDEXDISTAL 
    // --------------------------------------------------------------------- //
    /// VRM human bone 'rightIndexDistal'.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform token vrm:humanBones:rightIndexDistal` |
    /// | C++ Type | TfToken |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Token |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDVRM_API
    UsdAttribute GetVrmHumanBonesRightIndexDistalAttr() const;

    /// See GetVrmHumanBonesRightIndexDistalAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateVrmHumanBonesRightIndexDistalAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // VRMHUMANBONESRIGHTMIDDLEPROXIMAL 
    // --------------------------------------------------------------------- //
    /// VRM human bone 'rightMiddleProximal'.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform token vrm:humanBones:rightMiddleProximal` |
    /// | C++ Type | TfToken |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Token |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDVRM_API
    UsdAttribute GetVrmHumanBonesRightMiddleProximalAttr() const;

    /// See GetVrmHumanBonesRightMiddleProximalAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateVrmHumanBonesRightMiddleProximalAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // VRMHUMANBONESRIGHTMIDDLEINTERMEDIATE 
    // --------------------------------------------------------------------- //
    /// VRM human bone 'rightMiddleIntermediate'.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform token vrm:humanBones:rightMiddleIntermediate` |
    /// | C++ Type | TfToken |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Token |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDVRM_API
    UsdAttribute GetVrmHumanBonesRightMiddleIntermediateAttr() const;

    /// See GetVrmHumanBonesRightMiddleIntermediateAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateVrmHumanBonesRightMiddleIntermediateAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // VRMHUMANBONESRIGHTMIDDLEDISTAL 
    // --------------------------------------------------------------------- //
    /// VRM human bone 'rightMiddleDistal'.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform token vrm:humanBones:rightMiddleDistal` |
    /// | C++ Type | TfToken |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Token |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDVRM_API
    UsdAttribute GetVrmHumanBonesRightMiddleDistalAttr() const;

    /// See GetVrmHumanBonesRightMiddleDistalAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateVrmHumanBonesRightMiddleDistalAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // VRMHUMANBONESRIGHTRINGPROXIMAL 
    // --------------------------------------------------------------------- //
    /// VRM human bone 'rightRingProximal'.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform token vrm:humanBones:rightRingProximal` |
    /// | C++ Type | TfToken |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Token |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDVRM_API
    UsdAttribute GetVrmHumanBonesRightRingProximalAttr() const;

    /// See GetVrmHumanBonesRightRingProximalAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateVrmHumanBonesRightRingProximalAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // VRMHUMANBONESRIGHTRINGINTERMEDIATE 
    // --------------------------------------------------------------------- //
    /// VRM human bone 'rightRingIntermediate'.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform token vrm:humanBones:rightRingIntermediate` |
    /// | C++ Type | TfToken |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Token |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDVRM_API
    UsdAttribute GetVrmHumanBonesRightRingIntermediateAttr() const;

    /// See GetVrmHumanBonesRightRingIntermediateAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateVrmHumanBonesRightRingIntermediateAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // VRMHUMANBONESRIGHTRINGDISTAL 
    // --------------------------------------------------------------------- //
    /// VRM human bone 'rightRingDistal'.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform token vrm:humanBones:rightRingDistal` |
    /// | C++ Type | TfToken |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Token |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDVRM_API
    UsdAttribute GetVrmHumanBonesRightRingDistalAttr() const;

    /// See GetVrmHumanBonesRightRingDistalAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateVrmHumanBonesRightRingDistalAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // VRMHUMANBONESRIGHTLITTLEPROXIMAL 
    // --------------------------------------------------------------------- //
    /// VRM human bone 'rightLittleProximal'.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform token vrm:humanBones:rightLittleProximal` |
    /// | C++ Type | TfToken |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Token |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDVRM_API
    UsdAttribute GetVrmHumanBonesRightLittleProximalAttr() const;

    /// See GetVrmHumanBonesRightLittleProximalAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateVrmHumanBonesRightLittleProximalAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // VRMHUMANBONESRIGHTLITTLEINTERMEDIATE 
    // --------------------------------------------------------------------- //
    /// VRM human bone 'rightLittleIntermediate'.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform token vrm:humanBones:rightLittleIntermediate` |
    /// | C++ Type | TfToken |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Token |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDVRM_API
    UsdAttribute GetVrmHumanBonesRightLittleIntermediateAttr() const;

    /// See GetVrmHumanBonesRightLittleIntermediateAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateVrmHumanBonesRightLittleIntermediateAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // VRMHUMANBONESRIGHTLITTLEDISTAL 
    // --------------------------------------------------------------------- //
    /// VRM human bone 'rightLittleDistal'.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform token vrm:humanBones:rightLittleDistal` |
    /// | C++ Type | TfToken |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Token |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDVRM_API
    UsdAttribute GetVrmHumanBonesRightLittleDistalAttr() const;

    /// See GetVrmHumanBonesRightLittleDistalAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateVrmHumanBonesRightLittleDistalAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // VRMSKELETON 
    // --------------------------------------------------------------------- //
    /// The UsdSkelSkeleton whose joints the human bones name.
    ///
    USDVRM_API
    UsdRelationship GetVrmSkeletonRel() const;

    /// See GetVrmSkeletonRel(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create
    USDVRM_API
    UsdRelationship CreateVrmSkeletonRel() const;

public:
    // ===================================================================== //
    // Feel free to add custom code below this line, it will be preserved by 
    // the code generator. 
    //
    // Just remember to: 
    //  - Close the class declaration with }; 
    //  - Close the namespace with PXR_NAMESPACE_CLOSE_SCOPE
    //  - Close the include guard with #endif
    // ===================================================================== //
    // --(BEGIN CUSTOM CODE)--
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
