// SPDX-License-Identifier: Apache-2.0

#include "vrmAdapterMocopi/LiveSource.h"

#include <utility>

namespace vrmAdapterMocopi
{

MocopiLiveSource::MocopiLiveSource(const MocopiLiveSourceConfig& config)
    : _assembler(config.frame)
    , _intake(config.intake)
    , _restart(config.restart)
{
    // Before a frame exists, so the first pose out of the buffer already says it
    // arrived over this protocol from a live capture. Told once and never
    // compared again: unlike the sibling's, this protocol carries no handshake
    // that could teach the session anything later (see the header).
    _intake.SetSourceMetadata(_assembler.GetSourceMetadata());
}

void
MocopiLiveSource::SetSource(std::string source)
{
    _assembler.SetSource(std::move(source));
}

std::size_t
MocopiLiveSource::_Deliver()
{
    std::size_t admitted = 0;
    for (const MocopiFrame& frame : _frames) {
        if (frame.beginsNewSession) {
            _restartPending = true;
            if (_restart == SessionRestartPolicy::Reset) {
                // Before the frame is pushed, so it is admitted as the first of
                // the new buffer rather than refused against the old one's head.
                _intake.Reset();
                ++_stats.sessionsReset;
            }
        }

        ++_stats.framesDelivered;
        if (_intake.Push(frame.pose)) {
            ++_stats.framesAdmitted;
            ++admitted;
        } else {
            ++_stats.framesRefused;
        }
    }
    return admitted;
}

void
MocopiLiveSource::_StampDatagram(std::vector<Diagnostic>* diagnostics,
                                 std::size_t from) const
{
    if (!diagnostics) {
        return;
    }
    for (std::size_t index = from; index != diagnostics->size(); ++index) {
        (*diagnostics)[index].source = _assembler.GetSource();
        // Overwrites the assembler's own serial on the lines it raised, which is
        // the point: it counts packets it was handed and this counts deliveries,
        // and only one of the two can name the datagram a reader would go
        // looking for in a capture.
        (*diagnostics)[index].sequence = _datagramSerial;
    }
}

std::size_t
MocopiLiveSource::PushDatagram(const std::uint8_t* bytes, std::size_t size,
                               double receiveTime,
                               std::vector<Diagnostic>* diagnostics)
{
    ++_datagramSerial;
    const std::size_t before = diagnostics ? diagnostics->size() : 0;

    MotionPacket packet;
    if (!DecodeMotionPacket(bytes, size, &packet, diagnostics)) {
        ++_stats.datagramsRefused;
        // The frames of the previous push must not survive a datagram this one
        // refused: `GetFramesFromLastPush()` is a window on the delivery that
        // just happened, and a caller reading it after a refusal would be shown
        // the frame before last as though it had just arrived.
        _frames.clear();
        _StampDatagram(diagnostics, before);
        return 0;
    }
    ++_stats.datagramsDecoded;

    const std::size_t admitted = PushPacket(packet, receiveTime, diagnostics);
    // After the assembler rather than before it, so one datagram's diagnostics
    // are numbered alike however many layers raised them.
    _StampDatagram(diagnostics, before);
    return admitted;
}

std::size_t
MocopiLiveSource::PushPacket(const MotionPacket& packet, double receiveTime,
                             std::vector<Diagnostic>* diagnostics)
{
    _frames.clear();
    _assembler.Push(packet, receiveTime, &_frames, diagnostics);
    return _Deliver();
}

bool
MocopiLiveSource::ConsumeSessionRestart() noexcept
{
    const bool pending = _restartPending;
    _restartPending = false;
    return pending;
}

motion::PoseSampleResult
MocopiLiveSource::Sample(double evaluationTime)
{
    return _intake.Sample(evaluationTime);
}

motion::MotionSourceMetadata
MocopiLiveSource::GetSourceMetadata() const
{
    // The intake's rather than the assembler's, which on this protocol are the
    // same value — reported through the intake anyway, because this must say
    // what is actually stamped on the poses a caller is sampling, and a
    // constant that is read from the source of truth stays correct if the source
    // of truth stops being constant.
    return _intake.GetSourceMetadata();
}

bool
MocopiLiveSource::GetTimeRange(double* startTime, double* endTime) const
{
    return _intake.GetTimeRange(startTime, endTime);
}

void
MocopiLiveSource::Reset()
{
    _assembler.Reset();
    _intake.Reset();
    _frames.clear();
    _restartPending = false;
    _intake.SetSourceMetadata(_assembler.GetSourceMetadata());
}

} // namespace vrmAdapterMocopi
