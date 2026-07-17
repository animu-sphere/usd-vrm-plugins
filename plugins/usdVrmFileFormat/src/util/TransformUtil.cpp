// SPDX-License-Identifier: Apache-2.0
#include "TransformUtil.h"

#include "pxr/base/gf/rotation.h"

PXR_NAMESPACE_OPEN_SCOPE

GfMatrix4d
VrmConvertGltfMatrix(const float* g)
{
    // glTF column-major [c0r0,c0r1,c0r2,c0r3, c1r0,...]. Read straight into
    // GfMatrix4d's row-major storage to obtain the transpose USD wants.
    return GfMatrix4d(
        g[0],  g[1],  g[2],  g[3],
        g[4],  g[5],  g[6],  g[7],
        g[8],  g[9],  g[10], g[11],
        g[12], g[13], g[14], g[15]);
}

GfMatrix4d
VrmComposeTrs(const float t[3], const float r[4], const float s[3])
{
    GfMatrix4d scale(1.0);
    scale.SetScale(GfVec3d(s[0], s[1], s[2]));

    GfMatrix4d rot(1.0);
    // glTF quaternion order is (x, y, z, w); GfQuatd is (w, (x, y, z)).
    rot.SetRotate(GfQuatd(r[3], r[0], r[1], r[2]));

    GfMatrix4d trans(1.0);
    trans.SetTranslate(GfVec3d(t[0], t[1], t[2]));

    // USD row-vector convention: v' = v * (S * R * T) applies scale, then
    // rotation, then translation — matching glTF's T * R * S * v.
    return scale * rot * trans;
}

PXR_NAMESPACE_CLOSE_SCOPE
