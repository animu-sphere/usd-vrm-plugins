// SPDX-License-Identifier: Apache-2.0
//
// The socket, before anything decodes what comes out of it.
//
// Every test here opens a real socket, on loopback and on an OS-assigned port —
// never 9000, because that is the port VRChat itself binds and a developer with
// the application running would otherwise find this suite fighting it. That is
// also why these are their own CTest names: excluding a name is cheaper than
// excluding a claim (adapter plan §9.5).
//
// This binary carries **three** such names, and a runner forbidding sockets must
// exclude all three: `vrmAdapterVrchatOsc_udpReceiver`,
// `vrmAdapterVrchatOsc_udpReceiverTruncation`, and
// `vrmAdapterVrchatOsc_loopbackCorpus` — the corpus replay at the bottom of this
// file, which binds a loopback socket like everything else here. It is listed in
// CMake beside a corpus pass that binds nothing, so it is the one an operator
// building an exclusion list would miss.
//
// The sender is in this file rather than in the library. The adapter receives,
// and a `UdpSender` in `vrmAdapterVrchatOsc` would be a class no consumer has a
// use for — and, worse, the first half of an outbound path §12 excludes on
// purpose.
//
// **Nothing here asserts anything about the wire format.** The payloads below
// are counting patterns that are not claimed to be packets. That restraint reads
// as obvious for a protocol nobody has measured, and this is not one: VRChat's
// OSC surface is published, so a plausible payload could be written here from a
// document. §6 is why it is not — what a *sender* puts on the wire is a
// measurement, and a specification does not establish that any sender implements
// all of it or only it. A payload written from the specification and asserted
// here would be an assumption wearing the appearance of evidence, in the one
// place a later reader would trust it most.
//
// What is asserted is that whatever a source sends arrives whole, in order, with
// the instant it arrived — and that it survives the trip to a capture file
// unchanged. That is the whole of VRC-0's done-condition, and everything a
// decoder later concludes from a committed fixture is worth exactly what these
// two claims are worth.
//
// ## The one pass that reads a corpus, and what it can check without a decoder
//
// Given a corpus directory this binary runs a different check
// (`vrmAdapterVrchatOsc_loopbackCorpus`): every committed capture is replayed
// **through a real socket** and compared, datagram for datagram, against the
// same file read straight off disk. The siblings' equivalent passes compare
// poses, because they have decoders; this one compares bytes, which is the
// strongest claim available at this milestone and is also the exact claim VRC-0
// is asked for — a capture replays deterministically.
#include "vrmAdapterVrchatOsc/UdpReceiver.h"

#include "vrmAdapterVrchatOsc/Diagnostics.h"
#include "vrmAdapterVrchatOsc/PacketCapture.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
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

using vrmAdapterVrchatOsc::Diagnostic;
using vrmAdapterVrchatOsc::DiagnosticCode;
using vrmAdapterVrchatOsc::PacketCapture;
using vrmAdapterVrchatOsc::ReceivedDatagram;
using vrmAdapterVrchatOsc::ReceiveStatus;
using vrmAdapterVrchatOsc::RecordedDatagram;
using vrmAdapterVrchatOsc::UdpReceiver;
using vrmAdapterVrchatOsc::UdpReceiverConfig;

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
    // and on this adapter the default is the port a running VRChat holds.
    config.listenPort = 0;
    return config;
}

