// SPDX-License-Identifier: Apache-2.0
#include "TraceExport.h"

#include "motionRuntime/CaptureTrace.h"

namespace vmcRecordTool
{

void
TraceCollector::Observe(const std::vector<vrmAdapterVmc::VmcFrame>& frames,
                        const motion::MotionSourceMetadata& metadata)
{
    for (const vrmAdapterVmc::VmcFrame& frame : frames) {
        // A restart opens a session only when there is one to close. The
        // assembler never marks the first frame of a capture, but a collector
        // that assumed so would produce an empty leading session the first time
        // that changed.
        if (_sessions.empty()
            || (frame.beginsNewSession && !_sessions.back().samples.empty())) {
            _sessions.emplace_back();
        }
        motion::HumanoidAnimation& session = _sessions.back();
        session.samples.push_back(frame.pose);
        session.source = metadata;
    }
}

void
TraceCollector::Close()
{
    if (_closed) {
        return;
    }
    _closed = true;

    // A push that delivered nothing can leave a session opened and never
    // filled; a capture whose every datagram was refused leaves one and only
    // one. Either way an empty animation is not a recording.
    for (std::size_t i = _sessions.size(); i-- != 0;) {
        if (_sessions[i].samples.empty()) {
            _sessions.erase(_sessions.begin()
                            + static_cast<std::ptrdiff_t>(i));
        }
    }

    for (motion::HumanoidAnimation& session : _sessions) {
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

        // The format stores provenance once, in the header, so the reader
        // stamps it onto every sample it hands back. Doing the same here is
        // what makes a written trace compare equal to the session it came from
        // rather than merely replay the same -- the difference is invisible in
        // the file and is the whole strength of the round-trip check.
        for (motion::HumanoidPose& sample : session.samples) {
            sample.source = session.source;
        }
    }
}

} // namespace vmcRecordTool
