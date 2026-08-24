// SPDX-License-Identifier: Apache-2.0
//
// The socket, and the thread it does not create.
//
// This is the last layer of the VMC adapter to be written and the first one a
// live session touches, which is the order the plan insists on
// (roadmap/adapters-mocopi-vmc-ardy.md §5): building the receiver first would
// have made every test below it require a live sender. Everything under it is
// already verifiable from committed bytes, so this layer is the only one whose
// tests need a socket at all.
//
//     [ UdpReceiver ] -> datagram -> VmcLiveSource::PushDatagram -> pose
//
// It owns a socket, a bind address, a receive clock, and a size limit. It owns
// no decoding — `Receive` hands back the bytes exactly as they arrived,
// including the ones the layers above will refuse, because a receiver that
// filtered its own input would make the corpus a description of what the
// receiver let through rather than of what a sender sent.
//
// ## The thread question, answered
//
// Motion policy §11.4 draws the required arrangement as
//
//     network / device thread -> adapter -> thread-safe pose buffer
//
// and the buffer it names is not thread-safe: `motionRuntime` contains no mutex
// or atomic at all (LiveSource.h). So the receiver had to answer this before it
// could exist, and the answer is to move the boundary rather than to lock the
// buffer:
//
//     network thread -> [ DatagramQueue ] -> consumer thread -> decode -> pose
//
// The hand-off happens on **raw datagrams, before the decoder**, not on poses
// after it. Everything downstream — this adapter's five layers and all of
// `motionRuntime` — then runs on one thread exactly as its tests do, and the
// only synchronised object in the whole path is `DatagramQueue` below. Locking
// `LiveCaptureSource` instead would have made one class safe and left every
// `GetIntake()` caller racing on the same buffer, which is a worse fault for
// looking like a fixed one.
//
// A consumer that already has a tick — a DCC integration, a CLI's replay loop —
// needs no second thread at all, and should not take one. `Receive` with a zero
// timeout is a true poll, so draining the socket is a few lines inside whatever
// loop the consumer already runs:
//
//     ReceivedDatagram datagram;
//     while (receiver.Receive(&datagram) == ReceiveStatus::Received) {
//         source.PushDatagram(datagram.bytes, datagram.receiveTime, &log);
//     }
//     if (source.ConsumeSessionRestart()) {
//         source.GetIntake().AlignClock(now);
//     }
//     motion::PoseSampleResult pose = source.Sample(now);
//
// The queue is for the consumer that cannot poll often enough to keep a socket's
// receive buffer from overflowing between ticks. That is a real case and a
// narrow one, and it is offered here so that the first caller who meets it
// reaches for a queue of datagrams rather than for a mutex around the runtime.
//
// ## Why every wait has a timeout
//
// `Receive` never blocks indefinitely by default, and the reason is
// cancellation rather than latency. A thread parked in `recvfrom` can only be
// woken by closing the socket underneath it, which is a race on every platform
// this repository builds for — the descriptor may be reused between the close
// and the wakeup, and the blocked call then reports about a socket somebody else
// owns. A bounded wait turns stopping into a flag the loop already checks.
//
// ## Nothing is passed on half-read, and that needs one byte more than it looks
//
// The bound is `MaxDatagramBytes` — the largest payload UDP over IPv4 can
// deliver, and the same bound the capture format enforces — but the *buffer*
// is one byte larger, and the extra byte is the whole mechanism. That is a
// decision about blame: a truncated datagram is indistinguishable at the OSC
// layer from a malformed one, so a receiver that handed one on would
// manufacture `VRM_VMC_PACKET_MALFORMED` against a sender that did nothing
// wrong.
//
// A buffer of exactly `MaxDatagramBytes` cannot prevent that, which is what an
// earlier version of this file claimed it could. A datagram longer than the
// buffer is truncated **silently** on POSIX: `recvfrom` returns the buffer's
// length, and nothing in the result distinguishes that from a datagram which
// happened to be exactly that long. With one spare byte an over-long datagram
// comes back as `MaxDatagramBytes + 1`, is counted in `datagramsTruncated`, and
// is dropped. Windows says so directly with `WSAEMSGSIZE`; both paths lead to
// the same counter.
//
// The claim that no transport could deliver one was wrong in this class's own
// terms: `listenAddress` documents "::" below, and IPv6's 20 additional payload
// bytes are the one way `datagramsTruncated` is reachable at all — which is
// exactly why it has to be reachable *correctly* rather than assumed
// unreachable. Found in the sibling adapter's copy of this file on 2026-08-11,
// fixed here on 2026-08-24 (OSC-1).
//
// ## One transport code, because one transport failure is fatal
//
// `VRM_VMC_SOCKET_BIND_FAILED` is the only socket diagnostic in the frozen set
// (Diagnostics.h), and the set did not need a ninth: it is the only failure here
// a session cannot continue past. A receiver that never bound has nothing to
// recover into, where a lost datagram, a transient error, and an empty poll are
// all things a live session survives by definition. Those are counted in
// `UdpReceiverStats` and named in `GetLastErrorText()` rather than being
// reported as diagnostics — a code invented for them would say "recoverable" on
// every line and describe the receive loop instead of the protocol.
//
// ## The clock is monotonic, and that is a requirement rather than a taste
//
// `receiveTime` is seconds since `Open`, read from a steady clock. The recorded
// capture format states that receive times must not go backwards
// (PacketCapture.h) because arrival order is the whole point of it — and a wall
// clock steps backwards for reasons that have nothing to do with the session
// (NTP, a timezone edit, a suspended laptop). Counting from `Open` is also
// exactly the origin `vmc-packet-capture` records against, so a recording tool
// copies the number instead of rebasing it.
//
// This clock shares no origin with the consumer's evaluation time, and does not
// need to: `LiveCaptureSource::AlignClock` is what bridges the two, and a
// receive time only ever stamps a frame whose sender sent no `/VMC/Ext/T`.
//
// ## Diagnosing a session that receives nothing
//
// The commonest live failure is silence, and it has a small number of causes
// that a socket can distinguish: nothing was ever bound, something is bound but
// only to loopback so no other machine can reach it, or datagrams are arriving
// from an address the operator did not expect. `GetBoundEndpoint`,
// `IsLoopbackOnly`, and the stats' `lastPeer` are the facts for that; formatting
// them for a human is the CLI's job and not this class's.
#pragma once

