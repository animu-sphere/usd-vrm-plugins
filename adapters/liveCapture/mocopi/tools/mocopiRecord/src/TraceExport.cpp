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
            _hips.emplace_back();
            _hipsFirst.emplace_back();
            _hipsLast.emplace_back();
        }
        motion::HumanoidAnimation& session = _sessions.back();
        session.samples.push_back(frame.pose);
        session.source = metadata;
        ++_frames;

        // Measured on the way past, because this is the last place it exists.
        // The pose being appended above has nowhere to put it and the trace
        // has no line for it, so a caller reading the file back can never
        // recover what follows (see the header).
        HipsMotion& hips = _hips.back();
        if (!frame.hipsPosition) {
            ++hips.framesWithoutHips;
            continue;
        }
        const pxr::GfVec3f& position = *frame.hipsPosition;
        if (!_hipsFirst.back()) {
            _hipsFirst.back() = position;
        } else {
            // Summed step by step rather than measured end to end: a session
            // that walks out and back travels twice the distance it displaces,
            // and the second number alone would call it stationary.
            hips.pathMetres += (position - *_hipsLast.back()).GetLength();
        }
        _hipsLast.back() = position;
        hips.netMetres = (position - *_hipsFirst.back()).GetLength();
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
    //
    // The hips row goes with it, in the same pass: the two vectors are indexed
    // together by every caller, and pruning one of them alone would report the
    // wrong session's travel against the right session's frames — a defect that
    // shows up as a plausible number rather than as a crash.
    for (std::size_t i = _sessions.size(); i-- != 0;) {
        if (_sessions[i].samples.empty()) {
            const auto offset = static_cast<std::ptrdiff_t>(i);
            _sessions.erase(_sessions.begin() + offset);
            _hips.erase(_hips.begin() + offset);
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
