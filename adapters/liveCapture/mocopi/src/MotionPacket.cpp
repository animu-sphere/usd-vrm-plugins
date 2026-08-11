// SPDX-License-Identifier: Apache-2.0

#include "vrmAdapterMocopi/MotionPacket.h"

#include <cmath>
#include <string>
#include <utility>

namespace vrmAdapterMocopi
{
namespace
{

void Append(std::vector<Diagnostic>* diagnostics, Diagnostic diagnostic)
{
    if (diagnostics != nullptr)
    {
        diagnostics->push_back(std::move(diagnostic));
    }
}

Diagnostic Malformed(std::string subject, std::string detail)
{
    Diagnostic diagnostic =
        MakeDiagnostic(DiagnosticCode::PacketMalformed, std::move(detail));
    diagnostic.subject = std::move(subject);
    return diagnostic;
}

// Every chunk of `chunks` whose tag is not in `known`, recorded once per distinct
// tag rather than once per occurrence. A future application version that adds a
// chunk inside a bone record would otherwise report it 27 times per datagram,
// which buries the one fact worth having: that the chunk exists, and inside
// what.
void CollectUnread(const std::vector<PacketChunk>& chunks,
                   std::string_view container,
                   const std::vector<std::string_view>& known,
                   std::vector<UnreadChunk>* unread)
{
    for (const PacketChunk& chunk : chunks)
    {
        bool isKnown = false;
        for (const std::string_view tag : known)
        {
            if (chunk.tag == tag)
            {
                isKnown = true;
                break;
            }
        }
        if (isKnown)
        {
            continue;
        }
        bool alreadySeen = false;
        for (const UnreadChunk& entry : *unread)
        {
            if (entry.tag == chunk.tag && entry.container == container)
            {
                alreadySeen = true;
                break;
            }
        }
        if (!alreadySeen)
        {
            unread->push_back(UnreadChunk{chunk.tag, container});
        }
    }
}

// Exactly one chunk carrying `tag`, or a refusal that says which of the two ways
// it went wrong. Absent and duplicated are different bugs in a sender and a
// single "could not read" message for both would hide that.
const PacketChunk* RequireOne(const std::vector<PacketChunk>& chunks,
                              std::string_view tag, std::string_view container,
                              std::vector<Diagnostic>* diagnostics)
{
    // One pass, not `CountPacketChunks` then `FindPacketChunk`. This runs 59 times
    // per frame datagram -- twice at the top, five times inside `fram`, and twice
    // per bone record -- so at the measured 60 Hz a second scan is a few thousand
    // needless traversals a second for a list that is never longer than five.
    const PacketChunk* first = nullptr;
    std::size_t count = 0;
    for (const PacketChunk& chunk : chunks)
    {
        if (chunk.tag == tag)
        {
            if (count == 0)
            {
                first = &chunk;
            }
            ++count;
        }
    }
    if (count == 1)
    {
        return first;
    }
    Append(diagnostics,
           Malformed(std::string(tag),
                     count == 0 ? std::string(container) + " carries no "
                                      + std::string(tag) + " chunk"
                                : std::string(container) + " carries "
                                      + std::to_string(count) + " "
                                      + std::string(tag)
                                      + " chunks and must carry one"));
    return nullptr;
}

bool ReadU8(const PacketChunk& chunk, std::uint8_t* value)
{
    if (chunk.bytes == nullptr || chunk.size != 1)
    {
        return false;
    }
    *value = chunk.bytes[0];
    return true;
}

// A field whose declared length disagrees with the type this decoder reads it
// as. Quotes both widths, so a version change arrives as a legible diagnostic
// rather than as a silently truncated read.
Diagnostic BadWidth(std::string_view tag, std::size_t actual,
                    std::size_t expected)
{
    return Malformed(std::string(tag),
                     std::string(tag) + " declares " + std::to_string(actual)
                         + " byte(s) and this decoder reads "
                         + std::to_string(expected));
}

bool ReadBoneTransform(const PacketChunk& chunk, BoneTransform* transform,
                       std::vector<Diagnostic>* diagnostics)
{
    constexpr std::size_t kBytes = BoneTransformFloats * sizeof(float);
    if (chunk.size != kBytes)
    {
        Append(diagnostics, BadWidth(chunk.tag, chunk.size, kBytes));
        return false;
    }
    for (std::size_t index = 0; index < BoneTransformFloats; ++index)
    {
        PacketChunk component;
        component.tag = chunk.tag;
        component.bytes = chunk.bytes + index * sizeof(float);
        component.size = sizeof(float);
        float value = 0.0f;
        if (!ReadPacketChunkF32(component, &value))
        {
            // Unreachable while the width check above holds -- every component is
            // constructed at exactly sizeof(float) over non-null bytes -- but
            // returning false with nothing appended would break the contract that
            // every refusal reports a code, and leave a live receiver counting a
            // dropped datagram it cannot log. The diagnostic costs nothing and
            // survives a change to BoneTransformFloats or to the construction
            // above; without it, that change is silent.
            Append(diagnostics,
                   BadWidth(chunk.tag, component.size, sizeof(float)));
            return false;
        }
        if (index < transform->rotation.size())
        {
            transform->rotation[index] = value;
        }
        else
        {
            transform->translation[index - transform->rotation.size()] = value;
        }
    }
    return true;
}

// A transform that names no orientation. Both halves are the frozen
// `VRM_MOCOPI_NON_FINITE_TRANSFORM`'s description: a non-finite component, or a
// rotation of zero length, whose only carry-on value is exactly the identity a
// reader could not tell from a real sample.
bool TransformIsUsable(const BoneTransform& transform, std::string* reason)
{
    for (const float value : transform.rotation)
    {
        if (!std::isfinite(value))
        {
            *reason = "a rotation component is not finite";
            return false;
        }
    }
    for (const float value : transform.translation)
    {
        if (!std::isfinite(value))
        {
            *reason = "a translation component is not finite";
            return false;
        }
    }
    double lengthSquared = 0.0;
    for (const float value : transform.rotation)
    {
        lengthSquared += static_cast<double>(value) * static_cast<double>(value);
    }
    if (lengthSquared == 0.0)
    {
        *reason = "the rotation has zero length";
        return false;
    }
    return true;
}

bool DecodeProvenance(const std::vector<PacketChunk>& top,
                      MotionPacketProvenance* provenance,
                      std::vector<UnreadChunk>* unread,
                      std::vector<Diagnostic>* diagnostics)
{
    const PacketChunk* head =
        RequireOne(top, ChunkTag::Header, "the datagram", diagnostics);
    const PacketChunk* sender =
        RequireOne(top, ChunkTag::SenderInfo, "the datagram", diagnostics);
    if (head == nullptr || sender == nullptr)
    {
        return false;
    }

    std::vector<PacketChunk> headChunks;
    Diagnostic walkFailure;
    if (!DecodePacketChunks(*head, &headChunks, &walkFailure))
    {
        Append(diagnostics, std::move(walkFailure));
        return false;
    }
    CollectUnread(headChunks, ChunkTag::Header,
                  {ChunkTag::FormatType, ChunkTag::FormatVersion}, unread);

    const PacketChunk* formatType = RequireOne(headChunks, ChunkTag::FormatType,
                                               "head", diagnostics);
    const PacketChunk* version = RequireOne(headChunks, ChunkTag::FormatVersion,
                                            "head", diagnostics);
    if (formatType == nullptr || version == nullptr)
    {
        return false;
    }

    provenance->formatType = std::string_view(
        reinterpret_cast<const char*>(formatType->bytes), formatType->size);
    // The magic, and the reason a datagram of some other protocol handed to this
    // decoder is refused with one legible sentence rather than at whichever
    // field first looked wrong.
    if (provenance->formatType != MotionPacketFormatType)
    {
        Append(diagnostics,
               Malformed(std::string(ChunkTag::FormatType),
                         "ftyp is \"" + std::string(provenance->formatType)
                             + "\" and this decoder reads \""
                             + std::string(MotionPacketFormatType) + "\""));
        return false;
    }

    if (!ReadU8(*version, &provenance->formatVersion))
    {
        Append(diagnostics, BadWidth(ChunkTag::FormatVersion, version->size, 1));
        return false;
    }
    if (provenance->formatVersion != MotionPacketFormatVersion)
    {
        // The one place this decoder refuses something for being newer than the
        // measurement; MotionPacket.h argues why.
        Append(diagnostics,
               Malformed(std::string(ChunkTag::FormatVersion),
                         "vrsn is "
                             + std::to_string(provenance->formatVersion)
                             + " and this decoder has only measured version "
                             + std::to_string(MotionPacketFormatVersion)
                             + "; its field layout may differ"));
        return false;
    }

    std::vector<PacketChunk> senderChunks;
    if (!DecodePacketChunks(*sender, &senderChunks, &walkFailure))
    {
        Append(diagnostics, std::move(walkFailure));
        return false;
    }
    CollectUnread(senderChunks, ChunkTag::SenderInfo,
                  {ChunkTag::SenderId, ChunkTag::ReceivePort}, unread);

    const PacketChunk* senderId =
        RequireOne(senderChunks, ChunkTag::SenderId, "sndf", diagnostics);
    const PacketChunk* port =
        RequireOne(senderChunks, ChunkTag::ReceivePort, "sndf", diagnostics);
    if (senderId == nullptr || port == nullptr)
    {
        return false;
    }
    if (senderId->size != SenderIdBytes)
    {
        Append(diagnostics,
               BadWidth(ChunkTag::SenderId, senderId->size, SenderIdBytes));
        return false;
    }
    for (std::size_t index = 0; index < SenderIdBytes; ++index)
    {
        provenance->senderId[index] = senderId->bytes[index];
    }
    if (!ReadPacketChunkU16(*port, &provenance->receivePort))
    {
        Append(diagnostics,
               BadWidth(ChunkTag::ReceivePort, port->size,
                        sizeof(std::uint16_t)));
        return false;
    }
    return true;
}

// The repeated-record level shared by `btrs` and `bons`: one walk, one record
// tag, and a refusal that names the record's position when its own walk fails.
//
// `visit` is handed each record's fields in turn and returns false to abandon the
// container. **One scratch vector is reused across every record**, which is the
// point of the shape rather than a micro-optimisation: `DecodePacketChunks` clears
// and refills it, so 27 bone records cost one allocation's worth of capacity
// instead of 27. Materialising a vector-of-vectors here — which this did at first
// — allocates once or twice per bone per frame, which is precisely the cost
// `PacketChunk.h` explains the non-owning views exist to avoid, at the measured
// 27 bones and 60 Hz.
template <typename Visit>
bool ForEachRecord(const PacketChunk& container, std::string_view recordTag,
                   std::vector<UnreadChunk>* unread,
                   std::vector<Diagnostic>* diagnostics, Visit visit)
{
    std::vector<PacketChunk> chunks;
    Diagnostic walkFailure;
    if (!DecodePacketChunks(container, &chunks, &walkFailure))
    {
        Append(diagnostics, std::move(walkFailure));
        return false;
    }
    CollectUnread(chunks, container.tag, {recordTag}, unread);

    std::vector<PacketChunk> fields;
    std::size_t index = 0;
    for (const PacketChunk& chunk : chunks)
    {
        if (chunk.tag != recordTag)
        {
            continue;
        }
        if (!DecodePacketChunks(chunk, &fields, &walkFailure))
        {
            walkFailure.detail += " (record " + std::to_string(index) + " of "
                                  + std::string(container.tag) + ")";
            Append(diagnostics, std::move(walkFailure));
            return false;
        }
        if (!visit(fields))
        {
            return false;
        }
        ++index;
    }
    return true;
}

// A capacity hint and nothing more: `MeasuredBoneCount` is what every measured
// session sent, so reserving it costs one allocation for the ordinary case, and
// being wrong about a different rig costs only the growth it would have had.
// Counting the records first would mean walking the container twice to save five
// reallocations, which is the wrong trade.
constexpr std::size_t kBoneCapacityHint = MeasuredBoneCount;

bool DecodeFrame(const PacketChunk& frameChunk, MotionFrame* frame,
                 std::vector<UnreadChunk>* unread,
                 std::size_t* refusedBones,
                 std::vector<Diagnostic>* diagnostics)
{
    std::vector<PacketChunk> chunks;
    Diagnostic walkFailure;
    if (!DecodePacketChunks(frameChunk, &chunks, &walkFailure))
    {
        Append(diagnostics, std::move(walkFailure));
        return false;
    }
    CollectUnread(chunks, ChunkTag::Frame,
                  {ChunkTag::FrameNumber, ChunkTag::StreamTime,
                   ChunkTag::SenderUnixTime, ChunkTag::Timecode,
                   ChunkTag::BoneTransforms},
                  unread);

    const PacketChunk* number =
        RequireOne(chunks, ChunkTag::FrameNumber, "fram", diagnostics);
    const PacketChunk* streamTime =
        RequireOne(chunks, ChunkTag::StreamTime, "fram", diagnostics);
    const PacketChunk* unixTime =
        RequireOne(chunks, ChunkTag::SenderUnixTime, "fram", diagnostics);
    const PacketChunk* timecode =
        RequireOne(chunks, ChunkTag::Timecode, "fram", diagnostics);
    const PacketChunk* bones =
        RequireOne(chunks, ChunkTag::BoneTransforms, "fram", diagnostics);
    if (number == nullptr || streamTime == nullptr || unixTime == nullptr
        || timecode == nullptr || bones == nullptr)
    {
        return false;
    }

    if (!ReadPacketChunkU32(*number, &frame->frameNumber))
    {
        Append(diagnostics, BadWidth(ChunkTag::FrameNumber, number->size,
                                     sizeof(std::uint32_t)));
        return false;
    }
    if (!ReadPacketChunkF32(*streamTime, &frame->streamSeconds))
    {
        Append(diagnostics,
               BadWidth(ChunkTag::StreamTime, streamTime->size, sizeof(float)));
        return false;
    }
    if (!ReadPacketChunkF64(*unixTime, &frame->senderUnixSeconds))
    {
        Append(diagnostics, BadWidth(ChunkTag::SenderUnixTime, unixTime->size,
                                     sizeof(double)));
        return false;
    }
    if (timecode->size != TimecodeBytes)
    {
        Append(diagnostics,
               BadWidth(ChunkTag::Timecode, timecode->size, TimecodeBytes));
        return false;
    }
    for (std::size_t index = 0; index < TimecodeBytes; ++index)
    {
        frame->timecode[index] = timecode->bytes[index];
    }

    // A frame nothing can be ordered by is not a frame this adapter can hand on,
    // so this refusal is the packet's and not a bone's. `streamSeconds` begins at
    // exactly 0.0 in every measured session and never went backwards inside one;
    // a negative value is not a clock this decoder has a reading for.
    if (!std::isfinite(frame->streamSeconds) || frame->streamSeconds < 0.0f)
    {
        Diagnostic diagnostic = MakeDiagnostic(
            DiagnosticCode::TimestampInvalid,
            "time is " + std::to_string(frame->streamSeconds)
                + ", which names no instant in the stream");
        diagnostic.subject = std::string(ChunkTag::StreamTime);
        diagnostic.sequence = frame->frameNumber;
        Append(diagnostics, std::move(diagnostic));
        return false;
    }
    if (!std::isfinite(frame->senderUnixSeconds))
    {
        Diagnostic diagnostic =
            MakeDiagnostic(DiagnosticCode::TimestampInvalid,
                           "uttm is not finite");
        diagnostic.subject = std::string(ChunkTag::SenderUnixTime);
        diagnostic.sequence = frame->frameNumber;
        Append(diagnostics, std::move(diagnostic));
        return false;
    }

    frame->bones.reserve(kBoneCapacityHint);
    return ForEachRecord(
        *bones, ChunkTag::BoneTransformRecord, unread, diagnostics,
        [&](const std::vector<PacketChunk>& fields) {
            CollectUnread(fields, ChunkTag::BoneTransformRecord,
                          {ChunkTag::BoneId, ChunkTag::Transform}, unread);
            const PacketChunk* id =
                RequireOne(fields, ChunkTag::BoneId, "btdt", diagnostics);
            const PacketChunk* transform =
                RequireOne(fields, ChunkTag::Transform, "btdt", diagnostics);
            if (id == nullptr || transform == nullptr)
            {
                return false;
            }

            BoneFrame bone;
            if (!ReadPacketChunkU16(*id, &bone.boneId))
            {
                Append(diagnostics, BadWidth(ChunkTag::BoneId, id->size,
                                             sizeof(std::uint16_t)));
                return false;
            }
            if (!ReadBoneTransform(*transform, &bone.transform, diagnostics))
            {
                return false;
            }

            std::string reason;
            if (!TransformIsUsable(bone.transform, &reason))
            {
                // The bone, not the frame: MotionPacket.h's first rule.
                Diagnostic diagnostic = MakeDiagnostic(
                    DiagnosticCode::NonFiniteTransform, std::move(reason));
                diagnostic.subject = "bone " + std::to_string(bone.boneId);
                diagnostic.timestamp = frame->streamSeconds;
                diagnostic.sequence = frame->frameNumber;
                Append(diagnostics, std::move(diagnostic));
                ++*refusedBones;
                return true;
            }
            frame->bones.push_back(bone);
            return true;
        });
}

// No `refusedBones` out-parameter, unlike `DecodeFrame`: this layer never returns
// a skeleton with a bone missing, so a count of missing bones would only ever be
// zero. See the refusal below.
bool DecodeSkeleton(const PacketChunk& skeletonChunk, MotionSkeleton* skeleton,
                    std::vector<UnreadChunk>* unread,
                    std::vector<Diagnostic>* diagnostics)
{
    std::vector<PacketChunk> chunks;
    Diagnostic walkFailure;
    if (!DecodePacketChunks(skeletonChunk, &chunks, &walkFailure))
    {
        Append(diagnostics, std::move(walkFailure));
        return false;
    }
    CollectUnread(chunks, ChunkTag::Skeleton, {ChunkTag::BoneDefinitions},
                  unread);

    const PacketChunk* bones =
        RequireOne(chunks, ChunkTag::BoneDefinitions, "skdf", diagnostics);
    if (bones == nullptr)
    {
        return false;
    }

    skeleton->bones.reserve(kBoneCapacityHint);
    return ForEachRecord(
        *bones, ChunkTag::BoneDefinitionRecord, unread, diagnostics,
        [&](const std::vector<PacketChunk>& fields) {
        CollectUnread(fields, ChunkTag::BoneDefinitionRecord,
                      {ChunkTag::BoneId, ChunkTag::ParentBoneId,
                       ChunkTag::Transform},
                      unread);
        const PacketChunk* id =
            RequireOne(fields, ChunkTag::BoneId, "bndt", diagnostics);
        const PacketChunk* parent =
            RequireOne(fields, ChunkTag::ParentBoneId, "bndt", diagnostics);
        const PacketChunk* transform =
            RequireOne(fields, ChunkTag::Transform, "bndt", diagnostics);
        if (id == nullptr || parent == nullptr || transform == nullptr)
        {
            return false;
        }

        BoneDefinition bone;
        if (!ReadPacketChunkU16(*id, &bone.boneId))
        {
            Append(diagnostics, BadWidth(ChunkTag::BoneId, id->size,
                                         sizeof(std::uint16_t)));
            return false;
        }
        if (!ReadPacketChunkI16(*parent, &bone.parentBoneId))
        {
            Append(diagnostics, BadWidth(ChunkTag::ParentBoneId, parent->size,
                                         sizeof(std::int16_t)));
            return false;
        }
        if (!ReadBoneTransform(*transform, &bone.restTransform, diagnostics))
        {
            return false;
        }

        std::string reason;
        if (!TransformIsUsable(bone.restTransform, &reason))
        {
            // **The skeleton is refused whole, and this is the deliberate
            // asymmetry with a frame.** A frame is a set of independent samples,
            // so dropping one costs one joint. A skeleton is a *tree*: every
            // other bone's `pbid` names an id in this table, so a table with a
            // hole in it has bones whose parent does not exist, and a consumer
            // that indexes by position reads every bone after the hole as the
            // wrong joint. There is no partial hierarchy to hand on, so nothing
            // is handed on.
            Diagnostic diagnostic = MakeDiagnostic(
                DiagnosticCode::NonFiniteTransform,
                std::move(reason)
                    + "; a rest pose is a hierarchy and is refused whole");
            diagnostic.subject = "bone " + std::to_string(bone.boneId);
            Append(diagnostics, std::move(diagnostic));
            return false;
        }
        skeleton->bones.push_back(bone);
        return true;
        });
}

} // namespace

std::string_view MotionPacketKindTag(MotionPacketKind kind) noexcept
{
    switch (kind)
    {
    case MotionPacketKind::Frame:
        return ChunkTag::Frame;
    case MotionPacketKind::Skeleton:
        return ChunkTag::Skeleton;
    case MotionPacketKind::Count:
        break;
    }
    return {};
}

bool DecodeMotionPacket(const std::uint8_t* bytes, std::size_t size,
                        MotionPacket* packet,
                        std::vector<Diagnostic>* diagnostics)
{
    if (packet == nullptr)
    {
        return false;
    }

    std::vector<PacketChunk> top;
    Diagnostic walkFailure;
    if (!DecodePacketChunks(bytes, size, &top, "datagram", &walkFailure))
    {
        Append(diagnostics, std::move(walkFailure));
        return false;
    }

    const std::size_t frames = CountPacketChunks(top, ChunkTag::Frame);
    const std::size_t skeletons = CountPacketChunks(top, ChunkTag::Skeleton);
    if (frames + skeletons != 1)
    {
        // Neither kind, both kinds, or two of one: every measured datagram
        // carried exactly one payload chunk, and a datagram that carries a frame
        // *and* a skeleton does not say which clock the skeleton belongs to.
        Append(diagnostics,
               Malformed("datagram",
                         "the datagram carries " + std::to_string(frames)
                             + " fram and " + std::to_string(skeletons)
                             + " skdf chunk(s) and must carry exactly one of "
                               "the two"));
        return false;
    }

    MotionPacket decoded;
    decoded.kind =
        frames == 1 ? MotionPacketKind::Frame : MotionPacketKind::Skeleton;
    CollectUnread(top, "datagram",
                  {ChunkTag::Header, ChunkTag::SenderInfo, ChunkTag::Frame,
                   ChunkTag::Skeleton},
                  &decoded.unread);

    if (!DecodeProvenance(top, &decoded.provenance, &decoded.unread,
                          diagnostics))
    {
        return false;
    }

    if (decoded.kind == MotionPacketKind::Frame)
    {
        MotionFrame frame;
        if (!DecodeFrame(*FindPacketChunk(top, ChunkTag::Frame), &frame,
                         &decoded.unread, &decoded.refusedBones, diagnostics))
        {
            return false;
        }
        decoded.frame = std::move(frame);
    }
    else
    {
        MotionSkeleton skeleton;
        if (!DecodeSkeleton(*FindPacketChunk(top, ChunkTag::Skeleton), &skeleton,
                            &decoded.unread, diagnostics))
        {
            return false;
        }
        decoded.skeleton = std::move(skeleton);
    }

    *packet = std::move(decoded);
    return true;
}

} // namespace vrmAdapterMocopi
