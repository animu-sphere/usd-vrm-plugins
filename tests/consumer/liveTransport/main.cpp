// SPDX-License-Identifier: Apache-2.0
//
// Includes one public header of the installed `liveTransport` package and calls
// into it. The include proves the package installed its header root; the call
// proves it installed something to link.
//
// *Which* call is a packaging decision here, more than in any other fixture.
// This is a static library, so an object file is pulled out of the archive only
// when something the consumer wrote needs it -- and the package's platform
// dependency is `ws2_32` on Windows and `Threads::Threads` elsewhere, both of
// which are undefined symbols of the receiver's object file and of no other.
// A fixture that called only the diagnostic vehicle would link a package whose
// platform link line was missing entirely and report criterion 4 met. So the
// call below is into the socket class, and it is the one call there that needs
// no socket: `Receive` on a receiver that was never opened returns `Closed`.
//
// Nothing binds, nothing listens, and no port is named. A packaging fixture
// that took a port would fail on a host where something else already held it,
// which is a false red about the machine rather than a measurement of the
// package. `libs/liveTransport/tests/` owns the sessions that really bind, the
// truncation bound and the silence episodes; duplicating any of it here would
// make a packaging failure look like a receiver failure the first time this
// went red.
// Two headers rather than one, which between them reach all four this package
// installs: `UdpReceiver.h` pulls `PacketCapture.h` and `api.h` in with it, and
// `Diagnostics.h` is the one nothing else reaches. A prefix that installed a
// header root missing either of these fails at the `#include` rather than at
// the call.
#include <liveTransport/Diagnostics.h>
#include <liveTransport/UdpReceiver.h>

#include <cstdio>
#include <string>

int
main()
{
    // Constructing this is what forces the archive member carrying the
    // platform's socket calls to be linked, which is the half of this package's
    // contract that a header include cannot reach.
    liveTransport::UdpReceiver receiver;
    if (receiver.IsOpen()) {
        std::fprintf(stderr, "consumer: a fresh receiver reports itself open\n");
        return 1;
    }

    liveTransport::ReceivedDatagram datagram;
    const liveTransport::ReceiveStatus status = receiver.Receive(&datagram);
    if (status != liveTransport::ReceiveStatus::Closed) {
        std::fprintf(stderr, "consumer: receiving from a closed socket "
                             "returned status %d\n",
                     static_cast<int>(status));
        return 1;
    }

    // The diagnostic vehicle, from the header nothing else pulls in: a
    // severity is the one value this library names in its own vocabulary
    // rather than an adapter's.
    const std::string severity(liveTransport::DiagnosticSeverityString(
        liveTransport::DiagnosticSeverity::Error));
    if (severity.empty()) {
        std::fprintf(stderr, "consumer: the installed package names no "
                             "severity string\n");
        return 1;
    }

    std::fprintf(stdout, "consumer: a closed receiver and the `%s` severity "
                         "came back through the installed package\n",
                 severity.c_str());
    return 0;
}