// A counting pattern, and not a packet. See the file header: a payload written
// from the published specification would put an assumption where a measurement
// belongs.
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
    config.listenAddress = "sender.example.com";
    config.listenPort = 0;

    std::vector<Diagnostic> diagnostics;
    assert(!receiver.Open(config, &diagnostics));
    assert(!receiver.IsOpen());
    assert(diagnostics.size() == 1);
    assert(diagnostics[0].code == DiagnosticCode::SocketBindFailed);
    assert(!diagnostics[0].recoverable);
    // The requested endpoint, so a config file's mistake is legible without
    // reading the config file.
    assert(diagnostics[0].subject == "sender.example.com:0");
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
    // from the outside at all. It is refused instead — and this adapter is the
    // one where it is most likely to happen, because the port it defaults to is
    // the port the application it stands in for binds.
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
    // A fact, not a verdict — unlike the mocopi adapter's equivalent assertion.
    // A sender on this machine is a legitimate configuration for this protocol,
    // so loopback here is narrow rather than useless (UdpReceiver.h).
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
TestTheDefaultPortIsTheOneTheApplicationDocuments()
{
    // Not a socket test: it binds nothing. It pins the one number this adapter
    // adds to a transport that has no default port of its own, because a port
    // is a protocol's property and `liveTransport` knows no protocol.
    //
    // 9001 is deliberately not here. That is the port VRChat *sends* from, and
    // an adapter that defaulted to it would be listening for the half of the
    // conversation §12 excludes.
    const UdpReceiverConfig config;
    assert(config.listenPort == 9000);
    assert(vrmAdapterVrchatOsc::DefaultVrchatOscPort == 9000);
    assert(config.listenAddress == "0.0.0.0");
    // Off by default, so the refusal above happens rather than a silent split.
    assert(!config.reuseAddress);
    // No threshold by default: how long a sender may take to start is a
    // property of the session, not of the socket.
    assert(config.silenceTimeoutSeconds == 0.0);
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
    assert(datagram.peer.rfind("127.0.0.1:", 0) == 0);
    assert(datagram.receiveTime >= before);

    // Bigger than the first, into the same buffer: `Receive` resizes rather than
    // leaving the previous datagram's tail behind.
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
    // decoder has to refuse without crashing. A receiver that reported it as
    // `Idle` would hide it from the decoder that does not exist yet — which is
    // exactly the sort of hole a corpus recorded today would carry into it.
    assert(sender.Send(std::vector<std::uint8_t>()));
    assert(receiver.Receive(&datagram, kLoopbackTimeout)
           == ReceiveStatus::Received);
    assert(datagram.bytes.empty());

    const vrmAdapterVrchatOsc::UdpReceiverStats& stats = receiver.GetStats();
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
// above the 0.05 the silence test configures, so a loaded CI runner does not
// make it flaky, and far below any timeout a test runner enforces.
constexpr double kSilenceGiveUp = 2.0;

// Polls until the receiver has reported `expected` silence episodes, and gives
// up rather than waiting forever. The bound is the difference between a defect
// that fails this suite and one that hangs it.
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
    UdpReceiver receiver;
    assert(receiver.Open(LoopbackConfig()));

    ReceivedDatagram datagram;
    std::vector<Diagnostic> diagnostics;
    for (int poll = 0; poll != 5; ++poll) {
        assert(receiver.Receive(&datagram, 0.02, &diagnostics)
               == ReceiveStatus::Idle);
    }
    assert(diagnostics.empty());
    assert(receiver.GetStats().silenceReports == 0);
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
    // Nothing has ever arrived, which is half of what this code covers: a sender
    // that has not been started.
    PollUntilSilenceReports(receiver, 1, &diagnostics);
    assert(diagnostics.size() == 1);
    assert(diagnostics[0].code == DiagnosticCode::SourceTimeout);
    // Recoverable, and that is load-bearing rather than decorative: a session
    // waiting for its sender to be started is an ordinary session.
    assert(diagnostics[0].recoverable);
    assert(diagnostics[0].source == receiver.GetBoundEndpoint());
    // No `timestamp`, deliberately: that field is the *source's* clock at a
    // frame, and this diagnostic exists precisely because no frame arrived to
    // carry one.
    assert(!diagnostics[0].timestamp.has_value());
    assert(diagnostics[0].detail.find("s ago") != std::string::npos);

    // Still quiet, still one report. A loop that noticed silence a hundred times
    // a second would fill a session log with the loop rather than the session.
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
    assert(diagnostics[1].code == DiagnosticCode::SourceTimeout);
    // The second episode is the one that can say a source *stopped*, where the
    // first could only say nothing had started.
    assert(diagnostics[0].detail != diagnostics[1].detail);
}

void
TestSilenceIsCountedEvenWhenNobodyAskedForTheDiagnostic()
{
    // The tally is what a session report reads, and it must not depend on
    // whether the loop that noticed had somewhere to put a message.
    UdpReceiverConfig config = LoopbackConfig();
    config.silenceTimeoutSeconds = 0.05;

    UdpReceiver receiver;
    assert(receiver.Open(config));
    PollUntilSilenceReports(receiver, 1, nullptr);
    assert(receiver.GetStats().silenceReports == 1);
}

// ---------------------------------------------------------------------------
// Truncation
// ---------------------------------------------------------------------------

// CTest's convention for "this could not be checked here", set as
// `SKIP_RETURN_CODE` on the test that runs this case. It is a separate CTest
// name for exactly that reason: a skip inside the main suite is invisible, since
// ctest hides a passing test's output.
constexpr int kSkipExitCode = 77;

