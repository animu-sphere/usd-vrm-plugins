// SPDX-License-Identifier: Apache-2.0

#include "vrmAdapterVmc/FrameAssembler.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace vrmAdapterVmc
{

VmcFrameAssembler::VmcFrameAssembler(const VmcFrameConfig& config)
    : _config(config)
{
    _lastSeen.fill(0.0);
    _metadata.kind = motion::MotionSourceKind::LiveCapture;
    _metadata.protocol = "vmc";
}

void
VmcFrameAssembler::SetSource(std::string source)
{
    _source = std::move(source);
}

void
VmcFrameAssembler::Reset()
{
    _frame = OpenFrame();
    _observed.reset();
    _reportedStale.reset();
    _lastSeen.fill(0.0);
    _lastAccepted.reset();
    _metadata = motion::MotionSourceMetadata();
    _metadata.kind = motion::MotionSourceKind::LiveCapture;
    _metadata.protocol = "vmc";
}

void
VmcFrameAssembler::_Report(std::vector<Diagnostic>* diagnostics,
                           DiagnosticCode code, std::string_view subject,
                           std::optional<double> timestamp, std::string detail)
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

void
VmcFrameAssembler::_Open(double receiveTime)
{
    _frame = OpenFrame();
    _frame.open = true;
    _frame.receiveTime = receiveTime;
}

bool
VmcFrameAssembler::_Close(std::vector<VmcFrame>* frames,
                          std::vector<Diagnostic>* diagnostics)
{
    if (!_frame.open) {
        return false;
    }
    // Taken before anything can fail, so every path below leaves the assembler
    // with no frame open rather than with the one it just refused.
    OpenFrame frame = std::move(_frame);
    _frame = OpenFrame();

    // Expressions count as content: a pose can hold them, so a frame carrying
    // only them is carrying motion rather than an empty boundary.
    if (!frame.pose.validRotations.any() && !frame.hasRoot
        && frame.pose.expressions.IsEmpty()) {
        ++_stats.framesRefusedEmpty;
        _Report(diagnostics, DiagnosticCode::IncompleteFrame, {},
                frame.senderTime,
                "frame boundary carried neither a bone, a root, nor an "
                "expression");
        return false;
    }

    const double timestamp =
        frame.senderTime ? *frame.senderTime : frame.receiveTime;

    bool restart = false;
    if (_lastAccepted && timestamp <= *_lastAccepted) {
        const double backwards = *_lastAccepted - timestamp;
        if (_config.restartBackwardsSeconds > 0.0
            && backwards > _config.restartBackwardsSeconds) {
            restart = true;
        } else {
            ++_stats.framesRefusedOutOfOrder;
            _Report(diagnostics, DiagnosticCode::TimestampRegression, {},
                    timestamp,
                    "frame at " + std::to_string(timestamp)
                        + " s does not advance on the last accepted frame at "
                        + std::to_string(*_lastAccepted) + " s");
            return false;
        }
    }

    if (restart) {
        ++_stats.sessionRestarts;
        _Report(diagnostics, DiagnosticCode::SourceRestarted, {}, timestamp,
                "the sender's clock began again "
                    + std::to_string(*_lastAccepted - timestamp)
                    + " s before the last accepted frame");
        // Everything the old session taught is about a stream that has ended.
        // Keeping the observed rig across a restart would report the new
        // session's first frames as incomplete for bones a different sender may
        // never solve.
        _observed.reset();
        _reportedStale.reset();
        _lastSeen.fill(0.0);
        _lastAccepted.reset();
    }

    VmcFrame out;
    out.pose = std::move(frame.pose);
    out.pose.timestamp = timestamp;
    out.beginsNewSession = restart;
    out.timestampFromSender = frame.senderTime.has_value();
    out.hipsOffset = frame.hipsOffset;
    out.duplicateBones = frame.duplicateBones;

    // Measured against the rig observed *before* this frame, so a bone appearing
    // for the first time is not also reported as having been missing.
    out.missing = _observed & ~out.pose.validRotations;
    if (_config.stalenessSeconds > 0.0) {
        for (std::size_t index = 0; index != motion::HumanBoneCount; ++index) {
            if (!out.missing.test(index)
                || timestamp - _lastSeen[index] <= _config.stalenessSeconds) {
                continue;
            }
            out.stale.set(index);
            if (_reportedStale.test(index)) {
                continue;
            }
            _reportedStale.set(index);
            ++_stats.stalenessCrossings;
            // Named the way the sender wrote it, like every other diagnostic
            // this adapter raises: an operator comparing a diagnostic against a
            // capture is reading Unity's spelling, not VRM 1.0's.
            _Report(diagnostics, DiagnosticCode::StaleJoint,
                    VmcHumanBoneName(static_cast<motion::HumanBone>(index)),
                    timestamp,
                    "no update for "
                        + std::to_string(timestamp - _lastSeen[index]) + " s");
        }
    }

    for (std::size_t index = 0; index != motion::HumanBoneCount; ++index) {
        if (!out.pose.validRotations.test(index)) {
            continue;
        }
        _observed.set(index);
        _lastSeen[index] = timestamp;
        _reportedStale.reset(index);
    }

    if (out.missing.any()) {
        ++_stats.framesIncomplete;
        _Report(diagnostics, DiagnosticCode::IncompleteFrame, {}, timestamp,
                std::to_string(out.missing.count()) + " of "
                    + std::to_string(_observed.count())
                    + " observed bone(s) absent from this frame");
    }

    _lastAccepted = timestamp;
    ++_stats.framesEmitted;
    if (frames) {
        frames->push_back(std::move(out));
    }
    return true;
}

std::size_t
VmcFrameAssembler::Push(const VmcPacket& packet, double receiveTime,
                        std::vector<VmcFrame>* frames,
                        std::vector<Diagnostic>* diagnostics)
{
    ++_packetSerial;
    std::size_t completed = 0;

    for (const VmcMessage& message : packet.messages) {
        switch (message.kind) {
        case VmcMessageKind::Time: {
            // A second clock cannot describe the frame the first one already
            // stamped, whichever end of the frame each arrived at.
            if (_frame.open && _frame.senderTime) {
                completed += _Close(frames, diagnostics) ? 1 : 0;
            }
            if (!_frame.open) {
                _Open(receiveTime);
            }
            _frame.senderTime = message.seconds;
            break;
        }

        case VmcMessageKind::BoneTransform: {
            VmcBoneSample sample;
            Diagnostic refusal;
            if (!MapVmcBoneTransform(message, &sample, &refusal)) {
                if (refusal.code == DiagnosticCode::UnsupportedMessage) {
                    ++_stats.bonesUnsupported;
                } else {
                    ++_stats.bonesMalformed;
                }
                if (diagnostics) {
                    refusal.source = _source;
                    refusal.sequence = _packetSerial;
                    refusal.timestamp = _frame.senderTime;
                    diagnostics->push_back(std::move(refusal));
                }
                break;
            }
            const std::size_t index = static_cast<std::size_t>(sample.bone);
            if (_frame.open && _frame.pose.validRotations.test(index)) {
                if (_frame.bonePacket[index] == _packetSerial) {
                    // One delivery cannot be two frames' worth of this bone.
                    ++_frame.duplicateBones;
                    ++_stats.bonesDuplicated;
                    _Report(diagnostics, DiagnosticCode::DuplicateBone,
                            message.name, _frame.senderTime,
                            "already carried by this frame from the same "
                            "datagram; the first value stands");
                    break;
                }
                completed += _Close(frames, diagnostics) ? 1 : 0;
            }
            if (!_frame.open) {
                _Open(receiveTime);
            }
            _frame.pose.localRotations[index] = sample.localRotation;
            _frame.pose.validRotations.set(index);
            _frame.bonePacket[index] = _packetSerial;
            if (sample.bone == motion::HumanBone::Hips) {
                _frame.hipsOffset = sample.localPosition;
            }
            ++_stats.bonesAccepted;
            break;
        }

        case VmcMessageKind::RootTransform: {
            motion::RootMotion root;
            Diagnostic refusal;
            if (!MapVmcRootTransform(message, &root, &refusal)) {
                ++_stats.rootsMalformed;
                if (diagnostics) {
                    refusal.source = _source;
                    refusal.sequence = _packetSerial;
                    refusal.timestamp = _frame.senderTime;
                    diagnostics->push_back(std::move(refusal));
                }
                break;
            }
            if (_frame.open && _frame.hasRoot) {
                if (_frame.rootPacket == _packetSerial) {
                    // `VRM_VMC_DUPLICATE_BONE` reads oddly for the root and is
                    // still the right code: the set is frozen (Diagnostics.h),
                    // and the root occupies the same one-per-frame slot a bone
                    // does, so a second one in a single delivery is the same
                    // phenomenon under a different address.
                    ++_frame.duplicateBones;
                    ++_stats.bonesDuplicated;
                    _Report(diagnostics, DiagnosticCode::DuplicateBone,
                            VmcMessageKindAddress(
                                VmcMessageKind::RootTransform),
                            _frame.senderTime,
                            "already carried by this frame from the same "
                            "datagram; the first value stands");
                    break;
                }
                completed += _Close(frames, diagnostics) ? 1 : 0;
            }
            if (!_frame.open) {
                _Open(receiveTime);
            }
            _frame.pose.root = root;
            _frame.hasRoot = true;
            _frame.rootPacket = _packetSerial;
            ++_stats.rootsAccepted;
            break;
        }

        case VmcMessageKind::Model:
            // Kept as provenance and never resolved: the title identifies the
            // stream, and the path names a file on the sender's machine.
            _metadata.sourceId.assign(message.title);
            ++_stats.messagesIgnored;
            break;

        case VmcMessageKind::BlendValue: {
            // The name is the sender's, and this layer has no vocabulary to
            // check it against (motionCore/Humanoid.h): the only thing that can
            // be wrong with it here is arriving twice.
            const std::string name(message.name);
            // `expressionPacket` is asked rather than `pose.expressions`,
            // although a name is written to both: two containers that must
            // agree about which names are present is one invariant more than
            // this needs, and the packet serial is the thing being tested.
            // Copied out rather than held as an iterator: `_Close` below
            // replaces the map, so anything still pointing into it would
            // dangle. The bone path reads its serial the same way.
            std::optional<std::uint64_t> carriedBy;
            if (_frame.open) {
                const auto seen = _frame.expressionPacket.find(name);
                if (seen != _frame.expressionPacket.end()) {
                    carriedBy = seen->second;
                }
            }
            if (carriedBy) {
                if (*carriedBy == _packetSerial) {
                    // Same reading as a repeated bone, and the same code: the
                    // set is frozen (Diagnostics.h), and a blend shape occupies
                    // the same one-per-frame slot a bone does.
                    ++_frame.duplicateBones;
                    ++_stats.expressionsDuplicated;
                    _Report(diagnostics, DiagnosticCode::DuplicateBone,
                            message.name, _frame.senderTime,
                            "already carried by this frame from the same "
                            "datagram; the first value stands");
                    break;
                }
                completed += _Close(frames, diagnostics) ? 1 : 0;
            }
            if (!_frame.open) {
                _Open(receiveTime);
            }
            _frame.pose.expressions.Set(name, message.value);
            _frame.expressionPacket[name] = _packetSerial;
            ++_stats.expressionsAccepted;
            break;
        }

        case VmcMessageKind::Availability:
        case VmcMessageKind::BlendApply:
        case VmcMessageKind::Count:
            // None of these carry a value, and none of them open, close, or
            // stamp a frame. `Blend/Apply` in particular is not read as "the
            // blend set is complete": that would make it a frame boundary, and
            // the header says why a third boundary rule is not something the
            // two sender shapes justify.
            ++_stats.messagesIgnored;
            break;
        }
    }

    return completed;
}

std::size_t
VmcFrameAssembler::Flush(std::vector<VmcFrame>* frames,
                         std::vector<Diagnostic>* diagnostics)
{
    return _Close(frames, diagnostics) ? 1 : 0;
}

} // namespace vrmAdapterVmc
