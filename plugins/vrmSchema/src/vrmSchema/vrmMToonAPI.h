//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef USDVRM_GENERATED_VRMMTOONAPI_H
#define USDVRM_GENERATED_VRMMTOONAPI_H

/// \file usdVrm/vrmMToonAPI.h

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
// VRMMTOONAPI                                                                //
// -------------------------------------------------------------------------- //

/// \class UsdVrmMToonAPI
///
/// MToon source semantics: the VRMC_materials_mtoon 1.0 model, as
/// data and not as a shader. Apply to the /Asset/mtl/<name> UsdShadeMaterial
/// alongside VrmMaterialAPI, which carries the glTF core the MToon model
/// builds on. Property names are the specification's field names verbatim.
/// A VRM 0.x MToon material (Unity property names) is normalized into the same
/// fields at the importer boundary; this schema has no version switch. Fields
/// that no portable realization reproduces -- outline, render queue,
/// transparent-with-z-write, UV animation -- are canonical all the same: the
/// renderer decides what to do with them. MToon textures are
/// VrmTextureInfoAPI instances on the same prim. The source block stays at
/// customData `vrm:mtoon:raw` as the lossless fallback and is never a runtime
/// API.
///
/// For any described attribute \em Fallback \em Value or \em Allowed \em Values below
/// that are text/tokens, the actual token is published and defined in \ref UsdVrmTokens.
/// So to set an attribute to the value "rightHanded", use UsdVrmTokens->rightHanded
/// as the value.
///
class UsdVrmMToonAPI : public UsdAPISchemaBase
{
  public:
    /// Compile time constant representing what kind of schema this class is.
    ///
    /// \sa UsdSchemaKind
    static const UsdSchemaKind schemaKind = UsdSchemaKind::SingleApplyAPI;

    /// Construct a UsdVrmMToonAPI on UsdPrim \p prim .
    /// Equivalent to UsdVrmMToonAPI::Get(prim.GetStage(), prim.GetPath())
    /// for a \em valid \p prim, but will not immediately throw an error for
    /// an invalid \p prim
    explicit UsdVrmMToonAPI(const UsdPrim& prim = UsdPrim()) : UsdAPISchemaBase(prim)
    {
    }

    /// Construct a UsdVrmMToonAPI on the prim held by \p schemaObj .
    /// Should be preferred over UsdVrmMToonAPI(schemaObj.GetPrim()),
    /// as it preserves SchemaBase state.
    explicit UsdVrmMToonAPI(const UsdSchemaBase& schemaObj) : UsdAPISchemaBase(schemaObj)
    {
    }

    /// Destructor.
    USDVRM_API
    virtual ~UsdVrmMToonAPI();

    /// Return a vector of names of all pre-declared attributes for this schema
    /// class and all its ancestor classes.  Does not include attributes that
    /// may be authored by custom/extended methods of the schemas involved.
    USDVRM_API
    static const TfTokenVector& GetSchemaAttributeNames(bool includeInherited = true);

    /// Return a UsdVrmMToonAPI holding the prim adhering to this
    /// schema at \p path on \p stage.  If no prim exists at \p path on
    /// \p stage, or if the prim at that path does not adhere to this schema,
    /// return an invalid schema object.  This is shorthand for the following:
    ///
    /// \code
    /// UsdVrmMToonAPI(stage->GetPrimAtPath(path));
    /// \endcode
    ///
    USDVRM_API
    static UsdVrmMToonAPI Get(const UsdStagePtr& stage, const SdfPath& path);

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
    /// This information is stored by adding "VrmMToonAPI" to the
    /// token-valued, listOp metadata \em apiSchemas on the prim.
    ///
    /// \return A valid UsdVrmMToonAPI object is returned upon success.
    /// An invalid (or empty) UsdVrmMToonAPI object is returned upon
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
    static UsdVrmMToonAPI Apply(const UsdPrim& prim);

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
    // SPECVERSION
    // --------------------------------------------------------------------- //
    /// The VRMC_materials_mtoon version whose model these values
    /// follow. A VRM 0.x source is normalized into the 1.0 model and carries
    /// that model's version, not its own; the raw block keeps the original.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform token inputs:vrm:mtoon:specVersion = "1.0"` |
    /// | C++ Type | TfToken |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Token |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDVRM_API
    UsdAttribute GetSpecVersionAttr() const;

