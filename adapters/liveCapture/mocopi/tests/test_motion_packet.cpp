// SPDX-License-Identifier: Apache-2.0
//
// The container and the two packet kinds.
//
// Two things are pinned here and they are not the same thing, which matters more
// for this protocol than for its published sibling.
//
// The unit tests pin the *decoder*: which datagrams it accepts, which it
// refuses, which code it refuses them with, and what it carries forward unread.
// They are written against datagrams this file assembles, so they can be as
// hostile as necessary and cannot be surprised by anything.
//
// Corpus mode pins the *fixtures*, and each assertion below is named after the
// phenomenon its capture exists for (tests/corpus/manifest.json). What neither
// mode can do is validate the grammar itself: the fixtures were written from the
// same measurement the decoder reads them with, so a wrong measurement would
// make both agree and both be wrong. The evidence for the grammar is 8000
// datagrams of five real device sessions, recorded off a device that is not
// committable, and it is written down with its population on MotionPacket.h. The
// thing that can still surprise this decoder is a capture whose *encoding* is
// the vendor's own -- the `BVH Sender` pointed at a `.bvh` this repository wrote
// -- and when an operator makes one it joins this corpus and this test grows the
// case it deserves.
#include "vrmAdapterMocopi/MotionPacket.h"
#include "vrmAdapterMocopi/PacketCapture.h"
#include "vrmAdapterMocopi/PacketChunk.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace
{

using vrmAdapterMocopi::BoneDefinition;
using vrmAdapterMocopi::BoneFrame;
using vrmAdapterMocopi::Diagnostic;
using vrmAdapterMocopi::DiagnosticCode;
using vrmAdapterMocopi::MotionPacket;
using vrmAdapterMocopi::MotionPacketKind;
using vrmAdapterMocopi::PacketChunk;
using vrmAdapterMocopi::RecordedDatagram;

using Bytes = std::vector<std::uint8_t>;

// ---------------------------------------------------------------------------
// A container encoder that is this test's own, not the decoder's
// ---------------------------------------------------------------------------

void
AppendU16(Bytes* out, std::uint16_t value)
{
    out->push_back(static_cast<std::uint8_t>(value & 0xff));
    out->push_back(static_cast<std::uint8_t>((value >> 8) & 0xff));
}

void
AppendU32(Bytes* out, std::uint32_t value)
{
    for (int shift = 0; shift < 32; shift += 8) {
        out->push_back(static_cast<std::uint8_t>((value >> shift) & 0xff));
    }
}

void
AppendF32(Bytes* out, float value)
{
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    AppendU32(out, bits);
}

void
AppendF64(Bytes* out, double value)
{
    std::uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    for (int shift = 0; shift < 64; shift += 8) {
        out->push_back(static_cast<std::uint8_t>((bits >> shift) & 0xff));
    }
}

Bytes
Chunk(const char* tag, const Bytes& payload)
{
    assert(std::strlen(tag) == 4);
    Bytes out;
    AppendU32(&out, static_cast<std::uint32_t>(payload.size()));
    out.insert(out.end(), tag, tag + 4);
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

Bytes
Join(std::initializer_list<Bytes> parts)
{
    Bytes out;
    for (const Bytes& part : parts) {
        out.insert(out.end(), part.begin(), part.end());
    }
    return out;
}

Bytes
Text(const char* value)
{
    return Bytes(reinterpret_cast<const std::uint8_t*>(value),
                 reinterpret_cast<const std::uint8_t*>(value)
                     + std::strlen(value));
}

Bytes
Tran(float x, float y, float z, float w, float tx, float ty, float tz)
{
    Bytes out;
    for (const float value : {x, y, z, w, tx, ty, tz}) {
        AppendF32(&out, value);
    }
    return Chunk("tran", out);
}

Bytes
IdentityTran()
{
    return Tran(0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f);
}

Bytes
Head(std::uint8_t version = 1, const char* formatType = "sony motion format")
{
    return Chunk("head", Join({Chunk("ftyp", Text(formatType)),
                               Chunk("vrsn", Bytes{version})}));
}

Bytes
Sender()
{
    Bytes port;
    AppendU16(&port, 12351);
    return Chunk("sndf",
                 Join({Chunk("ipad", Bytes{1, 2, 3, 4, 5, 6, 7, 8}),
                       Chunk("rcvp", port)}));
}

Bytes
BoneId(std::uint16_t id)
{
    Bytes out;
    AppendU16(&out, id);
    return Chunk("bnid", out);
}

Bytes
Btdt(std::uint16_t id, const Bytes& tran)
{
    return Chunk("btdt", Join({BoneId(id), tran}));
}

// A frame whose fields are all well formed unless a caller replaces one.
Bytes
Frame(std::uint32_t number, float streamSeconds, double unixSeconds,
      const Bytes& boneRecords)
{
    Bytes fnum;
    AppendU32(&fnum, number);
    Bytes time;
    AppendF32(&time, streamSeconds);
    Bytes uttm;
    AppendF64(&uttm, unixSeconds);
    return Join({Head(), Sender(),
                 Chunk("fram", Join({Chunk("fnum", fnum), Chunk("time", time),
                                     Chunk("uttm", uttm),
                                     Chunk("tmcd", Bytes(6, 0)),
                                     Chunk("btrs", boneRecords)}))});
}

Bytes
OneBoneFrame()
{
    return Frame(1, 0.0f, 1786492800.0, Btdt(0, IdentityTran()));
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

bool
Decode(const Bytes& datagram, MotionPacket* packet,
       std::vector<Diagnostic>* diagnostics = nullptr)
{
    return vrmAdapterMocopi::DecodeMotionPacket(datagram, packet, diagnostics);
}

// The single code a refusal reported, asserted rather than merely counted: a
// decoder that refused the right datagram with the wrong code would otherwise
// pass every test here.
void
RefusedWith(const Bytes& datagram, DiagnosticCode expected, const char* what)
{
    MotionPacket packet;
    std::vector<Diagnostic> diagnostics;
    const bool decoded = Decode(datagram, &packet, &diagnostics);
    if (decoded) {
        std::fprintf(stderr, "%s: decoded when it should have been refused\n",
                     what);
        assert(false);
    }
    assert(!diagnostics.empty());
    if (diagnostics.back().code != expected) {
        std::fprintf(stderr, "%s: refused with %s, expected %s (%s)\n", what,
                     std::string(vrmAdapterMocopi::DiagnosticCodeString(
                                     diagnostics.back().code))
                         .c_str(),
                     std::string(
                         vrmAdapterMocopi::DiagnosticCodeString(expected))
                         .c_str(),
                     diagnostics.back().detail.c_str());
        assert(false);
    }
    // The packet is untouched on refusal, so a caller cannot read a half-decoded
    // frame back out of it.
    assert(packet.kind == MotionPacketKind::Count);
    assert(!packet.frame.has_value());
    assert(!packet.skeleton.has_value());
}

bool
NearlyEqual(float left, float right)
{
    return std::fabs(left - right) <= 1e-6f;
}

// ---------------------------------------------------------------------------
// The container
// ---------------------------------------------------------------------------

void
TestTheContainerClosesOrIsRefused()
{
    const Bytes datagram = Join({Head(), Sender()});
    std::vector<PacketChunk> chunks;
    Diagnostic diagnostic;
    assert(vrmAdapterMocopi::DecodePacketChunks(datagram, &chunks, "datagram",
                                                &diagnostic));
    assert(chunks.size() == 2);
    assert(chunks[0].tag == "head");
    assert(chunks[1].tag == "sndf");
    // Offsets are relative to the buffer walked, so a refusal one level down can
    // still be reported against a position in the datagram.
    assert(chunks[0].offset == 0);
    assert(chunks[1].offset == 8 + chunks[0].size);

    // A zero-length datagram closes on zero chunks. It is receivable, so the
    // container reads it rather than refusing it; the packet layer is what
    // refuses it, for carrying neither payload kind.
    const Bytes empty;
    assert(vrmAdapterMocopi::DecodePacketChunks(empty, &chunks));
    assert(chunks.empty());

    // A header that does not fit.
    Bytes shortHeader;
    AppendU32(&shortHeader, 0);
    assert(!vrmAdapterMocopi::DecodePacketChunks(shortHeader, &chunks,
                                                 "datagram", &diagnostic));
    assert(diagnostic.code == DiagnosticCode::PacketMalformed);
    assert(chunks.empty());

    // A payload longer than the bytes that remain. The refusal names the tag it
    // read, because a truncated frame and a truncated skeleton are different
    // bugs in a sender.
    Bytes overrun;
    AppendU32(&overrun, 4096);
    const Bytes tag = Text("fram");
    overrun.insert(overrun.end(), tag.begin(), tag.end());
    overrun.resize(overrun.size() + 16, 0);
    assert(!vrmAdapterMocopi::DecodePacketChunks(overrun, &chunks, "datagram",
                                                 &diagnostic));
    assert(diagnostic.detail.find("fram") != std::string::npos);
    assert(diagnostic.detail.find("4096") != std::string::npos);

    // A walk that lands between chunks rather than on the end.
    Bytes trailing = Head();
    trailing.insert(trailing.end(), {1, 2, 3});
    assert(!vrmAdapterMocopi::DecodePacketChunks(trailing, &chunks, "datagram",
                                                 &diagnostic));

    // A tag full of bytes a terminal would act on cannot reach a diagnostic as
    // itself: golden tests compare these lines.
    const std::string text =
        vrmAdapterMocopi::PacketChunkTagText(std::string_view("a\nb\x1b", 4));
    assert(text == "0x610a621b");
    assert(vrmAdapterMocopi::PacketChunkTagText("fram") == "fram");
}

void
TestALeafIsReadAtItsExactWidth()
{
    Bytes payload;
    AppendU32(&payload, 0x04030201);
    const Bytes datagram = Chunk("fnum", payload);
    std::vector<PacketChunk> chunks;
    assert(vrmAdapterMocopi::DecodePacketChunks(datagram, &chunks));
    assert(chunks.size() == 1);

    std::uint32_t value = 0;
    assert(vrmAdapterMocopi::ReadPacketChunkU32(chunks[0], &value));
    assert(value == 0x04030201);

    // Not a prefix of a wider field, and not a widened narrower one: a field
    // whose length changed is a field whose meaning is unmeasured.
    std::uint16_t narrow = 0;
    assert(!vrmAdapterMocopi::ReadPacketChunkU16(chunks[0], &narrow));
    double wide = 0.0;
    assert(!vrmAdapterMocopi::ReadPacketChunkF64(chunks[0], &wide));

    // The parent column's root sentinel is negative, so this field is signed.
    Bytes minusOne;
    AppendU16(&minusOne, 0xffff);
    const Bytes parent = Chunk("pbid", minusOne);
    assert(vrmAdapterMocopi::DecodePacketChunks(parent, &chunks));
    std::int16_t parentId = 0;
    assert(vrmAdapterMocopi::ReadPacketChunkI16(chunks[0], &parentId));
    assert(parentId == -1);
}

// ---------------------------------------------------------------------------
// The packet
// ---------------------------------------------------------------------------

void
TestAFrameDecodes()
{
    MotionPacket packet;
    std::vector<Diagnostic> diagnostics;
    const Bytes datagram = OneBoneFrame();
    assert(Decode(datagram, &packet, &diagnostics));
    assert(diagnostics.empty());
    assert(packet.kind == MotionPacketKind::Frame);
    assert(vrmAdapterMocopi::MotionPacketKindTag(packet.kind) == "fram");
    assert(packet.provenance.formatType == "sony motion format");
    assert(packet.provenance.formatVersion == 1);
    assert(packet.provenance.receivePort == 12351);
    assert(!packet.skeleton.has_value());
    assert(packet.frame.has_value());
    assert(packet.frame->frameNumber == 1);
    assert(packet.frame->bones.size() == 1);
    assert(packet.frame->bones[0].boneId == 0);
    assert(NearlyEqual(packet.frame->bones[0].transform.rotation[3], 1.0f));
    assert(packet.refusedBones == 0);
    assert(packet.unread.empty());
    // Six bytes carried and nothing claimed about them.
    for (const std::uint8_t byte : packet.frame->timecode) {
        assert(byte == 0);
    }
}

void
TestASkeletonDecodes()
{
    Bytes parent;
    AppendU16(&parent, 0xffff);
    const Bytes bones =
        Join({Chunk("bndt", Join({BoneId(0), Chunk("pbid", parent),
                                  Tran(0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.9f,
                                       0.0f)})),
              Chunk("bndt", Join({BoneId(1), Chunk("pbid", Bytes{0, 0}),
                                  IdentityTran()}))});
    const Bytes datagram =
        Join({Head(), Sender(), Chunk("skdf", Chunk("bons", bones))});

    MotionPacket packet;
    assert(Decode(datagram, &packet));
    assert(packet.kind == MotionPacketKind::Skeleton);
    assert(vrmAdapterMocopi::MotionPacketKindTag(packet.kind) == "skdf");
    assert(!packet.frame.has_value());
    assert(packet.skeleton.has_value());
    assert(packet.skeleton->bones.size() == 2);
    assert(packet.skeleton->bones[0].parentBoneId == -1);
    assert(packet.skeleton->bones[1].parentBoneId == 0);
    assert(NearlyEqual(packet.skeleton->bones[0].restTransform.translation[1],
                       0.9f));
}

void
TestASkeletonWithAnUnusableBoneIsRefusedWhole()
{
    // The asymmetry with a frame, and the reason for it: bone 1's parent is bone
    // 0, so a table that dropped bone 0 would leave bone 1 pointing at an id that
    // is not there. A frame has no such relationship between its records.
    Bytes rootParent;
    AppendU16(&rootParent, 0xffff);
    const float nan = std::nanf("");
    const Bytes bones = Join(
        {Chunk("bndt", Join({BoneId(0), Chunk("pbid", rootParent),
                             Tran(nan, 0.0f, 0.0f, 1.0f, 0.0f, 0.9f, 0.0f)})),
         Chunk("bndt", Join({BoneId(1), Chunk("pbid", Bytes{0, 0}),
                             IdentityTran()}))});
    RefusedWith(Join({Head(), Sender(),
                      Chunk("skdf", Chunk("bons", bones))}),
                DiagnosticCode::NonFiniteTransform,
                "a skeleton with an unusable rest transform");

    // The same defect in a *frame* costs the bone and not the packet, which is
    // the pair this test exists to hold apart.
    MotionPacket packet;
    std::vector<Diagnostic> diagnostics;
    const Bytes frame =
        Frame(1, 0.0f, 1786492800.0,
              Join({Btdt(0, Tran(nan, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f)),
                    Btdt(1, IdentityTran())}));
    assert(Decode(frame, &packet, &diagnostics));
    assert(packet.refusedBones == 1);
    assert(packet.frame->bones.size() == 1);
}

void
TestARefusalNamesAPositionInTheDatagram()
{
    // A truncated `tran` inside the second bone record. The offset quoted must be
    // findable in the datagram's hex, which means it accumulates down the tree
    // rather than restarting at each level -- so it is emphatically not the 8 a
    // payload-relative walk would report, and 8 in a real datagram is inside
    // `head`.
    Bytes shortTran;
    AppendU32(&shortTran, 4096);
    const Bytes tag = Text("tran");
    shortTran.insert(shortTran.end(), tag.begin(), tag.end());
    shortTran.resize(shortTran.size() + 4, 0);

    const Bytes records =
        Join({Btdt(0, IdentityTran()),
              Chunk("btdt", Join({BoneId(1), shortTran}))});
    const Bytes datagram = Frame(1, 0.0f, 1786492800.0, records);

    MotionPacket packet;
    std::vector<Diagnostic> diagnostics;
    assert(!Decode(datagram, &packet, &diagnostics));
    assert(!diagnostics.empty());
    const std::string& detail = diagnostics.back().detail;
    assert(detail.find("tran") != std::string::npos);

    // Recompute where that chunk really begins and require the message to name
    // it, rather than accepting any number.
    const Bytes head = Head();
    const Bytes sender = Sender();
    const std::size_t framHeader = head.size() + sender.size();
    Bytes fnum;
    AppendU32(&fnum, 1);
    const std::size_t insideFram =
        Chunk("fnum", fnum).size() + Chunk("time", Bytes(4, 0)).size()
        + Chunk("uttm", Bytes(8, 0)).size() + Chunk("tmcd", Bytes(6, 0)).size();
    const std::size_t btrsBody = framHeader + 8 + insideFram + 8;
    const std::size_t secondRecord = btrsBody + Btdt(0, IdentityTran()).size();
    const std::size_t tranOffset = secondRecord + 8 + BoneId(1).size();
    assert(detail.find(std::to_string(tranOffset)) != std::string::npos);
    // The trap the propagation exists to close.
    assert(tranOffset > 8);
}

void
TestTheQuaternionIsScalarLastAndIsNotReordered()
{
    // Distinct values in every slot, so a decoder that rotated the components
    // cannot pass by symmetry. The order is measured, not assumed: MotionPacket.h
    // records the three confirmations.
    const Bytes datagram =
        Frame(1, 0.0f, 1786492800.0,
              Btdt(0, Tran(0.1f, 0.2f, 0.3f, 0.4f, 1.5f, 2.5f, 3.5f)));
    MotionPacket packet;
    assert(Decode(datagram, &packet));
    const BoneFrame& bone = packet.frame->bones[0];
    assert(NearlyEqual(bone.transform.rotation[0], 0.1f));
    assert(NearlyEqual(bone.transform.rotation[1], 0.2f));
    assert(NearlyEqual(bone.transform.rotation[2], 0.3f));
    assert(NearlyEqual(bone.transform.rotation[3], 0.4f));
    assert(NearlyEqual(bone.transform.translation[0], 1.5f));
    assert(NearlyEqual(bone.transform.translation[1], 2.5f));
    assert(NearlyEqual(bone.transform.translation[2], 3.5f));

    // A rotation that is not unit is carried as it arrived. Normalising here
    // would make the corpus agree with one downstream reading and no other, and
    // the measured deviation is 4.2e-08 -- so a fixture whose rotation is
    // visibly not unit is describing something worth seeing downstream.
    const double lengthSquared = 0.1 * 0.1 + 0.2 * 0.2 + 0.3 * 0.3 + 0.4 * 0.4;
    assert(lengthSquared < 0.99);
}

void
TestTheContainerRefusalsReachThePacketLayer()
{
    // Neither payload kind. An empty datagram takes the same path, which is why
    // the container accepts it and this layer does not.
    RefusedWith(Join({Head(), Sender()}), DiagnosticCode::PacketMalformed,
                "a datagram carrying neither kind");
    RefusedWith(Bytes{}, DiagnosticCode::PacketMalformed,
                "a zero-length datagram");

    // Both kinds at once: nothing says which clock the skeleton belongs to.
    Bytes both = OneBoneFrame();
    const Bytes skeleton = Chunk("skdf", Chunk("bons", Bytes{}));
    both.insert(both.end(), skeleton.begin(), skeleton.end());
    RefusedWith(both, DiagnosticCode::PacketMalformed,
                "a datagram carrying both kinds");
}

void
TestTheMagicAndTheVersionAreTheOnlyGates()
{
    Bytes wrongMagic = OneBoneFrame();
    // Rebuild rather than patch: the magic's length is part of the chunk.
    wrongMagic = Join(
        {Head(1, "not a motion format"), Sender(),
         Chunk("fram", Join({Chunk("fnum", Bytes(4, 0)),
                             Chunk("time", Bytes(4, 0)),
                             Chunk("uttm", Bytes(8, 0)),
                             Chunk("tmcd", Bytes(6, 0)),
                             Chunk("btrs", Btdt(0, IdentityTran()))}))});
    RefusedWith(wrongMagic, DiagnosticCode::PacketMalformed,
                "a datagram of some other format");

    // The one refusal that blames a sender for being newer than the
    // measurement, and MotionPacket.h argues why that is the correct call.
    const Bytes newerVersion =
        Join({Head(2), Sender(),
              Chunk("fram", Join({Chunk("fnum", Bytes(4, 0)),
                                  Chunk("time", Bytes(4, 0)),
                                  Chunk("uttm", Bytes(8, 0)),
                                  Chunk("tmcd", Bytes(6, 0)),
                                  Chunk("btrs", Btdt(0, IdentityTran()))}))});
    RefusedWith(newerVersion, DiagnosticCode::PacketMalformed,
                "a version this decoder has not measured");
}

void
TestAFieldMustMatchItsType()
{
    Bytes narrowFnum;
    AppendU16(&narrowFnum, 7);
    RefusedWith(
        Join({Head(), Sender(),
              Chunk("fram", Join({Chunk("fnum", narrowFnum),
                                  Chunk("time", Bytes(4, 0)),
                                  Chunk("uttm", Bytes(8, 0)),
                                  Chunk("tmcd", Bytes(6, 0)),
                                  Chunk("btrs", Btdt(0, IdentityTran()))}))}),
        DiagnosticCode::PacketMalformed, "a two-byte fnum");

    // A six-float `tran`.
    Bytes sixFloats;
    for (int index = 0; index < 6; ++index) {
        AppendF32(&sixFloats, 0.0f);
    }
    RefusedWith(Frame(1, 0.0f, 1786492800.0,
                      Chunk("btdt", Join({BoneId(0),
                                          Chunk("tran", sixFloats)}))),
                DiagnosticCode::PacketMalformed, "a six-float tran");

    // A field read exactly once, sent twice. Absent and duplicated are
    // different bugs and neither is silently taken as the other.
    Bytes duplicate;
    AppendU32(&duplicate, 7);
    RefusedWith(
        Join({Head(), Sender(),
              Chunk("fram", Join({Chunk("fnum", duplicate),
                                  Chunk("fnum", duplicate),
                                  Chunk("time", Bytes(4, 0)),
                                  Chunk("uttm", Bytes(8, 0)),
                                  Chunk("tmcd", Bytes(6, 0)),
                                  Chunk("btrs", Btdt(0, IdentityTran()))}))}),
        DiagnosticCode::PacketMalformed, "two fnum chunks");

    // A `tmcd` this decoder never reads is still a field whose width the
    // protocol fixes.
    Bytes shortTimecode;
    AppendU32(&shortTimecode, 0);
    RefusedWith(
        Join({Head(), Sender(),
              Chunk("fram", Join({Chunk("fnum", duplicate),
                                  Chunk("time", Bytes(4, 0)),
                                  Chunk("uttm", Bytes(8, 0)),
                                  Chunk("tmcd", shortTimecode),
                                  Chunk("btrs", Btdt(0, IdentityTran()))}))}),
        DiagnosticCode::PacketMalformed, "a four-byte tmcd");
}

void
TestAnUnusableClockRefusesTheFrame()
{
    const float nan = std::nanf("");
    RefusedWith(Frame(1, nan, 1786492800.0, Btdt(0, IdentityTran())),
                DiagnosticCode::TimestampInvalid, "a non-finite time");
    RefusedWith(Frame(1, -1.0f, 1786492800.0, Btdt(0, IdentityTran())),
                DiagnosticCode::TimestampInvalid, "a negative time");
    RefusedWith(Frame(1, 0.0f, HUGE_VAL, Btdt(0, IdentityTran())),
                DiagnosticCode::TimestampInvalid, "a non-finite uttm");

    // Zero is the value every measured session's first frame carried, so it is
    // emphatically not the refusal above.
    MotionPacket packet;
    const Bytes datagram = Frame(0, 0.0f, 1786492800.0, Btdt(0, IdentityTran()));
    assert(Decode(datagram, &packet));
    assert(packet.frame->streamSeconds == 0.0f);
}

void
TestABoneIsRefusedRatherThanAFrame()
{
    const float nan = std::nanf("");
    const Bytes datagram =
        Frame(9, 0.5f, 1786492800.0,
              Join({Btdt(0, IdentityTran()),
                    // A rotation component that is not finite.
                    Btdt(1, Tran(nan, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f)),
                    // A rotation that names no orientation.
                    Btdt(2, Tran(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f)),
                    // A translation component that is not finite.
                    Btdt(3, Tran(0.0f, 0.0f, 0.0f, 1.0f, 0.0f, HUGE_VALF,
                                 0.0f)),
                    Btdt(4, IdentityTran())}));

    MotionPacket packet;
    std::vector<Diagnostic> diagnostics;
    // The frame decodes: its framing was never in doubt, and refusing the
    // datagram would throw away the two bones the device sent correctly.
    assert(Decode(datagram, &packet, &diagnostics));
    assert(packet.refusedBones == 3);
    assert(packet.frame->bones.size() == 2);
    assert(packet.frame->bones[0].boneId == 0);
    assert(packet.frame->bones[1].boneId == 4);
    assert(diagnostics.size() == 3);
    for (const Diagnostic& diagnostic : diagnostics) {
        assert(diagnostic.code == DiagnosticCode::NonFiniteTransform);
        // The bone, the frame's clock and the frame's counter, so a diagnostic
        // can be placed in a session without the packet it came from.
        assert(diagnostic.subject.find("bone ") == 0);
        assert(diagnostic.timestamp.has_value());
        assert(diagnostic.sequence.has_value());
        assert(*diagnostic.sequence == 9);
    }

    // Appended to, never cleared: a receive loop accumulates a session's worth.
    const Bytes second = OneBoneFrame();
    assert(Decode(second, &packet, &diagnostics));
    assert(diagnostics.size() == 3);
}

void
TestAnUnknownChunkIsCountedAndNotInterpreted()
{
    // One unknown beside the payload, one inside `fram`, and one inside every
    // bone record. The last is the reason the report is deduplicated: a rig with
    // 27 bones would otherwise say the same thing 27 times.
    Bytes fnum;
    AppendU32(&fnum, 3);
    Bytes records;
    for (std::uint16_t id = 0; id < 27; ++id) {
        const Bytes record =
            Chunk("btdt", Join({BoneId(id), IdentityTran(),
                                Chunk("conf", Bytes(4, 0))}));
        records.insert(records.end(), record.begin(), record.end());
    }
    const Bytes datagram = Join(
        {Head(), Sender(),
         Chunk("fram", Join({Chunk("fnum", fnum), Chunk("time", Bytes(4, 0)),
                             Chunk("uttm", Bytes(8, 0)),
                             Chunk("tmcd", Bytes(6, 0)),
                             Chunk("btrs", records),
                             Chunk("xxxx", Bytes{1})})),
         Chunk("yyyy", Bytes{2})});

    MotionPacket packet;
    std::vector<Diagnostic> diagnostics;
    assert(Decode(datagram, &packet, &diagnostics));
    // Not a diagnostic: an application newer than the measurement is not a
    // defect, and there is no code in the frozen set for one.
    assert(diagnostics.empty());
    assert(packet.frame->bones.size() == 27);
    assert(packet.unread.size() == 3);

    const auto found = [&packet](std::string_view tag,
                                 std::string_view container) {
        return std::any_of(packet.unread.begin(), packet.unread.end(),
                           [&](const vrmAdapterMocopi::UnreadChunk& entry) {
                               return entry.tag == tag
                                      && entry.container == container;
                           });
    };
    assert(found("yyyy", "datagram"));
    assert(found("xxxx", "fram"));
    assert(found("conf", "btdt"));
}

void
TestTheMeasuredBoneCountIsNotARequirement()
{
    // Eleven bones is a different rig, not a malformed packet. A decoder that
    // required the count it happened to measure would refuse rigs.
    Bytes records;
    for (std::uint16_t id = 0; id < 11; ++id) {
        const Bytes record = Btdt(id, IdentityTran());
        records.insert(records.end(), record.begin(), record.end());
    }
    MotionPacket packet;
    const Bytes datagram = Frame(1, 0.0f, 1786492800.0, records);
    assert(Decode(datagram, &packet));
    assert(packet.frame->bones.size() == 11);
    assert(vrmAdapterMocopi::MeasuredBoneCount == 27);

    // And a frame with no bones at all decodes. Whether that is a usable frame
    // is the assembler's question -- VRM_MOCOPI_FRAME_INCOMPLETE is its code --
    // so this layer does not pre-empt it.
    const Bytes noBones = Frame(2, 0.0f, 1786492800.0, Bytes{});
    assert(Decode(noBones, &packet));
    assert(packet.frame->bones.empty());
}

void
TestBoneIdsAreReportedNotJudged()
{
    // Out of order, duplicated, and far past the measured count. None of it is
    // this layer's business: `bnid` is a position in a record list, which is why
    // the frozen set has no duplicate-bone code where the sibling's does.
    const Bytes datagram =
        Frame(1, 0.0f, 1786492800.0,
              Join({Btdt(9, IdentityTran()), Btdt(3, IdentityTran()),
                    Btdt(3, IdentityTran()), Btdt(4000, IdentityTran())}));
    MotionPacket packet;
    std::vector<Diagnostic> diagnostics;
    assert(Decode(datagram, &packet, &diagnostics));
    assert(diagnostics.empty());
    assert(packet.frame->bones.size() == 4);
    assert(packet.frame->bones[0].boneId == 9);
    assert(packet.frame->bones[2].boneId == 3);
    assert(packet.frame->bones[3].boneId == 4000);
}

// ---------------------------------------------------------------------------
// Corpus mode
// ---------------------------------------------------------------------------

int
Failed(const std::string& name, const std::string& why)
{
    std::fprintf(stderr, "%s: %s\n", name.c_str(), why.c_str());
    return 1;
}

// The rest offsets the generator invented, in bone order. Repeated here rather
// than read from the capture so that the "only the root translates" invariant is
// checked against a stated table instead of against the fixture agreeing with
// itself.
const float kRestOffsets[27][3] = {
    {0.0f, 0.90f, 0.0f},     {0.0f, 0.06f, 0.0f},    {0.0f, 0.06f, 0.0f},
    {0.0f, 0.06f, 0.0f},     {0.0f, 0.06f, 0.0f},    {0.0f, 0.06f, 0.0f},
    {0.0f, 0.06f, 0.0f},     {0.0f, 0.10f, 0.0f},    {0.0f, 0.05f, 0.0f},
    {0.0f, 0.05f, 0.0f},     {0.0f, 0.05f, 0.0f},    {0.02f, -0.08f, 0.08f},
    {0.14f, 0.0f, 0.0f},     {0.30f, 0.0f, 0.0f},    {0.25f, 0.0f, 0.0f},
    {-0.02f, -0.08f, 0.08f}, {-0.14f, 0.0f, 0.0f},   {-0.30f, 0.0f, 0.0f},
    {-0.25f, 0.0f, 0.0f},    {0.09f, -0.05f, 0.0f},  {0.0f, -0.40f, 0.0f},
    {0.0f, -0.42f, 0.0f},    {0.0f, -0.10f, 0.13f},  {-0.09f, -0.05f, 0.0f},
    {0.0f, -0.40f, 0.0f},    {0.0f, -0.42f, 0.0f},   {0.0f, -0.10f, 0.13f},
};

const std::int16_t kParents[27] = {-1, 0,  1,  2,  3,  4,  5,  6,  7,
                                   8,  9,  7,  11, 12, 13, 7,  15, 16,
                                   17, 0,  19, 20, 21, 0,  23, 24, 25};

int
CheckNeutralStanding(const std::vector<MotionPacket>& packets,
                     const std::string& name)
{
    if (packets.size() != 6 || packets[0].kind != MotionPacketKind::Skeleton
        || !packets[0].skeleton.has_value()) {
        return Failed(name, "expected a skeleton packet then five frames");
    }
    const auto& bones = packets[0].skeleton->bones;
    if (bones.size() != 27) {
        return Failed(name, "the skeleton is not 27 bones");
    }
    for (std::size_t index = 0; index < bones.size(); ++index) {
        if (bones[index].boneId != index
            || bones[index].parentBoneId != kParents[index]) {
            return Failed(name, "the parent column is not the measured one");
        }
        // Every rest rotation is identity, in all 36 measured skeleton packets.
        if (!NearlyEqual(bones[index].restTransform.rotation[3], 1.0f)) {
            return Failed(name, "a rest rotation is not identity");
        }
    }

    std::uint32_t expected = 1000;
    for (std::size_t index = 1; index < packets.size(); ++index) {
        // Checked, not assumed: a regression that turned one of these into a
        // skeleton should print which claim broke, not dereference an empty
        // optional.
        if (packets[index].kind != MotionPacketKind::Frame
            || !packets[index].frame.has_value()) {
            return Failed(name, "a datagram after the first is not a frame");
        }
        const auto& frame = *packets[index].frame;
        if (frame.frameNumber != expected++) {
            return Failed(name, "the frame counter is not contiguous");
        }
        if (!NearlyEqual(frame.streamSeconds,
                         static_cast<float>(index - 1) / 60.0f)) {
            return Failed(name, "the stream clock is not 60 Hz");
        }
        // The measured invariant: only the root translates, and every other
        // bone's frame translation is its rest offset bit for bit.
        for (const BoneFrame& bone : frame.bones) {
            if (bone.boneId == 0) {
                continue;
            }
            for (int axis = 0; axis < 3; ++axis) {
                if (bone.transform.translation[axis]
                    != kRestOffsets[bone.boneId][axis]) {
                    return Failed(name,
                                  "a non-root translation is not its rest "
                                  "offset");
                }
            }
        }
    }
    return 0;
}

int
CheckArmsLowered(const std::vector<MotionPacket>& packets,
                 const std::string& name)
{
    // About 85 degrees, carried in the third imaginary component and in opposite
    // directions on the two sides -- bones 12 and 16, which handedness now says
    // are the left and the right upper arm.
    //
    // The check is still on the *component* and the antisymmetry rather than on a
    // side, and that is deliberate rather than left over. This layer names no
    // bone and changes no basis, so an assertion that "the left arm rotated"
    // would be testing `MocopiSkeletonMap`, which does not exist yet, through a
    // decoder that cannot be wrong about it.
    const float expected = std::sin(85.0f * 3.14159265f / 180.0f / 2.0f);
    for (const MotionPacket& packet : packets) {
        if (packet.kind != MotionPacketKind::Frame) {
            continue;
        }
        const BoneFrame* plusSide = nullptr;
        const BoneFrame* minusSide = nullptr;
        for (const BoneFrame& bone : packet.frame->bones) {
            if (bone.boneId == 12) {
                plusSide = &bone;
            }
            if (bone.boneId == 16) {
                minusSide = &bone;
            }
        }
        if (plusSide == nullptr || minusSide == nullptr) {
            return Failed(name, "the upper-arm bones are missing");
        }
        if (std::fabs(plusSide->transform.rotation[2] + expected) > 1e-5f
            || std::fabs(minusSide->transform.rotation[2] - expected) > 1e-5f) {
            return Failed(name,
                          "the rotation is not in the third imaginary "
                          "component, or is not antisymmetric");
        }
        // The other two imaginary components are exactly zero, so a decoder that
        // rotated the quaternion cannot pass.
        if (plusSide->transform.rotation[0] != 0.0f
            || plusSide->transform.rotation[1] != 0.0f) {
            return Failed(name, "the quaternion components were reordered");
        }

        // The capture is tagged `root-motion` and the root is the one thing that
        // varies across its three frames, so it is checked here rather than
        // left to the tag. `CheckNeutralStanding` deliberately skips bone 0 while
        // proving the *other* half of the same invariant -- that nothing else
        // moves -- which would leave the root unpinned in both places.
        const BoneFrame* root = nullptr;
        for (const BoneFrame& bone : packet.frame->bones) {
            if (bone.boneId == 0) {
                root = &bone;
            }
        }
        if (root == nullptr) {
            return Failed(name, "the root bone is missing");
        }
        const auto step = static_cast<float>(packet.frame->frameNumber - 2000);
        if (!NearlyEqual(root->transform.translation[0], 0.0f)
            || !NearlyEqual(root->transform.translation[1], 0.90f - 0.01f * step)
            || !NearlyEqual(root->transform.translation[2], 0.02f * step)) {
            return Failed(name, "the root translation is not the recorded one");
        }
        if (step != 0.0f
            && root->transform.translation[1] == kRestOffsets[0][1]) {
            return Failed(name, "the root did not move off its rest offset");
        }
    }
    return 0;
}

int
CheckFrameLoss(const std::vector<MotionPacket>& packets,
               const std::string& name)
{
    std::vector<std::uint32_t> numbers;
    std::vector<float> seconds;
    for (const MotionPacket& packet : packets) {
        if (packet.kind == MotionPacketKind::Frame) {
            numbers.push_back(packet.frame->frameNumber);
            seconds.push_back(packet.frame->streamSeconds);
        }
    }
    const std::vector<std::uint32_t> expected = {3000, 3001, 3002, 3005,
                                                 3006, 3006, 1};
    if (numbers != expected) {
        return Failed(name, "the counter sequence is not the recorded one");
    }
    // The measured shape of transport loss: the counter skipped 3 and the
    // sender's clock skipped exactly 3/60 s with it, which is how the recorded
    // sessions proved the loss was Wi-Fi's and not the device's.
    if (!NearlyEqual(seconds[3] - seconds[2], 3.0f / 60.0f)) {
        return Failed(name, "the gap's clock delta is not 3/60 s");
    }
    // A restart is not this layer's to detect, and nothing here refused it.
    if (seconds.back() != 0.0f) {
        return Failed(name, "the restart's clock did not begin again");
    }
    return 0;
}

int
CheckExtendedForm(const std::vector<MotionPacket>& packets,
                  const std::string& name)
{
    bool sawUnread = false;
    bool sawShortRig = false;
    bool sawTimecode = false;
    for (const MotionPacket& packet : packets) {
        if (!packet.unread.empty()) {
            sawUnread = true;
        }
        const std::size_t count = packet.frame.has_value()
                                      ? packet.frame->bones.size()
                                      : packet.skeleton->bones.size();
        if (count == 11) {
            sawShortRig = true;
        }
        if (packet.frame.has_value()) {
            for (const std::uint8_t byte : packet.frame->timecode) {
                if (byte != 0) {
                    sawTimecode = true;
                }
            }
        }
    }
    if (!sawUnread) {
        return Failed(name, "no unknown chunk was carried forward");
    }
    if (!sawShortRig) {
        return Failed(name, "the 11-bone rig was refused or absent");
    }
    if (!sawTimecode) {
        return Failed(name, "the non-zero timecode did not survive");
    }
    return 0;
}

int
CheckCorpus(const std::filesystem::path& directory)
{
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        // `is_regular_file` as well as the extension, which is what the format
        // suite does over this same directory. The corpus README invites an
        // operator to drop a BVH Sender recording in here, and a directory or a
        // dangling symlink with the right suffix should read as "that is not a
        // capture" rather than as a parse failure on line 0.
        if (entry.is_regular_file()
            && entry.path().extension() == ".mocopipackets") {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    if (files.empty()) {
        std::fprintf(stderr, "%s: no captures found\n",
                     directory.string().c_str());
        return 1;
    }

    int failures = 0;
    for (const std::filesystem::path& file : files) {
        const std::string name = file.filename().string();
        vrmAdapterMocopi::PacketCapture capture;
        vrmAdapterMocopi::PacketCaptureError error;
        if (!vrmAdapterMocopi::ReadPacketCaptureFile(file.string(), &capture,
                                                     &error)) {
            failures += Failed(name, "line " + std::to_string(error.line) + ": "
                                         + error.message);
            continue;
        }

        std::vector<MotionPacket> decoded;
        std::vector<Diagnostic> diagnostics;
        // Per datagram, in order: the code its refusal reported, or Count when it
        // decoded. Kept alongside `diagnostics` so a code can be attributed to the
        // datagram that caused it rather than to the capture as a whole.
        std::vector<DiagnosticCode> refusals;
        for (const RecordedDatagram& datagram : capture.datagrams) {
            MotionPacket packet;
            const std::size_t before = diagnostics.size();
            if (vrmAdapterMocopi::DecodeMotionPacket(datagram.bytes, &packet,
                                                     &diagnostics)) {
                decoded.push_back(std::move(packet));
                refusals.push_back(DiagnosticCode::Count);
            } else if (diagnostics.size() == before) {
                // A refusal that reported nothing breaks the header's contract,
                // and a live receiver would be dropping a datagram with nothing
                // to log.
                failures += Failed(name,
                                   "a datagram was refused with no diagnostic");
                refusals.push_back(DiagnosticCode::Count);
            } else {
                refusals.push_back(diagnostics.back().code);
            }
        }

        // The captures whose whole purpose is to be refused. Asserting the *code*
        // per datagram, and not merely the count, is the point: a decoder that
        // refused the cross-protocol datagram as a bad timestamp, or reported
        // PACKET_MALFORMED where NON_FINITE_TRANSFORM belongs, would otherwise
        // stay green while this file, the CMake comment and the corpus README all
        // claim every capture is refused with the code it exists for.
        static const std::vector<DiagnosticCode> kContainerCodes(
            5, DiagnosticCode::PacketMalformed);
        static const std::vector<DiagnosticCode> kPacketCodes = {
            DiagnosticCode::PacketMalformed,    // the wrong magic
            DiagnosticCode::PacketMalformed,    // an unmeasured version
            DiagnosticCode::PacketMalformed,    // no head
            DiagnosticCode::PacketMalformed,    // both payload kinds
            DiagnosticCode::PacketMalformed,    // neither
            DiagnosticCode::PacketMalformed,    // a duplicated fnum
            DiagnosticCode::PacketMalformed,    // a two-byte fnum
            DiagnosticCode::PacketMalformed,    // a six-float tran
            DiagnosticCode::TimestampInvalid,   // time is NaN
            DiagnosticCode::TimestampInvalid,   // time is negative
            DiagnosticCode::TimestampInvalid,   // uttm is not finite
            DiagnosticCode::PacketMalformed,    // a four-byte tmcd
            DiagnosticCode::NonFiniteTransform, // a skeleton refused whole
        };
        const std::vector<DiagnosticCode>* expectedCodes = nullptr;
        if (capture.sourceId == "malformed-container-01") {
            expectedCodes = &kContainerCodes;
        } else if (capture.sourceId == "malformed-packets-01") {
            expectedCodes = &kPacketCodes;
        }
        if (expectedCodes != nullptr) {
            if (refusals != *expectedCodes) {
                std::string detail = "the refusal codes are";
                for (const DiagnosticCode code : refusals) {
                    detail += ' ';
                    detail +=
                        code == DiagnosticCode::Count
                            ? std::string("DECODED")
                            : std::string(
                                vrmAdapterMocopi::DiagnosticCodeString(code));
                }
                failures += Failed(name, detail);
            } else {
                std::printf("%s: %zu datagram(s), each refused with the code it "
                            "exists for\n",
                            name.c_str(), refusals.size());
            }
            continue;
        }

        bool anyRefused = false;
        for (const DiagnosticCode code : refusals) {
            if (code != DiagnosticCode::Count) {
                anyRefused = true;
            }
        }
        if (anyRefused) {
            failures += Failed(name, "a datagram was refused");
            continue;
        }

        // The clean captures report nothing at all, except the one whose whole
        // subject is a bone the decoder drops. An unknown chunk is explicitly not
        // a diagnostic, and `extended-form` is the capture that would catch a
        // decoder that started treating it as one.
        const std::size_t expectedDiagnostics =
            capture.sourceId == "refused-bones-01" ? 3 : 0;
        if (diagnostics.size() != expectedDiagnostics) {
            failures += Failed(name, std::to_string(diagnostics.size())
                                         + " diagnostic(s), expected "
                                         + std::to_string(expectedDiagnostics));
            continue;
        }

        int result = 0;
        if (capture.sourceId == "neutral-standing-01") {
            result = CheckNeutralStanding(decoded, name);
        } else if (capture.sourceId == "arms-lowered-01") {
            result = CheckArmsLowered(decoded, name);
        } else if (capture.sourceId == "frame-loss-01") {
            result = CheckFrameLoss(decoded, name);
        } else if (capture.sourceId == "extended-form-01") {
            result = CheckExtendedForm(decoded, name);
        } else if (capture.sourceId == "refused-bones-01") {
            if (decoded.size() != 2 || decoded[0].refusedBones != 3
                || decoded[0].frame->bones.size() != 24
                || decoded[1].refusedBones != 0
                || decoded[1].frame->bones.size() != 27) {
                result = Failed(name,
                                "the bone-not-frame rule does not hold on this "
                                "capture");
            }
        } else {
            // A capture nothing asserts against is a capture that pins nothing,
            // and the corpus is small enough that this is a mistake rather than
            // a policy.
            result = Failed(name, "no assertion is registered for sourceId '"
                                      + capture.sourceId + "'");
        }
        failures += result;
        if (result == 0) {
            std::printf("%s: %zu packet(s) decoded, %zu diagnostic(s)\n",
                        name.c_str(), decoded.size(), diagnostics.size());
        }
    }

    if (failures != 0) {
        std::fprintf(stderr, "%d corpus capture(s) failed\n", failures);
        return 1;
    }
    std::printf("mocopi motion packet corpus: %zu capture(s) verified\n",
                files.size());
    return 0;
}

} // namespace

int
main(int argc, char** argv)
{
    if (argc > 1) {
        return CheckCorpus(std::filesystem::path(argv[1]));
    }

    TestTheContainerClosesOrIsRefused();
    TestALeafIsReadAtItsExactWidth();
    TestAFrameDecodes();
    TestASkeletonDecodes();
    TestASkeletonWithAnUnusableBoneIsRefusedWhole();
    TestARefusalNamesAPositionInTheDatagram();
    TestTheQuaternionIsScalarLastAndIsNotReordered();
    TestTheContainerRefusalsReachThePacketLayer();
    TestTheMagicAndTheVersionAreTheOnlyGates();
    TestAFieldMustMatchItsType();
    TestAnUnusableClockRefusesTheFrame();
    TestABoneIsRefusedRatherThanAFrame();
    TestAnUnknownChunkIsCountedAndNotInterpreted();
    TestTheMeasuredBoneCountIsNotARequirement();
    TestBoneIdsAreReportedNotJudged();
    std::puts("vrmAdapterMocopi motion packet tests passed");
    return 0;
}
