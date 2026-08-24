// SPDX-License-Identifier: Apache-2.0
//
// What a session was, for a session nothing here can read.
//
// The two sibling tools have five layers to gather from and answer four
// questions in the order an operator asks them -- is anything arriving, does it
// decode, does it become motion, what went wrong. This adapter has one layer, so
// only the first and the last of those have an answer here, and the discipline
// this file needs is about the middle two: **it must not invent them.**
//
// That discipline is harder to keep here than it was for the mocopi tool, and
// the reason is the one thing that looks like an advantage. This protocol's
// receiving end is *published*, so the addresses a decoder would look for are
// known, and a report that grouped datagrams by address could be written today
// from the specification alone. It would also be the first thing anybody read
// off a real session, and every number in it would be conditional on an
// assumption nobody has tested -- which is exactly the evidence VRC-1 exists to
// obtain honestly. The address inventory is that milestone's, measured from
// bytes, and this file stops one layer below it.
//
// So what is printed is what a socket can see:
//
//     is anything arriving      -> the counts, the peers, and the arrival rate
//     is it more than one thing -> the length census and the common prefix
//     what went wrong           -> the two transport codes, by code
//
// ## The census is observation, and the line between the two is the length
//
// Every number in the middle pair is a property of the datagram *envelope* --
// how many arrived, how long each was, and which leading bytes every one of them
// shares. None of it reads a field, assigns a meaning, or would change if the
// payload meant something entirely different.
//
// It is also the check a corpus curator actually needs. A capture whose every
// datagram is the same length recorded one kind of packet; a decoder built from
// it would meet the second kind for the first time in production.
//
// The common prefix earns its line twice over on this wire. OSC addresses are
// ASCII and lead the packet, so the gutter beside the hex is close to readable
// -- which is how a reviewer sees what a sender is talking about without a
// decoder ring, and, deliberately, without this tool claiming to have parsed
// anything. A prefix of zero bytes is as informative as a long one and is
// printed as such: it says the shapes differ from the very first byte, which on
// this wire means more than one address.
//
// ## Arrival is the receive clock, and says so
//
// The sibling tools report a cadence measured on the *sender's* clock, which
// they can do because they decode a timestamp. This one measures intervals
// between arrivals on the receiver's monotonic clock, which describes the
// sender, the network, and this process's own scheduling all at once. The label
// says which: a reader who mistakes an arrival rate for a tracker update rate
// will go looking for jitter in a sender that never had any.
#pragma once

