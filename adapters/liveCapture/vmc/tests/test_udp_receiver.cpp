// SPDX-License-Identifier: Apache-2.0
//
// The socket, and the hand-off that keeps it off the decoder's thread.
//
// Every test here opens a real socket, on loopback and on an OS-assigned port —
// never 39539, because a developer with a real sender running would otherwise
// find this suite fighting it for the port. That is also why these are their own
// CTest names: they are the only tests in this adapter that a runner forbidding
// sockets would have to exclude, and excluding a name is cheaper than excluding
// a claim (roadmap §9.5).
//
// The sender is in this file rather than in the library. §9.3 asks for a "test
// sender", and that is what it is: the adapter receives, and a `UdpSender` in
// `vrmAdapterVmc` would be a class no consumer of the adapter has a use for.
//
// Corpus mode makes the claim the whole layer is for: every committed capture,
// replayed *through a socket*, produces exactly the poses the same bytes produce
// when read from the file — and the arrival clock is the only thing the wire is
// allowed to have changed.
#include "vrmAdapterVmc/UdpReceiver.h"

#include "vrmAdapterVmc/Diagnostics.h"
#include "vrmAdapterVmc/FrameAssembler.h"
#include "vrmAdapterVmc/LiveSource.h"
#include "vrmAdapterVmc/PacketCapture.h"

#include "motionCore/Humanoid.h"

#include <bitset>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <set>
#include <string>
#include <thread>
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

using vrmAdapterVmc::DatagramQueue;
using vrmAdapterVmc::DatagramQueueConfig;
using vrmAdapterVmc::Diagnostic;
using vrmAdapterVmc::DiagnosticCode;
using vrmAdapterVmc::DiagnosticSeverity;
using vrmAdapterVmc::ReceivedDatagram;
using vrmAdapterVmc::ReceiveStatus;
using vrmAdapterVmc::UdpReceiver;
using vrmAdapterVmc::UdpReceiverConfig;
using vrmAdapterVmc::VmcFrame;
using vrmAdapterVmc::VmcLiveSource;

// Long enough that a loopback datagram which has been handed to the kernel is
// certainly readable, short enough that a genuinely lost one fails the suite
// rather than hanging it. Nothing here sleeps: every wait is on a datagram that
// has already been sent.
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
    // VMC port here would make the suite fail on any machine already receiving.
    config.listenPort = 0;
    return config;
}

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

    // The one transport code the frozen set has, and the one severity a session
    // cannot continue past.
    assert(diagnostics.size() == 1);
    assert(diagnostics[0].code == DiagnosticCode::SocketBindFailed);
    assert(diagnostics[0].severity == DiagnosticSeverity::Error);
    assert(!diagnostics[0].recoverable);
    // The endpoint that was asked for, so a reader knows which configuration
    // line to go and change.
    assert(diagnostics[0].subject == "sender.example.com:0");
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

    UdpReceiverConfig config = LoopbackConfig();
    config.listenPort = static_cast<std::uint16_t>(std::stoi(port));

    UdpReceiver second;
    std::vector<Diagnostic> diagnostics;
    assert(!second.Open(config, &diagnostics));
    assert(diagnostics.size() == 1);
    assert(diagnostics[0].code == DiagnosticCode::SocketBindFailed);
    // Two receivers splitting one sender's traffic is the failure an operator
    // has no way to see from the outside, which is why `reuseAddress` is off by
    // default and has to be asked for.
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
    // The commonest cause of a session that receives nothing from a sender that
    // is demonstrably sending.
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
    // Whatever the platform's default is, it is a real number and this class
    // knows it. A caller that cannot see one cannot diagnose the loss that
    // happens when its tick is slower than its sender.
    assert(defaulted.GetReceiveBufferBytes() > 0);

    UdpReceiverConfig config = LoopbackConfig();
    config.receiveBufferBytes = 1024u * 1024u;
    UdpReceiver asked;
    assert(asked.Open(config));

    // Deliberately *not* asserted equal to the request: every platform may clamp
    // it and Linux reports double what it was given. What is asserted is that
    // asking moved it and that the answer is read back from the socket, which is
    // the whole point — silently getting the default and then losing datagrams
    // is the hardest failure here to see from the outside.
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
    // An empty poll is not a failure at any timeout.
    assert(receiver.GetLastErrorText().empty());
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

    // A zero-length datagram is receivable, and is the smallest thing the
    // decoder above has to refuse without crashing (PacketCapture.h). A
    // receiver that reported it as `Idle` would hide it from that decoder.
    assert(sender.Send(std::vector<std::uint8_t>()));
    assert(receiver.Receive(&datagram, kLoopbackTimeout)
           == ReceiveStatus::Received);
    assert(datagram.bytes.empty());

    const vrmAdapterVmc::UdpReceiverStats& stats = receiver.GetStats();
    assert(stats.datagramsReceived == 4);
    assert(stats.bytesReceived == small.size() * 2 + large.size());
    assert(stats.datagramsTruncated == 0);
    assert(stats.lastPeer == datagram.peer);
    assert(stats.firstReceiveTime <= stats.lastReceiveTime);
    assert(receiver.Now() >= stats.lastReceiveTime);
}

