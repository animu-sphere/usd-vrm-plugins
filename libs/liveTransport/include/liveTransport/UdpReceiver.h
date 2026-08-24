// SPDX-License-Identifier: Apache-2.0
//
// The socket, and the thread it does not create.
//
//     [ UdpReceiver ] -> datagram -> a capture file, or an adapter's decoder
//
// Caller-driven throughout: no thread, no callback, no work between calls, and
// no decoding at all — `Receive` hands back the bytes exactly as they arrived,
// including the ones a decoder would refuse, because a receiver that filtered
// its own input would make a corpus a description of what the receiver let
// through rather than of what a source sent.
//
// ## Why this is a library and not a third copy
//
// Two adapters wrote this class twice, and by 2026-08-23 the two copies had
// drifted by 210 lines and four defects — an oversize datagram truncated
// silently, a large finite timeout mapped onto "wait forever", a `poll`
// wake-up trusted without reading `revents`, and idle accounting charged to a
// call that had just met traffic. All four had been *copied* along with
// everything else, fixed in the younger copy alone, and written down there as
// still present in the older one. Both files named the trigger for turning the
// repetition into a library and named it exactly: a third recorder. This is
// that library (roadmap/osc-and-vrchat-trackers.md §2, §3.2).
//
// The four fixes landed in both adapters *before* this move, so that a file
// move never carried a fix inside it (OSC-1). What arrives here is the merged
// behaviour, unchanged.
//
// ## It raises no diagnostic code, and that is the contract
//
// A diagnostic code set is frozen per adapter, before its decoder exists, so
// that the set describes a protocol rather than a bug history. A shared
// receiver therefore cannot name one — `liveTransport` holding an adapter's
// code is a WORKSPACE.md §2 violation — so it reports what it *observed*, as a
// `TransportEventReport`, and the caller that knows which adapter it is maps
// the event onto its own frozen code. This is the shape `MatchSourceProfile`
// already uses: the lower layer returns a typed refusal naming the event, and
// the caller supplies the vocabulary.
//
// It also settles a difference the two copies carried. The mocopi receiver had
// a silence timeout and the VMC one had no equivalent, and the reason was never
// that silence matters less to a relay: it was that the VMC adapter's frozen
// code set has no code for it. Here the capability is unconditional and the
// *code* stays the adapter's problem — an adapter with nothing to map
// `TransportEvent::Silence` onto leaves `silenceTimeoutSeconds` at 0, which is
// off, and gets exactly the behaviour it had.
//
// ## Nothing is passed on half-read, and that needs one byte more than it looks
//
// The bound is `MaxDatagramBytes` — the largest payload UDP over IPv4 can
// deliver, and the same bound the capture format enforces — but the *buffer* is
// one byte larger, and the extra byte is the whole mechanism. A datagram longer
// than the buffer is truncated **silently** on POSIX: `recvfrom` returns the
// buffer's length and there is no flag in the result to distinguish that from a
// datagram which happened to be exactly that long. A buffer of exactly
// `MaxDatagramBytes` would therefore hand a half-read datagram back as a whole
// one, and a recording tool would write a packet the source never sent into a
// fixture and blame the source for it — which is precisely the failure this
// bound exists to prevent. With one spare byte, an over-long datagram comes
// back as `MaxDatagramBytes + 1`, is counted in `datagramsTruncated`, and is
// dropped. Windows says so directly with `WSAEMSGSIZE`; both paths lead to the
// same counter.
//
// IPv4 is a product's own stated limit rather than this class's: a caller may
// bind an IPv6 address and the resolver will take it, since refusing one would
// be this layer inventing a restriction on its own socket. The only consequence
// is that IPv6's 20 additional payload bytes are the one way
// `datagramsTruncated` is reachable at all — which is exactly why it has to be
// reachable *correctly* rather than assumed unreachable.
//
// ## The clock is monotonic, and that is a requirement rather than a taste
//
// `receiveTime` is seconds since `Open`, read from a steady clock. The capture
// format states that receive times must not go backwards (PacketCapture.h)
// because arrival order is the whole point of it, and a wall clock steps
// backwards for reasons that have nothing to do with the session — NTP, a
// timezone edit, a suspended laptop. Counting from `Open` is also exactly the
// origin a `<sender>-packet-capture` records against, so a recording tool
// copies the number across instead of rebasing it.
//
// ## Diagnosing a session that receives nothing
//
// Silence is the commonest live failure and it has a small number of causes a
// socket can distinguish: nothing was ever bound, something is bound but only
// to loopback, or datagrams are arriving from an address the operator did not
// expect. `GetBoundEndpoint`, `IsLoopbackOnly`, and the stats' `lastPeer` are
// the facts for that; formatting them for a human is a CLI's job and not this
// class's.
#pragma once

