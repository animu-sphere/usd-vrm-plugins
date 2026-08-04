// SPDX-License-Identifier: Apache-2.0
//
// vmc_record — the tool that meets a real sender.
//
// Every layer under it is verifiable from committed bytes, which is the whole
// point of the adapter's build order (roadmap §5) and also its limit: the
// corpus is *generated*, so it reproduces the protocol's shapes and not what
// any real application actually emits. Milestone B's remaining items are all
// the same shape — two sender applications validated, a capture device through
// a relay, a recorded corpus — and none of them can be closed by writing more
// code. They are closed by an operator pointing a sender at this port. So this
// tool's job is to turn one such session into two things a repository can keep:
// a capture file, and a statement of what was in it.
//
// ## The datagram reaches the file before the decoder sees it
//
//     receive -> append to the capture -> decode -> report
//
// That order is the one rule here. A recorder whose decoder could refuse a
// datagram would record what this adapter already understands, and the sessions
// worth recording are exactly the ones it might not — a sender emitting a form
// no fixture pins, a device dropping a joint, a restart mid-frame. `UdpReceiver`
// makes the same argument one layer down about filtering, and this is where it
// is finished: nothing the decode path does or fails to do can change a byte of
// what was written.
//
// The decode still runs, in the same loop and not afterwards, because an
// operator with a sender application open needs to know *now* whether the
// session is worth keeping. What it produces is a report, and a report is not a
// filter.
//
// ## Two modes, one report
//
// `--inspect` decodes a recorded capture and prints the same block, opening no
// socket. That is what makes this tool testable in CI over the committed corpus
// with no sender at all, and it is also the answer to "is this fixture still
// what I thought it was" for a capture recorded months ago.
//
// ## Stopping
//
// A recorder that only stops on Ctrl-C cannot be run from a script, and one
// that cannot be interrupted cannot be run by a person. Both exist here, plus a
// duration, an idle timeout, and a datagram bound that is on by default. Every
// session reports which of them ended it — a capture that stopped because a
// flag said so and one that stopped because the socket failed are different
// sessions, and the file cannot tell them apart afterwards.
//
// Interruption is a flag the loop checks, which is the shape `UdpReceiver`'s
// bounded wait was built for: a thread parked in `recvfrom` can only be woken by
// closing the socket underneath it, and that races the descriptor's reuse.
#include "Options.h"
#include "SessionReport.h"
#include "TraceExport.h"

#include "vrmAdapterVmc/Diagnostics.h"
#include "vrmAdapterVmc/LiveSource.h"
#include "vrmAdapterVmc/PacketCapture.h"
#include "vrmAdapterVmc/UdpReceiver.h"

#include "motionRuntime/CaptureTrace.h"

#include <csignal>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

