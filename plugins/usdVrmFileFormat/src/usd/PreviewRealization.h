// SPDX-License-Identifier: Apache-2.0
//
// The UsdPreviewSurface realization, /Asset/mtl/<name>/preview (material policy
// §5.1), generated from a material's canonical semantics and from nothing else
// (P5 Step 5): no source JSON, no extension block, no importer state. The
// importer calls it with what it reads back from the stage; any other caller
// can do the same on any stage that carries the schemas, and gets the same
// graph.
#pragma once

#include "model/VrmCanonicalDocument.h"

#include "pxr/pxr.h"
#include "pxr/usd/usdShade/material.h"

PXR_NAMESPACE_OPEN_SCOPE

// Define `material`/preview from `s` and connect the material's universal
// surface terminal to it. Only the five glTF core texture roles are read:
// MToon semantics have no PreviewSurface input to go to (policy §5.1).
void UsdVrmAuthorPreview(const UsdShadeMaterial& material, const VrmMaterialSemantics& s);

PXR_NAMESPACE_CLOSE_SCOPE