#include "liveTransport/PacketCapture.h"
#include "liveTransport/api.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace liveTransport
{

struct UdpReceiverConfig
{
    // A numeric address, never a hostname: this is resolved with the
    // no-DNS-lookup flag, so an unresolvable string fails at `Open` without a
    // socket having touched the network. "0.0.0.0" listens on every IPv4
    // interface, "127.0.0.1" on loopback alone.
    std::string listenAddress = "0.0.0.0";
    // No default port, and that is deliberate: a port is a protocol's property
    // and this library knows no protocol. Each adapter names its own and passes
    // it in.
    std::uint16_t listenPort = 0;

    // Whether another socket may already hold this address and port. Off by
    // default, so a second receiver started against a port that is already
    // serving reports a bind failure instead of silently taking some fraction
    // of the traffic — which is the failure an operator has no way at all to
    // see from the outside.
    bool reuseAddress = false;

    // The kernel's receive buffer, in bytes; 0 leaves the platform default.
    // A request, not a setting: every platform may clamp it and Linux doubles
    // it, so what was actually granted is read back and reported by
    // `GetReceiveBufferBytes()`.
    std::size_t receiveBufferBytes = 0;

    // Seconds of silence after which `Receive` reports
    // `TransportEvent::Silence`, measured from the last accepted datagram or
    // from `Open` when none has arrived yet. **0 disables it**, and that is the
    // default because this layer has no basis for a number: how long a source
    // may reasonably take to start is a property of the session, not of the
    // socket. An adapter whose frozen code set has nothing to map the event
    // onto leaves it here.
    double silenceTimeoutSeconds = 0.0;
};

// What the transport observed, named as an event because a *code* is an
// adapter's frozen property and this library holds none (see the header).
enum class TransportEvent : std::uint8_t
{
    // The receiver could not bind its listen address and port.
    BindFailed,
    // No datagram has arrived within the configured silence threshold. Reported
    // once per episode: an accepted datagram ends the episode and the next
    // silence is a new one.
    Silence,
};

// One observation, in the shape a diagnostic needs minus the two things only
// the caller can supply — its code, and the severity and recoverability that
// code's table decides.
struct TransportEventReport
{
    TransportEvent event = TransportEvent::BindFailed;
    // The bound endpoint, when the receiver has one. Empty for a bind that
    // never succeeded.
    std::string source;
    // The requested endpoint, for a bind failure. Empty otherwise.
    std::string subject;
    // The platform's own message, or the duration of the silence. Human text;
    // no caller parses it.
    std::string detail;
};

// One datagram as it arrived. Reused across calls by design — `Receive` resizes
// `bytes` and rewrites `peer` in place — so a receive loop declares one of
// these outside it and pays for no allocation per packet.
//
// This is deliberately not `RecordedDatagram`, which is the *file format's*
// record and carries no peer: a capture names one peer in its header, where a
// live socket learns a possibly different one per datagram. A recording tool
// copies `bytes` and `receiveTime` across and keeps `peer` for its diagnosis.
struct ReceivedDatagram
{
    std::vector<std::uint8_t> bytes;
    // Seconds since `Open`, monotonic. See the header.
    double receiveTime = 0.0;
    // The sender, numerically: "192.168.1.5:52001", or "[fe80::1]:52001".
    std::string peer;
};

enum class ReceiveStatus : std::uint8_t
{
    // A datagram is in the caller's buffer.
    Received,
    // Nothing was waiting within the timeout. The expected answer to a poll,
    // and not an error at any timeout.
    Idle,
    // No socket is open. A caller that gets this and keeps looping is spinning.
    Closed,
    // The socket reported something this class cannot classify as transient.
    // Transient failures are retried inside the call and counted; this one
    // means the caller should `Close` and decide whether to `Open` again.
    // `GetLastErrorText()` says what the platform said.
    Failed,
};

// What an operator needs to tell silence apart from its causes. This counts
// what the *socket* did, so it is the only tally that can describe traffic
// which never became a packet.
struct UdpReceiverStats
{
    std::uint64_t datagramsReceived = 0;
    std::uint64_t bytesReceived = 0;

    // Calls that found nothing waiting. A zero-timeout poll that finds an empty
    // socket counts here too — it is the same fact, asked more often.
    //
    // A call that met a truncated datagram or a transient error and then ran
    // out of budget does **not** count here. Such a call found something
    // waiting; counting it as idle would tally it twice, once here and once in
    // the counter that describes what it actually met.
    std::uint64_t idleReceives = 0;

    // Transient platform errors retried inside `Receive`. Non-zero is not by
    // itself a fault; growing steadily is.
    std::uint64_t receiveErrors = 0;

    // Datagrams the transport delivered larger than `MaxDatagramBytes`, dropped
    // rather than passed on half-read. Unreachable over UDP/IPv4, where the
    // bound *is* the protocol's maximum; reachable over IPv6, which is why the
    // buffer holds one byte more than the bound (see the header) rather than
    // relying on a platform to report the overrun. Windows reports it as
    // `WSAEMSGSIZE`; POSIX truncates silently and the spare byte is what
    // catches it there.
    std::uint64_t datagramsTruncated = 0;

    // How many times silence crossed `silenceTimeoutSeconds`. One per episode,
    // not one per poll — see the header. Always 0 when the threshold is off,
    // which is what an adapter that does not expose the threshold reads.
    std::uint64_t silenceReports = 0;

    // The receive clock at the first and last accepted datagram. Both stay 0 on
    // a session that received nothing, which `datagramsReceived` disambiguates.
    // They are relative to the current `Open`, which is why a successful `Open`
    // clears this whole struct: carrying a time across an epoch that restarted
    // would leave two numbers on the same axis that were never on the same axis.
    double firstReceiveTime = 0.0;
    double lastReceiveTime = 0.0;

    // Who sent the last one. The second question after "is anything arriving",
    // and the one that catches a source pointed at the wrong machine.
    std::string lastPeer;
};

// A bound UDP socket. Caller-driven throughout: it starts no thread, invokes no
// callback, and does nothing between calls to `Receive`.
class LIVETRANSPORT_API UdpReceiver final
{
public:
    UdpReceiver();
    ~UdpReceiver();

    UdpReceiver(const UdpReceiver&) = delete;
    UdpReceiver& operator=(const UdpReceiver&) = delete;

    // Binds. Closes any socket this object already held first, because a
    // receiver that half-rebinds is worse than one that starts over.
    //
    // A successful `Open` starts a session: the receive clock restarts, the
    // stats are cleared, and the silence state is rearmed. A *failed* one
    // leaves the stats alone, so a receiver whose re-`Open` was refused can
    // still be asked what the session it had did.
    //
    // On failure the object is closed and `TransportEvent::BindFailed` is
    // appended, with the requested endpoint as its subject and the platform's
    // own message as its detail. That covers the three causes worth telling
    // apart: the port is already served, the address is not one this host
    // holds, and the address does not parse.
    bool Open(const UdpReceiverConfig& config,
              std::vector<TransportEventReport>* events = nullptr);

    void Close() noexcept;
    bool IsOpen() const noexcept { return _socket != -1; }

    // What the socket actually got, which is not always what was asked for: a
    // configured port of 0 is bound by the OS, and a test that wants two
    // receivers on one machine has to read the number back from here.
    const std::string& GetBoundEndpoint() const noexcept
    {
        return _boundEndpoint;
    }

    // Whether the bound address can only be reached from this machine.
    bool IsLoopbackOnly() const noexcept { return _loopbackOnly; }

    // What the kernel actually granted for the receive buffer, read back at
    // `Open` rather than assumed from the request. 0 when the socket is closed
    // or the platform would not say.
    std::size_t GetReceiveBufferBytes() const noexcept
    {
        return _receiveBufferBytes;
    }

    // Waits up to `timeoutSeconds` for one datagram. Zero polls and returns
    // immediately; negative waits indefinitely, which is only correct for a
    // caller that has another way to stop.
    //
    // `datagram` is reused: `bytes` is resized to the received length and
    // `peer` rewritten, so a loop declares one of these outside it. On any
    // status other than `Received` the contents are unspecified and must not be
    // read.
    //
    // `events` is where `TransportEvent::Silence` is appended when a silence
    // threshold is configured and has been crossed. It is threaded through the
    // receive call rather than offered as a separate query because this is the
    // call that knows time passed — a caller that had to remember a second one
    // would discover silence only in the sessions where it happened to
    // remember.
    ReceiveStatus Receive(ReceivedDatagram* datagram,
                          double timeoutSeconds = 0.0,
                          std::vector<TransportEventReport>* events = nullptr);

    // The receive clock, read without receiving: seconds since `Open`, on the
    // same monotonic timeline every `receiveTime` is stamped from. A loop
    // measures how long it has been quiet with this, and stamps its own events
    // on the same axis as the traffic.
    //
    // Counts from construction on a receiver that has never been opened, rather
    // than from the clock's own epoch — which on Linux would be the time since
    // the machine booted, a number a caller could easily mistake for a session
    // that has been quiet for weeks.
    double Now() const noexcept;

    // The platform's message for the last failure, bind or receive. Empty until
    // something fails; not cleared by a subsequent success, because a caller
    // reads it after a status told it to.
    const std::string& GetLastErrorText() const noexcept { return _lastError; }

    const UdpReceiverStats& GetStats() const noexcept { return _stats; }

    // Starts a new counting window without disturbing the session.
    //
    // It rearms the silence report as well as zeroing the tally, and that pair
    // is the contract rather than an implementation detail: a window that
    // cleared `silenceReports` but left the episode marked as already reported
    // would end saying `silenceReports == 0` — "the session was fine" — for a
    // source that was switched off for the whole of it. An episode still in
    // progress is therefore reported once more, in the new window, which is
    // what a window means.
    //
    // It does **not** move the point silence is measured from. That is a fact
    // about the wire, not a statistic, so resetting the tally mid-session does
    // not make a source that stopped ten seconds ago look freshly quiet.
    void ResetStats() noexcept
    {
        _stats = UdpReceiverStats();
        _silenceReported = false;
    }

private:
    // Appends `TransportEvent::Silence` if the configured threshold has been
    // crossed and has not already been reported for this episode.
    void _ReportSilence(std::vector<TransportEventReport>* events);

    // A platform socket handle, kept as an integer so that no Winsock or POSIX
    // header reaches this file. -1 is "closed" on both: Winsock's
    // INVALID_SOCKET is the all-ones value of an unsigned pointer-width
    // integer, which is this when it is signed.
    std::intptr_t _socket = -1;

    std::string _boundEndpoint;
    bool _loopbackOnly = false;
    std::size_t _receiveBufferBytes = 0;
    std::string _lastError;

    UdpReceiverConfig _config;

    // Whether the current silence episode has already been reported. Cleared by
    // an accepted datagram, which is what makes the report one per episode, and
    // by `ResetStats` and `Open`.
    bool _silenceReported = false;

    // The point silence is measured from: the receive clock at the last
    // accepted datagram, or 0.0 — which is `Open` — when none has arrived.
    //
    // Held here rather than read out of `_stats`, and the difference is not
    // cosmetic. `_stats` is a tally a caller may zero at any moment, and it is
    // cleared by `Open` while these two are set by it; deriving a wire fact
    // from it made a re-`Open` compare a clock that had restarted against a
    // time stamped in the previous epoch, which left the silence check blind
    // for as long as the previous session had run.
    double _lastTrafficTime = 0.0;
    // Whether any datagram has arrived since `Open`. Chooses between the two
    // things this code can honestly say — a source that has not started and one
    // that has stopped — and is a session fact, so `ResetStats` leaves it
    // alone.
    bool _sawTraffic = false;

    // The steady-clock reading at `Open`, in the clock's own ticks, so that
    // `receiveTime` counts from a session's start (see the header). Set at
    // construction too, so `Now()` never reports the clock's own epoch.
    std::int64_t _epoch = 0;

    // Where a datagram is read before its real length is known.
    // `MaxDatagramBytes + 1` bytes: the spare one is how an over-long datagram
    // is detected on a platform that truncates silently (see the header). Sized
    // once at `Open` and never resized, which is the point: resizing the
    // *caller's* vector up to `MaxDatagramBytes` and back on every call would
    // value-initialise ~64 KB per datagram — a memset on the hot path of a
    // class whose whole shape exists to avoid one. Released by `Close`, so a
    // receiver a caller is holding but not listening on costs nothing.
    std::vector<std::uint8_t> _buffer;

    UdpReceiverStats _stats;
};

struct DatagramQueueConfig
{
    // Bounded twice, because the two limits fail differently: a flood of small
    // datagrams exhausts the count, and a flood of maximum-sized ones exhausts
    // memory long before the count. Neither default is tuned — they are a
    // second or so of a fast sender, which is far more than a consumer that is
    // keeping up ever holds and far less than a leak.
    //
    // **A queue always holds the datagram it was last given**, even when that
    // one datagram is larger than `maxBytes` on its own: the bound is enforced
    // by dropping older datagrams, and there is nothing older to drop. So
    // `maxBytes` bounds what accumulates, not what a single push may cost, and
    // the worst case is one `MaxDatagramBytes` over. The alternative — refusing
    // the newest datagram — is the one thing this queue must never do, for the
    // same reason overflow drops the oldest.
    std::size_t maxDatagrams = 1024;
    std::size_t maxBytes = 4u * 1024u * 1024u;
};

struct DatagramQueueStats
{
    std::uint64_t pushed = 0;
    std::uint64_t drained = 0;
    // Datagrams displaced by a push into a full queue. Non-zero means the
    // consumer is not draining fast enough; the poses that would have come out
    // of them are simply not in the stream.
    std::uint64_t dropped = 0;
    std::size_t highWaterMark = 0;
};

// The hand-off, and the only synchronised object in this library.
//
// A network thread pushes; the consumer's thread drains and decodes. Nothing
// downstream of `Drain` learns that a second thread exists, which is the whole
// point: an adapter's decode path and `motionRuntime` keep the single-threaded
// contract their tests are written against.
//
// **It is opt-in, and that is the contract rather than a convenience.** The
// failure mode of an extraction is that everything one caller needed becomes
// everything every caller gets. A queue exists for a consumer that cannot poll
// often enough to keep a kernel receive buffer from overflowing; one adapter
// met that case and the other wrote down that it had not. A caller that polls
// inside its own tick never constructs one of these and pays for none of it —
// not the mutex, and not the second copy of every datagram.
//
// **Overflow drops the oldest.** For live motion that is the only defensible
// direction — a queue that refused the newest datagram to keep a stale one
// would add latency the session never recovers, and would hand an assembler a
// frame boundary out of order on top of it. The drop is counted rather than
// reported, because it is a statement about the consumer's tick rate and not
// about the sender.
//
// There is deliberately no blocking `Pop` and no condition variable. A consumer
// that would block on this queue already has a thread of its own to wait on,
// and giving it one here would need a wakeup path for shutdown — a second
// cancellation problem, in a class whose entire job is to have no opinions.
class LIVETRANSPORT_API DatagramQueue final
{
public:
    explicit DatagramQueue(const DatagramQueueConfig& config = {});

    // From the network thread. Returns false when this push displaced the
    // oldest datagram, so a recorder can log the moment it started losing
    // traffic. Takes its argument by value: a caller that is done with its
    // buffer moves it in, and one reusing a receive buffer copies.
    bool Push(ReceivedDatagram datagram);

    // From the consumer's thread. Appends everything queued, oldest first, and
    // returns how many. `out` is never cleared, so a caller can accumulate a
    // tick's worth across several queues.
    std::size_t Drain(std::vector<ReceivedDatagram>* out);

    std::size_t GetSize() const;
    void Clear();

    DatagramQueueStats GetStats() const;
    void ResetStats();

private:
    DatagramQueueConfig _config;

    mutable std::mutex _mutex;
    std::deque<ReceivedDatagram> _queued;
    std::size_t _queuedBytes = 0;
    DatagramQueueStats _stats;
};

} // namespace liveTransport
