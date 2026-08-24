// SPDX-License-Identifier: Apache-2.0
//
// What is left of this adapter's receiver after the socket moved: the map from
// a transport event to a `VRM_VMC_*` code.
//
// This is the only file in the pair that could not be shared, and the reason is
// the whole of WORKSPACE.md §2's diagnostic split. `liveTransport` reports what
// it observed; a code is frozen per adapter, before its decoder exists, so the
// layer that knows which adapter it is has to be the one that names it.

#include "vrmAdapterVmc/UdpReceiver.h"

#include <utility>

namespace vrmAdapterVmc
{

namespace
{

// One event, one code. `TransportEvent::Silence` is unreachable from here — the
// configuration below never sets a threshold — and it is handled rather than
// ignored so that a future `VRM_VMC_*` code for it is a table edit and not a
// hunt for the raise site. Until that code exists a silence report is dropped,
// which is exactly what an adapter with no vocabulary for an event must do:
// inventing a second spelling of the sibling's is the contract change §8 has
// not made.
bool
Translate(const liveTransport::TransportEventReport& report,
          Diagnostic* diagnostic)
{
    switch (report.event) {
    case liveTransport::TransportEvent::BindFailed:
        *diagnostic =
            MakeDiagnostic(DiagnosticCode::SocketBindFailed, report.detail);
        diagnostic->source = report.source;
        diagnostic->subject = report.subject;
        return true;
    case liveTransport::TransportEvent::Silence:
        return false;
    }
    return false;
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
    // `silenceTimeoutSeconds` is deliberately left at 0, which is off. See
    // UdpReceiver.h: this adapter's frozen code set has nothing to map the
    // event onto.

    // Collected unconditionally rather than only when the caller asked, so that
    // an event this adapter cannot yet name is dropped in one place with a
    // reason beside it, rather than by a null pointer that says nothing.
    std::vector<liveTransport::TransportEventReport> events;
    const bool opened = _receiver.Open(transport, &events);

    if (diagnostics) {
        for (const liveTransport::TransportEventReport& report : events) {
            Diagnostic diagnostic;
            if (Translate(report, &diagnostic)) {
                diagnostics->push_back(std::move(diagnostic));
            }
        }
    }
    return opened;
}

} // namespace vrmAdapterVmc
