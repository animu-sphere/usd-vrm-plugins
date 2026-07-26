// SPDX-License-Identifier: Apache-2.0
//
// The replay half of Motion Phase D (motion policy §16-D).
//
// A `ReplaySender` stands where a capture adapter stands: it pushes recorded
// frames into a `LiveCaptureSource` in order, as a clock advances. Nothing here
// reads a wall clock -- the caller drives the time -- so a replay is a pure
// function of the trace and the tick schedule. That is what makes a live
// pipeline testable: the same trace and the same ticks produce the same buffer
// contents, the same statistics, and the same retargeted output on every run
// and every OS.
//
// It also reproduces the two things that make live capture hard, because they
// are properties of the schedule rather than of the data: a tick that outruns
// the delivered frames leaves the source extrapolating or holding, and a
// deliberately delayed clock offset trades latency for a fully bracketed
// sample.
#pragma once

#include "motionRuntime/LiveCaptureSource.h"
#include "motionRuntime/api.h"

#include "motionCore/Humanoid.h"

#include <cstddef>

namespace motion
{

class MOTIONRUNTIME_API ReplaySender
{
public:
    // `sink` must outlive the sender. The trace is copied: a replay is
    // restartable, and a caller that mutates its animation mid-replay would
    // otherwise silently change history.
    ReplaySender(HumanoidAnimation trace, LiveCaptureSource* sink);

    // Pushes every not-yet-sent frame whose timestamp is at or before
    // `captureTime`, in recorded order. Returns how many the sink accepted.
    //
    // "At or before" carries a sub-microsecond tolerance for the recorded
    // timeline's own precision; without it a schedule computed as `k / rate`
    // makes half a trace's frames miss the tick they belong to. See
    // CaptureTraceTimeQuantum.
    std::size_t Advance(double captureTime);

    // Pushes the whole remaining trace at once. Returns how many the sink
    // accepted.
    std::size_t Flush();

    bool IsExhausted() const noexcept
    {
        return _next >= _trace.samples.size();
    }
    std::size_t GetSentCount() const noexcept { return _next; }
    std::size_t GetFrameCount() const noexcept { return _trace.samples.size(); }

    const HumanoidAnimation& GetTrace() const noexcept { return _trace; }

    // Rewinds to the first frame. The sink is left alone -- clearing it is the
    // caller's decision, because a seek and a restart want different things.
    void Rewind() noexcept { _next = 0; }

private:
    HumanoidAnimation _trace;
    LiveCaptureSource* _sink;
    std::size_t _next = 0;
};

} // namespace motion
