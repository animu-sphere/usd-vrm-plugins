// SPDX-License-Identifier: Apache-2.0
//
// The socket, and why it is the *first* layer of this adapter rather than the
// last.
//
//     [ UdpReceiver ] -> datagram -> a capture file -> (one day) a decoder
//
// It owns a socket, a bind address, a receive clock, and a size limit. It owns
// no decoding at all — `Receive` hands back the bytes exactly as they arrived,
// including the ones a decoder would refuse, because a receiver that filtered
// its own input would make a corpus a description of what the receiver let
// through rather than of what a source sent.
//
// ## The order this inverts, and the reason it inverts
//
// The sibling adapter built its receiver last, and the plan says this one
// should too — recorded decoder, mapping, live-source bridge, thin receiver
// (roadmap/adapters-mocopi-vmc-ardy.md §6). That order exists to keep every
// test below the transport runnable from committed bytes, and it was right
// there because the bytes existed: the VMC Protocol is a published
// specification, so a corpus could be *written* before a socket was opened.
//
// This protocol is not published. The vendor documents the transport — UDP,
// port 12351 by default, IPv4 only, unencrypted — and states nothing about the
// packet structure (§ Milestone D). So there are no committed bytes to build
// upward from, and there is exactly one way to obtain some without guessing:
// receive them from something that already speaks the format. The transport is
// therefore not the last layer that needs writing but the only one that *can*
// be written, and the rule it appears to break is the rule that sent it here —
// "the fixture-driven tests stay deterministic" is a statement about the layers
// that decode, and this layer decodes nothing.
//
// It also costs nothing that the original order was protecting. Its own tests
// need no sender, no device and no fixture: a socket bound on loopback and a
// dozen bytes sent to it exercise every path here, which is why this file
// arrives with a test suite and not with a caveat.
//
// ## Why this is a second socket and not the sibling's
//
// `PacketCapture.h` in this directory argues the same question for the capture
// format and the argument transfers unchanged: reaching the sibling's header is
// forbidden (WORKSPACE.md §2 gives an adapter two edges, adapter plan §2.1 makes
// the two live adapters siblings and never a stack), `motionRuntime` is the
// wrong home because a socket in that library is what its own boundary check
// exists to refuse, and a new shared library would be a boundary and a feature
// in one change (recorded-motion-sources.md §12).
//
// What is different — and worth stating plainly, because it is the strongest
// argument against this file that exists — is the *weight*. Repeating a
// line-oriented text format is a few hundred lines of parsing; repeating a
// socket is per-platform error classification, and the sibling paid for that
// classification in CI rather than in review. So the trigger named for the
// capture format is restated here and it has not changed: a **third** recorder
// — a third live adapter, or a tool that must drive both — is what turns the
// repetition into a library, and the boundary that library needs is argued in
// its own change rather than smuggled into this one. Until then the two copies
// are held together by their tests rather than by their source, and where this
// one deliberately differs from the sibling it says so at the difference.
//
// **The trigger now has evidence behind it rather than only an argument.** A
// review of this file found four defects the sibling has identically, because
// they were copied along with everything else: an oversize datagram silently
// truncated and handed back as whole on POSIX, a large *finite* timeout mapped
// onto the sentinel that means "wait forever", a poll wake-up whose `revents`
// was never inspected, and idle accounting applied to a call that had just
// proved traffic was arriving. All four are corrected here and remain in the
// sibling as of 2026-08-11. That is the cost of the repetition stated as a
// measurement rather than as a worry, and it belongs in this file rather than
// in a commit message: whoever weighs a third copy should be able to read what
// the second one cost.
//
// ## What this one does not have: a queue
//
// The sibling carries a `DatagramQueue`, because a consumer that cannot poll
// often enough to keep a kernel receive buffer from overflowing needs a
// hand-off that is not a mutex around the runtime. That case is real and this
// adapter has not met it: its first and only consumer is a recording loop that
// does nothing between receives. Adding a queue here now would be reserving a
// mechanism for a caller that does not exist, which is the same thing this
// library's CMakeLists refuses to do with platform libraries — so the queue
// arrives with the consumer that needs it, and the thread boundary it implies
// (network thread -> raw datagrams -> consumer thread, never poses) is settled
// in the sibling's header rather than re-argued when it does.
//
// ## Two transport codes, and the second one is opt-in
//
// The frozen set (Diagnostics.h) gives this layer two of its nine codes, and
// the split between them is the difference between a session that cannot start
// and a session that has not started yet.
//
// `VRM_MOCOPI_SOCKET_BIND_FAILED` is raised here and is the only failure in
// this file a session cannot continue past — a receiver that never bound has
// nothing to recover into. A lost datagram, a transient platform error and an
// empty poll are counted in `UdpReceiverStats` and named in
// `GetLastErrorText()` instead, because a code invented for them would say
// "recoverable" on every line and describe a receive loop rather than a source.
//
// `VRM_MOCOPI_DEVICE_UNAVAILABLE` is the code the sibling's frozen set does not
// have, and this layer is the only one that can raise it: silence is invisible
// to every layer above, which sees an absence of datagrams as an absence of
// work. What this layer cannot know is *how much* silence means a device is not
// there — a phone that is being strapped on and a phone that has been switched
// off produce the same nothing — so the threshold is stated by the caller and
// there is no default (`silenceTimeoutSeconds` of 0 disables it entirely). It
// is reported once per episode and re-armed by the next datagram, because a
// session that has genuinely lost its device should not have its log filled by
// the loop that noticed.
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
// one, and the recording tool above would write a packet the source never sent
// into a fixture and blame the source for it — which is precisely the failure
// this bound exists to prevent. With one spare byte, an over-long datagram comes
// back as `MaxDatagramBytes + 1`, is counted in `datagramsTruncated`, and is
// dropped. Windows says so directly with `WSAEMSGSIZE`; both paths lead to the
// same counter.
//
// IPv4 is the product's own stated limit rather than this class's: a caller may
// bind an IPv6 address and the resolver will take it, since refusing one would
// be this layer inventing a restriction on its own socket. The only consequence
// is that IPv6's 20 additional payload bytes are the one way `datagramsTruncated`
// is reachable at all — which is exactly why it has to be reachable *correctly*
// rather than assumed unreachable.
//
// ## The clock is monotonic, and that is a requirement rather than a taste
//
// `receiveTime` is seconds since `Open`, read from a steady clock. The capture
// format states that receive times must not go backwards (PacketCapture.h)
// because arrival order is the whole point of it, and a wall clock steps
// backwards for reasons that have nothing to do with the session — NTP, a
// timezone edit, a suspended laptop. Counting from `Open` is also exactly the
// origin `mocopi-packet-capture` records against, so a recording tool copies
// the number across instead of rebasing it.
//
// ## Diagnosing a session that receives nothing
//
// Silence is the commonest live failure and it has a small number of causes a
// socket can distinguish: nothing was ever bound, something is bound but only
// to loopback, or datagrams are arriving from an address the operator did not
// expect. The middle one is sharper here than it is for the sibling. The vendor
// states that `localhost` is not a supported destination — the source is a
// phone on the same network, and it cannot reach this machine's loopback at all
// — so a receiver reporting `IsLoopbackOnly()` is not merely narrow, it is
// guaranteed to hear nothing from a real device. `GetBoundEndpoint`,
// `IsLoopbackOnly`, and the stats' `lastPeer` are the facts for that; formatting
// them for a human is a CLI's job and not this class's.
#pragma once

