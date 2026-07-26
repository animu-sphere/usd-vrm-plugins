// SPDX-License-Identifier: Apache-2.0
#include "motionRuntime/ReplaySender.h"

#include <utility>

namespace motion
{

namespace
{

// Delivery uses the same boundary rule as sampling: a frame whose timestamp has
// been reached to within the timeline's precision has arrived. Without it half
// a trace's frames miss the tick they belong to and the consumer extrapolates a
// whole frame interval in their place. See PoseSampleTimeTolerance.
constexpr double kArrivalTolerance = PoseSampleTimeTolerance;

} // namespace

ReplaySender::ReplaySender(HumanoidAnimation trace, LiveCaptureSource* sink)
    : _trace(std::move(trace))
    , _sink(sink)
{
}

std::size_t
ReplaySender::Advance(double captureTime)
{
    if (!_sink) {
        return 0;
    }
    std::size_t accepted = 0;
    while (_next < _trace.samples.size()
           && _trace.samples[_next].timestamp
               <= captureTime + kArrivalTolerance) {
        if (_sink->Push(_trace.samples[_next])) {
            ++accepted;
        }
        ++_next;
    }
    return accepted;
}

std::size_t
ReplaySender::Flush()
{
    if (!_sink) {
        return 0;
    }
    std::size_t accepted = 0;
    for (; _next < _trace.samples.size(); ++_next) {
        if (_sink->Push(_trace.samples[_next])) {
            ++accepted;
        }
    }
    return accepted;
}

} // namespace motion
