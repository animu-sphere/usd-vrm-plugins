// SPDX-License-Identifier: Apache-2.0
//
// This product's two packet kinds, and nothing about a humanoid.
//
// One layer above `PacketChunk.h`: this knows that `fram` carries a frame
// counter, two clocks and a bone record per joint, and that `skdf` carries a
// parent column and a rest pose. It does not know that bone 12 is an upper arm,
// which way the device's +X points, or where a frame begins — those belong to
// the skeleton map, the coordinate converter and the frame assembler
// (roadmap/adapters-mocopi-vmc-ardy.md §6), and keeping them out is what lets a
// wire-format question be answered without a rig.
//
// ## The grammar, as measured
//
// Five real device sessions, 8000 datagrams, four different motions. Every
// datagram is one of exactly two shapes, and no session grew a third:
//
//     1605 B  head(35) sndf(26) fram(1520)    <- a frame,    x 7964
//     1821 B  head(35) sndf(26) skdf(1736)    <- a skeleton, x   36
//
//     head -> ftyp(18) "sony motion format", vrsn(1) 0x01
//     sndf -> ipad(8), rcvp(2) = 12351
//     fram -> fnum(4), time(4), uttm(8), tmcd(6), btrs(1458)
//     btrs -> btdt(46) x 27  ->  bnid(2), tran(28)
//     skdf -> bons(1728)
//     bons -> bndt(56) x 27  ->  bnid(2), pbid(2), tran(28)
//
// `tran` is seven IEEE-754 binary32 values: four of rotation, then three of
// translation. Which four is measured rather than assumed — the first four are
// unit-norm to 4.2e-08 across all 215,028 bone-frames and the last three are
// not, and in the skeleton packet the fourth component is exactly 1.0 while the
// first three are exactly 0.0, so the scalar is **last**.
//
// ## What the numbers are, and what is deliberately still unknown
//
// The translation triple is metres in a +Y-up, +Z-forward basis, and all three
// of those come from the rest pose rather than from a document: the root sits
// 0.96 up, which is a hip height in metres and nothing else; the upper-arm and
// forearm offsets are 0.30 and 0.25; the toe sits forward of the ankle on +Z.
// The rotation's imaginary parts are in the same component order as the
// translation, which is one more measurement and not an assumption — the rest
// pose holds the arms along ±X, a neutral standing frame has them hanging down,
// and the component that carries that ~85° rotation is index 2, which is the Z
// a rotation from +X to -Y must turn about.
//
// **Handedness is not resolved here, and that is on purpose.** A mirrored basis
// satisfies every measurement above, because a mirrored skeleton is
// geometrically identical — the two 4-bone limbs off the root and the two off
// the top of the torso stay legs and arms either way. Settling it takes an
// asymmetric motion whose side is *labelled*, which is an operator's session and
// not a commit, and until then a decoder that named these components x, y, z
// would be publishing a guess about which arm is which. So this layer reports
// components and the coordinate converter resolves them, which is also where the
// sibling adapter draws the same line for the same reason.
//
// ## Four rules that are decisions
//
// **A bone is refused, never a frame.** The container closed before this layer
// ran, so a frame's framing is not in doubt and one unusable bone record costs
// that bone and not the twenty-six that arrived with it. This is the opposite of
// what `PacketChunk.h` does and the difference is real: there, a bad length
// makes every following byte untrustworthy. Here it makes one joint
// untrustworthy, and refusing the datagram to punish it would throw away a frame
// the device sent correctly. Whether 26 of 27 bones is a usable frame is the
// assembler's question — `VRM_MOCOPI_FRAME_INCOMPLETE` is its code, not this
// layer's — so the decode still succeeds and says what it dropped.
//
// **An unknown `vrsn` is refused, and this is the one place where blaming a
// newer sender is correct.** Every other unmeasured thing here is carried
// forward unread, because refusing it would blame an application for being newer
// than the measurement. A version byte is different: it exists so a producer can
// say the layout changed, and reading a v2 `fram` with v1's field offsets is
// exactly the "plausible numbers" failure this whole path is ordered to avoid.
// A loud refusal costs one measured capture and one constant to fix; a silent
// misread costs the trust in every number downstream of it.
//
// **A chunk this decoder does not read is counted, never interpreted.** The
// measurement is of one application version on one device, so the vocabulary
// above is a floor and not a ceiling. `tmcd` is the case already in hand: six
// bytes, present in all 7964 frame packets, and **zero in every one of them**.
// Something that has never been observed non-zero has no measured meaning, so it
// is carried as bytes and no field of this struct claims to be a timecode.
//
// That rule is about chunks this decoder has never *seen*, and it does not extend
// to the width of one it has. A `tmcd` of four bytes is refused, even though
// nothing here reads the field — because within a version the field widths *are*
// the format, and `vrsn` is the thing a producer bumps when they change. So the
// two rules meet cleanly rather than contradicting: an unknown tag is carried, a
// known tag at an unmeasured width is a v1 packet that is not a v1 packet, and a
// genuinely new layout announces itself one field earlier.
//
// **`bnid` is an index, and duplicates are not this layer's business.** It
// arrived as 0..26 in order in all 8000 datagrams, and the decoder still neither
// requires the order nor the count: a rig with fingers would be a longer list,
// not a malformed one. That is also why the frozen diagnostic set has no
// duplicate-bone code where the sibling's does — VMC names a bone with a string
// that two messages can repeat, and here the position in a fixed-length record
// list is the name.
//
// ## Two clocks, a counter, and what a gap means
//
// `time` is binary32 seconds from the start of the stream — exactly 0.0 in the
// first frame of all five sessions — and `uttm` is binary64 seconds of Unix
// time. Both are the *sender's*, and neither shares an origin with the instant a
// datagram arrived. `uttm` was confirmed against something outside the protocol:
// it decodes to 2026-08-11 16:58:13 UTC in the first named session, which is
// when that file was written. The two agree with each other to within 2 µs over
// 33 s, so a caller has a drift check it did not have to build.
//
// `fnum` counts the device's frames since the application started, not the
// stream's: it began at 1928, 4495, 10231 and 21414 in four sessions whose
// `time` all began at 0.0. **A gap in it is transport loss and not a protocol
// event**, which is measured and not assumed — two of the four sessions lost 14
// and 8 datagrams to Wi-Fi, and every gap's `time` delta was exactly the missing
// count times 1/60 s. The sender's clock never skipped. So nothing here refuses
// a gap; a counter running *backwards* is what a restart looks like, and no
// single packet can see that either.
#pragma once

