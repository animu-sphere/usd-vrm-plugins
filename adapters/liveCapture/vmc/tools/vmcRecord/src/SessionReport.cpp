// SPDX-License-Identifier: Apache-2.0
#include "SessionReport.h"

#include "motionRuntime/LiveCaptureSource.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace vmcRecordTool
{
namespace
{

std::string
Number(double value)
{
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.6g", value);
    return buffer;
}

std::string
Vector(const pxr::GfVec3f& value)
{
    // `+ 0.0` normalises a negative zero, which is not cosmetic here: the basis
    // change reflects X, so every zero X component in a converted position
    // comes out as -0 and a report reading "(-0, 0.9, 0)" invites a reader to
    // look for a sign error that is not there. IEEE says -0.0 + 0.0 is +0.0
    // under round-to-nearest, and leaves every other value alone.
    char buffer[96];
    std::snprintf(buffer, sizeof(buffer), "(%.4g, %.4g, %.4g)",
                  static_cast<double>(value[0]) + 0.0,
                  static_cast<double>(value[1]) + 0.0,
                  static_cast<double>(value[2]) + 0.0);
    return buffer;
}

// The largest component-wise distance between two positions. Component-wise
// rather than a norm because the question these numbers answer is which *axis*
// moved: a hips offset that varies only in Y is a sender's floor contact, and
// one that varies in X and Z is the body walking around.
float
MaxComponentDistance(const pxr::GfVec3f& a, const pxr::GfVec3f& b)
{
    float largest = 0.0f;
    for (int axis = 0; axis < 3; ++axis) {
        largest = std::max(largest, std::fabs(a[axis] - b[axis]));
    }
    return largest;
}

} // namespace

const char*
StopReasonText(StopReason reason) noexcept
{
    switch (reason) {
    case StopReason::Interrupted:
        return "interrupted";
    case StopReason::Duration:
        return "--duration elapsed";
    case StopReason::IdleTimeout:
        return "--idle-timeout elapsed with nothing arriving";
    case StopReason::MaxDatagrams:
        return "--max-datagrams reached";
    case StopReason::EndOfCapture:
        return "end of capture";
    case StopReason::ReceiveFailed:
        break;
    }
    return "the socket failed";
}

void
SessionReport::ObserveDatagram(const std::string& peer, std::size_t bytes,
                               double receiveTime)
{
    if (_datagrams == 0) {
        _firstReceiveTime = receiveTime;
    }
    _lastReceiveTime = receiveTime;
    ++_datagrams;
    _payloadBytes += bytes;

    if (peer.empty()) {
        return;
    }
    if (std::find(_peers.begin(), _peers.end(), peer) != _peers.end()) {
        return;
    }
    ++_peerCount;
    if (_peers.size() < kMaxNamedPeers) {
        _peers.push_back(peer);
    }
}

void
SessionReport::ObserveFrames(const std::vector<vrmAdapterVmc::VmcFrame>& frames)
{
    for (const vrmAdapterVmc::VmcFrame& frame : frames) {
        ++_frames;
        if (frame.missing.any()) {
            ++_framesIncomplete;
        }
        if (!frame.timestampFromSender) {
            ++_framesFromReceiveClock;
        }
        if (frame.beginsNewSession) {
            ++_framesBeginningSession;
        }
        _observed |= frame.pose.validRotations;
        // The union across the session, because the vocabulary is the sender's
        // and an operator judging a capture wants to know which names it holds
        // -- not merely that some arrived.
        for (const motion::ExpressionWeight& weight :
             frame.pose.expressions.entries) {
            _expressionNames.insert(weight.name);
        }

        const double timestamp = frame.pose.timestamp;
        if (!_haveFrameTime) {
            _haveFrameTime = true;
        } else if (!frame.beginsNewSession) {
            // Within one session the assembler emits strictly advancing frames,
            // so this interval is positive by construction. Across a restart it
            // would not be, which is why the restart case is excluded rather
            // than clamped.
            const double interval = timestamp - _lastFrameTime;
            if (_intervals == 0) {
                _intervalMin = interval;
                _intervalMax = interval;
            } else {
                _intervalMin = std::min(_intervalMin, interval);
                _intervalMax = std::max(_intervalMax, interval);
            }
            _intervalSum += interval;
            ++_intervals;
        }
        _lastFrameTime = timestamp;

        if (frame.hipsOffset) {
            const pxr::GfVec3f& hips = *frame.hipsOffset;
            const float length = hips.GetLength();
            if (_framesWithHips == 0) {
                _firstHips = hips;
                _hipsMinLength = length;
                _hipsMaxLength = length;
            } else {
                _hipsMinLength = std::min(_hipsMinLength, length);
                _hipsMaxLength = std::max(_hipsMaxLength, length);
                _hipsMaxDeviation = std::max(
                    _hipsMaxDeviation, MaxComponentDistance(hips, _firstHips));
            }
            ++_framesWithHips;
        }

        if (frame.pose.root.hasPosition) {
            const pxr::GfVec3f& position = frame.pose.root.worldPosition;
            if (_framesWithRoot == 0) {
                _firstRoot = position;
            } else {
                _rootMaxDeviation = std::max(
                    _rootMaxDeviation,
                    MaxComponentDistance(position, _firstRoot));
            }
            ++_framesWithRoot;
        }
    }
}

void
SessionReport::ObserveDiagnostics(
    const std::vector<vrmAdapterVmc::Diagnostic>& log, std::size_t from)
{
    for (std::size_t i = from; i < log.size(); ++i) {
        const auto index = static_cast<std::size_t>(log[i].code);
        if (index >= vrmAdapterVmc::DiagnosticCodeCount) {
            continue;
        }
        if (_diagnostics[index] == 0) {
            _firstDiagnostic[index] = log[i];
        }
        ++_diagnostics[index];
    }
}

void
SessionReport::Print(std::FILE* out,
                     const vrmAdapterVmc::VmcLiveSource& source,
                     const vrmAdapterVmc::UdpReceiver* receiver) const
{
    const vrmAdapterVmc::VmcLiveSourceStats& bridge = source.GetStats();
    const vrmAdapterVmc::VmcFrameStats& assembly =
        source.GetAssembler().GetStats();
    const motion::LiveCaptureStats& intake = source.GetIntake().GetStats();

    if (receiver) {
        const vrmAdapterVmc::UdpReceiverStats& socket = receiver->GetStats();
        std::fprintf(out, "listen:      %s%s, receive buffer %zu bytes\n",
                     receiver->GetBoundEndpoint().c_str(),
                     receiver->IsLoopbackOnly()
                         ? " (loopback only: no other machine can reach it)"
                         : "",
                     receiver->GetReceiveBufferBytes());
        if (socket.receiveErrors != 0 || socket.datagramsTruncated != 0) {
            std::fprintf(out,
                         "socket:      %llu transient error(s), %llu datagram(s)"
                         " over the size limit and dropped\n",
                         static_cast<unsigned long long>(socket.receiveErrors),
                         static_cast<unsigned long long>(
                             socket.datagramsTruncated));
        }
    }

    std::fprintf(out, "received:    %llu datagram(s), %llu byte(s) over %s s\n",
                 static_cast<unsigned long long>(_datagrams),
                 static_cast<unsigned long long>(_payloadBytes),
                 Number(_datagrams == 0
                            ? 0.0
                            : _lastReceiveTime - _firstReceiveTime).c_str());

    std::fprintf(out, "peers:       %llu",
                 static_cast<unsigned long long>(_peerCount));
    for (std::size_t i = 0; i < _peers.size(); ++i) {
        std::fprintf(out, "%s%s", i == 0 ? " (" : ", ", _peers[i].c_str());
    }
    if (!_peers.empty()) {
        std::fprintf(out, "%s)", _peerCount > _peers.size() ? ", ..." : "");
    }
    std::fputc('\n', out);

    std::fprintf(out,
                 "decoded:     %llu datagram(s), %llu refused whole; "
                 "%llu message(s) ignored\n",
                 static_cast<unsigned long long>(bridge.datagramsDecoded),
                 static_cast<unsigned long long>(bridge.datagramsRefused),
                 static_cast<unsigned long long>(assembly.messagesIgnored));

    std::fprintf(out,
                 "frames:      %llu emitted, %llu incomplete; "
                 "%llu refused out of order, %llu refused empty\n",
                 static_cast<unsigned long long>(_frames),
                 static_cast<unsigned long long>(_framesIncomplete),
                 static_cast<unsigned long long>(
                     assembly.framesRefusedOutOfOrder),
                 static_cast<unsigned long long>(assembly.framesRefusedEmpty));

    if (_intervals != 0) {
        const double mean = _intervalSum / static_cast<double>(_intervals);
        std::fprintf(out,
                     "cadence:     %s Hz mean, interval %s-%s s, over %s s of "
                     "sender clock\n",
                     Number(mean > 0.0 ? 1.0 / mean : 0.0).c_str(),
                     Number(_intervalMin).c_str(), Number(_intervalMax).c_str(),
                     Number(_intervalSum).c_str());
    } else {
        std::fprintf(out, "cadence:     not measurable from %llu frame(s)\n",
                     static_cast<unsigned long long>(_frames));
    }

    std::fprintf(out,
                 "bones:       %zu of %zu observed; %llu accepted, "
                 "%llu duplicated, %llu unsupported, %llu malformed\n",
                 _observed.count(), motion::HumanBoneCount,
                 static_cast<unsigned long long>(assembly.bonesAccepted),
                 static_cast<unsigned long long>(assembly.bonesDuplicated),
                 static_cast<unsigned long long>(assembly.bonesUnsupported),
                 static_cast<unsigned long long>(assembly.bonesMalformed));

    // Only when the session carried any. A sender that sends no face should not
    // be reported as sending an empty one, for the same reason the bone line is
    // measured against what was observed rather than against all 55.
    if (assembly.expressionsAccepted != 0 || !_expressionNames.empty()) {
        std::fprintf(out,
                     "expressions: %zu name(s); %llu accepted, "
                     "%llu duplicated\n",
                     _expressionNames.size(),
                     static_cast<unsigned long long>(
                         assembly.expressionsAccepted),
                     static_cast<unsigned long long>(
                         assembly.expressionsDuplicated));
        std::fprintf(out, "             ");
        std::size_t printed = 0;
        for (const std::string& name : _expressionNames) {
            // The names are the sender's, so the list is evidence rather than
            // decoration -- but a face rig can carry dozens and the report is
            // meant to be read.
            if (printed == 12) {
                std::fprintf(out, ", ... (%zu more)",
                             _expressionNames.size() - printed);
                break;
            }
            std::fprintf(out, "%s%s", printed == 0 ? "" : ", ", name.c_str());
            ++printed;
        }
        std::fputc('\n', out);
    }

    std::fprintf(out,
                 "clock:       %llu frame(s) stamped by the sender, %llu by the "
                 "receiver; %llu restart(s)\n",
                 static_cast<unsigned long long>(_frames
                                                 - _framesFromReceiveClock),
                 static_cast<unsigned long long>(_framesFromReceiveClock),
                 static_cast<unsigned long long>(assembly.sessionRestarts));

    std::fprintf(out,
                 "intake:      %llu frame(s) delivered, %llu admitted, "
                 "%llu refused; %llu session reset(s)\n",
                 static_cast<unsigned long long>(bridge.framesDelivered),
                 static_cast<unsigned long long>(bridge.framesAdmitted),
                 static_cast<unsigned long long>(bridge.framesRefused),
                 static_cast<unsigned long long>(bridge.sessionsReset));
    if (intake.framesRejectedOutOfOrder != 0 || intake.framesRejectedStale != 0) {
        std::fprintf(out,
                     "             the intake refused %llu out of order and "
                     "%llu stale\n",
                     static_cast<unsigned long long>(
                         intake.framesRejectedOutOfOrder),
                     static_cast<unsigned long long>(
                         intake.framesRejectedStale));
    }

    _PrintEvidence(out);
    _PrintDiagnostics(out);

    const motion::MotionSourceMetadata& metadata =
        source.GetAssembler().GetSourceMetadata();
    if (!metadata.sourceId.empty()) {
        // Said rather than used. The title is in the recorded payload whatever
        // this tool does with it, so an operator deciding whether a capture can
        // be committed has to be told it is in there.
        std::fprintf(out,
                     "model:       \"%s\" - the sender's model title, and it is "
                     "in the recorded bytes\n",
                     metadata.sourceId.c_str());
    }

    std::fprintf(out, "stopped:     %s\n", StopReasonText(_stop));
}

void
SessionReport::_PrintEvidence(std::FILE* out) const
{
    // The two questions the adapter left for a real sender (FrameAssembler.h).
    // Reported as measurements and never as a conclusion: this tool is in no
    // better position to decide what a sender means by a field than the layer
    // that declined to.
    if (_framesWithHips != 0) {
        std::fprintf(out,
                     "hips offset: %llu frame(s), |offset| %s-%s m, moved at "
                     "most %s m from the first%s\n",
                     static_cast<unsigned long long>(_framesWithHips),
                     Number(_hipsMinLength).c_str(),
                     Number(_hipsMaxLength).c_str(),
                     Number(_hipsMaxDeviation).c_str(),
                     _hipsMaxDeviation == 0.0f
                         ? " (constant: rig geometry, not translation)"
                         : "");
        std::fprintf(out, "             first %s\n", Vector(_firstHips).c_str());
    } else if (_frames != 0) {
        std::fprintf(out, "hips offset: none in %llu frame(s)\n",
                     static_cast<unsigned long long>(_frames));
    }

    if (_framesWithRoot != 0) {
        std::fprintf(out,
                     "root:        %llu frame(s) with a position, moved at most "
                     "%s m from %s%s\n",
                     static_cast<unsigned long long>(_framesWithRoot),
                     Number(_rootMaxDeviation).c_str(),
                     Vector(_firstRoot).c_str(),
                     _rootMaxDeviation == 0.0f ? " (constant)" : "");
    }
}

void
SessionReport::_PrintDiagnostics(std::FILE* out) const
{
    std::uint64_t total = 0;
    for (const std::uint64_t count : _diagnostics) {
        total += count;
    }
    if (total == 0) {
        std::fprintf(out, "diagnostics: none\n");
        return;
    }

    for (std::size_t index = 0; index < vrmAdapterVmc::DiagnosticCodeCount;
         ++index) {
        if (_diagnostics[index] == 0) {
            continue;
        }
        const auto code = static_cast<vrmAdapterVmc::DiagnosticCode>(index);
        std::fprintf(out, "diagnostics: %llu x %s (%s)\n",
                     static_cast<unsigned long long>(_diagnostics[index]),
                     std::string(vrmAdapterVmc::DiagnosticCodeString(code))
                         .c_str(),
                     std::string(vrmAdapterVmc::DiagnosticSeverityString(
                                     vrmAdapterVmc::DiagnosticDefaultSeverity(
                                         code)))
                         .c_str());
        std::fprintf(out, "             first: %s\n",
                     vrmAdapterVmc::FormatDiagnostic(_firstDiagnostic[index])
                         .c_str());
    }
}

} // namespace vmcRecordTool
