// SPDX-License-Identifier: Apache-2.0
//
// VMC's address patterns, and nothing about a humanoid.
//
// One layer above OSC: this knows that `/VMC/Ext/Bone/Pos` carries a name and a
// transform and that `/VMC/Ext/T` carries the sender's clock. It does not know
// that "LeftUpperArm" is a `motion::HumanBone`, which way the sender's +X
// points, or where a frame begins — those belong to the skeleton map and the
// frame assembler (roadmap/adapters-mocopi-vmc-ardy.md §5), and keeping them
// out is what lets a wire-format question be answered without a rig.
//
// Four rules are decisions rather than details.
//
// **A message is refused, never a packet.** The OSC layer refuses a whole
// datagram because a bad element makes the framing untrustworthy — there is no
// telling which half arrived. Here the framing is already established, so each
// message stands alone: one malformed `/VMC/Ext/Bone/Pos` costs that bone and
// not the twenty-one that came with it. A frame missing one bone is the
// assembler's ordinary business, and a frame missing all of them is not.
//
// **An address this adapter does not implement is not a defect.**
// `VRM_VMC_UNSUPPORTED_MESSAGE` is info and recoverable, and every real sender
// emits traffic in that class — a headset transform, a camera, a MIDI note.
// Holding it apart from `VRM_VMC_PACKET_MALFORMED` is the whole reason the
// mixed-traffic capture is in the corpus.
//
// **A known address whose arguments disagree with the protocol is malformed.**
// `/VMC/Ext/Bone/Pos` is `,sfffffff`; a sender writing `,sddddddd` is not
// writing VMC, and reading the doubles anyway would leave the corpus unable to
// pin the wire format at all. The refusal quotes both tag strings, so a
// sender-compatibility surprise in Milestone B arrives as a legible diagnostic
// rather than as silently plausible numbers.
//
// **Arguments past the known form are counted, never interpreted.** VMC's
// messages grew over time, and longer forms are in the wild: a third string on
// `/VMC/Ext/VRM`, further status integers on `/VMC/Ext/OK`, more floats after
// `/VMC/Ext/Root/Pos`'s quaternion. Refusing those blames a sender for being
// newer; decoding them would be inventing a meaning for bytes no fixture in
// this repository pins. So the extras are reported as a count and left unread,
// and each one moves into the table below when a capture of it exists — which
// is also why this decoder describes the extension by its *shape* and not by
// what it is believed to mean.
#pragma once

