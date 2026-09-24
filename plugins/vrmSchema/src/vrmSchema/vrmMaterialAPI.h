//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef USDVRM_GENERATED_VRMMATERIALAPI_H
#define USDVRM_GENERATED_VRMMATERIALAPI_H

/// \file usdVrm/vrmMaterialAPI.h

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
// VRMMATERIALAPI                                                             //
// -------------------------------------------------------------------------- //

/// \class UsdVrmMaterialAPI
///
/// Generic source material semantics of one glTF / VRM material: the
/// glTF 2.0 metallic-roughness core plus the two material extensions VRM
/// avatars rely on (KHR_materials_unlit, KHR_materials_emissive_strength).
/// Apply to the /Asset/mtl/<name> UsdShadeMaterial. Texture-bound values
/// (normal scale, occlusion strength, every texture's asset and UV state) are
/// VrmTextureInfoAPI instances on the same prim. Colours are linear, as glTF
/// defines them. UsdPreviewSurface and MaterialX inputs are realizations of
/// these values and are never storage.
///
/// For any described attribute \em Fallback \em Value or \em Allowed \em Values below
/// that are text/tokens, the actual token is published and defined in \ref UsdVrmTokens.
/// So to set an attribute to the value "rightHanded", use UsdVrmTokens->rightHanded
/// as the value.
///
class UsdVrmMaterialAPI : public UsdAPISchemaBase
{
  public:
    /// Compile time constant representing what kind of schema this class is.
    ///
    /// \sa UsdSchemaKind
    static const UsdSchemaKind schemaKind = UsdSchemaKind::SingleApplyAPI;

    /// Construct a UsdVrmMaterialAPI on UsdPrim \p prim .
    /// Equivalent to UsdVrmMaterialAPI::Get(prim.GetStage(), prim.GetPath())
    /// for a \em valid \p prim, but will not immediately throw an error for
    /// an invalid \p prim
    explicit UsdVrmMaterialAPI(const UsdPrim& prim = UsdPrim()) : UsdAPISchemaBase(prim)
    {
    }

    /// Construct a UsdVrmMaterialAPI on the prim held by \p schemaObj .
    /// Should be preferred over UsdVrmMaterialAPI(schemaObj.GetPrim()),
    /// as it preserves SchemaBase state.
    explicit UsdVrmMaterialAPI(const UsdSchemaBase& schemaObj) : UsdAPISchemaBase(schemaObj)
    {
    }

    /// Destructor.
    USDVRM_API
    virtual ~UsdVrmMaterialAPI();

    /// Return a vector of names of all pre-declared attributes for this schema
    /// class and all its ancestor classes.  Does not include attributes that
    /// may be authored by custom/extended methods of the schemas involved.
    USDVRM_API
    static const TfTokenVector& GetSchemaAttributeNames(bool includeInherited = true);

    /// Return a UsdVrmMaterialAPI holding the prim adhering to this
    /// schema at \p path on \p stage.  If no prim exists at \p path on
    /// \p stage, or if the prim at that path does not adhere to this schema,
    /// return an invalid schema object.  This is shorthand for the following:
    ///
    /// \code
    /// UsdVrmMaterialAPI(stage->GetPrimAtPath(path));
    /// \endcode
    ///
    USDVRM_API
    static UsdVrmMaterialAPI Get(const UsdStagePtr& stage, const SdfPath& path);

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
    static bool CanApply(const UsdPrim& prim, std::string* whyNot = nullptr);

    /// Applies this <b>single-apply</b> API schema to the given \p prim.
    /// This information is stored by adding "VrmMaterialAPI" to the
    /// token-valued, listOp metadata \em apiSchemas on the prim.
    ///
    /// \return A valid UsdVrmMaterialAPI object is returned upon success.
    /// An invalid (or empty) UsdVrmMaterialAPI object is returned upon
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
    static UsdVrmMaterialAPI Apply(const UsdPrim& prim);

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
    static const TfType& _GetStaticTfType();

    static bool _IsTypedSchema();

    // override SchemaBase virtuals.
    USDVRM_API
    const TfType& _GetTfType() const override;

  public:
    // --------------------------------------------------------------------- //
    // BASECOLORFACTOR
    // --------------------------------------------------------------------- //
    /// RGB of glTF pbrMetallicRoughness.baseColorFactor. glTF's
    /// factor is RGBA; it is split so each half connects to a colour and a
    /// float input without a conversion node in every realization.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `color3f inputs:vrm:material:baseColorFactor = (1, 1, 1)` |
    /// | C++ Type | GfVec3f |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Color3f |
    USDVRM_API
    UsdAttribute GetBaseColorFactorAttr() const;

    /// See GetBaseColorFactorAttr(), and also
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateBaseColorFactorAttr(VtValue const& defaultValue = VtValue(),
                                           bool writeSparsely = false) const;

  public:
    // --------------------------------------------------------------------- //
    // BASECOLORALPHAFACTOR
    // --------------------------------------------------------------------- //
    /// Alpha (fourth component) of glTF pbrMetallicRoughness.baseColorFactor.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `float inputs:vrm:material:baseColorAlphaFactor = 1` |
    /// | C++ Type | float |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Float |
    USDVRM_API
    UsdAttribute GetBaseColorAlphaFactorAttr() const;

