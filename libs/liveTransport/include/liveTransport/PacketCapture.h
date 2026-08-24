// SPDX-License-Identifier: Apache-2.0
//
// A recorded packet capture: the on-disk form of what a socket received.
//
// This is the reader and writer two adapters had written twice — 44 header
// lines and 366 implementation lines apiece, differing by six
// (roadmap/osc-and-vrchat-trackers.md §2). Recording datagrams is not a
// protocol, which is why the census put this file at the opposite end of its
// table from `FrameAssembler`.
//
// It is deliberately *not* a `motion-capture-trace`. The two formats sit at
// opposite ends of an adapter and neither substitutes for the other:
//
//     packets in  ->  [ <sender>-packet-capture ]  ->  decode  ->  map
//                 ->  [ motion-capture-trace ]     ->  the canonical pipeline
//
// A trace records what an adapter *produced* — canonical poses, in capture
// order, with arrival order deliberately discarded. A capture records what the
// adapter was *given*, in arrival order, including the packets it will refuse.
// Recording a trace instead would mean a decoder's own tests are fed by the
// decoder, and every packet-level failure — a truncated datagram, a duplicate,
// a source restart mid-frame — would be untestable because a trace cannot
// represent one.
//
// ## One format, and a magic line that is still per adapter
//
// The magic is a parameter rather than a constant, and that is the one place
// this extraction did not converge. Both committed corpora name their producer
// in their first token — `!vmc-packet-capture`, `!mocopi-packet-capture` — and
// a single `!live-packet-capture` would have made every committed fixture a
// rewrite, which is the constraint §3.2 states. It also keeps a property worth
// keeping on its own: a capture of one protocol handed to the other protocol's
// decoder fails at the first line with a clear message, rather than at the
// first field with a malformed-packet diagnostic blamed on a source that did
// nothing wrong.
//
// The header *vocabulary*, by contrast, is one vocabulary. `device` was
// mocopi's alone and is now everyone's — an adapter that has nothing to put
// there writes nothing, since the writer emits only the fields a capture
// actually carries, so no committed fixture changes a byte. The one behaviour
// this widens is that a VMC capture carrying `device` now parses instead of
// being refused as an unknown key; keeping it refused would have meant a
// per-adapter key list, which is a knob for one optional field and exactly the
// per-caller difference this library exists to stop carrying.
//
// ## The format
//
// Line-oriented text, for the same reason the trace format is: a fixture is
// reviewed in a pull request, so it has to diff. Bytes are hex with an ASCII
// gutter, which is what makes an address pattern or a binary field tag legible
// in a review without a decoder ring:
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
// with arrival order.
//
// `sender` and `device` are provenance only. Nothing in a decode path may
// branch on either, because a protocol adapter that special-cases a source has
// become something narrower than a protocol adapter — and this library, which
// may not know a protocol at all, only stores them.
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

#include "liveTransport/api.h"

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <string_view>
#include <vector>

namespace liveTransport
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
// carrying none of them still parses, and a corpus manifest is what requires
// provenance of a *committed* fixture.
struct PacketCapture
{
    // The application that produced the packets, for provenance only.
    std::string sender;
    // The hardware behind the application, where a source has one and says so.
    // An adapter over a relay leaves it empty and the writer omits the line.
    std::string device;
    std::string sourceId;
    // The endpoints as the recorder observed them, so a replayed capture can
    // report the same diagnostic `source` a live session would.
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

// Parses a capture whose first token must be `magic`. On failure `capture` is
// left untouched and `error`, when given, names the line and the reason.
LIVETRANSPORT_API bool ReadPacketCapture(
    std::string_view magic, std::istream& input, PacketCapture* capture,
    PacketCaptureError* error = nullptr);

LIVETRANSPORT_API bool ReadPacketCaptureFile(
    std::string_view magic, const std::string& path, PacketCapture* capture,
    PacketCaptureError* error = nullptr);

// Writes `capture` under `magic`. Emission is deterministic — fixed precision,
// lowercase hex, sixteen bytes a line, a gutter on every line, and only the
// header fields the capture actually carries — so re-reading and rewriting a
// capture this writer produced is byte-identical, which is what lets a
// committed fixture be compared rather than merely parsed.
LIVETRANSPORT_API bool WritePacketCapture(
    std::string_view magic, std::ostream& output,
    const PacketCapture& capture);

LIVETRANSPORT_API bool WritePacketCaptureFile(
    std::string_view magic, const std::string& path,
    const PacketCapture& capture);

// The gutter rendering: printable ASCII as itself, everything else as '.'. The
// reader checks a gutter against this, so it is part of the format rather than
// a courtesy of the writer.
LIVETRANSPORT_API std::string PacketCaptureGutter(
    const std::uint8_t* bytes, std::size_t count);

} // namespace liveTransport
