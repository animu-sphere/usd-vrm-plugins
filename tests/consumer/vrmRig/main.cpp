// SPDX-License-Identifier: Apache-2.0
//
// Includes the public headers of the installed `vrmRig` package and calls
// into them. The include proves the package installed its header root; the
// calls prove it installed something to link.
//
// What the package holds now is what a VRM rig adds to a retarget -- the
// expression resolve, the look-at and VRM 1.0's required bones -- and the
// retarget itself is usd-motion-plugins' `motionRetarget`. So this reaches the
// two halves a consumer actually calls: `ExpressionResolver.h`, whose values
// are the value contract's channel set, and `RequiredBones.h`, the list a
// caller hands the retarget. `Resolve` is the call whose archive member reaches
// the value contract at the link, so a fixture that only built a rig would
// have compiled, linked, and never asked.
//
// This is deliberately not a test of either. `libs/vrmRig/tests/` owns the
// overrides, the clamps and the look-at geometry; duplicating any of it here
// would make a packaging failure look like a resolve failure the first time
// this fixture went red. What this asks is only: does one expression expand
// onto its one bind, and is the required set the one a VRM 1.0 avatar states.
#include <vrmRig/ExpressionResolver.h>
#include <vrmRig/RequiredBones.h>
#include <vrmRig/SpringBoneSolver.h>

#if !defined(VRMRIG_SPRING_BONE_API) || VRMRIG_SPRING_BONE_API < 1
#error "This consumer requires vrmRig's spring-bone API; rebuild the owner package."
#endif

#include <cstdio>

int
main()
{
    // The smallest rig there is: one expression driving one morph target.
    vrmRig::ExpressionRig rig;
    vrmRig::ExpressionDefinition happy;
    happy.name = "happy";
    happy.morphTargets.push_back({"/Asset/Meshes/Face/Smile", 1.0f});
    if (!rig.Add(happy))
    {
        std::fprintf(stderr, "consumer: the installed package would not declare "
                             "the rig's only expression\n");
        return 1;
    }

    openstrata::motion::MotionChannelSet weights;
    weights.Set("happy", 0.25f);
    const vrmRig::ExpressionResolver resolver(rig);
    const vrmRig::ResolvedExpressions resolved = resolver.Resolve(weights);
    if (resolved.morphTargets.size() != 1 || resolved.morphTargets[0].weight != 0.25f)
    {
        std::fprintf(stderr, "consumer: expanded onto %zu morph target(s)\n",
                     resolved.morphTargets.size());
        return 1;
    }

    const std::vector<openstrata::motion::HumanJoint>& required = vrmRig::GetRequiredBones();
    if (required.size() != 17 || required.front() != openstrata::motion::HumanJoint::Hips)
    {
        std::fprintf(stderr, "consumer: the installed package requires %zu bone(s)\n",
                     required.size());
        return 1;
    }

    vrmRig::SpringBoneRig springRig;
    springRig.parents = {-1, 0};
    springRig.restTransforms.resize(2);
    springRig.restTransforms[1].translation = pxr::GfVec3f(0, 0.1f, 0);
    vrmRig::SpringBoneSpring spring;
    spring.joints = {0, 1};
    spring.parameters.resize(2);
    springRig.springs = {spring};
    vrmRig::SpringBoneSolver solver;
    vrmRig::SpringBoneResult result;
    const vrmRig::SpringBonePose pose{springRig.restTransforms, {}};
    if (!solver.Configure(springRig) || !solver.Reset(pose, &result) ||
        !solver.Step(1.0f / 60.0f, pose, &result) || result.tails.size() != 1)
    {
        std::fprintf(stderr, "consumer: the installed spring solver did not step\n");
        return 1;
    }

    std::fprintf(stdout,
                 "consumer: resolved an expression onto %zu bind(s), and %zu "
                 "required bones, and stepped a spring through the installed package\n",
                 resolved.morphTargets.size(), required.size());
    return 0;
}
