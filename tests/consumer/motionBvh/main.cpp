// SPDX-License-Identifier: Apache-2.0
//
// Includes one public header of the installed `motionBvh` package and calls
// into it. The include proves the package installed its header root; the calls
// prove it installed something to link.
//
// `BvhExtract.h` is the header that carries this package's one declared edge
// into a consumer's translation unit -- the two headers it includes from the
// layer below are what a config that forgot its `find_dependency` would fail
// on, at the first `#include` rather than at the link. It also reaches this
// package's other three public headers, so a prefix that installed some of them
// fails there too.
//
// This is deliberately not a test of the parser or the extractor.
// `libs/motionBvh/tests/` owns the malformed-hierarchy refusals, the channel
// tables and the frame arithmetic; duplicating any of it here would make a
// packaging failure look like a parser failure the first time this fixture went
// red. What this asks is only: does a hand-written document parse, and does the
// rig that comes back out carry a type from a package this fixture never names.
#include <motionBvh/BvhExtract.h>
#include <motionBvh/BvhParser.h>

#include <cstdio>
#include <string>
#include <string_view>

namespace
{

// Two joints, one end site, two frames -- the smallest document that has a
// hierarchy to walk and a motion section to count, written here so the fixture
// carries no corpus file.
constexpr std::string_view kMinimal =
    "HIERARCHY\n"
    "ROOT Hips\n"
    "{\n"
    "\tOFFSET 0.0 0.0 0.0\n"
    "\tCHANNELS 6 Xposition Yposition Zposition Zrotation Xrotation Yrotation\n"
    "\tJOINT Spine\n"
    "\t{\n"
    "\t\tOFFSET 0.0 10.5 0.0\n"
    "\t\tCHANNELS 3 Zrotation Xrotation Yrotation\n"
    "\t\tEnd Site\n"
    "\t\t{\n"
    "\t\t\tOFFSET 0.0 5.25 0.0\n"
    "\t\t}\n"
    "\t}\n"
    "}\n"
    "MOTION\n"
    "Frames: 2\n"
    "Frame Time: 0.5\n"
    "0.0 90.0 0.0 1.0 2.0 3.0 4.0 5.0 6.0\n"
    "0.5 90.0 0.0 7.0 8.0 9.0 10.0 11.0 12.0\n";

} // namespace

int
main()
{
    motionBvh::BvhDocument document;
    motionBvh::Diagnostic diagnostic;
    if (!motionBvh::ParseBvhText(kMinimal, &document, &diagnostic)) {
        std::fprintf(stderr, "consumer: parse refused: %s\n",
                     diagnostic.detail.c_str());
        return 1;
    }

    // The extraction is the call that crosses the boundary: both outputs are
    // types from the layer below, which this fixture never names as a package.
    motionSource::SourceSkeleton skeleton;
    motionSource::SourceAnimation animation;
    if (!motionBvh::ExtractBvhSource(document, &skeleton, &animation,
                                     &diagnostic)) {
        std::fprintf(stderr, "consumer: extraction refused: %s\n",
                     diagnostic.detail.c_str());
        return 1;
    }

    if (skeleton.joints.size() != 2 || animation.frameCount != 2
        || animation.tracks.size() != skeleton.joints.size()) {
        std::fprintf(stderr, "consumer: extracted %zu joints and %zu frames\n",
                     skeleton.joints.size(), animation.frameCount);
        return 1;
    }
    if (!skeleton.FindJoint("Spine")) {
        std::fprintf(stderr, "consumer: the extracted rig has no Spine\n");
        return 1;
    }

    std::fprintf(stdout, "consumer: extracted %zu joints as %s through the "
                         "installed package\n",
                 skeleton.joints.size(),
                 std::string(motionBvh::BvhFormatLabel()).c_str());
    return 0;
}