// ---------------------------------------------------------------------------
// The hand-off
// ---------------------------------------------------------------------------

ReceivedDatagram
Queued(std::uint8_t seed, std::size_t size = 8)
{
    ReceivedDatagram datagram;
    datagram.bytes = Payload(size, seed);
    datagram.receiveTime = static_cast<double>(seed) / 30.0;
    datagram.peer = "127.0.0.1:1";
    return datagram;
}

void
TestTheQueueCarriesEveryDatagramAcrossAThreadInOrder()
{
    // `static` so the lambda below can read it without capturing it. A plain
    // function-local `constexpr` is not odr-used by `index != kCount` and needs
    // no capture by the standard, but MSVC 14.34 refuses it as C3493 where
    // 14.4x and both other compilers accept it — and a capture list is not the
    // fix, since adding one warns on clang instead.
    static constexpr std::size_t kCount = 500;
    DatagramQueueConfig config;
    // Comfortably above what is pushed, so the queue cannot overflow however the
    // two threads interleave. Sizing it at exactly `kCount` would also never
    // drop — but a later edit raising the count would then *hang* this test on
    // the drain loop below rather than failing it, and a hang is the one
    // outcome a suite cannot report.
    config.maxDatagrams = kCount * 4;

    DatagramQueue queue(config);

    // A real second thread, because the claim is about two threads. It pushes
    // as fast as it can while this one drains, which is the arrangement the
    // header describes and the only one in this adapter that needs a lock.
    std::thread network([&queue]() {
        for (std::size_t index = 0; index != kCount; ++index) {
            ReceivedDatagram datagram;
            datagram.bytes = Payload(4, static_cast<std::uint8_t>(index));
            datagram.receiveTime = static_cast<double>(index);
            queue.Push(std::move(datagram));
        }
    });

    std::vector<ReceivedDatagram> drained;
    while (drained.size() != kCount) {
        queue.Drain(&drained);
    }
    network.join();

    // Nothing lost, nothing duplicated, nothing reordered: a frame assembler
    // downstream of this reads arrival order as the protocol's, so a queue that
    // shuffled would be a frame-boundary defect wearing a threading costume.
    for (std::size_t index = 0; index != kCount; ++index) {
        assert(drained[index].receiveTime == static_cast<double>(index));
    }
    assert(queue.GetSize() == 0);
    assert(queue.GetStats().pushed == kCount);
    assert(queue.GetStats().drained == kCount);
    assert(queue.GetStats().dropped == 0);
    assert(queue.GetStats().highWaterMark <= kCount);
    assert(queue.GetStats().highWaterMark >= 1);
}

