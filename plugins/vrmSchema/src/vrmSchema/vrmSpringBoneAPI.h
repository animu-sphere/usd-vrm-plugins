//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef USDVRM_GENERATED_VRMSPRINGBONEAPI_H
#define USDVRM_GENERATED_VRMSPRINGBONEAPI_H

/// \file usdVrm/vrmSpringBoneAPI.h

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
// VRMSPRINGBONEAPI                                                           //
// -------------------------------------------------------------------------- //

/// \class UsdVrmSpringBoneAPI
///
/// A VRM SpringBone chain (secondary motion, data only - no simulation).
/// Apply to each /Asset/rig/SecondaryMotion/SpringBones/<name> prim. The per-joint
/// parameter arrays are parallel to vrm:joints. gravityDir is model-space (it takes
/// the VRM-0.x front bake). Colliders are referenced by relationship.
///
/// For any described attribute \em Fallback \em Value or \em Allowed \em Values below
/// that are text/tokens, the actual token is published and defined in \ref UsdVrmTokens.
/// So to set an attribute to the value "rightHanded", use UsdVrmTokens->rightHanded
/// as the value.
///
class UsdVrmSpringBoneAPI : public UsdAPISchemaBase
{
public:
    /// Compile time constant representing what kind of schema this class is.
    ///
    /// \sa UsdSchemaKind
    static const UsdSchemaKind schemaKind = UsdSchemaKind::SingleApplyAPI;

    /// Construct a UsdVrmSpringBoneAPI on UsdPrim \p prim .
    /// Equivalent to UsdVrmSpringBoneAPI::Get(prim.GetStage(), prim.GetPath())
    /// for a \em valid \p prim, but will not immediately throw an error for
    /// an invalid \p prim
    explicit UsdVrmSpringBoneAPI(const UsdPrim& prim=UsdPrim())
        : UsdAPISchemaBase(prim)
    {
    }

    /// Construct a UsdVrmSpringBoneAPI on the prim held by \p schemaObj .
    /// Should be preferred over UsdVrmSpringBoneAPI(schemaObj.GetPrim()),
    /// as it preserves SchemaBase state.
    explicit UsdVrmSpringBoneAPI(const UsdSchemaBase& schemaObj)
        : UsdAPISchemaBase(schemaObj)
    {
    }

    /// Destructor.
    USDVRM_API
    virtual ~UsdVrmSpringBoneAPI();

    /// Return a vector of names of all pre-declared attributes for this schema
    /// class and all its ancestor classes.  Does not include attributes that
    /// may be authored by custom/extended methods of the schemas involved.
    USDVRM_API
    static const TfTokenVector &
    GetSchemaAttributeNames(bool includeInherited=true);

    /// Return a UsdVrmSpringBoneAPI holding the prim adhering to this
    /// schema at \p path on \p stage.  If no prim exists at \p path on
    /// \p stage, or if the prim at that path does not adhere to this schema,
    /// return an invalid schema object.  This is shorthand for the following:
    ///
    /// \code
    /// UsdVrmSpringBoneAPI(stage->GetPrimAtPath(path));
    /// \endcode
    ///
    USDVRM_API
    static UsdVrmSpringBoneAPI
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
    /// This information is stored by adding "VrmSpringBoneAPI" to the 
    /// token-valued, listOp metadata \em apiSchemas on the prim.
    /// 
    /// \return A valid UsdVrmSpringBoneAPI object is returned upon success. 
    /// An invalid (or empty) UsdVrmSpringBoneAPI object is returned upon 
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
    static UsdVrmSpringBoneAPI 
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
    // VRMJOINTS 
    // --------------------------------------------------------------------- //
    /// Ordered joint path tokens forming the spring chain.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform token[] vrm:joints` |
    /// | C++ Type | VtArray<TfToken> |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->TokenArray |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDVRM_API
    UsdAttribute GetVrmJointsAttr() const;

    /// See GetVrmJointsAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateVrmJointsAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // VRMSTIFFNESS 
    // --------------------------------------------------------------------- //
    /// Per-joint stiffness, parallel to vrm:joints.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform float[] vrm:stiffness` |
    /// | C++ Type | VtArray<float> |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->FloatArray |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDVRM_API
    UsdAttribute GetVrmStiffnessAttr() const;

    /// See GetVrmStiffnessAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateVrmStiffnessAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // VRMGRAVITYPOWER 
    // --------------------------------------------------------------------- //
    /// Per-joint gravity power, parallel to vrm:joints.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform float[] vrm:gravityPower` |
    /// | C++ Type | VtArray<float> |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->FloatArray |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDVRM_API
    UsdAttribute GetVrmGravityPowerAttr() const;

    /// See GetVrmGravityPowerAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateVrmGravityPowerAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // VRMDRAGFORCE 
    // --------------------------------------------------------------------- //
    /// Per-joint drag force, parallel to vrm:joints.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform float[] vrm:dragForce` |
    /// | C++ Type | VtArray<float> |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->FloatArray |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDVRM_API
    UsdAttribute GetVrmDragForceAttr() const;

    /// See GetVrmDragForceAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateVrmDragForceAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // VRMHITRADIUS 
    // --------------------------------------------------------------------- //
    /// Per-joint hit radius, parallel to vrm:joints.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform float[] vrm:hitRadius` |
    /// | C++ Type | VtArray<float> |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->FloatArray |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDVRM_API
    UsdAttribute GetVrmHitRadiusAttr() const;

    /// See GetVrmHitRadiusAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateVrmHitRadiusAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // VRMGRAVITYDIR 
    // --------------------------------------------------------------------- //
    /// Per-joint gravity direction (model space), parallel to vrm:joints.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform float3[] vrm:gravityDir` |
    /// | C++ Type | VtArray<GfVec3f> |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Float3Array |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDVRM_API
    UsdAttribute GetVrmGravityDirAttr() const;

    /// See GetVrmGravityDirAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateVrmGravityDirAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // VRMCENTER 
    // --------------------------------------------------------------------- //
    /// Optional center joint token for inertial reference.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform token vrm:center` |
    /// | C++ Type | TfToken |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Token |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDVRM_API
    UsdAttribute GetVrmCenterAttr() const;

    /// See GetVrmCenterAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateVrmCenterAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // VRMCOLLIDERGROUPS 
    // --------------------------------------------------------------------- //
    /// The collider-group prims this chain collides against.
    ///
    USDVRM_API
    UsdRelationship GetVrmColliderGroupsRel() const;

    /// See GetVrmColliderGroupsRel(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create
    USDVRM_API
    UsdRelationship CreateVrmColliderGroupsRel() const;

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
