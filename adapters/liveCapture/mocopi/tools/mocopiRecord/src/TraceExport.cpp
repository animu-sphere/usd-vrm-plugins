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
    // Observing after `Close` re-opens it, so the derived fields are recomputed
    // rather than left describing the frames this call did not know about. The
    // tool observes then closes once and never comes back, so this costs
    // nothing; what it buys is that the only way to read a stale `startTime` is
    // to not call `Close` at all, which `GetSessions` already documents.
    _closed = false;

    for (const vrmAdapterMocopi::MocopiFrame& frame : frames) {
        // A restart opens a session only when there is one to close. The
        // assembler never marks the first frame of a capture, but a collector
        // that assumed so would produce an empty leading session the first time
        // that changed.
        //
        // On this protocol a restart's flag lands on the first frame the new
        // session *emits*, which is seconds after the restart was detected —
        // the new session is dark until a skeleton packet declares its rig
        // (FrameAssembler.h). **Measured at 3.8833 s on a real restart**
        // (2026-08-15): 233 frames refused, and the new session's first emitted
        // frame stamped exactly 233/60 s into its own stream clock. That gap
        // belongs to neither session and reaches no trace: the frames in it
        // were refused, so nothing was delivered to observe.
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

    // **No session here can be empty, so nothing is pruned.** The sibling
    // collector opens with a prune and a sentence about a push that delivered
    // nothing; this class cannot reach that state, because a session is created
    // only in the iteration that immediately appends a sample to it. A capture
    // whose every datagram was refused therefore produces no session at all
    // rather than an empty one, which is the same outcome by a shorter route --
    // and `GetSessions()` being empty is what `ExportTrace` already refuses on.
    //
    // Keeping the loop anyway would have cost more than the lines. The four
    // vectors below are indexed together by every caller, and an erase that
    // pruned two of them would leave the other two describing a different
    // session -- a defect that shows up as a plausible travel distance against
    // the wrong frames rather than as a crash. Unreachable code that has to
    // stay correct in four places is worse than no code.

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