    /// See GetBaseColorAlphaFactorAttr(), and also
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateBaseColorAlphaFactorAttr(VtValue const& defaultValue = VtValue(),
                                                bool writeSparsely = false) const;

  public:
    // --------------------------------------------------------------------- //
    // METALLICFACTOR
    // --------------------------------------------------------------------- //
    /// glTF pbrMetallicRoughness.metallicFactor.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `float inputs:vrm:material:metallicFactor = 1` |
    /// | C++ Type | float |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Float |
    USDVRM_API
    UsdAttribute GetMetallicFactorAttr() const;

    /// See GetMetallicFactorAttr(), and also
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateMetallicFactorAttr(VtValue const& defaultValue = VtValue(),
                                          bool writeSparsely = false) const;

  public:
    // --------------------------------------------------------------------- //
    // ROUGHNESSFACTOR
    // --------------------------------------------------------------------- //
    /// glTF pbrMetallicRoughness.roughnessFactor.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `float inputs:vrm:material:roughnessFactor = 1` |
    /// | C++ Type | float |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Float |
    USDVRM_API
    UsdAttribute GetRoughnessFactorAttr() const;

    /// See GetRoughnessFactorAttr(), and also
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateRoughnessFactorAttr(VtValue const& defaultValue = VtValue(),
                                           bool writeSparsely = false) const;

  public:
    // --------------------------------------------------------------------- //
    // EMISSIVEFACTOR
    // --------------------------------------------------------------------- //
    /// glTF emissiveFactor.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `color3f inputs:vrm:material:emissiveFactor = (0, 0, 0)` |
    /// | C++ Type | GfVec3f |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Color3f |
    USDVRM_API
    UsdAttribute GetEmissiveFactorAttr() const;

    /// See GetEmissiveFactorAttr(), and also
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateEmissiveFactorAttr(VtValue const& defaultValue = VtValue(),
                                          bool writeSparsely = false) const;

  public:
    // --------------------------------------------------------------------- //
    // EMISSIVESTRENGTH
    // --------------------------------------------------------------------- //
    /// KHR_materials_emissive_strength.emissiveStrength: a multiplier
    /// on emissiveFactor, kept separate so the factor stays the glTF value.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `float inputs:vrm:material:emissiveStrength = 1` |
    /// | C++ Type | float |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Float |
    USDVRM_API
    UsdAttribute GetEmissiveStrengthAttr() const;

    /// See GetEmissiveStrengthAttr(), and also
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateEmissiveStrengthAttr(VtValue const& defaultValue = VtValue(),
                                            bool writeSparsely = false) const;

  public:
    // --------------------------------------------------------------------- //
    // ALPHAMODE
    // --------------------------------------------------------------------- //
    /// glTF alphaMode: 'OPAQUE', 'MASK' or 'BLEND'; unauthored means
    /// 'OPAQUE', as in glTF. The one property without a schema fallback:
    /// usdGenSchema would name the fallback's C++ token `OPAQUE`, which is a
    /// Windows (wingdi.h) macro and breaks every translation unit that sees
    /// both.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform token inputs:vrm:material:alphaMode` |
    /// | C++ Type | TfToken |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Token |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDVRM_API
    UsdAttribute GetAlphaModeAttr() const;

    /// See GetAlphaModeAttr(), and also
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateAlphaModeAttr(VtValue const& defaultValue = VtValue(),
                                     bool writeSparsely = false) const;

  public:
    // --------------------------------------------------------------------- //
    // ALPHACUTOFF
    // --------------------------------------------------------------------- //
    /// glTF alphaCutoff. Meaningful only when alphaMode is 'MASK'.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `float inputs:vrm:material:alphaCutoff = 0.5` |
    /// | C++ Type | float |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Float |
    USDVRM_API
    UsdAttribute GetAlphaCutoffAttr() const;

    /// See GetAlphaCutoffAttr(), and also
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateAlphaCutoffAttr(VtValue const& defaultValue = VtValue(),
                                       bool writeSparsely = false) const;

  public:
    // --------------------------------------------------------------------- //
    // DOUBLESIDED
    // --------------------------------------------------------------------- //
    /// glTF doubleSided.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform bool inputs:vrm:material:doubleSided = 0` |
    /// | C++ Type | bool |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Bool |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDVRM_API
    UsdAttribute GetDoubleSidedAttr() const;

    /// See GetDoubleSidedAttr(), and also
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateDoubleSidedAttr(VtValue const& defaultValue = VtValue(),
                                       bool writeSparsely = false) const;

  public:
    // --------------------------------------------------------------------- //
    // UNLIT
    // --------------------------------------------------------------------- //
    /// Whether the source material declares KHR_materials_unlit. VRM
    /// MToon materials usually do, as their glTF-only fallback.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform bool inputs:vrm:material:unlit = 0` |
    /// | C++ Type | bool |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Bool |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDVRM_API
    UsdAttribute GetUnlitAttr() const;

    /// See GetUnlitAttr(), and also
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateUnlitAttr(VtValue const& defaultValue = VtValue(),
                                 bool writeSparsely = false) const;

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
