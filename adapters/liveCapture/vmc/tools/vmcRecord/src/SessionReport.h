// SPDX-License-Identifier: Apache-2.0
//
// What a session was, in the order an operator asks it.
//
// The adapter's five layers each count what they alone can see — `VmcFrameStats`
// knows nothing about a datagram that never decoded, `UdpReceiverStats` nothing
// about a bone. This gathers those tallies and the few facts none of them holds,
// and prints one block that answers four questions in the order they are asked
// when a live session is not working:
//
//     is anything arriving      -> the socket's counts, and from whom
//     does it decode            -> the OSC and VMC layers' counts
//     does it become motion     -> frames, cadence, the rig observed
//     what went wrong           -> the diagnostics, by code
//
// Then one more that nobody asks and this repository needs: **what does this
// session say about the two questions the adapter left open** — whether the
// hips offset is body translation or rig geometry, and whether a real sender's
// root transform moves at all (FrameAssembler.h, roadmap Milestone B). Those
// are answered by evidence from a real sender or not at all, so the tool that
// meets the first real sender is the one that has to collect it.
//
// Everything here is accumulated from `VmcLiveSource::GetFramesFromLastPush()`,
// which is valid only until the next push — so this class copies what it needs
// per frame and holds no reference into the source.
#pragma once

#include "vrmAdapterVmc/Diagnostics.h"
#include "vrmAdapterVmc/FrameAssembler.h"
#include "vrmAdapterVmc/LiveSource.h"
#include "vrmAdapterVmc/UdpReceiver.h"

#include "motionCore/Humanoid.h"

#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

namespace vmcRecordTool
{

// Why the session ended. Exactly one of these is true of any run, which is the
// point: a recording that stopped early because a flag said so and one that
// stopped early because the socket failed are different sessions, and a capture
// file cannot tell them apart afterwards.
enum class StopReason : std::uint8_t
{
    Interrupted,
    Duration,
    IdleTimeout,
    MaxDatagrams,
    EndOfCapture,
    ReceiveFailed,
};

const char* StopReasonText(StopReason reason) noexcept;

class SessionReport
{
public:
    // One received datagram, before anything has decoded it.
    void ObserveDatagram(const std::string& peer, std::size_t bytes,
                         double receiveTime);

    // The frames one push produced. Called with the source's window straight
    // after every push, including the empty ones — a push that completes no
    // frame is the normal case for a per-message sender.
    void ObserveFrames(const std::vector<vrmAdapterVmc::VmcFrame>& frames);

    // The diagnostics one push appended. The caller's list is passed as the
    // slice this push added, so a caller accumulating a whole session's list
    // does not re-count it.
    void ObserveDiagnostics(const std::vector<vrmAdapterVmc::Diagnostic>& log,
                            std::size_t from);

    void SetStopReason(StopReason reason) noexcept { _stop = reason; }
    StopReason GetStopReason() const noexcept { return _stop; }

    std::uint64_t GetDatagramCount() const noexcept { return _datagrams; }
    std::uint64_t GetFrameCount() const noexcept { return _frames; }

    // Whether the session heard from more than one sender. The capture format
    // names one peer in its header, so this is the difference between a
    // fixture's provenance being true and being the first of several.
    bool HasMultiplePeers() const noexcept { return _peers.size() > 1; }
    const std::vector<std::string>& GetPeers() const noexcept { return _peers; }

    // Prints the block. `receiver` is null when the session came off a file:
    // the socket lines are then omitted rather than printed as zeroes, because
    // a bound endpoint a replay never had is not a fact about the replay.
    void Print(std::FILE* out, const vrmAdapterVmc::VmcLiveSource& source,
               const vrmAdapterVmc::UdpReceiver* receiver) const;

private:
    void _PrintEvidence(std::FILE* out) const;
    void _PrintDiagnostics(std::FILE* out) const;

    std::uint64_t _datagrams = 0;
    std::uint64_t _payloadBytes = 0;
    double _firstReceiveTime = 0.0;
    double _lastReceiveTime = 0.0;

    // Bounded: a session receiving from a hundred hosts is a misconfiguration,
    // and the report needs to say that rather than list them.
    static constexpr std::size_t kMaxNamedPeers = 4;
    std::vector<std::string> _peers;
    std::uint64_t _peerCount = 0;

    std::uint64_t _frames = 0;
    std::uint64_t _framesIncomplete = 0;
    std::uint64_t _framesFromReceiveClock = 0;
    std::uint64_t _framesBeginningSession = 0;

    // Cadence, measured from the sender's own clock over intervals within one
    // session. An interval spanning a restart is not an interval — it is the
    // gap between two streams — so it is left out rather than averaged in, and
    // the span the mean is over is the sum of the intervals rather than the
    // distance from the first frame to the last. On a capture that restarts,
    // that distance is *negative*: the second session's clock starts before the
    // first one's ended, which is what a restart is.
    double _lastFrameTime = 0.0;
    double _intervalSum = 0.0;
    double _intervalMin = 0.0;
    double _intervalMax = 0.0;
    std::uint64_t _intervals = 0;
    bool _haveFrameTime = false;

    // The union across restarts, which is deliberately not the assembler's
    // `GetObservedBones()`: that one is reset by a restart, and a report of
    // what a sender sends should not shrink because the operator restarted it.
    std::bitset<motion::HumanBoneCount> _observed;

    // The same union for expression names, which cannot be a bitset because the
    // vocabulary is the sender's model's rather than a fixed one
    // (motionCore/Humanoid.h). Sorted, so two runs of the same capture report
    // the names in the same order.
    std::set<std::string> _expressionNames;

    // The first open question's evidence. A hips offset that never moves is rig
    // geometry; one that tracks the body is translation. Neither this tool nor
    // the adapter decides which — it reports the numbers that decide it.
    std::uint64_t _framesWithHips = 0;
    float _hipsMinLength = 0.0f;
    float _hipsMaxLength = 0.0f;
    float _hipsMaxDeviation = 0.0f;
    pxr::GfVec3f _firstHips = pxr::GfVec3f(0.0f);

    // The second: whether `/VMC/Ext/Root/Pos` moves at all in a real session.
    std::uint64_t _framesWithRoot = 0;
    float _rootMaxDeviation = 0.0f;
    pxr::GfVec3f _firstRoot = pxr::GfVec3f(0.0f);

    std::array<std::uint64_t, vrmAdapterVmc::DiagnosticCodeCount> _diagnostics{};
    // The first of each code, kept whole. A count says a session had 400
    // malformed packets; the first line says which address and which byte.
    std::array<vrmAdapterVmc::Diagnostic, vrmAdapterVmc::DiagnosticCodeCount>
        _firstDiagnostic{};

    StopReason _stop = StopReason::EndOfCapture;
};

} // namespace vmcRecordTool
