// SPDX-License-Identifier: Apache-2.0
//
// The tracker layer: what a VRChat OSC address means, and what it does not.
//
// Every datagram in the unit half is built byte by byte, for the reason the
// address inventory's suite is: a fixture written by this repository's own
// encoder would let the decoder agree with itself. The corpus half replays the
// committed captures, which are written by a *Python* encoder for the same
// reason one layer out.
//
// What is checked here is this adapter's four contributions and nothing below
// them. OSC's own grammar is `libs/osc`'s suite; the map from a neutral OSC
// refusal onto `VRM_VRCHAT_OSC_PACKET_MALFORMED` is checked here because it is
// this adapter's half of that split.
//
// The first test is the one this milestone exists for. `head` and `1` occupy
// the same path position, so an identity type that could not hold both would
// have dropped the head silently and reported nothing wrong — which is what a
// decoder written from the specification would have done, and what VRC-1's
// session is what caught.
#include "vrmAdapterVrchatOsc/TrackerMessage.h"

#include "vrmAdapterVrchatOsc/Diagnostics.h"
#include "vrmAdapterVrchatOsc/PacketCapture.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace
{

using vrmAdapterVrchatOsc::Diagnostic;
using vrmAdapterVrchatOsc::DiagnosticCode;
using vrmAdapterVrchatOsc::PacketCapture;
using vrmAdapterVrchatOsc::RecordedDatagram;
using vrmAdapterVrchatOsc::TrackerChannel;
using vrmAdapterVrchatOsc::TrackerMessage;
using vrmAdapterVrchatOsc::TrackerPacket;

// ---------------------------------------------------------------------------
// Byte assembly. Big-endian throughout, like the wire.
// ---------------------------------------------------------------------------

struct Bytes
{
    std::vector<std::uint8_t> data;

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
        return U32(static_cast<std::uint32_t>(value));
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

    Bytes& Append(const std::vector<std::uint8_t>& values)
    {
        data.insert(data.end(), values.begin(), values.end());
        return *this;
    }
};

// The ordinary form: an address and three floats.
std::vector<std::uint8_t>
Message(std::string_view address, float x, float y, float z)
{
    Bytes out;
    out.Str(address).Str(",fff").F32(x).F32(y).F32(z);
    return out.data;
}

// An address with no arguments at all, which is well-formed OSC.
std::vector<std::uint8_t>
Bare(std::string_view address)
{
    Bytes out;
    out.Str(address).Str(",");
    return out.data;
}

std::vector<std::uint8_t>
Bundle(const std::vector<std::vector<std::uint8_t>>& elements)
{
    Bytes out;
    out.Str("#bundle").U64(1);
    for (const std::vector<std::uint8_t>& element : elements) {
        out.U32(static_cast<std::uint32_t>(element.size()));
        out.Append(element);
    }
    return out.data;
}

// ---------------------------------------------------------------------------
// Unit tests
// ---------------------------------------------------------------------------

// The first line of the first test, per report 02 §6.1: the identity type holds
// a number and a name, and neither is the special case.
void
TestTheIdentityHoldsANumberAndAName()
{
    // Every datagram below is a named local rather than a temporary, and that
    // is not a style choice: `DecodeTrackerDatagram`'s deleted rvalue overload
    // refuses a temporary, because every `TrackerId::segment` in the result
    // points into the bytes. This suite would not compile written the other
    // way, which is the strongest form that rule can take.
    const std::vector<std::uint8_t> numberedBytes =
        Message("/tracking/trackers/1/position", 0.5f, 1.0f, -0.25f);
    const std::vector<std::uint8_t> namedBytes =
        Message("/tracking/trackers/head/position", 0.5f, 1.0f, -0.25f);
    const TrackerPacket numbered =
        vrmAdapterVrchatOsc::DecodeTrackerDatagram(numberedBytes);
    const TrackerPacket named =
        vrmAdapterVrchatOsc::DecodeTrackerDatagram(namedBytes);

    assert(numbered.messages.size() == 1);
    assert(named.messages.size() == 1);
    assert(numbered.diagnostics.empty());
    assert(named.diagnostics.empty());

    assert(numbered.messages[0].tracker.segment == "1");
    assert(numbered.messages[0].tracker.index.has_value());
    assert(*numbered.messages[0].tracker.index == 1);
    assert(!numbered.messages[0].tracker.named());

    assert(named.messages[0].tracker.segment == "head");
    assert(!named.messages[0].tracker.index.has_value());
    assert(named.messages[0].tracker.named());

    // The two are different trackers, and the head is not tracker zero.
    assert(numbered.messages[0].tracker != named.messages[0].tracker);
}

void
TestEveryIdentityAndChannelTheSurfaceDefines()
{
    // 1 through 8 and the head, both channels: the whole surface, and nothing
    // in it needs a capture to have been seen for it to be legal.
    for (int index = 1; index <= 8; ++index) {
        const std::string segment = std::to_string(index);
        for (const std::string_view channel : {"position", "rotation"}) {
            const std::string address =
                "/tracking/trackers/" + segment + "/" + std::string(channel);
            const std::vector<std::uint8_t> datagram =
                Message(address, 1.0f, 2.0f, 3.0f);
            const TrackerPacket packet =
                vrmAdapterVrchatOsc::DecodeTrackerDatagram(datagram);
            assert(packet.messages.size() == 1);
            assert(packet.diagnostics.empty());
            assert(packet.messages[0].tracker.segment == segment);
            assert(*packet.messages[0].tracker.index == index);
            assert(vrmAdapterVrchatOsc::TrackerChannelString(
                       packet.messages[0].channel)
                   == channel);
        }
    }

    // The channel table is whole and round-trips, so a channel added later
    // cannot be spelled two ways.
    for (std::size_t slot = 0; slot < vrmAdapterVrchatOsc::TrackerChannelCount;
         ++slot) {
        const auto channel = static_cast<TrackerChannel>(slot);
        const std::string_view name =
            vrmAdapterVrchatOsc::TrackerChannelString(channel);
        assert(!name.empty());
        assert(vrmAdapterVrchatOsc::FindTrackerChannel(name) == channel);
    }
    assert(vrmAdapterVrchatOsc::TrackerChannelString(TrackerChannel::Count)
               .empty());
    assert(!vrmAdapterVrchatOsc::FindTrackerChannel("Position").has_value());
}

// The claim no count can make: three floats arrive as three floats.
void
TestNothingIsConvertedOnTheWayThrough()
{
    // Exactly representable in binary32, asymmetric in all three components, so
    // a reflection through any single axis is visible and equality is the right
    // comparison rather than a tolerance.
    const std::vector<std::uint8_t> datagram =
        Message("/tracking/trackers/2/rotation", -90.0f, 0.5f, 45.25f);
    const TrackerPacket packet =
        vrmAdapterVrchatOsc::DecodeTrackerDatagram(datagram);
    assert(packet.messages.size() == 1);
    const TrackerMessage& message = packet.messages[0];
    assert(message.channel == TrackerChannel::Rotation);
    assert(message.values[0] == -90.0f);
    assert(message.values[1] == 0.5f);
    assert(message.values[2] == 45.25f);

    // Zero is a value, not an absence: a rotation of (0, 0, 0) is what a
    // tracker at rest reports, which is why a message decoder may never fill a
    // channel it did not read.
    const std::vector<std::uint8_t> restBytes =
        Message("/tracking/trackers/2/rotation", 0.0f, 0.0f, 0.0f);
    const TrackerPacket rest =
        vrmAdapterVrchatOsc::DecodeTrackerDatagram(restBytes);
    assert(rest.messages.size() == 1);
    assert(rest.messages[0].values[0] == 0.0f);
    assert(rest.messages[0].values[1] == 0.0f);
    assert(rest.messages[0].values[2] == 0.0f);
}

void
TestUnimplementedAddressesAreUnsupportedNotMalformed()
{
    // Four shapes of "this adapter maps it to nothing": a foreign family, a
    // tracker address with no channel, a channel this adapter does not read,
    // and a segment past the channel.
    const std::vector<std::string> addresses = {
        "/avatar/parameters/VRCEmote",
        "/tracking/eye/CenterPitchYaw",
        "/tracking/trackers",
        "/tracking/trackers/1",
        "/tracking/trackers/1/velocity",
        "/tracking/trackers/1/position/x",
        "/tracking/trackers/1/Position",
        // The prefix is a prefix of a *path segment* rather than of a string:
        // this address is not in the family at all.
        "/tracking/trackersfoo/1/position",
    };

    for (const std::string& address : addresses) {
        const std::vector<std::uint8_t> datagram =
            Message(address, 1.0f, 2.0f, 3.0f);
        const TrackerPacket packet =
            vrmAdapterVrchatOsc::DecodeTrackerDatagram(datagram);
        assert(packet.messages.empty());
        assert(packet.diagnostics.size() == 1);
        assert(packet.unsupported == 1);
        assert(packet.diagnostics[0].code == DiagnosticCode::UnsupportedAddress);
        // Info and recoverable: a session carrying VRChat's wider surface
        // beside tracker data is the ordinary case, not a fault.
        assert(packet.diagnostics[0].recoverable);
        assert(packet.diagnostics[0].severity
               == vrmAdapterVrchatOsc::DiagnosticSeverity::Info);
        // The subject is the address, because that is what this layer knows. A
        // bone name here would be a humanoid claim from a layer that has made
        // none.
        assert(packet.diagnostics[0].subject == address);
    }
}

void
TestAnIdentityThisAdapterCannotReadIsNotAnUnsupportedAddress()
{
    // The address is one this adapter knows; the identity in it is not one it
    // can read. Reporting these as unsupported would make a sender's bad index
    // indistinguishable from a part of VRChat's surface nobody implemented.
    const std::vector<std::string> segments = {
        "0",    // below the range the surface defines
        "9",    // above it
        "12",   // above it, two digits
        "999",  // above it, and above what one byte holds
        "1234", // more digits than the range can spell
        "01",   // a leading zero: one tracker with two identities
        "hip",  // a name that is not the one name the surface defines
        "HEAD", // and the right name in the wrong case
        "",     // no identity at all
        "-1",   // not a decimal at all
        "1.0",
    };

    for (const std::string& segment : segments) {
        const std::string address =
            "/tracking/trackers/" + segment + "/position";
        const std::vector<std::uint8_t> datagram =
            Message(address, 1.0f, 2.0f, 3.0f);
        const TrackerPacket packet =
            vrmAdapterVrchatOsc::DecodeTrackerDatagram(datagram);
        assert(packet.messages.empty());
        assert(packet.diagnostics.size() == 1);
        assert(packet.unsupported == 0);
        assert(packet.diagnostics[0].code == DiagnosticCode::TrackerIdInvalid);
        assert(packet.diagnostics[0].subject == address);
        // The detail names the segment it could not read, so an operator does
        // not have to re-read the address to find out which part was wrong.
        if (!segment.empty()) {
            assert(packet.diagnostics[0].detail.find(segment)
                   != std::string::npos);
        }
    }

    // "01" is a decimal *in* range spelled a way the surface does not, so its
    // detail may not say it is outside the range. A false detail is worse than
    // a vague one, and this is the one input where the two readings differ.
    const std::vector<std::uint8_t> leadingZero =
        Message("/tracking/trackers/01/position", 1.0f, 2.0f, 3.0f);
    const TrackerPacket packet =
        vrmAdapterVrchatOsc::DecodeTrackerDatagram(leadingZero);
    assert(packet.diagnostics.size() == 1);
    assert(packet.diagnostics[0].detail.find("outside") == std::string::npos);
    assert(packet.diagnostics[0].detail.find("leading zero")
           != std::string::npos);
}

void
TestAKnownAddressWithTheWrongArgumentsIsAMismatch()
{
    const std::string address = "/tracking/trackers/1/rotation";

    // Two floats where three are required.
    Bytes shortForm;
    shortForm.Str(address).Str(",ff").F32(1.0f).F32(2.0f);
    // Four floats: a quaternion, which is the case this adapter refuses where
    // its sibling would count the extra. Reading the first three components as
    // Euler angles is a confident misreading of a different form.
    Bytes quaternion;
    quaternion.Str(address).Str(",ffff").F32(0.0f).F32(0.0f).F32(0.0f).F32(1.0f);
    // The right count and every type wrong.
    Bytes doubles;
    doubles.Str(address).Str(",ddd").F64(1.0).F64(2.0).F64(3.0);
    // The identity in an argument, as if this were VMC's `,sfff`.
    Bytes named;
    named.Str(address).Str(",sfff").Str("1").F32(1.0f).F32(2.0f).F32(3.0f);
    // Integers, which are not floats and are not coerced into them.
    Bytes integers;
    integers.Str(address).Str(",iii").U32(1).U32(2).U32(3);

    const std::vector<std::vector<std::uint8_t>> datagrams = {
        shortForm.data, quaternion.data,   doubles.data,
        named.data,     integers.data,     Bare(address),
    };

    for (const std::vector<std::uint8_t>& datagram : datagrams) {
        const TrackerPacket packet =
            vrmAdapterVrchatOsc::DecodeTrackerDatagram(datagram);
        assert(packet.messages.empty());
        assert(packet.diagnostics.size() == 1);
        assert(packet.diagnostics[0].code == DiagnosticCode::ArgumentMismatch);
        assert(packet.diagnostics[0].subject == address);
        // Both tag strings are in the detail, with their commas, so the count
        // and the types are legible without a second lookup.
        assert(packet.diagnostics[0].detail.find("\",fff\"")
               != std::string::npos);
    }

    // The one that would be silently plausible: the refusal quotes what it saw.
    const TrackerPacket quaternionPacket =
        vrmAdapterVrchatOsc::DecodeTrackerDatagram(quaternion.data);
    assert(quaternionPacket.diagnostics[0].detail.find("\",ffff\"")
           != std::string::npos);
}

void
TestANonFiniteComponentIsRefused()
{
    const std::string address = "/tracking/trackers/3/position";
    const std::vector<float> values = {
        std::nanf(""),
        std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(),
    };

    for (const float value : values) {
        for (std::size_t slot = 0; slot < 3; ++slot) {
            float components[3] = {0.0f, 0.0f, 0.0f};
            components[slot] = value;
            const std::vector<std::uint8_t> datagram = Message(
                address, components[0], components[1], components[2]);
            const TrackerPacket packet =
                vrmAdapterVrchatOsc::DecodeTrackerDatagram(datagram);
            assert(packet.messages.empty());
            assert(packet.diagnostics.size() == 1);
            assert(packet.diagnostics[0].code
                   == DiagnosticCode::CoordinateInvalid);
            // Which component, so an operator does not have to bisect for it.
            assert(packet.diagnostics[0].detail.find(std::to_string(slot))
                   != std::string::npos);
        }
    }
}

void
TestAPacketRefusesMessagesNotTheDatagram()
{
    // A bundled frame with one bad message in the middle. The other three
    // decode: this is the claim the malformed-forms capture makes over recorded
    // bytes, made here over bytes this file wrote.
    Bytes quaternion;
    quaternion.Str("/tracking/trackers/1/rotation")
        .Str(",ffff")
        .F32(0.0f)
        .F32(0.0f)
        .F32(0.0f)
        .F32(1.0f);

    const std::vector<std::uint8_t> datagram =
        Bundle({Message("/tracking/trackers/head/rotation", 1.0f, 2.0f, 3.0f),
                Message("/tracking/trackers/head/position", 4.0f, 5.0f, 6.0f),
                quaternion.data,
                Message("/tracking/trackers/1/position", 7.0f, 8.0f, 9.0f),
                Message("/avatar/parameters/VRCEmote", 1.0f, 0.0f, 0.0f)});
    const TrackerPacket packet =
        vrmAdapterVrchatOsc::DecodeTrackerDatagram(datagram);

    assert(!packet.refused);
    assert(packet.bundled);
    assert(packet.messagesSeen == 5);
    assert(packet.messages.size() == 3);
    assert(packet.diagnostics.size() == 2);
    assert(packet.unsupported == 1);
    assert(packet.diagnostics[0].code == DiagnosticCode::ArgumentMismatch);
    assert(packet.diagnostics[1].code == DiagnosticCode::UnsupportedAddress);
    // Wire order is kept: a bundle is flattened, never sorted.
    assert(packet.messages[0].tracker.segment == "head");
    assert(packet.messages[0].channel == TrackerChannel::Rotation);
    assert(packet.messages[2].tracker.segment == "1");
    assert(packet.messages[2].channel == TrackerChannel::Position);
}

void
TestADatagramThatIsNotOscIsRefusedWhole()
{
    // The shared decoder's refusal, wearing this adapter's code. `libs/osc`
    // raises none — a refusal has a subject and a detail and no code — and the
    // caller that knows whose wire this is supplies one.
    const std::vector<std::vector<std::uint8_t>> datagrams = {
        {},
        {'/', 't', 'r'},
        {'t', 'r', 'a', 'c'},
    };

    for (const std::vector<std::uint8_t>& datagram : datagrams) {
        const TrackerPacket packet =
            vrmAdapterVrchatOsc::DecodeTrackerDatagram(datagram);
        assert(packet.refused);
        assert(packet.messages.empty());
        assert(packet.messagesSeen == 0);
        assert(packet.diagnostics.size() == 1);
        assert(packet.diagnostics[0].code == DiagnosticCode::PacketMalformed);
        assert(!packet.diagnostics[0].detail.empty());
    }

    // A bundle whose element runs past the end: the *whole* datagram goes, not
    // the elements before it. A half-decoded frame is worse than a refused one.
    Bytes overrun;
    const std::vector<std::uint8_t> element =
        Message("/tracking/trackers/1/position", 1.0f, 2.0f, 3.0f);
    overrun.Str("#bundle")
        .U64(1)
        .U32(static_cast<std::uint32_t>(element.size() + 16))
        .Append(element);
    const TrackerPacket packet =
        vrmAdapterVrchatOsc::DecodeTrackerDatagram(overrun.data);
    assert(packet.refused);
    assert(packet.messages.empty());
}

void
TestNoPartialIsRaisedByAMessageDecoder()
{
    // A position with no rotation is exactly what every datagram on this wire
    // is, so `TRACKER_PARTIAL` here would be a warning about once a datagram
    // forever. The window a tracker is partial *within* is VRC-4's, and so is
    // that code.
    const std::vector<std::uint8_t> datagram =
        Message("/tracking/trackers/1/position", 1.0f, 2.0f, 3.0f);
    const TrackerPacket packet =
        vrmAdapterVrchatOsc::DecodeTrackerDatagram(datagram);
    assert(packet.messages.size() == 1);
    assert(packet.diagnostics.empty());

    for (std::size_t slot = 0; slot < vrmAdapterVrchatOsc::DiagnosticCodeCount;
         ++slot) {
        const auto code = static_cast<DiagnosticCode>(slot);
        assert(!vrmAdapterVrchatOsc::DiagnosticCodeString(code).empty());
    }
}

// The two guards that refuse a caller rather than a sender. Both are reached
// only through the message overload: `DecodeTrackerDatagram` cannot produce
// either, which is the point — the OSC layer emits one argument per type tag,
// so a message where the two disagree did not come from it, and this is the one
// place that can be shown.
void
TestTheStructuralGuardsRefuseRatherThanDereference()
{
    osc::OscMessage message;
    message.address = "/tracking/trackers/1/position";
    message.typeTags = "fff";
    message.arguments.resize(3);
    for (std::size_t slot = 0; slot < 3; ++slot) {
        message.arguments[slot].tag = 'f';
        message.arguments[slot].real = 1.0 + static_cast<double>(slot);
    }

    // It decodes as written, so the two refusals below are about what was
    // removed rather than about the message being unusable to begin with.
    TrackerMessage decoded;
    Diagnostic error;
    assert(vrmAdapterVrchatOsc::DecodeTrackerMessage(message, &decoded, &error));
    assert(decoded.values[2] == 3.0f);

    // No output. Refused rather than written through.
    assert(!vrmAdapterVrchatOsc::DecodeTrackerMessage(message, nullptr, &error));
    assert(error.code == DiagnosticCode::PacketMalformed);

    // Three type tags and no arguments. The tag check below it would pass, and
    // the values loop would read three elements that are not there.
    osc::OscMessage starved = message;
    starved.arguments.clear();
    assert(!vrmAdapterVrchatOsc::DecodeTrackerMessage(starved, &decoded, &error));
    assert(error.code == DiagnosticCode::PacketMalformed);
    assert(error.detail.find("3 argument(s) and 0 were given")
           != std::string::npos);

    // And the other direction, which is harmless to read but is still a message
    // no OSC decoder produced.
    osc::OscMessage overfed = message;
    overfed.arguments.resize(4);
    assert(!vrmAdapterVrchatOsc::DecodeTrackerMessage(overfed, &decoded, &error));
    assert(error.code == DiagnosticCode::PacketMalformed);

    // A refusal leaves the caller's message untouched, so a decode loop that
    // reuses one cannot mistake the last good message for this one.
    assert(decoded.values[2] == 3.0f);
}

void
TestTheFormattedLineNamesTheAddress()
{
    const std::vector<std::uint8_t> datagram =
        Message("/tracking/trackers/1/velocity", 1.0f, 2.0f, 3.0f);
    const TrackerPacket packet =
        vrmAdapterVrchatOsc::DecodeTrackerDatagram(datagram);
    assert(packet.diagnostics.size() == 1);
    const std::string line =
        vrmAdapterVrchatOsc::FormatDiagnostic(packet.diagnostics[0]);
    assert(line.find("[VRM_VRCHAT_OSC_UNSUPPORTED_ADDRESS]") == 0);
    assert(line.find("subject=/tracking/trackers/1/velocity")
           != std::string::npos);
}

// ---------------------------------------------------------------------------
// The corpus
// ---------------------------------------------------------------------------

struct Expected
{
    const char* file;
    std::size_t datagrams;
    // Datagrams `libs/osc` refused whole.
    std::size_t refusedDatagrams;
    std::size_t bundledDatagrams;
    // Messages the OSC layer produced, and what became of each.
    std::size_t messagesSeen;
    std::size_t decoded;
    std::size_t unsupported;
    std::size_t trackerIdInvalid;
    std::size_t argumentMismatch;
    std::size_t coordinateInvalid;
};

// Derived from the generator's structure, not from a run.
//
// `three-trackers-58hz` is eight frames of eight datagrams with one frame
// dropped whole and one address dropped from another: 7 x 8 - 1 = 55.
// `eight-trackers` is two frames over nine identities; `head-absent`,
// `position-only` and `rotation-only` are three frames over three or four
// halves; `tracker-dropout` is two full frames and four with one identity gone.
// `duplicate-and-reordered` is three frames plus one repeated address.
// `mixed-traffic` is two clean frames, five foreign addresses, three
// tracker-shaped addresses this adapter maps to nothing, and four identities it
// cannot read. `malformed-packets` is nine datagrams that are not OSC and one
// that is. `malformed-forms` is a clean frame, a bundled frame with one
// four-float rotation in it, and seven individually bad messages.
// `rig-motion` is twelve unbroken frames of eight over the measured four
// identities: nothing is lost and nothing is refused, which is what makes a
// failed end-to-end run of it unambiguous.
// `session-restart`, `silent-gap` and `calibration-jump` are six frames of
// eight apiece and carry nothing this layer refuses: what each of them says is
// said by its *timing* and its peers, which is VRC-4's subject and not this
// one's. They are listed because a capture with no row here fails this test,
// which is what stops a fixture being added to the corpus and decoded by
// nobody.
constexpr Expected kExpected[] = {
    {"bundled-frame.vrchatoscpackets", 3, 0, 3, 24, 24, 0, 0, 0, 0},
    {"calibration-jump.vrchatoscpackets", 48, 0, 0, 48, 48, 0, 0, 0, 0},
    {"duplicate-and-reordered.vrchatoscpackets", 25, 0, 0, 25, 25, 0, 0, 0, 0},
    {"eight-trackers.vrchatoscpackets", 36, 0, 0, 36, 36, 0, 0, 0, 0},
    {"head-absent.vrchatoscpackets", 18, 0, 0, 18, 18, 0, 0, 0, 0},
    {"malformed-forms.vrchatoscpackets", 16, 0, 1, 23, 15, 0, 0, 6, 2},
    {"malformed-packets.vrchatoscpackets", 10, 9, 0, 1, 1, 0, 0, 0, 0},
    {"mixed-traffic.vrchatoscpackets", 28, 0, 0, 28, 16, 8, 4, 0, 0},
    {"one-tracker.vrchatoscpackets", 6, 0, 0, 6, 6, 0, 0, 0, 0},
    {"position-only.vrchatoscpackets", 12, 0, 0, 12, 12, 0, 0, 0, 0},
    {"rig-motion.vrchatoscpackets", 96, 0, 0, 96, 96, 0, 0, 0, 0},
    {"rotation-only.vrchatoscpackets", 12, 0, 0, 12, 12, 0, 0, 0, 0},
    {"session-restart.vrchatoscpackets", 48, 0, 0, 48, 48, 0, 0, 0, 0},
    {"silent-gap.vrchatoscpackets", 48, 0, 0, 48, 48, 0, 0, 0, 0},
    {"three-trackers-58hz.vrchatoscpackets", 55, 0, 0, 55, 55, 0, 0, 0, 0},
    {"tracker-dropout.vrchatoscpackets", 40, 0, 0, 40, 40, 0, 0, 0, 0},
};

struct Decoded
{
    std::size_t datagrams = 0;
    std::size_t refusedDatagrams = 0;
    std::size_t bundledDatagrams = 0;
    std::size_t messagesSeen = 0;
    std::size_t decoded = 0;
    std::size_t unsupported = 0;
    std::size_t trackerIdInvalid = 0;
    std::size_t argumentMismatch = 0;
    std::size_t coordinateInvalid = 0;

    // Per identity segment, then per channel, so a capture's shape is visible
    // as more than a total: a decoder that read every message as tracker 1
    // would produce the same totals as one that read them correctly.
    std::map<std::string, std::array<std::size_t, 2>> perTracker;
    std::vector<TrackerMessage> messages;
    std::vector<std::string> refusals;
};

int
CheckCorpus(const std::filesystem::path& directory)
{
    if (!std::filesystem::is_directory(directory)) {
        std::fprintf(stderr, "corpus directory not found: %s\n",
                     directory.string().c_str());
        return 1;
    }

    std::vector<std::filesystem::path> captures;
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::recursive_directory_iterator(directory)) {
        if (entry.is_regular_file()
            && entry.path().extension() == ".vrchatoscpackets") {
            captures.push_back(entry.path());
        }
    }
    std::sort(captures.begin(), captures.end());
    if (captures.empty()) {
        std::fprintf(stderr, "no .vrchatoscpackets fixtures in %s\n",
                     directory.string().c_str());
        return 1;
    }

    int failures = 0;
    std::set<std::string> covered;
    // A capture has to carry values that change, or the value checks below
    // would also pass on a decoder that returned the first frame forever.
    bool anyValueMoved = false;

    for (const std::filesystem::path& path : captures) {
        const std::string name = path.filename().string();
        const Expected* entry = nullptr;
        for (const Expected& candidate : kExpected) {
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

        PacketCapture capture;
        vrmAdapterVrchatOsc::PacketCaptureError error;
        if (!vrmAdapterVrchatOsc::ReadPacketCaptureFile(path.string(), &capture,
                                                        &error)) {
            std::fprintf(stderr, "%s:%zu: %s\n", name.c_str(), error.line,
                         error.message.c_str());
            ++failures;
            continue;
        }

        Decoded actual;
        actual.datagrams = capture.datagrams.size();
        for (const RecordedDatagram& datagram : capture.datagrams) {
            const TrackerPacket packet =
                vrmAdapterVrchatOsc::DecodeTrackerDatagram(datagram.bytes);
            actual.refusedDatagrams += packet.refused ? 1 : 0;
            actual.bundledDatagrams += packet.bundled ? 1 : 0;
            actual.messagesSeen += packet.messagesSeen;
            actual.decoded += packet.messages.size();
            actual.unsupported += packet.unsupported;

            // Every message is accounted for exactly once, whatever happened to
            // it. The tallies and the vectors are filled on separate paths, and
            // this is the only place the two can be caught disagreeing.
            assert(packet.refused
                   || packet.messages.size() + packet.diagnostics.size()
                          == packet.messagesSeen);

            for (const Diagnostic& diagnostic : packet.diagnostics) {
                switch (diagnostic.code) {
                case DiagnosticCode::TrackerIdInvalid:
                    ++actual.trackerIdInvalid;
                    break;
                case DiagnosticCode::ArgumentMismatch:
                    ++actual.argumentMismatch;
                    break;
                case DiagnosticCode::CoordinateInvalid:
                    ++actual.coordinateInvalid;
                    break;
                default:
                    break;
                }
                // Kept rather than printed as they arrive: a capture that is
                // supposed to carry refusals would otherwise fill the log with
                // its own expected output.
                actual.refusals.push_back(
                    vrmAdapterVrchatOsc::FormatDiagnostic(diagnostic));
            }

            for (const TrackerMessage& message : packet.messages) {
                // The segment is copied out of the datagram here, which is the
                // lifetime rule the header states meeting a caller that keeps a
                // decoded message: `datagram.bytes` outlives this loop, and a
                // receive loop's reusable buffer would not.
                auto& slots =
                    actual.perTracker[std::string(message.tracker.segment)];
                ++slots[static_cast<std::size_t>(message.channel)];
                actual.messages.push_back(message);
            }
        }

        if (actual.datagrams != entry->datagrams
            || actual.refusedDatagrams != entry->refusedDatagrams
            || actual.bundledDatagrams != entry->bundledDatagrams
            || actual.messagesSeen != entry->messagesSeen
            || actual.decoded != entry->decoded
            || actual.unsupported != entry->unsupported
            || actual.trackerIdInvalid != entry->trackerIdInvalid
            || actual.argumentMismatch != entry->argumentMismatch
            || actual.coordinateInvalid != entry->coordinateInvalid) {
            std::fprintf(
                stderr,
                "%s: %zu datagrams (%zu refused, %zu bundled), %zu messages -> "
                "%zu decoded, %zu unsupported, %zu bad id, %zu bad args, %zu "
                "bad coordinate -- expected %zu (%zu, %zu), %zu -> %zu, %zu, "
                "%zu, %zu, %zu\n",
                name.c_str(), actual.datagrams, actual.refusedDatagrams,
                actual.bundledDatagrams, actual.messagesSeen, actual.decoded,
                actual.unsupported, actual.trackerIdInvalid,
                actual.argumentMismatch, actual.coordinateInvalid,
                entry->datagrams, entry->refusedDatagrams,
                entry->bundledDatagrams, entry->messagesSeen, entry->decoded,
                entry->unsupported, entry->trackerIdInvalid,
                entry->argumentMismatch, entry->coordinateInvalid);
            for (const std::string& refusal : actual.refusals) {
                std::fprintf(stderr, "  %s\n", refusal.c_str());
            }
            ++failures;
            continue;
        }

        // The claims about *values and identities*, per capture, which no total
        // can make.
        if (name == "one-tracker.vrchatoscpackets") {
            // Rotation precedes position, and both are exact. A decoder that
            // reflected an axis, reordered components, converted degrees to
            // radians or rescaled units fails on this equality.
            const bool ok = actual.messages.size() == 6
                && actual.messages[0].channel == TrackerChannel::Rotation
                && actual.messages[0].values[0] == -90.0f
                && actual.messages[0].values[1] == 0.5f
                && actual.messages[0].values[2] == 45.25f
                && actual.messages[1].channel == TrackerChannel::Position
                && actual.messages[1].values[0] == 0.25f
                && actual.messages[1].values[1] == -0.5f
                && actual.messages[1].values[2] == 1.25f;
            if (!ok) {
                std::fprintf(stderr,
                             "%s: the recorded values did not arrive verbatim\n",
                             name.c_str());
                ++failures;
            }
            if (actual.messages[0].values[0] != actual.messages[2].values[0]) {
                anyValueMoved = true;
            }
        }

        if (name == "three-trackers-58hz.vrchatoscpackets") {
            // Seven frames survive, and tracker 1 is one rotation short of
            // them: the single-address loss the real session put 96 % of its
            // residual loss on.
            const bool ok = actual.perTracker.size() == 4
                && actual.perTracker["head"] == std::array<std::size_t, 2>{7, 7}
                && actual.perTracker["1"] == std::array<std::size_t, 2>{7, 6}
                && actual.perTracker["2"] == std::array<std::size_t, 2>{7, 7}
                && actual.perTracker["3"] == std::array<std::size_t, 2>{7, 7};
            if (!ok) {
                std::fprintf(stderr,
                             "%s: per-tracker counts are not 7/7, 7/6, 7/7, "
                             "7/7 over four identities\n",
                             name.c_str());
                ++failures;
            }
        }

        if (name == "head-absent.vrchatoscpackets") {
            // No named identity anywhere: a session with no head is
            // well-formed, and nothing invents one.
            bool named = false;
            for (const auto& row : actual.perTracker) {
                named = named || row.first == "head";
            }
            if (named || actual.perTracker.size() != 3) {
                std::fprintf(stderr,
                             "%s: expected three numbered identities and no "
                             "head, got %zu identities\n",
                             name.c_str(), actual.perTracker.size());
                ++failures;
            }
        }

        if (name == "eight-trackers.vrchatoscpackets") {
            // Nine identities: eight numbered and one named, and every numbered
            // one carries its index.
            bool ok = actual.perTracker.size() == 9;
            for (const TrackerMessage& message : actual.messages) {
                const bool head = message.tracker.segment == "head";
                ok = ok && (head == message.tracker.named());
            }
            if (!ok) {
                std::fprintf(stderr,
                             "%s: expected 1-8 plus a named head, got %zu "
                             "identities\n",
                             name.c_str(), actual.perTracker.size());
                ++failures;
            }
        }

        if (name == "position-only.vrchatoscpackets"
            || name == "rotation-only.vrchatoscpackets") {
            const auto channel = name[0] == 'p' ? TrackerChannel::Position
                                                : TrackerChannel::Rotation;
            bool ok = true;
            for (const TrackerMessage& message : actual.messages) {
                ok = ok && message.channel == channel;
            }
            if (!ok) {
                std::fprintf(stderr,
                             "%s: a message arrived on the other channel\n",
                             name.c_str());
                ++failures;
            }
        }

        if (name == "duplicate-and-reordered.vrchatoscpackets") {
            // The duplicate is the last message and carries different values
            // from the frame's own, so "keep the first" and "keep the last" are
            // distinguishable downstream rather than only countable.
            const TrackerMessage& last = actual.messages.back();
            const bool ok = last.tracker.segment == "1"
                && last.channel == TrackerChannel::Position
                && actual.perTracker["1"][static_cast<std::size_t>(
                       TrackerChannel::Position)]
                    == 4;
            if (!ok) {
                std::fprintf(stderr,
                             "%s: the duplicated address is not where the "
                             "generator puts it\n",
                             name.c_str());
                ++failures;
            }
        }

        if (name == "malformed-forms.vrchatoscpackets") {
            // The headline claim as one number: the bundled frame's other seven
            // messages survive the one four-float rotation in it.
            const auto rotations = actual.perTracker["1"][static_cast<
                std::size_t>(TrackerChannel::Rotation)];
            if (rotations != 1) {
                std::fprintf(stderr,
                             "%s: tracker 1 yielded %zu rotation(s) -- expected "
                             "1, the clean frame's, with the bundled frame's "
                             "refused and its siblings kept\n",
                             name.c_str(), rotations);
                ++failures;
            }
        }

        std::printf("%s: %zu datagram(s) -> %zu message(s), %zu decoded, %zu "
                    "refused\n",
                    name.c_str(), actual.datagrams, actual.messagesSeen,
                    actual.decoded,
                    actual.messagesSeen - actual.decoded
                        + actual.refusedDatagrams);
    }

    for (const Expected& entry : kExpected) {
        if (covered.find(entry.file) == covered.end()) {
            std::fprintf(stderr, "%s: expected in this test, absent from %s\n",
                         entry.file, directory.string().c_str());
            ++failures;
        }
    }
    if (!anyValueMoved) {
        std::fprintf(stderr,
                     "no capture carries values that change between frames; "
                     "the value checks would pass on a decoder that returned "
                     "the first frame forever\n");
        ++failures;
    }

    if (failures != 0) {
        std::fprintf(stderr, "%d corpus capture(s) failed\n", failures);
        return 1;
    }
    std::printf("VRChat OSC tracker decode: %zu capture(s) verified\n",
                captures.size());
    return 0;
}

} // namespace

int
main(int argc, char** argv)
{
    if (argc > 1) {
        return CheckCorpus(std::filesystem::path(argv[1]));
    }

    TestTheIdentityHoldsANumberAndAName();
    TestEveryIdentityAndChannelTheSurfaceDefines();
    TestNothingIsConvertedOnTheWayThrough();
    TestUnimplementedAddressesAreUnsupportedNotMalformed();
    TestAnIdentityThisAdapterCannotReadIsNotAnUnsupportedAddress();
    TestAKnownAddressWithTheWrongArgumentsIsAMismatch();
    TestANonFiniteComponentIsRefused();
    TestAPacketRefusesMessagesNotTheDatagram();
    TestADatagramThatIsNotOscIsRefusedWhole();
    TestNoPartialIsRaisedByAMessageDecoder();
    TestTheStructuralGuardsRefuseRatherThanDereference();
    TestTheFormattedLineNamesTheAddress();
    std::puts("vrmAdapterVrchatOsc tracker message tests passed");
    return 0;
}