#include "vrmAdapterMocopi/Diagnostics.h"
#include "vrmAdapterMocopi/PacketCapture.h"
#include "vrmAdapterMocopi/api.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace vrmAdapterMocopi
{

// The port the capture product sends to unless it is told otherwise. Named
// rather than defaulted silently, because an operator reading a config file
// should see the number the vendor's documentation told them.
inline constexpr std::uint16_t DefaultMocopiPort = 12351;

struct UdpReceiverConfig
{
    // A numeric address, never a hostname: this is resolved with the
    // no-DNS-lookup flag, so an unresolvable string fails at `Open` without a
    // socket having touched the network. "0.0.0.0" listens on every IPv4
    // interface, "127.0.0.1" on loopback alone — which no real device can
    // reach (see the header).
    std::string listenAddress = "0.0.0.0";
    std::uint16_t listenPort = DefaultMocopiPort;

    // Whether another socket may already hold this address and port. Off by
    // default, so a second receiver started against a port that is already
    // serving reports `VRM_MOCOPI_SOCKET_BIND_FAILED` instead of silently
    // taking some fraction of the traffic — which is the failure an operator
    // has no way at all to see from the outside.
    bool reuseAddress = false;

    // The kernel's receive buffer, in bytes; 0 leaves the platform default.
    // A request, not a setting: every platform may clamp it and Linux doubles
    // it, so what was actually granted is read back and reported by
    // `GetReceiveBufferBytes()`.
    std::size_t receiveBufferBytes = 0;

    // Seconds of silence after which `Receive` reports
    // `VRM_MOCOPI_DEVICE_UNAVAILABLE`, measured from the last accepted datagram
    // or from `Open` when none has arrived yet. **0 disables it**, and that is
    // the default because this layer has no basis for a number: how long a
    // device may reasonably take to start is a property of the session, not of
    // the socket (see the header).
    double silenceTimeoutSeconds = 0.0;
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
// what the *socket* did, so it is the only tally in this adapter that can
// describe traffic which never became a packet — and for now it is the only
// tally there is, since nothing here decodes one.
struct UdpReceiverStats
{
    std::uint64_t datagramsReceived = 0;
    std::uint64_t bytesReceived = 0;

    // Calls that found nothing waiting. A zero-timeout poll that finds an empty
    // socket counts here too — it is the same fact, asked more often.
    //
    // A call that met a truncated datagram or a transient error and then ran out
    // of budget does **not** count here, and that is a deliberate departure from
    // the sibling. Such a call found something waiting; counting it as idle
    // would tally it twice, once here and once in the counter that describes
    // what it actually met.
    std::uint64_t idleReceives = 0;

    // Transient platform errors retried inside `Receive`. Non-zero is not by
    // itself a fault; growing steadily is.
    std::uint64_t receiveErrors = 0;

    // Datagrams the transport delivered larger than `MaxDatagramBytes`, dropped
    // rather than passed on half-read. Unreachable over UDP/IPv4, where the
    // bound *is* the protocol's maximum; reachable over IPv6, which is why the
    // buffer holds one byte more than the bound (see the header) rather than
    // relying on a platform to report the overrun. Windows reports it as
    // `WSAEMSGSIZE`; POSIX truncates silently and the spare byte is what catches
    // it there.
    std::uint64_t datagramsTruncated = 0;

    // How many times silence crossed `silenceTimeoutSeconds`. One per episode,
    // not one per poll — see the header. Always 0 when the threshold is off.
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
class VRMADAPTERMOCOPI_API UdpReceiver final
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
    // stats are cleared, and the silence state is rearmed. A *failed* one leaves
    // the stats alone, so a receiver whose re-`Open` was refused can still be
    // asked what the session it had did.
    //
    // On failure the object is closed and `VRM_MOCOPI_SOCKET_BIND_FAILED` is
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

    // Whether the bound address can only be reached from this machine. For this
    // adapter that is a stronger statement than for its sibling: the vendor
    // documents `localhost` as unsupported, so a loopback-only receiver will
    // hear nothing from a device no matter what else is right.
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
    // caller that has another way to stop (see the header).
    //
    // `datagram` is reused: `bytes` is resized to the received length and
    // `peer` rewritten, so a loop declares one of these outside it. On any
    // status other than `Received` the contents are unspecified and must not be
    // read.
    //
    // `diagnostics` is where `VRM_MOCOPI_DEVICE_UNAVAILABLE` is appended when a
    // silence threshold is configured and has been crossed. It is threaded
    // through the receive call rather than offered as a separate query because
    // this is the call that knows time passed — a caller that had to remember a
    // second one would discover silence only in the sessions where it happened
    // to remember.
    ReceiveStatus Receive(ReceivedDatagram* datagram,
                          double timeoutSeconds = 0.0,
                          std::vector<Diagnostic>* diagnostics = nullptr);

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
    // device that was switched off for the whole of it. An episode still in
    // progress is therefore reported once more, in the new window, which is what
    // a window means.
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
    // Appends `VRM_MOCOPI_DEVICE_UNAVAILABLE` if the configured threshold has
    // been crossed and has not already been reported for this episode.
    void _ReportSilence(std::vector<Diagnostic>* diagnostics);

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

    // The point silence is measured from: the receive clock at the last accepted
    // datagram, or 0.0 — which is `Open` — when none has arrived.
    //
    // Held here rather than read out of `_stats`, and the difference is not
    // cosmetic. `_stats` is a tally a caller may zero at any moment, and it is
    // cleared by `Open` while these two are set by it; deriving a wire fact from
    // it made a re-`Open` compare a clock that had restarted against a time
    // stamped in the previous epoch, which left the silence check blind for as
    // long as the previous session had run.
    double _lastTrafficTime = 0.0;
    // Whether any datagram has arrived since `Open`. Chooses between the two
    // things this code can honestly say — a source that has not started and one
    // that has stopped — and is a session fact, so `ResetStats` leaves it alone.
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
    // value-initialise ~64 KB per datagram — a memset on the hot path of a class
    // whose whole shape exists to avoid one. Released by `Close`, so a receiver
    // a caller is holding but not listening on costs nothing.
    std::vector<std::uint8_t> _buffer;

    UdpReceiverStats _stats;
};

} // namespace vrmAdapterMocopi