void
TestAFullQueueDropsTheOldestAndCountsIt()
{
    DatagramQueueConfig config;
    config.maxDatagrams = 4;

    DatagramQueue queue(config);
    for (std::uint8_t seed = 0; seed != 6; ++seed) {
        const bool clean = queue.Push(Queued(seed));
        // The first four fit; the last two each displace one, and the push says
        // so, so a recorder can log the moment it began losing traffic.
        assert(clean == (seed < 4));
    }

    std::vector<ReceivedDatagram> drained;
    assert(queue.Drain(&drained) == 4);
    // The newest four. For live motion the alternative is indefensible: holding
    // a stale frame and refusing a fresh one adds latency the session never
    // gets back.
    assert(drained.front().receiveTime == 2.0 / 30.0);
    assert(drained.back().receiveTime == 5.0 / 30.0);
    assert(queue.GetStats().dropped == 2);
    assert(queue.GetStats().highWaterMark == 4);
}

void
TestTheQueueIsBoundedByBytesAsWellAsByCount()
{
    DatagramQueueConfig config;
    config.maxDatagrams = 1000;
    config.maxBytes = 2048;

    DatagramQueue queue(config);
    for (std::uint8_t seed = 0; seed != 8; ++seed) {
        queue.Push(Queued(seed, 1024));
    }

    // A flood of maximum-sized datagrams exhausts memory long before it
    // exhausts a count, which is why there are two limits and not one.
    assert(queue.GetSize() == 2);
    assert(queue.GetStats().dropped == 6);

    queue.Clear();
    assert(queue.GetSize() == 0);
    // Clearing is not draining: the tally describes the session, not the queue.
    assert(queue.GetStats().pushed == 8);
}

void
TestAQueueAlwaysHoldsTheDatagramItWasLastGiven()
{
    DatagramQueueConfig config;
    config.maxBytes = 512;

    DatagramQueue queue(config);
    queue.Push(Queued(0, 256));
    // Larger on its own than the whole bound. It displaces everything older and
    // is then kept anyway, because refusing the newest datagram is the one thing
    // this queue must never do — the same reason overflow drops the oldest. So
    // `maxBytes` bounds what accumulates, not what a single push may cost.
    queue.Push(Queued(1, 4096));

    assert(queue.GetSize() == 1);
    assert(queue.GetStats().dropped == 1);

    std::vector<ReceivedDatagram> drained;
    assert(queue.Drain(&drained) == 1);
    assert(drained.front().bytes.size() == 4096);
}

// ---------------------------------------------------------------------------
// Corpus: the socket changes nothing but the arrival clock
// ---------------------------------------------------------------------------

struct Delivered
{
    motion::HumanoidPose pose;
    bool beginsNewSession = false;
    bool timestampFromSender = false;
    std::bitset<motion::HumanBoneCount> missing;
    std::bitset<motion::HumanBoneCount> stale;
    std::size_t duplicateBones = 0;
};

void
Collect(const VmcLiveSource& source, std::vector<Delivered>* out)
{
    for (const VmcFrame& frame : source.GetFramesFromLastPush()) {
        Delivered delivered;
        delivered.pose = frame.pose;
        delivered.beginsNewSession = frame.beginsNewSession;
        delivered.timestampFromSender = frame.timestampFromSender;
        delivered.missing = frame.missing;
        delivered.stale = frame.stale;
        delivered.duplicateBones = frame.duplicateBones;
        out->push_back(std::move(delivered));
    }
}

// Replays a capture straight from the file, which is what every other corpus
// test in this adapter does.
std::vector<Delivered>
ReplayFromFile(const vrmAdapterVmc::PacketCapture& capture,
               std::vector<DiagnosticCode>* codes)
{
    VmcLiveSource source;
    source.SetSource("file");

    std::vector<Delivered> delivered;
    std::vector<Diagnostic> diagnostics;
    for (const vrmAdapterVmc::RecordedDatagram& datagram : capture.datagrams) {
        source.PushDatagram(datagram.bytes, datagram.receiveTime, &diagnostics);
        Collect(source, &delivered);
    }
    source.Flush(&diagnostics);
    Collect(source, &delivered);

    for (const Diagnostic& diagnostic : diagnostics) {
        codes->push_back(diagnostic.code);
    }
    return delivered;
}

