// SPDX-License-Identifier: Apache-2.0
//
// The socket, before anything decodes what comes out of it.
//
// Every test here opens a real socket, on loopback and on an OS-assigned port —
// never 12351, because a developer with a real source running would otherwise
// find this suite fighting it for the port. That is also why this is its own
// CTest name: it is the only test in this adapter that a runner forbidding
// sockets would have to exclude, and excluding a name is cheaper than excluding
// a claim (roadmap §9.5).
//
// The sender is in this file rather than in the library. §9.3 asks for a "test
// sender", and that is what it is: the adapter receives, and a `UdpSender` in
// `vrmAdapterMocopi` would be a class no consumer of the adapter has a use for.
//
// **Nothing here asserts anything about the wire format**, and that is the
// point of the layer rather than a gap in its tests. The payloads below are
// counting patterns that are not claimed to be packets, because the protocol
// has no published specification and a payload shaped to look plausible would
// be this repository's guess wearing the appearance of evidence
// (PacketCapture.h says the same thing about its own fixtures). What is
// asserted is that whatever a source sends arrives whole, in order, with the
// instant it arrived — and that it survives the trip to a capture file
// unchanged, which is the one claim that has to hold before a corpus recorded
// with this can be trusted to mean anything.
#include "vrmAdapterMocopi/UdpReceiver.h"

#include "vrmAdapterMocopi/Diagnostics.h"
#include "vrmAdapterMocopi/PacketCapture.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    include <winsock2.h>
#    include <ws2tcpip.h>
#else
#    include <netdb.h>
#    include <netinet/in.h>
#    include <sys/socket.h>
#    include <unistd.h>
#endif

namespace
{

using vrmAdapterMocopi::Diagnostic;
using vrmAdapterMocopi::DiagnosticCode;
using vrmAdapterMocopi::DiagnosticSeverity;
using vrmAdapterMocopi::PacketCapture;
using vrmAdapterMocopi::ReceivedDatagram;
using vrmAdapterMocopi::ReceiveStatus;
using vrmAdapterMocopi::RecordedDatagram;
using vrmAdapterMocopi::UdpReceiver;
using vrmAdapterMocopi::UdpReceiverConfig;

// Long enough that a loopback datagram which has been handed to the kernel is
// certainly readable, short enough that a genuinely lost one fails the suite
// rather than hanging it. Nothing here sleeps waiting for traffic: every wait is
// on a datagram that has already been sent.
constexpr double kLoopbackTimeout = 5.0;

// ---------------------------------------------------------------------------
// The test sender
// ---------------------------------------------------------------------------

#if defined(_WIN32)
using RawSocket = SOCKET;
constexpr RawSocket kNoSocket = INVALID_SOCKET;
void
CloseRaw(RawSocket handle)
{
    closesocket(handle);
}
#else
using RawSocket = int;
constexpr RawSocket kNoSocket = -1;
void
CloseRaw(RawSocket handle)
{
    ::close(handle);
}
#endif

// Splits "127.0.0.1:54321" and "[::1]:54321" the way the receiver renders them.
bool
SplitEndpoint(const std::string& endpoint, std::string* host, std::string* port)
{
    const std::size_t colon = endpoint.rfind(':');
    if (colon == std::string::npos) {
        return false;
    }
    *host = endpoint.substr(0, colon);
    *port = endpoint.substr(colon + 1);
    if (host->size() >= 2 && host->front() == '[' && host->back() == ']') {
        *host = host->substr(1, host->size() - 2);
    }
    return !host->empty() && !port->empty();
}

class LoopbackSender
{
public:
    ~LoopbackSender() { Close(); }

