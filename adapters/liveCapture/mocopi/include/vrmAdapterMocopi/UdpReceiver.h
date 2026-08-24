// SPDX-License-Identifier: Apache-2.0
//
// The socket, and why it is the *first* layer of this adapter rather than the
// last.
//
//     [ UdpReceiver ] -> datagram -> a capture file -> (one day) a decoder
//
// Caller-driven throughout: no thread, no callback, no work between calls, and
// no decoding at all — `Receive` hands back the bytes exactly as they arrived,
// including the ones a decoder would refuse, because a receiver that filtered
// its own input would make a corpus a description of what the receiver let
// through rather than of what a source sent.
//
// ## The order this inverted, and the reason it inverted
//
// The sibling adapter built its receiver last, and the plan said this one
// should too — recorded decoder, mapping, live-source bridge, thin receiver
// (roadmap/adapters-mocopi-vmc-ardy.md §6). That order exists to keep every
// test below the transport runnable from committed bytes, and it was right
// there because the bytes existed: the VMC Protocol is a published
// specification, so a corpus could be *written* before a socket was opened.
//
// This protocol is not published. The vendor documents the transport — UDP,
// port 12351 by default, IPv4 only, unencrypted — and states nothing about the
// packet structure. So there were no committed bytes to build upward from, and
// exactly one way to obtain some without guessing: receive them from something
// that already speaks the format.
//
// ## The four defects this file predicted, and where they went
//
// A review of this header on 2026-08-11 found four defects the sibling receiver
// had identically, because they had been copied along with everything else —
// an oversize datagram truncated silently, a large finite timeout mapped onto
// "wait forever", a `poll` wake-up trusted without reading `revents`, and idle
// accounting charged to a call that had just met traffic. It corrected all four
// here, wrote down that they remained in the sibling, and named the trigger for
// turning the repetition into a library:
//
// > a **third** recorder — a third live adapter, or a tool that must drive both
// > — is what turns the repetition into a library, and the boundary that
// > library needs is argued in its own change rather than smuggled into this
// > one.
//
// All three halves of that happened, in that order. The four were merged into
// the sibling first, each with a test where a test could tell the fix from the
// defect (OSC-1). The boundary was argued in its own change. Then the class
// moved, unchanged, to `liveTransport` (osc-and-vrchat-trackers.md §2, §3.2).
//
// ## What is left in this file, and why it is not the socket
//
// The **diagnostic code**, and nothing else. A code set is frozen per adapter,
// before its decoder exists, so a shared receiver may not name one — it reports
// a `TransportEvent`, and the layer that knows which adapter it is maps the
// event onto that adapter's frozen code. This class is that layer: every call
// below forwards, and the only thing it *does* is the translation in
// `src/UdpReceiver.cpp`.
//
// This adapter maps both events, where the sibling maps one. `VRM_MOCOPI_*` has
// a code for silence and `VRM_VMC_*` does not, which is why the silence
// threshold is in `UdpReceiverConfig` here and absent there. The difference used
// to be thirty lines of receiver present in one copy and missing from the other;
// it is now one configuration field and one `switch` arm, which is a difference
// a reader can see rather than diff for.
//
// ## What this class still promises, unchanged
//
// An over-long datagram is detected and dropped rather than passed on
// half-read; `receiveTime` is seconds since `Open` on a steady clock, which is
// the origin `mocopi-packet-capture` records against; and silence is reported
// once per episode rather than once per poll. All three are `liveTransport`'s to
// keep now, and its header is where each is argued.
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

#include "liveTransport/UdpReceiver.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace vrmAdapterMocopi
{

// The port the capture product sends to unless it is told otherwise. Named
// rather than defaulted silently, because an operator reading a config file
// should see the number the vendor's documentation told them — and because the
// shared receiver has no default port at all: a port is a protocol's property.
inline constexpr std::uint16_t DefaultMocopiPort = 12351;

// The datagram, the status and the tally are the shared library's. So is
// `DatagramQueue`, which this adapter still does not use: a queue exists for a
// consumer that cannot poll often enough to keep a kernel receive buffer from
// overflowing, and that case is real and this adapter has not met it. It is
// opt-in there for exactly this reason, so not naming it costs nothing and
// reserves nothing.
using liveTransport::ReceivedDatagram;
using liveTransport::ReceiveStatus;
using liveTransport::UdpReceiverStats;

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
    // the socket.
    double silenceTimeoutSeconds = 0.0;
};

// A bound UDP socket, reporting this adapter's codes.
class VRMADAPTERMOCOPI_API UdpReceiver final
{
public:
    UdpReceiver() = default;
    ~UdpReceiver() = default;

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

    void Close() noexcept { _receiver.Close(); }
    bool IsOpen() const noexcept { return _receiver.IsOpen(); }

    // What the socket actually got, which is not always what was asked for: a
    // configured port of 0 is bound by the OS, and a test that wants two
    // receivers on one machine has to read the number back from here.
    const std::string& GetBoundEndpoint() const noexcept
    {
        return _receiver.GetBoundEndpoint();
    }

    // Whether the bound address can only be reached from this machine. For this
    // adapter that is a stronger statement than for its sibling: the vendor
    // documents `localhost` as unsupported, so a loopback-only receiver will
    // hear nothing from a device no matter what else is right.
    bool IsLoopbackOnly() const noexcept { return _receiver.IsLoopbackOnly(); }

    // What the kernel actually granted for the receive buffer, read back at
    // `Open` rather than assumed from the request. 0 when the socket is closed
    // or the platform would not say.
    std::size_t GetReceiveBufferBytes() const noexcept
    {
        return _receiver.GetReceiveBufferBytes();
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
    double Now() const noexcept { return _receiver.Now(); }

    // The platform's message for the last failure, bind or receive. Empty until
    // something fails; not cleared by a subsequent success, because a caller
    // reads it after a status told it to.
    const std::string& GetLastErrorText() const noexcept
    {
        return _receiver.GetLastErrorText();
    }

    const UdpReceiverStats& GetStats() const noexcept
    {
        return _receiver.GetStats();
    }

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
    void ResetStats() noexcept { _receiver.ResetStats(); }

private:
    liveTransport::UdpReceiver _receiver;
};

} // namespace vrmAdapterMocopi
