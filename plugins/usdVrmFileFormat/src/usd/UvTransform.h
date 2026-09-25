// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "model/VrmCanonicalDocument.h"

#include "pxr/pxr.h"
#include "pxr/base/gf/math.h"
#include "pxr/base/gf/vec2f.h"

#include <cmath>

PXR_NAMESPACE_OPEN_SCOPE

// KHR_texture_transform, resolved into the UV space USD actually samples.
//
// Two changes of variable sit between glTF's statement of the transform and
// what either realization should author, and neither is visible on an identity
// transform — which is every transform in the corpus, so regenerating baselines
// proves nothing about this:
//
//   * The importer already flipped V (`VrmConvertUv`), so a transform glTF
//     defines against its own top-left-origin UVs has to be conjugated by that
//     flip before it applies to `st`.
//   * glTF states the rotation in radians; both `UsdTransform2d.rotation` and
//     MaterialX's `place2d.rotate` are declared in degrees.
//
// Conjugating glTF's T*R*S by (u, v) -> (u, 1-v) leaves the scale alone, flips
// the sense of the rotation, and lands the translation on the value below. What
// comes out is one affine map in st space, written with MaterialX's `rotate2d`
// (which turns clockwise and takes degrees — both realizations are built on it):
//
//     st' = rotate2d(scale * st, -rotationDegrees) + translation
//
// Each realization then spells that in its own vocabulary, and the two spellings
// share nothing but this: UsdTransform2d multiplies by its scale, adds its
// translation and negates its rotation, while place2d divides, subtracts and
// does not negate. Deriving the map once is what stops them drifting apart --
// verified against glTF's own matrix in check_texture_transform, since on an
// identity transform (which is every transform in the corpus) every wrong
// answer coincides with the right one.
struct VrmStTransform
{
    GfVec2f scale;
    float rotationDegrees;
    GfVec2f translation;
};

inline VrmStTransform
VrmGltfStTransform(const VrmTextureRef& ref)
{
    const float r = ref.uvRotation; // radians, per KHR_texture_transform
    return {ref.uvScale, static_cast<float>(GfRadiansToDegrees(r)),
            GfVec2f(ref.uvOffset[0] + ref.uvScale[1] * std::sin(r),
                    1.0f - ref.uvOffset[1] - ref.uvScale[1] * std::cos(r))};
}

PXR_NAMESPACE_CLOSE_SCOPE