    bool Open(const std::string& endpoint)
    {
        Close();
        std::string host;
        std::string port;
        if (!SplitEndpoint(endpoint, &host, &port)) {
            return false;
        }

        addrinfo hints = {};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_DGRAM;
        hints.ai_protocol = IPPROTO_UDP;
        hints.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV;

        addrinfo* resolved = nullptr;
        if (::getaddrinfo(host.c_str(), port.c_str(), &hints, &resolved) != 0
            || !resolved) {
            return false;
        }
        for (const addrinfo* it = resolved; it; it = it->ai_next) {
            const RawSocket handle =
                ::socket(it->ai_family, it->ai_socktype, it->ai_protocol);
            if (handle == kNoSocket) {
                continue;
            }
            // Connected, so a send is one call and a peer is one value. The
            // receiver never sends, so nothing here depends on the reverse
            // direction working.
            if (::connect(handle, it->ai_addr,
                          static_cast<socklen_t>(it->ai_addrlen))
                != 0) {
                CloseRaw(handle);
                continue;
            }
            _socket = handle;
            break;
        }
        ::freeaddrinfo(resolved);
        return _socket != kNoSocket;
    }

    bool Send(const std::vector<std::uint8_t>& bytes) const
    {
        if (_socket == kNoSocket) {
            return false;
        }
        const auto sent = ::send(
            _socket, reinterpret_cast<const char*>(bytes.data()),
            static_cast<int>(bytes.size()), 0);
        return sent == static_cast<decltype(sent)>(bytes.size());
    }

