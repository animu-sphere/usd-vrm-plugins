// SPDX-License-Identifier: Apache-2.0
//
// Includes one public header of the installed `vrmRetarget` package and calls
// into it. The include proves the package installed its header root; the calls
// prove it installed something to link -- and here that second half is the
// point rather than a formality.
//
// `PoseRetargeter.h` is the header that reaches every other public header this
// package installs, and the only one whose implementation reaches the layer
// between this package and the value contract. That layer appears in no
// include list, so a consumer meets it for the first time at the link. Calling
// `Retarget` is what pulls the archive member carrying it, and a fixture that
// only constructed a skeleton would have compiled, linked, and never asked.
//
// This is deliberately not a test of retargeting. `libs/vrmRetarget/tests/`
// owns the rest-pose correction, the root-motion policy and the diagnostics;
// duplicating any of it here would make a packaging failure look like a
// retarget failure the first time this fixture went red. What this asks is
// only: does a one-joint rig expand a one-bone pose, and does the rotation that
// goes in come back out in the rig's own order.
#include <vrmRetarget/PoseRetargeter.h>

#include <cstdio>

int
main()
{
    // The smallest rig there is: one root joint, at rest, with the joint-path
    // token a UsdSkelSkeleton would carry.
    vrmRetarget::TargetJoint root;
    root.token = "Root";
    root.parent = vrmRetarget::TargetSkeleton::kNoParent;
    vrmRetarget::TargetSkeleton skeleton;
    skeleton.AddJoint(root);

    vrmRetarget::HumanoidMap map;
    if (!map.SetJointToken(motion::HumanBone::Hips, "Root", skeleton)) {
        std::fprintf(stderr, "consumer: the installed package would not bind "
                             "hips to the rig's only joint\n");
        return 1;
    }

    // A quarter turn about Y on the one bone the rig drives.
    const pxr::GfQuatf quarter(0.70710678f,
                               pxr::GfVec3f(0.0f, 0.70710678f, 0.0f));
    motion::HumanoidPose pose;
    pose.timestamp = 0.25;
    pose.localRotations[static_cast<std::size_t>(motion::HumanBone::Hips)] =
        quarter;
    pose.validRotations.set(static_cast<std::size_t>(motion::HumanBone::Hips));

    const vrmRetarget::PoseRetargeter retargeter(skeleton, map);
    const vrmRetarget::RetargetedPose expanded = retargeter.Retarget(pose);

    if (expanded.rotations.size() != skeleton.GetSize()
        || expanded.translations.size() != skeleton.GetSize()) {
        std::fprintf(stderr, "consumer: expanded %zu rotations for a %zu-joint "
                             "rig\n",
                     expanded.rotations.size(), skeleton.GetSize());
        return 1;
    }
    if (expanded.timestamp != pose.timestamp
        || expanded.rotations[0].GetReal() != quarter.GetReal()) {
        std::fprintf(stderr, "consumer: expanded to real part %f at t=%f\n",
                     expanded.rotations[0].GetReal(), expanded.timestamp);
        return 1;
    }

    std::fprintf(stdout, "consumer: expanded a pose onto %zu joint(s) through "
                         "the installed package\n",
                 expanded.rotations.size());
    return 0;
}
