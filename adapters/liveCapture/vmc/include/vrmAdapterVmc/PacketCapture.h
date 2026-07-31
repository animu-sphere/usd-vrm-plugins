// SPDX-License-Identifier: Apache-2.0
//
// A recorded VMC packet capture: the on-disk form of what the socket received.
//
// The implementation order for this adapter puts transport last
// (roadmap/adapters-mocopi-vmc-ardy.md §5), which only works if there is
// something to decode without one. This is that something: the datagrams a
// session delivered, verbatim, with the instant each arrived.
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
// Recording a trace instead would mean the decoder's own tests are fed by the
// decoder, and every packet-level failure — a truncated datagram, a duplicate,
// a sender restart mid-frame — would be untestable because a trace cannot
// represent one.
//
// The format is line-oriented text, for the same reason the trace format is: a
// fixture is reviewed in a pull request, so it has to diff. Bytes are hex with
// an ASCII gutter, which is what makes an OSC address pattern legible in a
// review without a decoder ring:
//
//     # a comment
//     !vmc-packet-capture 1
//     sender example.synthetic
//     sourceId neutral-standing-01
//     listen 0.0.0.0:39539
//     peer 127.0.0.1:52001
//
//     d 0.000000 20
//       2f 56 4d 43 2f 45 78 74 2f 54 00 00 2c 66 00 00  |/VMC/Ext/T..,f..|
//       3d cc cc cd                                      |=...|
//
// `d <receiveTime> <length>` opens a record and the hex lines that follow carry
// exactly `length` bytes. Times are seconds on the *receiver's* clock, relative
// to the start of the recording, and must not go backwards: arrival order is
// the whole point of this format, and a clock that runs backwards is a recorder
// defect rather than a phenomenon to reproduce. What the sender believed the
// time was lives inside the payload, where a decoder can find it disagreeing
// with arrival order — which is exactly what `VRM_VMC_TIMESTAMP_REGRESSION`
// reports.
//
// The reader is strict, in the four ways a fixture goes wrong silently:
//
// * a record whose hex lines carry fewer or more bytes than it declared is an
//   error, rather than a short read that looks like a truncated datagram the
//   sender is to blame for;
// * the ASCII gutter is verified, not skipped — a reviewer reads the gutter and
//   not the hex, so a gutter that disagrees with its bytes is worse than none;
// * an unknown header key is an error rather than an ignored line, so a typo
//   does not read as an absent field;
// * a declared length above `MaxDatagramBytes` is an error rather than an
//   allocation sized from a corrupt file.
#pragma once

#include "vrmAdapterVmc/api.h"

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

namespace vrmAdapterVmc
{

inline constexpr int PacketCaptureFormatVersion = 1;

// The largest payload one UDP datagram carries over IPv4: 65535 less the 8-byte
// UDP header and the 20-byte IPv4 header. A record declaring more than this
// describes something no socket delivered.
inline constexpr std::size_t MaxDatagramBytes = 65507;

// The bytes the writer emits per hex line. Sixteen is what every hex dump uses,
// so the gutter lines up with what a reviewer's eye expects.
inline constexpr std::size_t PacketCaptureBytesPerLine = 16;

struct RecordedDatagram
{
    // Seconds on the receiver's clock, relative to the start of the recording.
    double receiveTime = 0.0;
    // Verbatim payload. Empty is legal and meaningful: a zero-length UDP
    // datagram is receivable, and it is the smallest thing a decoder must
    // refuse without crashing.
    std::vector<std::uint8_t> bytes;
};

// Provenance plus the datagrams. Every string field is optional; a capture
// carrying none of them still parses, and the corpus manifest is what requires
// provenance of a *committed* fixture.
struct PacketCapture
{
    // The application that produced the packets, for provenance only. Nothing
    // in the decode path may branch on it — a protocol adapter that special
    // cases a sender has become a vendor adapter (§2.1).
    std::string sender;
    std::string sourceId;
    // The endpoints as the recorder observed them, so a replayed capture can
    // report the same `Diagnostic::source` a live session would.
    std::string listenEndpoint;
    std::string peerEndpoint;

    std::vector<RecordedDatagram> datagrams;
};

struct PacketCaptureError
{
    // 1-based; 0 when the failure is not tied to a line (a file that will not
    // open, for instance).
    std::size_t line = 0;
    std::string message;
};

// Parses a capture. On failure `capture` is left untouched and `error`, when
// given, names the line and the reason.
VRMADAPTERVMC_API bool ReadPacketCapture(
    std::istream& input, PacketCapture* capture,
    PacketCaptureError* error = nullptr);

VRMADAPTERVMC_API bool ReadPacketCaptureFile(
    const std::string& path, PacketCapture* capture,
    PacketCaptureError* error = nullptr);

// Writes `capture`. Emission is deterministic — fixed precision, lowercase hex,
// sixteen bytes a line, a gutter on every line, and only the header fields the
// capture actually carries — so re-reading and rewriting a capture this writer
// produced is byte-identical, which is what lets a committed fixture be
// compared rather than merely parsed.
VRMADAPTERVMC_API bool WritePacketCapture(
    std::ostream& output, const PacketCapture& capture);

VRMADAPTERVMC_API bool WritePacketCaptureFile(
    const std::string& path, const PacketCapture& capture);

// The gutter rendering: printable ASCII as itself, everything else as '.'. The
// reader checks a gutter against this, so it is part of the format rather than
// a courtesy of the writer.
VRMADAPTERVMC_API std::string PacketCaptureGutter(
    const std::uint8_t* bytes, std::size_t count);

} // namespace vrmAdapterVmc
