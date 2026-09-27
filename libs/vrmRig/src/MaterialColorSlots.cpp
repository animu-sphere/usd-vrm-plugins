// SPDX-License-Identifier: Apache-2.0
#include "vrmRig/MaterialColorSlots.h"

namespace vrmRig
{

const std::vector<MaterialColorSlot>&
GetMaterialColorSlots()
{
    // VRM 1.0 expressions.md, "MaterialColorBind": `color` is
    // pbrMetallicRoughness.baseColorFactor for every shading model, and
    // canonical storage splits glTF's RGBA into a colour and a float.
    static const std::vector<MaterialColorSlot> slots = {
        {"color", "VrmMaterialAPI", "inputs:vrm:material:baseColorFactor",
         "inputs:vrm:material:baseColorAlphaFactor"},
        {"emissionColor", "VrmMaterialAPI", "inputs:vrm:material:emissiveFactor", nullptr},
        {"shadeColor", "VrmMToonAPI", "inputs:vrm:mtoon:shadeColorFactor", nullptr},
        {"matcapColor", "VrmMToonAPI", "inputs:vrm:mtoon:matcapFactor", nullptr},
        {"rimColor", "VrmMToonAPI", "inputs:vrm:mtoon:parametricRimColorFactor", nullptr},
        {"outlineColor", "VrmMToonAPI", "inputs:vrm:mtoon:outlineColorFactor", nullptr},
    };
    return slots;
}

const MaterialColorSlot*
FindMaterialColorSlot(const std::string& name)
{
    for (const MaterialColorSlot& slot : GetMaterialColorSlots())
    {
        if (name == slot.name)
            return &slot;
    }
    return nullptr;
}

} // namespace vrmRig
