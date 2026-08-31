// SPDX-License-Identifier: Apache-2.0
#include "TraceExport.h"

#include <string_view>
#include <utility>

namespace vrchatOscRecordTool
{
namespace
{

std::size_t
RefusalIndex(motionTracking::TrackerSolveRefusal refusal) noexcept
{
    const auto index = static_cast<std::size_t>(refusal);
    // A value outside the enum cannot come from `SolveTrackerPose`, and the
    // clamp is here so that a future enumerator added without a bump to
    // `TrackerSolveRefusalCount` writes into a bucket rather than past the
    // array. It is cheaper than the alternative and it is not a policy: nothing
    // reads the bucket back as a refusal name.
    return index < motionTracking::TrackerSolveRefusalCount ? index : 0;
}

std::size_t
RegionIndex(motionTracking::TrackerRegion region) noexcept
{
    const auto index = static_cast<std::size_t>(region);
    return index < motionTracking::TrackerRegionCount
        ? index
        : motionTracking::TrackerRegionCount;
}

void
Tally(std::array<std::size_t, motionTracking::TrackerRegionCount>& counts,
      const std::vector<motionTracking::TrackerRegion>& regions)
{
    for (const motionTracking::TrackerRegion region : regions) {
        const std::size_t index = RegionIndex(region);
        if (index < counts.size()) {
            ++counts[index];
        }
    }
}

// One "region count, region count" line, or the word that says there were none.
// Written rather than left blank: a report whose line is missing and one whose
// line is empty read the same to somebody scrolling, and only one of them is a
// measurement.
void
PrintRegionCounts(
    std::FILE* out, const char* label,
    const std::array<std::size_t, motionTracking::TrackerRegionCount>& counts)
{
    std::fprintf(out, "  %s:", label);
    bool any = false;
    for (std::size_t i = 0; i < counts.size(); ++i) {
        if (counts[i] == 0) {
            continue;
        }
        const std::string_view name = motionTracking::TrackerRegionName(
            static_cast<motionTracking::TrackerRegion>(i));
        std::fprintf(out, "%s %.*s %zu", any ? "," : "",
                     static_cast<int>(name.size()), name.data(), counts[i]);
        any = true;
    }
    std::fprintf(out, "%s\n", any ? "" : " none");
}

} // namespace

TraceCollector::TraceCollector(
    motionTracking::TrackerAssignmentSpec assignment,
    motionTracking::TrackerSolveConfig solve)
    : _assignment(std::move(assignment))
    , _solve(solve)
{
}

void
TraceCollector::_OpenSession()
{
    _sessions.emplace_back();
}

void
TraceCollector::Observe(
    const std::vector<vrmAdapterVrchatOsc::TrackerFrame>& frames,
    const motion::MotionSourceMetadata& metadata)
{
    for (const vrmAdapterVrchatOsc::TrackerFrame& frame : frames) {
        ++_report.framesObserved;

        // A restart opens a session only when there is one to close. The
        // assembler never marks the first frame of a capture, but a collector
        // that assumed so would produce an empty leading session the first time
        // that changed -- and a session whose every frame refused is not one to
        // close either, which is why the test is on the poses rather than on
        // the count.
        if (_sessions.empty()
            || (frame.beginsNewSession && !_sessions.back().samples.empty())) {
            _OpenSession();
        }

        // The conversion this file exists for: two types, four fields, no
        // arithmetic. See the header on the two that do not cross.
        std::vector<motionTracking::TrackerObservation> observed;
        observed.reserve(frame.samples.size());
        for (const vrmAdapterVrchatOsc::TrackerSample& sample : frame.samples) {
            motionTracking::TrackerObservation observation;
            observation.tracker = sample.tracker;
            observation.position = sample.position;
            observation.rotation = sample.rotation;
            observation.hasPosition = sample.hasPosition;
            observation.hasRotation = sample.hasRotation;
            observed.push_back(std::move(observation));
        }

        // `TrackerIdentities` rather than a hand-built list, because a binding
        // holds an index into the array the assignment was made from: building
        // the identities separately is how the two calls drift apart and bind a
        // region to a device nobody wore (TrackerObservation.h).
        const motionTracking::TrackerAssignment assignment =
            motionTracking::AssignTrackers(
                _assignment, motionTracking::TrackerIdentities(observed));

        // Filled whatever the refusal, which is that layer's rule -- so these
        // are read before the solve rather than under its success. The last
        // frame that produced an assignment wins; see the header.
        _report.absent = assignment.absent;
        _report.unplaced.clear();
        for (const std::size_t index : assignment.unplaced) {
            if (index < observed.size()) {
                _report.unplaced.push_back(observed[index].tracker);
            }
        }

        const motionTracking::TrackerSolve solve =
            motionTracking::SolveTrackerPose(assignment, observed,
                                             frame.receiveTime, _solve);

        const std::size_t refusal = RefusalIndex(solve.refusal);
        ++_report.refusals[refusal];
        if (_report.firstDetail[refusal].empty() && !solve.detail.empty()) {
            _report.firstDetail[refusal] = solve.detail;
        }

        if (!solve.Solved()) {
            continue;
        }

        ++_report.framesSolved;
        Tally(_report.placed, solve.placed);
        Tally(_report.unsolved, solve.unsolved);
        Tally(_report.withoutRotation, solve.withoutRotation);
        Tally(_report.positionsUnused, solve.positionsUnused);

        motion::HumanoidAnimation& session = _sessions.back();
        session.samples.push_back(solve.pose);
        session.source = metadata;
        ++_poses;
    }
}

void
TraceCollector::Close()
{
    if (_closed) {
        return;
    }
    _closed = true;

    // A restart can leave a session opened and never filled, and a capture
    // whose every frame refused leaves one and only one. Either way an empty
    // animation is not a recording.
    for (std::size_t i = _sessions.size(); i-- != 0;) {
        if (_sessions[i].samples.empty()) {
            _sessions.erase(_sessions.begin()
                            + static_cast<std::ptrdiff_t>(i));
        }
    }

    _hips.assign(_sessions.size(), HipsMotion());
    for (std::size_t i = 0; i < _sessions.size(); ++i) {
        motion::HumanoidAnimation& session = _sessions[i];
        session.startTime = session.samples.front().timestamp;
        session.endTime = session.samples.back().timestamp;

        // CaptureTrace.h's derivation, deliberately duplicated rather than
        // approximated: a trace this writes declares the rate its reader would
        // otherwise have measured, so the file is a fixed point.
        const double span = session.endTime - session.startTime;
        const std::size_t intervals = session.samples.size() - 1;
        session.nominalFrameRate = (span > 0.0 && intervals > 0)
            ? static_cast<double>(intervals) / span
            : 30.0;

        // The hips path, measured over the poses that carry one. A session
        // exported with `--no-root-motion` carries none at all and reports its
        // whole length under `framesWithoutRoot`, which is the honest reading
        // of a flag that turned the measurement off.
        HipsMotion& hips = _hips[i];
        bool started = false;
        pxr::GfVec3f first(0.0f);
        pxr::GfVec3f previous(0.0f);
        for (const motion::HumanoidPose& pose : session.samples) {
            if (!pose.root.hasPosition) {
                ++hips.framesWithoutRoot;
                continue;
            }
            if (!started) {
                started = true;
                first = pose.root.worldPosition;
            } else {
                hips.pathMetres +=
                    static_cast<double>((pose.root.worldPosition - previous)
                                            .GetLength());
            }
            previous = pose.root.worldPosition;
        }
        if (started) {
            hips.netMetres =
                static_cast<double>((previous - first).GetLength());
        }
    }
}

void
PrintSolveReport(std::FILE* out, const SolveReport& report)
{
    std::fprintf(out, "solve: %zu of %zu frame(s)\n", report.framesSolved,
                 report.framesObserved);

    for (std::size_t i = 0; i < report.refusals.size(); ++i) {
        const auto refusal =
            static_cast<motionTracking::TrackerSolveRefusal>(i);
        if (refusal == motionTracking::TrackerSolveRefusal::None
            || report.refusals[i] == 0) {
            continue;
        }
        const std::string_view name =
            motionTracking::TrackerSolveRefusalName(refusal);
        std::fprintf(out, "  refused %.*s: %zu frame(s)",
                     static_cast<int>(name.size()), name.data(),
                     report.refusals[i]);
        if (!report.firstDetail[i].empty()) {
            std::fprintf(out, "  first: %s", report.firstDetail[i].c_str());
        }
        std::fprintf(out, "\n");
    }

    // Four lines, always, in the order a reader asks the questions: what
    // reached a joint, what was worn and reached none, what was worn and sent
    // no orientation, and what sent a position nothing read.
    PrintRegionCounts(out, "placed", report.placed);
    PrintRegionCounts(out, "unsolved", report.unsolved);
    PrintRegionCounts(out, "withoutRotation", report.withoutRotation);
    PrintRegionCounts(out, "positionsUnused", report.positionsUnused);

    std::fprintf(out, "  stated but absent:");
    if (report.absent.empty()) {
        std::fprintf(out, " none");
    }
    for (std::size_t i = 0; i < report.absent.size(); ++i) {
        const std::string_view name =
            motionTracking::TrackerRegionName(report.absent[i]);
        std::fprintf(out, "%s %.*s", i == 0 ? "" : ",",
                     static_cast<int>(name.size()), name.data());
    }
    std::fprintf(out, "\n");

    std::fprintf(out, "  observed but unplaced:");
    if (report.unplaced.empty()) {
        std::fprintf(out, " none");
    }
    for (std::size_t i = 0; i < report.unplaced.size(); ++i) {
        std::fprintf(out, "%s %s", i == 0 ? "" : ",",
                     report.unplaced[i].c_str());
    }
    std::fprintf(out, "\n");
}

} // namespace vrchatOscRecordTool
