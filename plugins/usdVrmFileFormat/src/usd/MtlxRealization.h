// SPDX-License-Identifier: Apache-2.0
//
// The MaterialX realization, /Asset/mtl/<name>/mtlx (material policy §5.2),
// generated from a material's canonical semantics and from nothing else
// (P5 Step 6): no source JSON, no extension block, no importer state, and
// never a translation of /preview (§3). The importer calls it with what it
// reads back from the stage; any other caller can do the same on any stage
// that carries the schemas, and gets the same graph.
#pragma once

#include "model/VrmCanonicalDocument.h"

#include "pxr/pxr.h"
#include "pxr/usd/usdShade/material.h"

PXR_NAMESPACE_OPEN_SCOPE

// Define `material`/mtlx from `s`, connect the material's `mtlx` surface
// terminal to it and declare the MaterialX version it was written against.
// Every material gets one: unlit materials as emission, lit ones as glTF
// metallic-roughness PBR, both through `gltf_pbr` (policy §5.2.1).
void UsdVrmAuthorMtlx(const UsdShadeMaterial& material, const VrmMaterialSemantics& s);

PXR_NAMESPACE_CLOSE_SCOPE
