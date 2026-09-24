//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "./vrmTextureInfoAPI.h"
#include "pxr/usd/usd/schemaRegistry.h"
#include "pxr/usd/usd/typed.h"

#include "pxr/usd/sdf/types.h"
#include "pxr/usd/sdf/assetPath.h"

PXR_NAMESPACE_OPEN_SCOPE

// Register the schema with the TfType system.
TF_REGISTRY_FUNCTION(TfType)
{
    TfType::Define<UsdVrmTextureInfoAPI, TfType::Bases<UsdAPISchemaBase>>();
}

/* virtual */
UsdVrmTextureInfoAPI::~UsdVrmTextureInfoAPI()
{
}

/* static */
UsdVrmTextureInfoAPI
UsdVrmTextureInfoAPI::Get(const UsdStagePtr& stage, const SdfPath& path)
{
    if (!stage)
    {
        TF_CODING_ERROR("Invalid stage");
        return UsdVrmTextureInfoAPI();
    }
    TfToken name;
    if (!IsVrmTextureInfoAPIPath(path, &name))
    {
        TF_CODING_ERROR("Invalid inputs:vrm:textureInfo path <%s>.", path.GetText());
        return UsdVrmTextureInfoAPI();
    }
    return UsdVrmTextureInfoAPI(stage->GetPrimAtPath(path.GetPrimPath()), name);
}

UsdVrmTextureInfoAPI
UsdVrmTextureInfoAPI::Get(const UsdPrim& prim, const TfToken& name)
{
    return UsdVrmTextureInfoAPI(prim, name);
}

/* static */
std::vector<UsdVrmTextureInfoAPI>
UsdVrmTextureInfoAPI::GetAll(const UsdPrim& prim)
{
    std::vector<UsdVrmTextureInfoAPI> schemas;

    for (const auto& schemaName :
         UsdAPISchemaBase::_GetMultipleApplyInstanceNames(prim, _GetStaticTfType()))
    {
        schemas.emplace_back(prim, schemaName);
    }

    return schemas;
}

/* static */
bool
UsdVrmTextureInfoAPI::IsSchemaPropertyBaseName(const TfToken& baseName)
{
    static TfTokenVector attrsAndRels = {
        UsdSchemaRegistry::GetMultipleApplyNameTemplateBaseName(
            UsdVrmTokens->inputsVrmTextureInfo_MultipleApplyTemplate_File),
        UsdSchemaRegistry::GetMultipleApplyNameTemplateBaseName(
            UsdVrmTokens->inputsVrmTextureInfo_MultipleApplyTemplate_TexCoord),
        UsdSchemaRegistry::GetMultipleApplyNameTemplateBaseName(
            UsdVrmTokens->inputsVrmTextureInfo_MultipleApplyTemplate_WrapS),
        UsdSchemaRegistry::GetMultipleApplyNameTemplateBaseName(
            UsdVrmTokens->inputsVrmTextureInfo_MultipleApplyTemplate_WrapT),
        UsdSchemaRegistry::GetMultipleApplyNameTemplateBaseName(
            UsdVrmTokens->inputsVrmTextureInfo_MultipleApplyTemplate_Scale),
        UsdSchemaRegistry::GetMultipleApplyNameTemplateBaseName(
            UsdVrmTokens->inputsVrmTextureInfo_MultipleApplyTemplate_Strength),
        UsdSchemaRegistry::GetMultipleApplyNameTemplateBaseName(
            UsdVrmTokens->inputsVrmTextureInfo_MultipleApplyTemplate_TransformOffset),
        UsdSchemaRegistry::GetMultipleApplyNameTemplateBaseName(
            UsdVrmTokens->inputsVrmTextureInfo_MultipleApplyTemplate_TransformRotation),
        UsdSchemaRegistry::GetMultipleApplyNameTemplateBaseName(
            UsdVrmTokens->inputsVrmTextureInfo_MultipleApplyTemplate_TransformScale),
    };

    return find(attrsAndRels.begin(), attrsAndRels.end(), baseName) != attrsAndRels.end();
}