namespace
{

// Set from a signal handler, so it is the one type the standard promises is
// safe to write there. `volatile` for the same reason: the loop below reads it
// on every iteration and nothing in that loop tells a compiler it can change.
volatile std::sig_atomic_t gInterrupted = 0;

extern "C" void
OnInterrupt(int)
{
    gInterrupted = 1;
}

// How long a poll waits. Long enough that an idle recorder is not a spin, short
// enough that Ctrl-C feels immediate and a --duration overshoots by no more
// than this.
constexpr double kPollSeconds = 0.2;

// How often the progress line is rewritten, on the receive clock.
constexpr double kProgressSeconds = 1.0;

void
ReportDiagnostics(const std::vector<vrmAdapterVmc::Diagnostic>& log,
                  std::size_t from, bool quiet)
{
    if (quiet) {
        return;
    }
    for (std::size_t i = from; i < log.size(); ++i) {
        // Only the ones a session cannot continue past reach stderr as they
        // happen. A 30 Hz sender missing one bone would otherwise write a
        // thousand recoverable lines a minute over the progress line, and the
        // report at the end counts every one of them by code anyway.
        if (!log[i].recoverable) {
            std::cerr << "vmc_record: "
                      << vrmAdapterVmc::FormatDiagnostic(log[i]) << "\n";
        }
    }
}

// Writes the canonical trace the export flag asked for. Returns false when
// nothing could be written, having said why -- the caller prints its report
// either way, for the reason `RunRecord` gives about the capture file.
//
// A capture holding two sessions is refused rather than resolved. Picking the
// first would silently discard a recording, and concatenating them would
// manufacture a continuity the sender's clock denies (TraceExport.h).
bool
WriteTrace(const vmcRecordTool::Options& options,
           vmcRecordTool::TraceCollector& collector, bool quiet)
{
    collector.Close();
    const std::vector<motion::HumanoidAnimation>& sessions =
        collector.GetSessions();

    if (sessions.empty()) {
        std::cerr << "vmc_record: nothing decoded into a frame, so there is no "
                     "trace to write\n";
        return false;
    }

    std::size_t index = 0;
    if (options.traceSession != 0) {
        if (options.traceSession > sessions.size()) {
            std::cerr << "vmc_record: --session " << options.traceSession
                      << ": this recording holds " << sessions.size()
                      << " session(s)\n";
            return false;
        }
        index = options.traceSession - 1;
    } else if (sessions.size() > 1) {
        std::cerr << "vmc_record: the sender restarted, so this recording holds "
                  << sessions.size()
                  << " sessions whose clocks overlap; one trace is one session, "
                     "so name the one to export with --session 1.."
                  << sessions.size() << "\n";
        return false;
    }

    const motion::HumanoidAnimation& session = sessions[index];
    if (!motion::WriteCaptureTraceFile(options.traceExportPath, session)) {
        // The writer refuses before its first byte when a frame carries an
        // expression name the format cannot spell, so a refusal here leaves the
        // path untouched rather than half-written.
        std::cerr << "vmc_record: could not write " << options.traceExportPath
                  << "\n";
        return false;
    }
    if (!quiet) {
        std::cerr << "vmc_record: wrote " << session.samples.size()
                  << " delivered frame(s)";
        if (sessions.size() > 1) {
            std::cerr << " of session " << (index + 1) << " of "
                      << sessions.size();
        }
        std::cerr << " to " << options.traceExportPath << "\n";
    }
    return true;
}

int
RunInspect(const vmcRecordTool::Options& options)
{
    vrmAdapterVmc::PacketCapture capture;
    vrmAdapterVmc::PacketCaptureError captureError;
    if (!vrmAdapterVmc::ReadPacketCaptureFile(options.inspectPath, &capture,
                                              &captureError)) {
        std::cerr << "vmc_record: " << options.inspectPath;
        if (captureError.line != 0) {
            std::cerr << ":" << captureError.line;
        }
        std::cerr << ": " << captureError.message << "\n";
        return 1;
    }

    vrmAdapterVmc::VmcLiveSourceConfig config;
    config.frame = options.frame;
    vrmAdapterVmc::VmcLiveSource source(config);
    // The capture's own peer, so a replayed session's diagnostics name what the
    // live one's would have named. A capture that recorded none falls back to
    // its path, which is what the corpus tests use.
    source.SetSource(capture.peerEndpoint.empty() ? options.inspectPath
                                                  : capture.peerEndpoint);

    vmcRecordTool::SessionReport report;
    vmcRecordTool::TraceCollector trace;
    std::vector<vrmAdapterVmc::Diagnostic> log;
    for (const vrmAdapterVmc::RecordedDatagram& datagram : capture.datagrams) {
        report.ObserveDatagram(capture.peerEndpoint, datagram.bytes.size(),
                               datagram.receiveTime);
        source.PushDatagram(datagram.bytes, datagram.receiveTime, &log);
        report.ObserveFrames(source.GetFramesFromLastPush());
        trace.Observe(source.GetFramesFromLastPush(),
                      source.GetSourceMetadata());
        report.ObserveDiagnostics(log, 0);
        ReportDiagnostics(log, 0, options.quiet);
        // Cleared per datagram, exactly as the record loop does it: the report
        // has counted these and kept the first of each code, and a capture
        // whose every packet is malformed would otherwise accumulate one
        // diagnostic per packet for the length of the file.
        log.clear();
    }

    // A capture ends with a frame still open — a live session never reaches
    // this, and a replay that forgets it loses its last frame.
    source.Flush(&log);
    report.ObserveFrames(source.GetFramesFromLastPush());
    trace.Observe(source.GetFramesFromLastPush(), source.GetSourceMetadata());
    report.ObserveDiagnostics(log, 0);

    bool exported = true;
    if (!options.traceExportPath.empty()) {
        exported = WriteTrace(options, trace, options.quiet);
    }

    report.SetStopReason(vmcRecordTool::StopReason::EndOfCapture);
    report.Print(stdout, source, nullptr);
    return exported ? 0 : 1;
}

int
RunRecord(const vmcRecordTool::Options& options)
{
    vrmAdapterVmc::UdpReceiver receiver;
    std::vector<vrmAdapterVmc::Diagnostic> log;
    if (!receiver.Open(options.receiver, &log)) {
        for (const vrmAdapterVmc::Diagnostic& diagnostic : log) {
            std::cerr << "vmc_record: "
                      << vrmAdapterVmc::FormatDiagnostic(diagnostic) << "\n";
        }
        return 1;
    }
    log.clear();

    if (!options.quiet) {
        // Before anything is received, and on stderr, because it is the one
        // line a script waiting to start a sender has to read — and because a
        // `--port 0` session cannot be reached until this says where it landed.
        std::cerr << "vmc_record: listening on " << receiver.GetBoundEndpoint()
                  << (receiver.IsLoopbackOnly() ? " (loopback only)" : "")
                  << "\n";
    }

    vrmAdapterVmc::VmcLiveSourceConfig config;
    config.frame = options.frame;
    vrmAdapterVmc::VmcLiveSource source(config);
    source.SetSource(receiver.GetBoundEndpoint());

    vrmAdapterVmc::PacketCapture capture;
    capture.sender = options.sender;
    capture.sourceId = options.sourceId;
    capture.listenEndpoint = receiver.GetBoundEndpoint();

    vmcRecordTool::SessionReport report;
    vmcRecordTool::TraceCollector trace;
    report.SetStopReason(vmcRecordTool::StopReason::Interrupted);

    vrmAdapterVmc::ReceivedDatagram datagram;
    double lastArrival = 0.0;
    double lastProgress = 0.0;
    bool running = true;
    while (running) {
        if (gInterrupted != 0) {
            report.SetStopReason(vmcRecordTool::StopReason::Interrupted);
            break;
        }

        const vrmAdapterVmc::ReceiveStatus status =
            receiver.Receive(&datagram, kPollSeconds);
        switch (status) {
        case vrmAdapterVmc::ReceiveStatus::Received: {
            // Recorded first. See the header: this order is the rule.
            capture.datagrams.push_back(
                vrmAdapterVmc::RecordedDatagram{datagram.receiveTime,
                                                datagram.bytes});
            if (capture.peerEndpoint.empty()) {
                capture.peerEndpoint = datagram.peer;
            }
            lastArrival = datagram.receiveTime;

            const std::size_t seen = log.size();
            report.ObserveDatagram(datagram.peer, datagram.bytes.size(),
                                   datagram.receiveTime);
            source.PushDatagram(datagram.bytes, datagram.receiveTime, &log);
            report.ObserveFrames(source.GetFramesFromLastPush());
            trace.Observe(source.GetFramesFromLastPush(),
                          source.GetSourceMetadata());
            report.ObserveDiagnostics(log, seen);
            ReportDiagnostics(log, seen, options.quiet);
            // The list is a session's worth of history nobody reads twice: the
            // report has counted these and kept the first of each code.
            log.clear();

            if (report.GetDatagramCount() >= options.maxDatagrams) {
                report.SetStopReason(
                    vmcRecordTool::StopReason::MaxDatagrams);
                running = false;
            }
            break;
        }
        case vrmAdapterVmc::ReceiveStatus::Idle:
            break;
        case vrmAdapterVmc::ReceiveStatus::Closed:
        case vrmAdapterVmc::ReceiveStatus::Failed:
            std::cerr << "vmc_record: the socket failed: "
                      << receiver.GetLastErrorText() << "\n";
            report.SetStopReason(vmcRecordTool::StopReason::ReceiveFailed);
            running = false;
            break;
        }

        // `Now()` rather than the last datagram's stamp: a session that stops
        // receiving still has to notice its own duration passing.
        const double now = receiver.Now();
        if (running && options.durationSeconds > 0.0
            && now >= options.durationSeconds) {
            report.SetStopReason(vmcRecordTool::StopReason::Duration);
            running = false;
        }
        if (running && options.idleSeconds > 0.0
            && now - lastArrival >= options.idleSeconds) {
            // Measured from `Open` until the first datagram, so a sender that
            // never starts times out exactly as one that stops does.
            report.SetStopReason(vmcRecordTool::StopReason::IdleTimeout);
            running = false;
        }

        if (!options.quiet && now - lastProgress >= kProgressSeconds) {
            lastProgress = now;
            std::fprintf(stderr,
                         "vmc_record: %6.1f s  %llu datagram(s)  "
                         "%llu frame(s)\n",
                         now,
                         static_cast<unsigned long long>(
                             report.GetDatagramCount()),
                         static_cast<unsigned long long>(
                             report.GetFrameCount()));
        }
    }

    // The frame the session was in the middle of when it stopped. It changes
    // nothing about the file — that was written datagram by datagram — and it
    // is one more frame in the report.
    const std::size_t seen = log.size();
    source.Flush(&log);
    report.ObserveFrames(source.GetFramesFromLastPush());
    trace.Observe(source.GetFramesFromLastPush(), source.GetSourceMetadata());
    report.ObserveDiagnostics(log, seen);

    // The receiver is deliberately not closed here. `Close` clears the bound
    // endpoint, the loopback flag and the granted buffer size, which are three
    // of the facts the report is about to print — and the socket's own
    // destructor releases it a few lines later anyway.

    if (!options.quiet && report.HasMultiplePeers()) {
        // The capture header names one peer, so a mixed session's provenance is
        // true of some of its datagrams and not the rest. Worth an operator's
        // attention before the file becomes a fixture.
        std::cerr << "vmc_record: warning: datagrams arrived from more than one "
                     "peer; the capture header names only "
                  << capture.peerEndpoint << "\n";
    }
    if (!options.quiet && report.GetDatagramCount() == 0) {
        std::cerr << "vmc_record: warning: nothing arrived"
                  << (receiver.IsLoopbackOnly()
                          ? "; the socket was bound to loopback, which no other "
                            "machine can reach"
                          : "")
                  << "\n";
    }

    // The write can fail, and the report is printed either way. A session that
    // has just run for ten minutes exists in exactly two places — the file and
    // this report — and returning early on a full disk would destroy both at
    // once, which is the moment an operator most needs to be told what they
    // had.
    bool written = true;
    if (!options.dryRun) {
        written = vrmAdapterVmc::WritePacketCaptureFile(options.outputPath,
                                                        capture);
        if (!written) {
            std::cerr << "vmc_record: could not write " << options.outputPath
                      << "\n";
        } else if (!options.quiet) {
            std::cerr << "vmc_record: wrote " << capture.datagrams.size()
                      << " datagram(s) to " << options.outputPath << "\n";
        }
    }

    // After the capture, always. The verbatim bytes are the record a session
    // cannot be re-run to recover; the trace is derived from them and can be
    // written again from the capture by `--inspect --export-trace`. So the
    // derived artifact never gets to be the reason the raw one was not written.
    bool exported = true;
    if (!options.traceExportPath.empty()) {
        exported = WriteTrace(options, trace, options.quiet);
    }

    report.Print(stdout, source, &receiver);
    return written && exported ? 0 : 1;
}

} // namespace

int
main(int argc, char** argv)
{
    const std::vector<std::string> arguments(argv + 1, argv + argc);

    vmcRecordTool::Options options;
    bool showHelp = false;
    std::string error;
    if (!vmcRecordTool::ParseOptions(arguments, &options, &showHelp, &error)) {
        std::cerr << "vmc_record: " << error << "\n\n"
                  << vmcRecordTool::GetUsage();
        return 2;
    }
    if (showHelp) {
        std::fputs(vmcRecordTool::GetUsage(), stdout);
        return 0;
    }

    if (!options.inspectPath.empty()) {
        return RunInspect(options);
    }

    std::signal(SIGINT, OnInterrupt);
    std::signal(SIGTERM, OnInterrupt);
    return RunRecord(options);
}
