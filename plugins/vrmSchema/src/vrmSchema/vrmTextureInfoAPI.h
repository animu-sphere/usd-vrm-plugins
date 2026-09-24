//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef USDVRM_GENERATED_VRMTEXTUREINFOAPI_H
#define USDVRM_GENERATED_VRMTEXTUREINFOAPI_H

/// \file usdVrm/vrmTextureInfoAPI.h

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
// VRMTEXTUREINFOAPI                                                          //
// -------------------------------------------------------------------------- //

/// \class UsdVrmTextureInfoAPI
///
/// One texture a material samples, by role. Apply once per role to
/// the /Asset/mtl/<name> UsdShadeMaterial; the instance name is the role,
/// spelled as the source specification spells the texture without its
/// 'Texture' suffix: the five glTF core textures and the six
/// VRMC_materials_mtoon textures. Only these eleven instance names are
/// allowed. Whether a texture is colour (sRGB) or data is fixed by its role
/// and is not stored. KHR_texture_transform lives under `transform:`, apart
/// from the texture's own contribution scalars (`scale`, `strength`): a UV
/// scale and a contribution scale are different quantities.
///
/// For any described attribute \em Fallback \em Value or \em Allowed \em Values below
/// that are text/tokens, the actual token is published and defined in \ref UsdVrmTokens.
/// So to set an attribute to the value "rightHanded", use UsdVrmTokens->rightHanded
/// as the value.
///
class UsdVrmTextureInfoAPI : public UsdAPISchemaBase
{
  public:
    /// Compile time constant representing what kind of schema this class is.
    ///
    /// \sa UsdSchemaKind
    static const UsdSchemaKind schemaKind = UsdSchemaKind::MultipleApplyAPI;

    /// Construct a UsdVrmTextureInfoAPI on UsdPrim \p prim with
    /// name \p name . Equivalent to
    /// UsdVrmTextureInfoAPI::Get(
    ///    prim.GetStage(),
    ///    prim.GetPath().AppendProperty(
    ///        "inputs:vrm:textureInfo:name"));
    ///
    /// for a \em valid \p prim, but will not immediately throw an error for
    /// an invalid \p prim
    explicit UsdVrmTextureInfoAPI(const UsdPrim& prim = UsdPrim(), const TfToken& name = TfToken())
        : UsdAPISchemaBase(prim, /*instanceName*/ name)
    {
    }

    /// Construct a UsdVrmTextureInfoAPI on the prim held by \p schemaObj with
    /// name \p name.  Should be preferred over
    /// UsdVrmTextureInfoAPI(schemaObj.GetPrim(), name), as it preserves
    /// SchemaBase state.
    explicit UsdVrmTextureInfoAPI(const UsdSchemaBase& schemaObj, const TfToken& name)
        : UsdAPISchemaBase(schemaObj, /*instanceName*/ name)
    {
    }

    /// Destructor.
    USDVRM_API
    virtual ~UsdVrmTextureInfoAPI();

    /// Return a vector of names of all pre-declared attributes for this schema
    /// class and all its ancestor classes.  Does not include attributes that
    /// may be authored by custom/extended methods of the schemas involved.
    USDVRM_API
    static const TfTokenVector& GetSchemaAttributeNames(bool includeInherited = true);

    /// Return a vector of names of all pre-declared attributes for this schema
    /// class and all its ancestor classes for a given instance name.  Does not
    /// include attributes that may be authored by custom/extended methods of
    /// the schemas involved. The names returned will have the proper namespace
    /// prefix.
    USDVRM_API
    static TfTokenVector GetSchemaAttributeNames(bool includeInherited,
                                                 const TfToken& instanceName);

    /// Returns the name of this multiple-apply schema instance
    TfToken
    GetName() const
    {
        return _GetInstanceName();
    }