/* static */
bool
UsdVrmTextureInfoAPI::IsVrmTextureInfoAPIPath(const SdfPath& path, TfToken* name)
{
    if (!path.IsPropertyPath())
    {
        return false;
    }

    std::string propertyName = path.GetName();
    TfTokenVector tokens = SdfPath::TokenizeIdentifierAsTokens(propertyName);

    // The baseName of the  path can't be one of the
    // schema properties. We should validate this in the creation (or apply)
    // API.
    TfToken baseName = *tokens.rbegin();
    if (IsSchemaPropertyBaseName(baseName))
    {
        return false;
    }

    if (tokens.size() >= 2 && tokens[0] == UsdVrmTokens->inputsVrmTextureInfo)
    {
        *name =
            TfToken(propertyName.substr(UsdVrmTokens->inputsVrmTextureInfo.GetString().size() + 1));
        return true;
    }

    return false;
}

/* virtual */
UsdSchemaKind
UsdVrmTextureInfoAPI::_GetSchemaKind() const
{
    return UsdVrmTextureInfoAPI::schemaKind;
}

/* static */
bool
UsdVrmTextureInfoAPI::CanApply(const UsdPrim& prim, const TfToken& name, std::string* whyNot)
{
    return prim.CanApplyAPI<UsdVrmTextureInfoAPI>(name, whyNot);
}

/* static */
UsdVrmTextureInfoAPI
UsdVrmTextureInfoAPI::Apply(const UsdPrim& prim, const TfToken& name)
{
    if (prim.ApplyAPI<UsdVrmTextureInfoAPI>(name))
    {
        return UsdVrmTextureInfoAPI(prim, name);
    }
    return UsdVrmTextureInfoAPI();
}

/* static */
const TfType&
UsdVrmTextureInfoAPI::_GetStaticTfType()
{
    static TfType tfType = TfType::Find<UsdVrmTextureInfoAPI>();
    return tfType;
}

/* static */
bool
UsdVrmTextureInfoAPI::_IsTypedSchema()
{
    static bool isTyped = _GetStaticTfType().IsA<UsdTyped>();
    return isTyped;
}

/* virtual */
const TfType&
UsdVrmTextureInfoAPI::_GetTfType() const
{
    return _GetStaticTfType();
}

/// Returns the property name prefixed with the correct namespace prefix, which
/// is composed of the the API's propertyNamespacePrefix metadata and the
/// instance name of the API.
static inline TfToken
_GetNamespacedPropertyName(const TfToken instanceName, const TfToken propName)
{
    return UsdSchemaRegistry::MakeMultipleApplyNameInstance(propName, instanceName);
}

UsdAttribute
UsdVrmTextureInfoAPI::GetFileAttr() const
{
    return GetPrim().GetAttribute(_GetNamespacedPropertyName(
        GetName(), UsdVrmTokens->inputsVrmTextureInfo_MultipleApplyTemplate_File));
}

UsdAttribute
UsdVrmTextureInfoAPI::CreateFileAttr(VtValue const& defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(
        _GetNamespacedPropertyName(GetName(),
                                   UsdVrmTokens->inputsVrmTextureInfo_MultipleApplyTemplate_File),
        SdfValueTypeNames->Asset,
        /* custom = */ false, SdfVariabilityVarying, defaultValue, writeSparsely);
}

UsdAttribute
UsdVrmTextureInfoAPI::GetTexCoordAttr() const
{
    return GetPrim().GetAttribute(_GetNamespacedPropertyName(
        GetName(), UsdVrmTokens->inputsVrmTextureInfo_MultipleApplyTemplate_TexCoord));
}

