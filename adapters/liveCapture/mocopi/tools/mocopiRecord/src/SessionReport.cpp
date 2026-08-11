// SPDX-License-Identifier: Apache-2.0
#include "SessionReport.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace mocopiRecordTool
{
namespace
{

std::string
Number(double value)
{
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.6g", value);
    return buffer;
}

} // namespace

const char*
StopReasonText(StopReason reason) noexcept
{
    switch (reason) {
    case StopReason::Interrupted:
        return "interrupted";
    case StopReason::Duration:
        return "--duration elapsed";
    case StopReason::IdleTimeout:
        return "--idle-timeout elapsed with nothing arriving";
    case StopReason::MaxDatagrams:
        return "--max-datagrams reached";
    case StopReason::EndOfCapture:
        return "end of capture";
    case StopReason::ReceiveFailed:
        break;
    }
    return "the socket failed";
}

void
SessionReport::ObserveDatagram(const std::string& peer,
                               const std::uint8_t* bytes, std::size_t count,
                               double receiveTime)
{
    // Before the counter moves: both of these ask whether this is the first
    // datagram, and answering that from `_datagrams` after incrementing it would
    // fold the first arrival into the interval statistics as an interval from
    // the start of the session, which is not an interval between arrivals.
    _ObservePrefix(bytes, count);
    if (_datagrams == 0) {
        _firstReceiveTime = receiveTime;
    } else {
        const double interval = receiveTime - _lastReceiveTime;
        if (_intervals == 0) {
            _intervalMin = interval;
            _intervalMax = interval;
        } else {
            _intervalMin = std::min(_intervalMin, interval);
            _intervalMax = std::max(_intervalMax, interval);
        }
        _intervalSum += interval;
        ++_intervals;
    }
    _lastReceiveTime = receiveTime;

    ++_datagrams;
    _payloadBytes += count;
    if (count == 0) {
        ++_emptyDatagrams;
    }

    const auto tally = _lengths.find(count);
    if (tally != _lengths.end()) {
        ++tally->second;
    } else if (_lengths.size() < kMaxTrackedLengths) {
        _lengths.emplace(count, 1);
    } else {
        ++_untalliedDatagrams;
    }

    if (peer.empty()) {
        return;
    }
    if (std::find(_peers.begin(), _peers.end(), peer) != _peers.end()) {
        return;
    }
    ++_peerCount;
    if (_peers.size() < kMaxNamedPeers) {
        _peers.push_back(peer);
    }
}

void
SessionReport::_ObservePrefix(const std::uint8_t* bytes, std::size_t count)
{
    if (_datagrams == 0) {
        _prefix.assign(bytes, bytes + std::min(count, kMaxPrefixBytes));
        return;
    }
    const std::size_t limit = std::min(_prefix.size(), count);
    std::size_t common = 0;
    while (common < limit && _prefix[common] == bytes[common]) {
        ++common;
    }
    _prefix.resize(common);
}

void
SessionReport::ObserveDiagnostics(
    const std::vector<vrmAdapterMocopi::Diagnostic>& log, std::size_t from)
{
    for (std::size_t i = from; i < log.size(); ++i) {
        const auto index = static_cast<std::size_t>(log[i].code);
        if (index >= vrmAdapterMocopi::DiagnosticCodeCount) {
            continue;
        }
        if (_diagnostics[index] == 0) {
            _firstDiagnostic[index] = log[i];
        }
        ++_diagnostics[index];
    }
}

void
SessionReport::Print(std::FILE* out,
                     const vrmAdapterMocopi::UdpReceiver* receiver,
                     const vrmAdapterMocopi::PacketCapture* provenance) const
{
    if (receiver) {
        const vrmAdapterMocopi::UdpReceiverStats& socket = receiver->GetStats();
        // The loopback note is stronger here than in the sibling's report, and
        // it is the vendor's statement rather than this tool's inference: the
        // product documents `localhost` as an unsupported destination, so a
        // loopback-only recorder will hear nothing from a device however right
        // everything else is.
        std::fprintf(out, "listen:      %s%s, receive buffer %zu bytes\n",
                     receiver->GetBoundEndpoint().c_str(),
                     receiver->IsLoopbackOnly()
                         ? " (loopback only: no device can reach it)"
                         : "",
                     receiver->GetReceiveBufferBytes());
        if (socket.receiveErrors != 0 || socket.datagramsTruncated != 0) {
            std::fprintf(out,
                         "socket:      %llu transient error(s), %llu datagram(s)"
                         " over the size limit and dropped\n",
                         static_cast<unsigned long long>(socket.receiveErrors),
                         static_cast<unsigned long long>(
                             socket.datagramsTruncated));
        }
    }

    std::fprintf(out, "received:    %llu datagram(s), %llu byte(s) over %s s\n",
                 static_cast<unsigned long long>(_datagrams),
                 static_cast<unsigned long long>(_payloadBytes),
                 Number(_datagrams == 0
                            ? 0.0
                            : _lastReceiveTime - _firstReceiveTime).c_str());

    if (_emptyDatagrams != 0) {
        std::fprintf(out,
                     "empty:       %llu zero-length datagram(s), kept as they "
                     "arrived\n",
                     static_cast<unsigned long long>(_emptyDatagrams));
    }

    std::fprintf(out, "peers:       %llu",
                 static_cast<unsigned long long>(_peerCount));
    for (std::size_t i = 0; i < _peers.size(); ++i) {
        std::fprintf(out, "%s%s", i == 0 ? " (" : ", ", _peers[i].c_str());
    }
    if (!_peers.empty()) {
        std::fprintf(out, "%s)", _peerCount > _peers.size() ? ", ..." : "");
    }
    std::fputc('\n', out);

    if (_intervals != 0) {
        const double mean = _intervalSum / static_cast<double>(_intervals);
        // "of receive clock", said in the line rather than only in the header:
        // this is an arrival rate and not the source's frame rate, and the two
        // differ by whatever the network and this process's scheduling did.
        std::fprintf(out,
                     "arrival:     %s Hz mean, interval %s-%s s, over %s s of "
                     "receive clock\n",
                     Number(mean > 0.0 ? 1.0 / mean : 0.0).c_str(),
                     Number(_intervalMin).c_str(), Number(_intervalMax).c_str(),
                     Number(_intervalSum).c_str());
    } else {
        std::fprintf(out, "arrival:     not measurable from %llu datagram(s)\n",
                     static_cast<unsigned long long>(_datagrams));
    }

    _PrintLengths(out);

    // Two datagrams at least, because "the bytes every datagram shares" said of
    // one datagram is just that datagram, which the file already holds in hex.
    if (_datagrams >= 2) {
        if (_prefix.empty()) {
            std::fprintf(out,
                         "prefix:      no bytes common to every datagram\n");
        } else {
            std::fprintf(out, "prefix:      %s%zu byte(s) common to every "
                              "datagram:\n             ",
                         _prefix.size() == kMaxPrefixBytes ? "at least " : "",
                         _prefix.size());
            for (std::size_t i = 0; i < _prefix.size(); ++i) {
                std::fprintf(out, "%s%02x", i == 0 ? "" : " ",
                             static_cast<unsigned>(_prefix[i]));
            }
            std::fputc('\n', out);
        }
    }

    if (provenance) {
        // Only for a capture read off disk. On a live session the operator typed
        // these a moment ago; for a fixture recorded months ago they are half of
        // what "is this still what I thought it was" means.
        // A pointer rather than a reference member: an array of pairs holding
        // `const std::string&` is legal and is not uniformly well-supported, and
        // nothing here needs it to be a pair.
        struct Field
        {
            const char* key;
            const std::string* value;
        };
        const Field fields[] = {
            {"sender", &provenance->sender},
            {"device", &provenance->device},
            {"sourceId", &provenance->sourceId},
            {"listen", &provenance->listenEndpoint},
            {"peer", &provenance->peerEndpoint},
        };
        std::size_t printed = 0;
        for (const Field& field : fields) {
            if (field.value->empty()) {
                continue;
            }
            std::fprintf(out, "%s%s=%s",
                         printed == 0 ? "provenance:  " : " ", field.key,
                         field.value->c_str());
            ++printed;
        }
        if (printed == 0) {
            // Legal — every header field is optional (PacketCapture.h) — and
            // worth saying, because a committed fixture needs provenance and
            // this is where its absence is noticed.
            std::fprintf(out, "provenance:  none recorded");
        }
        std::fputc('\n', out);
    }

    _PrintDiagnostics(out);

    std::fprintf(out, "stopped:     %s\n", StopReasonText(_stop));
}

void
SessionReport::_PrintLengths(std::FILE* out) const
{
    if (_lengths.empty()) {
        std::fprintf(out, "lengths:     none\n");
        return;
    }

    std::fprintf(out, "lengths:     %zu distinct in %llu datagram(s)",
                 _lengths.size(),
                 static_cast<unsigned long long>(_datagrams));
    if (_untalliedDatagrams != 0) {
        // The census is bounded, so it has to be able to say when it stopped
        // being the whole truth rather than reporting a subset as a total.
        std::fprintf(out,
                     "; census partial, %llu datagram(s) of further lengths "
                     "not tallied",
                     static_cast<unsigned long long>(_untalliedDatagrams));
    }
    std::fputc('\n', out);

    // Commonest first, and by length when two are equally common, so that two
    // runs over the same capture print the same order.
    std::vector<std::pair<std::size_t, std::uint64_t>> ordered(_lengths.begin(),
                                                               _lengths.end());
    std::sort(ordered.begin(), ordered.end(),
              [](const std::pair<std::size_t, std::uint64_t>& a,
                 const std::pair<std::size_t, std::uint64_t>& b) {
                  if (a.second != b.second) {
                      return a.second > b.second;
                  }
                  return a.first < b.first;
              });

    std::fprintf(out, "             ");
    for (std::size_t i = 0; i < ordered.size(); ++i) {
        if (i == kNamedLengths) {
            std::fprintf(out, ", ... (%zu more)", ordered.size() - i);
            break;
        }
        std::fprintf(out, "%s%llu of %zu", i == 0 ? "" : ", ",
                     static_cast<unsigned long long>(ordered[i].second),
                     ordered[i].first);
        if (i == 0) {
            std::fprintf(out, " byte(s)");
        }
    }
    std::fputc('\n', out);
}

void
SessionReport::_PrintDiagnostics(std::FILE* out) const
{
    std::uint64_t total = 0;
    for (const std::uint64_t count : _diagnostics) {
        total += count;
    }
    if (total == 0) {
        std::fprintf(out, "diagnostics: none\n");
        return;
    }

    for (std::size_t index = 0; index < vrmAdapterMocopi::DiagnosticCodeCount;
         ++index) {
        if (_diagnostics[index] == 0) {
            continue;
        }
        const auto code = static_cast<vrmAdapterMocopi::DiagnosticCode>(index);
        std::fprintf(out, "diagnostics: %llu x %s (%s)\n",
                     static_cast<unsigned long long>(_diagnostics[index]),
                     std::string(vrmAdapterMocopi::DiagnosticCodeString(code))
                         .c_str(),
                     std::string(vrmAdapterMocopi::DiagnosticSeverityString(
                                     vrmAdapterMocopi::DiagnosticDefaultSeverity(
                                         code)))
                         .c_str());
        std::fprintf(out, "             first: %s\n",
                     vrmAdapterMocopi::FormatDiagnostic(_firstDiagnostic[index])
                         .c_str());
    }
}

} // namespace mocopiRecordTool
