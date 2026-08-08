// SPDX-License-Identifier: Apache-2.0
//
// A recorded mocopi packet capture: the on-disk form of what the socket
// received.
//
// The implementation order for this adapter puts transport last
// (roadmap/adapters-mocopi-vmc-ardy.md §6), which only works if there is
// something to decode without one. This is that something: the datagrams a
// session delivered, verbatim, with the instant each arrived.
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
// Recording a trace instead would mean the decoder's own tests are fed by the
// decoder, and every packet-level failure — a truncated datagram, a duplicate,
// a source restart mid-frame — would be untestable because a trace cannot
// represent one.
//
// ## Why this is a second format and not the sibling adapter's
//
// The VMC adapter has a capture format of the same shape, and this one repeats
// it rather than sharing it. That is a decision, and the alternatives lost on
// the contract rather than on taste:
//
// * **Reaching the sibling's header is forbidden.** WORKSPACE.md §2 gives an
//   adapter exactly two edges, and adapter plan §2.1 says the two live adapters
//   are siblings and never a stack. A shared reader would either be that edge or
//   would need a *new* library — and one PR never introduces a boundary and a
//   large feature together (recorded-motion-sources.md §12).
// * **`motionRuntime` is the wrong home**, though it is the one library both
//   adapters may reach. A recorded-datagram format is a transport artifact —
//   its entire content is what a socket delivered — and a socket in the runtime
//   is exactly what that library's own boundary check refuses.
// * **The magic line is the fixture's type tag**, and having two of them is a
//   feature. A capture of one protocol handed to the other protocol's decoder
//   fails at the first line with a clear message rather than at the first field
//   with a malformed-packet diagnostic blamed on a source that did nothing.
//
// This follows the answer the semantic clip writer already gave in this
// repository: a repeated shape, with the condition that would change it written
// down. What changes it is a *third* recorder — a third live adapter, or a tool
// that must read both formats — at which point the shape is a library and its
// home is the boundary question above, argued in its own change.
//
// ## The format
//
// Line-oriented text, for the same reason the trace format is: a fixture is
// reviewed in a pull request, so it has to diff. Bytes are hex with an ASCII
// gutter, which is what makes a binary protocol's field tags legible in a
// review without a decoder ring:
//
//     # a comment
//     !mocopi-packet-capture 1
//     sender example.synthetic
//     device example.synthetic
//     sourceId neutral-standing-01
//     listen 0.0.0.0:12351
//     peer 192.168.0.20:52001
//
//     d 0.000000 16
//       00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f  |................|
//
// `d <receiveTime> <length>` opens a record and the hex lines that follow carry
// exactly `length` bytes. Times are seconds on the *receiver's* clock, relative
// to the start of the recording, and must not go backwards: arrival order is
// the whole point of this format, and a clock that runs backwards is a recorder
// defect rather than a phenomenon to reproduce. What the source believed the
// time was lives inside the payload, where a decoder can find it disagreeing
// with arrival order — which is what `VRM_MOCOPI_TIMESTAMP_INVALID` reports.
//
// `device` is the one header key the sibling format does not have, and it is
// here for the reason this adapter exists at all. The native path's whole claim
// is that it keeps device and sensor state a protocol relay drops (§6), and a
// capture that cannot say which device produced it cannot support that claim
// later. Like `sender`, it is provenance only: nothing in the decode path may
// branch on either, because a protocol adapter that special-cases a source has
// become something narrower than a protocol adapter.
//
// The reader is strict, in the four ways a fixture goes wrong silently:
//
// * a record whose hex lines carry fewer or more bytes than it declared is an
//   error, rather than a short read that looks like a truncated datagram the
//   source is to blame for;
// * the ASCII gutter is verified, not skipped — a reviewer reads the gutter and
//   not the hex, so a gutter that disagrees with its bytes is worse than none;
// * an unknown header key is an error rather than an ignored line, so a typo
//   does not read as an absent field;
// * a declared length above `MaxDatagramBytes` is an error rather than an
//   allocation sized from a corrupt file.
#pragma once

#include "vrmAdapterMocopi/api.h"

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

namespace vrmAdapterMocopi
{

inline constexpr int PacketCaptureFormatVersion = 1;

// The largest payload one UDP datagram carries over IPv4: 65535 less the 8-byte
// UDP header and the 20-byte IPv4 header. A record declaring more than this
// describes something no socket delivered. IPv4 is the right bound rather than
// a simplification here — the source's own documentation states that IPv6 and
// `localhost` are not supported.
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
    // The application that produced the packets, and the hardware behind it.
    // Provenance only, both of them (see the header comment).
    std::string sender;
    std::string device;
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
VRMADAPTERMOCOPI_API bool ReadPacketCapture(
    std::istream& input, PacketCapture* capture,
    PacketCaptureError* error = nullptr);

VRMADAPTERMOCOPI_API bool ReadPacketCaptureFile(
    const std::string& path, PacketCapture* capture,
    PacketCaptureError* error = nullptr);

// Writes `capture`. Emission is deterministic — fixed precision, lowercase hex,
// sixteen bytes a line, a gutter on every line, and only the header fields the
// capture actually carries — so re-reading and rewriting a capture this writer
// produced is byte-identical, which is what lets a committed fixture be
// compared rather than merely parsed.
VRMADAPTERMOCOPI_API bool WritePacketCapture(
    std::ostream& output, const PacketCapture& capture);

VRMADAPTERMOCOPI_API bool WritePacketCaptureFile(
    const std::string& path, const PacketCapture& capture);

// The gutter rendering: printable ASCII as itself, everything else as '.'. The
// reader checks a gutter against this, so it is part of the format rather than
// a courtesy of the writer.
VRMADAPTERMOCOPI_API std::string PacketCaptureGutter(
    const std::uint8_t* bytes, std::size_t count);

} // namespace vrmAdapterMocopi
