// SPDX-License-Identifier: Apache-2.0
//
// What is left of this adapter's receiver after the socket moved: the map from
// a transport event to a `VRM_MOCOPI_*` code.
//
// This is the only file in the pair that could not be shared, and the reason is
// the whole of WORKSPACE.md §2's diagnostic split. `liveTransport` reports what
// it observed; a code is frozen per adapter, before its decoder exists, so the
// layer that knows which adapter it is has to be the one that names it.
//
// This adapter names both events. The sibling names one, because `VRM_VMC_*`
// has no code for silence — and that is the entire remaining difference between
// two receivers that had drifted by 210 lines.

#include "vrmAdapterMocopi/UdpReceiver.h"

#include <utility>

namespace vrmAdapterMocopi
{

namespace
{

// One event, one code.
//
// The silence detail carries which of the two silences it was — a source that
// has not started, and one that has stopped — because the frozen code covers
// both by design ("a device that has not been started and one that has stopped"
// — Diagnostics.h) and the sentence is what tells an operator which they are
// looking at.
//
// No `timestamp` on either, and the shared receiver supplies none. That field is
// "seconds in the source's own clock, when the diagnostic is tied to a frame",
// and neither of these is tied to a frame: nothing arrived to carry a source
// clock reading. Putting the *receiver's* clock there would give a session
// report two unrelated timelines in one column, and would sort these lines
// against the decoder's `TRACKING_LOST` and `TIMESTAMP_INVALID` lines as though
// they were comparable.
Diagnostic
Translate(const liveTransport::TransportEventReport& report)
{
    switch (report.event) {
    case liveTransport::TransportEvent::Silence: {
        Diagnostic diagnostic =
            MakeDiagnostic(DiagnosticCode::DeviceUnavailable, report.detail);
        diagnostic.source = report.source;
        diagnostic.subject = report.subject;
        return diagnostic;
    }
    case liveTransport::TransportEvent::BindFailed:
        break;
    }
    Diagnostic diagnostic =
        MakeDiagnostic(DiagnosticCode::SocketBindFailed, report.detail);
    diagnostic.source = report.source;
    diagnostic.subject = report.subject;
    return diagnostic;
}

void
Append(const std::vector<liveTransport::TransportEventReport>& events,
       std::vector<Diagnostic>* diagnostics)
{
    if (!diagnostics) {
        return;
    }
    for (const liveTransport::TransportEventReport& report : events) {
        diagnostics->push_back(Translate(report));
    }
}

} // namespace

bool
UdpReceiver::Open(const UdpReceiverConfig& config,
                  std::vector<Diagnostic>* diagnostics)
{
    liveTransport::UdpReceiverConfig transport;
    transport.listenAddress = config.listenAddress;
    transport.listenPort = config.listenPort;
    transport.reuseAddress = config.reuseAddress;
    transport.receiveBufferBytes = config.receiveBufferBytes;
    transport.silenceTimeoutSeconds = config.silenceTimeoutSeconds;

    std::vector<liveTransport::TransportEventReport> events;
    const bool opened = _receiver.Open(transport, &events);
    Append(events, diagnostics);
    return opened;
}

ReceiveStatus
UdpReceiver::Receive(ReceivedDatagram* datagram, double timeoutSeconds,
                     std::vector<Diagnostic>* diagnostics)
{
    // The sink is passed through only when the caller wants one. The shared
    // receiver counts a silence episode either way — the tally is what a session
    // report reads, and it must not depend on whether the loop that noticed had
    // somewhere to put a message — so a null `diagnostics` here still moves
    // `silenceReports`.
    if (!diagnostics) {
        return _receiver.Receive(datagram, timeoutSeconds);
    }

    std::vector<liveTransport::TransportEventReport> events;
    const ReceiveStatus status =
        _receiver.Receive(datagram, timeoutSeconds, &events);
    Append(events, diagnostics);
    return status;
}

} // namespace vrmAdapterMocopi
