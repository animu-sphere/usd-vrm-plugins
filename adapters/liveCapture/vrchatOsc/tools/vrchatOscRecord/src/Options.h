// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "vrmAdapterVrchatOsc/UdpReceiver.h"

#include "motionTracking/TrackerAssignment.h"
#include "motionTracking/TrackerSolve.h"

#include <cstddef>
#include <string>
#include <vector>

namespace vrchatOscRecordTool
{

struct Options
{
    // The socket. `UdpReceiverConfig` is taken whole rather than copied field by
    // field, so a receiver setting added there reaches this tool by being parsed
    // rather than by being re-declared.
    vrmAdapterVrchatOsc::UdpReceiverConfig receiver;

    // The capture to write. Empty with --inspect or --dry-run.
    std::string outputPath;

    // Read a recorded capture and report on it, opening no socket. The other
    // half of this tool: the same report, from a file instead of a wire.
    std::string inspectPath;

    // Provenance for the capture's header, operator-supplied throughout. Not one
    // of these can be taken from the traffic: nothing in this repository reads a
    // VRChat OSC datagram yet, so a `device` inferred from a payload would be a
    // guess written into a fixture's provenance -- which is the one place a guess
    // survives longest and is questioned least.
    //
    // `device` is asked for more insistently here than in either sibling tool,
    // and the reason is the protocol rather than the format. This wire is
    // *relayed*: the sender is an application re-expressing some other device's
    // tracking, so `sender` alone names the relay and not the thing that was
    // measured.
    std::string sender;
    std::string device;
    std::string sourceId;

    // The canonical trace to derive from `inspectPath`, and the operator's
    // statement that makes deriving one possible.
    //
    // **The assignment is required, and it is the one flag in this tool that
    // cannot be defaulted.** A tracker index is not a body role
    // (osc-and-vrchat-trackers.md §5.1), so there is no reading of
    // `/tracking/trackers/1` that this tool is entitled to pick — and a default
    // would be a calibration invented by a decoder, which is the thing the
    // whole tracker path is arranged to prevent. It is parsed here rather than
    // in the export so that a spelling mistake is refused at the prompt, before
    // a capture is read.
    //
    // `solve` is the third decision's own configuration and carries exactly one
    // field today: whether an observed hips position becomes root motion.
    std::string traceExportPath;
    motionTracking::TrackerAssignmentSpec assignment;
    motionTracking::TrackerSolveConfig solve;
    // Which of the capture's sessions to export, counting from 1. 0 is "the
    // capture holds one", and a capture that holds more is refused rather than
    // resolved -- the sibling tools' rule, for their reason (TraceExport.h).
    std::size_t sourceSession = 0;

    // There is no flag for `TrackerFrameConfig`, and the absence is a decision.
    // Every one of its four thresholds was measured off the 2026-08-30 session
    // (FrameAssembler.h), so a flag would offer an operator the chance to
    // replace a measurement with a guess — and the frame window in particular
    // decides *what a frame is*, which decides what the trace holds. A sender
    // whose cadence those numbers do not fit is a re-measurement, not a
    // command line.

    // Stop conditions. A recorder with none is a process that never exits, so
    // there is always at least one: `maxDatagrams` has a default and the other
    // two are off until asked for.
    //
    // There is still no `--max-frames` here, and the export is why the sentence
    // needed rewriting rather than deleting. The sibling tools carry that bound
    // because they accumulate poses *during a recording*; this tool's export
    // runs against a file that is already bounded by the datagram count that
    // wrote it, so the second unit has nothing to bound.
    double durationSeconds = 0.0;  // 0: until interrupted
    double idleSeconds = 0.0;      // 0: never
    std::size_t maxDatagrams = 0;  // 0: the default, applied at parse

    bool dryRun = false;

    // Silences the progress line and the warnings on stderr. Not the report:
    // that is what this tool produces, so a flag to suppress it would be a flag
    // to run it for nothing.
    bool quiet = false;
};

// Parses argv. On failure `error` explains why and the result is false; on
// --help `showHelp` is set and the caller should print usage and exit 0.
bool ParseOptions(const std::vector<std::string>& arguments, Options* options,
                  bool* showHelp, std::string* error);

const char* GetUsage();

} // namespace vrchatOscRecordTool
