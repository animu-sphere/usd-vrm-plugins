// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "vrmAdapterVmc/FrameAssembler.h"
#include "vrmAdapterVmc/UdpReceiver.h"

#include <cstddef>
#include <string>
#include <vector>

namespace vmcRecordTool
{

struct Options
{
    // The socket. `UdpReceiverConfig` is taken whole rather than copied field
    // by field, so a receiver setting added there reaches this tool by being
    // parsed rather than by being re-declared.
    vrmAdapterVmc::UdpReceiverConfig receiver;

    // The capture to write. Empty with --inspect or --dry-run.
    std::string outputPath;

    // Decode a recorded capture and report on it, opening no socket. The other
    // half of this tool: the same report, from a file instead of a wire.
    std::string inspectPath;

    // The canonical trace to write beside the report: what the adapter
    // delivered, in the format `motion_capture` replays (TraceExport.h). It
    // belongs to neither mode in particular -- a live session and a recorded
    // capture deliver the same frames -- which is why it is not one of the
    // session-only flags `--inspect` refuses.
    std::string traceExportPath;

    // Which session to export, 1-based. Zero means "the only one", and a
    // capture holding more than one is refused rather than guessed at: the
    // sessions in it are separate recordings that happen to share a file.
    std::size_t traceSession = 0;

    // Provenance for the capture's header, and operator-supplied on purpose --
    // see main.cpp on why the sender's model title is never borrowed for it.
    std::string sender;
    std::string sourceId;

    // What the report calls stale, and what it calls a restart. The only two
    // settings of the decode path this tool exposes, because they are the only
    // two that change the *reading* of a session rather than what is recorded.
    vrmAdapterVmc::VmcFrameConfig frame;

    // Stop conditions. A recorder with none is a process that never exits, so
    // there is always at least one: `maxDatagrams` has a default and the other
    // two are off until asked for.
    double durationSeconds = 0.0;  // 0: until interrupted
    double idleSeconds = 0.0;      // 0: never
    std::size_t maxDatagrams = 0;  // 0: the default below, applied at parse

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

} // namespace vmcRecordTool
