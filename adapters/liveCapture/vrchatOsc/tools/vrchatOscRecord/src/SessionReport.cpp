// SPDX-License-Identifier: Apache-2.0
#include "SessionReport.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace vrchatOscRecordTool
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
    case StopReason::SocketClosed:
        return "the socket was no longer open";
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

    if (_datagrams == 0 || count < _shortestDatagram) {
        _shortestDatagram = count;
    }
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
    // Deduplicated against the *counting* set and not against the short named
    // list. Searching the named list alone counts a datagram as a host for every
    // peer that arrived too late to be named -- a defect the mocopi tool shipped
    // and corrected, and the reason this loop is written in this order here.
    if (_distinctPeers.count(peer) != 0) {
        return;
    }
    if (_distinctPeers.size() < kMaxTrackedPeers) {
        _distinctPeers.insert(peer);
    } else {
        // Past the bound the count stops being exact and says so. Not inserted,
        // so the set cannot be grown without limit by a source that sends from a
        // new port every datagram.
        _peersUntracked = true;
        return;
    }
    if (_peers.size() < kMaxNamedPeers) {
        _peers.push_back(peer);
    }
}

void
SessionReport::_ObservePrefix(const std::uint8_t* bytes, std::size_t count)
{
    // A zero-length datagram shares no byte with anything, and `bytes` is the
    // `data()` of an empty vector -- which may be null. `assign(null, null)` is a
    // formally invalid iterator range even though every implementation treats it
    // as empty, so it is answered here rather than relied on.
    if (count == 0) {
        _prefix.clear();
        return;
    }
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
    const std::vector<vrmAdapterVrchatOsc::Diagnostic>& log)
{
    for (const vrmAdapterVrchatOsc::Diagnostic& diagnostic : log) {
        const auto index = static_cast<std::size_t>(diagnostic.code);
        if (index >= vrmAdapterVrchatOsc::DiagnosticCodeCount) {
            continue;
        }
        if (_diagnostics[index] == 0) {
            _firstDiagnostic[index] = diagnostic;
        }
        ++_diagnostics[index];
    }
}

