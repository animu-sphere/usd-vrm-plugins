// SPDX-License-Identifier: Apache-2.0
//
// A recorded VRChat OSC packet capture: the on-disk form of what the socket
// received.
//
// The format, its reader and its writer are `liveTransport`'s
// (liveTransport/PacketCapture.h). What is this adapter's is one string — the
// magic line — and that is the whole of what is written here.
//
// **This file is the measurement the extraction was made for.** Both siblings
// carried the format's 400 lines, differing by six of them; `vrmAdapterMocopi`'s
// copy named the condition for turning that into a library — *a third recorder,
// a third live adapter, or a tool that must drive both* — and this adapter is
// the third. So the number to compare against those two is the length of this
// file: the third copy of the packet-capture format is a constant and four
// forwarding functions, which is what OSC-2 bought and what its done-condition
// said it would.
//
// It is deliberately *not* a `motion-capture-trace`. The two formats sit at
// opposite ends of the adapter and neither substitutes for the other:
//
//     packets in  ->  [ vrchat-osc-packet-capture ]  ->  decode  ->  map
//                 ->  [ motion-capture-trace ]        ->  the canonical pipeline
//
// A trace records what an adapter *produced* — canonical poses, in capture
// order, with arrival order deliberately discarded. A capture records what the
// adapter was *given*, in arrival order, including the packets it will refuse.
// Nothing in this repository can write the trace half for this adapter yet, and
// that is VRC-0's boundary rather than a gap: there is no decoder, so there is
// nothing decoded to write down.
//
// ## Why the magic is still per adapter
//
// It is the fixture's type tag, and having three of them is a feature. The case
// that makes it one is sharper here than it was for either sibling: a VMC
// capture and a VRChat OSC capture hold *the same wire format* — OSC over UDP —
// so a capture of one handed to the other's decoder would not fail at the first
// field. It would decode. Every address would be unknown, and the session would
// be reported as a sender speaking a subset of the surface, which is exactly
// what a genuine partial sender looks like. The magic is what makes that
// confusion impossible before the first byte of payload is read.
//
// ## What the header carries, and what it may not
//
// `sender` and `device` are provenance only: nothing in a decode path may branch
// on either. That rule survived the extraction unchanged and is now enforced by
// construction, because the library that stores them knows no protocol at all.
//
// `device` matters here for a reason the VMC adapter does not have. A VRChat OSC
// stream is *relayed* — the sender is an application re-expressing some other
// device's tracking — so a capture that names the application and not the device
// behind it cannot answer the question §11 exists to ask, which is whether one
// physical session observed four ways agrees with itself. The operator supplies
// both; neither is inferred from a payload.
#pragma once

#include "vrmAdapterVrchatOsc/api.h"

#include "liveTransport/PacketCapture.h"

#include <iosfwd>
#include <string>
#include <string_view>

namespace vrmAdapterVrchatOsc
{

// The first token of every capture this adapter reads or writes. A fixture's
// type tag, in the sense above.
inline constexpr std::string_view PacketCaptureMagic =
    "!vrchat-osc-packet-capture";

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

} // namespace vrmAdapterVrchatOsc
