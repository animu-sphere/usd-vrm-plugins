// SPDX-License-Identifier: Apache-2.0
//
// Includes one public header of the installed `vrmAdapterMocopi` package and
// calls into it. The include proves the package installed its header root; the
// calls prove it installed something to link.
//
// `SkeletonMap.h` is the header that carries this package's edge set into a
// consumer's translation unit -- a canonical humanoid header and two OpenUSD
// value-type headers arrive with it -- so a config that resolved this package's
// target and left a required package unresolved compiles no further than the
// first `#include`. That is the failure PACKAGE_CONTRACT.md §1 describes,
// caught at the earliest point a consumer can catch it.
//
// This is deliberately not a test of the adapter.
// `adapters/liveCapture/mocopi/tests/` owns the measured rig, the packet
// grammar and the frame assembly; duplicating any of it here would make a
// packaging failure look like a decoder failure the first time this fixture
// went red. What this asks is only: does a joint id map to the bone the
// measured rig says it carries, and does a converted position cross the package
// boundary as an OpenUSD value type.
#include <vrmAdapterMocopi/SkeletonMap.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

int
main()
{
    // Joint 12 rather than joint 0: the root maps to the bone anything would
    // guess, and a limb does not. `IsMeasuredJoint` is what separates a joint
    // this rig does not have from one it has and carries no canonical bone for,
    // and the pair is the smallest question that needs the installed table
    // rather than an assumption about it.
    constexpr std::uint16_t leftUpperArmJoint = 12;
    if (!vrmAdapterMocopi::IsMeasuredJoint(leftUpperArmJoint)) {
        std::fprintf(stderr, "consumer: the installed package does not know "
                             "joint %u\n",
                     leftUpperArmJoint);
        return 1;
    }
    const auto bone = vrmAdapterMocopi::MeasuredHumanBone(leftUpperArmJoint);
    if (!bone) {
        std::fprintf(stderr, "consumer: joint %u carries no canonical bone\n",
                     leftUpperArmJoint);
        return 1;
    }
    const std::string_view name = motion::HumanBoneName(*bone);
    if (name != "leftUpperArm") {
        std::fprintf(stderr, "consumer: joint %u mapped to %s\n",
                     leftUpperArmJoint, std::string(name).c_str());
        return 1;
    }

    // The change of basis, which is where a value type from a package this
    // fixture never names crosses the boundary. This device's basis and the
    // canonical one agree -- +X is the body's left in both -- so the identity
    // here is the measurement rather than a missing conversion.
    const pxr::GfVec3f position = vrmAdapterMocopi::ToCanonicalPosition(
        std::array<float, 3>{1.0f, 2.0f, 3.0f});
    if (!(position[0] == 1.0f && position[1] == 2.0f && position[2] == 3.0f)) {
        std::fprintf(stderr, "consumer: converted position is (%f, %f, %f)\n",
                     position[0], position[1], position[2]);
        return 1;
    }

    // The wire orders a quaternion scalar-last and OpenUSD orders it
    // scalar-first, so this is the one call whose answer would be wrong rather
    // than merely absent if the package behind the header were a different
    // build.
    const pxr::GfQuatf rotation = vrmAdapterMocopi::ToCanonicalRotation(
        std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f});
    if (rotation.GetReal() != 1.0f) {
        std::fprintf(stderr, "consumer: converted rotation has real part %f\n",
                     rotation.GetReal());
        return 1;
    }

    std::fprintf(stdout, "consumer: mapped joint %u to %s through the "
                         "installed package\n",
                 leftUpperArmJoint, std::string(name).c_str());
    return 0;
}
