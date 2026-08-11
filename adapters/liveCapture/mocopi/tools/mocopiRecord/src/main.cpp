// SPDX-License-Identifier: Apache-2.0
//
// mocopi_record — the tool that obtains the bytes.
//
// Every other recorder in this repository turns a session into a file so that a
// decoder can be *tested*. This one turns a session into a file so that a
// decoder can be *written*. That is the whole difference, and it comes from the
// protocol: the vendor documents the transport and stops, so there is no
// specification to write a corpus from and exactly one way to obtain one without
// guessing (roadmap Milestone D, UdpReceiver.h). The receiver landed first for
// that reason; this is the consumer it was waiting for.
//
// ## Nothing here decodes anything, and that is not a stage of completion
//
//     receive -> append to the capture -> report on the envelope
//
// The sibling tool has a decode step in that chain and a rule about where it
// sits — the datagram reaches the file before the decoder sees it, so nothing the
// decoder makes of a packet can change what was recorded. This tool has no
// decode step at all, which makes the rule trivially true and worth restating for
// the opposite reason: the temptation here is not to let a decoder filter the
// recording, it is to write a decoder *inside the recorder* because the operator
// is standing there wanting to know whether the session was any good.
//
// What answers that question instead is `SessionReport`, and the line it holds
// is argued in its own header: every number it prints is a property of the
// datagram envelope — how many arrived, how long each was, which leading bytes
// they all share — and none of it reads a field. A guess about a field would go
// straight into the provenance of a committed fixture, which is the one place a
// guess survives longest and is questioned least.
//
// ## Two modes, one report
//
// `--inspect` reads a recorded capture and prints the same block, opening no
// socket. It is what makes this tool testable with no device and no sender at
// all, and it is the answer to "is this fixture still what I thought it was" for
// a capture recorded months ago — which is why it prints the capture's own
// provenance and the live path does not.
//
// ## The device that is not there yet is the ordinary case
//
// `--silence-timeout` is this tool's own flag and the reason
// `VRM_MOCOPI_DEVICE_UNAVAILABLE` has no default threshold one layer down: how
// long a device may reasonably take to start is a property of the session, and
// this tool is where a session is stated. An operator strapping on a phone
// wants the message; a script waiting on a device that was switched off wants it
// too, and neither wants the recording to end because of it. So it reports and
// keeps listening, and `--idle-timeout` remains the flag that stops.
//
// That also settles what reaches stderr mid-session, where this tool differs
// from its sibling deliberately. The sibling prints only the diagnostics a
// session cannot continue past, because a 30 Hz sender missing one bone would
// otherwise write a thousand recoverable lines a minute over the progress line.
// This layer raises two codes in total: one fatal, and one the receiver
// rate-limits to once per silence episode. There is no flood to protect against,
// and the recoverable one is the single most useful thing an operator waiting on
// a device can be told — so every diagnostic is printed.
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
// Interruption is a flag the loop checks, which is the shape `UdpReceiver`'s
// bounded wait was built for: a thread parked in `recvfrom` can only be woken by
// closing the socket underneath it, and that races the descriptor's reuse.
#include "Options.h"
#include "SessionReport.h"

#include "vrmAdapterMocopi/Diagnostics.h"
#include "vrmAdapterMocopi/PacketCapture.h"
#include "vrmAdapterMocopi/UdpReceiver.h"

#include <algorithm>
#include <csignal>
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
// that raises it, so the sibling's filter would suppress the only mid-session
// message an operator actually waits for.
void
ReportDiagnostics(const std::vector<vrmAdapterMocopi::Diagnostic>& log,
                  bool quiet)
{
    if (quiet) {
        return;
    }
    for (const vrmAdapterMocopi::Diagnostic& diagnostic : log) {
        std::cerr << "mocopi_record: "
                  << vrmAdapterMocopi::FormatDiagnostic(diagnostic) << "\n";
    }
}

// Whether an endpoint this receiver bound is IPv6, from its text.
//
// `UdpReceiver` brackets an IPv6 host to keep its colons apart from the port's,
// so "[::1]:12351" against "127.0.0.1:12351" would be a leading '['. Counting
// colons instead covers one more case for free: when `getsockname` fails the
// receiver falls back to the address the caller *asked* for, which is not
// bracketed. Either way, two colons in an endpoint means IPv6 and one means
// IPv4.
bool
EndpointIsIpv6(const std::string& endpoint)
{
    return std::count(endpoint.begin(), endpoint.end(), ':') > 1;
}