int
CheckAnOverlongDatagramIsDroppedRatherThanHandedBackAsWhole()
{
    // The defect this pins is the worst one a recorder can have, because it
    // fails *quietly and plausibly*: POSIX truncates a too-long datagram and
    // reports the buffer's length, which is indistinguishable from a datagram
    // that happened to be exactly that long. A receiver whose buffer was the
    // bound itself would write a packet the source never sent into a fixture,
    // and every conclusion a decoder later drew from that fixture would be about
    // this repository's own mistake.
    //
    // This adapter never had that defect, because the fix landed in OSC-1 and
    // the class moved in OSC-2 — which is the point of extracting before the
    // third consumer rather than after. The name exists here anyway: what it
    // checks is that the shared receiver still behaves this way *as this adapter
    // reaches it*, and a third lane reporting the property is a third chance to
    // notice it going away.
    //
    // Reaching it needs IPv6, the one transport that can carry more than
    // `MaxDatagramBytes`, so this reports a skip rather than a pass where the
    // host has no IPv6 loopback or refuses to send a datagram that large.
    UdpReceiverConfig config;
    config.listenAddress = "::1";
    config.listenPort = 0;

    UdpReceiver receiver;
    if (!receiver.Open(config)) {
        std::puts("skipped: no IPv6 loopback on this host");
        return kSkipExitCode;
    }

    LoopbackSender sender;
    if (!sender.Open(receiver.GetBoundEndpoint())) {
        std::puts("skipped: could not reach the IPv6 loopback endpoint");
        return kSkipExitCode;
    }

    // Above the IPv4 bound the capture format enforces, below IPv6's own 65527
    // maximum. One byte over would do; a handful makes the intent legible.
    const std::vector<std::uint8_t> overlong =
        Payload(vrmAdapterVrchatOsc::MaxDatagramBytes + 8, 0x00);
    if (!sender.Send(overlong)) {
        std::puts("skipped: this host will not send an over-long datagram");
        return kSkipExitCode;
    }

    ReceivedDatagram datagram;
    const ReceiveStatus status = receiver.Receive(&datagram, 0.25);

    // Dropped, counted, and never presented as a datagram — on both platforms
    // and by two different mechanisms: Windows says WSAEMSGSIZE, POSIX says
    // nothing and is caught by the buffer's one spare byte. Which means **this
    // name cannot fail on Windows for the defect it is about**: the lane that
    // would prove it is a POSIX one. Worth knowing before reading a green
    // Windows run as evidence.
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

    std::puts("an over-long datagram was dropped rather than recorded");
    return 0;
}

// ---------------------------------------------------------------------------
// The claim this milestone exists to make
// ---------------------------------------------------------------------------