    void Close()
    {
        if (_socket != kNoSocket) {
            CloseRaw(_socket);
            _socket = kNoSocket;
        }
    }

private:
    RawSocket _socket = kNoSocket;
};

UdpReceiverConfig
LoopbackConfig()
{
    UdpReceiverConfig config;
    config.listenAddress = "127.0.0.1";
    // Assigned by the OS and read back from `GetBoundEndpoint`. Naming the real
    // port here would make the suite fail on any machine already receiving —
    // and on this adapter the default is one a developer's own device is very
    // likely to be aimed at.
    config.listenPort = 0;
    return config;
}

// A counting pattern, and not a packet. See the file header: this adapter has
// no wire format to shape a payload after, and inventing one here would put a
// guess where a fixture belongs.
std::vector<std::uint8_t>
Payload(std::size_t size, std::uint8_t seed)
{
    std::vector<std::uint8_t> bytes(size);
    for (std::size_t index = 0; index != size; ++index) {
        bytes[index] = static_cast<std::uint8_t>(seed + index);
    }
    return bytes;
}

// ---------------------------------------------------------------------------
// Binding
// ---------------------------------------------------------------------------

void
TestAnAddressThatIsNotOneIsRefusedBeforeTheNetworkIsTouched()
{
    UdpReceiver receiver;
    UdpReceiverConfig config;
    config.listenAddress = "device.example.com";
    config.listenPort = 0;

    std::vector<Diagnostic> diagnostics;
    assert(!receiver.Open(config, &diagnostics));
    assert(!receiver.IsOpen());

    // The frozen code for the one transport failure a session cannot continue
    // past, at the one severity that says so.
    assert(diagnostics.size() == 1);
    assert(diagnostics[0].code == DiagnosticCode::SocketBindFailed);
    assert(diagnostics[0].severity == DiagnosticSeverity::Error);
    assert(!diagnostics[0].recoverable);
    // The endpoint that was asked for, so a reader knows which configuration
    // line to go and change.
    assert(diagnostics[0].subject == "device.example.com:0");
    assert(!receiver.GetLastErrorText().empty());
}

void
TestAPortAlreadyServedIsRefusedRatherThanQuietlyShared()
{
    UdpReceiver first;
    assert(first.Open(LoopbackConfig()));

    std::string host;
    std::string port;
    assert(SplitEndpoint(first.GetBoundEndpoint(), &host, &port));

    UdpReceiverConfig second = LoopbackConfig();
    second.listenPort = static_cast<std::uint16_t>(std::stoi(port));

    // A second receiver on a port that is already serving takes some unknowable
    // fraction of the traffic, which is the one failure an operator cannot see
    // from the outside at all. It is refused instead.
    UdpReceiver other;
    std::vector<Diagnostic> diagnostics;
    assert(!other.Open(second, &diagnostics));
    assert(diagnostics.size() == 1);
    assert(diagnostics[0].code == DiagnosticCode::SocketBindFailed);
    assert(diagnostics[0].subject == host + ":" + port);
}

void
TestASocketReportsWhereItLandedAndWhoCanReachIt()
{
    UdpReceiver loopback;
    assert(loopback.Open(LoopbackConfig()));
    assert(loopback.IsOpen());
    // A configured 0 is not what the socket got.
    assert(loopback.GetBoundEndpoint() != "127.0.0.1:0");
    assert(loopback.GetBoundEndpoint().rfind("127.0.0.1:", 0) == 0);
    // Sharper here than for the sibling adapter: the vendor documents
    // `localhost` as an unsupported destination, so a loopback-only receiver is
    // not merely narrow — it is guaranteed to hear nothing from a device.
    assert(loopback.IsLoopbackOnly());

    UdpReceiverConfig everywhere;
    everywhere.listenAddress = "0.0.0.0";
    everywhere.listenPort = 0;
    UdpReceiver open;
    assert(open.Open(everywhere));
    assert(!open.IsLoopbackOnly());

    open.Close();
    assert(!open.IsOpen());
    assert(open.GetBoundEndpoint().empty());
    assert(open.GetReceiveBufferBytes() == 0);
}

void
TestTheReceiveBufferIsReportedAsGrantedRatherThanAsAsked()
{
    UdpReceiver defaulted;
    assert(defaulted.Open(LoopbackConfig()));

    // **This is the read-back proof, and the request case below is not.** This
    // receiver asked for nothing — `receiveBufferBytes` is 0 — so a non-zero
    // answer cannot be an echo of anything the caller said and can only have
    // come off the socket. Silently getting the default and then losing
    // datagrams is the hardest failure here to see from the outside, and it is
    // this assertion that would catch the read-back going away.
    assert(defaulted.GetReceiveBufferBytes() > 0);

    UdpReceiverConfig config = LoopbackConfig();
    config.receiveBufferBytes = 1024u * 1024u;
    UdpReceiver asked;
    assert(asked.Open(config));

    // Deliberately *not* asserted equal to the request, and deliberately not
    // asserted greater than the default either. Every platform may clamp the
    // value, Linux reports double what it was given, and macOS caps it at a
    // sysctl a test cannot read — so a strict `>` would be asserting a property
    // of somebody's kernel configuration. What this checks is the weaker and
    // true claim: asking for more never comes back with less.
    assert(asked.GetReceiveBufferBytes() > 0);
    assert(asked.GetReceiveBufferBytes() >= defaulted.GetReceiveBufferBytes());
}

void
TestAReceiverCountsFromItselfBeforeItIsEverOpened()
{
    // Not the steady clock's own epoch, which on Linux is the time since the
    // machine booted — a number a caller would read as a session that has been
    // quiet for weeks.
    UdpReceiver fresh;
    assert(fresh.Now() >= 0.0);
    assert(fresh.Now() < 60.0);
}

// ---------------------------------------------------------------------------
// Receiving
// ---------------------------------------------------------------------------

void
TestAQuietSocketIsIdleAndAClosedOneSaysSo()
{
    ReceivedDatagram datagram;

    UdpReceiver closed;
    // A caller that gets `Closed` and keeps looping is spinning, so it is a
    // different answer from "nothing arrived".
    assert(closed.Receive(&datagram) == ReceiveStatus::Closed);
    assert(closed.GetStats().idleReceives == 0);

    UdpReceiver receiver;
    assert(receiver.Open(LoopbackConfig()));
    assert(receiver.Receive(&datagram) == ReceiveStatus::Idle);
    assert(receiver.Receive(&datagram, 0.001) == ReceiveStatus::Idle);
    assert(receiver.GetStats().idleReceives == 2);
    assert(receiver.GetStats().datagramsReceived == 0);
    // An empty poll is not a failure at any timeout, and with no silence
    // threshold configured it is not a diagnostic either.
    assert(receiver.GetLastErrorText().empty());
    assert(receiver.GetStats().silenceReports == 0);
}

void
TestADatagramArrivesWholeWithItsSenderAndItsInstant()
{
    UdpReceiver receiver;
    assert(receiver.Open(LoopbackConfig()));

    LoopbackSender sender;
    assert(sender.Open(receiver.GetBoundEndpoint()));

    const double before = receiver.Now();

    const std::vector<std::uint8_t> small = Payload(20, 0x40);
    assert(sender.Send(small));

    ReceivedDatagram datagram;
    assert(receiver.Receive(&datagram, kLoopbackTimeout)
           == ReceiveStatus::Received);
    assert(datagram.bytes == small);
    // The sender is on loopback, on a port it did not choose; what matters is
    // that the receiver knows an address at all, which is the second question
    // after "is anything arriving".
    assert(datagram.peer.rfind("127.0.0.1:", 0) == 0);
    assert(datagram.receiveTime >= before);

    // Bigger than the first, into the same buffer: `Receive` resizes rather
    // than leaving the previous datagram's tail behind.
    const std::vector<std::uint8_t> large = Payload(2000, 0x01);
    assert(sender.Send(large));
    assert(receiver.Receive(&datagram, kLoopbackTimeout)
           == ReceiveStatus::Received);
    assert(datagram.bytes == large);
    const double second = datagram.receiveTime;

    // And smaller again, which is the direction a stale tail would survive.
    assert(sender.Send(small));
    assert(receiver.Receive(&datagram, kLoopbackTimeout)
           == ReceiveStatus::Received);
    assert(datagram.bytes == small);
    assert(datagram.receiveTime >= second);

    // A zero-length datagram is receivable, and it is the smallest thing a
    // decoder has to refuse without crashing (PacketCapture.h). A receiver that
    // reported it as `Idle` would hide it from the decoder that does not exist
    // yet — which is exactly the sort of hole a corpus recorded today would
    // carry into it.
    assert(sender.Send(std::vector<std::uint8_t>()));
    assert(receiver.Receive(&datagram, kLoopbackTimeout)
           == ReceiveStatus::Received);
    assert(datagram.bytes.empty());

    const vrmAdapterMocopi::UdpReceiverStats& stats = receiver.GetStats();
    assert(stats.datagramsReceived == 4);
    assert(stats.bytesReceived == small.size() * 2 + large.size());
    assert(stats.datagramsTruncated == 0);
    assert(stats.lastPeer == datagram.peer);
    assert(stats.firstReceiveTime <= stats.lastReceiveTime);
    assert(receiver.Now() >= stats.lastReceiveTime);
}

// ---------------------------------------------------------------------------
// Silence
// ---------------------------------------------------------------------------

// The one threshold in this file that is a duration rather than a count. Far
// above the 0.05 the silence tests configure, so a loaded CI runner does not
// make it flaky, and far below any timeout a test runner enforces.
constexpr double kSilenceGiveUp = 2.0;

// Polls until the receiver has reported `expected` silence episodes, and gives
// up rather than waiting forever.
//
// The bound is not defensive decoration — it is the difference between a defect
// that fails this suite and one that hangs it. Watching this test fail is how
// it was found: with the re-arm removed from `Receive`, the second episode
// never arrives, and an unbounded loop turned a two-line defect into a build
// that had to be killed by hand. A test whose failure mode is a hang reports
// "timed out" for every cause there is.
void
PollUntilSilenceReports(UdpReceiver& receiver, std::uint64_t expected,
                        std::vector<Diagnostic>* diagnostics)
{
    ReceivedDatagram datagram;
    const double deadline = receiver.Now() + kSilenceGiveUp;
    while (receiver.GetStats().silenceReports < expected) {
        assert(receiver.Now() < deadline);
        assert(receiver.Receive(&datagram, 0.02, diagnostics)
               == ReceiveStatus::Idle);
    }
}

void
TestSilenceIsNotReportedUntilACallerSaysHowMuchIsTooMuch()
{
    // No threshold, and therefore no code — however long the socket is quiet.
    // This layer has no basis for a number: a device being strapped on and a
    // device switched off produce the same nothing.
    UdpReceiver silent;
    assert(silent.Open(LoopbackConfig()));

    ReceivedDatagram datagram;
    std::vector<Diagnostic> diagnostics;
    for (int poll = 0; poll != 5; ++poll) {
        assert(silent.Receive(&datagram, 0.01, &diagnostics)
               == ReceiveStatus::Idle);
    }
    assert(diagnostics.empty());
    assert(silent.GetStats().silenceReports == 0);
}

void
TestSilenceIsReportedOncePerEpisodeAndRearmedByADatagram()
{
    UdpReceiverConfig config = LoopbackConfig();
    // Small enough that a few polls cross it, large enough that it is a
    // measurement rather than a race: the polls below wait for it.
    config.silenceTimeoutSeconds = 0.05;

    UdpReceiver receiver;
    assert(receiver.Open(config));

    ReceivedDatagram datagram;
    std::vector<Diagnostic> diagnostics;
    // Nothing has ever arrived, which is the half of this code the sibling
    // adapter's frozen set cannot express: a device that has not been started.
    PollUntilSilenceReports(receiver, 1, &diagnostics);
    assert(diagnostics.size() == 1);
    assert(diagnostics[0].code == DiagnosticCode::DeviceUnavailable);
    // Recoverable, and that is load-bearing rather than decorative: a session
    // waiting for a device to be switched on is an ordinary session.
    assert(diagnostics[0].recoverable);
    assert(diagnostics[0].source == receiver.GetBoundEndpoint());
    // No `timestamp`, deliberately. Diagnostics.h defines that field as the
    // *source's* clock at a frame, and this diagnostic exists precisely because
    // no frame arrived to carry one; the receiver's own clock in that column
    // would put two unrelated timelines in one session log. The duration is in
    // the detail instead, where a number needs no shared origin.
    assert(!diagnostics[0].timestamp.has_value());
    assert(diagnostics[0].detail.find("s ago") != std::string::npos);

    // Still quiet, still one report. A loop that noticed silence a hundred
    // times a second would fill a session log with the loop rather than with
    // the session.
    for (int poll = 0; poll != 5; ++poll) {
        assert(receiver.Receive(&datagram, 0.02, &diagnostics)
               == ReceiveStatus::Idle);
    }
    assert(diagnostics.size() == 1);
    assert(receiver.GetStats().silenceReports == 1);

    // A datagram ends the episode, and the next silence is a new one — so a
    // source that drops out repeatedly says so repeatedly.
    LoopbackSender sender;
    assert(sender.Open(receiver.GetBoundEndpoint()));
    assert(sender.Send(Payload(8, 0x10)));
    assert(receiver.Receive(&datagram, kLoopbackTimeout, &diagnostics)
           == ReceiveStatus::Received);
    assert(diagnostics.size() == 1);

    PollUntilSilenceReports(receiver, 2, &diagnostics);
    assert(diagnostics.size() == 2);
    assert(diagnostics[1].code == DiagnosticCode::DeviceUnavailable);
    // The second episode is the one that can say a source *stopped*, where the
    // first could only say nothing had started.
    assert(diagnostics[0].detail != diagnostics[1].detail);
}

void
TestSilenceIsCountedEvenWhenNobodyAskedForTheDiagnostic()
{
    // The tally is what a session report reads. If it depended on the loop
    // having somewhere to put a message, a report would say a session was fine
    // precisely because nobody was listening.
    UdpReceiverConfig config = LoopbackConfig();
    config.silenceTimeoutSeconds = 0.05;

    UdpReceiver receiver;
    assert(receiver.Open(config));

    PollUntilSilenceReports(receiver, 1, nullptr);
    assert(receiver.GetStats().silenceReports == 1);
}

void
TestAReopenedReceiverIsNotStillWaitingForTheLastSessionsSource()
{
    // The defect this pins: `Open` restarts the receive clock, so a silence
    // check that measured from a time stamped in the *previous* epoch compared
    // a clock near zero against a number from a session that had been running
    // for a while — and stayed blind for exactly that long, on a reconnect,
    // which is the one moment the code exists for.
    UdpReceiverConfig config = LoopbackConfig();
    config.silenceTimeoutSeconds = 0.05;

    UdpReceiver receiver;
    assert(receiver.Open(config));

    LoopbackSender sender;
    assert(sender.Open(receiver.GetBoundEndpoint()));
    assert(sender.Send(Payload(8, 0x30)));

    ReceivedDatagram datagram;
    assert(receiver.Receive(&datagram, kLoopbackTimeout)
           == ReceiveStatus::Received);
    assert(receiver.GetStats().datagramsReceived == 1);
    assert(receiver.GetStats().lastReceiveTime > 0.0);

    // Reconnect. The port is not the same one, which is the ordinary case and
    // also the reason the previous session's peer means nothing now.
    sender.Close();
    receiver.Close();
    assert(receiver.Open(config));

    // A session's tallies are the session's: everything in the struct is either
    // a count for this socket or a time on this epoch.
    assert(receiver.GetStats().datagramsReceived == 0);
    assert(receiver.GetStats().lastReceiveTime == 0.0);
    assert(receiver.GetStats().silenceReports == 0);

    std::vector<Diagnostic> diagnostics;
    PollUntilSilenceReports(receiver, 1, &diagnostics);
    assert(diagnostics.size() == 1);
    // And it is the *right* half of the code: nothing has arrived on this
    // socket, whatever the last one saw.
    assert(diagnostics[0].detail.find("no datagram has arrived")
           != std::string::npos);
}

void
TestANewCountingWindowCanStillSeeAnOngoingSilence()
{
    // `ResetStats` starts a new window. If it zeroed the tally but left the
    // episode marked as already reported, the window would end saying
    // `silenceReports == 0` — "the session was fine" — for a device that was
    // switched off the whole time, which is the precise failure the tally
    // exists to prevent.
    UdpReceiverConfig config = LoopbackConfig();
    config.silenceTimeoutSeconds = 0.05;

    UdpReceiver receiver;
    assert(receiver.Open(config));

    std::vector<Diagnostic> diagnostics;
    PollUntilSilenceReports(receiver, 1, &diagnostics);
    assert(receiver.GetStats().silenceReports == 1);

    receiver.ResetStats();
    assert(receiver.GetStats().silenceReports == 0);

    // The same episode, still in progress, reported once more — in the new
    // window. It is not measured afresh from the reset either: the silence is
    // already older than the threshold, so the very next poll carries it.
    ReceivedDatagram datagram;
    assert(receiver.Receive(&datagram, 0.0, &diagnostics)
           == ReceiveStatus::Idle);
    assert(receiver.GetStats().silenceReports == 1);
    assert(diagnostics.size() == 2);
}

// ---------------------------------------------------------------------------
// Truncation
// ---------------------------------------------------------------------------

void
TestAnOverlongDatagramIsDroppedRatherThanHandedBackAsWhole()
{
    // The defect this pins is the worst one a recorder can have, because it
    // fails *quietly and plausibly*: POSIX truncates a too-long datagram and
    // reports the buffer's length, which is indistinguishable from a datagram
    // that happened to be exactly that long. A receiver whose buffer was the
    // bound itself would therefore write a packet the source never sent into a
    // fixture, and every conclusion a decoder later drew from that fixture would
    // be about this repository's own mistake.
    //
    // Reaching it needs IPv6, which is the one transport that can carry more
    // than `MaxDatagramBytes` — so this test skips rather than fails where the
    // host has no IPv6 loopback or refuses to send a datagram that large. A
    // skip is honest here and a pass would not be: the platforms that can run
    // it are the platforms where the claim is testable at all.
    UdpReceiverConfig config;
    config.listenAddress = "::1";
    config.listenPort = 0;

    UdpReceiver receiver;
    if (!receiver.Open(config)) {
        std::puts("  (skipped: no IPv6 loopback on this host)");
        return;
    }

    LoopbackSender sender;
    if (!sender.Open(receiver.GetBoundEndpoint())) {
        std::puts("  (skipped: could not reach the IPv6 loopback endpoint)");
        return;
    }

    // Above the IPv4 bound the capture format enforces, below IPv6's own 65527
    // maximum. One byte over would do; a handful makes the intent legible.
    const std::vector<std::uint8_t> overlong =
        Payload(vrmAdapterMocopi::MaxDatagramBytes + 8, 0x00);
    if (!sender.Send(overlong)) {
        std::puts("  (skipped: this host will not send an over-long datagram)");
        return;
    }

    ReceivedDatagram datagram;
    const ReceiveStatus status = receiver.Receive(&datagram, 0.25);

    // Dropped, counted, and never presented as a datagram — on both platforms
    // and by two different mechanisms: Windows says WSAEMSGSIZE, POSIX says
    // nothing and is caught by the buffer's one spare byte.
    //
    // Which means **this test cannot fail on Windows for the defect it was
    // written about**, because WSAEMSGSIZE catches it there with or without the
    // spare byte. The lane that proves the fix is a POSIX one: without it,
    // `recvfrom` returns the buffer's length, the code below sees an ordinary
    // datagram, and the first assertion fails on `Received`. Worth knowing
    // before reading a green Windows run as evidence.
    assert(status == ReceiveStatus::Idle);
    assert(receiver.GetStats().datagramsTruncated == 1);
    assert(receiver.GetStats().datagramsReceived == 0);
    assert(receiver.GetStats().bytesReceived == 0);

    // And the socket still works afterwards: a refusal is not a shutdown.
    const std::vector<std::uint8_t> ordinary = Payload(24, 0x61);
    assert(sender.Send(ordinary));
    assert(receiver.Receive(&datagram, kLoopbackTimeout)
           == ReceiveStatus::Received);
    assert(datagram.bytes == ordinary);
}

// ---------------------------------------------------------------------------
// The instrument this layer exists to be
// ---------------------------------------------------------------------------

void
TestWhatCameOffTheSocketIsWhatACaptureKeeps()
{
    // This is the claim the whole change is for. There is no decoder and no
    // corpus, and the corpus cannot arrive until something can record one —
    // so what has to hold first is that the recording path preserves bytes and
    // arrival order exactly. Anything a decoder later concludes from a
    // committed fixture is only worth what this test is worth.
    UdpReceiver receiver;
    assert(receiver.Open(LoopbackConfig()));

    LoopbackSender sender;
    assert(sender.Open(receiver.GetBoundEndpoint()));

    // Deliberately awkward: a zero-length datagram, a payload whose bytes span
    // the printable range and out of it, and one that is not a multiple of the
    // sixteen bytes a hex line carries.
    const std::vector<std::vector<std::uint8_t>> sent = {
        std::vector<std::uint8_t>(),
        Payload(1, 0x00),
        Payload(16, 0x20),
        Payload(37, 0x7d),
        Payload(256, 0x00),
    };

    PacketCapture capture;
    capture.sender = "example.synthetic";
    capture.device = "example.synthetic";
    capture.sourceId = "loopback-01";
    capture.listenEndpoint = receiver.GetBoundEndpoint();

    ReceivedDatagram datagram;
    for (const std::vector<std::uint8_t>& bytes : sent) {
        assert(sender.Send(bytes));
        assert(receiver.Receive(&datagram, kLoopbackTimeout)
               == ReceiveStatus::Received);

        // What a recording tool does, and all it does: copy the bytes and the
        // instant across, and keep the peer for its own diagnosis. The capture
        // record has no peer field because a capture names one in its header.
        RecordedDatagram recorded;
        recorded.receiveTime = datagram.receiveTime;
        recorded.bytes = datagram.bytes;
        capture.datagrams.push_back(std::move(recorded));
        capture.peerEndpoint = datagram.peer;
    }

    // The format forbids a receive time that goes backwards, and the receiver's
    // monotonic clock is what makes that a guarantee rather than a hope. A wall
    // clock would satisfy this assertion on almost every run and fail it on the
    // one where NTP stepped mid-session.
    for (std::size_t index = 1; index != capture.datagrams.size(); ++index) {
        assert(capture.datagrams[index].receiveTime
               >= capture.datagrams[index - 1].receiveTime);
    }

    std::ostringstream written;
    assert(vrmAdapterMocopi::WritePacketCapture(written, capture));

    PacketCapture reread;
    std::istringstream input(written.str());
    vrmAdapterMocopi::PacketCaptureError error;
    if (!vrmAdapterMocopi::ReadPacketCapture(input, &reread, &error)) {
        std::fprintf(stderr, "line %zu: %s\n", error.line,
                     error.message.c_str());
        assert(false);
    }

    assert(reread.datagrams.size() == sent.size());
    for (std::size_t index = 0; index != sent.size(); ++index) {
        assert(reread.datagrams[index].bytes == sent[index]);
    }
    assert(reread.listenEndpoint == capture.listenEndpoint);
    assert(reread.peerEndpoint == capture.peerEndpoint);

    // And re-emitting reproduces the file byte for byte, which is what will let
    // a committed capture be compared rather than merely parsed.
    std::ostringstream again;
    assert(vrmAdapterMocopi::WritePacketCapture(again, reread));
    assert(again.str() == written.str());
}

void
StartSockets()
{
#if defined(_WIN32)
    // The library starts Winsock at its first `Open`, but the test sender is
    // not the library's and may run first.
    WSADATA data;
    WSAStartup(MAKEWORD(2, 2), &data);
#endif
}

} // namespace

