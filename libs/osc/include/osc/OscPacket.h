// SPDX-License-Identifier: Apache-2.0
//
// OSC 1.0 packet decoding, and nothing that uses it.
//
// This layer knows addresses, type tags, arguments, and bundles. It does not
// know that `/VMC/Ext/Bone/Pos` means anything, or `/tracking/trackers/1/
// position`, and that separation is what makes both layers testable: OSC has
// its own malformed-input cases, and a decoder that mixed the two could only
// ever be tested end to end
// (roadmap/adapters-mocopi-vmc-ardy.md §5, roadmap/osc-and-vrchat-trackers.md
// §4).
//
// Four rules are worth stating before the API, because each is a decision
// rather than a detail.
//
// **A datagram decodes entirely or not at all.** A bundle whose third element
// is malformed yields no messages, not two. A half-decoded frame is worse than
// a refused one: the assembler cannot tell which half it got, and the sender
// gets blamed for a frame it sent whole.
//
// **Every OSC 1.0 and 1.1 type tag is understood, even the ones nothing here
// uses.** Skipping an argument requires knowing its size, so a decoder that
// handled only `i`, `f` and `s` — everything VMC sends — would have to refuse a
// message the moment a sender attached a `d` or a `T`. Refusing valid OSC is
// blaming the sender for something it did correctly, so the size table below is
// complete and only a genuinely unknown tag is a refusal.
//
// **The refusal carries no diagnostic code, and that is the contract rather
// than an omission.** A code set is frozen per protocol, before the decoder
// that raises it, so it belongs to an adapter and never to a shared leaf
// (WORKSPACE.md §2). What this layer knows is *which byte* and *which address*
// — so that is what a refusal carries, and the caller that knows which adapter
// it is turns that into `VRM_VMC_PACKET_MALFORMED` or whatever its own frozen
// set spells.
//
// It carries no neutral event *enum* either, where `liveTransport` does, and
// the difference is real rather than stylistic. That library's receiver raises
// two events a caller must tell apart — one adapter maps `BindFailed` and drops
// `Silence`. This one makes a single distinction: a datagram is decodable OSC
// or it is not. Naming three neutral refusals would be a classification
// invented at the boundary, mapped straight back onto one code by every caller,
// and believed by the next reader (roadmap/osc-and-vrchat-trackers.md §8).
//
// **This layer cannot tell an unimplemented address from any other address**,
// because it does not know what an address means. `/foo/bar` and
// `/VMC/Ext/Midi/Note` both decode cleanly here; deciding that neither is
// implemented is the adapter's job, one step up.
#pragma once

#include "osc/api.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace osc
{

// Nested bundles are legal OSC and no sender in this space emits them, so the
// cap is a refusal of the pathological case rather than a limit anyone will
// meet: a datagram can otherwise ask a decoder to recurse as deep as its own
// length allows.
inline constexpr std::size_t MaxOscBundleDepth = 8;

// The NTP time tag every real-time sender uses: "immediately".
inline constexpr std::uint64_t OscTimeTagImmediate = 1;

struct OscBlob
{
    const std::uint8_t* bytes = nullptr;
    std::size_t size = 0;
};

// One argument, tagged with its wire type. Nothing is coerced: a `,f` lands in
// `real` and an `,i` in `integer`, and a caller that wants a float from an int
// argument has found a sender disagreeing with the protocol, which is a
// diagnostic rather than a conversion.
//
//     tag  | wire size            | read from
//     -----+----------------------+---------------------------------
//     i    | 4                    | integer
//     f    | 4                    | real
//     s S  | padded string        | text
//     b    | 4 + padded blob      | blob
//     h    | 8                    | integer
//     t    | 8                    | timeTag
//     d    | 8                    | real
//     c    | 4                    | integer (the character code)
//     r m  | 4                    | integer (the raw 32 bits)
//     T F  | 0                    | integer (1 / 0)
//     N I  | 0                    | nothing
//     [ ]  | 0                    | nothing (array delimiters)
//
// **`t` has its own field, and that is a correction rather than a preference.**
// It used to share `h`'s signed 64-bit path, which made every NTP time tag a
// real sender emits read as a *negative* `integer`: NTP seconds have had their
// high bit set since 1968, so the wrong answer was the normal case rather than
// a far-future edge. Splitting it out rather than widening `integer` is what
// makes the two spellings of one concept agree — a bundle's time tag has always
// landed in `OscPacket::timeTag`, unsigned, and an argument's now lands in a
// field of the same name and the same type. `integer` is left at zero for a
// `t`, on the same rule the rest of this table follows: nothing is coerced, and
// a caller reading the wrong field gets zero rather than a plausible number.
//
// `text` and `blob` point into the datagram the caller passed in. They are
// valid exactly as long as it is: the decoder copies no payload, because a live
// receiver would otherwise allocate a string per bone per frame.
struct OscArgument
{
    char tag = '\0';
    std::int64_t integer = 0;
    // Set for a `t` argument only. Unsigned, because an NTP time tag is.
    std::uint64_t timeTag = 0;
    double real = 0.0;
    std::string_view text;
    OscBlob blob;
};

struct OscMessage
{
    // Points into the caller's datagram, like `OscArgument::text`.
    std::string_view address;
    // The type tag string without its leading comma, so `typeTags.size()` is
    // the argument count for every tag except none.
    std::string_view typeTags;
    std::vector<OscArgument> arguments;
};

// One decoded datagram. Bundles are flattened into wire order, because a frame
// is a set of messages however the sender chose to package it -- and senders
// disagree: one bundles a whole frame, another sends every message on its own
// (adapters/liveCapture/vmc/tests/corpus/README.md). A consumer that had to
// handle both shapes would grow the same loop twice.
struct OscPacket
{
    std::vector<OscMessage> messages;

    // Provenance for the packaging the flattening just discarded. VMC carries
    // its own clock in a message rather than in the time tag, so nothing
    // downstream schedules on this; it is here because dropping what the wire
    // said, silently, is how a format detail becomes unavailable the day it
    // matters.
    bool bundled = false;
    std::uint64_t timeTag = OscTimeTagImmediate;
};

// Why one datagram was refused, in the shape a diagnostic needs minus the three
// things only the caller can supply: its code, and the severity and
// recoverability that code's table decides.
//
// Both fields are plain text a caller pastes into its own diagnostic rather
// than parses. `subject` is the offending address where one had been read
// before the refusal and empty where none had; `detail` says what was wrong and
// ends with the byte it was wrong at, counted from the start of the datagram
// the caller passed in rather than from within some nested bundle element — so
// that an operator can find the byte in a capture instead of bisecting for it.
struct OscDecodeError
{
    std::string subject;
    std::string detail;
};

// Decodes one datagram. On failure `packet` is left untouched and `error`, when
// given, says which byte and which address.
OSC_API bool DecodeOscPacket(const std::uint8_t* bytes, std::size_t size,
                             OscPacket* packet, OscDecodeError* error = nullptr);

inline bool
DecodeOscPacket(const std::vector<std::uint8_t>& datagram, OscPacket* packet,
                OscDecodeError* error = nullptr)
{
    return DecodeOscPacket(datagram.data(), datagram.size(), packet, error);
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
// reference above.
bool DecodeOscPacket(std::vector<std::uint8_t>&& datagram, OscPacket* packet,
                     OscDecodeError* error = nullptr) = delete;

} // namespace osc
