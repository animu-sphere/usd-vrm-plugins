//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef USDVRM_GENERATED_VRMCOLLIDERAPI_H
#define USDVRM_GENERATED_VRMCOLLIDERAPI_H

/// \file usdVrm/vrmColliderAPI.h

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
// VRMCOLLIDERAPI                                                             //
// -------------------------------------------------------------------------- //

/// \class UsdVrmColliderAPI
///
/// A single VRM SpringBone collider (data only). Apply to each
/// /Asset/rig/SecondaryMotion/Colliders/<group>/Collider_<n> prim. vrm:tail is only
/// meaningful for capsule shapes.
///
/// For any described attribute \em Fallback \em Value or \em Allowed \em Values below
/// that are text/tokens, the actual token is published and defined in \ref UsdVrmTokens.
/// So to set an attribute to the value "rightHanded", use UsdVrmTokens->rightHanded
/// as the value.
///
class UsdVrmColliderAPI : public UsdAPISchemaBase
{
public:
    /// Compile time constant representing what kind of schema this class is.
    ///
    /// \sa UsdSchemaKind
    static const UsdSchemaKind schemaKind = UsdSchemaKind::SingleApplyAPI;

    /// Construct a UsdVrmColliderAPI on UsdPrim \p prim .
    /// Equivalent to UsdVrmColliderAPI::Get(prim.GetStage(), prim.GetPath())
    /// for a \em valid \p prim, but will not immediately throw an error for
    /// an invalid \p prim
    explicit UsdVrmColliderAPI(const UsdPrim& prim=UsdPrim())
        : UsdAPISchemaBase(prim)
    {
    }

    /// Construct a UsdVrmColliderAPI on the prim held by \p schemaObj .
    /// Should be preferred over UsdVrmColliderAPI(schemaObj.GetPrim()),
    /// as it preserves SchemaBase state.
    explicit UsdVrmColliderAPI(const UsdSchemaBase& schemaObj)
        : UsdAPISchemaBase(schemaObj)
    {
    }

    /// Destructor.
    USDVRM_API
    virtual ~UsdVrmColliderAPI();

    /// Return a vector of names of all pre-declared attributes for this schema
    /// class and all its ancestor classes.  Does not include attributes that
    /// may be authored by custom/extended methods of the schemas involved.
    USDVRM_API
    static const TfTokenVector &
    GetSchemaAttributeNames(bool includeInherited=true);

    /// Return a UsdVrmColliderAPI holding the prim adhering to this
    /// schema at \p path on \p stage.  If no prim exists at \p path on
    /// \p stage, or if the prim at that path does not adhere to this schema,
    /// return an invalid schema object.  This is shorthand for the following:
    ///
    /// \code
    /// UsdVrmColliderAPI(stage->GetPrimAtPath(path));
    /// \endcode
    ///
    USDVRM_API
    static UsdVrmColliderAPI
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
    /// This information is stored by adding "VrmColliderAPI" to the 
    /// token-valued, listOp metadata \em apiSchemas on the prim.
    /// 
    /// \return A valid UsdVrmColliderAPI object is returned upon success. 
    /// An invalid (or empty) UsdVrmColliderAPI object is returned upon 
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
    static UsdVrmColliderAPI 
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
    // VRMSHAPE 
    // --------------------------------------------------------------------- //
    /// 'sphere' or 'capsule'.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform token vrm:shape` |
    /// | C++ Type | TfToken |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Token |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDVRM_API
    UsdAttribute GetVrmShapeAttr() const;

    /// See GetVrmShapeAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateVrmShapeAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // VRMNODE 
    // --------------------------------------------------------------------- //
    /// Joint path token the collider is attached to.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform token vrm:node` |
    /// | C++ Type | TfToken |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Token |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDVRM_API
    UsdAttribute GetVrmNodeAttr() const;

    /// See GetVrmNodeAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateVrmNodeAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // VRMOFFSET 
    // --------------------------------------------------------------------- //
    /// Collider center offset from its node.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform float3 vrm:offset` |
    /// | C++ Type | GfVec3f |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Float3 |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDVRM_API
    UsdAttribute GetVrmOffsetAttr() const;

    /// See GetVrmOffsetAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateVrmOffsetAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // VRMRADIUS 
    // --------------------------------------------------------------------- //
    /// Collider radius.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform float vrm:radius` |
    /// | C++ Type | float |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Float |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDVRM_API
    UsdAttribute GetVrmRadiusAttr() const;

    /// See GetVrmRadiusAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateVrmRadiusAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // VRMTAIL 
    // --------------------------------------------------------------------- //
    /// Capsule tail position (capsule shape only).
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform float3 vrm:tail` |
    /// | C++ Type | GfVec3f |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Float3 |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDVRM_API
    UsdAttribute GetVrmTailAttr() const;

    /// See GetVrmTailAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateVrmTailAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

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