    /// See GetSpecVersionAttr(), and also
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateSpecVersionAttr(VtValue const& defaultValue = VtValue(),
                                       bool writeSparsely = false) const;

  public:
    // --------------------------------------------------------------------- //
    // TRANSPARENTWITHZWRITE
    // --------------------------------------------------------------------- //
    /// VRMC_materials_mtoon transparentWithZWrite.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform bool inputs:vrm:mtoon:transparentWithZWrite = 0` |
    /// | C++ Type | bool |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Bool |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDVRM_API
    UsdAttribute GetTransparentWithZWriteAttr() const;

    /// See GetTransparentWithZWriteAttr(), and also
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateTransparentWithZWriteAttr(VtValue const& defaultValue = VtValue(),
                                                 bool writeSparsely = false) const;

  public:
    // --------------------------------------------------------------------- //
    // RENDERQUEUEOFFSETNUMBER
    // --------------------------------------------------------------------- //
    /// VRMC_materials_mtoon renderQueueOffsetNumber.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform int inputs:vrm:mtoon:renderQueueOffsetNumber = 0` |
    /// | C++ Type | int |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Int |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDVRM_API
    UsdAttribute GetRenderQueueOffsetNumberAttr() const;

    /// See GetRenderQueueOffsetNumberAttr(), and also
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateRenderQueueOffsetNumberAttr(VtValue const& defaultValue = VtValue(),
                                                   bool writeSparsely = false) const;

  public:
    // --------------------------------------------------------------------- //
    // SHADECOLORFACTOR
    // --------------------------------------------------------------------- //
    /// VRMC_materials_mtoon shadeColorFactor (linear).
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `color3f inputs:vrm:mtoon:shadeColorFactor = (1, 1, 1)` |
    /// | C++ Type | GfVec3f |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Color3f |
    USDVRM_API
    UsdAttribute GetShadeColorFactorAttr() const;

    /// See GetShadeColorFactorAttr(), and also
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateShadeColorFactorAttr(VtValue const& defaultValue = VtValue(),
                                            bool writeSparsely = false) const;

  public:
    // --------------------------------------------------------------------- //
    // SHADINGSHIFTFACTOR
    // --------------------------------------------------------------------- //
    /// VRMC_materials_mtoon shadingShiftFactor.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `float inputs:vrm:mtoon:shadingShiftFactor = 0` |
    /// | C++ Type | float |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Float |
    USDVRM_API
    UsdAttribute GetShadingShiftFactorAttr() const;

    /// See GetShadingShiftFactorAttr(), and also
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateShadingShiftFactorAttr(VtValue const& defaultValue = VtValue(),
                                              bool writeSparsely = false) const;

  public:
    // --------------------------------------------------------------------- //
    // SHADINGTOONYFACTOR
    // --------------------------------------------------------------------- //
    /// VRMC_materials_mtoon shadingToonyFactor.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `float inputs:vrm:mtoon:shadingToonyFactor = 0.9` |
    /// | C++ Type | float |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Float |
    USDVRM_API
    UsdAttribute GetShadingToonyFactorAttr() const;

    /// See GetShadingToonyFactorAttr(), and also
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateShadingToonyFactorAttr(VtValue const& defaultValue = VtValue(),
                                              bool writeSparsely = false) const;

  public:
    // --------------------------------------------------------------------- //
    // GIEQUALIZATIONFACTOR
    // --------------------------------------------------------------------- //
    /// VRMC_materials_mtoon giEqualizationFactor.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `float inputs:vrm:mtoon:giEqualizationFactor = 0.9` |
    /// | C++ Type | float |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Float |
    USDVRM_API
    UsdAttribute GetGiEqualizationFactorAttr() const;

    /// See GetGiEqualizationFactorAttr(), and also
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateGiEqualizationFactorAttr(VtValue const& defaultValue = VtValue(),
                                                bool writeSparsely = false) const;