UsdAttribute
UsdVrmTextureInfoAPI::CreateTexCoordAttr(VtValue const& defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(
        _GetNamespacedPropertyName(
            GetName(), UsdVrmTokens->inputsVrmTextureInfo_MultipleApplyTemplate_TexCoord),
        SdfValueTypeNames->Int,
        /* custom = */ false, SdfVariabilityUniform, defaultValue, writeSparsely);
}

UsdAttribute
UsdVrmTextureInfoAPI::GetWrapSAttr() const
{
    return GetPrim().GetAttribute(_GetNamespacedPropertyName(
        GetName(), UsdVrmTokens->inputsVrmTextureInfo_MultipleApplyTemplate_WrapS));
}

UsdAttribute
UsdVrmTextureInfoAPI::CreateWrapSAttr(VtValue const& defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(
        _GetNamespacedPropertyName(GetName(),
                                   UsdVrmTokens->inputsVrmTextureInfo_MultipleApplyTemplate_WrapS),
        SdfValueTypeNames->Token,
        /* custom = */ false, SdfVariabilityUniform, defaultValue, writeSparsely);
}

UsdAttribute
UsdVrmTextureInfoAPI::GetWrapTAttr() const
{
    return GetPrim().GetAttribute(_GetNamespacedPropertyName(
        GetName(), UsdVrmTokens->inputsVrmTextureInfo_MultipleApplyTemplate_WrapT));
}

UsdAttribute
UsdVrmTextureInfoAPI::CreateWrapTAttr(VtValue const& defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(
        _GetNamespacedPropertyName(GetName(),
                                   UsdVrmTokens->inputsVrmTextureInfo_MultipleApplyTemplate_WrapT),
        SdfValueTypeNames->Token,
        /* custom = */ false, SdfVariabilityUniform, defaultValue, writeSparsely);
}

UsdAttribute
UsdVrmTextureInfoAPI::GetScaleAttr() const
{
    return GetPrim().GetAttribute(_GetNamespacedPropertyName(
        GetName(), UsdVrmTokens->inputsVrmTextureInfo_MultipleApplyTemplate_Scale));
}

UsdAttribute
UsdVrmTextureInfoAPI::CreateScaleAttr(VtValue const& defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(
        _GetNamespacedPropertyName(GetName(),
                                   UsdVrmTokens->inputsVrmTextureInfo_MultipleApplyTemplate_Scale),
        SdfValueTypeNames->Float,
        /* custom = */ false, SdfVariabilityVarying, defaultValue, writeSparsely);
}

UsdAttribute
UsdVrmTextureInfoAPI::GetStrengthAttr() const
{
    return GetPrim().GetAttribute(_GetNamespacedPropertyName(
        GetName(), UsdVrmTokens->inputsVrmTextureInfo_MultipleApplyTemplate_Strength));
}

UsdAttribute
UsdVrmTextureInfoAPI::CreateStrengthAttr(VtValue const& defaultValue, bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(
        _GetNamespacedPropertyName(
            GetName(), UsdVrmTokens->inputsVrmTextureInfo_MultipleApplyTemplate_Strength),
        SdfValueTypeNames->Float,
        /* custom = */ false, SdfVariabilityVarying, defaultValue, writeSparsely);
}

UsdAttribute
UsdVrmTextureInfoAPI::GetTransformOffsetAttr() const
{
    return GetPrim().GetAttribute(_GetNamespacedPropertyName(
        GetName(), UsdVrmTokens->inputsVrmTextureInfo_MultipleApplyTemplate_TransformOffset));
}

UsdAttribute
UsdVrmTextureInfoAPI::CreateTransformOffsetAttr(VtValue const& defaultValue,
                                                bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(
        _GetNamespacedPropertyName(
            GetName(), UsdVrmTokens->inputsVrmTextureInfo_MultipleApplyTemplate_TransformOffset),
        SdfValueTypeNames->Float2,
        /* custom = */ false, SdfVariabilityVarying, defaultValue, writeSparsely);
}

