// SPDX-License-Identifier: Apache-2.0
//
// What a session was, for a session nothing can yet read.
//
// The sibling tool's report has five layers to gather from and answers four
// questions in the order an operator asks them — is anything arriving, does it
// decode, does it become motion, what went wrong. This adapter has one layer, so
// only the first and the last of those questions have an answer here, and the
// discipline this file needs is about the middle two: **it must not invent
// them.** A recorder that guessed at a field to make its report look complete
// would be the BVH-0 failure mode with a nicer output format, and it would
// contaminate exactly the evidence the recording exists to obtain.
//
// So what is printed is what a socket can see:
//
//     is anything arriving      -> the counts, the peers, and the arrival rate
//     is it more than one thing -> the length census and the common prefix
//     what went wrong           -> the two transport codes, by code
//
// ## The census is observation, and the line between the two is the length
//
// The middle pair is the one worth defending, because "3 distinct lengths, 96 of
// them 68 bytes" is the first sentence about this protocol anything in this
// repository has been able to say. It stays on the right side of the line
// because every number in it is a property of the datagram *envelope* — how many
// arrived, how long each was, and which leading bytes every one of them shares.
// None of it reads a field, assigns a meaning, or would change if the payload
// meant something entirely different.
//
// It is also the check a corpus curator actually needs. A capture whose every
// datagram is the same length recorded one kind of packet; a decoder built from
// it would meet the second kind for the first time in production. Whether a
// session *should* hold more than one shape is a question this tool refuses to
// answer — it does not know, and neither does anything else here yet — so the
// census is reported as a measurement and no threshold is attached to it.
//
// The common prefix earns its line for the same reason and one more: it is how a
// reviewer sees a container's magic without a decoder ring. `PacketCapture.h`
// puts an ASCII gutter beside the hex so a field tag is legible in a diff; this
// is the same courtesy for a hundred datagrams at once. A prefix of zero bytes
// is as informative as a long one and is printed as such, because it says the
// shapes differ from the very first byte.
//
// ## Arrival is the receive clock, and says so
//
// The sibling reports a cadence measured on the *sender's* clock, which it can
// do because it decodes a timestamp. What that number describes is the source's
// frame rate. This one measures intervals between arrivals on the receiver's
// monotonic clock, which describes the source, the network, and this process's
// own scheduling all at once. The two are not the same measurement and the label
// says which one this is: a reader who mistakes an arrival rate for a frame rate
// will go looking for jitter in a device that never had any.
#pragma once