  public:
    // --------------------------------------------------------------------- //
    // MATCAPFACTOR
    // --------------------------------------------------------------------- //
    /// VRMC_materials_mtoon matcapFactor (linear).
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `color3f inputs:vrm:mtoon:matcapFactor = (1, 1, 1)` |
    /// | C++ Type | GfVec3f |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Color3f |
    USDVRM_API
    UsdAttribute GetMatcapFactorAttr() const;

    /// See GetMatcapFactorAttr(), and also
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateMatcapFactorAttr(VtValue const& defaultValue = VtValue(),
                                        bool writeSparsely = false) const;

  public:
    // --------------------------------------------------------------------- //
    // PARAMETRICRIMCOLORFACTOR
    // --------------------------------------------------------------------- //
    /// VRMC_materials_mtoon parametricRimColorFactor (linear).
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `color3f inputs:vrm:mtoon:parametricRimColorFactor = (0, 0, 0)` |
    /// | C++ Type | GfVec3f |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Color3f |
    USDVRM_API
    UsdAttribute GetParametricRimColorFactorAttr() const;

    /// See GetParametricRimColorFactorAttr(), and also
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateParametricRimColorFactorAttr(VtValue const& defaultValue = VtValue(),
                                                    bool writeSparsely = false) const;

  public:
    // --------------------------------------------------------------------- //
    // PARAMETRICRIMFRESNELPOWERFACTOR
    // --------------------------------------------------------------------- //
    /// VRMC_materials_mtoon parametricRimFresnelPowerFactor.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `float inputs:vrm:mtoon:parametricRimFresnelPowerFactor = 5` |
    /// | C++ Type | float |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Float |
    USDVRM_API
    UsdAttribute GetParametricRimFresnelPowerFactorAttr() const;

    /// See GetParametricRimFresnelPowerFactorAttr(), and also
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateParametricRimFresnelPowerFactorAttr(VtValue const& defaultValue = VtValue(),
                                                           bool writeSparsely = false) const;

  public:
    // --------------------------------------------------------------------- //
    // PARAMETRICRIMLIFTFACTOR
    // --------------------------------------------------------------------- //
    /// VRMC_materials_mtoon parametricRimLiftFactor.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `float inputs:vrm:mtoon:parametricRimLiftFactor = 0` |
    /// | C++ Type | float |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Float |
    USDVRM_API
    UsdAttribute GetParametricRimLiftFactorAttr() const;

    /// See GetParametricRimLiftFactorAttr(), and also
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateParametricRimLiftFactorAttr(VtValue const& defaultValue = VtValue(),
                                                   bool writeSparsely = false) const;

  public:
    // --------------------------------------------------------------------- //
    // RIMLIGHTINGMIXFACTOR
    // --------------------------------------------------------------------- //
    /// VRMC_materials_mtoon rimLightingMixFactor.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `float inputs:vrm:mtoon:rimLightingMixFactor = 1` |
    /// | C++ Type | float |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Float |
    USDVRM_API
    UsdAttribute GetRimLightingMixFactorAttr() const;

    /// See GetRimLightingMixFactorAttr(), and also
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateRimLightingMixFactorAttr(VtValue const& defaultValue = VtValue(),
                                                bool writeSparsely = false) const;

  public:
    // --------------------------------------------------------------------- //
    // OUTLINEWIDTHMODE
    // --------------------------------------------------------------------- //
    /// VRMC_materials_mtoon outlineWidthMode: 'none',
    /// 'worldCoordinates' or 'screenCoordinates'. An MToon semantic, not a
    /// rendering instruction: how an outline is drawn is the renderer's
    /// decision.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform token inputs:vrm:mtoon:outlineWidthMode = "none"` |
    /// | C++ Type | TfToken |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Token |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDVRM_API
    UsdAttribute GetOutlineWidthModeAttr() const;

    /// See GetOutlineWidthModeAttr(), and also
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateOutlineWidthModeAttr(VtValue const& defaultValue = VtValue(),
                                            bool writeSparsely = false) const;

