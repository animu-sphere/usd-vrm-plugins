// SPDX-License-Identifier: Apache-2.0
#include "TraceExport.h"

#include "motionRuntime/CaptureTrace.h"

namespace mocopiRecordTool
{

void
TraceCollector::Observe(
    const std::vector<vrmAdapterMocopi::MocopiFrame>& frames,
    const motion::MotionSourceMetadata& metadata)
{
    for (const vrmAdapterMocopi::MocopiFrame& frame : frames) {
        // A restart opens a session only when there is one to close. The
        // assembler never marks the first frame of a capture, but a collector
        // that assumed so would produce an empty leading session the first time
        // that changed.
        //
        // On this protocol a restart's flag lands on the first frame the new
        // session *emits*, which can be up to 3.5 s after the restart was
        // detected — the new session is dark until a skeleton packet declares
        // its rig (FrameAssembler.h). That gap belongs to neither session and
        // reaches no trace: the frames in it were refused, so nothing was
        // delivered to observe.
        if (_sessions.empty()
            || (frame.beginsNewSession && !_sessions.back().samples.empty())) {
            _sessions.emplace_back();
        }
        motion::HumanoidAnimation& session = _sessions.back();
        session.samples.push_back(frame.pose);
        session.source = metadata;
        ++_frames;
    }
}

void
TraceCollector::Close()
{
    if (_closed) {
        return;
    }
    _closed = true;

    // A capture whose every datagram was refused — one recorded before its
    // device declared a rig, for instance — leaves one opened and never filled
    // session. An empty animation is not a recording.
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
    }
}

} // namespace mocopiRecordTool
