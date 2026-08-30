// SPDX-License-Identifier: Apache-2.0
//
// VRChat's tracker addresses, and nothing about a body.
//
// One layer above OSC: this knows that `/tracking/trackers/1/position` carries
// three floats and that the segment before the channel identifies a tracker. It
// does not know which body region that tracker is on, which way its axes point,
// what unit its numbers are in, or where a frame begins — those are
// assignment's, VRC-3's, VRC-3's and VRC-4's respectively
// (roadmap/osc-and-vrchat-trackers.md §5, §5.1). Keeping them out is what lets
// a wire-format question be answered without a calibration.
//
// It is the sibling of `vrmAdapterVmc`'s `VmcMessage.h`, one adapter over, and
// three of that file's four rules hold here verbatim. The fourth is reversed,
// and the reversal is the interesting line in this header.
//
// **A message is refused, never a packet.** The OSC layer refuses a whole
// datagram because a bad element makes the framing untrustworthy. Here the
// framing is established, so each message stands alone — which on this wire is
// nearly a tautology, since the sender measured in
// [report 02](../../../../../docs/reports/motion/02-2026-08-30-vrchat-osc-address-inventory.md)
// puts exactly one message in every datagram and never bundles. It is written
// this way for the sender that does not: a bundled frame is legal OSC, this
// adapter must read one, and a bundle whose fourth element is malformed costs
// that element.
//
// **An address this adapter does not implement is not a defect.**
// `VRM_VRCHAT_OSC_UNSUPPORTED_ADDRESS` is info and recoverable. VRChat's OSC
// surface is far larger than the tracker subset read here — avatar parameters,
// chatbox, eye tracking, input — and a session carrying them alongside tracker
// data is the ordinary case rather than a fault. This is also the code that
// keeps the port shareable: 9000 is a well-known one and anything on the
// network may send to it.
//
// **A known address whose arguments disagree is `ARGUMENT_MISMATCH`**, and the
// refusal quotes both type tag strings, so a sender-compatibility surprise
// arrives as a legible diagnostic rather than as silently plausible numbers.
//
// **Arguments past the known form are refused here, where the sibling counts
// them** — and this is the reversal. `VmcMessage.h` reads the form it knows and
// counts the rest, because VMC's messages grew by *appending* fields to
// messages whose leading form stayed what it was: a longer `/VMC/Ext/OK` says
// more about the same thing. On this wire the arity of the arguments **is** the
// open question. A rotation arrives as three floats
// ([report 02](../../../../../docs/reports/motion/02-2026-08-30-vrchat-osc-address-inventory.md) §1),
// which is Euler; a four-float rotation is a quaternion, and reading its first
// three components as Euler angles is not a partial read of a longer form but a
// confident misreading of a different one. So `,fff` exactly, and a `,ffff`
// rotation is a refusal that names what it saw. The day a sender emitting one
// is measured, that measurement — not this comment — is what adds the form.
//
// ## Why the decoded type is a `TrackerMessage` and not a `TrackerSample`
//
// [§5](../../../../../docs/roadmap/osc-and-vrchat-trackers.md#5-a-tracker-source-is-not-a-pose-source)
// names `TrackerSample { trackerId, position, rotation, ... }` as this
// adapter's intermediate, and VRC-2 is written as decoding to one. It decodes
// to half of one, and the difference is this wire's rather than a liberty:
// **position and rotation arrive in separate datagrams**, so no single message
// can fill both halves of a sample. A decoder that returned a `TrackerSample`
// per message would fill the other half with a default, and a defaulted
// rotation of (0, 0, 0) is bit-for-bit the rotation a tracker at rest actually
// reports — the reader could not tell the invented value from the measured one.
// That is the same "a caller reading the wrong field gets zero rather than a
// plausible number" rule `OscArgument` and `VmcMessage` are both built on, met
// at the one layer where the plausible number would have been *correct-looking*.
//
// So the split is: this file decodes one message into one channel of one
// tracker, and VRC-4 — which owns the window a frame is assembled over, and is
// therefore the only layer that can say a position and a rotation belong
// together — is where a `TrackerSample` is constructed and where
// `VRM_VRCHAT_OSC_TRACKER_PARTIAL` is raised. That code is not raised anywhere
// in this file, deliberately: a single message is *always* partial, and a layer
// that reported it would raise a warning about once a datagram forever.
#pragma once