void
SessionReport::Print(std::FILE* out,
                     const vrmAdapterVrchatOsc::UdpReceiver* receiver,
                     const vrmAdapterVrchatOsc::PacketCapture* provenance) const
{
    if (receiver) {
        const vrmAdapterVrchatOsc::UdpReceiverStats& socket =
            receiver->GetStats();
        // The loopback note is weaker here than in the mocopi tool's report, and
        // deliberately so: that product documents `localhost` as an unsupported
        // destination, so a loopback-only recorder there is guaranteed to hear
        // nothing. On this wire a sender on the same machine is an ordinary
        // arrangement, so the fact is reported and no verdict is attached.
        std::fprintf(out, "listen:      %s%s, receive buffer %zu bytes\n",
                     receiver->GetBoundEndpoint().c_str(),
                     receiver->IsLoopbackOnly()
                         ? " (loopback only: reachable from this machine alone)"
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

    std::fprintf(out, "peers:       %s%zu", _peersUntracked ? "at least " : "",
                 _distinctPeers.size());
    for (std::size_t i = 0; i < _peers.size(); ++i) {
        std::fprintf(out, "%s%s", i == 0 ? " (" : ", ", _peers[i].c_str());
    }
    if (!_peers.empty()) {
        std::fprintf(out, "%s)",
                     _distinctPeers.size() > _peers.size() || _peersUntracked
                         ? ", ..."
                         : "");
    }
    std::fputc('\n', out);

    if (_intervals != 0) {
        const double mean = _intervalSum / static_cast<double>(_intervals);
        // "of receive clock", said in the line rather than only in the header:
        // this is an arrival rate and not the sender's update rate, and the two
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
    _PrintPrefix(out);

    if (provenance) {
        // Only for a capture read off disk. On a live session the operator typed
        // these a moment ago; for a fixture recorded months ago they are half of
        // what "is this still what I thought it was" means.
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
            std::fprintf(out, "%s%s=%s", printed == 0 ? "provenance:  " : " ",
                         field.key, field.value->c_str());
            ++printed;
        }
        if (printed == 0) {
            // Legal -- every header field is optional -- and worth saying,
            // because a committed fixture needs provenance and this is where its
            // absence is noticed.
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
                 _lengths.size(), static_cast<unsigned long long>(_datagrams));
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
SessionReport::_PrintPrefix(std::FILE* out) const
{
    // Two datagrams at least, because "the bytes every datagram shares" said of
    // one datagram is just that datagram, which the file already holds in hex.
    if (_datagrams < 2) {
        return;
    }
    if (_prefix.empty()) {
        std::fprintf(out, "prefix:      no bytes common to every datagram\n");
        return;
    }

    // "at least" only when the *cap* is what shortened the prefix. A prefix of
    // exactly `kMaxPrefixBytes` where some datagram was itself that long is
    // exact, and reporting it as a lower bound would send a reviewer looking for
    // bytes that are not there.
    const bool capped = _prefix.size() == kMaxPrefixBytes
        && _shortestDatagram > kMaxPrefixBytes;
    std::fprintf(out, "prefix:      %s%zu byte(s) common to every datagram:\n",
                 capped ? "at least " : "", _prefix.size());

    // Laid out exactly as the capture format lays out a datagram -- sixteen
    // bytes a line, a padded hex column, an ASCII gutter -- and rendered by the
    // format's own gutter function rather than a second copy of it. On this wire
    // the gutter is the whole point of the line: an OSC address is ASCII and
    // leads the packet, so a shared prefix reads as text without this tool
    // having parsed anything.
    const std::size_t perLine = vrmAdapterVrchatOsc::PacketCaptureBytesPerLine;
    for (std::size_t offset = 0; offset < _prefix.size(); offset += perLine) {
        const std::size_t count = std::min(perLine, _prefix.size() - offset);
        std::fprintf(out, "             ");
        for (std::size_t i = 0; i < count; ++i) {
            std::fprintf(out, "%s%02x", i == 0 ? "" : " ",
                         static_cast<unsigned>(_prefix[offset + i]));
        }
        // Pad to the full hex column so a short last line's gutter stays in the
        // same column as every other line's.
        for (std::size_t i = count; i < perLine; ++i) {
            std::fprintf(out, "   ");
        }
        std::fprintf(out, "  |%s|\n",
                     vrmAdapterVrchatOsc::PacketCaptureGutter(
                         _prefix.data() + offset, count).c_str());
    }
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

    for (std::size_t index = 0;
         index < vrmAdapterVrchatOsc::DiagnosticCodeCount; ++index) {
        if (_diagnostics[index] == 0) {
            continue;
        }
        const auto code = static_cast<vrmAdapterVrchatOsc::DiagnosticCode>(index);
        // The severity the diagnostic was *raised* with, not the code's default.
        // Diagnostics.h contemplates a caller escalating one, and the whole
        // diagnostic is already kept here -- so recomputing the severity from
        // the code's table would make this line contradict the `first:` line
        // printed immediately below it, which formats the real one.
        std::fprintf(out, "diagnostics: %llu x %s (%s)\n",
                     static_cast<unsigned long long>(_diagnostics[index]),
                     std::string(vrmAdapterVrchatOsc::DiagnosticCodeString(code))
                         .c_str(),
                     std::string(vrmAdapterVrchatOsc::DiagnosticSeverityString(
                                     _firstDiagnostic[index].severity))
                         .c_str());
        std::fprintf(out, "             first: %s\n",
                     vrmAdapterVrchatOsc::FormatDiagnostic(
                         _firstDiagnostic[index]).c_str());
    }
}

} // namespace vrchatOscRecordTool
