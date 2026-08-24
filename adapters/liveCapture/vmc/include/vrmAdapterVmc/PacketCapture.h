// SPDX-License-Identifier: Apache-2.0
//
// A recorded VMC packet capture: the on-disk form of what the socket received.
//
// The format, its reader and its writer are `liveTransport`'s
// (liveTransport/PacketCapture.h). What is this adapter's is one string — the
// magic line — and that is the whole of what stayed behind.
//
// It is deliberately *not* a `motion-capture-trace`. The two formats sit at
// opposite ends of the adapter and neither substitutes for the other:
//
//     packets in  ->  [ vmc-packet-capture ]  ->  decode  ->  map
//                 ->  [ motion-capture-trace ]  ->  the canonical pipeline
//
// A trace records what an adapter *produced* — canonical poses, in capture
// order, with arrival order deliberately discarded. A capture records what the
// adapter was *given*, in arrival order, including the packets it will refuse.
//
// ## Why the magic is still per adapter
//
// This file used to argue that the *whole* format was repeated rather than
// shared, and named the condition that would change that: a third recorder, at
// which point the shape is a library and its home is argued in its own change.
// That happened (osc-and-vrchat-trackers.md §3.2), and the argument's third
// point is the part that survived the move intact:
//
// > **The magic line is the fixture's type tag**, and having two of them is a
// > feature. A capture of one protocol handed to the other protocol's decoder
// > fails at the first line with a clear message rather than at the first field
// > with a malformed-packet diagnostic blamed on a source that did nothing.
//
// Every committed fixture in both corpora names its producer in its first
// token, so a single `!live-packet-capture` would have made the extraction a
// corpus rewrite. It did not.
//
// The header *vocabulary* did converge, and this adapter gains one key by it:
// `PacketCapture::device` was mocopi's and is now everyone's. Nothing here sets
// it and the writer emits only the fields a capture carries, so no VMC fixture
// changes a byte; the reader now accepts a `device` line where it used to
// refuse an unknown key, which is a widening and the only behaviour this move
// changed.
#pragma once

#include "vrmAdapterVmc/api.h"

#include "liveTransport/PacketCapture.h"

#include <iosfwd>
#include <string>
#include <string_view>

namespace vrmAdapterVmc
{

// The first token of every capture this adapter reads or writes. A fixture's
// type tag, in the sense above.
inline constexpr std::string_view PacketCaptureMagic = "!vmc-packet-capture";

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

} // namespace vrmAdapterVmc
