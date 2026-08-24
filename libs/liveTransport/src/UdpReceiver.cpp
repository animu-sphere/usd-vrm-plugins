// SPDX-License-Identifier: Apache-2.0
//
// The one file in this library that includes a platform header. `UdpReceiver.h`
// keeps its socket as an integer precisely so that Winsock's macros — which
// rename `min`, `max`, and a good deal else — reach nothing but this
// translation unit.

#include "liveTransport/UdpReceiver.h"

#include "PollTimeout.h"

#include "liveTransport/Diagnostics.h"
#include "liveTransport/PacketCapture.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <utility>

#if defined(_WIN32)
#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    include <winsock2.h>
// After winsock2.h, always: ws2tcpip.h needs its declarations and including the
// two the other way round pulls the winsock 1.1 header through windows.h.
#    include <ws2tcpip.h>
#else
#    include <arpa/inet.h>
#    include <cerrno>
#    include <cstring>
#    include <fcntl.h>
#    include <netdb.h>
#    include <netinet/in.h>
#    include <poll.h>
#    include <sys/socket.h>
#    include <sys/types.h>
#    include <unistd.h>
#endif

namespace liveTransport
{

namespace
{

#if defined(_WIN32)

using SocketHandle = SOCKET;
using PollDescriptor = WSAPOLLFD;
constexpr std::intptr_t kInvalidSocket = -1;

// Winsock has to be initialised per process and torn down as many times as it
// was started. A function-local static does both once, at the first `Open`,
// rather than making every consumer of this library remember to.
bool
EnsureSocketsUsable()
{
    struct Startup
    {
        Startup()
        {
            WSADATA data;
            ok = WSAStartup(MAKEWORD(2, 2), &data) == 0;
        }
        ~Startup()
        {
            if (ok) {
                WSACleanup();
            }
        }
        bool ok = false;
    };
    static Startup startup;
    return startup.ok;
}

int
LastSocketError()
{
    return WSAGetLastError();
}

std::string
SocketErrorText(int code)
{
    char* text = nullptr;
    const DWORD length = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM
            | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, static_cast<DWORD>(code),
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<char*>(&text), 0, nullptr);
    std::string message;
    if (length != 0 && text) {
        message.assign(text, length);
    }
    if (text) {
        LocalFree(text);
    }
    // FormatMessage ends its lines; a diagnostic is one line by contract.
    while (!message.empty()
           && (message.back() == '\n' || message.back() == '\r'
               || message.back() == ' ')) {
        message.pop_back();
    }
    if (message.empty()) {
        message = "socket error " + std::to_string(code);
    }
    return message;
}

void
CloseSocket(SocketHandle handle)
{
    closesocket(handle);
}

bool
SetNonBlocking(SocketHandle handle)
{
    u_long mode = 1;
    return ioctlsocket(handle, FIONBIO, &mode) == 0;
}

int
PollSockets(PollDescriptor* descriptors, int count, int timeoutMilliseconds)
{
    return WSAPoll(descriptors, static_cast<ULONG>(count),
                   timeoutMilliseconds);
}

bool
ErrorIsWouldBlock(int code)
{
    return code == WSAEWOULDBLOCK;
}

bool
ErrorIsInterrupted(int code)
{
    return code == WSAEINTR;
}

// Transient on a datagram socket: the call failed, this datagram is gone, and
// the next one is unaffected. WSAECONNRESET is the Winsock trap — a UDP socket
// reports it when an earlier datagram *it sent* drew an ICMP port-unreachable,
// which is a fact about a peer and not about this socket. Treating it as fatal
// is how a Windows receive loop dies of somebody else's absence.
bool
ErrorIsTransient(int code)
{
    return code == WSAECONNRESET || code == WSAENETRESET
           || code == WSAETIMEDOUT;
}

bool
ErrorIsTruncation(int code)
{
    return code == WSAEMSGSIZE;
}

#else

using SocketHandle = int;
using PollDescriptor = struct pollfd;
constexpr std::intptr_t kInvalidSocket = -1;

bool
EnsureSocketsUsable()
{
    return true;
}

int
LastSocketError()
{
    return errno;
}

std::string
SocketErrorText(int code)
{
    return std::strerror(code);
}

void
CloseSocket(SocketHandle handle)
{
    ::close(handle);
}

bool
SetNonBlocking(SocketHandle handle)
{
    const int flags = ::fcntl(handle, F_GETFL, 0);
    if (flags == -1) {
        return false;
    }
    return ::fcntl(handle, F_SETFL, flags | O_NONBLOCK) == 0;
}

int
PollSockets(PollDescriptor* descriptors, int count, int timeoutMilliseconds)
{
    return ::poll(descriptors, static_cast<nfds_t>(count),
                  timeoutMilliseconds);
}

bool
ErrorIsWouldBlock(int code)
{
    return code == EAGAIN || code == EWOULDBLOCK;
}

bool
ErrorIsInterrupted(int code)
{
    return code == EINTR;
}

// ECONNREFUSED is the POSIX spelling of the same ICMP report Winsock calls
// WSAECONNRESET, and arrives for the same reason.
bool
ErrorIsTransient(int code)
{
    return code == ECONNREFUSED || code == ENETUNREACH || code == EHOSTUNREACH;
}

bool
ErrorIsTruncation(int)
{
    // POSIX truncates silently rather than failing; the caller detects it from
    // the returned length instead.
    return false;
}

#endif

SocketHandle
ToHandle(std::intptr_t socket)
{
    return static_cast<SocketHandle>(socket);
}

// "host:port", with the brackets IPv6 needs to keep its colons apart from the
// port's.
std::string
FormatEndpoint(const sockaddr* address, std::size_t length)
{
    // Sized rather than `NI_MAXHOST` / `NI_MAXSERV`, which glibc declares only
    // under a feature-test macro this translation unit does not set. 46 is the
    // longest numeric IPv6 form and 5 the longest port; both buffers are far
    // above what `NI_NUMERICHOST | NI_NUMERICSERV` can produce.
    char host[64] = {};
    char service[16] = {};
    const int result = ::getnameinfo(
        address, static_cast<socklen_t>(length), host, sizeof(host), service,
        sizeof(service), NI_NUMERICHOST | NI_NUMERICSERV);
    if (result != 0) {
        return std::string();
    }
    std::string endpoint;
    if (address->sa_family == AF_INET6) {
        endpoint = "[" + std::string(host) + "]";
    } else {
        endpoint = host;
    }
    endpoint += ":";
    endpoint += service;
    return endpoint;
}

bool
AddressIsLoopbackOnly(const sockaddr* address)
{
    if (address->sa_family == AF_INET) {
        const auto* v4 = reinterpret_cast<const sockaddr_in*>(address);
        // 127.0.0.0/8, in host order.
        const std::uint32_t host = ntohl(v4->sin_addr.s_addr);
        return (host >> 24) == 127u;
    }
    if (address->sa_family == AF_INET6) {
        const auto* v6 = reinterpret_cast<const sockaddr_in6*>(address);
        if (IN6_IS_ADDR_LOOPBACK(&v6->sin6_addr) != 0) {
            return true;
        }
        // `::ffff:127.0.0.1` is the same unreachable-from-the-LAN address
        // wearing the other family's clothes, and a dual-stack socket is how a
        // caller meets it. Reporting that one as reachable would be exactly
        // backwards for the question this answers.
        if (IN6_IS_ADDR_V4MAPPED(&v6->sin6_addr) != 0) {
            // The last four bytes carry the IPv4 address; read them where they
            // are rather than through a member name that differs per platform.
            std::uint32_t mapped = 0;
            std::memcpy(&mapped,
                        reinterpret_cast<const std::uint8_t*>(&v6->sin6_addr)
                            + 12,
                        sizeof(mapped));
            return (ntohl(mapped) >> 24) == 127u;
        }
        return false;
    }
    return false;
}

std::int64_t
SteadyTicks()
{
    return std::chrono::steady_clock::now().time_since_epoch().count();
}

double
TicksToSeconds(std::int64_t ticks)
{
    using Period = std::chrono::steady_clock::period;
    return static_cast<double>(ticks) * static_cast<double>(Period::num)
           / static_cast<double>(Period::den);
}

// What the kernel says the receive buffer is, which is rarely what was asked
// for: every platform may clamp it and Linux reports double what was set. 0
// when the platform will not say, which is a fact rather than a failure — the
// socket works either way, and a caller reading 0 knows only that it cannot
// check.
std::size_t
ReadReceiveBuffer(SocketHandle handle)
{
    int granted = 0;
    socklen_t length = sizeof(granted);
    if (::getsockopt(handle, SOL_SOCKET, SO_RCVBUF,
                     reinterpret_cast<char*>(&granted), &length)
            != 0
        || granted < 0) {
        return 0;
    }
    return static_cast<std::size_t>(granted);
}

} // namespace

