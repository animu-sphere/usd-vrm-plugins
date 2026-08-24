// SPDX-License-Identifier: Apache-2.0
//
// The OSC layer, with no VMC semantics anywhere in it.
//
// The unit tests build datagrams byte by byte, because the point of this layer
// is what it does with bytes. Corpus mode then runs the decoder over every
// committed capture, which is where the two halves meet: the recorded corpus
// was authored by a generator that encodes OSC, this decodes it, and neither
// was written from the other.
#include "vrmAdapterVmc/OscPacket.h"

#include "vrmAdapterVmc/Diagnostics.h"
#include "vrmAdapterVmc/PacketCapture.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <initializer_list>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace
{

using vrmAdapterVmc::Diagnostic;
using vrmAdapterVmc::DiagnosticCode;
using vrmAdapterVmc::OscPacket;

// ---------------------------------------------------------------------------
// Byte assembly. Big-endian throughout, like the wire.
// ---------------------------------------------------------------------------

struct Bytes
{
    std::vector<std::uint8_t> data;

    Bytes& Raw(std::initializer_list<std::uint8_t> values)
    {
        data.insert(data.end(), values);
        return *this;
    }

    Bytes& Append(const std::vector<std::uint8_t>& values)
    {
        data.insert(data.end(), values.begin(), values.end());
        return *this;
    }

    // A NUL-terminated string padded to four bytes -- always at least one NUL.
    Bytes& Str(std::string_view text)
    {
        data.insert(data.end(), text.begin(), text.end());
        data.push_back(0);
        while (data.size() % 4 != 0) {
            data.push_back(0);
        }
        return *this;
    }

    Bytes& U32(std::uint32_t value)
    {
        data.push_back(static_cast<std::uint8_t>(value >> 24));
        data.push_back(static_cast<std::uint8_t>(value >> 16));
        data.push_back(static_cast<std::uint8_t>(value >> 8));
        data.push_back(static_cast<std::uint8_t>(value));
        return *this;
    }

    Bytes& U64(std::uint64_t value)
    {
        U32(static_cast<std::uint32_t>(value >> 32));
        U32(static_cast<std::uint32_t>(value));
        return *this;
    }

    Bytes& F32(float value)
    {
        std::uint32_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        return U32(bits);
    }

    Bytes& F64(double value)
    {
        std::uint64_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        return U64(bits);
    }
};

std::vector<std::uint8_t>
Message(std::string_view address, std::string_view tags,
        const std::vector<std::uint8_t>& body = {})
{
    Bytes out;
    out.Str(address);
    out.Str(std::string(",") + std::string(tags));
    out.Append(body);
    return out.data;
}

std::vector<std::uint8_t>
Bundle(std::uint64_t timeTag,
       std::initializer_list<std::vector<std::uint8_t>> elements)
{
    Bytes out;
    out.Str("#bundle");
    out.U64(timeTag);
    for (const std::vector<std::uint8_t>& element : elements) {
        out.U32(static_cast<std::uint32_t>(element.size()));
        out.Append(element);
    }
    return out.data;
}

bool
Decode(const std::vector<std::uint8_t>& datagram, OscPacket* packet,
       Diagnostic* diagnostic = nullptr)
{
    return vrmAdapterVmc::DecodeOscPacket(datagram, packet, diagnostic);
}

// A decoded packet points into the datagram, so decoding a temporary is a read
// of freed memory the moment the full expression ends -- which is how the first
// version of this file was written, and it passed on Windows and aborted on
// Linux and macOS. `DecodeOscPacket` refuses an rvalue for that reason, and
// this wrapper has to refuse one too: taking `const&` and forwarding an lvalue
// would launder the temporary straight past the guard.
bool Decode(std::vector<std::uint8_t>&& datagram, OscPacket* packet,
            Diagnostic* diagnostic = nullptr) = delete;

// ---------------------------------------------------------------------------

void
TestAMessageDecodesToItsArguments()
{
    // The shape VMC uses for every transform: a name and seven floats.
    Bytes body;
    body.Str("Hips");
    body.F32(0.0f).F32(0.9f).F32(0.0f);
    body.F32(0.0f).F32(0.0f).F32(0.0f).F32(1.0f);

    // Named, not a temporary: every view in the decoded packet points into it.
    const std::vector<std::uint8_t> datagram =
        Message("/VMC/Ext/Bone/Pos", "sfffffff", body.data);

    OscPacket packet;
    Diagnostic diagnostic;
    if (!Decode(datagram, &packet, &diagnostic)) {
        std::fprintf(stderr, "%s\n",
                     vrmAdapterVmc::FormatDiagnostic(diagnostic).c_str());
        assert(false);
    }

    assert(!packet.bundled);
    // An unbundled datagram carries no time tag of its own; the default reads
    // as "immediately", which is what a lone message means.
    assert(packet.timeTag == vrmAdapterVmc::OscTimeTagImmediate);
    assert(packet.messages.size() == 1);
    const auto& message = packet.messages.front();
    assert(message.address == "/VMC/Ext/Bone/Pos");
    assert(message.typeTags == "sfffffff");
    assert(message.arguments.size() == 8);
    assert(message.arguments[0].tag == 's');
    assert(message.arguments[0].text == "Hips");
    assert(message.arguments[2].real == 0.9f);
    assert(message.arguments[7].real == 1.0f);
    // Nothing is coerced: a float argument leaves `integer` alone, so a caller
    // that reads the wrong field gets zero rather than a plausible number.
    assert(message.arguments[7].integer == 0);
}

void
TestEveryOscTypeTagIsSized()
{
    // Not one of these appears in VMC. They are decoded anyway because
    // *skipping* an argument needs its size: a decoder that knew only i, f and
    // s would have to refuse a valid message the moment a sender attached a
    // 'd', which is blaming the sender for the decoder's gap.
    const std::vector<std::uint8_t> blob = {0xde, 0xad, 0xbe, 0xef, 0x01};
    Bytes body;
    body.U32(0x7fffffff);                 // i
    body.F32(0.5f);                       // f
    body.Str("text");                     // s
    body.Str("symbol");                   // S
    body.U32(static_cast<std::uint32_t>(blob.size()));
    body.Append(blob).Raw({0, 0, 0});     // b, padded to four
    body.U64(0x0123456789abcdefULL);      // h
    body.U64(1);                          // t
    body.F64(0.25);                       // d
    body.U32('A');                        // c
    body.U32(0x11223344);                 // r
    body.U32(0x55667788);                 // m
                                          // T F N I [ ] carry no bytes

    const std::vector<std::uint8_t> datagram =
        Message("/every/tag", "ifsSbhtdcrmTFNI[]", body.data);

    OscPacket packet;
    Diagnostic diagnostic;
    if (!Decode(datagram, &packet, &diagnostic)) {
        std::fprintf(stderr, "%s\n",
                     vrmAdapterVmc::FormatDiagnostic(diagnostic).c_str());
        assert(false);
    }

    const auto& arguments = packet.messages.front().arguments;
    assert(arguments.size() == 17);
    assert(arguments[0].integer == 0x7fffffff);
    assert(arguments[1].real == 0.5f);
    assert(arguments[2].text == "text");
    assert(arguments[3].text == "symbol");
    assert(arguments[4].blob.size == blob.size());
    assert(std::memcmp(arguments[4].blob.bytes, blob.data(), blob.size()) == 0);
    assert(arguments[5].integer == 0x0123456789abcdefLL);
    assert(arguments[6].integer == 1);
    assert(arguments[7].real == 0.25);
    assert(arguments[8].integer == 'A');
    assert(arguments[9].integer == 0x11223344);
    assert(arguments[10].integer == 0x55667788);
    assert(arguments[11].tag == 'T' && arguments[11].integer == 1);
    assert(arguments[12].tag == 'F' && arguments[12].integer == 0);
    assert(arguments[13].tag == 'N');
    assert(arguments[14].tag == 'I');
    // The array delimiters are kept rather than dropped, so the tag string and
    // the arguments stay index-for-index.
    assert(arguments[15].tag == '[');
    assert(arguments[16].tag == ']');
    assert(packet.messages.front().typeTags.size() == arguments.size());
}

void
TestABundleFlattensIntoWireOrder()
{
    Bytes time;
    time.F32(12.5f);
    Bytes value;
    value.Str("Joy").F32(1.0f);

    const std::vector<std::uint8_t> datagram = Bundle(
        vrmAdapterVmc::OscTimeTagImmediate,
        {Message("/VMC/Ext/T", "f", time.data),
         Message("/VMC/Ext/Blend/Val", "sf", value.data),
         Message("/VMC/Ext/Blend/Apply", "")});

    OscPacket packet;
    assert(Decode(datagram, &packet));
    assert(packet.bundled);
    assert(packet.timeTag == vrmAdapterVmc::OscTimeTagImmediate);
    assert(packet.messages.size() == 3);
    assert(packet.messages[0].address == "/VMC/Ext/T");
    assert(packet.messages[1].address == "/VMC/Ext/Blend/Val");
    // A message with no arguments is well-formed: the type tag string is just
    // the comma.
    assert(packet.messages[2].address == "/VMC/Ext/Blend/Apply");
    assert(packet.messages[2].typeTags.empty());
    assert(packet.messages[2].arguments.empty());

    // An empty bundle carries nothing and is still valid OSC.
    const std::vector<std::uint8_t> hollow =
        Bundle(vrmAdapterVmc::OscTimeTagImmediate, {});
    OscPacket empty;
    assert(Decode(hollow, &empty));
    assert(empty.bundled);
    assert(empty.messages.empty());
}

void
TestNestedBundlesFlattenAndDepthIsCapped()
{
    const std::vector<std::uint8_t> inner =
        Bundle(2, {Message("/inner/one", ""), Message("/inner/two", "")});
    const std::vector<std::uint8_t> outer =
        Bundle(1, {Message("/outer", ""), inner});
    OscPacket packet;
    assert(Decode(outer, &packet));
    assert(packet.messages.size() == 3);
    assert(packet.messages[0].address == "/outer");
    assert(packet.messages[2].address == "/inner/two");
    // The outermost time tag is the one kept; the nested one is packaging.
    assert(packet.timeTag == 1);

    std::vector<std::uint8_t> nested = Message("/deep", "");
    for (std::size_t depth = 0; depth <= vrmAdapterVmc::MaxOscBundleDepth;
         ++depth) {
        nested = Bundle(1, {nested});
    }
    OscPacket refused;
    Diagnostic diagnostic;
    assert(!Decode(nested, &refused, &diagnostic));
    assert(diagnostic.code == DiagnosticCode::PacketMalformed);
    assert(diagnostic.detail.find("nested") != std::string::npos);
}

void
TestValidButUnimplementedAddressesDecodeCleanly()
{
    // This layer cannot tell an unimplemented address from any other one,
    // because it does not know what an address means. Deciding that neither of
    // these is implemented belongs one layer up, as VRM_VMC_UNSUPPORTED_MESSAGE
    // -- which is never raised from here.
    Bytes one;
    one.U32(1);
    Bytes three;
    three.U32(1).U32(60).U32(100);

    const std::vector<std::uint8_t> outside = Message("/foo/bar", "i", one.data);
    const std::vector<std::uint8_t> unimplemented =
        Message("/VMC/Ext/Midi/Note", "iii", three.data);

    OscPacket packet;
    assert(Decode(outside, &packet));
    assert(packet.messages.front().address == "/foo/bar");
    assert(Decode(unimplemented, &packet));
    assert(packet.messages.front().address == "/VMC/Ext/Midi/Note");
    assert(packet.messages.front().arguments[1].integer == 60);
}

struct BadDatagram
{
    const char* name;
    std::vector<std::uint8_t> bytes;
    const char* detail;
};

std::vector<std::uint8_t>
Concatenated(std::vector<std::uint8_t> head,
             std::initializer_list<std::uint8_t> tail)
{
    head.insert(head.end(), tail);
    return head;
}

void
TestMalformedDatagramsAreRefusedAndSayWhy()
{
    Bytes oneFloat;
    oneFloat.F32(0.5f);

    // A bundle whose element size claims more than the bundle holds.
    Bytes overrun;
    overrun.Str("#bundle").U64(1).U32(512).Append(
        Message("/VMC/Ext/T", "f", oneFloat.data));

    // A blob claiming more bytes than the message carries.
    Bytes longBlob;
    longBlob.U32(64).Raw({1, 2, 3, 4});

    const BadDatagram cases[] = {
        {"empty", {}, "empty"},
        {"unaligned", {'/', 'a', 'b'}, "multiple of four"},
        {"neither a message nor a bundle",
         {0xde, 0xad, 0xbe, 0xef}, "not '/'"},
        {"an address with no terminator",
         {'/', 'V', 'M', 'C'}, "address pattern has no terminator"},
        // Built by hand rather than with `Message`, which always writes the
        // comma.
        {"a type tag string with no comma",
         Bytes().Str("/VMC/Ext/T").Str("f").F32(0.5f).data,
         "does not begin with a comma"},
        {"a message with no type tag string",
         Bytes().Str("/VMC/Ext/T").data, "type tag string has no terminator"},
        {"an argument the type tags promised",
         Message("/VMC/Ext/T", "ff", oneFloat.data), "needs 4 bytes"},
        {"a string argument with no terminator",
         Concatenated(Message("/VMC/Ext/Bone/Pos", "s"),
                      {'H', 'i', 'p', 's'}),
         "argument has no terminator"},
        {"a blob running past the message",
         Message("/blob", "b", longBlob.data), "runs past the end"},
        {"a blob of negative length",
         Message("/blob", "b", Bytes().U32(0xffffffff).data),
         "declares -1 bytes"},
        // The one refusal that is about the decoder's own limits rather than
        // the sender's bytes -- and it has to be a refusal: an unknown tag has
        // an unknown size, so everything after it would be read at the wrong
        // offset.
        {"an unknown type tag", Message("/VMC/Ext/T", "q"),
         "unknown type tag 'q'"},
        {"bytes after the arguments",
         Concatenated(Message("/VMC/Ext/T", "f", oneFloat.data),
                      {0, 0, 0, 0}),
         "follow the arguments"},
        {"a bundle element running past the bundle", overrun.data,
         "runs past the end of the bundle"},
        {"a bundle with no time tag", Bytes().Str("#bundle").data,
         "eight-byte time tag"},
        {"a bundle element declaring nothing",
         Bytes().Str("#bundle").U64(1).U32(0).data, "declares 0 bytes"},
        // The offset in this one points *into* the bundle, at the element, not
        // at the datagram's start -- which is the whole reason a nested decode
        // carries a base offset.
        {"a bundle element that is not a multiple of four",
         Bytes().Str("#bundle").U64(1).U32(3).Raw({'/', 'a', 'b', 0}).data,
         "multiple of four"},
    };

    for (const BadDatagram& testCase : cases) {
        const std::vector<std::uint8_t>& bytes = testCase.bytes;

        OscPacket packet;
        packet.messages.push_back({});
        Diagnostic diagnostic;
        if (Decode(bytes, &packet, &diagnostic)) {
            std::fprintf(stderr, "malformed datagram was accepted: %s\n",
                         testCase.name);
            assert(false);
        }

        // Every refusal from this layer is the protocol-level code, never the
        // canonical layer's and never UNSUPPORTED_MESSAGE.
        assert(diagnostic.code == DiagnosticCode::PacketMalformed);
        assert(diagnostic.recoverable);
        if (diagnostic.detail.find(testCase.detail) == std::string::npos) {
            std::fprintf(stderr, "%s: detail was \"%s\", expected \"%s\"\n",
                         testCase.name, diagnostic.detail.c_str(),
                         testCase.detail);
            assert(false);
        }
        // A byte offset, so a fixture can be found rather than bisected.
        assert(diagnostic.detail.find("at byte") != std::string::npos);
        // A refused datagram leaves the caller's packet as it was.
        assert(packet.messages.size() == 1);
    }
}

void
TestTheArgumentGuardsRefuseRatherThanDereference()
{
    const std::vector<std::uint8_t> datagram = Message("/VMC/Ext/T", "");
    OscPacket packet;
    Diagnostic diagnostic;

    // A caller with no output has nowhere for a decode to land, and a size with
    // no bytes describes a datagram that cannot exist. Both are caller bugs,
    // and both are reported rather than dereferenced.
    assert(!vrmAdapterVmc::DecodeOscPacket(datagram.data(), datagram.size(),
                                           nullptr, &diagnostic));
    assert(diagnostic.code == DiagnosticCode::PacketMalformed);
    assert(!vrmAdapterVmc::DecodeOscPacket(nullptr, 4, &packet, &diagnostic));
    assert(diagnostic.code == DiagnosticCode::PacketMalformed);
    assert(packet.messages.empty());

    // No diagnostic is the documented default, and it must not crash either.
    assert(!vrmAdapterVmc::DecodeOscPacket(nullptr, 4, &packet));
}

// ---------------------------------------------------------------------------
// OSC-0 characterisation
//
// Everything above was written beside the decoder. These were written to
// freeze what it does *before* it moves into a shared library
// (roadmap/osc-and-vrchat-trackers.md §9, OSC-0), and each names a behaviour
// the header promises that no test above would have caught the loss of. The
// step's acceptance criterion is that a change to `OscPacket.cpp` altering
// observable behaviour fails a test named for the behaviour, so the subject of
// each is the promise rather than the code path that keeps it.
// ---------------------------------------------------------------------------

void
TestARefusedBundleYieldsNoMessagesAtAll()
{
    // The header's first rule: "a bundle whose third element is malformed
    // yields no messages, not two". Every malformed case above puts the bad
    // bytes in the first element, so a decoder that appended messages as it
    // went would satisfy all of them -- it would refuse, and hand back the two
    // messages it had already accepted. Here that is the difference between
    // zero messages and two.
    Bytes time;
    time.F32(12.5f);
    Bytes oneFloat;
    oneFloat.F32(0.5f);

    const std::vector<std::uint8_t> datagram = Bundle(
        vrmAdapterVmc::OscTimeTagImmediate,
        {Message("/VMC/Ext/T", "f", time.data),
         Message("/VMC/Ext/Blend/Apply", ""),
         // Two floats promised, one delivered.
         Message("/VMC/Ext/Bone/Pos", "ff", oneFloat.data)});

    OscPacket packet;
    Diagnostic diagnostic;
    assert(!Decode(datagram, &packet, &diagnostic));
    assert(diagnostic.code == DiagnosticCode::PacketMalformed);
    assert(packet.messages.empty());
    // Not only the messages: the packaging the first two elements established
    // is not left behind either.
    assert(!packet.bundled);
    assert(packet.timeTag == vrmAdapterVmc::OscTimeTagImmediate);
}

void
TestADecodeReplacesWhateverTheCallerHandedIn()
{
    // A receive loop decodes datagram after datagram into one `OscPacket`.
    // Everything here is about that reuse: a decode overwrites all three
    // fields, and a refusal touches none of them.
    Bytes time;
    time.F32(12.5f);
    const std::vector<std::uint8_t> bundled =
        Bundle(7, {Message("/VMC/Ext/T", "f", time.data),
                   Message("/VMC/Ext/Blend/Apply", "")});
    const std::vector<std::uint8_t> lone = Message("/VMC/Ext/Blend/Apply", "");

    OscPacket packet;
    assert(Decode(bundled, &packet));
    assert(packet.messages.size() == 2);
    assert(packet.bundled);
    // A time tag that is not "immediately" survives verbatim; the flattening
    // discards the packaging, not the provenance.
    assert(packet.timeTag == 7);

    // The same packet, now given an unbundled single message: two messages
    // become one, `bundled` goes back to false, and the previous datagram's
    // time tag does not survive as this one's.
    assert(Decode(lone, &packet));
    assert(packet.messages.size() == 1);
    assert(!packet.bundled);
    assert(packet.timeTag == vrmAdapterVmc::OscTimeTagImmediate);

    // And a refusal leaves the last good decode intact, which is what lets a
    // caller keep using the packet it already has.
    const std::vector<std::uint8_t> refused = {0xde, 0xad, 0xbe, 0xef};
    assert(!Decode(refused, &packet));
    assert(packet.messages.size() == 1);
    assert(packet.messages.front().address == "/VMC/Ext/Blend/Apply");
    assert(!packet.bundled);
}

struct Refusal
{
    const char* name;
    std::vector<std::uint8_t> bytes;
    // The byte the diagnostic must name, and the address it must carry as its
    // subject -- empty where the refusal happened before one was read.
    std::size_t offset;
    const char* subject;
};

void
TestARefusalNamesTheByteAndTheAddress()
{
    // Two promises the header makes and the malformed suite above checks only
    // the shape of: the diagnostic carries "the offending address as its
    // subject where one was read, and a byte offset in its detail". That suite
    // asserts the substring "at byte" and never the number, so every offset
    // computation in the decoder -- and in particular the `base` a nested
    // decode carries -- could be wrong without failing anything.
    Bytes oneFloat;
    oneFloat.F32(0.5f);

    // A bad message one level down inside a bundle inside a bundle. Its offset
    // is the whole point: 60 is where those four extra bytes sit in *this
    // datagram*, and a decoder that reported an offset within the innermost
    // element would say 20.
    const std::vector<std::uint8_t> nested = Bundle(
        1, {Bundle(1, {Concatenated(Message("/VMC/Ext/T", "f", oneFloat.data),
                                    {0, 0, 0, 0})})});

    const Refusal cases[] = {
        {"an unaligned datagram", {'/', 'a', 'b'}, 0, ""},
        {"an address with no terminator", {'/', 'V', 'M', 'C'}, 0, ""},
        // The address was read, so from here on the subject is set even though
        // the message never decoded.
        {"a message with no type tag string", Bytes().Str("/VMC/Ext/T").data,
         12, "/VMC/Ext/T"},
        {"an argument the type tags promised",
         Message("/VMC/Ext/T", "ff", oneFloat.data), 20, "/VMC/Ext/T"},
        {"an unknown type tag", Message("/VMC/Ext/T", "q"), 16, "/VMC/Ext/T"},
        {"bytes after the arguments",
         Concatenated(Message("/VMC/Ext/T", "f", oneFloat.data), {0, 0, 0, 0}),
         20, "/VMC/Ext/T"},
        // Inside a bundle: the element starts at byte 20 of the datagram, and
        // that is the byte named rather than 0.
        {"a bundle element that is not a multiple of four",
         Bytes().Str("#bundle").U64(1).U32(3).Raw({'/', 'a', 'b', 0}).data, 20,
         ""},
        {"a message two bundles deep", nested, 60, "/VMC/Ext/T"},
    };

    for (const Refusal& testCase : cases) {
        const std::vector<std::uint8_t>& bytes = testCase.bytes;

        OscPacket packet;
        Diagnostic diagnostic;
        if (Decode(bytes, &packet, &diagnostic)) {
            std::fprintf(stderr, "malformed datagram was accepted: %s\n",
                         testCase.name);
            assert(false);
        }

        const std::string expected =
            "(at byte " + std::to_string(testCase.offset) + ")";
        if (diagnostic.detail.find(expected) == std::string::npos) {
            std::fprintf(stderr, "%s: detail was \"%s\", expected %s\n",
                         testCase.name, diagnostic.detail.c_str(),
                         expected.c_str());
            assert(false);
        }
        if (diagnostic.subject != testCase.subject) {
            std::fprintf(stderr, "%s: subject was \"%s\", expected \"%s\"\n",
                         testCase.name, diagnostic.subject.c_str(),
                         testCase.subject);
            assert(false);
        }
    }
}

void
TestDecodedViewsPointIntoTheCallersDatagram()
{
    // "The decoder copies no payload, because a live receiver would otherwise
    // allocate a string per bone per frame." The observable form of that
    // promise is that every view lies inside the caller's buffer -- which is
    // also the reason `DecodeOscPacket` refuses an rvalue, and a decoder that
    // started copying would turn that deleted overload into an inconvenience
    // rather than a guard.
    const std::vector<std::uint8_t> blob = {1, 2, 3, 4};
    Bytes body;
    body.Str("Hips");
    body.U32(static_cast<std::uint32_t>(blob.size()));
    body.Append(blob);
    const std::vector<std::uint8_t> datagram =
        Bundle(vrmAdapterVmc::OscTimeTagImmediate,
               {Message("/VMC/Ext/Bone/Pos", "sb", body.data)});

    OscPacket packet;
    assert(Decode(datagram, &packet));

    const std::uint8_t* first = datagram.data();
    const std::uint8_t* last = first + datagram.size();
    const auto inside = [first, last](const void* pointer) {
        const std::uint8_t* byte = static_cast<const std::uint8_t*>(pointer);
        return byte >= first && byte < last;
    };

    // A bundled datagram on purpose: the flattening is of the message list and
    // not of the bytes, so a bundled message's views point at the datagram too
    // rather than at some staging buffer the bundle walk allocated.
    assert(packet.bundled);
    const auto& message = packet.messages.front();
    assert(inside(message.address.data()));
    assert(inside(message.typeTags.data()));
    assert(inside(message.arguments[0].text.data()));
    assert(inside(message.arguments[1].blob.bytes));
}

void
TestArgumentSignednessFollowsTheWire()
{
    // The tag table sends five different tags into `integer` and they do not
    // agree about the sign bit: 'i' and 'h' are signed, 'c', 'r' and 'm' are
    // the raw bits. Every value used above is positive, which is exactly where
    // the two paths are indistinguishable.
    Bytes body;
    body.U32(0xffffffffu);           // i
    body.U64(0xffffffffffffffffULL); // h
    body.U32(0xffffffffu);           // c
    body.U32(0xffffffffu);           // r
    body.U32(0xffffffffu);           // m
    body.U64(0xe9a1000000000000ULL); // t
    const std::vector<std::uint8_t> datagram =
        Message("/signs", "ihcrmt", body.data);

    OscPacket packet;
    Diagnostic diagnostic;
    if (!Decode(datagram, &packet, &diagnostic)) {
        std::fprintf(stderr, "%s\n",
                     vrmAdapterVmc::FormatDiagnostic(diagnostic).c_str());
        assert(false);
    }

    const auto& arguments = packet.messages.front().arguments;
    assert(arguments[0].integer == -1);
    assert(arguments[1].integer == -1);
    // Not -1: a character code, a colour and a MIDI message are four bytes of
    // payload rather than a number with a sign.
    assert(arguments[2].integer == 0xffffffffLL);
    assert(arguments[3].integer == 0xffffffffLL);
    assert(arguments[4].integer == 0xffffffffLL);

    // 't' is a 64-bit NTP time tag and it shares 'h''s signed path, so a real
    // one reads as a negative integer -- NTP seconds have had their high bit
    // set since 1968, so that is every time tag a sender would emit today
    // rather than a far-future edge. Recorded rather than endorsed:
    // `OscArgument` has no unsigned field to widen into, nothing in VMC sends a
    // 't' argument, and OSC-0 changes no behaviour by construction. It is a
    // question the shared decoder owes an answer to (plan §10), written down
    // here so that answer is a decision rather than a rediscovery.
    assert(arguments[5].integer
           == static_cast<std::int64_t>(0xe9a1000000000000ULL));
    assert(arguments[5].integer < 0);
    // A *bundle's* time tag is unaffected: it lands in `OscPacket::timeTag`,
    // which is a `std::uint64_t`.
}

void
TestStringPaddingIsAlwaysAtLeastOneNul()
{
    // A string whose length is already a multiple of four is followed by
    // *four* NULs, not none. A decoder that rounded up instead would read
    // every following argument four bytes early -- so this surfaces as a
    // refusal rather than as a wrong number, and no test above uses a string
    // whose length would show it.
    Bytes body;
    body.Str("abcd"); // four characters, eight bytes on the wire
    body.F32(0.5f);
    const std::vector<std::uint8_t> aligned = Message("/pad", "sf", body.data);

    OscPacket packet;
    Diagnostic diagnostic;
    if (!Decode(aligned, &packet, &diagnostic)) {
        std::fprintf(stderr, "%s\n",
                     vrmAdapterVmc::FormatDiagnostic(diagnostic).c_str());
        assert(false);
    }
    // "/pad" is four characters too, so the address and the argument exercise
    // the same rule at both ends of the message.
    assert(packet.messages.front().address == "/pad");
    assert(packet.messages.front().arguments[0].text == "abcd");
    assert(packet.messages.front().arguments[1].real == 0.5f);

    // The other end of the same rule: an empty string is one NUL and three pad
    // bytes, and it is a valid argument rather than a missing one.
    Bytes emptyBody;
    emptyBody.Str("");
    emptyBody.F32(0.5f);
    const std::vector<std::uint8_t> empty =
        Message("/pad", "sf", emptyBody.data);
    assert(Decode(empty, &packet));
    assert(packet.messages.front().arguments[0].text.empty());
    assert(packet.messages.front().arguments[1].real == 0.5f);

    // A blob is padded by a different rule -- its declared size is not padded,
    // its data is -- so a zero-length blob is a size field and nothing else.
    // Applying the string rule here would demand four bytes a sender is right
    // not to have sent.
    Bytes blobBody;
    blobBody.U32(0);
    const std::vector<std::uint8_t> hollow =
        Message("/pad", "b", blobBody.data);
    assert(Decode(hollow, &packet));
    assert(packet.messages.front().arguments[0].blob.size == 0);
}

void
TestNestingUpToTheCapIsAccepted()
{
    // The refusal above proves a cap exists; this proves where it is. Nothing
    // else in this file nests at all, so an off-by-one that refused one level
    // early would leave every other test green while turning a legal datagram
    // into the sender's fault.
    std::vector<std::uint8_t> nested = Message("/deep", "");
    for (std::size_t depth = 0; depth < vrmAdapterVmc::MaxOscBundleDepth;
         ++depth) {
        nested = Bundle(1, {nested});
    }

    OscPacket packet;
    Diagnostic diagnostic;
    if (!Decode(nested, &packet, &diagnostic)) {
        std::fprintf(stderr, "%s\n",
                     vrmAdapterVmc::FormatDiagnostic(diagnostic).c_str());
        assert(false);
    }
    assert(packet.messages.size() == 1);
    assert(packet.messages.front().address == "/deep");
    assert(packet.bundled);
    assert(packet.timeTag == 1);
}

// Corpus mode: the decoder against every committed capture.
int
CheckCorpus(const std::filesystem::path& directory)
{
    // Message counts derived from the generator's own structure, not from a
    // run: neutral-standing is a two-message handshake plus five bundles of
    // (time + root + 22 bones); arm-raise sends one message per datagram;
    // mixed-traffic adds three blend values, an apply, and three device
    // messages per frame; sender-restart is nine full frames' worth minus the
    // fifteen bones its interrupted frame never sent.
    struct Expected
    {
        const char* file;
        std::size_t datagrams;
        // The indices of the datagrams this layer refuses, comma separated. A
        // count would not do: refusing a different eight of the ten would leave
        // it unchanged, and *which* datagram is refused is the claim the
        // malformed corpus exists to make.
        const char* refused;
        std::size_t messages;
        std::size_t addresses;
        bool bundled;
    };
    const Expected expected[] = {
        {"arm-raise-30hz.vmcpackets", 117, "", 117, 5, false},
        // Two captures this layer has nothing to say about: every datagram is
        // valid OSC, and whether the *VMC* arguments make sense is a question
        // one layer up. They are listed because the corpus is enumerated rather
        // than the table -- a capture nobody decodes is a failure here.
        {"extended-forms.vmcpackets", 2, "", 26, 6, true},
        {"malformed-forms.vmcpackets", 10, "", 55, 6, true},
        // The first eight are the packet-level refusals. The last two are
        // well-formed OSC this adapter does not implement, which is not this
        // layer's business to notice.
        {"malformed-packets.vmcpackets", 10, "0,1,2,3,4,5,6,7", 2, 2, false},
        {"mixed-traffic-30hz.vmcpackets", 13, "", 93, 11, true},
        {"neutral-standing-30hz.vmcpackets", 6, "", 122, 5, true},
        {"sender-restart-30hz.vmcpackets", 10, "", 173, 5, true},
    };

    // Enumerate the corpus rather than the table. A test that walked only its
    // own expectations would skip a capture added later in silence -- and the
    // corpus README tells an author to check that a new capture appears here.
    std::vector<std::filesystem::path> captures;
    if (!std::filesystem::is_directory(directory)) {
        std::fprintf(stderr, "corpus directory not found: %s\n",
                     directory.string().c_str());
        return 1;
    }
    for (const std::filesystem::directory_entry& file :
         std::filesystem::directory_iterator(directory)) {
        if (file.is_regular_file()
            && file.path().extension() == ".vmcpackets") {
            captures.push_back(file.path());
        }
    }
    std::sort(captures.begin(), captures.end());
    if (captures.empty()) {
        std::fprintf(stderr, "no .vmcpackets fixtures in %s\n",
                     directory.string().c_str());
        return 1;
    }

    int failures = 0;
    std::set<std::string> covered;
    for (const std::filesystem::path& path : captures) {
        const std::string name = path.filename().string();
        const Expected* entry = nullptr;
        for (const Expected& candidate : expected) {
            if (name == candidate.file) {
                entry = &candidate;
                break;
            }
        }
        if (!entry) {
            std::fprintf(stderr,
                         "%s: no expected decode in this test -- add one, or "
                         "the capture is in the corpus and decoded by nobody\n",
                         name.c_str());
            ++failures;
            continue;
        }
        covered.insert(name);

        vrmAdapterVmc::PacketCapture capture;
        vrmAdapterVmc::PacketCaptureError error;
        if (!vrmAdapterVmc::ReadPacketCaptureFile(path.string(), &capture,
                                                  &error)) {
            std::fprintf(stderr, "%s:%zu: %s\n", name.c_str(), error.line,
                         error.message.c_str());
            ++failures;
            continue;
        }

        std::string refused;
        std::size_t messages = 0;
        std::size_t bundles = 0;
        std::set<std::string> addresses;
        for (std::size_t index = 0; index < capture.datagrams.size(); ++index) {
            OscPacket packet;
            Diagnostic diagnostic;
            if (!vrmAdapterVmc::DecodeOscPacket(capture.datagrams[index].bytes,
                                                &packet, &diagnostic)) {
                if (!refused.empty()) {
                    refused += ',';
                }
                refused += std::to_string(index);
                if (std::string_view(entry->refused).empty()) {
                    std::fprintf(stderr, "%s: %s\n", name.c_str(),
                                 vrmAdapterVmc::FormatDiagnostic(diagnostic)
                                     .c_str());
                }
                continue;
            }
            messages += packet.messages.size();
            bundles += packet.bundled ? 1 : 0;
            for (const vrmAdapterVmc::OscMessage& message : packet.messages) {
                addresses.insert(std::string(message.address));
            }
        }

        const bool ok = capture.datagrams.size() == entry->datagrams
            && refused == entry->refused && messages == entry->messages
            && addresses.size() == entry->addresses
            && (bundles != 0) == entry->bundled;
        if (!ok) {
            std::fprintf(stderr,
                         "%s: %zu datagram(s), refused [%s], %zu message(s), "
                         "%zu address(es), %zu bundle(s) -- expected %zu, [%s], "
                         "%zu, %zu, bundled=%d\n",
                         name.c_str(), capture.datagrams.size(),
                         refused.c_str(), messages, addresses.size(), bundles,
                         entry->datagrams, entry->refused, entry->messages,
                         entry->addresses, entry->bundled ? 1 : 0);
            ++failures;
            continue;
        }

        std::printf("%s: %zu datagram(s), refused [%s], %zu message(s), %zu "
                    "address(es)\n",
                    name.c_str(), capture.datagrams.size(), refused.c_str(),
                    messages, addresses.size());
    }

    // The other direction: an expectation whose capture is gone would otherwise
    // pass by never being visited.
    for (const Expected& entry : expected) {
        if (covered.find(entry.file) == covered.end()) {
            std::fprintf(stderr, "%s: expected in this test, absent from %s\n",
                         entry.file, directory.string().c_str());
            ++failures;
        }
    }

    if (failures != 0) {
        std::fprintf(stderr, "%d corpus capture(s) failed\n", failures);
        return 1;
    }
    std::printf("OSC decode: %zu capture(s) verified\n", captures.size());
    return 0;
}

} // namespace

int
main(int argc, char** argv)
{
    if (argc > 1) {
        return CheckCorpus(std::filesystem::path(argv[1]));
    }

    TestAMessageDecodesToItsArguments();
    TestEveryOscTypeTagIsSized();
    TestABundleFlattensIntoWireOrder();
    TestNestedBundlesFlattenAndDepthIsCapped();
    TestValidButUnimplementedAddressesDecodeCleanly();
    TestMalformedDatagramsAreRefusedAndSayWhy();
    TestTheArgumentGuardsRefuseRatherThanDereference();
    TestARefusedBundleYieldsNoMessagesAtAll();
    TestADecodeReplacesWhateverTheCallerHandedIn();
    TestARefusalNamesTheByteAndTheAddress();
    TestDecodedViewsPointIntoTheCallersDatagram();
    TestArgumentSignednessFollowsTheWire();
    TestStringPaddingIsAlwaysAtLeastOneNul();
    TestNestingUpToTheCapIsAccepted();
    std::puts("vrmAdapterVmc OSC packet tests passed");
    return 0;
}
