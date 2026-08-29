// SPDX-License-Identifier: Apache-2.0
//
// Includes one public header of the installed `motionSource` package and calls
// into it. The include proves the package installed its header root; the calls
// prove it installed something to link.
//
// `CanonicalConversion.h` is the header that reaches every other public header
// this package installs *and* names both of its edges in its own include list.
// A config that resolved this package's target and left either unresolved
// compiles no further than the first `#include`, and a prefix that installed
// four of the package's five headers fails there too.
//
// This is deliberately not a test of the conversion.
// `libs/motionSource/tests/` owns the profile matching, the handedness cases
// and the validators; duplicating any of it here would make a packaging failure
// look like a basis-change failure the first time this fixture went red. What
// this asks is only: does a source-space vector go in and a canonical value
// type come back.
#include <motionSource/CanonicalConversion.h>

#include <cstdio>

int
main()
{
    // A mirror through X, stated rather than derived from a profile: the
    // determinant is what tells a rotation from a reflection, and it is a
    // stored field precisely so a caller does not recompute it. Building the
    // basis here keeps the fixture free of a profile file.
    motionSource::CanonicalBasis mirrored;
    mirrored.negate[0] = true;
    mirrored.determinant = -1;

    const pxr::GfVec3f position = motionSource::ConvertPosition(
        mirrored, motionSource::SourceVec3{1.0f, 2.0f, 3.0f});
    if (!(position[0] == -1.0f && position[1] == 2.0f
          && position[2] == 3.0f)) {
        std::fprintf(stderr, "consumer: converted position is (%f, %f, %f)\n",
                     position[0], position[1], position[2]);
        return 1;
    }

    // The rotation half, where the determinant is used differently: a mirror
    // negates the vector part twice over -- once for the permutation and once
    // for the handedness -- so a half turn about X survives it unchanged while
    // the position above did not. `SourceQuat` puts the real part first.
    const pxr::GfQuatf rotation = motionSource::ConvertRotation(
        mirrored, motionSource::SourceQuat{0.0f, 1.0f, 0.0f, 0.0f});
    if (rotation.GetReal() != 0.0f
        || rotation.GetImaginary()[0] != 1.0f) {
        std::fprintf(stderr, "consumer: converted rotation is (%f, %f, %f, %f)\n",
                     rotation.GetReal(), rotation.GetImaginary()[0],
                     rotation.GetImaginary()[1], rotation.GetImaginary()[2]);
        return 1;
    }

    std::fprintf(stdout, "consumer: converted a mirrored basis through the "
                         "installed package\n");
    return 0;
}