    /// Return a UsdVrmTextureInfoAPI holding the prim adhering to this
    /// schema at \p path on \p stage.  If no prim exists at \p path on
    /// \p stage, or if the prim at that path does not adhere to this schema,
    /// return an invalid schema object.  \p path must be of the format
    /// <path>.inputs:vrm:textureInfo:name .
    ///
    /// This is shorthand for the following:
    ///
    /// \code
    /// TfToken name = SdfPath::StripNamespace(path.GetToken());
    /// UsdVrmTextureInfoAPI(
    ///     stage->GetPrimAtPath(path.GetPrimPath()), name);
    /// \endcode
    ///
    USDVRM_API
    static UsdVrmTextureInfoAPI Get(const UsdStagePtr& stage, const SdfPath& path);

    /// Return a UsdVrmTextureInfoAPI with name \p name holding the
    /// prim \p prim. Shorthand for UsdVrmTextureInfoAPI(prim, name);
    USDVRM_API
    static UsdVrmTextureInfoAPI Get(const UsdPrim& prim, const TfToken& name);

    /// Return a vector of all named instances of UsdVrmTextureInfoAPI on the
    /// given \p prim.
    USDVRM_API
    static std::vector<UsdVrmTextureInfoAPI> GetAll(const UsdPrim& prim);

    /// Checks if the given name \p baseName is the base name of a property
    /// of VrmTextureInfoAPI.
    USDVRM_API
    static bool IsSchemaPropertyBaseName(const TfToken& baseName);

    /// Checks if the given path \p path is of an API schema of type
    /// VrmTextureInfoAPI. If so, it stores the instance name of
    /// the schema in \p name and returns true. Otherwise, it returns false.
    USDVRM_API
    static bool IsVrmTextureInfoAPIPath(const SdfPath& path, TfToken* name);

    /// Returns true if this <b>multiple-apply</b> API schema can be applied,
    /// with the given instance name, \p name, to the given \p prim. If this
    /// schema can not be a applied the prim, this returns false and, if
    /// provided, populates \p whyNot with the reason it can not be applied.
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
    static bool CanApply(const UsdPrim& prim, const TfToken& name, std::string* whyNot = nullptr);

    /// Applies this <b>multiple-apply</b> API schema to the given \p prim
    /// along with the given instance name, \p name.
    ///
    /// This information is stored by adding "VrmTextureInfoAPI:<i>name</i>"
    /// to the token-valued, listOp metadata \em apiSchemas on the prim.
    /// For example, if \p name is 'instance1', the token
    /// 'VrmTextureInfoAPI:instance1' is added to 'apiSchemas'.
    ///
    /// \return A valid UsdVrmTextureInfoAPI object is returned upon success.
    /// An invalid (or empty) UsdVrmTextureInfoAPI object is returned upon
    /// failure. See \ref UsdPrim::ApplyAPI() for
    /// conditions resulting in failure.
    ///
    /// \sa UsdPrim::GetAppliedSchemas()
    /// \sa UsdPrim::HasAPI()
    /// \sa UsdPrim::CanApplyAPI()
    /// \sa UsdPrim::ApplyAPI()
    /// \sa UsdPrim::RemoveAPI()
    ///
    USDVRM_API
    static UsdVrmTextureInfoAPI Apply(const UsdPrim& prim, const TfToken& name);

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
    // FILE
    // --------------------------------------------------------------------- //
    /// The image, as a USD asset path (an external file or a path
    /// inside the .vrm package).
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `asset file` |
    /// | C++ Type | SdfAssetPath |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Asset |
    USDVRM_API
    UsdAttribute GetFileAttr() const;

    /// See GetFileAttr(), and also
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateFileAttr(VtValue const& defaultValue = VtValue(),
                                bool writeSparsely = false) const;

  public:
    // --------------------------------------------------------------------- //
    // TEXCOORD
    // --------------------------------------------------------------------- //
    /// The TEXCOORD_<n> set sampled. The effective set: a
    /// KHR_texture_transform texCoord override is folded in by the importer.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform int texCoord = 0` |
    /// | C++ Type | int |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Int |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDVRM_API
    UsdAttribute GetTexCoordAttr() const;

