// SPDX-License-Identifier: Apache-2.0
//
// motion_capture — Motion Phase D's replay tool.
//
// It is the composition point, not the algorithm: `motionRuntime` owns the
// intake, the buffer and the recorder, `ClipWriter` owns everything that
// touches a stage, and this file wires them together on a fixed tick and
// reports what happened.
//
// The loop below is the whole of "live capture" as this project defines it:
//
//     sender.Advance(now - deliveryLag)   // what has arrived by now
//     recorder.Record(source.Sample(now)) // what the consumer sees now
//
// A real adapter replaces the first line with a decoded packet and nothing
// else changes — which is why the replay is a faithful test and not a mock.
#include "ClipWriter.h"
#include "Options.h"

#include "motionRuntime/CaptureTrace.h"
#include "motionRuntime/LiveCaptureSource.h"
#include "motionRuntime/Recorder.h"
#include "motionRuntime/ReplaySender.h"

#include <cstdio>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace
{

std::string
Number(double value)
{
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.6g", value);
    return buffer;
}

std::string
Count(unsigned long long value)
{
    return std::to_string(value);
}

const char*
MissingBoneName(motion::MissingBonePolicy policy)
{
    return policy == motion::MissingBonePolicy::HoldLast ? "hold" : "unbound";
}

const char*
RootMotionName(motion::RootMotionIntake intake)
{
    switch (intake) {
    case motion::RootMotionIntake::Ignore:
        return "ignore";
    case motion::RootMotionIntake::Passthrough:
        return "passthrough";
    case motion::RootMotionIntake::DeriveVelocity:
        break;
    }
    return "derive";
}

void
PrintReport(const motion::LiveCaptureSource& source,
            const motion::RecordReport& report, std::size_t observedBones)
{
    const motion::LiveCaptureStats& stats = source.GetStats();
    std::printf("intake:     %s accepted, %s out-of-order, %s stale, %s empty\n",
                Count(stats.framesAccepted).c_str(),
                Count(stats.framesRejectedOutOfOrder).c_str(),
                Count(stats.framesRejectedStale).c_str(),
                Count(stats.framesRejectedEmpty).c_str());
    std::printf("bones:      %zu of %zu observed; %s gated by confidence, "
                "%s held, %s left unbound\n",
                observedBones, motion::HumanBoneCount,
                Count(stats.bonesGatedByConfidence).c_str(),
                Count(stats.bonesHeld).c_str(),
                Count(stats.bonesUnbound).c_str());
    std::printf("root:       %s samples observed, %s velocities derived\n",
                Count(stats.rootSamplesObserved).c_str(),
                Count(stats.rootVelocitiesDerived).c_str());
    std::printf("evaluation: %zu ticks -> %zu sampled, %zu held, "
                "%zu extrapolated, %zu unavailable\n",
                report.ticks, report.sampled, report.held, report.extrapolated,
                report.unavailable);
    std::printf("peak lag:   %s s\n", Number(report.peakLagSeconds).c_str());
}

} // namespace

