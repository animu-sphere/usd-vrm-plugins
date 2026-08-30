// SPDX-License-Identifier: Apache-2.0
//
// Includes two public headers of the installed `motionTracking` package and
// calls into both. The includes prove the package installed its header root;
// the calls prove it installed something to link.
//
// This is deliberately not a test of the policy. `libs/motionTracking/tests/`
// owns that, and duplicating any of it here would make a packaging failure look
// like a policy failure the first time this fixture went red. What this asks is
// only: does a statement reach a binding through the installed package.
#include <motionTracking/TrackerAssignment.h>
#include <motionTracking/TrackerRegion.h>

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

    std::fprintf(stdout,
                 "consumer: placed %zu regions through the installed package\n",
                 assignment.bound.size());
    return 0;
}