#include "vrmAdapterVrchatOsc/Diagnostics.h"
#include "vrmAdapterVrchatOsc/PacketCapture.h"
#include "vrmAdapterVrchatOsc/UdpReceiver.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace vrchatOscRecordTool
{

// Why the session ended. Exactly one of these is true of any run, which is the
// point: a recording that stopped early because a flag said so and one that
// stopped early because the socket failed are different sessions, and a capture
// file cannot tell them apart afterwards.
//
// There is no `MaxFrames` here. The siblings have one because they accumulate
// poses as well as datagrams; nothing in this tool assembles a frame, so a
// reason that could never be reached would be a claim that it could.
enum class StopReason : std::uint8_t
{
    Interrupted,
    Duration,
    IdleTimeout,
    MaxDatagrams,
    EndOfCapture,
    // A socket that reported an error, and a socket that was no longer open.
    // Two reasons rather than one, because `UdpReceiver` distinguishes them and
    // only the first has a platform message to print.
    ReceiveFailed,
    SocketClosed,
};

const char* StopReasonText(StopReason reason) noexcept;

class SessionReport
{
public:
    // One received datagram, whole. The bytes are read and not kept: the census
    // and the prefix are folded in here so that a session's memory is the
    // capture's and not twice the capture's.
    void ObserveDatagram(const std::string& peer, const std::uint8_t* bytes,
                         std::size_t count, double receiveTime);

    // The diagnostics one receive call appended, and only those: the caller
    // clears its list every iteration, so the whole of it is what the last call
    // added.
    void ObserveDiagnostics(
        const std::vector<vrmAdapterVrchatOsc::Diagnostic>& log);

    void SetStopReason(StopReason reason) noexcept { _stop = reason; }
    StopReason GetStopReason() const noexcept { return _stop; }

    std::uint64_t GetDatagramCount() const noexcept { return _datagrams; }

    // Whether the session heard from more than one source. The capture format
    // names one peer in its header, so this is the difference between a
    // fixture's provenance being true and being the first of several.
    bool HasMultiplePeers() const noexcept { return _distinctPeers.size() > 1; }

    // Prints the block. `receiver` is null when the session came off a file: the
    // socket lines are then omitted rather than printed as zeroes, because a
    // bound endpoint a replay never had is not a fact about the replay.
    //
    // `provenance` is the capture's own header, printed only in that case. On a
    // live session the operator supplied it on the command line a moment ago and
    // does not need it read back; for a capture recorded months ago it is half
    // of what "is this fixture still what I thought it was" means.
    void Print(std::FILE* out, const vrmAdapterVrchatOsc::UdpReceiver* receiver,
               const vrmAdapterVrchatOsc::PacketCapture* provenance) const;

private:
    void _ObservePrefix(const std::uint8_t* bytes, std::size_t count);
    void _PrintLengths(std::FILE* out) const;
    void _PrintPrefix(std::FILE* out) const;
    void _PrintDiagnostics(std::FILE* out) const;

    std::uint64_t _datagrams = 0;
    std::uint64_t _payloadBytes = 0;
    // Legal, receivable, and the smallest thing a decoder must refuse without
    // crashing, so a session that carried any says so rather than hiding them in
    // a census entry for length 0.
    std::uint64_t _emptyDatagrams = 0;

    double _firstReceiveTime = 0.0;
    double _lastReceiveTime = 0.0;

    // Intervals between arrivals, on the receive clock. Non-negative by
    // construction: the clock is monotonic and the capture format requires
    // receive times not to go backwards.
    double _intervalSum = 0.0;
    double _intervalMin = 0.0;
    double _intervalMax = 0.0;
    std::uint64_t _intervals = 0;

    // Bounded twice, at two different bounds. `_peers` is what the report
    // *names* and stays small, because a session receiving from a hundred hosts
    // is a misconfiguration the report should say rather than list.
    // `_distinctPeers` is what it *counts*, and is bounded too, since a set is
    // memory a hostile network can grow. Past the bound the report says "at
    // least", which is the honest form of a number that stopped being exact.
    static constexpr std::size_t kMaxNamedPeers = 4;
    static constexpr std::size_t kMaxTrackedPeers = 64;
    std::vector<std::string> _peers;
    std::set<std::string> _distinctPeers;
    bool _peersUntracked = false;

    // The census. Bounded for the same reason the peer list is, and generously
    // rather than tightly: this is a variable-length protocol by construction --
    // an OSC address is a string -- so many distinct lengths is the expected
    // shape and not a symptom. Datagrams whose length arrives after the map is
    // full are counted rather than dropped, so the report can say the census is
    // partial instead of quietly being wrong.
    static constexpr std::size_t kMaxTrackedLengths = 64;
    static constexpr std::size_t kNamedLengths = 8;
    std::map<std::size_t, std::uint64_t> _lengths;
    std::uint64_t _untalliedDatagrams = 0;

    // The leading bytes every datagram shares, shortened as datagrams disagree.
    // Capped on the first datagram, because a session of one 60 KB packet should
    // not print 60 KB of hex.
    //
    // 80 bytes, the number the mocopi tool arrived at from a real device after
    // 32 proved too short by 45 bytes. Nothing about this wire has been measured
    // yet, so the number is inherited rather than derived -- but the direction
    // of the error is known, and it is worth stating which way the cap should
    // move if it turns out wrong here too: a cap has to be past the shared
    // header of the protocol it is describing, or the line describes the cap.
    static constexpr std::size_t kMaxPrefixBytes = 80;
    std::vector<std::uint8_t> _prefix;

    // The shortest datagram the session saw, which is what makes the cap
    // reportable honestly. A full-length prefix means "at least this much" only
    // when the cap is what shortened it.
    std::size_t _shortestDatagram = 0;

    std::array<std::uint64_t, vrmAdapterVrchatOsc::DiagnosticCodeCount>
        _diagnostics{};
    // The first of each code, kept whole. A count says a session reported
    // silence twice; the first line says when.
    std::array<vrmAdapterVrchatOsc::Diagnostic,
               vrmAdapterVrchatOsc::DiagnosticCodeCount> _firstDiagnostic{};

    StopReason _stop = StopReason::EndOfCapture;
};

} // namespace vrchatOscRecordTool