int
main()
{
    StartSockets();

    TestAnAddressThatIsNotOneIsRefusedBeforeTheNetworkIsTouched();
    TestAPortAlreadyServedIsRefusedRatherThanQuietlyShared();
    TestASocketReportsWhereItLandedAndWhoCanReachIt();
    TestTheReceiveBufferIsReportedAsGrantedRatherThanAsAsked();
    TestAReceiverCountsFromItselfBeforeItIsEverOpened();
    TestAQuietSocketIsIdleAndAClosedOneSaysSo();
    TestADatagramArrivesWholeWithItsSenderAndItsInstant();
    TestSilenceIsNotReportedUntilACallerSaysHowMuchIsTooMuch();
    TestSilenceIsReportedOncePerEpisodeAndRearmedByADatagram();
    TestSilenceIsCountedEvenWhenNobodyAskedForTheDiagnostic();
    TestAReopenedReceiverIsNotStillWaitingForTheLastSessionsSource();
    TestANewCountingWindowCanStillSeeAnOngoingSilence();
    TestAnOverlongDatagramIsDroppedRatherThanHandedBackAsWhole();
    TestWhatCameOffTheSocketIsWhatACaptureKeeps();
    std::puts("vrmAdapterMocopi udp receiver tests passed");
    return 0;
}
