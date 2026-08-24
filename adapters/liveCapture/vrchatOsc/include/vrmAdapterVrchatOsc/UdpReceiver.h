// SPDX-License-Identifier: Apache-2.0
//
// The socket, and the only thing this adapter does with it before a decoder
// exists.
//
//     [ UdpReceiver ] -> datagram -> a capture file -> (VRC-2) a decoder
//
// Caller-driven throughout: no thread, no callback, no work between calls, and
// no decoding at all — `Receive` hands back the bytes exactly as they arrived,
// including the ones a decoder would refuse, because a receiver that filtered
// its own input would make a corpus a description of what the receiver let
// through rather than of what a source sent.
//
// ## The order, and why it is the mocopi one rather than the VMC one
//
// `vrmAdapterVmc` built its receiver last: the VMC Protocol is published, so a
// corpus could be *written* before a socket was opened, and every test below the
// transport ran from committed bytes. `vrmAdapterMocopi` had to invert that,
// because its protocol is not published and there were no bytes to build upward
// from.
//
// This adapter's protocol *is* published, and it takes the inverted order
// anyway. That is a choice rather than a constraint, and it is the whole of §6:
// what a product sends is a measurement, and the fact that the receiving
// specification is public does not establish that a sender implements all of it,
// or only it. Sony's help pages list `VRChat (OSC)` as a mocopi transfer format
// and name VRChat's default port; that is a menu entry, and this repository does
// not infer a packet shape from one. So the recorder lands first, the inventory
// is measured from real datagrams (VRC-1), and the decoder is designed from the
// inventory (VRC-2).
//
// ## What is in this file, and why it is not the socket
//
// The **diagnostic code**, and nothing else. A code set is frozen per adapter,
// before its decoder exists, so a shared receiver may not name one — it reports
// a `TransportEvent`, and the layer that knows which adapter it is maps the
// event onto that adapter's frozen code. This class is that layer: every call
// below forwards, and the only thing it *does* is the translation in
// `src/UdpReceiver.cpp`.
//
// This adapter maps both events, as `vrmAdapterMocopi` does and
// `vrmAdapterVmc` does not: `VRM_VRCHAT_OSC_SOURCE_TIMEOUT` exists, so the
// silence threshold is a field in `UdpReceiverConfig` here. Nothing about that
// was decided in this file — the code set was frozen in §8 before this directory
// existed, and whether a threshold is exposed follows from whether the set has a
// code for it.
//
// ## What this class promises, all of it `liveTransport`'s to keep
//
// An over-long datagram is detected and dropped rather than passed on half-read;
// `receiveTime` is seconds since `Open` on a steady clock, which is the origin a
// capture records against; and silence is reported once per episode rather than
// once per poll. Those are the four defects a review found in this pair of files
// on 2026-08-11 and OSC-1 merged, one behaviour per commit, before the class
// moved. This adapter inherits every one of them fixed, which is the value of
// having extracted the ring *before* the third consumer rather than after.
//
// ## Diagnosing a session that receives nothing
//
// Silence is the commonest live failure. A socket can distinguish a small number
// of causes: nothing was ever bound, something is bound but only to loopback, or
// datagrams are arriving from an address the operator did not expect.
//
// The middle one differs from `vrmAdapterMocopi`'s and the difference is worth
// stating, because copying that adapter's warning across would be wrong. There,
// loopback is *guaranteed* useless: the source is a phone on the same network
// and the vendor documents `localhost` as unsupported. Here the sender may
// legitimately be a process on this machine — a relay, or a capture application
// configured to send to 127.0.0.1 — so loopback is narrow rather than
// impossible. `IsLoopbackOnly()` is reported and left for a CLI to phrase;
// refusing it would be a socket inventing a restriction on itself.
#pragma once

#include "vrmAdapterVrchatOsc/Diagnostics.h"
#include "vrmAdapterVrchatOsc/PacketCapture.h"
#include "vrmAdapterVrchatOsc/api.h"