void
TestWhatCameOffTheSocketIsWhatACaptureKeeps()
{
    // VRC-0's done-condition, as an assertion: bytes off the socket and bytes in
    // the capture file are identical. There is no decoder and no corpus, and the
    // corpus cannot arrive until something can record one — so what has to hold
    // first is that the recording path preserves bytes and arrival order
    // exactly.
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
    assert(vrmAdapterVrchatOsc::WritePacketCapture(written, capture));

    PacketCapture reread;
    std::istringstream input(written.str());
    vrmAdapterVrchatOsc::PacketCaptureError error;
    if (!vrmAdapterVrchatOsc::ReadPacketCapture(input, &reread, &error)) {
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
    assert(vrmAdapterVrchatOsc::WritePacketCapture(again, reread));
    assert(again.str() == written.str());
}

// ---------------------------------------------------------------------------
// Corpus: the socket adds nothing and loses nothing
// ---------------------------------------------------------------------------

// Replays one capture through a real socket and compares what came back with
// what the file holds. Byte for byte and in order, because at this milestone
// bytes are the whole of what this adapter claims to preserve.
//
// The receive *times* are deliberately not compared: they are this replay's
// clock and not the recorded session's, and asserting them would be asserting
// that a loopback send takes as long as a Wi-Fi one did. What is compared is
// their **order**, which is the property the format enforces and the one a
// decoder will depend on.
bool
ReplayOneCapture(const std::filesystem::path& path)
{
    const std::string name = path.filename().string();
    PacketCapture fromFile;
    vrmAdapterVrchatOsc::PacketCaptureError error;
    if (!vrmAdapterVrchatOsc::ReadPacketCaptureFile(path.string(), &fromFile,
                                                    &error)) {
        std::fprintf(stderr, "%s:%zu: %s\n", name.c_str(), error.line,
                     error.message.c_str());
        return false;
    }

    UdpReceiver receiver;
    if (!receiver.Open(LoopbackConfig())) {
        std::fprintf(stderr, "%s: could not bind a loopback receiver\n",
                     name.c_str());
        return false;
    }
    LoopbackSender sender;
    if (!sender.Open(receiver.GetBoundEndpoint())) {
        std::fprintf(stderr, "%s: could not reach the loopback endpoint\n",
                     name.c_str());
        return false;
    }

    std::vector<std::vector<std::uint8_t>> received;
    received.reserve(fromFile.datagrams.size());
    double previous = -1.0;
    for (const RecordedDatagram& recorded : fromFile.datagrams) {
        // One at a time rather than a burst: a kernel receive buffer is finite,
        // and a corpus large enough to overflow it would fail this pass for a
        // reason that is not about the receiver.
        if (!sender.Send(recorded.bytes)) {
            std::fprintf(stderr, "%s: could not send a recorded datagram\n",
                         name.c_str());
            return false;
        }
        ReceivedDatagram live;
        if (receiver.Receive(&live, kLoopbackTimeout)
            != ReceiveStatus::Received) {
            std::fprintf(stderr, "%s: a recorded datagram did not arrive\n",
                         name.c_str());
            return false;
        }
        if (live.receiveTime < previous) {
            std::fprintf(stderr, "%s: arrival order was not preserved\n",
                         name.c_str());
            return false;
        }
        previous = live.receiveTime;
        received.push_back(live.bytes);
    }

    for (std::size_t index = 0; index != received.size(); ++index) {
        if (received[index] != fromFile.datagrams[index].bytes) {
            std::fprintf(stderr, "%s: datagram %zu differs after a round trip\n",
                         name.c_str(), index);
            return false;
        }
    }

    std::printf("%s: %zu datagram(s) replayed through a socket unchanged\n",
                name.c_str(), received.size());
    return true;
}

int
CheckCorpus(const std::filesystem::path& directory)
{
    if (!std::filesystem::is_directory(directory)) {
        std::fprintf(stderr, "corpus directory not found: %s\n",
                     directory.string().c_str());
        return 1;
    }

    std::vector<std::filesystem::path> captures;
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::recursive_directory_iterator(directory)) {
        if (entry.is_regular_file()
            && entry.path().extension() == ".vrchatoscpackets") {
            captures.push_back(entry.path());
        }
    }
    std::sort(captures.begin(), captures.end());

    if (captures.empty()) {
        std::fprintf(stderr, "no .vrchatoscpackets fixtures in %s\n",
                     directory.string().c_str());
        return 1;
    }

    int failures = 0;
    for (const std::filesystem::path& path : captures) {
        if (!ReplayOneCapture(path)) {
            ++failures;
        }
    }
    if (failures != 0) {
        std::fprintf(stderr, "%d capture(s) did not replay unchanged\n",
                     failures);
        return 1;
    }
    std::printf("VRChat OSC loopback: %zu capture(s) replayed through a "
                "socket, every datagram identical to the file's\n",
                captures.size());
    return 0;
}

void
StartSockets()
{
#if defined(_WIN32)
    // The library starts Winsock at its first `Open`, but the test sender is not
    // the library's and may run first.
    WSADATA data;
    WSAStartup(MAKEWORD(2, 2), &data);
#endif
}

} // namespace

int
main(int argc, char** argv)
{
    StartSockets();

    // One case is split off behind an argument because it is the one that may
    // legitimately not run here (see `kSkipExitCode`). Everything else is
    // unconditional.
    if (argc > 1 && std::string(argv[1]) == "truncation") {
        return CheckAnOverlongDatagramIsDroppedRatherThanHandedBackAsWhole();
    }
    // Any other argument is a corpus directory, which is the convention every
    // other test binary in this repository's adapters already follows.
    if (argc > 1) {
        return CheckCorpus(std::filesystem::path(argv[1]));
    }

    TestAnAddressThatIsNotOneIsRefusedBeforeTheNetworkIsTouched();
    TestAPortAlreadyServedIsRefusedRatherThanQuietlyShared();
    TestASocketReportsWhereItLandedAndWhoCanReachIt();
    TestTheDefaultPortIsTheOneTheApplicationDocuments();
    TestAQuietSocketIsIdleAndAClosedOneSaysSo();
    TestADatagramArrivesWholeWithItsSenderAndItsInstant();
    TestSilenceIsNotReportedUntilACallerSaysHowMuchIsTooMuch();
    TestSilenceIsReportedOncePerEpisodeAndRearmedByADatagram();
    TestSilenceIsCountedEvenWhenNobodyAskedForTheDiagnostic();
    TestWhatCameOffTheSocketIsWhatACaptureKeeps();
    std::puts("vrmAdapterVrchatOsc udp receiver tests passed");
    return 0;
}
