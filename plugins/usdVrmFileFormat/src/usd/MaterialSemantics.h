// SPDX-License-Identifier: Apache-2.0
//
// A material's canonical semantics on the stage (material policy §6): the
// VrmMaterialAPI / VrmMToonAPI / VrmTextureInfoAPI attributes on a
// UsdShadeMaterial, written from and read back into VrmMaterialSemantics.
//
// Reading is what makes a realization a function of the stage: the importer
// authors the semantics, reads them back, and generates each realization from
// what it read -- so the same generator run on any stage that carries the
// schemas produces the same graph (P5 Steps 5-6).
#pragma once

#include "model/VrmCanonicalDocument.h"

#include "pxr/pxr.h"
#include "pxr/usd/usdShade/material.h"

PXR_NAMESPACE_OPEN_SCOPE

// Author `s` on `material`: VrmMaterialAPI always, VrmMToonAPI when it has
// MToon, one VrmTextureInfoAPI instance per texture role.
void UsdVrmAuthorMaterialSemantics(const UsdShadeMaterial& material, const VrmMaterialSemantics& s);

// The semantics `material` carries: authored values, else the schema
// fallbacks. A texture's `hasTransform` says whether its `transform:*`
// attributes are authored -- whether the source stated a KHR_texture_transform.
VrmMaterialSemantics UsdVrmReadMaterialSemantics(const UsdShadeMaterial& material);

PXR_NAMESPACE_CLOSE_SCOPE
