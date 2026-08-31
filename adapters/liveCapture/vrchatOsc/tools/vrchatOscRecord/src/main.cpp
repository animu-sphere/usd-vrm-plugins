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
#include "TraceExport.h"

#include "vrmAdapterVrchatOsc/AddressInventory.h"
#include "vrmAdapterVrchatOsc/Diagnostics.h"
#include "vrmAdapterVrchatOsc/FrameAssembler.h"
#include "vrmAdapterVrchatOsc/PacketCapture.h"
#include "vrmAdapterVrchatOsc/TrackerMessage.h"
#include "vrmAdapterVrchatOsc/UdpReceiver.h"

#include "motionCore/Humanoid.h"
#include "motionRuntime/CaptureTrace.h"

#include <algorithm>
#include <csignal>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <system_error>
#include <utility>
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

// Decodes a capture, solves each frame against the operator's statement, and
// writes the canonical trace `--export-trace` asked for. Returns false when
// nothing could be written, having said why; the caller prints its report
// either way, for the reason `RunRecord` gives about the capture file.
//
// **This runs a second pass over the same datagrams, and the repetition is the
// point.** The envelope report and the address inventory are derived from bytes
// alone, so nothing a decoder or a solve makes of a packet can move a number in
// either — the rule this tool is built on, kept in the one mode that decodes at
// all (TraceExport.h). Folding the passes together would save two loops and
// cost the only claim `--inspect` has.
bool
ExportTrace(const vrchatOscRecordTool::Options& options,
            const vrmAdapterVrchatOsc::PacketCapture& capture)
{
    // The second half of the refusal `ParseOptions` makes on the spelling. That
    // one catches `--inspect x --export-trace x`; this catches the same file
    // named two ways — `./x`, an absolute path, a symlink, a hard link — which
    // no comparison of strings can see. `equivalent` answers only when both
    // paths exist, and a trace path that does not exist yet cannot be the
    // capture, so the error code is discarded rather than reported: "these are
    // not the same file" and "one of them is not there" are the same answer
    // here.
    std::error_code aliased;
    if (std::filesystem::equivalent(options.inspectPath,
                                    options.traceExportPath, aliased)) {
        std::cerr << "vrchat_osc_record: " << options.traceExportPath
                  << " is the capture being read, named differently; writing "
                     "the trace there would destroy it\n";
        return false;
    }

    vrmAdapterVrchatOsc::TrackerFrameAssembler assembler;
    // The capture's own peer, so a replayed session's diagnostics name what the
    // live one's would have named. A capture that recorded none falls back to
    // its path, which is what the corpus tests read.
    assembler.SetSource(capture.peerEndpoint.empty() ? options.inspectPath
                                                     : capture.peerEndpoint);

    // The provenance the adapter refuses to invent and the operator already
    // stated. `protocol` is this file's to fill because no type in the adapter
    // holds one: `vrmAdapterVrchatOsc` produces no pose, so it carries no
    // `MotionSourceMetadata` for a frame assembler to stamp — which is the
    // library's edge set showing through rather than an omission.
    motion::MotionSourceMetadata metadata;
    metadata.kind = motion::MotionSourceKind::LiveCapture;
    metadata.protocol = "vrchat-osc";
    metadata.provider = capture.sender;
    metadata.sourceId = capture.sourceId;

    vrchatOscRecordTool::TraceCollector trace(options.assignment,
                                              options.solve);
    std::vector<vrmAdapterVrchatOsc::TrackerFrame> frames;
    std::vector<vrmAdapterVrchatOsc::Diagnostic> log;
    // First of each code, and how many there were. An eight-datagram frame that
    // is short one address raises one diagnostic per frame, so a 2000-frame
    // session with a strap off would otherwise write 2000 lines over the report
    // an operator ran this for.
    std::map<vrmAdapterVrchatOsc::DiagnosticCode,
             std::pair<std::string, std::size_t>>
        seen;
    const auto drain = [&seen, &log]() {
        for (const vrmAdapterVrchatOsc::Diagnostic& diagnostic : log) {
            auto& entry = seen[diagnostic.code];
            if (entry.second == 0) {
                entry.first = vrmAdapterVrchatOsc::FormatDiagnostic(diagnostic);
            }
            ++entry.second;
        }
        log.clear();
    };

    for (const vrmAdapterVrchatOsc::RecordedDatagram& datagram :
         capture.datagrams) {
        const vrmAdapterVrchatOsc::TrackerPacket packet =
            vrmAdapterVrchatOsc::DecodeTrackerDatagram(datagram.bytes);
        // The decoder's own refusals, which it raises without a source or a
        // timestamp because it knows neither. Stamped here, where both are
        // known, exactly as `InventoryAddresses` stamps them.
        for (vrmAdapterVrchatOsc::Diagnostic diagnostic : packet.diagnostics) {
            diagnostic.source = assembler.GetSource();
            diagnostic.timestamp = datagram.receiveTime;
            log.push_back(std::move(diagnostic));
        }
        drain();

        frames.clear();
        // The peer-carrying overload. A capture written before the format could
        // say who sent a datagram passes empty throughout and never sees a
        // restart, which is the assembler's stated behaviour rather than a
        // fallback (FrameAssembler.h).
        assembler.Push(packet, datagram.receiveTime,
                       datagram.peer.empty() ? capture.peerEndpoint
                                             : datagram.peer,
                       &frames, &log);
        drain();
        trace.Observe(frames, metadata);
    }

    // The frame still open at the end of the stream, which on this wire is the
    // ordinary case rather than an edge one: a frame closes on the next frame's
    // first repeat or on a gap, and a capture that ends mid-burst has neither.
    // The sibling recorder has no line here because one mocopi datagram is one
    // frame; this one does, and the difference is the frame policy.
    frames.clear();
    assembler.Flush(&frames, &log);
    drain();
    trace.Observe(frames, metadata);

    trace.Close();
    const std::vector<motion::HumanoidAnimation>& sessions =
        trace.GetSessions();

    if (!options.quiet) {
        for (const auto& entry : seen) {
            std::cerr << "vrchat_osc_record: " << entry.second.first;
            if (entry.second.second > 1) {
                std::cerr << " (and " << (entry.second.second - 1) << " more of "
                          << vrmAdapterVrchatOsc::DiagnosticCodeString(
                                 entry.first)
                          << ")";
            }
            std::cerr << "\n";
        }
    }

    // The solve report goes to stdout with the rest of the report, and it is
    // printed whether or not a trace was written. A session that solved nothing
    // is the one an operator most needs it for: it is what tells a misspelled
    // tracker identity from a strap that was never worn.
    vrchatOscRecordTool::PrintSolveReport(stdout, trace.GetReport());

    if (sessions.empty()) {
        std::cerr << "vrchat_osc_record: no frame reached a pose, so there is "
                     "no trace to write; the solve lines above say whether the "
                     "assignment or the traffic is why\n";
        return false;
    }

    std::size_t index = 0;
    if (options.sourceSession != 0) {
        if (options.sourceSession > sessions.size()) {
            std::cerr << "vrchat_osc_record: --source-session "
                      << options.sourceSession << ": this capture holds "
                      << sessions.size() << " session(s)\n";
            return false;
        }
        index = options.sourceSession - 1;
    } else if (sessions.size() > 1) {
        // Refused rather than resolved. Picking the first would silently
        // discard a recording, and concatenating them would assert a continuity
        // of tracking *space* across a restart that nothing here can check --
        // which is this wire's version of the sibling tools' refusal and not
        // theirs, because the receiver's clock does not go back (TraceExport.h).
        std::cerr << "vrchat_osc_record: the sender restarted, so this capture "
                     "holds " << sessions.size()
                  << " sessions from different peers, each calibrated on its "
                     "own; one trace is one session, so name the one to export "
                     "with --source-session 1.." << sessions.size() << "\n";
        return false;
    }

    const motion::HumanoidAnimation& session = sessions[index];
    if (!motion::WriteCaptureTraceFile(options.traceExportPath, session)) {
        // The writer refuses before its first byte when a value cannot be
        // spelled in that format, so a refusal here leaves the path untouched
        // rather than half-written.
        std::cerr << "vrchat_osc_record: could not write "
                  << options.traceExportPath << "\n";
        return false;
    }
    if (!options.quiet) {
        std::cerr << "vrchat_osc_record: wrote " << session.samples.size()
                  << " solved frame(s)";
        if (sessions.size() > 1) {
            std::cerr << " of session " << (index + 1) << " of "
                      << sessions.size();
        }
        std::cerr << " over " << (session.endTime - session.startTime) << " s at "
                  << session.nominalFrameRate << " Hz to "
                  << options.traceExportPath << "\n";

        // The largest thing the trace carries beside the rotations, said at the
        // point it crosses -- the sibling tool's line, for its reason. Here it
        // is also the one number that says whether `--no-root-motion` did what
        // was asked: a session exported under it reports every frame as
        // carrying no root record.
        const vrchatOscRecordTool::HipsMotion& hips =
            trace.GetHipsMotion()[index];
        std::cerr << "vrchat_osc_record: the trace carries " << hips.pathMetres
                  << " m of hips path (" << hips.netMetres
                  << " m net) as root motion";
        if (hips.framesWithoutRoot != 0) {
            std::cerr << "; " << hips.framesWithoutRoot
                      << " frame(s) carried no root record and are not in that "
                         "sum";
        }
        std::cerr << "\n";
    }
    return true;
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
        // The record's own peer where the capture carries one, and the
        // header's where it does not. A capture written before the format
        // could say takes the second path and reports what it always did.
        report.ObserveDatagram(datagram.peer.empty() ? capture.peerEndpoint
                                                     : datagram.peer,
                               datagram.bytes.data(), datagram.bytes.size(),
                               datagram.receiveTime);
    }

    // A file has already stopped, and it stopped by ending. None of the live
    // reasons can be true of a replay, so this is not a default being left in
    // place — it is the only reason there is.
    report.SetStopReason(vrchatOscRecordTool::StopReason::EndOfCapture);
    report.Print(stdout, nullptr, &capture);
    PrintAddressInventory(stdout, capture);

    // After both, so that a reader sees the envelope and the addresses a sender
    // actually sent before they see what a solve made of them. An export that
    // fails is exit 1 with the report already printed: the reading of the
    // capture succeeded, and it is the derivation that did not.
    if (!options.traceExportPath.empty()
        && !ExportTrace(options, capture)) {
        return 1;
    }
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
                datagram.receiveTime, datagram.peer, datagram.bytes});
            // The header names the first peer the session saw and the
            // records name every one of them. On this wire that is the
            // difference between a source that paused and a second source
            // that began: a restart is marked by a new ephemeral source
            // port and by nothing else
            // ([report 02](../../../../../docs/reports/motion/02-2026-08-30-vrchat-osc-address-inventory.md) §4).
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