    /// See GetTexCoordAttr(), and also
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateTexCoordAttr(VtValue const& defaultValue = VtValue(),
                                    bool writeSparsely = false) const;

  public:
    // --------------------------------------------------------------------- //
    // WRAPS
    // --------------------------------------------------------------------- //
    /// glTF sampler wrapS: 'repeat', 'clampToEdge' or 'mirroredRepeat'.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform token wrapS = "repeat"` |
    /// | C++ Type | TfToken |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Token |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDVRM_API
    UsdAttribute GetWrapSAttr() const;

    /// See GetWrapSAttr(), and also
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateWrapSAttr(VtValue const& defaultValue = VtValue(),
                                 bool writeSparsely = false) const;

  public:
    // --------------------------------------------------------------------- //
    // WRAPT
    // --------------------------------------------------------------------- //
    /// glTF sampler wrapT: 'repeat', 'clampToEdge' or 'mirroredRepeat'.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform token wrapT = "repeat"` |
    /// | C++ Type | TfToken |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Token |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDVRM_API
    UsdAttribute GetWrapTAttr() const;

    /// See GetWrapTAttr(), and also
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateWrapTAttr(VtValue const& defaultValue = VtValue(),
                                 bool writeSparsely = false) const;

  public:
    // --------------------------------------------------------------------- //
    // SCALE
    // --------------------------------------------------------------------- //
    /// Contribution scale: glTF normalTexture.scale, or
    /// VRMC_materials_mtoon shadingShiftTexture.scale. Not a UV scale, and
    /// meaningless on any other role.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `float scale = 1` |
    /// | C++ Type | float |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Float |
    USDVRM_API
    UsdAttribute GetScaleAttr() const;

    /// See GetScaleAttr(), and also
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateScaleAttr(VtValue const& defaultValue = VtValue(),
                                 bool writeSparsely = false) const;

  public:
    // --------------------------------------------------------------------- //
    // STRENGTH
    // --------------------------------------------------------------------- //
    /// glTF occlusionTexture.strength. Meaningless on any other role.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `float strength = 1` |
    /// | C++ Type | float |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Float |
    USDVRM_API
    UsdAttribute GetStrengthAttr() const;

    /// See GetStrengthAttr(), and also
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateStrengthAttr(VtValue const& defaultValue = VtValue(),
                                    bool writeSparsely = false) const;

  public:
    // --------------------------------------------------------------------- //
    // TRANSFORMOFFSET
    // --------------------------------------------------------------------- //
    /// KHR_texture_transform offset.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `float2 transform:offset = (0, 0)` |
    /// | C++ Type | GfVec2f |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Float2 |
    USDVRM_API
    UsdAttribute GetTransformOffsetAttr() const;

    /// See GetTransformOffsetAttr(), and also
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateTransformOffsetAttr(VtValue const& defaultValue = VtValue(),
                                           bool writeSparsely = false) const;

  public:
    // --------------------------------------------------------------------- //
    // TRANSFORMROTATION
    // --------------------------------------------------------------------- //
    /// KHR_texture_transform rotation, in radians, as glTF defines it.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `float transform:rotation = 0` |
    /// | C++ Type | float |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Float |
    USDVRM_API
    UsdAttribute GetTransformRotationAttr() const;

    /// See GetTransformRotationAttr(), and also
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateTransformRotationAttr(VtValue const& defaultValue = VtValue(),
                                             bool writeSparsely = false) const;

  public:
    // --------------------------------------------------------------------- //
    // TRANSFORMSCALE
    // --------------------------------------------------------------------- //
    /// KHR_texture_transform scale: a UV scale, not the contribution scale above.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `float2 transform:scale = (1, 1)` |
    /// | C++ Type | GfVec2f |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Float2 |
    USDVRM_API
    UsdAttribute GetTransformScaleAttr() const;

    /// See GetTransformScaleAttr(), and also
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDVRM_API
    UsdAttribute CreateTransformScaleAttr(VtValue const& defaultValue = VtValue(),
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
