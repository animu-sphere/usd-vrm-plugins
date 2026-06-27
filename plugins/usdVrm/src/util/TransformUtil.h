// SPDX-License-Identifier: Apache-2.0
//
// All glTF/VRM -> USD numeric conversion lives here, in one place, per the
// implementation plan. glTF and USD are *both* right-handed, Y-up, metric, so no
// axis or handedness flip is required for geometry. The only real conversions
// are (a) glTF column-major matrices -> USD row-vector GfMatrix4d, and (b) the
// glTF UV origin (top-left) -> USD st origin (bottom-left), i.e. V := 1 - V.
#pragma once

#include "pxr/pxr.h"
#include "pxr/base/gf/matrix4d.h"
#include "pxr/base/gf/quatf.h"
#include "pxr/base/gf/vec2f.h"
#include "pxr/base/gf/vec3f.h"

PXR_NAMESPACE_OPEN_SCOPE

// Convert a glTF 16-float, column-major matrix into a USD GfMatrix4d.
//
// glTF transforms column vectors (M*v); USD transforms row vectors (v*M). The
// USD matrix is therefore the transpose of the glTF one. Reading glTF's
// column-major array straight into GfMatrix4d's row-major storage performs that
// transpose for free (translation correctly lands in the 4th row).
GfMatrix4d VrmConvertGltfMatrix(const float* gltfColumnMajor16);

// Compose a node-local transform from glTF TRS. Quaternion is (x,y,z,w).
GfMatrix4d VrmComposeTrs(const float translation[3],
                         const float rotationXyzw[4],
                         const float scale[3]);

// glTF UV (top-left origin) -> USD st (bottom-left origin).
inline GfVec2f VrmConvertUv(const GfVec2f& gltfUv)
{
    return GfVec2f(gltfUv[0], 1.0f - gltfUv[1]);
}

PXR_NAMESPACE_CLOSE_SCOPE