#include "vrmAdapterVrchatOsc/Diagnostics.h"
#include "vrmAdapterVrchatOsc/api.h"

#include "osc/OscPacket.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace vrmAdapterVrchatOsc
{

// The address family this adapter reads, with its trailing slash so that a
// prefix test cannot match `/tracking/trackersfoo/...`.
inline constexpr std::string_view TrackerAddressPrefix = "/tracking/trackers/";

// The one non-numeric tracker identity VRChat's surface defines, and the row
// VRC-1 exists to have found: it sits in the same path position as `1`, `2` and
// `3`, so a decoder that read that segment as an integer would drop the head
// and report nothing wrong
// ([report 02](../../../../../docs/reports/motion/02-2026-08-30-vrchat-osc-address-inventory.md) §6).
inline constexpr std::string_view HeadTrackerSegment = "head";

// The numbered range VRChat's tracker surface defines. The recorded sender uses
// 1–3 and no capture in this repository contains a 4–8, so accepting them is a
// decision about *VRChat's surface* rather than about that sender: the
// alternative — accepting only what has been observed — would make this decoder
// refuse a legal address the first time anyone connects a six-point setup, and
// call it a protocol violation. What is *not* accepted is 0, which the surface
// does not define, and 9 and above.
inline constexpr std::uint8_t MinTrackerIndex = 1;
inline constexpr std::uint8_t MaxTrackerIndex = 8;

// Which half of a tracker's transform a message carries. Values are stable
// array indices; append only before Count.
enum class TrackerChannel : std::uint8_t
{
    Position,
    Rotation,

    Count,
};

inline constexpr std::size_t TrackerChannelCount =
    static_cast<std::size_t>(TrackerChannel::Count);

// The address segment the channel is spelled with, e.g. "position". Empty for
// Count.
VRMADAPTERVRCHATOSC_API std::string_view TrackerChannelString(
    TrackerChannel channel) noexcept;

VRMADAPTERVRCHATOSC_API std::optional<TrackerChannel> FindTrackerChannel(
    std::string_view segment) noexcept;

// Which tracker a message is about, as the address spelled it.
//
// **The identity is the segment, and the index is a reading of it.** Both are
// kept because both are true and neither implies the other: `head` has no
// index, and a caller that keyed a table on the index alone would collapse the
// head onto tracker 0 or drop it. Ordering, grouping and equality are the
// segment's throughout this adapter, so the head is never a special case in any
// of them — it is a tracker whose name is not a number.
//
// `segment` points into the datagram the message was decoded from, exactly as
// `osc::OscMessage::address` does, and is valid for exactly as long. A caller
// that keeps a `TrackerId` past the buffer it came from — a receive loop with
// one reusable buffer is the case that will meet this — must copy the text.
struct TrackerId
{
    // Verbatim, so an identity this adapter cannot read is still reportable.
    std::string_view segment;
    // Set when `segment` is a decimal in [MinTrackerIndex, MaxTrackerIndex].
    std::optional<std::uint8_t> index;

    // True for `head`: an identity the surface names rather than numbers.
    bool named() const noexcept { return !index.has_value(); }
};

inline bool
operator==(const TrackerId& lhs, const TrackerId& rhs) noexcept
{
    return lhs.segment == rhs.segment;
}

inline bool
operator!=(const TrackerId& lhs, const TrackerId& rhs) noexcept
{
    return !(lhs == rhs);
}

// One decoded tracker message: which tracker, which channel, three floats.
//
// The floats are **verbatim**. Nothing here is converted, negated, reordered,
// scaled or range-checked beyond finiteness: VRChat's tracking space is
// documented as Unity's — left-handed, +Y up, metres, Euler rotations — and a
// documented basis is a hypothesis until a recorded rest pose agrees with it,
// which is VRC-3's job and
// [the handedness episode](../../../../../docs/roadmap/adapters-mocopi-vmc-ardy.md#96-cross-source-comparison)'s
// precedent. A decoder that quietly applied the documented conversion would
// make the corpus agree with one reading of it and no other, and would leave
// VRC-3 nothing to verify against.
//
// Which is also why the field is called `values` and not `position` /
// `eulerAngles`: at this layer they are three numbers off a wire, and the
// channel says which address they arrived on.
struct TrackerMessage
{
    TrackerId tracker;
    TrackerChannel channel = TrackerChannel::Position;
    std::array<float, 3> values{{0.0f, 0.0f, 0.0f}};
};

// One datagram, decoded at this layer.
//
// The counts are not derived from the vectors, for the reason the address
// inventory's are not: a reader can see `messagesSeen != messages.size() +
// diagnostics.size()` and know this file has an arithmetic bug, rather than
// take the arithmetic on trust.
struct TrackerPacket
{
    // In wire order — including a bundle's, flattened, which is the order the
    // sender chose to package rather than any frame order.
    std::vector<TrackerMessage> messages;

    // One per refused message, in wire order. Every one carries a code from
    // this adapter's frozen set and a subject that is the OSC address, because
    // that is what this layer has: it reports what the wire said, and this wire
    // says `/tracking/trackers/4/position` rather than a body part.
    std::vector<Diagnostic> diagnostics;

    std::size_t messagesSeen = 0;
    // Of the diagnostics, how many are `UnsupportedAddress`. Held apart because
    // it is the one refusal that is not a complaint about the sender, and a
    // session made entirely of it — a VRChat client sending avatar parameters
    // and no trackers — is a legitimate reading rather than a broken stream.
    std::size_t unsupported = 0;

    // Set by the datagram overload only, when the bytes are not decodable OSC
    // at all. The whole datagram is then refused: `messages` is empty and
    // `diagnostics` holds one `PacketMalformed` carrying the shared decoder's
    // own subject and detail.
    bool refused = false;
    // Provenance for the packaging `osc` flattened away, forwarded rather than
    // re-derived. No sender measured here bundles; a fixture does, so that the
    // claim is checked rather than assumed.
    bool bundled = false;
};

// Decodes one message. On refusal returns false and fills `error`, when given,
// with the code, subject and detail; `out` is left untouched.
//
// The order of the checks is the order the bytes are read — address family,
// channel, tracker identity, type tags, values — so a message that is wrong in
// two ways is reported as the first one, which is the one a sender would fix
// first. A `/tracking/trackers/99/position` carrying doubles is a tracker id
// failure, not an argument failure.
//
// **Two structural guards precede all of that**, and they are a different kind
// of thing: a null `out`, and a message whose `arguments` and `typeTags`
// disagree in length. Neither can come from `osc::DecodeOscPacket` — it emits
// one argument per tag, including the zero-width ones — so both are a caller's
// mistake rather than a sender's, and both raise `PacketMalformed`. The second
// is load-bearing rather than defensive: the type tag check below establishes
// that there are three tags, and the values loop indexes `arguments` on the
// strength of it, which is sound only while the two agree.
VRMADAPTERVRCHATOSC_API bool DecodeTrackerMessage(
    const osc::OscMessage& message, TrackerMessage* out,
    Diagnostic* error = nullptr);

// Decodes every message in an already-decoded packet. Never fails: a packet of
// nothing but unsupported addresses is a `TrackerPacket` with no messages,
// which is a reading and not an error.
VRMADAPTERVRCHATOSC_API TrackerPacket DecodeTrackerPacket(
    const osc::OscPacket& packet);

// Decodes one datagram, mapping the shared decoder's neutral refusal onto this
// adapter's `VRM_VRCHAT_OSC_PACKET_MALFORMED`. That map is the adapter's half
// of OSC-3's split — a refusal carries no code, and the caller that knows which
// adapter it is supplies one
// ([§8](../../../../../docs/roadmap/osc-and-vrchat-trackers.md#8-diagnostics)).
//
// Neither overload stamps `source` or `timestamp` on a diagnostic: a decoder
// knows neither where the bytes came from nor when they arrived. The caller
// holding the capture or the socket fills both, as `InventoryAddresses` does.
VRMADAPTERVRCHATOSC_API TrackerPacket DecodeTrackerDatagram(
    const std::uint8_t* bytes, std::size_t size);

inline TrackerPacket
DecodeTrackerDatagram(const std::vector<std::uint8_t>& datagram)
{
    return DecodeTrackerDatagram(datagram.data(), datagram.size());
}

// Decoding a temporary is always a bug: every `TrackerId::segment` in the
// result points into the datagram. `osc::DecodeOscPacket` refuses the same
// thing one layer down, and the refusal has to be repeated here or this
// overload set would quietly re-open the hole it closed.
TrackerPacket DecodeTrackerDatagram(std::vector<std::uint8_t>&& datagram) =
    delete;

} // namespace vrmAdapterVrchatOsc