  public:
    // --------------------------------------------------------------------- //
    // OUTLINEWIDTHFACTOR
    // --------------------------------------------------------------------- //
    /// VRMC_materials_mtoon outlineWidthFactor.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `float inputs:vrm:mtoon:outlineWidthFactor = 0` |
    /// | C++ Type | float |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Float |
    USDVRM_API
    UsdAttribute GetOutlineWidthFactorAttr() const;

    /// See GetOutlineWidthFactorAttr(), and also
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateOutlineWidthFactorAttr(VtValue const& defaultValue = VtValue(),
                                              bool writeSparsely = false) const;

  public:
    // --------------------------------------------------------------------- //
    // OUTLINECOLORFACTOR
    // --------------------------------------------------------------------- //
    /// VRMC_materials_mtoon outlineColorFactor (linear).
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `color3f inputs:vrm:mtoon:outlineColorFactor = (0, 0, 0)` |
    /// | C++ Type | GfVec3f |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Color3f |
    USDVRM_API
    UsdAttribute GetOutlineColorFactorAttr() const;

    /// See GetOutlineColorFactorAttr(), and also
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateOutlineColorFactorAttr(VtValue const& defaultValue = VtValue(),
                                              bool writeSparsely = false) const;

  public:
    // --------------------------------------------------------------------- //
    // OUTLINELIGHTINGMIXFACTOR
    // --------------------------------------------------------------------- //
    /// VRMC_materials_mtoon outlineLightingMixFactor.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `float inputs:vrm:mtoon:outlineLightingMixFactor = 1` |
    /// | C++ Type | float |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Float |
    USDVRM_API
    UsdAttribute GetOutlineLightingMixFactorAttr() const;

    /// See GetOutlineLightingMixFactorAttr(), and also
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateOutlineLightingMixFactorAttr(VtValue const& defaultValue = VtValue(),
                                                    bool writeSparsely = false) const;

  public:
    // --------------------------------------------------------------------- //
    // UVANIMATIONSCROLLXSPEEDFACTOR
    // --------------------------------------------------------------------- //
    /// VRMC_materials_mtoon uvAnimationScrollXSpeedFactor.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `float inputs:vrm:mtoon:uvAnimationScrollXSpeedFactor = 0` |
    /// | C++ Type | float |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Float |
    USDVRM_API
    UsdAttribute GetUvAnimationScrollXSpeedFactorAttr() const;

    /// See GetUvAnimationScrollXSpeedFactorAttr(), and also
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateUvAnimationScrollXSpeedFactorAttr(VtValue const& defaultValue = VtValue(),
                                                         bool writeSparsely = false) const;

  public:
    // --------------------------------------------------------------------- //
    // UVANIMATIONSCROLLYSPEEDFACTOR
    // --------------------------------------------------------------------- //
    /// VRMC_materials_mtoon uvAnimationScrollYSpeedFactor.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `float inputs:vrm:mtoon:uvAnimationScrollYSpeedFactor = 0` |
    /// | C++ Type | float |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Float |
    USDVRM_API
    UsdAttribute GetUvAnimationScrollYSpeedFactorAttr() const;

    /// See GetUvAnimationScrollYSpeedFactorAttr(), and also
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateUvAnimationScrollYSpeedFactorAttr(VtValue const& defaultValue = VtValue(),
                                                         bool writeSparsely = false) const;

  public:
    // --------------------------------------------------------------------- //
    // UVANIMATIONROTATIONSPEEDFACTOR
    // --------------------------------------------------------------------- //
    /// VRMC_materials_mtoon uvAnimationRotationSpeedFactor.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `float inputs:vrm:mtoon:uvAnimationRotationSpeedFactor = 0` |
    /// | C++ Type | float |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Float |
    USDVRM_API
    UsdAttribute GetUvAnimationRotationSpeedFactorAttr() const;

    /// See GetUvAnimationRotationSpeedFactorAttr(), and also
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateUvAnimationRotationSpeedFactorAttr(VtValue const& defaultValue = VtValue(),
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