// The same capture through a real socket, one datagram at a time and one buffer
// throughout. Sending the whole capture first and draining afterwards would put
// the loss behaviour of a kernel receive buffer inside the assertion, which is
// not what this test is about.
bool
ReplayFromWire(const vrmAdapterVmc::PacketCapture& capture,
               std::vector<Delivered>* delivered,
               std::vector<DiagnosticCode>* codes)
{
    UdpReceiver receiver;
    if (!receiver.Open(LoopbackConfig())) {
        std::fprintf(stderr, "could not bind loopback: %s\n",
                     receiver.GetLastErrorText().c_str());
        return false;
    }
    LoopbackSender sender;
    if (!sender.Open(receiver.GetBoundEndpoint())) {
        std::fprintf(stderr, "could not open the test sender\n");
        return false;
    }

    VmcLiveSource source;
    source.SetSource(receiver.GetBoundEndpoint());

    std::vector<Diagnostic> diagnostics;
    // One buffer for the whole session, reused by every `Receive` — which is
    // the shape the bridge says a receiver should have, and is checked here by
    // the poses coming out identical rather than by an assertion about bytes.
    ReceivedDatagram received;
    for (const vrmAdapterVmc::RecordedDatagram& datagram : capture.datagrams) {
        if (!sender.Send(datagram.bytes)) {
            std::fprintf(stderr, "the test sender could not send %zu bytes\n",
                         datagram.bytes.size());
            return false;
        }
        if (receiver.Receive(&received, kLoopbackTimeout)
            != ReceiveStatus::Received) {
            std::fprintf(stderr, "a loopback datagram never arrived\n");
            return false;
        }
        source.PushDatagram(received.bytes, received.receiveTime,
                            &diagnostics);
        Collect(source, delivered);
    }
    source.Flush(&diagnostics);
    Collect(source, delivered);

    for (const Diagnostic& diagnostic : diagnostics) {
        codes->push_back(diagnostic.code);
    }
    return receiver.GetStats().datagramsReceived == capture.datagrams.size();
}

int
CheckTheWireChangesNothing(const std::filesystem::path& path)
{
    const std::string name = path.filename().string();

    vrmAdapterVmc::PacketCapture capture;
    vrmAdapterVmc::PacketCaptureError error;
    if (!ReadPacketCaptureFile(path.string(), &capture, &error)) {
        std::fprintf(stderr, "%s:%zu: %s\n", name.c_str(), error.line,
                     error.message.c_str());
        return 1;
    }

    std::vector<DiagnosticCode> fileCodes;
    const std::vector<Delivered> fromFile = ReplayFromFile(capture, &fileCodes);

    std::vector<Delivered> fromWire;
    std::vector<DiagnosticCode> wireCodes;
    if (!ReplayFromWire(capture, &fromWire, &wireCodes)) {
        std::fprintf(stderr, "%s: the loopback replay did not complete\n",
                     name.c_str());
        return 1;
    }

    int failures = 0;
    if (fromFile.size() != fromWire.size()) {
        std::fprintf(stderr,
                     "%s: %zu pose(s) from the file, %zu from the wire\n",
                     name.c_str(), fromFile.size(), fromWire.size());
        return 1;
    }
    if (fileCodes != wireCodes) {
        std::fprintf(stderr,
                     "%s: the wire produced a different diagnostic sequence\n",
                     name.c_str());
        ++failures;
    }

    for (std::size_t index = 0; index != fromFile.size(); ++index) {
        const Delivered& file = fromFile[index];
        const Delivered& wire = fromWire[index];

        if (file.timestampFromSender != wire.timestampFromSender
            || file.beginsNewSession != wire.beginsNewSession
            || file.missing != wire.missing || file.stale != wire.stale
            || file.duplicateBones != wire.duplicateBones) {
            std::fprintf(stderr, "%s: frame %zu differs in what it reports\n",
                         name.c_str(), index);
            ++failures;
            continue;
        }

        // The arrival clock is the one thing a socket legitimately changes: a
        // recorded receive time is the recorder's, and a live one is this
        // session's. It reaches a pose only when the sender sent no
        // `/VMC/Ext/T`, so the comparison is exact everywhere else — the whole
        // point of `operator==` being *is this the same recorded value*
        // (MOTION_CONTRACT.md), which is a stronger claim than `NearlyEqual`
        // and the right one here, because both paths decoded the same bytes.
        motion::HumanoidPose expected = file.pose;
        if (!file.timestampFromSender) {
            expected.timestamp = wire.pose.timestamp;
        }
        if (!(expected == wire.pose)) {
            std::fprintf(stderr,
                         "%s: frame %zu is not the pose the file produced\n",
                         name.c_str(), index);
            ++failures;
        }
    }

    if (failures != 0) {
        return 1;
    }
    std::printf("%s: %zu datagram(s) over a socket, %zu identical pose(s)\n",
                name.c_str(), capture.datagrams.size(), fromWire.size());
    return 0;
}