#include "vrmAdapterVmc/Diagnostics.h"
#include "vrmAdapterVmc/PacketCapture.h"
#include "vrmAdapterVmc/api.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace vrmAdapterVmc
{

// The VMC Protocol's registered listen port. Named rather than defaulted
// silently, because a sender that cannot be reconfigured is the normal case and
// an operator reading a config file should see the number they were told.
inline constexpr std::uint16_t DefaultVmcPort = 39539;

struct UdpReceiverConfig
{
    // A numeric address, never a hostname: this is resolved with the
    // no-DNS-lookup flag, so an unresolvable string fails at `Open` without a
    // socket having touched the network. "0.0.0.0" listens on every IPv4
    // interface, "127.0.0.1" on loopback alone, "::" on every IPv6 one.
    std::string listenAddress = "0.0.0.0";
    std::uint16_t listenPort = DefaultVmcPort;

    // Whether another socket may already hold this address and port. Off by
    // default, so a second receiver started against a port that is already
    // serving reports `VRM_VMC_SOCKET_BIND_FAILED` instead of silently taking
    // some fraction of the traffic — which is the failure an operator has no
    // way at all to see from the outside.
    bool reuseAddress = false;

    // The kernel's receive buffer, in bytes; 0 leaves the platform default. This
    // is the only place in the adapter where a number is a latency/loss
    // trade-off rather than a contract: a consumer that ticks at 60 Hz against a
    // 30 Hz sender needs almost none, and one that ticks at 1 Hz needs enough to
    // hold a second of frames or the kernel drops them before this class ever
    // sees them. The queue below is the other answer to the same problem.
    //
    // A request, not a setting. Every platform is free to clamp it and Linux
    // doubles it, so what was actually granted is read back and reported by
    // `GetReceiveBufferBytes()` — asking for four megabytes, silently getting
    // 212992, and then losing datagrams is the single hardest failure in this
    // class to diagnose from the outside.
    std::size_t receiveBufferBytes = 0;
};

// One datagram as it arrived. Reused across calls by design — `Receive` resizes
// `bytes` and rewrites `peer` in place — because the layer above states that a
// datagram's lifetime ends inside `PushDatagram`, so a receive loop wants one
// buffer and not one allocation per packet (LiveSource.h).
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
    // Transient failures are retried inside the call and counted; this one means
    // the caller should `Close` and decide whether to `Open` again.
    // `GetLastErrorText()` says what the platform said.
    Failed,
};

// What an operator needs to tell silence apart from its causes, in the same
// spirit as `VmcFrameStats` and `LiveCaptureStats`: this counts what the
// *socket* did, so it is the only tally that can describe traffic which never
// became a packet.
struct UdpReceiverStats
{
    std::uint64_t datagramsReceived = 0;
    std::uint64_t bytesReceived = 0;

    // Calls that found nothing waiting. A zero-timeout poll that finds an empty
    // socket counts here too — it is the same fact, asked more often. A call
    // that returned `Idle` after dropping an over-long datagram or meeting a
    // transient error does **not**: it met something, and a tally that said
    // otherwise would describe a quiet socket while `datagramsTruncated` and
    // `receiveErrors` were counting what reached it.
    std::uint64_t idleReceives = 0;

    // Transient platform errors retried inside `Receive`. Non-zero is not by
    // itself a fault; growing steadily is.
    std::uint64_t receiveErrors = 0;

    // Datagrams the transport delivered larger than `MaxDatagramBytes`, dropped
    // rather than passed on half-read. Unreachable over UDP/IPv4, where the
    // bound *is* the maximum, and reachable over IPv6, which carries 20 bytes
    // more — so it is reached by two different mechanisms: Windows reports
    // `WSAEMSGSIZE`, and POSIX truncates silently and is caught by the buffer's
    // one spare byte (see the header).
    std::uint64_t datagramsTruncated = 0;

