// SPDX-License-Identifier: Apache-2.0
//
// Includes one public header of the installed `motionRuntime` package and calls
// into it. The include proves the package installed its header root; the calls
// prove it installed something to link.
//
// `Interpolation.h` is the header that names both of this package's edges in
// its own include list -- the layer below it and two OpenUSD value-type headers
// -- so a config that resolved this package's target and left either of them
// unresolved compiles no further than the first `#include`. The other public
// headers reach only one of the two, which is why this is the one.
//
// The namespace is `motion` and the package is `motionRuntime`: the identity is
// the artifact's name and the namespace is the layer, which this package shares
// with the value contract underneath it. A consumer finds that out from the
// header, which is one more reason a fixture includes one rather than only
// linking.
//
// This is deliberately not a test of the interpolator.
// `libs/motionRuntime/tests/` owns the shortest-arc cases, the hold-not-fade
// rule and the buffer; duplicating any of it here would make a packaging
// failure look like an interpolation failure the first time this fixture went
// red. What this asks is only: do two poses go in and one come back, carrying
// value types from a package this fixture never names.
#include <motionRuntime/Interpolation.h>

#include <cstdio>

int
main()
{
    // A quarter turn about Y, halved. The endpoints are built here rather than
    // read from anywhere, so the fixture carries no fixture file.
    const pxr::GfQuatf identity(1.0f, pxr::GfVec3f(0.0f));
    const pxr::GfQuatf quarter(0.70710678f,
                               pxr::GfVec3f(0.0f, 0.70710678f, 0.0f));

    motion::HumanoidPose a;
    a.timestamp = 0.0;
    a.localRotations[static_cast<std::size_t>(motion::HumanBone::Hips)] =
        identity;
    a.validRotations.set(static_cast<std::size_t>(motion::HumanBone::Hips));

    motion::HumanoidPose b = a;
    b.timestamp = 1.0;
    b.localRotations[static_cast<std::size_t>(motion::HumanBone::Hips)] =
        quarter;

    const motion::HumanoidPose mid = motion::LerpPose(a, b, 0.5f);
    if (mid.timestamp != 0.5) {
        std::fprintf(stderr, "consumer: midpoint timestamp is %f\n",
                     mid.timestamp);
        return 1;
    }
    if (!mid.validRotations.test(
            static_cast<std::size_t>(motion::HumanBone::Hips))) {
        std::fprintf(stderr, "consumer: the interpolated pose drives no hips\n");
        return 1;
    }

    // A bone valid in both endpoints is slerped, so the midpoint's real part
    // sits strictly between the two -- which is the smallest assertion that
    // could not be satisfied by a package that returned one endpoint unchanged.
    const float real =
        mid.localRotations[static_cast<std::size_t>(motion::HumanBone::Hips)]
            .GetReal();
    if (!(real > quarter.GetReal() && real < identity.GetReal())) {
        std::fprintf(stderr, "consumer: midpoint real part is %f\n", real);
        return 1;
    }

    std::fprintf(stdout, "consumer: interpolated to %f through the installed "
                         "package\n",
                 real);
    return 0;
}