UsdAttribute
UsdVrmTextureInfoAPI::GetTransformRotationAttr() const
{
    return GetPrim().GetAttribute(_GetNamespacedPropertyName(
        GetName(), UsdVrmTokens->inputsVrmTextureInfo_MultipleApplyTemplate_TransformRotation));
}

UsdAttribute
UsdVrmTextureInfoAPI::CreateTransformRotationAttr(VtValue const& defaultValue,
                                                  bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(
        _GetNamespacedPropertyName(
            GetName(), UsdVrmTokens->inputsVrmTextureInfo_MultipleApplyTemplate_TransformRotation),
        SdfValueTypeNames->Float,
        /* custom = */ false, SdfVariabilityVarying, defaultValue, writeSparsely);
}

UsdAttribute
UsdVrmTextureInfoAPI::GetTransformScaleAttr() const
{
    return GetPrim().GetAttribute(_GetNamespacedPropertyName(
        GetName(), UsdVrmTokens->inputsVrmTextureInfo_MultipleApplyTemplate_TransformScale));
}

UsdAttribute
UsdVrmTextureInfoAPI::CreateTransformScaleAttr(VtValue const& defaultValue,
                                               bool writeSparsely) const
{
    return UsdSchemaBase::_CreateAttr(
        _GetNamespacedPropertyName(
            GetName(), UsdVrmTokens->inputsVrmTextureInfo_MultipleApplyTemplate_TransformScale),
        SdfValueTypeNames->Float2,
        /* custom = */ false, SdfVariabilityVarying, defaultValue, writeSparsely);
}

namespace
{
static inline TfTokenVector
_ConcatenateAttributeNames(const TfTokenVector& left, const TfTokenVector& right)
{
    TfTokenVector result;
    result.reserve(left.size() + right.size());
    result.insert(result.end(), left.begin(), left.end());
    result.insert(result.end(), right.begin(), right.end());
    return result;
}
} // namespace

/*static*/
const TfTokenVector&
UsdVrmTextureInfoAPI::GetSchemaAttributeNames(bool includeInherited)
{
    static TfTokenVector localNames = {
        UsdVrmTokens->inputsVrmTextureInfo_MultipleApplyTemplate_File,
        UsdVrmTokens->inputsVrmTextureInfo_MultipleApplyTemplate_TexCoord,
        UsdVrmTokens->inputsVrmTextureInfo_MultipleApplyTemplate_WrapS,
        UsdVrmTokens->inputsVrmTextureInfo_MultipleApplyTemplate_WrapT,
        UsdVrmTokens->inputsVrmTextureInfo_MultipleApplyTemplate_Scale,
        UsdVrmTokens->inputsVrmTextureInfo_MultipleApplyTemplate_Strength,
        UsdVrmTokens->inputsVrmTextureInfo_MultipleApplyTemplate_TransformOffset,
        UsdVrmTokens->inputsVrmTextureInfo_MultipleApplyTemplate_TransformRotation,
        UsdVrmTokens->inputsVrmTextureInfo_MultipleApplyTemplate_TransformScale,
    };
    static TfTokenVector allNames =
        _ConcatenateAttributeNames(UsdAPISchemaBase::GetSchemaAttributeNames(true), localNames);

    if (includeInherited)
        return allNames;
    else
        return localNames;
}

/*static*/
TfTokenVector
UsdVrmTextureInfoAPI::GetSchemaAttributeNames(bool includeInherited, const TfToken& instanceName)
{
    const TfTokenVector& attrNames = GetSchemaAttributeNames(includeInherited);
    if (instanceName.IsEmpty())
    {
        return attrNames;
    }
    TfTokenVector result;
    result.reserve(attrNames.size());
    for (const TfToken& attrName : attrNames)
    {
        result.push_back(UsdSchemaRegistry::MakeMultipleApplyNameInstance(attrName, instanceName));
    }
    return result;
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
