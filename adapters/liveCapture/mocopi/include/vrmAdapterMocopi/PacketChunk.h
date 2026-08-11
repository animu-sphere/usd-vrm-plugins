// SPDX-License-Identifier: Apache-2.0
//
// The container this product's datagrams are built from, and nothing about
// motion.
//
// Every datagram is a sequence of chunks, and every chunk is
// `<uint32 little-endian payload length><4-byte ASCII tag><payload>`. Chunks
// nest: a payload is itself a sequence of chunks until it is a leaf, and this
// layer cannot tell which is which. It knows lengths and tags. It does not know
// that `fram` is a frame or that `tran` holds a quaternion — those belong to
// `MotionPacket.h`, one step up, for the same reason the sibling adapter keeps
// OSC away from VMC (roadmap/adapters-mocopi-vmc-ardy.md §5, §6): a container
// has its own malformed-input cases, and a decoder that mixed the two could only
// ever be tested end to end.
//
// **This grammar is measured, not remembered, and that is the whole reason it
// may be written down.** The vendor documents the transport and stops — UDP,
// port 12351, IPv4, unencrypted — and says nothing about the payload, so a
// decoder written from a recollection of it would be the BVH-0 failure mode with
// worse consequences: a misread BVH file makes a visibly broken figure, and a
// field read at the wrong offset makes plausible numbers. What is written here
// instead comes from walking 8000 datagrams of five real device sessions and
// checking that the arithmetic *closes* — that consuming `length + 8` bytes per
// chunk lands exactly on the end of the datagram, at every level, on every
// datagram, with no byte left over and none borrowed. A container guess that
// closes 8000 times over four different motions is a measurement. One that does
// not is a wrong guess, and the check was written to be able to say so.
//
// Three independent confirmations that the little-endian reading is the right
// one, none of which the arithmetic alone would give:
//
//   - `ftyp`'s declared length is 18, and the string it carries — "sony motion
//     format" — is exactly 18 characters long.
//   - `rcvp` carries `3f 30`, which little-endian is 12351: the port the
//     vendor's documentation already named, arriving from the payload.
//   - the longest prefix every datagram in a session shares is 77 bytes, and
//     byte 77 is exactly where the two packet kinds first differ — which is the
//     boundary this grammar predicts and not one it was fitted to.
//
// Two rules are decisions rather than details.
//
// **A level is walked, never recursed.** OSC bundles nest to whatever depth a
// datagram cares to declare, so the sibling's decoder needs a depth cap to
// refuse the pathological case. This container nests too, but nothing here
// discovers that: `MotionPacket.h` descends a fixed number of measured levels
// and asks for the chunks of one payload at a time. So there is no recursion to
// bound, and — the part that pays for the extra call — a refusal names the level
// it happened at, rather than a byte offset somewhere inside a tree.
//
// **A tag is reported, never judged.** A four-byte tag this project has never
// seen is not malformed; it is a chunk this decoder does not read, and the
// difference matters because the measurement is of one application version. The
// packet layer counts those and leaves them unread. What *is* refused here is
// only arithmetic: a header that does not fit, a payload that claims more bytes
// than remain, or a walk that ends between chunks instead of on the end.
#pragma once

