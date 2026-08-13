// SPDX-License-Identifier: Apache-2.0

#include "vrmAdapterMocopi/FrameAssembler.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace vrmAdapterMocopi
{

namespace
{

// `std::to_string` on a double gives six decimals, which is the precision the
// recorded capture format keeps and so the precision at which two readers of one
// session can be expected to agree (PacketCapture.h).
std::string
Seconds(double value)
{
    return std::to_string(value);
}

} // namespace

MocopiFrameAssembler::MocopiFrameAssembler(const MocopiFrameConfig& config)
    : _config(config)
{
    _metadata.kind = motion::MotionSourceKind::LiveCapture;
    _metadata.protocol = "mocopi";
}

void
MocopiFrameAssembler::SetSource(std::string source)
{
    _source = std::move(source);
}

void
MocopiFrameAssembler::_ForgetSession()
{
    _map = SkeletonMap();
    _hasMap = false;
    // Re-armed rather than left set: the session that follows a restart is
    // entitled to be told once that it is waiting for a rig, exactly as the
    // first one was.
    _reportedNoRig = false;
    _lastStreamSeconds.reset();
    _lastFrameNumber.reset();
    _sessionEpoch.reset();
}

void
MocopiFrameAssembler::Reset()
{
    _ForgetSession();
    _pendingNewSession = false;
    _metadata = motion::MotionSourceMetadata();
    _metadata.kind = motion::MotionSourceKind::LiveCapture;
    _metadata.protocol = "mocopi";
}

void
MocopiFrameAssembler::_Report(std::vector<Diagnostic>* diagnostics,
                              DiagnosticCode code, std::string_view subject,
                              std::optional<double> timestamp,
                              std::string detail)
{
    if (!diagnostics) {
        return;
    }
    Diagnostic diagnostic = MakeDiagnostic(code, std::move(detail));
    diagnostic.source = _source;
    diagnostic.subject.assign(subject);
    diagnostic.timestamp = timestamp;
    diagnostic.sequence = _packetSerial;
    diagnostics->push_back(std::move(diagnostic));
}

bool
MocopiFrameAssembler::_PushSkeleton(const MotionSkeleton& skeleton,
                                    std::vector<Diagnostic>* diagnostics)
{
    // `MakeSkeletonMap` knows nothing about a session, so it stamps neither the
    // source nor a sequence. Everything it appends here is stamped afterwards,
    // so one session's log reads uniformly whichever layer raised a line.
    const std::size_t before = diagnostics ? diagnostics->size() : 0;

    SkeletonMap built;
    const bool ok = MakeSkeletonMap(skeleton, &built, diagnostics);

    if (diagnostics) {
        for (std::size_t index = before; index != diagnostics->size(); ++index) {
            (*diagnostics)[index].source = _source;
            (*diagnostics)[index].sequence = _packetSerial;
        }
    }

    if (!ok) {
        ++_stats.skeletonsRefused;
        // The existing rig stands. A rig is a session-long thing worth keeping
        // when a packet fails to replace it, and the device repeats the same
        // table about every 3.5 s — so a single corrupt skeleton packet costs
        // nothing, where dropping the rig would blind the session until the next
        // one arrived.
        return false;
    }

    ++_stats.skeletonsAccepted;
    _map = std::move(built);
    _hasMap = true;
    _reportedNoRig = false;
    return true;
}

bool
MocopiFrameAssembler::_PushFrame(const MotionFrame& frame,
                                 std::vector<MocopiFrame>* frames,
                                 std::vector<Diagnostic>* diagnostics)
{
    const double streamSeconds = static_cast<double>(frame.streamSeconds);

    // Ordering is decided before the rig is consulted, and the order of those
    // two matters: whether a datagram was delivered twice is a property of the
    // packet rather than of the rig, so a duplicate arriving before any skeleton
    // packet is still reported as a duplicate rather than as a missing rig.
    if (_lastFrameNumber && frame.frameNumber == *_lastFrameNumber) {
        ++_stats.framesRefusedOutOfOrder;
        _Report(diagnostics, DiagnosticCode::TimestampInvalid, {}, streamSeconds,
                "frame " + std::to_string(frame.frameNumber)
                    + " was already accepted at " + Seconds(streamSeconds)
                    + " s; a delivery repeated rather than a frame advanced");
        return false;
    }

    bool restart = false;
    if (_lastStreamSeconds && streamSeconds <= *_lastStreamSeconds) {
        const double backwards = *_lastStreamSeconds - streamSeconds;
        const bool counterBackwards =
            _lastFrameNumber && frame.frameNumber < *_lastFrameNumber;
        const bool clockRestarted = _config.restartBackwardsSeconds > 0.0
                                    && backwards > _config.restartBackwardsSeconds;

        if (counterBackwards || clockRestarted) {
            restart = true;
            ++_stats.sessionRestarts;
            _Report(diagnostics, DiagnosticCode::SourceRestarted, {},
                    streamSeconds,
                    counterBackwards
                        ? "the counter began again at "
                              + std::to_string(frame.frameNumber) + " from "
                              + std::to_string(*_lastFrameNumber)
                              + ", with the stream clock " + Seconds(backwards)
                              + " s back"
                        : "the stream clock began again " + Seconds(backwards)
                              + " s before the last accepted frame");
            // Everything the old stream established, dropped — the rig included.
            // See the header for what that costs and why it is the safe
            // direction.
            _ForgetSession();
            // Carried until a frame is actually emitted: the restart is seen on
            // a frame this class is about to refuse for having no rig, so the
            // flag has to outlive it and land on the new session's first real
            // frame.
            _pendingNewSession = true;
        } else {
            ++_stats.framesRefusedOutOfOrder;
            _Report(diagnostics, DiagnosticCode::TimestampInvalid, {},
                    streamSeconds,
                    "frame at " + Seconds(streamSeconds)
                        + " s does not advance on the last accepted frame at "
                        + Seconds(*_lastStreamSeconds) + " s");
            return false;
        }
    }

    if (!_hasMap) {
        ++_stats.framesRefusedNoRig;
        if (!_reportedNoRig) {
            _reportedNoRig = true;
            _Report(diagnostics, DiagnosticCode::FrameIncomplete, {},
                    streamSeconds,
                    "no skeleton packet has declared this session's rig yet, so "
                    "a bone id names a position in nothing; frames are refused "
                    "until one arrives");
        }
        return false;
    }

    FrameMapping mapping;
    const std::size_t before = diagnostics ? diagnostics->size() : 0;
    const bool mapped = MapMotionFrame(_map, frame, &mapping, diagnostics);
    if (diagnostics) {
        for (std::size_t index = before; index != diagnostics->size(); ++index) {
            (*diagnostics)[index].source = _source;
            (*diagnostics)[index].sequence = _packetSerial;
            (*diagnostics)[index].timestamp = streamSeconds;
        }
    }

    if (!mapped) {
        ++_stats.framesRefusedEmpty;
        _Report(diagnostics, DiagnosticCode::FrameIncomplete, {}, streamSeconds,
                "the frame formed no canonical bone at all, which is an absence "
                "of a pose rather than a sparse one");
        return false;
    }

    MocopiFrame out;
    out.pose.timestamp = streamSeconds;
    out.pose.validRotations = mapping.present;
    for (const BoneSample& sample : mapping.bones) {
        out.pose.localRotations[static_cast<std::size_t>(sample.bone)] =
            sample.localRotation;
    }

    out.frameNumber = frame.frameNumber;
    out.senderUnixSeconds = frame.senderUnixSeconds;
    out.beginsNewSession = _pendingNewSession || restart;
    _pendingNewSession = false;

    // The offset between the two clocks is fixed by the session's first accepted
    // frame, so that frame's drift is zero by construction and every later one is
    // measured against it.
    const double offset = frame.senderUnixSeconds - streamSeconds;
    if (!_sessionEpoch) {
        _sessionEpoch = offset;
    }
    out.clockDrift = offset - *_sessionEpoch;

    if (_lastFrameNumber && frame.frameNumber > *_lastFrameNumber + 1) {
        out.lostFrames = frame.frameNumber - *_lastFrameNumber - 1;
        _stats.framesLost += out.lostFrames;
    }

    if (mapping.hasHipsPosition) {
        out.hipsPosition = mapping.hipsPosition;
    }
    out.unusedJoints = mapping.unusedJoints;
    out.droppedTranslations = mapping.droppedTranslations;
    _stats.unusedJoints += mapping.unusedJoints;
    _stats.droppedTranslations += mapping.droppedTranslations;

    // Against the rig the session *declared*, never against the full canonical
    // humanoid: a rig that ends at the wrists is complete without fingers.
    out.missing = _map.present & ~mapping.present;
    if (out.missing.any()) {
        ++_stats.framesIncomplete;
        _Report(diagnostics, DiagnosticCode::FrameIncomplete, {}, streamSeconds,
                std::to_string(out.missing.count()) + " of "
                    + std::to_string(_map.present.count())
                    + " declared bone(s) absent from this frame");
    }

    _lastStreamSeconds = streamSeconds;
    _lastFrameNumber = frame.frameNumber;
    ++_stats.framesEmitted;
    if (frames) {
        frames->push_back(std::move(out));
    }
    return true;
}

bool
MocopiFrameAssembler::Push(const MotionPacket& packet, double receiveTime,
                           std::vector<MocopiFrame>* frames,
                           std::vector<Diagnostic>* diagnostics)
{
    ++_packetSerial;
    // The receiver's clock is deliberately unread. Every frame this protocol
    // sends carries the sender's own `time`, so there is no receive fallback to
    // stamp a frame with — the parameter is kept because a caller replaying a
    // capture has the value and a later diagnostic may want it, and dropping it
    // from the signature would be the change that is hard to undo.
    (void)receiveTime;

    switch (packet.kind) {
    case MotionPacketKind::Skeleton:
        if (packet.skeleton) {
            _PushSkeleton(*packet.skeleton, diagnostics);
        }
        // A skeleton packet is never a frame, whatever it did to the rig.
        return false;

    case MotionPacketKind::Frame:
        if (!packet.frame) {
            return false;
        }
        return _PushFrame(*packet.frame, frames, diagnostics);

    case MotionPacketKind::Count:
        break;
    }
    return false;
}

} // namespace vrmAdapterMocopi
