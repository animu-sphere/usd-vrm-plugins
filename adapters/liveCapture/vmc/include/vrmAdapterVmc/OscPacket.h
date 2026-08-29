// SPDX-License-Identifier: Apache-2.0
//
// The OSC decoder, reached through this adapter's diagnostic vocabulary.
//
// The decoder itself is `libs/osc` and knows nothing about VMC — not the
// addresses, not the argument shapes, not the code it used to raise. What is
// left here is the one thing a shared library may not hold: the map from *a
// datagram was not decodable OSC* onto `VRM_VMC_PACKET_MALFORMED`, which is a
// code this adapter froze before its decoder existed and which its golden tests
// spell out in full.
//
// ## Why the names below are `using` and not new spellings
//
// `OscPacket`, `OscMessage`, `OscArgument`, `OscBlob` and the two constants are
// the same types they always were, reached through a `using` rather than
// redeclared — so `VmcMessage.cpp`, `LiveSource.cpp` and every test that
// spelled `vrmAdapterVmc::OscPacket` did not have to learn a second spelling.
// `Diagnostics.h` did exactly this for `liveTransport`'s vehicle on 2026-08-24
// and this follows it.
//
// The **function** is not a `using`, and that is the whole of the boundary. The
// shared decoder fills an `osc::OscDecodeError` — a subject and a detail and no
// code — and the overloads below turn that into a `Diagnostic` carrying this
// adapter's code, its severity and its recoverability. A caller here sees the
// signature it always saw.
//
// ## What this layer still cannot do
//
// It cannot tell an unimplemented address from any other address, because the
// decoder underneath it does not know what an address means. `/foo/bar` and
// `/VMC/Ext/Midi/Note` both decode cleanly; deciding that neither is
// implemented is `VmcMessage`'s job, one step further up, as
// `VRM_VMC_UNSUPPORTED_MESSAGE` — which is never raised from here.
#pragma once

#include "vrmAdapterVmc/Diagnostics.h"
#include "vrmAdapterVmc/api.h"

#include "osc/OscPacket.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace vrmAdapterVmc
{

using osc::MaxOscBundleDepth;
using osc::OscArgument;
using osc::OscBlob;
using osc::OscMessage;
using osc::OscPacket;
using osc::OscTimeTagImmediate;

// Decodes one datagram. On failure `packet` is left untouched and `diagnostic`,
// when given, carries `VRM_VMC_PACKET_MALFORMED` with the offending address as
// its subject where one was read, and a byte offset in its detail.
//
// The refusal's text is the shared decoder's verbatim; the code, the severity
// and the recoverability are this adapter's, from its own table.
VRMADAPTERVMC_API bool DecodeOscPacket(
    const std::uint8_t* bytes, std::size_t size, OscPacket* packet,
    Diagnostic* diagnostic = nullptr);

inline bool
DecodeOscPacket(const std::vector<std::uint8_t>& datagram, OscPacket* packet,
                Diagnostic* diagnostic = nullptr)
{
    return DecodeOscPacket(datagram.data(), datagram.size(), packet,
                           diagnostic);
}

// Decoding a temporary is always a bug: the decoded packet's `address`, `text`
// and `blob` point into the datagram, and a temporary is gone at the end of the
// full expression that produced it. This overload turns that into a compile
// error rather than a read of freed memory -- which is not hypothetical. The
// first test written against this API did exactly that, passed on Windows
// because the freed bytes happened to survive, and aborted on Linux and macOS.
//
// The default argument is load-bearing: without it a two-argument call would
// not consider this overload at all, and the temporary would bind to the
// reference above. `libs/osc` deletes the same overload for the same reason;
// deleting it here as well is what keeps the guard on the signature callers
// actually reach for.
bool DecodeOscPacket(std::vector<std::uint8_t>&& datagram, OscPacket* packet,
                     Diagnostic* diagnostic = nullptr) = delete;

} // namespace vrmAdapterVmc
