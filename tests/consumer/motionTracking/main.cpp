// SPDX-License-Identifier: Apache-2.0
//
// Includes three public headers of the installed `motionTracking` package and
// calls into all of them. The includes prove the package installed its header
// root; the calls prove it installed something to link.
//
// **The third include is the one that measures VRC-5's edge.** `TrackerSolve.h`
// names `motionCore/Humanoid.h`, so a config that lost its `find_dependency`
// line fails here at compile time rather than at link time — and it fails in a
// consumer that names no target from the source tree, which is the only place
// that question can be asked.
//
// This is deliberately not a test of the policy. `libs/motionTracking/tests/`
// owns that, and duplicating any of it here would make a packaging failure look
// like a policy failure the first time this fixture went red. What this asks is
// only: does a statement reach a binding through the installed package.
#include <motionTracking/TrackerAssignment.h>
#include <motionTracking/TrackerObservation.h>
#include <motionTracking/TrackerRegion.h>
#include <motionTracking/TrackerSolve.h>

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

int
main()
{
    motionTracking::TrackerAssignmentSpec spec;
    std::string reason;
    if (!motionTracking::ParseTrackerAssignmentSpec("a=head b=hips", &spec,
                                                    &reason)) {
        std::fprintf(stderr, "consumer: statement refused: %s\n",
                     reason.c_str());
        return 1;
    }

    const std::vector<std::string_view> observed{"b", "a"};
    const motionTracking::TrackerAssignment assignment =
        motionTracking::AssignTrackers(spec, observed);
    if (!assignment.Placed()) {
        std::fprintf(stderr, "consumer: assignment refused: %s (%s)\n",
                     std::string(motionTracking::TrackerAssignmentRefusalName(
                                     assignment.refusal))
                         .c_str(),
                     assignment.detail.c_str());
        return 1;
    }

    const auto head = assignment.ObservedFor(motionTracking::TrackerRegion::Head);
    if (!head.has_value() || *head != 1) {
        std::fprintf(stderr, "consumer: head bound to the wrong observation\n");
        return 1;
    }

    // And on through the solve, which is what carries the `motionCore` edge.
    // The observation is built here rather than reusing the vector above,
    // because a binding indexes the array its identities came from.
    std::vector<motionTracking::TrackerObservation> observations(2);
    observations[0].tracker = "b";
    observations[0].rotation = pxr::GfQuatf(1.0f, pxr::GfVec3f(0.0f));
    observations[0].hasRotation = true;
    observations[1].tracker = "a";
    observations[1].rotation = pxr::GfQuatf(1.0f, pxr::GfVec3f(0.0f));
    observations[1].hasRotation = true;

    const motionTracking::TrackerSolve solve =
        motionTracking::SolveTrackerPose(
            motionTracking::AssignTrackers(
                spec, motionTracking::TrackerIdentities(observations)),
            observations, 0.0);
    if (!solve.Solved()) {
        std::fprintf(stderr, "consumer: solve refused: %s (%s)\n",
                     std::string(motionTracking::TrackerSolveRefusalName(
                                     solve.refusal))
                         .c_str(),
                     solve.detail.c_str());
        return 1;
    }
    if (solve.pose.validRotations.count() != 2) {
        std::fprintf(stderr, "consumer: the solve authored %zu rotation(s)\n",
                     solve.pose.validRotations.count());
        return 1;
    }

    std::fprintf(stdout,
                 "consumer: placed %zu regions and authored %zu bone rotation(s) "
                 "through the installed package\n",
                 assignment.bound.size(), solve.pose.validRotations.count());
    return 0;
}
