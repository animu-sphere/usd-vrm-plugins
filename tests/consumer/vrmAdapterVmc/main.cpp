// SPDX-License-Identifier: Apache-2.0
//
// Includes one public header of the installed `vrmAdapterVmc` package and calls
// into it. The include proves the package installed its header root; the calls
// prove it installed something to link.
//
// The header chosen is the one that carries the package's whole edge set into a
// consumer's translation unit. `SkeletonMap.h` includes a canonical humanoid
// header and two OpenUSD value-type headers, so a config that resolved its
// exported target and none of the packages behind it compiles no further than
// the first `#include` -- which is the failure PACKAGE_CONTRACT.md §1 describes,
// caught at the earliest point a consumer can catch it.
//
// This is deliberately not a test of the adapter. `adapters/liveCapture/vmc/tests/`
// owns the bone table, the basis change and the refusals; duplicating any of it
// here would make a packaging failure look like a decoder failure the first time
// this fixture went red. What this asks is only: does a name come back, and does
// a converted position carry an OpenUSD value type across the package boundary.
#include <vrmAdapterVmc/SkeletonMap.h>

#include <array>
#include <cstdio>
#include <string>
#include <string_view>

int
main()
{
    // A Unity `HumanBodyBones` spelling in, the same spelling out. The thumb is
    // the one bone whose canonical name differs from the sender's by more than
    // its first letter, so a round trip through it is the smallest call pair
    // that could not be satisfied by a table this fixture accidentally shipped
    // itself.
    const auto bone = vrmAdapterVmc::FindVmcHumanBone("LeftThumbProximal");
    if (!bone) {
        std::fprintf(stderr, "consumer: the installed package maps no "
                             "LeftThumbProximal\n");
        return 1;
    }
    const std::string_view name = vrmAdapterVmc::VmcHumanBoneName(*bone);
    if (name != "LeftThumbProximal") {
        std::fprintf(stderr, "consumer: round trip returned %s\n",
                     std::string(name).c_str());
        return 1;
    }

    // The basis change, which is where a value type from a package this fixture
    // never names crosses the boundary. VRM 1.0 reflects through X, so the sign
    // of the first component is the whole assertion.
    const auto position = vrmAdapterVmc::ToCanonicalPosition(
        std::array<float, 3>{1.0f, 2.0f, 3.0f});
    if (!(position[0] == -1.0f && position[1] == 2.0f
          && position[2] == 3.0f)) {
        std::fprintf(stderr, "consumer: converted position is (%f, %f, %f)\n",
                     position[0], position[1], position[2]);
        return 1;
    }

    std::fprintf(stdout, "consumer: mapped %s through the installed package\n",
                 std::string(name).c_str());
    return 0;
}
