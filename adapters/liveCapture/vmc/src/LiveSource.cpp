// SPDX-License-Identifier: Apache-2.0

#include "vrmAdapterVmc/LiveSource.h"

#include "vrmAdapterVmc/OscPacket.h"

#include <utility>

namespace vrmAdapterVmc
{

VmcLiveSource::VmcLiveSource(const VmcLiveSourceConfig& config)
    : _assembler(config.frame)
    , _intake(config.intake)
    , _restart(config.restart)
{
    // Before a frame exists, so the first pose out of the buffer already says it
    // arrived over VMC from a live capture. The model's title joins it when the
    // sender sends one.
    _metadata = _assembler.GetSourceMetadata();
    _intake.SetSourceMetadata(_metadata);
}

void
VmcLiveSource::SetSource(std::string source)
{
    _assembler.SetSource(std::move(source));
}

std::size_t
VmcLiveSource::_Deliver()
{
    std::size_t admitted = 0;
    for (const VmcFrame& frame : _frames) {
        if (frame.beginsNewSession) {
            _restartPending = true;
            if (_restart == SessionRestartPolicy::Reset) {
                // Before the frame is pushed, so it is admitted as the first of
                // the new buffer rather than refused against the old one's head.
                _intake.Reset();
                ++_stats.sessionsReset;
            }
        }

        // The handshake can arrive at any point in a session, including between
        // two frames. Comparing rather than assigning keeps a 30 Hz stream from
        // re-stamping the intake sixty times a second to say the same thing.
        const motion::MotionSourceMetadata& metadata =
            _assembler.GetSourceMetadata();
        if (metadata != _metadata) {
            _metadata = metadata;
            _intake.SetSourceMetadata(_metadata);
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
VmcLiveSource::_StampSource(std::vector<Diagnostic>* diagnostics,
                            std::size_t from) const
{
    if (!diagnostics) {
        return;
    }
    for (std::size_t index = from; index != diagnostics->size(); ++index) {
        (*diagnostics)[index].source = _assembler.GetSource();
    }
}

std::size_t
VmcLiveSource::PushDatagram(const std::uint8_t* bytes, std::size_t size,
                            double receiveTime,
                            std::vector<Diagnostic>* diagnostics)
{
    const std::size_t before = diagnostics ? diagnostics->size() : 0;

    OscPacket osc;
    Diagnostic refusal;
    if (!DecodeOscPacket(bytes, size, &osc, &refusal)) {
        ++_stats.datagramsRefused;
        if (diagnostics) {
            diagnostics->push_back(std::move(refusal));
            _StampSource(diagnostics, before);
        }
        return 0;
    }
    ++_stats.datagramsDecoded;

    VmcPacket packet;
    DecodeVmcPacket(osc, &packet, diagnostics);
    _StampSource(diagnostics, before);
    // The assembler stamps its own from here on, including the ones it passes
    // through from the skeleton map, so the stamp above covers exactly the two
    // layers that could not.
    return PushPacket(packet, receiveTime, diagnostics);
}

std::size_t
VmcLiveSource::PushPacket(const VmcPacket& packet, double receiveTime,
                          std::vector<Diagnostic>* diagnostics)
{
    _frames.clear();
    _assembler.Push(packet, receiveTime, &_frames, diagnostics);
    return _Deliver();
}

std::size_t
VmcLiveSource::Flush(std::vector<Diagnostic>* diagnostics)
{
    _frames.clear();
    _assembler.Flush(&_frames, diagnostics);
    return _Deliver();
}

bool
VmcLiveSource::ConsumeSessionRestart() noexcept
{
    const bool pending = _restartPending;
    _restartPending = false;
    return pending;
}

motion::PoseSampleResult
VmcLiveSource::Sample(double evaluationTime)
{
    return _intake.Sample(evaluationTime);
}

motion::MotionSourceMetadata
VmcLiveSource::GetSourceMetadata() const
{
    // The intake's rather than the assembler's: this reports the provenance
    // actually stamped on the poses a caller is sampling, which lags the
    // assembler's by nothing except a handshake that arrived after the last
    // frame was delivered.
    return _intake.GetSourceMetadata();
}

bool
VmcLiveSource::GetTimeRange(double* startTime, double* endTime) const
{
    return _intake.GetTimeRange(startTime, endTime);
}

void
VmcLiveSource::Reset()
{
    _assembler.Reset();
    _intake.Reset();
    _frames.clear();
    _restartPending = false;
    _metadata = _assembler.GetSourceMetadata();
    _intake.SetSourceMetadata(_metadata);
}

} // namespace vrmAdapterVmc
