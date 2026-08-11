// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "vrmAdapterMocopi/UdpReceiver.h"

#include <cstddef>
#include <string>
#include <vector>

namespace mocopiRecordTool
{

struct Options
{
    // The socket. `UdpReceiverConfig` is taken whole rather than copied field
    // by field, so a receiver setting added there reaches this tool by being
    // parsed rather than by being re-declared.
    vrmAdapterMocopi::UdpReceiverConfig receiver;

    // The capture to write. Empty with --inspect or --dry-run.
    std::string outputPath;

    // Read a recorded capture and report on it, opening no socket. The other
    // half of this tool: the same report, from a file instead of a wire.
    //
    // It carries none of the sibling tool's decode settings, because there is
    // nothing here to configure the reading of — this tool reports on bytes, and
    // a byte has no staleness policy.
    std::string inspectPath;

    // Provenance for the capture's header, operator-supplied throughout. Not one
    // of these can be taken from the traffic: nothing in this repository can yet
    // read a mocopi packet, so a `device` inferred from a payload would be a
    // guess written into a fixture's provenance — which is the one place a guess
    // survives longest and is questioned least.
    std::string sender;
    std::string device;
    std::string sourceId;

    // Stop conditions. A recorder with none is a process that never exits, so
    // there is always at least one: `maxDatagrams` has a default and the other
    // two are off until asked for.
    //
    // There is no `--max-frames` here and there cannot be. The sibling needs a
    // second bound because it accumulates a second thing — one pose per decoded
    // frame — and this tool accumulates datagrams alone.
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

} // namespace mocopiRecordTool