#include "vrmAdapterVmc/Diagnostics.h"
#include "vrmAdapterVmc/OscPacket.h"
#include "vrmAdapterVmc/api.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace vrmAdapterVmc
{

// The VMC messages this adapter implements. Values are stable array indices;
// append only before Count.
enum class VmcMessageKind : std::uint8_t
{
    Availability,  // /VMC/Ext/OK
    Time,          // /VMC/Ext/T
    Model,         // /VMC/Ext/VRM
    RootTransform, // /VMC/Ext/Root/Pos
    BoneTransform, // /VMC/Ext/Bone/Pos
    BlendValue,    // /VMC/Ext/Blend/Val
    BlendApply,    // /VMC/Ext/Blend/Apply

    Count,
};

inline constexpr std::size_t VmcMessageKindCount =
    static_cast<std::size_t>(VmcMessageKind::Count);

// The address pattern the kind decodes, e.g. "/VMC/Ext/Bone/Pos". Empty for
// Count.
VRMADAPTERVMC_API std::string_view VmcMessageKindAddress(
    VmcMessageKind kind) noexcept;

// The type tag string the kind's required arguments must begin with, without
// its leading comma — "sfffffff" for a bone. Optional arguments are not in it;
// `unreadArguments` covers everything past the known form.
VRMADAPTERVMC_API std::string_view VmcMessageKindTypeTags(
    VmcMessageKind kind) noexcept;

// Exact match on the whole address. `/VMC/Ext/Bone` and `/VMC/Ext/Bone/Pos/2`
// are not bone poses, and a prefix test would make both into one.
VRMADAPTERVMC_API std::optional<VmcMessageKind> FindVmcMessageKind(
    std::string_view address) noexcept;

// A transform exactly as the wire carries it: the sender's own axes and units,
// and a quaternion in the (x, y, z, w) component order VMC serialises rather
// than the (w, x, y, z) `pxr::GfQuatf` uses. Nothing here is converted,
// normalised, or range-checked — handedness, up axis, units and normalisation
// belong to the skeleton map, which is the layer that knows what the numbers
// are *for*. A decoder that quietly reordered the components would make the
// corpus agree with one downstream reading of it and no other.
struct VmcTransform
{
    std::array<float, 3> position{{0.0f, 0.0f, 0.0f}};
    std::array<float, 4> rotation{{0.0f, 0.0f, 0.0f, 1.0f}};
};

// `/VMC/Ext/OK`. `loaded` is the only field every sender version carries; the
// calibration pair arrived with a later one and is absent rather than
// defaulted, because "the sender did not say" and "the sender said 0" are
// different facts — calibration state 0 means *un*calibrated.
struct VmcAvailability
{
    std::int32_t loaded = 0;
    std::optional<std::int32_t> calibrationState;
    std::optional<std::int32_t> calibrationMode;
};

// One decoded VMC message. Like `OscArgument`, nothing is coerced and nothing
// is shared between kinds: a field the kind does not carry keeps its default,
// so a caller reading the wrong one gets zero rather than a plausible number.
//
//     kind          | address              | known form | fields
//     --------------+----------------------+------------+--------------------
//     Availability  | /VMC/Ext/OK          | ,i[ii]     | availability
//     Time          | /VMC/Ext/T           | ,f         | seconds
//     Model         | /VMC/Ext/VRM         | ,ss        | name (path), title
//     RootTransform | /VMC/Ext/Root/Pos    | ,sfffffff  | name, transform
//     BoneTransform | /VMC/Ext/Bone/Pos    | ,sfffffff  | name, transform
//     BlendValue    | /VMC/Ext/Blend/Val   | ,sf        | name, value
//     BlendApply    | /VMC/Ext/Blend/Apply | ,          | —
//
// `name` and `title` point at the *datagram*, not at the `OscPacket` they were
// read through: the OSC decoder copies no payload either, so both views outlive
// a temporary packet and neither outlives the bytes. Decode a packet from a
// temporary datagram and they dangle — which is what `DecodeOscPacket`'s
// deleted rvalue overload already refuses one step earlier.
//
// The receiver that has not been written yet meets this from a direction no
// overload can refuse: a receive loop with **one reusable buffer** invalidates
// every `name` in an already-decoded `VmcPacket` the next time it calls `recv`.
// Nothing here can detect that, so a caller that keeps a decoded message past
// the datagram must copy the string itself — or, better, convert it to the
// canonical value it was going to become anyway before the next receive.
struct VmcMessage
{
    // Count is the "no message" value, and no decode produces it: a VmcMessage
    // that did not come from the decoder is not a message.
    VmcMessageKind kind = VmcMessageKind::Count;

    // The bone, blend-shape, or root name — or the model path, for Model.
    std::string_view name;
    // Model only.
    std::string_view title;

    // Time only: seconds in the *sender's* clock, which shares no origin with
    // the instant the datagram arrived (tests/corpus/README.md).
    double seconds = 0.0;
    // BlendValue only.
    float value = 0.0f;

    VmcTransform transform;
    VmcAvailability availability;

    // Arguments the sender sent past the known form. Neither an error nor a
    // loss the caller must act on — a signal that this sender speaks a longer
    // form of the message than the table above describes.
    std::size_t unreadArguments = 0;

    // Which message of the OSC packet this was. Filled by `DecodeVmcPacket`, so
    // a caller can name a position inside a bundle and go back to the arguments
    // the known form did not read; left 0 by the single-message overload, which
    // has no packet to count within.
    std::size_t oscIndex = 0;
};

// Every VMC message one decoded datagram carried, in wire order.
struct VmcPacket
{
    std::vector<VmcMessage> messages;

    // Tallies rather than only diagnostics: a live receiver reports "42
    // messages ignored" in its statistics whether or not it kept every
    // diagnostic, and a caller that passes none still has to be able to see
    // that traffic was dropped.
    std::size_t unsupported = 0;
    std::size_t malformed = 0;
};

// One message. Returns false and fills `diagnostic` when the address is not one
// this adapter implements (`VRM_VMC_UNSUPPORTED_MESSAGE`, with the address as
// its subject) or when a known address carries arguments the protocol does not
// describe (`VRM_VMC_PACKET_MALFORMED`). `out` is left untouched on either.
VRMADAPTERVMC_API bool DecodeVmcMessage(const OscMessage& message,
                                        VmcMessage* out,
                                        Diagnostic* diagnostic = nullptr);

// Every message in a decoded datagram, in wire order. Returns false when at
// least one message was *malformed*; an unsupported one leaves the result true,
// because ignoring traffic this adapter does not consume is the normal case and
// not a failure. Either way `out` carries what did decode plus both tallies,
// and `diagnostics`, when given, is appended to — never cleared, so a receive
// loop can accumulate a datagram's worth or a session's. A null `out` is a
// caller bug and is reported as `VRM_VMC_PACKET_MALFORMED` rather than
// dereferenced.
VRMADAPTERVMC_API bool DecodeVmcPacket(
    const OscPacket& packet, VmcPacket* out,
    std::vector<Diagnostic>* diagnostics = nullptr);

} // namespace vrmAdapterVmc