int
main(int argc, char** argv)
{
    const std::vector<std::string> arguments(argv + 1, argv + argc);

    motionCaptureTool::Options options;
    bool showHelp = false;
    std::string error;
    if (!motionCaptureTool::ParseOptions(arguments, &options, &showHelp,
                                         &error)) {
        std::cerr << "motion_capture: " << error << "\n\n"
                  << motionCaptureTool::GetUsage();
        return 2;
    }
    if (showHelp) {
        std::fputs(motionCaptureTool::GetUsage(), stdout);
        return 0;
    }

    motion::HumanoidAnimation trace;
    motion::CaptureTraceError traceError;
    if (!motion::ReadCaptureTraceFile(options.tracePath, &trace, &traceError)) {
        std::cerr << "motion_capture: " << options.tracePath;
        if (traceError.line != 0) {
            std::cerr << ":" << traceError.line;
        }
        std::cerr << ": " << traceError.message << "\n";
        return 1;
    }

    if (!options.normalizePath.empty()) {
        if (!motion::WriteCaptureTraceFile(options.normalizePath, trace)) {
            std::cerr << "motion_capture: could not write "
                      << options.normalizePath << "\n";
            return 1;
        }
        if (!options.quiet) {
            std::cerr << "motion_capture: normalized " << trace.samples.size()
                      << " frame(s) into " << options.normalizePath << "\n";
        }
        return 0;
    }

    const double rate = options.evaluationRate > 0.0
        ? options.evaluationRate
        : trace.nominalFrameRate;

    motion::LiveCaptureSource source(options.capture);
    source.SetSourceMetadata(trace.source);

    motion::ReplaySender sender(trace, &source);
    motion::CaptureRecorder recorder(rate);

    // The session runs from the first recorded frame to the last, plus enough
    // ticks to consume the delivery lag -- otherwise the tail of the trace is
    // delivered and never evaluated.
    //
    // The tolerance matters at both ends. Without it, a span that computes to
    // 59.999999 ticks truncates to 59 and drops the final frame; with a `<=`
    // loop instead of `<`, a lag-free replay would emit one tick past the end
    // of the trace and report a phantom extrapolation.
    const double duration = trace.endTime - trace.startTime;
    const double span = duration + options.deliveryLag;
    const auto tickCount = static_cast<std::size_t>(
        span * rate + motion::PoseSampleTimeTolerance * rate) + 1;

    // Pin the capture clock to the consumer's: a trace may be stamped in any
    // epoch (a real session's timestamps start wherever the device's clock
    // was), and the authored clip has to start at zero.
    source.SetClockOffset(trace.startTime);

    for (std::size_t tick = 0; tick < tickCount; ++tick) {
        const double now = static_cast<double>(tick) / rate;
        sender.Advance(trace.startTime + now - options.deliveryLag);
        if (source.IsEmpty()) {
            // Nothing has arrived yet. Recording an unavailable tick would open
            // the clip with a gap instead of starting it when the session
            // actually started producing.
            continue;
        }
        recorder.Record(source.Sample(now));
    }

    const motion::RecordReport report = recorder.GetReport();
    const motion::HumanoidAnimation recorded = recorder.Take();

    if (recorded.samples.empty()) {
        std::cerr << "motion_capture: the replay produced no evaluated "
                     "frames\n";
        return 1;
    }

    if (!options.quiet && report.unavailable != 0) {
        std::cerr << "motion_capture: warning: " << report.unavailable
                  << " tick(s) could not be answered by the source\n";
    }
    if (!options.quiet && source.GetStats().framesRejectedOutOfOrder != 0) {
        std::cerr << "motion_capture: warning: "
                  << source.GetStats().framesRejectedOutOfOrder
                  << " frame(s) arrived out of order; the source's clock is "
                     "not monotonic\n";
    }

    if (options.report) {
        PrintReport(source, report, source.GetObservedBones().count());
    }

    if (options.dryRun) {
        return 0;
    }

    std::map<std::string, std::string> provenance;
    provenance["kind"] = "liveCapture";
    provenance["provider"] = recorded.source.provider;
    provenance["protocol"] = recorded.source.protocol;
    provenance["sourceId"] = recorded.source.sourceId;
    provenance["trace"] = options.tracePath;
    provenance["evaluationRate"] = Number(rate);
    provenance["deliveryLag"] = Number(options.deliveryLag);
    provenance["confidenceFloor"] =
        Number(options.capture.confidenceFloor);
    provenance["missingBones"] =
        MissingBoneName(options.capture.missingBones);
    provenance["rootMotion"] = RootMotionName(options.capture.rootMotion);
    provenance["smoothingCutoffHz"] =
        Number(options.capture.smoothingCutoffHz);
    provenance["framesEvaluated"] = Count(report.ticks);
    provenance["framesSampled"] = Count(report.sampled);
    provenance["framesHeld"] = Count(report.held);
    provenance["framesExtrapolated"] = Count(report.extrapolated);
    provenance["peakLagSeconds"] = Number(report.peakLagSeconds);

    if (!motionCaptureTool::WriteSemanticClip(options.outputPath, recorded,
                                              options.clipName, provenance,
                                              &error)) {
        std::cerr << "motion_capture: " << error << "\n";
        return 1;
    }

    if (!options.quiet) {
        std::cerr << "motion_capture: wrote " << recorded.samples.size()
                  << " frame(s) over " << source.GetObservedBones().count()
                  << " observed bone(s) to " << options.outputPath << "\n";
    }
    return 0;
}
