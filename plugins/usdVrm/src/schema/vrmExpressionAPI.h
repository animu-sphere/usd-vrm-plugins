//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef USDVRM_GENERATED_VRMEXPRESSIONAPI_H
#define USDVRM_GENERATED_VRMEXPRESSIONAPI_H

/// \file usdVrm/vrmExpressionAPI.h

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
// VRMEXPRESSIONAPI                                                           //
// -------------------------------------------------------------------------- //

/// \class UsdVrmExpressionAPI
///
/// A VRM expression (VRM 1.0 Expression / VRM 0.x BlendShapeGroup). Apply
/// to each /Asset/rig/Expressions/<name> prim. Morph-target and material-color
/// bindings are authored as relationships with parallel value arrays; evaluation is
/// a downstream-runtime concern. Per-expression curves and unmapped data stay in
/// customData.
///
/// For any described attribute \em Fallback \em Value or \em Allowed \em Values below
/// that are text/tokens, the actual token is published and defined in \ref UsdVrmTokens.
/// So to set an attribute to the value "rightHanded", use UsdVrmTokens->rightHanded
/// as the value.
///
class UsdVrmExpressionAPI : public UsdAPISchemaBase
{
public:
    /// Compile time constant representing what kind of schema this class is.
    ///
    /// \sa UsdSchemaKind
    static const UsdSchemaKind schemaKind = UsdSchemaKind::SingleApplyAPI;

    /// Construct a UsdVrmExpressionAPI on UsdPrim \p prim .
    /// Equivalent to UsdVrmExpressionAPI::Get(prim.GetStage(), prim.GetPath())
    /// for a \em valid \p prim, but will not immediately throw an error for
    /// an invalid \p prim
    explicit UsdVrmExpressionAPI(const UsdPrim& prim=UsdPrim())
        : UsdAPISchemaBase(prim)
    {
    }

    /// Construct a UsdVrmExpressionAPI on the prim held by \p schemaObj .
    /// Should be preferred over UsdVrmExpressionAPI(schemaObj.GetPrim()),
    /// as it preserves SchemaBase state.
    explicit UsdVrmExpressionAPI(const UsdSchemaBase& schemaObj)
        : UsdAPISchemaBase(schemaObj)
    {
    }

    /// Destructor.
    USDVRM_API
    virtual ~UsdVrmExpressionAPI();

    /// Return a vector of names of all pre-declared attributes for this schema
    /// class and all its ancestor classes.  Does not include attributes that
    /// may be authored by custom/extended methods of the schemas involved.
    USDVRM_API
    static const TfTokenVector &
    GetSchemaAttributeNames(bool includeInherited=true);

    /// Return a UsdVrmExpressionAPI holding the prim adhering to this
    /// schema at \p path on \p stage.  If no prim exists at \p path on
    /// \p stage, or if the prim at that path does not adhere to this schema,
    /// return an invalid schema object.  This is shorthand for the following:
    ///
    /// \code
    /// UsdVrmExpressionAPI(stage->GetPrimAtPath(path));
    /// \endcode
    ///
    USDVRM_API
    static UsdVrmExpressionAPI
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
    /// This information is stored by adding "VrmExpressionAPI" to the 
    /// token-valued, listOp metadata \em apiSchemas on the prim.
    /// 
    /// \return A valid UsdVrmExpressionAPI object is returned upon success. 
    /// An invalid (or empty) UsdVrmExpressionAPI object is returned upon 
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
    static UsdVrmExpressionAPI 
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
    // VRMEXPRESSIONTYPE 
    // --------------------------------------------------------------------- //
    /// 'preset' for a standard VRM expression, 'custom' otherwise.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform token vrm:expressionType` |
    /// | C++ Type | TfToken |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Token |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDVRM_API
    UsdAttribute GetVrmExpressionTypeAttr() const;

    /// See GetVrmExpressionTypeAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateVrmExpressionTypeAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // VRMISBINARY 
    // --------------------------------------------------------------------- //
    /// Whether the expression value is clamped to 0/1 (binary).
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform bool vrm:isBinary` |
    /// | C++ Type | bool |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Bool |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDVRM_API
    UsdAttribute GetVrmIsBinaryAttr() const;

    /// See GetVrmIsBinaryAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateVrmIsBinaryAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // VRMMORPHTARGETWEIGHTS 
    // --------------------------------------------------------------------- //
    /// Per-target weights, parallel to vrm:morphTargets.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform float[] vrm:morphTargetWeights` |
    /// | C++ Type | VtArray<float> |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->FloatArray |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDVRM_API
    UsdAttribute GetVrmMorphTargetWeightsAttr() const;

    /// See GetVrmMorphTargetWeightsAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateVrmMorphTargetWeightsAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // VRMMATERIALCOLORTYPES 
    // --------------------------------------------------------------------- //
    /// Color slot per target (e.g. color, emission), parallel to vrm:materialColorTargets.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform token[] vrm:materialColorTypes` |
    /// | C++ Type | VtArray<TfToken> |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->TokenArray |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDVRM_API
    UsdAttribute GetVrmMaterialColorTypesAttr() const;

    /// See GetVrmMaterialColorTypesAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateVrmMaterialColorTypesAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // VRMMATERIALCOLORVALUES 
    // --------------------------------------------------------------------- //
    /// Target RGBA per material color bind, parallel to vrm:materialColorTargets.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform float4[] vrm:materialColorValues` |
    /// | C++ Type | VtArray<GfVec4f> |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Float4Array |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDVRM_API
    UsdAttribute GetVrmMaterialColorValuesAttr() const;

    /// See GetVrmMaterialColorValuesAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateVrmMaterialColorValuesAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // VRMMORPHTARGETS 
    // --------------------------------------------------------------------- //
    /// The UsdSkelBlendShape prims this expression drives.
    ///
    USDVRM_API
    UsdRelationship GetVrmMorphTargetsRel() const;

    /// See GetVrmMorphTargetsRel(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create
    USDVRM_API
    UsdRelationship CreateVrmMorphTargetsRel() const;

public:
    // --------------------------------------------------------------------- //
    // VRMMATERIALCOLORTARGETS 
    // --------------------------------------------------------------------- //
    /// The materials whose colors this expression overrides.
    ///
    USDVRM_API
    UsdRelationship GetVrmMaterialColorTargetsRel() const;

    /// See GetVrmMaterialColorTargetsRel(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create
    USDVRM_API
    UsdRelationship CreateVrmMaterialColorTargetsRel() const;

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
