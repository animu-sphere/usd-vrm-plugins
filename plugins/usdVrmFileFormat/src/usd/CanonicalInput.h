// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "pxr/pxr.h"
#include "pxr/base/tf/token.h"
#include "pxr/usd/sdf/valueTypeName.h"
#include "pxr/usd/usdShade/input.h"
#include "pxr/usd/usdShade/material.h"
#include "pxr/usd/usdShade/nodeGraph.h"

#include <string>

PXR_NAMESPACE_OPEN_SCOPE

// A realization graph's interface input for one canonical property, connected
// to the Material's own `inputs:<name>` -- `name` spelled without `inputs:`,
// e.g. "vrm:material:baseColorFactor".
//
// This is how a realization *reads* a canonical value instead of holding a copy
// of it (material policy §6.4.1, §11 q12): a node input connected here resolves
// through the graph to the Material, so a value an expression animates on the
// Material reaches every realization that connects, frame by frame, with
// nothing regenerated and nothing written below either graph. Nodes inside the
// graph connect to the graph's interface rather than to the Material directly,
// which is UsdShade's encapsulation rule.
//
// The connection resolves to an *authored* value only -- a schema fallback is
// not what a connected node sees -- which is why the importer authors every
// canonical value (Step 4). The interface input carries no value of its own.
inline UsdShadeInput
UsdVrmCanonicalInput(const UsdShadeMaterial& material, const UsdShadeNodeGraph& graph,
                     const std::string& name, const SdfValueTypeName& type)
{
    const TfToken token(name);
    UsdShadeInput input = graph.GetInput(token);
    if (!input)
    {
        input = graph.CreateInput(token, type);
        input.ConnectToSource(material.CreateInput(token, type));
    }
    return input;
}

PXR_NAMESPACE_CLOSE_SCOPE