int
CheckCorpus(const std::filesystem::path& directory)
{
    std::set<std::filesystem::path> captures;
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(directory)) {
        if (entry.is_regular_file()
            && entry.path().extension() == ".vmcpackets") {
            captures.insert(entry.path());
        }
    }
    if (captures.empty()) {
        std::fprintf(stderr, "no captures in %s\n",
                     directory.string().c_str());
        return 1;
    }

    int failures = 0;
    for (const std::filesystem::path& path : captures) {
        failures += CheckTheWireChangesNothing(path);
    }
    if (failures != 0) {
        std::fprintf(stderr, "%d capture(s) failed the loopback replay\n",
                     failures);
        return 1;
    }
    std::printf("VMC loopback: %zu capture(s) replayed through a socket, every "
                "pose identical to the file's\n",
                captures.size());
    return 0;
}

void
TestAReopenedReceiverCountsTheNewSessionAndNotTheLastOne()
{
    // `Open` restarts `_epoch`, so every time in the stats is measured from the
    // new session. If the counters carried over, `datagramsReceived` would span
    // two sessions while `firstReceiveTime` described one -- a report whose two
    // halves disagree, with nothing in it saying so.
    UdpReceiver receiver;
    assert(receiver.Open(LoopbackConfig()));

    LoopbackSender sender;
    assert(sender.Open(receiver.GetBoundEndpoint()));
    const std::vector<std::uint8_t> payload = Payload(16, 0x20);
    assert(sender.Send(payload));

    ReceivedDatagram datagram;
    assert(receiver.Receive(&datagram, kLoopbackTimeout)
           == ReceiveStatus::Received);
    assert(receiver.GetStats().datagramsReceived == 1);
    assert(receiver.GetStats().bytesReceived == payload.size());

    // A second session on the same object, which is what a caller that lost a
    // sender and re-bound has.
    receiver.Close();
    assert(receiver.Open(LoopbackConfig()));
    const vrmAdapterVmc::UdpReceiverStats& stats = receiver.GetStats();
    assert(stats.datagramsReceived == 0);
    assert(stats.bytesReceived == 0);
    assert(stats.idleReceives == 0);
    assert(stats.datagramsTruncated == 0);
    assert(stats.receiveErrors == 0);
    assert(stats.firstReceiveTime == 0.0);
    assert(stats.lastReceiveTime == 0.0);
    assert(stats.lastPeer.empty());

    // The receiver still works, and the new session's first datagram is its
    // first -- not its second.
    LoopbackSender second;
    assert(second.Open(receiver.GetBoundEndpoint()));
    assert(second.Send(payload));
    assert(receiver.Receive(&datagram, kLoopbackTimeout)
           == ReceiveStatus::Received);
    assert(stats.datagramsReceived == 1);
}

// ---------------------------------------------------------------------------
// The over-long datagram
// ---------------------------------------------------------------------------

// Split off behind an argument, with `SKIP_RETURN_CODE` on its own CTest name,
// because it is the one case here that may legitimately not run: reaching it
// needs IPv6, the only transport that can carry more than the IPv4 payload
// maximum this adapter bounds itself by. Inside the suite that would have been
// invisible -- ctest hides a passing test's output, so a lane that checked the
// claim and a lane that quietly declined to would print the same line.
constexpr int kSkipExitCode = 77;

