// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "motionRuntime/LiveCaptureSource.h"

#include <string>
#include <vector>

namespace motionCaptureTool
{

struct Options
{
    // The recorded session to drive. Today this is the only source; a live
    // adapter (motion policy §8.2) becomes a second flag, not a second tool.
    std::string tracePath;

    // Semantic humanoid clip to author. Empty with --normalize or --dry-run.
    std::string outputPath;

    // Rewrite the trace in canonical form and exit. Used to normalise a
    // hand-written fixture into what motionRuntime's writer emits.
    std::string normalizePath;

    // Evaluation tick rate. Zero means "the trace's own nominal rate".
    double evaluationRate = 0.0;

    // How far behind each evaluation tick a frame is delivered. This is the
    // knob that makes a replay behave like a real session: raise it and the
    // consumer starts holding and extrapolating, exactly as it would when the
    // transport falls behind.
    double deliveryLag = 0.0;

    motion::LiveCaptureConfig capture;

    // Prim name for the authored UsdSkelAnimation.
    std::string clipName = "BodyAnimation";

    bool report = false;
    bool dryRun = false;
    bool quiet = false;
};

// Parses argv. On failure `error` explains why and the result is false; on
// --help `showHelp` is set and the caller should print usage and exit 0.
bool ParseOptions(const std::vector<std::string>& arguments, Options* options,
                  bool* showHelp, std::string* error);

const char* GetUsage();

} // namespace motionCaptureTool
