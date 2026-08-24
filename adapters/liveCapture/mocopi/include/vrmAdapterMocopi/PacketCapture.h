// SPDX-License-Identifier: Apache-2.0
//
// A recorded mocopi packet capture: the on-disk form of what the socket
// received.
//
// The format, its reader and its writer are `liveTransport`'s
// (liveTransport/PacketCapture.h). What is this adapter's is one string — the
// magic line — and that is the whole of what stayed behind.
//
// It is deliberately *not* a `motion-capture-trace`. The two formats sit at
// opposite ends of the adapter and neither substitutes for the other:
//
//     packets in  ->  [ mocopi-packet-capture ]  ->  decode  ->  map
//                 ->  [ motion-capture-trace ]   ->  the canonical pipeline
//
// A trace records what an adapter *produced* — canonical poses, in capture
// order, with arrival order deliberately discarded. A capture records what the
// adapter was *given*, in arrival order, including the packets it will refuse.
//
// ## The condition this file wrote down, and its arrival
//
// This header used to argue that the *whole* format was repeated rather than
// shared, having ruled out both homes a reader reaches for first — a sibling
// adapter's include is forbidden, and `motionRuntime` refuses a socket — and it
// closed by naming exactly what would change the answer:
//
// > What changes it is a *third* recorder — a third live adapter, or a tool
// > that must read both formats — at which point the shape is a library and its
// > home is the boundary question above, argued in its own change.
//
// A third live adapter arrived, the boundary was argued in its own change, and
// `liveTransport` is the answer (osc-and-vrchat-trackers.md §3.2). One of the
// three arguments survived the move intact, and it is why the magic is still
// here rather than shared:
//
// > **The magic line is the fixture's type tag**, and having two of them is a
// > feature. A capture of one protocol handed to the other protocol's decoder
// > fails at the first line with a clear message rather than at the first field
// > with a malformed-packet diagnostic blamed on a source that did nothing.
//
// The header *vocabulary* did converge, in this adapter's direction:
// `PacketCapture::device` was this format's alone and is now everyone's. It is
// still what it was — the native path's whole claim is that it keeps device and
// sensor state a protocol relay drops, and a capture that cannot say which
// device produced it cannot support that claim later. Like `sender` it is
// provenance only: nothing in the decode path may branch on either, and the
// library that now stores them cannot, since it knows no protocol at all.
#pragma once

#include "vrmAdapterMocopi/api.h"

#include "liveTransport/PacketCapture.h"

#include <iosfwd>
#include <string>
#include <string_view>

namespace vrmAdapterMocopi
{

// The first token of every capture this adapter reads or writes. A fixture's
// type tag, in the sense above.
inline constexpr std::string_view PacketCaptureMagic = "!mocopi-packet-capture";

using liveTransport::MaxDatagramBytes;
using liveTransport::PacketCaptureBytesPerLine;
using liveTransport::PacketCaptureFormatVersion;

using liveTransport::PacketCapture;
using liveTransport::PacketCaptureError;
using liveTransport::RecordedDatagram;

using liveTransport::PacketCaptureGutter;

// Parses a capture. On failure `capture` is left untouched and `error`, when
// given, names the line and the reason.
inline bool
ReadPacketCapture(std::istream& input, PacketCapture* capture,
                  PacketCaptureError* error = nullptr)
{
    return liveTransport::ReadPacketCapture(PacketCaptureMagic, input, capture,
                                            error);
}

inline bool
ReadPacketCaptureFile(const std::string& path, PacketCapture* capture,
                      PacketCaptureError* error = nullptr)
{
    return liveTransport::ReadPacketCaptureFile(PacketCaptureMagic, path,
                                                capture, error);
}

// Writes `capture`. Emission is deterministic, so re-reading and rewriting a
// capture this writer produced is byte-identical — which is what lets a
// committed fixture be compared rather than merely parsed.
inline bool
WritePacketCapture(std::ostream& output, const PacketCapture& capture)
{
    return liveTransport::WritePacketCapture(PacketCaptureMagic, output,
                                             capture);
}

inline bool
WritePacketCaptureFile(const std::string& path, const PacketCapture& capture)
{
    return liveTransport::WritePacketCaptureFile(PacketCaptureMagic, path,
                                                 capture);
}

} // namespace vrmAdapterMocopi