// `TimeoutToMilliseconds` and `ClassifyPollWakeUp` live in PollTimeout.h, where
// a unit test can reach them — the debt OSC-1 recorded and could not pay from
// inside a single adapter. `FormatSeconds` is Diagnostics.h's, so a silence
// report and the diagnostic line an adapter turns it into cannot disagree about
// how a duration is spelled.
using internal::ClassifyPollWakeUp;
using internal::PollWakeUp;
using internal::TimeoutToMilliseconds;

UdpReceiver::UdpReceiver()
    : _epoch(SteadyTicks())
{
}

UdpReceiver::~UdpReceiver()
{
    Close();
}

bool
UdpReceiver::Open(const UdpReceiverConfig& config,
                  std::vector<TransportEventReport>* events)
{
    Close();

    const std::string requested =
        config.listenAddress + ":" + std::to_string(config.listenPort);

    const auto fail = [&](const std::string& detail) {
        _lastError = detail;
        if (events) {
            TransportEventReport report;
            report.event = TransportEvent::BindFailed;
            report.subject = requested;
            report.detail = detail;
            events->push_back(std::move(report));
        }
        Close();
        return false;
    };

    if (!EnsureSocketsUsable()) {
        return fail("the platform's socket layer could not be initialised");
    }

    addrinfo hints = {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;
    // AI_NUMERICHOST keeps a DNS lookup out of a receiver's start-up path: a
    // listen address is a literal address, so an unresolvable string fails here
    // rather than after a resolver timeout an operator will read as silence.
    hints.ai_flags = AI_PASSIVE | AI_NUMERICHOST | AI_NUMERICSERV;

    addrinfo* resolved = nullptr;
    const std::string port = std::to_string(config.listenPort);
    const int status =
        ::getaddrinfo(config.listenAddress.c_str(), port.c_str(), &hints,
                      &resolved);
    if (status != 0 || !resolved) {
        if (resolved) {
            ::freeaddrinfo(resolved);
        }
        return fail("the listen address is not a numeric address this host "
                    "understands");
    }

    std::string error;
    for (const addrinfo* candidate = resolved; candidate;
         candidate = candidate->ai_next) {
        const SocketHandle handle = ::socket(
            candidate->ai_family, candidate->ai_socktype,
            candidate->ai_protocol);
        if (static_cast<std::intptr_t>(handle) == kInvalidSocket) {
            error = SocketErrorText(LastSocketError());
            continue;
        }

        if (config.reuseAddress) {
            const int on = 1;
            ::setsockopt(handle, SOL_SOCKET, SO_REUSEADDR,
                         reinterpret_cast<const char*>(&on), sizeof(on));
        }
        if (config.receiveBufferBytes != 0) {
            const int size = static_cast<int>(std::min<std::size_t>(
                config.receiveBufferBytes, 0x7fffffffu));
            ::setsockopt(handle, SOL_SOCKET, SO_RCVBUF,
                         reinterpret_cast<const char*>(&size), sizeof(size));
        }

        if (::bind(handle, candidate->ai_addr,
                   static_cast<socklen_t>(candidate->ai_addrlen)) != 0) {
            error = SocketErrorText(LastSocketError());
            CloseSocket(handle);
            continue;
        }
        if (!SetNonBlocking(handle)) {
            // Every wait in this class is a poll followed by a non-blocking
            // read; a socket that will not go non-blocking would turn a zero
            // timeout into an indefinite one, which is the one behaviour a
            // caller polling inside its own tick cannot survive.
            error = SocketErrorText(LastSocketError());
            CloseSocket(handle);
            continue;
        }

        _socket = static_cast<std::intptr_t>(handle);
        break;
    }
    ::freeaddrinfo(resolved);

    if (_socket == -1) {
        return fail(error.empty() ? std::string("the address could not be bound")
                                  : error);
    }

    // Sized once, and never resized again: this is where a datagram is read
    // before its real length is known. One byte above the bound, so that a
    // platform which truncates silently still yields a length this class can
    // recognise as too long (UdpReceiver.h).
    _buffer.resize(MaxDatagramBytes + 1);
    _receiveBufferBytes = ReadReceiveBuffer(ToHandle(_socket));

    sockaddr_storage bound = {};
    socklen_t boundLength = sizeof(bound);
    if (::getsockname(ToHandle(_socket),
                      reinterpret_cast<sockaddr*>(&bound), &boundLength) == 0) {
        _boundEndpoint =
            FormatEndpoint(reinterpret_cast<const sockaddr*>(&bound),
                           static_cast<std::size_t>(boundLength));
        _loopbackOnly =
            AddressIsLoopbackOnly(reinterpret_cast<const sockaddr*>(&bound));
    } else {
        _boundEndpoint = requested;
    }

    _config = config;
    _epoch = SteadyTicks();
    _lastError.clear();

    // A successful `Open` starts a session, and everything measured against the
    // receive clock restarts with it. `_stats` is in that list rather than
    // outside it: `firstReceiveTime` and `lastReceiveTime` are relative to this
    // epoch, so carrying them across one would put two numbers on an axis they
    // were never both on — and the silence check reading such a value found a
    // source that "last sent" further in the future than the clock had yet
    // reached, and stayed blind for as long as the previous session had run.
    _stats = UdpReceiverStats();
    _silenceReported = false;
    _lastTrafficTime = 0.0;
    _sawTraffic = false;
    return true;
}

void
UdpReceiver::Close() noexcept
{
    if (_socket != -1) {
        CloseSocket(ToHandle(_socket));
        _socket = -1;
    }
    _boundEndpoint.clear();
    _loopbackOnly = false;
    _receiveBufferBytes = 0;
    // Actually released, rather than merely cleared. This is ~64 KB, and a
    // caller holding receivers for interfaces it is not listening on — a
    // reconnect loop is the ordinary case — should pay for the sockets it has
    // open and not for the ones it has closed. `Open` sizes it again.
    std::vector<std::uint8_t>().swap(_buffer);
    // The configuration belonged to the session that just ended, and a stale
    // silence threshold surviving a failed re-`Open` is the kind of coupling
    // that is only harmless until something reorders two calls.
    _config = UdpReceiverConfig();
    _silenceReported = false;
    _lastTrafficTime = 0.0;
    _sawTraffic = false;
}

double
UdpReceiver::Now() const noexcept
{
    return TicksToSeconds(SteadyTicks() - _epoch);
}

void
UdpReceiver::_ReportSilence(std::vector<TransportEventReport>* events)
{
    if (_config.silenceTimeoutSeconds <= 0.0 || _silenceReported) {
        return;
    }
    // Measured from the last accepted datagram, or from `Open` when none has
    // arrived at all — the two are the same question asked at different points
    // in a session, and one event covers both. Which of the two it was is in
    // `_sawTraffic` and reaches the caller in the detail below, so an adapter
    // whose code set distinguishes them still can. Both facts are read from
    // members rather than from `_stats`, which a caller may zero at any moment
    // (UdpReceiver.h).
    const double since = Now() - _lastTrafficTime;
    if (since < _config.silenceTimeoutSeconds) {
        return;
    }

    _silenceReported = true;
    ++_stats.silenceReports;
    if (!events) {
        // Counted even when nobody asked for the report: the tally is what a
        // session report reads, and it must not depend on whether the loop that
        // noticed had somewhere to put a message.
        return;
    }

    // No time is carried, and an adapter must not add one. A diagnostic's
    // `timestamp` is "seconds in the source's own clock, when the diagnostic is
    // tied to a frame" (Diagnostics.h), and this observation is tied to no frame
    // and has no source clock reading by definition — nothing arrived to carry
    // one. Putting the *receiver's* clock there would give a session report two
    // unrelated timelines in one column, and would sort this line against a
    // decoder's own lines as though they were comparable. The bind failure above
    // says nothing about time for the same reason; the duration goes in the
    // detail, where a number needs no shared origin to be read.
    TransportEventReport report;
    report.event = TransportEvent::Silence;
    report.source = _boundEndpoint;
    report.detail =
        (_sawTraffic ? "the source stopped sending "
                     : "no datagram has arrived since the receiver was opened ")
        + FormatSeconds(since) + "s ago";
    events->push_back(std::move(report));
}

ReceiveStatus
UdpReceiver::Receive(ReceivedDatagram* datagram, double timeoutSeconds,
                     std::vector<TransportEventReport>* events)
{
    if (!datagram) {
        return ReceiveStatus::Failed;
    }
    if (_socket == -1) {
        return ReceiveStatus::Closed;
    }

    // The deadline is computed once, so the retries a transient error costs
    // come out of the caller's timeout rather than extending it. A caller
    // polling with a zero timeout inside its own tick must not be held for an
    // unbounded number of ICMP reports.
    const std::int64_t start = SteadyTicks();
    double remaining = timeoutSeconds;
    // Charges the elapsed time against the caller's budget and answers whether
    // any is left. A negative timeout means "wait indefinitely" and always has
    // budget: a caller that asked to wait forever and meets an unending stream
    // of transient errors waits forever, which is what it asked for — and it
    // cannot spin, because the poll at the top of the loop blocks.
    //
    // A *bounded* budget must be checked rather than merely decremented. Once
    // it reaches zero the poll stops blocking, so a socket that reports
    // readable and then yields nothing would turn the retry below into a tight
    // loop.
    const auto spend = [&]() {
        if (timeoutSeconds < 0.0) {
            return true;
        }
        remaining = std::max(
            0.0, timeoutSeconds - TicksToSeconds(SteadyTicks() - start));
        return remaining > 0.0;
    };

    // The two exits that mean *no traffic arrived*, and only those two: a poll
    // that timed out, and a wait whose budget ran out with the socket still
    // empty. `Closed` and `Failed` do not come through here and neither does the
    // error tail below — a call that met a truncated datagram or an ICMP report
    // found something waiting, so counting it as idle would tally it twice and
    // then diagnose "the source stopped sending" about a source that had
    // demonstrably just sent.
    const auto quiet = [&]() {
        ++_stats.idleReceives;
        _ReportSilence(events);
        return ReceiveStatus::Idle;
    };

    for (;;) {
        PollDescriptor descriptor = {};
        descriptor.fd = ToHandle(_socket);
        descriptor.events = POLLIN;

        const int ready =
            PollSockets(&descriptor, 1, TimeoutToMilliseconds(remaining));
        if (ready == 0) {
            return quiet();
        }
        if (ready < 0) {
            const int code = LastSocketError();
            if (ErrorIsInterrupted(code)) {
                if (!spend()) {
                    return quiet();
                }
                continue;
            }
            _lastError = SocketErrorText(code);
            return ReceiveStatus::Failed;
        }

        // `revents` is checked rather than assumed, and the reason is a spin
        // rather than tidiness. PollTimeout.h states it in full, and its unit
        // test is where the wake-ups no socket produces on demand are exercised.
        if (ClassifyPollWakeUp(descriptor.revents, POLLIN)
            != PollWakeUp::Readable) {
            _lastError = "the socket reported an error condition rather than a "
                         "readable datagram";
            return ReceiveStatus::Failed;
        }

        sockaddr_storage from = {};
        socklen_t fromLength = sizeof(from);
        const auto received = ::recvfrom(
            ToHandle(_socket), reinterpret_cast<char*>(_buffer.data()),
            static_cast<int>(_buffer.size()), 0,
            reinterpret_cast<sockaddr*>(&from), &fromLength);

        if (received >= 0) {
            const std::size_t size = static_cast<std::size_t>(received);
            // The buffer holds one byte more than the bound precisely so this
            // comparison can exist. POSIX truncates silently — a short read and
            // a whole datagram are the same return value — so on that spare byte
            // rests the difference between refusing an over-long datagram and
            // writing a packet the source never sent into a fixture. Windows
            // reaches the same counter through WSAEMSGSIZE below.
            if (size > MaxDatagramBytes) {
                ++_stats.datagramsTruncated;
                _lastError = "a datagram larger than the protocol's maximum was "
                             "dropped rather than passed on half-read";
                if (!spend()) {
                    return ReceiveStatus::Idle;
                }
                continue;
            }
            // Assigned rather than read into directly: growing the caller's
            // vector to `MaxDatagramBytes` and shrinking it back would value-
            // initialise ~64 KB per datagram, which at a capture product's rate
            // is megabytes a second of memset on the one path that has to stay
            // cheap. This copies the datagram's real length and nothing more,
            // and reuses the caller's capacity exactly as before.
            datagram->bytes.assign(_buffer.data(), _buffer.data() + size);
            datagram->receiveTime = TicksToSeconds(SteadyTicks() - _epoch);
            datagram->peer =
                FormatEndpoint(reinterpret_cast<const sockaddr*>(&from),
                               static_cast<std::size_t>(fromLength));

            if (_stats.datagramsReceived == 0) {
                _stats.firstReceiveTime = datagram->receiveTime;
            }
            ++_stats.datagramsReceived;
            _stats.bytesReceived += size;
            _stats.lastReceiveTime = datagram->receiveTime;
            _stats.lastPeer = datagram->peer;
            // A datagram ends the episode. The next silence is a new one and is
            // reported again, which is what "once per episode" means: a source
            // that drops out repeatedly says so repeatedly. These three are the
            // wire facts the silence check reads, and they are members rather
            // than `_stats` fields so that zeroing the tally cannot move them.
            _lastTrafficTime = datagram->receiveTime;
            _sawTraffic = true;
            _silenceReported = false;
            return ReceiveStatus::Received;
        }

        const int code = LastSocketError();
        if (ErrorIsWouldBlock(code) || ErrorIsInterrupted(code)) {
            // The poll said readable and the read found nothing — the kernel
            // discarded the datagram between the two, or a signal arrived. The
            // caller asked to wait for a datagram, so keep waiting out what is
            // left of its timeout rather than reporting an idle socket it did
            // not ask about. Neither is a receive error: a caller polling with
            // a zero timeout meets both routinely and can do nothing with the
            // count.
            if (!spend()) {
                return quiet();
            }
            continue;
        }
        if (ErrorIsTruncation(code)) {
            ++_stats.datagramsTruncated;
        } else if (ErrorIsTransient(code)) {
            ++_stats.receiveErrors;
        } else {
            _lastError = SocketErrorText(code);
            return ReceiveStatus::Failed;
        }
        _lastError = SocketErrorText(code);
        // Deliberately not `quiet()`. Both branches above met something —
        // an over-long datagram, or an ICMP report about a peer — and a call
        // that met something is neither an idle poll nor evidence of silence.
        // Reporting it as either would let a session log say a source had
        // stopped sending in the same breath as counting what it sent.
        if (!spend()) {
            return ReceiveStatus::Idle;
        }
    }
}

DatagramQueue::DatagramQueue(const DatagramQueueConfig& config)
    : _config(config)
{
}

bool
DatagramQueue::Push(ReceivedDatagram datagram)
{
    const std::size_t size = datagram.bytes.size();

    std::lock_guard<std::mutex> lock(_mutex);
    bool displaced = false;
    while (!_queued.empty()
           && (_queued.size() + 1 > _config.maxDatagrams
               || _queuedBytes + size > _config.maxBytes)) {
        _queuedBytes -= _queued.front().bytes.size();
        _queued.pop_front();
        ++_stats.dropped;
        displaced = true;
    }

    _queued.push_back(std::move(datagram));
    _queuedBytes += size;
    ++_stats.pushed;
    _stats.highWaterMark = std::max(_stats.highWaterMark, _queued.size());
    return !displaced;
}

std::size_t
DatagramQueue::Drain(std::vector<ReceivedDatagram>* out)
{
    if (!out) {
        return 0;
    }

    std::deque<ReceivedDatagram> taken;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        taken.swap(_queued);
        _queuedBytes = 0;
        _stats.drained += static_cast<std::uint64_t>(taken.size());
    }

    // Moved out with the lock released: a consumer's vector may reallocate, and
    // holding the network thread off while it does is the one avoidable stall in
    // this class.
    const std::size_t count = taken.size();
    out->reserve(out->size() + count);
    for (ReceivedDatagram& datagram : taken) {
        out->push_back(std::move(datagram));
    }
    return count;
}

std::size_t
DatagramQueue::GetSize() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _queued.size();
}

void
DatagramQueue::Clear()
{
    std::lock_guard<std::mutex> lock(_mutex);
    _queued.clear();
    _queuedBytes = 0;
}

DatagramQueueStats
DatagramQueue::GetStats() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _stats;
}

void
DatagramQueue::ResetStats()
{
    std::lock_guard<std::mutex> lock(_mutex);
    _stats = DatagramQueueStats();
}

} // namespace liveTransport