    // The receive clock at the first and last accepted datagram. Both stay 0 on
    // a session that received nothing, which `datagramsReceived` disambiguates.
    double firstReceiveTime = 0.0;
    double lastReceiveTime = 0.0;

    // Who sent the last one. The second question after "is anything arriving",
    // and the one that catches a sender pointed at the wrong machine.
    std::string lastPeer;
};

// A bound UDP socket. Caller-driven throughout: it starts no thread, invokes no
// callback, and does nothing between calls to `Receive`.
class VRMADAPTERVMC_API UdpReceiver final
{
public:
    UdpReceiver();
    ~UdpReceiver();

    UdpReceiver(const UdpReceiver&) = delete;
    UdpReceiver& operator=(const UdpReceiver&) = delete;

    // Binds. Closes any socket this object already held first, because a
    // receiver that half-rebinds is worse than one that starts over.
    //
    // On failure the object is closed and `VRM_VMC_SOCKET_BIND_FAILED` is
    // appended — error, not recoverable, with the requested endpoint as its
    // subject and the platform's own message as its detail. That covers the
    // three causes worth telling apart: the port is already served, the address
    // is not one this host holds, and the address does not parse.
    bool Open(const UdpReceiverConfig& config,
              std::vector<Diagnostic>* diagnostics = nullptr);

    void Close() noexcept;
    bool IsOpen() const noexcept { return _socket != -1; }

    // What the socket actually got, which is not always what was asked for: a
    // configured port of 0 is bound by the OS, and a test that wants two
    // receivers on one machine has to read the number back from here.
    const std::string& GetBoundEndpoint() const noexcept
    {
        return _boundEndpoint;
    }

    // Whether the bound address can only be reached from this machine. The
    // commonest reason a live session receives nothing from a sender that is
    // demonstrably sending.
    bool IsLoopbackOnly() const noexcept { return _loopbackOnly; }

    // What the kernel actually granted for the receive buffer, read back at
    // `Open` rather than assumed from the request — see
    // `UdpReceiverConfig::receiveBufferBytes`. 0 when the socket is closed or
    // the platform would not say.
    std::size_t GetReceiveBufferBytes() const noexcept
    {
        return _receiveBufferBytes;
    }

    // Waits up to `timeoutSeconds` for one datagram. Zero polls and returns
    // immediately; negative waits indefinitely, which is only correct for a
    // caller that has another way to stop (see the header).
    //
    // `datagram` is reused: `bytes` is resized to the received length and `peer`
    // rewritten, so a loop declares one of these outside it. On any status other
    // than `Received` the contents are unspecified and must not be read.
    ReceiveStatus Receive(ReceivedDatagram* datagram,
                          double timeoutSeconds = 0.0);

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
    void ResetStats() noexcept { _stats = UdpReceiverStats(); }

private:
    // A platform socket handle, kept as an integer so that no Winsock or POSIX
    // header reaches this file. -1 is "closed" on both: Winsock's INVALID_SOCKET
    // is the all-ones value of an unsigned pointer-width integer, which is this
    // when it is signed.
    std::intptr_t _socket = -1;

    std::string _boundEndpoint;
    bool _loopbackOnly = false;
    std::size_t _receiveBufferBytes = 0;
    std::string _lastError;

    // The steady-clock reading at `Open`, in the clock's own ticks, so that
    // `receiveTime` counts from a session's start (see the header). Set at
    // construction too, so `Now()` never reports the clock's own epoch.
    std::int64_t _epoch = 0;

    // Where a datagram is read before its real length is known. Sized once at
    // `Open` and never resized, which is the point: resizing the *caller's*
    // vector up to `MaxDatagramBytes` and back on every call would value-
    // initialise ~64 KB per datagram — a memset on the hot path of a class
    // whose whole shape exists to avoid one.
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

// The hand-off, and the only synchronised object in this adapter.
//
// A network thread pushes; the consumer's thread drains and decodes. Nothing
// downstream of `Drain` learns that a second thread exists, which is the whole
// point: the decode path and `motionRuntime` keep the single-threaded contract
// their tests are written against.
//
// **Overflow drops the oldest.** For live motion that is the only defensible
// direction — a queue that refused the newest datagram to keep a stale one would
// add latency the session never recovers, and would hand the assembler a frame
// boundary out of order on top of it. The drop is counted rather than reported,
// because it is a statement about the consumer's tick rate and not about the
// sender.
//
// There is deliberately no blocking `Pop` and no condition variable. A consumer
// that would block on this queue already has a thread of its own to wait on, and
// giving it one here would need a wakeup path for shutdown — a second
// cancellation problem, in a class whose entire job is to have no opinions.
class VRMADAPTERVMC_API DatagramQueue final
{
public:
    explicit DatagramQueue(const DatagramQueueConfig& config = {});

    // From the network thread. Returns false when this push displaced the oldest
    // datagram, so a recorder can log the moment it started losing traffic.
    // Takes its argument by value: a caller that is done with its buffer moves
    // it in, and one reusing a receive buffer copies, which is the same choice
    // it already makes with `PushDatagram`.
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

} // namespace vrmAdapterVmc