int
RunInspect(const mocopiRecordTool::Options& options)
{
    vrmAdapterMocopi::PacketCapture capture;
    vrmAdapterMocopi::PacketCaptureError captureError;
    if (!vrmAdapterMocopi::ReadPacketCaptureFile(options.inspectPath, &capture,
                                                 &captureError)) {
        std::cerr << "mocopi_record: " << options.inspectPath;
        if (captureError.line != 0) {
            std::cerr << ":" << captureError.line;
        }
        std::cerr << ": " << captureError.message << "\n";
        return 1;
    }

    mocopiRecordTool::SessionReport report;
    for (const vrmAdapterMocopi::RecordedDatagram& datagram :
         capture.datagrams) {
        report.ObserveDatagram(capture.peerEndpoint, datagram.bytes.data(),
                               datagram.bytes.size(), datagram.receiveTime);
    }

    // A file has already stopped, and it stopped by ending. None of the live
    // reasons can be true of a replay, so this is not a default being left in
    // place — it is the only reason there is.
    report.SetStopReason(mocopiRecordTool::StopReason::EndOfCapture);
    report.Print(stdout, nullptr, &capture);
    return 0;
}

int
RunRecord(const mocopiRecordTool::Options& options)
{
    vrmAdapterMocopi::UdpReceiver receiver;
    std::vector<vrmAdapterMocopi::Diagnostic> log;
    if (!receiver.Open(options.receiver, &log)) {
        for (const vrmAdapterMocopi::Diagnostic& diagnostic : log) {
            std::cerr << "mocopi_record: "
                      << vrmAdapterMocopi::FormatDiagnostic(diagnostic) << "\n";
        }
        return 1;
    }
    log.clear();

    if (!options.quiet) {
        // Before anything is received, and on stderr, because it is the one line
        // a script waiting to start a source has to read — and because a
        // `--port 0` session cannot be reached until this says where it landed.
        std::cerr << "mocopi_record: listening on "
                  << receiver.GetBoundEndpoint() << "\n";

        // The two ways a bind can be right for the socket and useless for this
        // product, warned about *now* rather than in the report. Both are
        // knowable before a single datagram, and an operator who learns after
        // ten minutes that nothing could ever have arrived has lost the ten
        // minutes. Both are also the vendor's statements rather than this
        // tool's inferences, which is why they live here and not in
        // `UdpReceiver`: refusing an address because a *product* does not send
        // to it would be a socket inventing a restriction on itself.
        if (receiver.IsLoopbackOnly()) {
            std::cerr << "mocopi_record: warning: loopback only, which no "
                         "device can reach; the vendor documents 'localhost' "
                         "as an unsupported destination\n";
        }
        if (EndpointIsIpv6(receiver.GetBoundEndpoint())) {
            std::cerr << "mocopi_record: warning: this is an IPv6 endpoint and "
                         "the product sends to IPv4 only\n";
        }
    }

    vrmAdapterMocopi::PacketCapture capture;
    capture.sender = options.sender;
    capture.device = options.device;
    capture.sourceId = options.sourceId;
    capture.listenEndpoint = receiver.GetBoundEndpoint();

    mocopiRecordTool::SessionReport report;
    report.SetStopReason(mocopiRecordTool::StopReason::Interrupted);

    vrmAdapterMocopi::ReceivedDatagram datagram;
    double lastArrival = 0.0;
    double lastProgress = 0.0;
    bool running = true;
    while (running) {
        if (gInterrupted != 0) {
            report.SetStopReason(mocopiRecordTool::StopReason::Interrupted);
            break;
        }

        const vrmAdapterMocopi::ReceiveStatus status =
            receiver.Receive(&datagram, kPollSeconds, &log);
        switch (status) {
        case vrmAdapterMocopi::ReceiveStatus::Received: {
            // Recorded first. See the header: with no decoder in this process
            // the rule costs nothing to keep, and it is the rule the file's
            // whole value rests on.
            capture.datagrams.push_back(vrmAdapterMocopi::RecordedDatagram{
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
                    mocopiRecordTool::StopReason::MaxDatagrams);
                running = false;
            }
            break;
        }
        case vrmAdapterMocopi::ReceiveStatus::Idle:
            break;
        case vrmAdapterMocopi::ReceiveStatus::Closed:
            // Not folded into the arm below. `Closed` means no socket is open,
            // which is a different fact from a socket that reported an error —
            // and `GetLastErrorText()` is documented as empty until something
            // fails, so the shared message printed "the socket failed:" with
            // nothing after the colon for a socket that never said so.
            std::cerr << "mocopi_record: the socket is no longer open\n";
            report.SetStopReason(mocopiRecordTool::StopReason::SocketClosed);
            running = false;
            break;
        case vrmAdapterMocopi::ReceiveStatus::Failed:
            std::cerr << "mocopi_record: the socket failed: "
                      << receiver.GetLastErrorText() << "\n";
            report.SetStopReason(mocopiRecordTool::StopReason::ReceiveFailed);
            running = false;
            break;
        }

        // Whatever the status was: a silence report is appended by a call that
        // received nothing, which is the majority of the calls a session waiting
        // for a device makes.
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
            report.SetStopReason(mocopiRecordTool::StopReason::Duration);
            running = false;
        }
        if (running && options.idleSeconds > 0.0
            && now - lastArrival >= options.idleSeconds) {
            // Measured from `Open` until the first datagram, so a source that
            // never starts times out exactly as one that stops does.
            report.SetStopReason(mocopiRecordTool::StopReason::IdleTimeout);
            running = false;
        }

        if (!options.quiet && now - lastProgress >= kProgressSeconds) {
            lastProgress = now;
            std::fprintf(stderr,
                         "mocopi_record: %6.1f s  %llu datagram(s)\n", now,
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
        // attention before the file becomes a fixture.
        std::cerr << "mocopi_record: warning: datagrams arrived from more than "
                     "one peer; the capture header names only "
                  << capture.peerEndpoint << "\n";
    }
    if (!options.quiet && report.GetDatagramCount() == 0) {
        std::cerr << "mocopi_record: warning: nothing arrived\n";
    }

    // The write can fail, and the report is printed either way. A session that
    // has just run for ten minutes exists in exactly two places — the file and
    // this report — and returning early on a full disk would destroy both at
    // once, which is the moment an operator most needs to be told what they had.
    bool written = true;
    if (!options.dryRun) {
        if (report.GetDatagramCount() == 0) {
            // Declined rather than written, because the format has no
            // datagram-less form: `WritePacketCapture` will happily emit a
            // header and stop, and `ReadPacketCapture` refuses the result at
            // "the capture carries no datagrams". Leaving that file on disk
            // would hand an operator an artifact this adapter's own reader
            // rejects, and they would find out at the point they tried to use
            // it. Said on stderr whatever `--quiet` says: it is the reason for
            // a non-zero exit, not a warning about the session.
            std::cerr << "mocopi_record: nothing arrived, so "
                      << options.outputPath
                      << " was not written: a capture carrying no datagrams is "
                         "one this adapter's reader refuses\n";
            written = false;
        } else {
            written = vrmAdapterMocopi::WritePacketCaptureFile(
                options.outputPath, capture);
            if (!written) {
                std::cerr << "mocopi_record: could not write "
                          << options.outputPath << "\n";
            } else if (!options.quiet) {
                std::cerr << "mocopi_record: wrote " << capture.datagrams.size()
                          << " datagram(s) to " << options.outputPath << "\n";
            }
        }
    }

    report.Print(stdout, &receiver, nullptr);

    // A session the transport cut short exits non-zero even though its datagrams
    // were written, and the file is still there to be used. The alternative was
    // exit 0 with the distinction surviving only as prose on stdout — which is no
    // distinction at all to the script that wrapped this tool, and this file's own
    // header says a capture cannot tell the two apart afterwards. So the one
    // machine-readable channel carries it.
    const bool completed =
        report.GetStopReason() != mocopiRecordTool::StopReason::ReceiveFailed
        && report.GetStopReason() != mocopiRecordTool::StopReason::SocketClosed;
    return written && completed ? 0 : 1;
}

} // namespace

int
main(int argc, char** argv)
{
    const std::vector<std::string> arguments(argv + 1, argv + argc);

    mocopiRecordTool::Options options;
    bool showHelp = false;
    std::string error;
    if (!mocopiRecordTool::ParseOptions(arguments, &options, &showHelp,
                                        &error)) {
        std::cerr << "mocopi_record: " << error << "\n\n"
                  << mocopiRecordTool::GetUsage();
        return 2;
    }
    if (showHelp) {
        std::fputs(mocopiRecordTool::GetUsage(), stdout);
        return 0;
    }

    if (!options.inspectPath.empty()) {
        return RunInspect(options);
    }

    std::signal(SIGINT, OnInterrupt);
    std::signal(SIGTERM, OnInterrupt);
    return RunRecord(options);
}