int
CheckAnOverlongDatagramIsDroppedRatherThanHandedBackAsWhole()
{
    // The defect this pins fails *quietly and plausibly*, which is what makes
    // it the worst of the four: POSIX truncates a too-long datagram and reports
    // the buffer's length, and that is indistinguishable from a datagram which
    // happened to be exactly that long. A receiver whose buffer was the bound
    // itself would hand the half-read one to the decoder, which would refuse it
    // as `VRM_VMC_PACKET_MALFORMED` -- this adapter blaming a sender for its own
    // truncation, in the one diagnostic an operator has no way to check.
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
        Payload(vrmAdapterVmc::MaxDatagramBytes + 8, 0x00);
    if (!sender.Send(overlong)) {
        std::puts("skipped: this host will not send an over-long datagram");
        return kSkipExitCode;
    }

    ReceivedDatagram datagram;
    const ReceiveStatus status = receiver.Receive(&datagram, 0.25);

    // Dropped, counted, and never presented as a datagram -- on both platforms
    // and by two different mechanisms: Windows says WSAEMSGSIZE, POSIX says
    // nothing and is caught by the buffer's one spare byte.
    //
    // Which means **this test cannot fail on Windows for the defect it was
    // written about**: WSAEMSGSIZE catches it there with or without the spare
    // byte. The lane that proves the fix is a POSIX one, where without it
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

    // The fourth defect rides on the same case, because this is the only path
    // in the class a test can reach it through: the call that drops an
    // over-long datagram returns `Idle`, and it must not be *counted* as idle.
    // It met something.
    //
    // The zero timeout is what isolates the claim. With a waiting one the drop
    // is followed by another poll, and that poll's timeout is a genuine idle
    // receive -- so the count would be 1 either way and the assertion would say
    // nothing. At zero, `spend()` finds no budget left and the call returns
    // straight from the drop, which is the one call whose accounting is in
    // question. `ResetStats` immediately before it makes the window exactly
    // that call.
    assert(sender.Send(overlong));
    bool dropped = false;
    for (int attempt = 0; attempt != 200 && !dropped; ++attempt) {
        receiver.ResetStats();
        receiver.Receive(&datagram, 0.0);
        dropped = receiver.GetStats().datagramsTruncated == 1;
        if (!dropped) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    assert(dropped);
    assert(receiver.GetStats().idleReceives == 0);

    std::puts("an over-long datagram was dropped rather than recorded, and the "
              "call that dropped it was not counted as an idle one");
    return 0;
}

void
StartSockets()
{
#if defined(_WIN32)
    // The library starts Winsock at its first `Open`, but the test sender is
    // not the library's and may be built first.
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
    // other test binary in this adapter already follows.
    if (argc > 1) {
        return CheckCorpus(std::filesystem::path(argv[1]));
    }

    TestAnAddressThatIsNotOneIsRefusedBeforeTheNetworkIsTouched();
    TestAPortAlreadyServedIsRefusedRatherThanQuietlyShared();
    TestASocketReportsWhereItLandedAndWhoCanReachIt();
    TestTheReceiveBufferIsReportedAsGrantedRatherThanAsAsked();
    TestAReceiverCountsFromItselfBeforeItIsEverOpened();
    TestAQuietSocketIsIdleAndAClosedOneSaysSo();
    TestADatagramArrivesWholeWithItsSenderAndItsInstant();
    TestAReopenedReceiverCountsTheNewSessionAndNotTheLastOne();
    TestTheQueueCarriesEveryDatagramAcrossAThreadInOrder();
    TestAFullQueueDropsTheOldestAndCountsIt();
    TestTheQueueIsBoundedByBytesAsWellAsByCount();
    TestAQueueAlwaysHoldsTheDatagramItWasLastGiven();
    std::puts("vrmAdapterVmc udp receiver tests passed");
    return 0;
}