#include "liveTransport/UdpReceiver.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace vrmAdapterVrchatOsc
{

// The port a VRChat OSC sender sends to unless it is told otherwise — the port
// VRChat itself listens on, which is what this adapter stands in for. Named
// rather than defaulted silently, because an operator reading a config file
// should see the number the application's own documentation told them, and
// because the shared receiver has no default port at all: a port is a protocol's
// property and `liveTransport` knows no protocol.
//
// 9001 is the other half of that pair and is deliberately not here. It is the
// port VRChat *sends* from, and this adapter never sends: an outbound half is
// one of the things §12 excludes on purpose.
inline constexpr std::uint16_t DefaultVrchatOscPort = 9000;

// The datagram, the status and the tally are the shared library's. So is
// `DatagramQueue`, which this adapter does not use: a queue exists for a
// consumer that cannot poll often enough to keep a kernel receive buffer from
// overflowing, and a recorder is a polling loop that always can. It is opt-in
// there for exactly this reason, so not naming it costs nothing and reserves
// nothing.
using liveTransport::ReceivedDatagram;
using liveTransport::ReceiveStatus;
using liveTransport::UdpReceiverStats;

struct UdpReceiverConfig
{
    // A numeric address, never a hostname: this is resolved with the
    // no-DNS-lookup flag, so an unresolvable string fails at `Open` without a
    // socket having touched the network. "0.0.0.0" listens on every IPv4
    // interface, "127.0.0.1" on loopback alone — which is narrow rather than
    // useless here (see the header).
    std::string listenAddress = "0.0.0.0";
    std::uint16_t listenPort = DefaultVrchatOscPort;

    // Whether another socket may already hold this address and port. Off by
    // default, so a second receiver started against a port that is already
    // serving reports `VRM_VRCHAT_OSC_SOCKET_BIND_FAILED` instead of silently
    // taking some fraction of the traffic — which is the failure an operator has
    // no way at all to see from the outside.
    //
    // It is worth more than a default here than it is for either sibling. This
    // is the port VRChat itself binds, so the machine an operator is most likely
    // to record on is the one where something else may already be listening.
    bool reuseAddress = false;

    // The kernel's receive buffer, in bytes; 0 leaves the platform default. A
    // request, not a setting: every platform may clamp it and Linux doubles it,
    // so what was actually granted is read back and reported by
    // `GetReceiveBufferBytes()`.
    std::size_t receiveBufferBytes = 0;

    // Seconds of silence after which `Receive` reports
    // `VRM_VRCHAT_OSC_SOURCE_TIMEOUT`, measured from the last accepted datagram
    // or from `Open` when none has arrived yet. **0 disables it**, and that is
    // the default because this layer has no basis for a number: how long a
    // sender may reasonably take to start is a property of the session, not of
    // the socket.
    double silenceTimeoutSeconds = 0.0;
};

// A bound UDP socket, reporting this adapter's codes.
class VRMADAPTERVRCHATOSC_API UdpReceiver final
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
    // On failure the object is closed and `VRM_VRCHAT_OSC_SOCKET_BIND_FAILED` is
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

    // Whether the bound address can only be reached from this machine. A fact
    // rather than a verdict here, unlike the mocopi adapter's: a sender on this
    // machine is a legitimate configuration for this protocol.
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
    // caller that has another way to stop.
    //
    // `datagram` is reused: `bytes` is resized to the received length and `peer`
    // rewritten, so a loop declares one of these outside it. On any status other
    // than `Received` the contents are unspecified and must not be read.
    //
    // `diagnostics` is where `VRM_VRCHAT_OSC_SOURCE_TIMEOUT` is appended when a
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

    // Starts a new counting window without disturbing the session. It rearms the
    // silence report as well as zeroing the tally, and does not move the point
    // silence is measured from — both are `liveTransport`'s contract, argued in
    // its header.
    void ResetStats() noexcept { _receiver.ResetStats(); }

private:
    liveTransport::UdpReceiver _receiver;
};

} // namespace vrmAdapterVrchatOsc
