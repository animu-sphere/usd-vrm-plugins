// SPDX-License-Identifier: Apache-2.0
//
// Includes one public header of the installed `motionCore` package and calls
// into it. The include proves the package installed its header root; the calls
// prove it installed something to link.
//
// `Humanoid.h` is the header that carries this package's whole edge set into a
// consumer's translation unit: two OpenUSD value-type headers arrive with it,
// so a config that resolved this package's target and left `pxr` unresolved
// compiles no further than the first `#include`. That is the failure
// PACKAGE_CONTRACT.md §1 describes, caught at the earliest point a consumer
// can catch it, and it is why this fixture includes that header rather than the
// self-contained one beside it.
//
// This is deliberately not a test of the humanoid taxonomy.
// `libs/motionCore/tests/` owns the table, the parent chain and the joint
// paths; duplicating any of it here would make a packaging failure look like a
// taxonomy failure the first time this fixture went red. What this asks is
// only: does a bone name go in and come back, and does a value type from a
// package this fixture never names cross the boundary intact.
#include <motionCore/Humanoid.h>

// The namespace is `motion` and the package is `motionCore`, which is not a
// slip: the library's identity is the artifact's name and the C++ namespace is
// the *layer* it belongs to, shared with the runtime beside it. A consumer
// finds that out from the header, which is one more reason a fixture includes
// one rather than only linking.

#include <bitset>
#include <cstdio>
#include <string>
#include <string_view>

int
main()
{
    // A canonical VRM 1.0 spelling in, the same spelling out. `leftUpperArm`
    // rather than `hips`, because a table this fixture accidentally shipped
    // itself would be likelier to agree about the root than about a limb.
    const auto bone = motion::FindHumanBone("leftUpperArm");
    if (!bone) {
        std::fprintf(stderr, "consumer: the installed package names no "
                             "leftUpperArm\n");
        return 1;
    }
    const std::string_view name = motion::HumanBoneName(*bone);
    if (name != "leftUpperArm") {
        std::fprintf(stderr, "consumer: round trip returned %s\n",
                     std::string(name).c_str());
        return 1;
    }

    // The joint path walks the hierarchy through a rig that carries every bone,
    // so it is the one call here whose answer depends on more than a single
    // table lookup crossing the boundary.
    std::bitset<motion::HumanBoneCount> present;
    present.set();
    const std::string path = motion::HumanBoneJointPath(*bone, present);
    if (path.rfind("hips/", 0) != 0 || path.find("/leftUpperArm") == path.npos) {
        std::fprintf(stderr, "consumer: joint path is %s\n", path.c_str());
        return 1;
    }

    // Where a value type from a package this fixture never names crosses the
    // boundary. A default pose's root orientation is the identity quaternion,
    // and reading its real part back through the imported target is the
    // smallest question that could not be answered by headers alone.
    const motion::HumanoidPose pose;
    if (pose.root.worldOrientation.GetReal() != 1.0f
        || pose.validRotations.any()) {
        std::fprintf(stderr, "consumer: a default pose carries %zu valid "
                             "rotations\n",
                     pose.validRotations.count());
        return 1;
    }

    std::fprintf(stdout, "consumer: resolved %s through the installed "
                         "package\n",
                 path.c_str());
    return 0;
}
