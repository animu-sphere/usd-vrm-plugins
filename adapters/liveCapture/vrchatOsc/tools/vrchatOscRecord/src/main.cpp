// SPDX-License-Identifier: Apache-2.0
//
// vrchat_osc_record — the tool that obtains the bytes.
//
// Every other recorder in this repository turns a session into a file so that a
// decoder can be *tested*. This one and its mocopi sibling turn a session into a
// file so that a decoder can be *written* — and this one does it for a protocol
// whose receiving end is published, which is the part worth explaining.
//
//     receive -> append to the capture -> report on the envelope
//
// ## Recording still decodes nothing, and the specification is why
//
// The tempting reading is that a published surface makes a decoder cheap enough
// to write first, and report on addresses from the first session. It does not,
// and the reason is a distinction the document cannot settle: a specification
// says what a *receiver* must accept, and what a *sender* sends is a
// measurement. Sony's help pages list `VRChat (OSC)` as a transfer format and
// name VRChat's port; that is a menu entry, and this repository does not infer a
// packet shape from one (osc-and-vrchat-trackers.md §6).
//
// So the *recording* path has no decode step at all, and its report says only
// what a socket can see — how much arrived, from whom, how fast, in how many
// distinct lengths, and which leading bytes every datagram shares. Every number
// in it is a property of the datagram envelope, and none of it would change if
// the payload meant something entirely different.
//
// ## Two modes, and `--inspect` now has one section more
//
// `--inspect` reads a recorded capture and prints the same block, opening no
// socket. It is what makes this tool testable with no sender at all, and it is
// the answer to "is this fixture still what I thought it was" for a capture
// recorded months ago — which is why it prints the capture's own provenance and
// the live path does not.
//
// Since OSC-3 it prints one section the live path does not: the **address
// inventory**, which is VRC-1's measurement (AddressInventory.h). That is not a
// retreat from the paragraph above. What VRC-0 refused was grouping a session by
// addresses a *document* predicted; what this prints is the addresses a sender
// actually sent, read out of the bytes by a decoder that knows OSC's grammar and
// nothing about VRChat. An address nobody expected appears as a row.
//
// It is on the file path and not on the socket path deliberately, and the
// difference is not squeamishness. A recorder's job is to obtain bytes without
// having an opinion about them, so that the file is worth the same whatever the
// decoder later turns out to be wrong about. Reading that file is a separate
// act, and it is repeatable.
//
// ## The sender that is not there yet is the ordinary case
//
// `--silence-timeout` is this tool's own flag and the reason
// `VRM_VRCHAT_OSC_SOURCE_TIMEOUT` has no default threshold one layer down: how
// long a sender may reasonably take to start is a property of the session, and
// this tool is where a session is stated. It reports and keeps listening;
// `--idle-timeout` is the flag that stops.
//
// This layer raises two codes in total: one fatal, and one the receiver
// rate-limits to once per silence episode. There is no flood to protect against,
// so every diagnostic is printed rather than only the fatal ones.
//
// ## Stopping
//
// A recorder that only stops on Ctrl-C cannot be run from a script, and one that
// cannot be interrupted cannot be run by a person. Both exist here, plus a
// duration, an idle timeout, and a datagram bound that is on by default. Every
// session reports which of them ended it — a capture that stopped because a flag
// said so and one that stopped because the socket failed are different sessions,
// and the file cannot tell them apart afterwards.
//
// Interruption is a flag the loop checks, which is the shape the shared
// receiver's bounded wait was built for: a thread parked in `recvfrom` can only
// be woken by closing the socket underneath it, and that races the descriptor's
// reuse.
#include "Options.h"
#include "SessionReport.h"

#include "vrmAdapterVrchatOsc/AddressInventory.h"
#include "vrmAdapterVrchatOsc/Diagnostics.h"
#include "vrmAdapterVrchatOsc/PacketCapture.h"
#include "vrmAdapterVrchatOsc/UdpReceiver.h"

