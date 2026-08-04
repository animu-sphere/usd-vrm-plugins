// SPDX-License-Identifier: Apache-2.0
//
// motion_bvh_inspect — what a BVH file contains, and nothing about whose it is.
//
// This is the first thing anyone runs against a file the pipeline has never
// seen, and its whole job is to answer that with the file's own words. It names
// no producer, guesses no unit, labels no axis and maps no joint to a
// `HumanBone`, because a BVH file states none of those — they are facts about
// the application that wrote it (roadmap/recorded-motion-sources.md §2).
//
// **Reporting a producer's candidate profiles is the other half of this tool,
// and it lands with the profiles.** WORKSPACE.md §1 describes it as reporting
// what a file contains *and optionally* which profiles are candidates for it,
// with reasons; there is no profile contract yet, so there is nothing to be a
// candidate. Adding a detector before the contract exists would settle the
// profile schema on whichever file was inspected first, which is exactly the
// order BVH-0 is shaped to prevent.
//
// **A refusal is a report too.** The parser reads a file whole or not at all,
// so a refused file produces one diagnostic line on stderr and exit 1 rather
// than a partial summary — a half-printed hierarchy would read as a fact about
// the file rather than as the point the reader gave up.
#include "Options.h"
#include "Report.h"

#include "motionBvh/BvhDocument.h"
#include "motionBvh/BvhParser.h"
#include "motionBvh/Diagnostics.h"

#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

int
main(int argc, char** argv)
{
    const std::vector<std::string> arguments(argv + 1, argv + argc);

    motionBvhTool::Options options;
    bool showHelp = false;
    std::string error;
    if (!motionBvhTool::ParseOptions(arguments, &options, &showHelp, &error)) {
        std::cerr << "motion_bvh_inspect: " << error << "\n\n"
                  << motionBvhTool::GetUsage();
        return 2;
    }
    if (showHelp) {
        std::fputs(motionBvhTool::GetUsage(), stdout);
        return 0;
    }

    motionBvh::BvhParseOptions parseOptions;
    parseOptions.limits = options.limits;
    // Left empty on purpose: `ParseBvhFile` fills it with the path it opened,
    // so the diagnostic names the file the parser actually read rather than a
    // second spelling of it assembled here.

    motionBvh::BvhDocument document;
    motionBvh::Diagnostic diagnostic;
    if (!motionBvh::ParseBvhFile(options.inputPath, &document, &diagnostic,
                                 parseOptions)) {
        std::cerr << "motion_bvh_inspect: "
                  << motionBvh::FormatDiagnostic(diagnostic) << "\n";
        return 1;
    }

    // Checked here rather than in ParseOptions, because the file's frame count
    // is what makes the request wrong and no one knows it until the parse has
    // run. It is still a wrong command rather than a bad file: exit 2.
    if (options.frame && *options.frame >= document.frameCount) {
        std::cerr << "motion_bvh_inspect: --frame " << *options.frame
                  << " is out of range; the file carries "
                  << document.frameCount << " frame(s)\n";
        return 2;
    }

    // One fixed order, so two runs over the same file are byte-identical and a
    // reader learns where to look: what the file is, then its shape, then how
    // to read a row, then a row, then what the rows do.
    motionBvhTool::PrintSummary(std::cout, document, options.inputPath);
    if (options.hierarchy) {
        std::cout << "\n";
        motionBvhTool::PrintHierarchy(std::cout, document);
    }
    if (options.channelMap) {
        std::cout << "\n";
        motionBvhTool::PrintChannelMap(std::cout, document);
    }
    if (options.frame) {
        std::cout << "\n";
        motionBvhTool::PrintFrame(std::cout, document, *options.frame);
    }
    if (options.ranges) {
        std::cout << "\n";
        motionBvhTool::PrintChannelRanges(std::cout, document);
    }
    return 0;
}