#include "vrmAdapterMocopi/Diagnostics.h"
#include "vrmAdapterMocopi/PacketCapture.h"
#include "vrmAdapterMocopi/UdpReceiver.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace mocopiRecordTool
{

// Why the session ended. Exactly one of these is true of any run, which is the
// point: a recording that stopped early because a flag said so and one that
// stopped early because the socket failed are different sessions, and a capture
// file cannot tell them apart afterwards.
//
// There is no `MaxFrames` here. The sibling has one because it accumulates
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
    //
    // Deliberately not the sibling's `(log, from)` slice. Two mechanisms for one
    // invariant is one too many -- with the per-iteration clear, `from` can only
    // ever be 0, so a reader has to go and verify the clear before they can tell
    // that the offset is inert. One of them had to go, and the clear is the one
    // that also bounds how much a long session accumulates.
    void ObserveDiagnostics(
        const std::vector<vrmAdapterMocopi::Diagnostic>& log);

    void SetStopReason(StopReason reason) noexcept { _stop = reason; }
    StopReason GetStopReason() const noexcept { return _stop; }

    std::uint64_t GetDatagramCount() const noexcept { return _datagrams; }

    // Whether the session heard from more than one source. The capture format
    // names one peer in its header, so this is the difference between a
    // fixture's provenance being true and being the first of several.
    bool HasMultiplePeers() const noexcept
    {
        return _distinctPeers.size() > 1;
    }

    // Prints the block. `receiver` is null when the session came off a file: the
    // socket lines are then omitted rather than printed as zeroes, because a
    // bound endpoint a replay never had is not a fact about the replay.
    //
    // `provenance` is the capture's own header, printed only in that case. On a
    // live session the operator supplied it on the command line a moment ago and
    // does not need it read back; for a capture recorded months ago it is half of
    // what "is this fixture still what I thought it was" means.
    void Print(std::FILE* out, const vrmAdapterMocopi::UdpReceiver* receiver,
               const vrmAdapterMocopi::PacketCapture* provenance) const;

private:
    void _ObservePrefix(const std::uint8_t* bytes, std::size_t count);
    void _PrintLengths(std::FILE* out) const;
    void _PrintDiagnostics(std::FILE* out) const;

    std::uint64_t _datagrams = 0;
    std::uint64_t _payloadBytes = 0;
    // Legal, receivable, and the smallest thing a decoder must refuse without
    // crashing (PacketCapture.h), so a session that carried any says so rather
    // than hiding them in a census entry for length 0.
    std::uint64_t _emptyDatagrams = 0;

    double _firstReceiveTime = 0.0;
    double _lastReceiveTime = 0.0;

    // Intervals between arrivals, on the receive clock. Non-negative by
    // construction: the clock is monotonic and the capture format requires
    // receive times not to go backwards, so a negative one here would be a
    // reader or recorder defect rather than a session's property.
    double _intervalSum = 0.0;
    double _intervalMin = 0.0;
    double _intervalMax = 0.0;
    std::uint64_t _intervals = 0;

    // Bounded twice, at two different bounds, and the difference is the
    // correction the sibling still needs. `_peers` is what the report *names*
    // and stays small, because a session receiving from a hundred hosts is a
    // misconfiguration the report should say rather than list. `_distinctPeers`
    // is what it *counts*, and deduplicating against the named list alone made
    // the count wrong the moment a fifth host appeared: every datagram from a
    // peer too late to be named failed the search and incremented the tally, so
    // one extra host sending a thousand datagrams reported a thousand peers.
    //
    // A count is bounded too, since a set is memory a hostile network can grow.
    // Past the bound the report says "at least", which is the honest form of a
    // number that stopped being exact.
    static constexpr std::size_t kMaxNamedPeers = 4;
    static constexpr std::size_t kMaxTrackedPeers = 64;
    std::vector<std::string> _peers;
    std::set<std::string> _distinctPeers;
    bool _peersUntracked = false;

    // The census. Bounded for the same reason the peer list is, and the bound is
    // generous rather than tight because a variable-length protocol is a real
    // possibility here — nobody has measured this one. Datagrams whose length
    // arrives after the map is full are counted rather than dropped, so the
    // report can say that the census is partial instead of quietly being wrong.
    static constexpr std::size_t kMaxTrackedLengths = 64;
    static constexpr std::size_t kNamedLengths = 8;
    std::map<std::size_t, std::uint64_t> _lengths;
    std::uint64_t _untalliedDatagrams = 0;

    // The leading bytes every datagram shares, shortened as datagrams disagree.
    // Capped on the first datagram, because a session of one 60 KB packet should
    // not print 60 KB of hex.
    //
    // **80, and the number comes from a device rather than from taste.** It was
    // 32, on the reasoning that a container's magic is not longer than that. The
    // first real session disproved it precisely: every datagram shared **77**
    // bytes, and byte 77 was exactly where the two packet kinds diverged -- so a
    // 32-byte cap hid 45 of the 77 and cut the line off immediately before the
    // one offset that mattered. A cap has to be past the shared header of the
    // protocol it is describing or the line describes the cap instead.
    static constexpr std::size_t kMaxPrefixBytes = 80;
    std::vector<std::uint8_t> _prefix;

    // The shortest datagram the session saw, which is what makes the cap
    // reportable honestly. A full-length prefix means "at least this much" only
    // when the cap is what shortened it -- if some datagram was itself only
    // `kMaxPrefixBytes` long, then the shared prefix is exactly that and saying
    // "at least" would invite a reviewer to look for bytes that are not there.
    std::size_t _shortestDatagram = 0;

    std::array<std::uint64_t, vrmAdapterMocopi::DiagnosticCodeCount>
        _diagnostics{};
    // The first of each code, kept whole. A count says a session reported
    // silence twice; the first line says when.
    std::array<vrmAdapterMocopi::Diagnostic,
               vrmAdapterMocopi::DiagnosticCodeCount> _firstDiagnostic{};

    StopReason _stop = StopReason::EndOfCapture;
};

} // namespace mocopiRecordTool
