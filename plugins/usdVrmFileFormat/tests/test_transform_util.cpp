// SPDX-License-Identifier: Apache-2.0
//
// Pure-logic unit tests for util/TransformUtil — the single place glTF/VRM
// numerics become USD. These lock the three conversions that are easy to get
// subtly wrong and hard to notice downstream: the column-major -> row-vector
// matrix transpose, the (x,y,z,w) -> (w,x,y,z) quaternion reorder in the TRS
// compose, and the UV V-flip. No USD plugin/runtime needed — just gf.
#include "util/TransformUtil.h"

#include "pxr/base/gf/matrix4d.h"
#include "pxr/base/gf/vec3d.h"

#include <cmath>
#include <cstdio>

PXR_NAMESPACE_USING_DIRECTIVE

namespace {

int g_failures = 0;

void _Check(bool ok, const char* expr, int line)
{
    if (!ok) {
        std::printf("  FAIL (line %d): %s\n", line, expr);
        ++g_failures;
    }
}
#define CHECK(expr) _Check((expr), #expr, __LINE__)

bool _Close(double a, double b, double eps = 1e-9)
{
    return std::fabs(a - b) <= eps;
}

bool _VecClose(const GfVec3d& a, const GfVec3d& b, double eps = 1e-9)
{
    return _Close(a[0], b[0], eps) && _Close(a[1], b[1], eps) &&
           _Close(a[2], b[2], eps);
}

// glTF transforms column vectors (M * v); USD transforms row vectors (v * M), so
// the USD matrix is the transpose. The constructor reads glTF's column-major
// array straight into row-major storage to do that for free.
void TestConvertMatrix()
{
    // A column-major matrix that is NOT symmetric, so a missing transpose would
    // show. Columns: c0=(1,2,3,0) c1=(4,5,6,0) c2=(7,8,9,0) c3=(10,11,12,1).
    const float g[16] = {
        1, 2, 3, 0,
        4, 5, 6, 0,
        7, 8, 9, 0,
        10, 11, 12, 1,
    };
    GfMatrix4d m = VrmConvertGltfMatrix(g);

    // Row-major load => m[row][col] = g[row*4 + col]. The transpose puts glTF's
    // translation column (g[12..14]) into USD's translation row (m[3][0..2]).
    CHECK(_Close(m[0][0], 1.0) && _Close(m[0][1], 2.0) && _Close(m[0][2], 3.0));
    CHECK(_Close(m[3][0], 10.0) && _Close(m[3][1], 11.0) && _Close(m[3][2], 12.0));
    CHECK(_VecClose(m.ExtractTranslation(), GfVec3d(10, 11, 12)));

    // Equivalence with the gf transpose of the naive (non-converting) load.
    GfMatrix4d naive(
        g[0], g[1], g[2], g[3], g[4], g[5], g[6], g[7],
        g[8], g[9], g[10], g[11], g[12], g[13], g[14], g[15]);
    CHECK(m == naive);  // the constructor already loaded row-major == transpose-free
}

// USD row-vector convention: v' = v * (S*R*T) applies scale, then rotation, then
// translation, matching glTF's T*R*S*v on column vectors.
void TestComposeTrs()
{
    // Translation only — sign-unambiguous, so check the point action directly.
    {
        const float t[3] = {10, -2, 3}, r[4] = {0, 0, 0, 1}, s[3] = {1, 1, 1};
        GfMatrix4d m = VrmComposeTrs(t, r, s);
        CHECK(_VecClose(m.ExtractTranslation(), GfVec3d(10, -2, 3)));
        CHECK(_VecClose(m.Transform(GfVec3d(0, 0, 0)), GfVec3d(10, -2, 3)));
    }
    // Scale only — likewise unambiguous.
    {
        const float t[3] = {0, 0, 0}, r[4] = {0, 0, 0, 1}, s[3] = {2, 3, 4};
        GfMatrix4d m = VrmComposeTrs(t, r, s);
        CHECK(_VecClose(m.Transform(GfVec3d(1, 1, 1)), GfVec3d(2, 3, 4)));
    }
    // Rotation: cross-check VrmComposeTrs against the *same* rotation built as a
    // glTF column-major matrix and run through VrmConvertGltfMatrix. This locks
    // the (x,y,z,w) -> (w,x,y,z) quaternion reorder and the matrix transpose
    // together, without depending on USD's own rotation sign convention. A
    // swapped quaternion order yields a different axis and this fails.
    {
        const float c = 0.0f, s = 1.0f;  // cos/sin(90 deg)
        const float t[3] = {0, 0, 0};
        const float r[4] = {0.0f, 0.0f, (float)std::sqrt(0.5),
                            (float)std::sqrt(0.5)};  // +90 about Z, (x,y,z,w)
        const float sc[3] = {1, 1, 1};
        // glTF column-major R_z(90): col0=(c,s,0,0), col1=(-s,c,0,0).
        const float gz[16] = {c, s, 0, 0, -s, c, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        CHECK(GfIsClose(VrmComposeTrs(t, r, sc), VrmConvertGltfMatrix(gz), 1e-6));
    }
    // Full T*R*S cross-check: rotation about Z by 90 deg, non-uniform scale, and
    // translation, against the hand-built glTF column-major composite. Columns
    // are scaled rotation columns; the 4th column is the translation.
    {
        const float t[3] = {10, 20, 30};
        const float r[4] = {0.0f, 0.0f, (float)std::sqrt(0.5),
                            (float)std::sqrt(0.5)};  // +90 about Z
        const float s[3] = {2, 3, 4};
        // R_z(90) columns: c0=(0,1,0), c1=(-1,0,0), c2=(0,0,1); scale per column.
        const float g[16] = {
            0, 2, 0, 0,      // s0 * c0
            -3, 0, 0, 0,     // s1 * c1
            0, 0, 4, 0,      // s2 * c2
            10, 20, 30, 1,   // translation
        };
        CHECK(GfIsClose(VrmComposeTrs(t, r, s), VrmConvertGltfMatrix(g), 1e-6));
    }
}

// glTF UV origin is top-left, USD st origin is bottom-left: V := 1 - V.
void TestConvertUv()
{
    GfVec2f uv = VrmConvertUv(GfVec2f(0.25f, 0.1f));
    CHECK(_Close(uv[0], 0.25, 1e-6));
    CHECK(_Close(uv[1], 0.9, 1e-6));
    // U is untouched; the flip is an involution.
    GfVec2f back = VrmConvertUv(uv);
    CHECK(_Close(back[0], 0.25, 1e-6) && _Close(back[1], 0.1, 1e-6));
}

}  // namespace

int main()
{
    TestConvertMatrix();
    TestComposeTrs();
    TestConvertUv();
    if (g_failures) {
        std::printf("TransformUtil unit tests: %d FAILED\n", g_failures);
        return 1;
    }
    std::printf("TransformUtil unit tests: OK\n");
    return 0;
}
