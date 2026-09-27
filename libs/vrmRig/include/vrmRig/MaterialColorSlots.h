// SPDX-License-Identifier: Apache-2.0
//
// Where an expression's material-colour bind lands: the VRM colour slot, and
// the canonical material attribute it drives (material policy §6.7).
//
// An expression changes a material *semantic*, never a realization's shader
// input. A colour written into `/preview` is invisible through `/mtlx` and
// unknown to a renderer that reads neither, so a slot resolves onto the
// Material's own `inputs:vrm:*` attributes, and every realization that
// connects to them follows. This table is the schema contract's ("Expression
// colours drive canonical slots"); every writer and every evaluator resolves a
// slot through it, so two of them can never land one slot in two places.
//
// The slots are VRM 1.0's `MaterialColorType`, and each attribute is the glTF
// or `VRMC_materials_mtoon` field the specification's table names for it. A
// VRM 0.x `materialValues` property reaches the same slots through the
// importer, which migrates `_Color` to `color` the way it migrates the preset
// names.
#pragma once

#include "vrmRig/api.h"

#include <string>
#include <vector>

namespace vrmRig
{

struct MaterialColorSlot
{
    // The slot as VRM 1.0 spells it: "color", "emissionColor", "shadeColor",
    // "matcapColor", "rimColor", "outlineColor".
    const char* name;

    // The API schema that carries the attributes, "VrmMaterialAPI" or
    // "VrmMToonAPI". A bind onto a material that does not apply it has no
    // attribute to land on: an MToon slot of a glTF PBR material is one the
    // specification calls "Unused".
    const char* schema;

    // The RGB half: a `color3f` Material input.
    const char* colorInput;

    // The fourth component's attribute, a `float` Material input, or nullptr
    // where the destination has none. The specification: "the 4th value must
    // be ignored if there is no 4th component in the destination parameter".
    const char* alphaInput;
};

// The six slots, in the specification's order.
VRMRIG_API const std::vector<MaterialColorSlot>& GetMaterialColorSlots();

// The slot `name` spells, or nullptr for a name outside the six -- which a
// caller reports rather than guesses at, as it does an unknown override token.
VRMRIG_API const MaterialColorSlot* FindMaterialColorSlot(const std::string& name);

} // namespace vrmRig