#include <algorithm>
#include <csignal>
#include <cstddef>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

namespace
{

// Set from a signal handler, so it is the one type the standard promises is safe
// to write there. `volatile` for the same reason: the loop below reads it on
// every iteration and nothing in that loop tells a compiler it can change.
volatile std::sig_atomic_t gInterrupted = 0;

extern "C" void
OnInterrupt(int)
{
    gInterrupted = 1;
}

// How long a poll waits. Long enough that an idle recorder is not a spin, short
// enough that Ctrl-C feels immediate and a --duration overshoots by no more than
// this.
constexpr double kPollSeconds = 0.2;

// How often the progress line is rewritten, on the receive clock.
constexpr double kProgressSeconds = 1.0;

// Every diagnostic, recoverable or not. See the header: this layer has two codes
// and the recoverable one is rate-limited to once per episode by the receiver
// that raises it, so a filter would suppress the only mid-session message an
// operator actually waits for.
void
ReportDiagnostics(const std::vector<vrmAdapterVrchatOsc::Diagnostic>& log,
                  bool quiet)
{
    if (quiet) {
        return;
    }
    for (const vrmAdapterVrchatOsc::Diagnostic& diagnostic : log) {
        std::cerr << "vrchat_osc_record: "
                  << vrmAdapterVrchatOsc::FormatDiagnostic(diagnostic) << "\n";
    }
}

// Whether an endpoint this receiver bound is IPv6, from its text.
//
// `UdpReceiver` brackets an IPv6 host to keep its colons apart from the port's,
// so "[::1]:9000" against "127.0.0.1:9000" would be a leading '['. Counting
// colons instead covers one more case for free: when `getsockname` fails the
// receiver falls back to the address the caller *asked* for, which is not
// bracketed.
bool
EndpointIsIpv6(const std::string& endpoint)
{
    return std::count(endpoint.begin(), endpoint.end(), ':') > 1;
}

// VRC-1's measurement, printed after the envelope block rather than inside it.
//
// `SessionReport` is the *live* report and decodes nothing, which is the whole
// of the recording path's design; this is a second pass over a file that has
// already been written, so nothing here can change what was recorded.
//
// One row per address and type tag pair, because a sender that spells one
// address two ways is the finding a table keyed on the address alone would hide.
void
PrintAddressInventory(std::FILE* out,
                      const vrmAdapterVrchatOsc::PacketCapture& capture)
{
    const vrmAdapterVrchatOsc::AddressInventory inventory =
        vrmAdapterVrchatOsc::InventoryAddresses(capture);

    std::fprintf(out, "addresses: %zu (%zu message(s), %zu bundled datagram(s), "
                      "%zu refused)\n",
                 inventory.rows.size(), inventory.messages, inventory.bundled,
                 inventory.refused);
    for (const vrmAdapterVrchatOsc::AddressRow& row : inventory.rows) {
        std::fprintf(out, "  %s ,%s  %zu message(s) in %zu datagram(s)  "
                          "%.6f-%.6f s\n",
                     row.address.c_str(), row.typeTags.c_str(), row.messages,
                     row.datagrams, row.firstTime, row.lastTime);
    }
    // Every refusal, not a count: a session half-refused is one an operator has
    // to be able to read the reason for, and a capture is a bounded file.
    for (const vrmAdapterVrchatOsc::Diagnostic& diagnostic :
         inventory.diagnostics) {
        std::fprintf(out, "  %s\n",
                     vrmAdapterVrchatOsc::FormatDiagnostic(diagnostic).c_str());
    }
}

int
RunInspect(const vrchatOscRecordTool::Options& options)
{
    vrmAdapterVrchatOsc::PacketCapture capture;
    vrmAdapterVrchatOsc::PacketCaptureError captureError;
    if (!vrmAdapterVrchatOsc::ReadPacketCaptureFile(options.inspectPath,
                                                    &capture, &captureError)) {
        std::cerr << "vrchat_osc_record: " << options.inspectPath;
        if (captureError.line != 0) {
            std::cerr << ":" << captureError.line;
        }
        std::cerr << ": " << captureError.message << "\n";
        return 1;
    }

    vrchatOscRecordTool::SessionReport report;
    for (const vrmAdapterVrchatOsc::RecordedDatagram& datagram :
         capture.datagrams) {
        report.ObserveDatagram(capture.peerEndpoint, datagram.bytes.data(),
                               datagram.bytes.size(), datagram.receiveTime);
    }

    // A file has already stopped, and it stopped by ending. None of the live
    // reasons can be true of a replay, so this is not a default being left in
    // place — it is the only reason there is.
    report.SetStopReason(vrchatOscRecordTool::StopReason::EndOfCapture);
    report.Print(stdout, nullptr, &capture);
    PrintAddressInventory(stdout, capture);
    return 0;
}

int
RunRecord(const vrchatOscRecordTool::Options& options)
{
    vrmAdapterVrchatOsc::UdpReceiver receiver;
    std::vector<vrmAdapterVrchatOsc::Diagnostic> log;
    if (!receiver.Open(options.receiver, &log)) {
        for (const vrmAdapterVrchatOsc::Diagnostic& diagnostic : log) {
            std::cerr << "vrchat_osc_record: "
                      << vrmAdapterVrchatOsc::FormatDiagnostic(diagnostic)
                      << "\n";
        }
        return 1;
    }
    log.clear();

    if (!options.quiet) {
        // Before anything is received, and on stderr, because it is the one line
        // a script waiting to start a sender has to read — and because a
        // `--port 0` session cannot be reached until this says where it landed.
        std::cerr << "vrchat_osc_record: listening on "
                  << receiver.GetBoundEndpoint() << "\n";

        // Two facts an operator can act on before a single datagram arrives.
        // Neither is a refusal: the socket is right in both cases, and it is the
        // arrangement around it that may not be.
        //
        // The loopback one is deliberately weaker than the mocopi tool's. There
        // the vendor documents `localhost` as unsupported, so loopback-only is
        // hopeless; here a sender on this machine is an ordinary arrangement, so
        // this says what was bound and stops.
        if (receiver.IsLoopbackOnly()) {
            std::cerr << "vrchat_osc_record: note: loopback only, so only a "
                         "sender on this machine can reach it\n";
        }
        if (EndpointIsIpv6(receiver.GetBoundEndpoint())) {
            std::cerr << "vrchat_osc_record: warning: this is an IPv6 endpoint, "
                         "and a sender configured for VRChat will be aimed at "
                         "an IPv4 address\n";
        }
    }

    vrmAdapterVrchatOsc::PacketCapture capture;
    capture.sender = options.sender;
    capture.device = options.device;
    capture.sourceId = options.sourceId;
    capture.listenEndpoint = receiver.GetBoundEndpoint();

    vrchatOscRecordTool::SessionReport report;
    report.SetStopReason(vrchatOscRecordTool::StopReason::Interrupted);

    vrmAdapterVrchatOsc::ReceivedDatagram datagram;
    double lastArrival = 0.0;
    double lastProgress = 0.0;
    bool running = true;
    while (running) {
        if (gInterrupted != 0) {
            report.SetStopReason(vrchatOscRecordTool::StopReason::Interrupted);
            break;
        }

        const vrmAdapterVrchatOsc::ReceiveStatus status =
            receiver.Receive(&datagram, kPollSeconds, &log);
        switch (status) {
        case vrmAdapterVrchatOsc::ReceiveStatus::Received: {
            // Recorded first. With no decoder in this process the rule costs
            // nothing to keep, and it is the rule the file's whole value rests
            // on: nothing anything here makes of a packet can change what was
            // recorded.
            capture.datagrams.push_back(vrmAdapterVrchatOsc::RecordedDatagram{
                datagram.receiveTime, datagram.bytes});
            if (capture.peerEndpoint.empty()) {
                capture.peerEndpoint = datagram.peer;
            }
            lastArrival = datagram.receiveTime;

            report.ObserveDatagram(datagram.peer, datagram.bytes.data(),
                                   datagram.bytes.size(),
                                   datagram.receiveTime);

            if (report.GetDatagramCount() >= options.maxDatagrams) {
                report.SetStopReason(
                    vrchatOscRecordTool::StopReason::MaxDatagrams);
                running = false;
            }
            break;
        }
        case vrmAdapterVrchatOsc::ReceiveStatus::Idle:
            break;
        case vrmAdapterVrchatOsc::ReceiveStatus::Closed:
            // Not folded into the arm below. `Closed` means no socket is open,
            // which is a different fact from a socket that reported an error —
            // and `GetLastErrorText()` is documented as empty until something
            // fails, so a shared message would print "the socket failed:" with
            // nothing after the colon.
            std::cerr << "vrchat_osc_record: the socket is no longer open\n";
            report.SetStopReason(
                vrchatOscRecordTool::StopReason::SocketClosed);
            running = false;
            break;
        case vrmAdapterVrchatOsc::ReceiveStatus::Failed:
            std::cerr << "vrchat_osc_record: the socket failed: "
                      << receiver.GetLastErrorText() << "\n";
            report.SetStopReason(
                vrchatOscRecordTool::StopReason::ReceiveFailed);
            running = false;
            break;
        }

        // Whatever the status was: a silence report is appended by a call that
        // received nothing, which is the majority of the calls a session waiting
        // for a sender makes.
        report.ObserveDiagnostics(log);
        ReportDiagnostics(log, options.quiet);
        // The list is a session's worth of history nobody reads twice: the
        // report has counted these and kept the first of each code.
        log.clear();

        // `Now()` rather than the last datagram's stamp: a session that stops
        // receiving still has to notice its own duration passing.
        const double now = receiver.Now();
        if (running && options.durationSeconds > 0.0
            && now >= options.durationSeconds) {
            report.SetStopReason(vrchatOscRecordTool::StopReason::Duration);
            running = false;
        }
        if (running && options.idleSeconds > 0.0
            && now - lastArrival >= options.idleSeconds) {
            // Measured from `Open` until the first datagram, so a sender that
            // never starts times out exactly as one that stops does.
            report.SetStopReason(vrchatOscRecordTool::StopReason::IdleTimeout);
            running = false;
        }

        if (!options.quiet && now - lastProgress >= kProgressSeconds) {
            lastProgress = now;
            std::fprintf(stderr,
                         "vrchat_osc_record: %6.1f s  %llu datagram(s)\n", now,
                         static_cast<unsigned long long>(
                             report.GetDatagramCount()));
        }
    }

    // The receiver is deliberately not closed here. `Close` clears the bound
    // endpoint, the loopback flag and the granted buffer size, which are three
    // of the facts the report is about to print — and the socket's own
    // destructor releases it a few lines later anyway.

    if (!options.quiet && report.HasMultiplePeers()) {
        // The capture header names one peer, so a mixed session's provenance is
        // true of some of its datagrams and not the rest. Worth an operator's
        // attention before the file becomes a fixture — and more likely here
        // than on a native wire, because this port is a well-known one that
        // several applications on a machine may be sending to.
        std::cerr << "vrchat_osc_record: warning: datagrams arrived from more "
                     "than one peer; the capture header names only "
                  << capture.peerEndpoint << "\n";
    }
    if (!options.quiet && report.GetDatagramCount() == 0) {
        std::cerr << "vrchat_osc_record: warning: nothing arrived\n";
    }

    // The write can fail, and the report is printed either way. A session that
    // has just run for ten minutes exists in exactly two places — the file and
    // this report — and returning early on a full disk would destroy both at
    // once, which is the moment an operator most needs to be told what they had.
    bool written = true;
    if (!options.dryRun) {
        if (report.GetDatagramCount() == 0) {
            // Declined rather than written, because the format has no
            // datagram-less form: the writer will happily emit a header and
            // stop, and the reader refuses the result at "the capture carries no
            // datagrams". Leaving that file on disk would hand an operator an
            // artifact this adapter's own reader rejects, and they would find
            // out at the point they tried to use it. Said on stderr whatever
            // `--quiet` says: it is the reason for a non-zero exit, not a
            // warning about the session.
            std::cerr << "vrchat_osc_record: nothing arrived, so "
                      << options.outputPath
                      << " was not written: a capture carrying no datagrams is "
                         "one this adapter's reader refuses\n";
            written = false;
        } else {
            written = vrmAdapterVrchatOsc::WritePacketCaptureFile(
                options.outputPath, capture);
            if (!written) {
                std::cerr << "vrchat_osc_record: could not write "
                          << options.outputPath << "\n";
            } else if (!options.quiet) {
                std::cerr << "vrchat_osc_record: wrote "
                          << capture.datagrams.size() << " datagram(s) to "
                          << options.outputPath << "\n";

                // Said at the write, because this is the last moment it is
                // cheap. The corpus check refuses a committed fixture carrying
                // neither a `sender` nor a `sourceId`, and the operator who can
                // still supply them is the one who just ran the session — half
                // an hour later the answer is a guess, and a guessed provenance
                // is worse than an absent one. A warning and not a refusal: an
                // exploratory recording is a legitimate thing to want, and the
                // first session against a new sender is exactly that.
                std::string missing;
                if (capture.sender.empty()) {
                    missing += " --sender";
                }
                if (capture.sourceId.empty()) {
                    missing += " --source-id";
                }
                if (!missing.empty()) {
                    std::cerr << "vrchat_osc_record: warning: no" << missing
                              << ", which the corpus check requires of a "
                                 "committed fixture\n";
                }
                if (capture.device.empty()) {
                    // Not required by that check, and named separately for a
                    // reason this wire has and the native one does not: a
                    // VRChat OSC stream is relayed, so `sender` names the
                    // application that re-expressed somebody else's tracking
                    // and `device` is the only place the thing that was
                    // actually measured can appear.
                    std::cerr << "vrchat_osc_record: warning: no --device, so "
                                 "this capture names the application that sent "
                                 "it and not what produced the tracking\n";
                }
            }
        }
    }

    report.Print(stdout, &receiver, nullptr);

    // A session the transport cut short exits non-zero even though its datagrams
    // were written, and the file is still there to be used. The alternative was
    // exit 0 with the distinction surviving only as prose on stdout — which is
    // no distinction at all to the script that wrapped this tool.
    const bool completed =
        report.GetStopReason() != vrchatOscRecordTool::StopReason::ReceiveFailed
        && report.GetStopReason()
            != vrchatOscRecordTool::StopReason::SocketClosed;
    return written && completed ? 0 : 1;
}

} // namespace

int
main(int argc, char** argv)
{
    const std::vector<std::string> arguments(argv + 1, argv + argc);

    vrchatOscRecordTool::Options options;
    bool showHelp = false;
    std::string error;
    if (!vrchatOscRecordTool::ParseOptions(arguments, &options, &showHelp,
                                           &error)) {
        std::cerr << "vrchat_osc_record: " << error << "\n\n"
                  << vrchatOscRecordTool::GetUsage();
        return 2;
    }
    if (showHelp) {
        std::fputs(vrchatOscRecordTool::GetUsage(), stdout);
        return 0;
    }

    if (!options.inspectPath.empty()) {
        return RunInspect(options);
    }

    std::signal(SIGINT, OnInterrupt);
    std::signal(SIGTERM, OnInterrupt);
    return RunRecord(options);
}
