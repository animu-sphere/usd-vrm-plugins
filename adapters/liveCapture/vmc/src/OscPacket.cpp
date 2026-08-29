// SPDX-License-Identifier: Apache-2.0

#include "vrmAdapterVmc/OscPacket.h"

#include <utility>

namespace vrmAdapterVmc
{

// One refusal, one code. The shared decoder makes a single distinction -- a
// datagram is decodable OSC or it is not -- so unlike the transport's
// `TransportEvent` there is nothing to switch over here, and inventing
// something to switch over would have been this adapter's problem rather than
// the decoder's (osc-and-vrchat-trackers.md §8).
//
// `PacketMalformed` and never `UnsupportedMessage`: the decoder cannot tell an
// unimplemented address from any other one, because it does not know what an
// address means. That decision belongs to `VmcMessage`, one layer up.
bool
DecodeOscPacket(const std::uint8_t* bytes, std::size_t size, OscPacket* packet,
                Diagnostic* diagnostic)
{
    if (!diagnostic) {
        return osc::DecodeOscPacket(bytes, size, packet);
    }

    osc::OscDecodeError error;
    if (osc::DecodeOscPacket(bytes, size, packet, &error)) {
        return true;
    }
    // `MakeDiagnostic` fills severity and recoverability from the code's own
    // table row, so the two cannot disagree with it at a raise site.
    *diagnostic =
        MakeDiagnostic(DiagnosticCode::PacketMalformed, std::move(error.detail));
    diagnostic->subject = std::move(error.subject);
    return false;
}

} // namespace vrmAdapterVmc
