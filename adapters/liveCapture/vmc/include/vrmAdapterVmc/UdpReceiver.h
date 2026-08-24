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
// ## What is left in this file, and why it is not the socket
//
// The socket is `liveTransport`'s (liveTransport/UdpReceiver.h). This adapter
// and its sibling wrote the same receiver twice and had drifted by 210 lines
// and four defects by the time the third live adapter arrived; the four were
// merged into both copies first, and then the class moved
// (osc-and-vrchat-trackers.md §2, §3.2).
//
// What could not move is the **diagnostic code**. A code set is frozen per
// adapter, before its decoder exists, so a shared receiver may not name one —
// it reports a `TransportEvent`, and the layer that knows which adapter it is
// maps the event onto that adapter's frozen code. This class is that layer.
// It is a facade rather than a re-implementation: every call below forwards,
// and the only thing it *does* is the translation in `src/UdpReceiver.cpp`.
//
// ## The silence timeout is still absent, and now for a visible reason
//
// The shared receiver has one; this adapter does not expose it, because
// `VRM_VMC_*` has no code for silence and its own documentation argues it did
// not need a ninth. Inventing a second spelling of the sibling's
// `VRM_MOCOPI_DEVICE_UNAVAILABLE` would be a contract change, and it is the
// adapter plan's §8 to make, not this file's. The difference used to be a
// missing 30 lines of receiver; it is now a `UdpReceiverConfig` with four
// fields instead of five, which is a difference a reader can see.
//
// ## What this class still promises, unchanged
//
// Caller-driven throughout: no thread, no callback, no work between calls, and
// no decoding — `Receive` hands back the bytes exactly as they arrived,
// including the ones a decoder would refuse. An over-long datagram is detected
// and dropped rather than passed on half-read. `receiveTime` is seconds since
// `Open` on a steady clock, which is the origin `vmc-packet-capture` records
// against. All three are `liveTransport`'s to keep now, and its header is where
// each is argued.
#pragma once

#include "vrmAdapterVmc/Diagnostics.h"
#include "vrmAdapterVmc/PacketCapture.h"
#include "vrmAdapterVmc/api.h"

#include "liveTransport/UdpReceiver.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace vrmAdapterVmc
{

// The port VMC senders use unless they are told otherwise. Named rather than
// defaulted silently, because an operator reading a config file should see the
// number the protocol's documentation told them — and because the shared
// receiver has no default port at all: a port is a protocol's property.
inline constexpr std::uint16_t DefaultVmcPort = 39539;

// The datagram, the status, the tally, and the opt-in queue are all the shared
// library's. Only the configuration is restated, and only because this adapter
// exposes four of its five fields (see the header).
using liveTransport::DatagramQueue;
using liveTransport::DatagramQueueConfig;
using liveTransport::DatagramQueueStats;
using liveTransport::ReceivedDatagram;
using liveTransport::ReceiveStatus;
using liveTransport::UdpReceiverStats;

struct UdpReceiverConfig
{
    // A numeric address, never a hostname: this is resolved with the
    // no-DNS-lookup flag, so an unresolvable string fails at `Open` without a
    // socket having touched the network. "0.0.0.0" listens on every interface,
    // "127.0.0.1" on loopback alone.
    std::string listenAddress = "0.0.0.0";
    std::uint16_t listenPort = DefaultVmcPort;

    // Whether another socket may already hold this address and port. Off by
    // default, so a second receiver started against a port that is already
    // serving reports `VRM_VMC_SOCKET_BIND_FAILED` instead of silently taking
    // some fraction of the traffic — which is the failure an operator has no
    // way at all to see from the outside.
    bool reuseAddress = false;

    // The kernel's receive buffer, in bytes; 0 leaves the platform default.
    // A request, not a setting: every platform may clamp it and Linux doubles
    // it, so what was actually granted is read back and reported by
    // `GetReceiveBufferBytes()`.
    std::size_t receiveBufferBytes = 0;
};

// A bound UDP socket, reporting this adapter's codes.
//
// Every member forwards to `liveTransport::UdpReceiver`. The two that do more
// than forward are `Open` and the constructor of `UdpReceiverConfig`: the first
// turns a `TransportEvent` into a `VRM_VMC_*` diagnostic, and the second is
// where this adapter's port and its four-of-five configuration surface live.
class VRMADAPTERVMC_API UdpReceiver final
{
public:
    UdpReceiver() = default;
    ~UdpReceiver() = default;

    UdpReceiver(const UdpReceiver&) = delete;
    UdpReceiver& operator=(const UdpReceiver&) = delete;

    // Binds. Closes any socket this object already held first, because a
    // receiver that half-rebinds is worse than one that starts over.
    //
    // A successful `Open` starts a session: the receive clock restarts and the
    // stats are cleared. A *failed* one leaves the stats alone, so a receiver
    // whose re-`Open` was refused can still be asked what the session it had
    // did.
    //
    // On failure the object is closed and `VRM_VMC_SOCKET_BIND_FAILED` is
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

    // Whether the bound address can only be reached from this machine.
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
    // `datagram` is reused: `bytes` is resized to the received length and
    // `peer` rewritten, so a loop declares one of these outside it. On any
    // status other than `Received` the contents are unspecified and must not be
    // read.
    //
    // No diagnostic sink, and that is this adapter's silence decision showing
    // through: the shared receiver reports silence into one, and with no
    // threshold configured there is nothing for it to report (see the header).
    ReceiveStatus Receive(ReceivedDatagram* datagram,
                          double timeoutSeconds = 0.0)
    {
        return _receiver.Receive(datagram, timeoutSeconds);
    }

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
    void ResetStats() noexcept { _receiver.ResetStats(); }

private:
    liveTransport::UdpReceiver _receiver;
};

} // namespace vrmAdapterVmc