#include "vrmAdapterMocopi/Diagnostics.h"
#include "vrmAdapterMocopi/PacketChunk.h"
#include "vrmAdapterMocopi/api.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace vrmAdapterMocopi
{

// `head/ftyp`. The format's magic, and the only string in the protocol.
inline constexpr std::string_view MotionPacketFormatType = "sony motion format";

// `head/vrsn`. The only value ever measured; see the header's third rule for why
// anything else is refused rather than attempted.
inline constexpr std::uint8_t MotionPacketFormatVersion = 1;

// What the device sent, in every measured session. Not a requirement and not a
// bound — the decoder reads the list the packet carries — but a fixture that
// disagrees with it is describing a rig this project has not seen, which is
// worth a test noticing.
inline constexpr std::size_t MeasuredBoneCount = 27;

// The width of `tran`: four rotation components then three of translation.
inline constexpr std::size_t BoneTransformFloats = 7;

// `sndf/ipad`, eight bytes, constant for a whole session and identical across
// all five measured ones.
inline constexpr std::size_t SenderIdBytes = 8;

// `fram/tmcd`, six bytes, zero in every frame ever measured.
inline constexpr std::size_t TimecodeBytes = 6;

enum class MotionPacketKind : std::uint8_t
{
    Frame,    // `fram`
    Skeleton, // `skdf`

    Count,
};

// The tag the kind is carried under, e.g. "fram". Empty for Count.
VRMADAPTERMOCOPI_API std::string_view MotionPacketKindTag(
    MotionPacketKind kind) noexcept;

// A transform exactly as the wire carries it: the device's own axes and units,
// and a quaternion in the scalar-last component order this protocol serialises
// rather than the (w, x, y, z) `pxr::GfQuatf` uses. Nothing here is converted,
// normalised, or reordered — handedness, up axis, units and normalisation belong
// to the layers that know what the numbers are *for*. A decoder that quietly
// reordered the components would make the corpus agree with one downstream
// reading of it and no other.
//
// The naming stops short of x/y/z on purpose; the header says why.
struct BoneTransform
{
    std::array<float, 4> rotation{{0.0f, 0.0f, 0.0f, 1.0f}};
    std::array<float, 3> translation{{0.0f, 0.0f, 0.0f}};
};

// One `btdt`: a joint's pose in a frame.
struct BoneFrame
{
    std::uint16_t boneId = 0;
    BoneTransform transform;
};

// One `bndt`: a joint's place in the hierarchy and its rest pose.
struct BoneDefinition
{
    std::uint16_t boneId = 0;
    // Signed on the wire, and -1 is the root's sentinel. In every measured
    // session the column was a single-rooted tree already in topological order,
    // and this layer reports it without checking either: a hierarchy is the
    // skeleton map's to validate, and a decoder that refused a shape it had only
    // ever seen one of would be refusing rigs rather than packets.
    std::int16_t parentBoneId = -1;
    BoneTransform restTransform;
};

// `head` and `sndf`, which every packet of both kinds carries identically.
//
// **`senderId` is provenance and nothing may branch on it.** Eight bytes,
// constant per session, unidentified — so it is treated as possibly
// device-identifying: it stays out of committed fixtures and out of anything
// this project publishes, exactly like the peer address the capture format
// already withholds. `receivePort` is the port the device believes it is sending
// to, which is the sender's opinion and not the socket's fact.
struct MotionPacketProvenance
{
    // Points into the caller's datagram, like `PacketChunk::tag`.
    std::string_view formatType;
    std::uint8_t formatVersion = 0;
    std::array<std::uint8_t, SenderIdBytes> senderId{};
    std::uint16_t receivePort = 0;
};

// `fram`.
struct MotionFrame
{
    // The device's frame counter since its application started. A gap is
    // transport loss; see the header.
    std::uint32_t frameNumber = 0;
    // Seconds since the stream began, in the sender's clock.
    float streamSeconds = 0.0f;
    // Unix seconds, in the sender's clock.
    double senderUnixSeconds = 0.0;
    // `tmcd`, unread. Never observed non-zero, so no accessor pretends to know
    // what a non-zero value would mean.
    std::array<std::uint8_t, TimecodeBytes> timecode{};

    // In wire order. Bones whose transform was refused are absent, and
    // `MotionPacket::refusedBones` counts them.
    std::vector<BoneFrame> bones;
};

// `skdf`. Byte-identical in every skeleton packet of a session — 36 of them
// across the five measured sessions, one distinct table — and it repeats about
// every 3.5 s rather than arriving once at the start, so a capture longer than
// four seconds contains one and "start recording before the device" is not a
// corpus requirement.
struct MotionSkeleton
{
    std::vector<BoneDefinition> bones;
};

// A chunk the decoder walked past without reading. Neither an error nor a loss
// the caller must act on — a signal that this sender speaks a longer form of the
// container than the measurement describes.
struct UnreadChunk
{
    // Both point into the caller's datagram. `container` is the tag of the
    // chunk this one was found inside, or "datagram" at the top level, so a
    // surprise can be located without a byte offset nobody can read.
    std::string_view tag;
    std::string_view container;
};

// One decoded datagram. Nothing is shared between kinds: the payload a kind does
// not carry is absent rather than defaulted, so a caller reading the wrong one
// gets nothing rather than a plausible number.
struct MotionPacket
{
    // Count is the "no packet" value, and no successful decode produces it.
    MotionPacketKind kind = MotionPacketKind::Count;
    MotionPacketProvenance provenance;

    std::optional<MotionFrame> frame;
    std::optional<MotionSkeleton> skeleton;

    std::vector<UnreadChunk> unread;

    // Bone records dropped because their transform named no orientation: a
    // non-finite component anywhere in the seven floats, or a rotation of zero
    // length. That is `VRM_MOCOPI_NON_FINITE_TRANSFORM`'s own definition, and it
    // covers the translation as well as the rotation.
    //
    // A tally rather than only a diagnostic: a live receiver reports "3 bones
    // dropped" in its statistics whether or not it kept every diagnostic, and a
    // caller that passes none still has to be able to see that data was lost.
    //
    // Only ever non-zero for a frame. A skeleton packet with an unusable bone is
    // refused whole, because a rest table with a hole in it has bones whose
    // `pbid` names an id that is not there.
    std::size_t refusedBones = 0;
};

// Decodes one datagram.
//
// Returns false when no packet could be produced, leaving `packet` untouched:
// the container did not close, the magic is not this format, the version is not
// one this decoder has measured, a required chunk is missing or duplicated, a
// field's length disagrees with its type, or a clock is unusable. `diagnostics`,
// when given, is **appended to** and never cleared, so a receive loop can
// accumulate a session's worth across many calls.
//
// Returns true with `refusedBones` set and a diagnostic appended per bone when
// the packet decoded but a bone record did not — see the header's first rule.
//
// The codes this layer raises, and no others: `VRM_MOCOPI_PACKET_MALFORMED`,
// `VRM_MOCOPI_NON_FINITE_TRANSFORM`, `VRM_MOCOPI_TIMESTAMP_INVALID`.
VRMADAPTERMOCOPI_API bool DecodeMotionPacket(
    const std::uint8_t* bytes, std::size_t size, MotionPacket* packet,
    std::vector<Diagnostic>* diagnostics = nullptr);

inline bool DecodeMotionPacket(const std::vector<std::uint8_t>& datagram,
                               MotionPacket* packet,
                               std::vector<Diagnostic>* diagnostics = nullptr)
{
    return DecodeMotionPacket(datagram.data(), datagram.size(), packet,
                              diagnostics);
}

// Deleted for the reason `DecodePacketChunks`' overload is: `formatType` and
// every `UnreadChunk` point into the datagram, so decoding a temporary leaves
// them dangling. The bone data does not — it is copied out of the wire into
// floats — which makes this the more dangerous case rather than the less, since
// a caller that only reads bones will never notice.
bool DecodeMotionPacket(std::vector<std::uint8_t>&& datagram,
                        MotionPacket* packet,
                        std::vector<Diagnostic>* diagnostics = nullptr) = delete;

} // namespace vrmAdapterMocopi