#include "vrmAdapterMocopi/Diagnostics.h"
#include "vrmAdapterMocopi/api.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace vrmAdapterMocopi
{

// `<uint32 length><4-byte tag>`, ahead of every payload.
inline constexpr std::size_t PacketChunkTagBytes = 4;
inline constexpr std::size_t PacketChunkHeaderBytes = 8;

// The tags the measurement found, at the level each was found on. Named here
// rather than in the packet decoder because they are facts about the container's
// vocabulary, and a test that pins the wire format should be able to spell one
// without linking a decoder to do it.
namespace ChunkTag
{
// Datagram level.
inline constexpr std::string_view Header = "head";
inline constexpr std::string_view SenderInfo = "sndf";
inline constexpr std::string_view Frame = "fram";
inline constexpr std::string_view Skeleton = "skdf";
// Inside `head`: the format's magic and its version byte.
inline constexpr std::string_view FormatType = "ftyp";
inline constexpr std::string_view FormatVersion = "vrsn";
// Inside `sndf`.
inline constexpr std::string_view SenderId = "ipad";
inline constexpr std::string_view ReceivePort = "rcvp";
// Inside `fram`.
inline constexpr std::string_view FrameNumber = "fnum";
inline constexpr std::string_view StreamTime = "time";
inline constexpr std::string_view SenderUnixTime = "uttm";
inline constexpr std::string_view Timecode = "tmcd";
inline constexpr std::string_view BoneTransforms = "btrs";
// Inside `skdf`.
inline constexpr std::string_view BoneDefinitions = "bons";
// One repeated record, inside `btrs` and `bons` respectively.
inline constexpr std::string_view BoneTransformRecord = "btdt";
inline constexpr std::string_view BoneDefinitionRecord = "bndt";
// Inside either record.
inline constexpr std::string_view BoneId = "bnid";
inline constexpr std::string_view ParentBoneId = "pbid";
inline constexpr std::string_view Transform = "tran";
} // namespace ChunkTag

// One chunk of one level.
//
// `tag` and `bytes` point into the buffer the caller passed in and are valid
// exactly as long as it is. Nothing is copied, because a live receiver would
// otherwise allocate once per bone per frame — 27 bones at 60 Hz is the measured
// rate, and the sibling adapter's decoder made the same choice for the same
// reason.
struct PacketChunk
{
    // Four bytes as they arrived. Not necessarily printable: use
    // `PacketChunkTagText` to put one in a message.
    std::string_view tag;
    const std::uint8_t* bytes = nullptr;
    std::size_t size = 0;

    // Where this chunk's *header* began, counted from the start of the
    // **datagram** and not of the payload it was found in. That is what makes a
    // refusal four levels down quotable: "chunk tran at offset 1163" names a byte
    // a reviewer can find in a capture's hex, where a payload-relative offset 8
    // would point at a byte inside `head`. The nested overload below is what
    // carries the base along, and `baseOffset` is how a caller that starts
    // mid-buffer keeps it honest.
    std::size_t offset = 0;
};

// A printable form of a four-byte tag: the tag itself when every byte is
// printable ASCII, and `0x`-prefixed hex when any byte is not. Diagnostics are
// compared by golden tests, so a tag full of control bytes must not be able to
// put a newline or a terminal escape into one.
VRMADAPTERMOCOPI_API std::string PacketChunkTagText(std::string_view tag);

// Walks exactly one level. Returns false and fills `diagnostic`, when given,
// with `VRM_MOCOPI_PACKET_MALFORMED` and a byte offset in its detail; `chunks`
// is cleared either way, so a partial walk cannot be mistaken for a whole one.
//
// `context` names what is being walked ("datagram", "fram", "btdt") and appears
// in the refusal. It is the caller's word rather than a tag read from the bytes,
// because the level that failed to parse is the one thing the bytes cannot say.
// `baseOffset` is added to every `PacketChunk::offset` and to the byte position a
// refusal quotes, so a payload walked out of the middle of a datagram still
// reports datagram-absolute positions. Zero is right for a whole datagram.
VRMADAPTERMOCOPI_API bool DecodePacketChunks(
    const std::uint8_t* bytes, std::size_t size,
    std::vector<PacketChunk>* chunks, std::string_view context = "datagram",
    Diagnostic* diagnostic = nullptr, std::size_t baseOffset = 0);

// Walks the payload of a chunk another walk produced, which is the only way this
// container is descended. The base is that chunk's own offset plus its header, so
// offsets accumulate down the tree instead of restarting at every level.
inline bool DecodePacketChunks(const PacketChunk& chunk,
                               std::vector<PacketChunk>* chunks,
                               Diagnostic* diagnostic = nullptr)
{
    return DecodePacketChunks(chunk.bytes, chunk.size, chunks,
                              PacketChunkTagText(chunk.tag), diagnostic,
                              chunk.offset + PacketChunkHeaderBytes);
}

inline bool DecodePacketChunks(const std::vector<std::uint8_t>& datagram,
                               std::vector<PacketChunk>* chunks,
                               std::string_view context = "datagram",
                               Diagnostic* diagnostic = nullptr)
{
    return DecodePacketChunks(datagram.data(), datagram.size(), chunks, context,
                              diagnostic);
}

// Walking a temporary is always a bug: every chunk's `tag` and `bytes` point
// into the datagram, and a temporary is gone at the end of the full expression
// that produced it. The sibling adapter's OSC decoder learned this the hard way
// — the first test written against it did exactly that, passed on Windows
// because the freed bytes happened to survive, and aborted on Linux and macOS —
// so the same overload is deleted here rather than waiting to be rediscovered.
//
// The default argument is load-bearing: without it a two-argument call would not
// consider this overload at all, and the temporary would bind to the reference
// above.
bool DecodePacketChunks(std::vector<std::uint8_t>&& datagram,
                        std::vector<PacketChunk>* chunks,
                        std::string_view context = "datagram",
                        Diagnostic* diagnostic = nullptr) = delete;

// The first chunk carrying `tag`, or nullptr. Returns the first rather than
// refusing a duplicate: whether two `fram` chunks in one datagram are a protocol
// violation is not a question the container can answer, and the packet decoder
// above does answer it.
VRMADAPTERMOCOPI_API const PacketChunk* FindPacketChunk(
    const std::vector<PacketChunk>& chunks, std::string_view tag) noexcept;

// How many chunks carry `tag`. The packet decoder uses this to refuse a
// duplicate of a field it reads exactly once.
VRMADAPTERMOCOPI_API std::size_t CountPacketChunks(
    const std::vector<PacketChunk>& chunks, std::string_view tag) noexcept;

// Little-endian scalar reads of a leaf payload. Each returns false when the
// payload's length is not exactly the type's width — which is
// `VRM_MOCOPI_PACKET_MALFORMED` territory at the layer above, and the reason
// these do not read a *prefix* of a longer payload: a field that grew is a field
// whose meaning this project has not measured, and reading its first four bytes
// anyway is how a version change becomes a plausible number.
VRMADAPTERMOCOPI_API bool ReadPacketChunkU16(const PacketChunk& chunk,
                                             std::uint16_t* value) noexcept;
VRMADAPTERMOCOPI_API bool ReadPacketChunkI16(const PacketChunk& chunk,
                                             std::int16_t* value) noexcept;
VRMADAPTERMOCOPI_API bool ReadPacketChunkU32(const PacketChunk& chunk,
                                             std::uint32_t* value) noexcept;
VRMADAPTERMOCOPI_API bool ReadPacketChunkF32(const PacketChunk& chunk,
                                             float* value) noexcept;
VRMADAPTERMOCOPI_API bool ReadPacketChunkF64(const PacketChunk& chunk,
                                             double* value) noexcept;

} // namespace vrmAdapterMocopi
