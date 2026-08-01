// SPDX-License-Identifier: Apache-2.0
//
// The VMC layer: address patterns, argument forms, and nothing about a
// humanoid.
//
// The unit tests build `OscMessage` values directly rather than encoding
// datagrams, because this layer's input *is* a decoded OSC message — going
// through the wire again would test the OSC decoder a second time and hide
// which of the two refused something. It also reaches shapes the OSC decoder
// cannot produce, which is how the defensive guards get exercised at all.
//
// Corpus mode then runs both layers over every committed capture, which is
// where the composition is checked: recorded bytes in, VMC messages out,
// against counts derived from the generator's structure.
#include "vrmAdapterVmc/VmcMessage.h"

#include "vrmAdapterVmc/Diagnostics.h"
#include "vrmAdapterVmc/OscPacket.h"
#include "vrmAdapterVmc/PacketCapture.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace
{

using vrmAdapterVmc::Diagnostic;
using vrmAdapterVmc::DiagnosticCode;
using vrmAdapterVmc::DiagnosticSeverity;
using vrmAdapterVmc::OscArgument;
using vrmAdapterVmc::OscMessage;
using vrmAdapterVmc::OscPacket;
using vrmAdapterVmc::VmcMessage;
using vrmAdapterVmc::VmcMessageKind;
using vrmAdapterVmc::VmcMessageKindCount;
using vrmAdapterVmc::VmcPacket;

// ---------------------------------------------------------------------------
// Building an OSC message by hand
// ---------------------------------------------------------------------------

struct Arg
{
    char tag = '\0';
    std::int64_t integer = 0;
    double real = 0.0;
    const char* text = nullptr;
};

Arg I(std::int64_t value) { return Arg{'i', value, 0.0, nullptr}; }
Arg F(double value) { return Arg{'f', 0, value, nullptr}; }
Arg D(double value) { return Arg{'d', 0, value, nullptr}; }
Arg S(const char* value) { return Arg{'s', 0, 0.0, value}; }

// Owns its type tag string, which `OscMessage::typeTags` then points into — so
// it is neither copyable nor movable. A short tag string lives inside the
// object under SSO, and moving one would leave the view pointing at the corpse:
// the same lifetime trap `DecodeOscPacket`'s deleted rvalue overload refuses
// one layer down, arriving here from the other direction. Addresses and text
// arguments are literals, which outlive everything.
class Built
{
public:
    Built(std::string_view address, const std::vector<Arg>& arguments)
    {
        for (const Arg& argument : arguments) {
            _tags += argument.tag;
        }
        _message.address = address;
        _message.typeTags = _tags;
        _message.arguments.reserve(arguments.size());
        for (const Arg& argument : arguments) {
            OscArgument decoded;
            decoded.tag = argument.tag;
            decoded.integer = argument.integer;
            decoded.real = argument.real;
            if (argument.text) {
                decoded.text = argument.text;
            }
            _message.arguments.push_back(decoded);
        }
    }

    Built(const Built&) = delete;
    Built& operator=(const Built&) = delete;

    const OscMessage& Get() const { return _message; }

private:
    std::string _tags;
    OscMessage _message;
};

// The eight arguments every VMC transform carries: a name, a position, and a
// quaternion in the sender's (x, y, z, w) order. A `std::vector` rather than an
// `initializer_list`, which refers to a temporary array and would dangle the
// moment it left this function.
std::vector<Arg>
Transform(const char* name, double px, double py, double pz, double qx,
          double qy, double qz, double qw)
{
    return {S(name), F(px), F(py), F(pz), F(qx), F(qy), F(qz), F(qw)};
}

bool
Decode(const Built& built, VmcMessage* out, Diagnostic* diagnostic = nullptr)
{
    return vrmAdapterVmc::DecodeVmcMessage(built.Get(), out, diagnostic);
}

// ---------------------------------------------------------------------------

void
TestTheKindTableIsWholeAndAddressesRoundTrip()
{
    for (std::size_t index = 0; index < VmcMessageKindCount; ++index) {
        const auto kind = static_cast<VmcMessageKind>(index);
        const std::string_view address =
            vrmAdapterVmc::VmcMessageKindAddress(kind);
        assert(!address.empty());
        assert(address.rfind("/VMC/Ext/", 0) == 0);
        const auto found = vrmAdapterVmc::FindVmcMessageKind(address);
        assert(found && *found == kind);
    }
    // Count is the "no message" value and names nothing.
    assert(vrmAdapterVmc::VmcMessageKindAddress(VmcMessageKind::Count).empty());
    assert(vrmAdapterVmc::VmcMessageKindTypeTags(VmcMessageKind::BoneTransform)
           == "sfffffff");
    assert(vrmAdapterVmc::VmcMessageKindTypeTags(VmcMessageKind::BlendApply)
           .empty());
}

void
TestEachKnownAddressDecodes()
{
    const Built availability("/VMC/Ext/OK", {I(1), I(3), I(0)});
    VmcMessage message;
    Diagnostic diagnostic;
    if (!Decode(availability, &message, &diagnostic)) {
        std::fprintf(stderr, "%s\n",
                     vrmAdapterVmc::FormatDiagnostic(diagnostic).c_str());
        assert(false);
    }
    assert(message.kind == VmcMessageKind::Availability);
    assert(message.availability.loaded == 1);
    assert(message.availability.calibrationState
           && *message.availability.calibrationState == 3);
    assert(message.availability.calibrationMode
           && *message.availability.calibrationMode == 0);
    assert(message.unreadArguments == 0);
    // The single-message overload has no packet to count within.
    assert(message.oscIndex == 0);

    // The form every sender version carries. "The sender did not say" is absent
    // rather than zero, because calibration state 0 means *un*calibrated and a
    // defaulted 0 would report a sender as uncalibrated for not being new.
    const Built minimal("/VMC/Ext/OK", {I(1)});
    assert(Decode(minimal, &message));
    assert(message.availability.loaded == 1);
    assert(!message.availability.calibrationState);
    assert(!message.availability.calibrationMode);

    const Built time("/VMC/Ext/T", {F(12.5)});
    assert(Decode(time, &message));
    assert(message.kind == VmcMessageKind::Time);
    assert(message.seconds == 12.5);

    const Built model("/VMC/Ext/VRM", {S("avatar.vrm"), S("Example Avatar")});
    assert(Decode(model, &message));
    assert(message.kind == VmcMessageKind::Model);
    assert(message.name == "avatar.vrm");
    assert(message.title == "Example Avatar");

    const Built root("/VMC/Ext/Root/Pos",
                     Transform("root", 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0));
    assert(Decode(root, &message));
    assert(message.kind == VmcMessageKind::RootTransform);
    assert(message.name == "root");

    const Built bone("/VMC/Ext/Bone/Pos",
                     Transform("LeftUpperArm", 0.12, 0.0, 0.0, 0.0, 0.0, 0.0,
                               1.0));
    assert(Decode(bone, &message));
    assert(message.kind == VmcMessageKind::BoneTransform);
    // Plain text. Whether "LeftUpperArm" is a `motion::HumanBone` is a question
    // this layer must not be able to answer.
    assert(message.name == "LeftUpperArm");
    assert(message.transform.position[0] == 0.12f);

    const Built blend("/VMC/Ext/Blend/Val", {S("Joy"), F(0.25)});
    assert(Decode(blend, &message));
    assert(message.kind == VmcMessageKind::BlendValue);
    assert(message.name == "Joy");
    assert(message.value == 0.25f);

    // No arguments at all, which is a bare comma on the wire and a well-formed
    // message.
    const Built apply("/VMC/Ext/Blend/Apply", {});
    assert(Decode(apply, &message));
    assert(message.kind == VmcMessageKind::BlendApply);
    assert(message.unreadArguments == 0);
    assert(message.name.empty());
}

void
TestNothingIsConvertedOnTheWayThrough()
{
    // Four distinguishable components, so a decoder that rotated them into (w,
    // x, y, z) -- the order `pxr::GfQuatf` uses one layer down -- cannot pass
    // by symmetry. Handedness, up axis, units and normalisation belong to the
    // skeleton map; a conversion here would make the corpus agree with exactly
    // one downstream reading of it.
    const Built bone("/VMC/Ext/Bone/Pos",
                     Transform("Hips", 1.0, 2.0, 3.0, 0.25, 0.5, 0.75, 2.0));
    VmcMessage message;
    assert(Decode(bone, &message));
    assert(message.transform.position[0] == 1.0f);
    assert(message.transform.position[1] == 2.0f);
    assert(message.transform.position[2] == 3.0f);
    assert(message.transform.rotation[0] == 0.25f);
    assert(message.transform.rotation[1] == 0.5f);
    assert(message.transform.rotation[2] == 0.75f);
    // Not normalised on the way through, and w stays last. A quaternion of
    // length 2.1 is a sender problem the canonical layer reports; silently
    // fixing it here would hide it from the fixture that recorded it.
    assert(message.transform.rotation[3] == 2.0f);
}

void
TestUnimplementedAddressesAreUnsupportedNotMalformed()
{
    // Every one of these is well-formed OSC, and refusing them as malformed
    // would blame the sender for traffic it emitted correctly. The last two are
    // the reason the address match is exact: a prefix test would make both into
    // bone poses and then read arguments that are not there.
    const char* const addresses[] = {
        "/foo/bar",           "/VMC/Ext/Midi/Note", "/VMC/Ext/Hmd/Pos",
        "/VMC/Ext/Con/Pos",   "/VMC/Ext/Cam",       "/VMC/Ext/Opt",
        "/VMC/Ext/Bone",      "/VMC/Ext/Bone/Pos/2",
    };

    // Carries a real decode, so "left untouched" is a claim about the refusal
    // rather than about a fresh default. A receive loop reuses one of these per
    // datagram, and a decoder that half-filled it on the way out would hand the
    // assembler the previous bone under this message's name.
    const Built previous("/VMC/Ext/Bone/Pos",
                         Transform("Hips", 0.0, 0.9, 0.0, 0.0, 0.0, 0.0, 1.0));
    VmcMessage message;
    assert(Decode(previous, &message));

    for (const char* address : addresses) {
        // Deliberately the *bone* argument shape: `/VMC/Ext/Hmd/Pos` really
        // does carry it, and matching on arguments rather than on the address
        // would decode a headset as a humanoid bone.
        const Built built(
            address, Transform("Head", 0.0, 1.6, 0.0, 0.0, 0.0, 0.0, 1.0));
        Diagnostic diagnostic;
        if (Decode(built, &message, &diagnostic)) {
            std::fprintf(stderr, "%s decoded as a VMC message\n", address);
            assert(false);
        }
        assert(diagnostic.code == DiagnosticCode::UnsupportedMessage);
        assert(diagnostic.severity == DiagnosticSeverity::Info);
        assert(diagnostic.recoverable);
        assert(diagnostic.subject == address);
        assert(message.kind == VmcMessageKind::BoneTransform);
        assert(message.name == "Hips");
        assert(message.transform.position[1] == 0.9f);
    }
}

struct BadForm
{
    const char* name;
    const char* address;
    std::vector<Arg> arguments;
    const char* carried;
};

void
TestAKnownAddressWithTheWrongArgumentsIsMalformed()
{
    const std::vector<BadForm> cases = {
        {"a bone with three floats", "/VMC/Ext/Bone/Pos",
         {S("Hips"), F(0.0), F(0.9), F(0.0)}, ",sfff"},
        // The count is right and every type is wrong. OSC puts an `f` and a `d`
        // in the same field, so a decoder reading values without checking tags
        // would accept this and pin nothing about the wire format.
        {"a bone in doubles", "/VMC/Ext/Bone/Pos",
         {S("Hips"), D(0.0), D(0.9), D(0.0), D(0.0), D(0.0), D(0.0), D(1.0)},
         ",sddddddd"},
        {"a bone with its name last", "/VMC/Ext/Bone/Pos",
         {F(0.0), F(0.9), F(0.0), F(0.0), F(0.0), F(0.0), F(1.0), S("Hips")},
         ",fffffffs"},
        {"a time in integer seconds", "/VMC/Ext/T", {I(12)}, ",i"},
        {"a time with no argument", "/VMC/Ext/T", {}, ","},
        {"a blend value with no name", "/VMC/Ext/Blend/Val", {F(1.0), F(0.5)},
         ",ff"},
        {"an availability in floats", "/VMC/Ext/OK", {F(1.0)}, ",f"},
        {"a model with no title", "/VMC/Ext/VRM", {S("avatar.vrm"), I(1)},
         ",si"},
    };

    // Reused across the loop and carrying a real decode, for the reason above:
    // a refusal that left half of this message's values behind would be
    // indistinguishable from the previous bone arriving twice.
    const Built previous("/VMC/Ext/Blend/Val", {S("Joy"), F(0.25)});
    VmcMessage message;
    assert(Decode(previous, &message));

    for (const BadForm& testCase : cases) {
        const Built built(testCase.address, testCase.arguments);
        Diagnostic diagnostic;
        if (Decode(built, &message, &diagnostic)) {
            std::fprintf(stderr, "accepted a malformed form: %s\n",
                         testCase.name);
            assert(false);
        }
        assert(diagnostic.code == DiagnosticCode::PacketMalformed);
        assert(diagnostic.severity == DiagnosticSeverity::Warning);
        assert(diagnostic.recoverable);
        assert(diagnostic.subject == testCase.address);

        // Both tag strings, so a sender-compatibility surprise reads as "this
        // sender writes X where VMC says Y" rather than as a bare refusal.
        const std::string expected =
            std::string(",")
            + std::string(vrmAdapterVmc::VmcMessageKindTypeTags(
                *vrmAdapterVmc::FindVmcMessageKind(testCase.address)));
        if (diagnostic.detail.find(expected) == std::string::npos
            || diagnostic.detail.find(testCase.carried) == std::string::npos) {
            std::fprintf(stderr, "%s: detail was \"%s\", wanted %s and %s\n",
                         testCase.name, diagnostic.detail.c_str(),
                         expected.c_str(), testCase.carried);
            assert(false);
        }
        assert(message.kind == VmcMessageKind::BlendValue);
        assert(message.name == "Joy");
        assert(message.value == 0.25f);
    }
}

void
TestArgumentsPastTheKnownFormAreCountedNotRead()
{
    VmcMessage message;

    // A status integer this decoder has no capture of. Refusing it would blame
    // a sender for being newer; reading it would be inventing a meaning.
    const Built longer("/VMC/Ext/OK", {I(1), I(3), I(0), I(2)});
    assert(Decode(longer, &message));
    assert(message.availability.loaded == 1);
    assert(*message.availability.calibrationState == 3);
    assert(*message.availability.calibrationMode == 0);
    assert(message.unreadArguments == 1);

    // The first optional argument whose tag disagrees ends the known form, and
    // everything from there is unread -- including the one after it, which
    // happens to be the type the form wanted.
    const Built mismatched("/VMC/Ext/OK", {I(1), F(3.0), I(0)});
    assert(Decode(mismatched, &message));
    assert(message.availability.loaded == 1);
    assert(!message.availability.calibrationState);
    assert(message.unreadArguments == 2);

    // A third string on the model message. What it carries is not this layer's
    // claim to make -- no fixture here records one -- so it is a count.
    const Built hashed("/VMC/Ext/VRM",
                       {S("avatar.vrm"), S("Example Avatar"), S("0f1e2d")});
    assert(Decode(hashed, &message));
    assert(message.name == "avatar.vrm");
    assert(message.title == "Example Avatar");
    assert(message.unreadArguments == 1);

    // Six more floats after the root quaternion. The first seven still decode,
    // and the extras are counted rather than folded into the transform.
    const Built scaled("/VMC/Ext/Root/Pos",
                       {S("root"), F(0.0), F(0.0), F(0.0), F(0.0), F(0.0),
                        F(0.0), F(1.0), F(1.0), F(1.0), F(1.0), F(0.0), F(0.0),
                        F(0.0)});
    assert(Decode(scaled, &message));
    assert(message.kind == VmcMessageKind::RootTransform);
    assert(message.transform.rotation[3] == 1.0f);
    assert(message.unreadArguments == 6);

    const Built decorated("/VMC/Ext/Blend/Apply", {I(1)});
    assert(Decode(decorated, &message));
    assert(message.kind == VmcMessageKind::BlendApply);
    assert(message.unreadArguments == 1);
}

void
TestAPacketRefusesMessagesNotTheDatagram()
{
    const Built time("/VMC/Ext/T", {F(30.0)});
    const Built hips("/VMC/Ext/Bone/Pos",
                     Transform("Hips", 0.0, 0.9, 0.0, 0.0, 0.0, 0.0, 1.0));
    const Built broken("/VMC/Ext/Bone/Pos", {S("Spine"), F(0.0), F(0.1)});
    const Built headset("/VMC/Ext/Hmd/Pos",
                        Transform("Head", 0.0, 1.6, 0.0, 0.0, 0.0, 0.0, 1.0));
    const Built chest("/VMC/Ext/Bone/Pos",
                      Transform("Chest", 0.0, 0.12, 0.0, 0.0, 0.0, 0.0, 1.0));
    const Built apply("/VMC/Ext/Blend/Apply", {});

    OscPacket packet;
    packet.bundled = true;
    for (const Built* built :
         {&time, &hips, &broken, &headset, &chest, &apply}) {
        packet.messages.push_back(built->Get());
    }

    // Seeded, because a receive loop accumulates across a session and a decoder
    // that cleared the vector would erase the frame before this one.
    std::vector<Diagnostic> diagnostics;
    diagnostics.push_back(
        vrmAdapterVmc::MakeDiagnostic(DiagnosticCode::SourceRestarted, "seed"));

    VmcPacket decoded;
    // False because one message was malformed -- the other five are in
    // `decoded` regardless, which is the difference from the OSC layer's
    // all-or-nothing.
    assert(!vrmAdapterVmc::DecodeVmcPacket(packet, &decoded, &diagnostics));
    assert(decoded.messages.size() == 4);
    assert(decoded.malformed == 1);
    assert(decoded.unsupported == 1);

    assert(decoded.messages[0].kind == VmcMessageKind::Time);
    assert(decoded.messages[1].name == "Hips");
    assert(decoded.messages[2].name == "Chest");
    assert(decoded.messages[3].kind == VmcMessageKind::BlendApply);
    // The index in the datagram, not in the result: the two differ by exactly
    // the messages that were refused, which is what makes it worth carrying.
    assert(decoded.messages[2].oscIndex == 4);
    assert(decoded.messages[3].oscIndex == 5);

    assert(diagnostics.size() == 3);
    assert(diagnostics[0].code == DiagnosticCode::SourceRestarted);
    assert(diagnostics[1].code == DiagnosticCode::PacketMalformed);
    assert(diagnostics[1].detail.find("message 2 of the datagram")
           != std::string::npos);
    assert(diagnostics[2].code == DiagnosticCode::UnsupportedMessage);
    assert(diagnostics[2].detail.find("message 3 of the datagram")
           != std::string::npos);

    // A datagram of nothing but traffic this adapter ignores is not a failure.
    // Every sender emits one, and reporting it as an error would make an error
    // the normal state of a live session.
    OscPacket ignorable;
    ignorable.messages.push_back(headset.Get());
    ignorable.messages.push_back(headset.Get());
    VmcPacket nothing;
    assert(vrmAdapterVmc::DecodeVmcPacket(ignorable, &nothing));
    assert(nothing.messages.empty());
    assert(nothing.unsupported == 2);
    assert(nothing.malformed == 0);
}

void
TestTheArgumentGuardsRefuseRatherThanDereference()
{
    const Built bone("/VMC/Ext/Bone/Pos",
                     Transform("Hips", 0.0, 0.9, 0.0, 0.0, 0.0, 0.0, 1.0));
    Diagnostic diagnostic;
    assert(!vrmAdapterVmc::DecodeVmcMessage(bone.Get(), nullptr, &diagnostic));
    assert(diagnostic.code == DiagnosticCode::PacketMalformed);

    // The OSC layer emits one argument per type tag. A message where the two
    // disagree did not come from it -- and reading arguments by tag index would
    // run off the end, so it is refused rather than trusted for its provenance.
    OscMessage truncated = bone.Get();
    truncated.arguments.resize(3);
    VmcMessage message;
    assert(!vrmAdapterVmc::DecodeVmcMessage(truncated, &message, &diagnostic));
    assert(diagnostic.code == DiagnosticCode::PacketMalformed);
    assert(diagnostic.detail.find("8 argument(s) and 3 were given")
           != std::string::npos);
    assert(message.kind == VmcMessageKind::Count);

    OscPacket packet;
    packet.messages.push_back(bone.Get());
    assert(!vrmAdapterVmc::DecodeVmcPacket(packet, nullptr));

    // No diagnostic is the documented default, and it must not crash either.
    assert(!vrmAdapterVmc::DecodeVmcMessage(truncated, &message));
    VmcPacket decoded;
    assert(vrmAdapterVmc::DecodeVmcPacket(packet, &decoded));
    assert(decoded.messages.size() == 1);
}

// ---------------------------------------------------------------------------
// Corpus mode: both layers, over every committed capture
// ---------------------------------------------------------------------------

struct Expected
{
    const char* file;
    std::size_t decoded;
    std::size_t unsupported;
    std::size_t malformed;
    // Per kind, in enum order: OK, T, VRM, Root/Pos, Bone/Pos, Blend/Val,
    // Blend/Apply.
    std::array<std::size_t, VmcMessageKindCount> kinds;
};

// Derived from the generator's structure, not from a run. A frame is a `T`, a
// root and one message per bone; neutral-standing sends a 22-bone torso in five
// bundles after a two-message handshake, arm-raise a 21-bone one message at a
// time, mixed-traffic adds three blend values and an apply per frame with three
// device messages this adapter ignores, and sender-restart is seven full frames
// plus one cut off after six bones, around a second handshake.
constexpr Expected kExpected[] = {
    {"arm-raise-30hz.vmcpackets", 117, 0, 0, {1, 5, 1, 5, 105, 0, 0}},
    // Eight of its ten datagrams never reach this layer -- the OSC decoder
    // refuses them, which `vrmAdapterVmc_oscCorpus` is what pins. The two that
    // do are valid OSC outside what this adapter implements.
    {"malformed-packets.vmcpackets", 0, 2, 0, {0, 0, 0, 0, 0, 0, 0}},
    {"mixed-traffic-30hz.vmcpackets", 83, 10, 0, {1, 3, 1, 3, 63, 9, 3}},
    {"neutral-standing-30hz.vmcpackets", 122, 0, 0, {1, 5, 1, 5, 110, 0, 0}},
    {"sender-restart-30hz.vmcpackets", 173, 0, 0, {2, 8, 2, 8, 153, 0, 0}},
};

struct Decoded
{
    std::size_t decoded = 0;
    std::size_t unsupported = 0;
    std::size_t malformed = 0;
    std::array<std::size_t, VmcMessageKindCount> kinds{};
    std::vector<double> times;
    bool everyRotationIsIdentity = true;
    bool everyRootIsAtTheOrigin = true;
};

bool
IsIdentity(const vrmAdapterVmc::VmcTransform& transform)
{
    return transform.rotation[0] == 0.0f && transform.rotation[1] == 0.0f
        && transform.rotation[2] == 0.0f && transform.rotation[3] == 1.0f;
}

int
CheckCorpus(const std::filesystem::path& directory)
{
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
    // One capture has to disagree with the identity pose, or the check that the
    // neutral one is all identities would also pass on a decoder that returned
    // its defaults and read nothing.
    bool anyRotationMoved = false;

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

        vrmAdapterVmc::PacketCapture capture;
        vrmAdapterVmc::PacketCaptureError error;
        if (!vrmAdapterVmc::ReadPacketCaptureFile(path.string(), &capture,
                                                  &error)) {
            std::fprintf(stderr, "%s:%zu: %s\n", name.c_str(), error.line,
                         error.message.c_str());
            ++failures;
            continue;
        }

        Decoded actual;
        for (const vrmAdapterVmc::RecordedDatagram& datagram :
             capture.datagrams) {
            OscPacket osc;
            // A datagram the OSC layer refuses never reaches this one. Which
            // eight of the malformed capture's ten those are is a claim
            // `vrmAdapterVmc_oscCorpus` already makes; repeating it here would
            // move it rather than strengthen it.
            if (!vrmAdapterVmc::DecodeOscPacket(datagram.bytes, &osc)) {
                continue;
            }

            VmcPacket vmc;
            std::vector<Diagnostic> diagnostics;
            vrmAdapterVmc::DecodeVmcPacket(osc, &vmc, &diagnostics);
            actual.decoded += vmc.messages.size();
            actual.unsupported += vmc.unsupported;
            actual.malformed += vmc.malformed;
            if (vmc.malformed != 0) {
                for (const Diagnostic& diagnostic : diagnostics) {
                    if (diagnostic.code == DiagnosticCode::PacketMalformed) {
                        const std::string line =
                            vrmAdapterVmc::FormatDiagnostic(diagnostic);
                        std::fprintf(stderr, "%s: %s\n", name.c_str(),
                                     line.c_str());
                    }
                }
            }

            for (const VmcMessage& message : vmc.messages) {
                ++actual.kinds[static_cast<std::size_t>(message.kind)];
                switch (message.kind) {
                case VmcMessageKind::Time:
                    actual.times.push_back(message.seconds);
                    break;
                case VmcMessageKind::BoneTransform:
                    if (IsIdentity(message.transform)) {
                        break;
                    }
                    actual.everyRotationIsIdentity = false;
                    anyRotationMoved = true;
                    break;
                case VmcMessageKind::RootTransform:
                    if (message.transform.position[0] != 0.0f
                        || message.transform.position[1] != 0.0f
                        || message.transform.position[2] != 0.0f) {
                        actual.everyRootIsAtTheOrigin = false;
                    }
                    break;
                default:
                    break;
                }
                if (message.unreadArguments != 0) {
                    std::fprintf(stderr,
                                 "%s: %zu unread argument(s) on %s -- the "
                                 "corpus records no extended form\n",
                                 name.c_str(), message.unreadArguments,
                                 std::string(message.name).c_str());
                    ++failures;
                }
            }
        }

        if (actual.decoded != entry->decoded
            || actual.unsupported != entry->unsupported
            || actual.malformed != entry->malformed
            || actual.kinds != entry->kinds) {
            std::fprintf(stderr,
                         "%s: %zu decoded, %zu unsupported, %zu malformed, "
                         "kinds [%zu %zu %zu %zu %zu %zu %zu] -- expected %zu, "
                         "%zu, %zu, [%zu %zu %zu %zu %zu %zu %zu]\n",
                         name.c_str(), actual.decoded, actual.unsupported,
                         actual.malformed, actual.kinds[0], actual.kinds[1],
                         actual.kinds[2], actual.kinds[3], actual.kinds[4],
                         actual.kinds[5], actual.kinds[6], entry->decoded,
                         entry->unsupported, entry->malformed, entry->kinds[0],
                         entry->kinds[1], entry->kinds[2], entry->kinds[3],
                         entry->kinds[4], entry->kinds[5], entry->kinds[6]);
            ++failures;
            continue;
        }

        // Two claims about *values*, not counts. A neutral pose is every
        // rotation identity and a root at the origin, which is the one expected
        // result stateable without a second implementation to compare against;
        // and the sender's clock starts at 12.5 s where the receive clock
        // starts at 0, so a decoder that read the wrong one fails here.
        if (name == "neutral-standing-30hz.vmcpackets") {
            const double first =
                actual.times.empty() ? -1.0 : actual.times.front();
            if (!actual.everyRotationIsIdentity
                || !actual.everyRootIsAtTheOrigin || first != 12.5) {
                std::fprintf(stderr,
                             "%s: identity=%d origin=%d first sender time=%f "
                             "-- expected 1, 1, 12.500000\n",
                             name.c_str(),
                             actual.everyRotationIsIdentity ? 1 : 0,
                             actual.everyRootIsAtTheOrigin ? 1 : 0, first);
                ++failures;
            }
        }

        // The sender's clock goes backwards in this capture and is decoded
        // without complaint: VRM_VMC_TIMESTAMP_REGRESSION needs a memory of the
        // previous frame, and this layer has none. Raising it here would make
        // every out-of-order datagram a decode failure.
        if (name == "sender-restart-30hz.vmcpackets") {
            const bool monotonic = std::is_sorted(actual.times.begin(),
                                                  actual.times.end());
            if (monotonic || actual.malformed != 0) {
                std::fprintf(stderr,
                             "%s: sender clock monotonic=%d, malformed=%zu -- "
                             "expected a backwards clock decoded cleanly\n",
                             name.c_str(), monotonic ? 1 : 0, actual.malformed);
                ++failures;
            }
        }

        std::printf("%s: %zu decoded, %zu unsupported, %zu malformed\n",
                    name.c_str(), actual.decoded, actual.unsupported,
                    actual.malformed);
    }

    for (const Expected& entry : kExpected) {
        if (covered.find(entry.file) == covered.end()) {
            std::fprintf(stderr, "%s: expected in this test, absent from %s\n",
                         entry.file, directory.string().c_str());
            ++failures;
        }
    }
    if (!anyRotationMoved) {
        std::fprintf(stderr,
                     "no capture carries a rotation off identity; the neutral "
                     "check would pass on a decoder that read nothing\n");
        ++failures;
    }

    if (failures != 0) {
        std::fprintf(stderr, "%d corpus capture(s) failed\n", failures);
        return 1;
    }
    std::printf("VMC decode: %zu capture(s) verified\n", captures.size());
    return 0;
}

} // namespace

int
main(int argc, char** argv)
{
    if (argc > 1) {
        return CheckCorpus(std::filesystem::path(argv[1]));
    }

    TestTheKindTableIsWholeAndAddressesRoundTrip();
    TestEachKnownAddressDecodes();
    TestNothingIsConvertedOnTheWayThrough();
    TestUnimplementedAddressesAreUnsupportedNotMalformed();
    TestAKnownAddressWithTheWrongArgumentsIsMalformed();
    TestArgumentsPastTheKnownFormAreCountedNotRead();
    TestAPacketRefusesMessagesNotTheDatagram();
    TestTheArgumentGuardsRefuseRatherThanDereference();
    std::puts("vrmAdapterVmc VMC message tests passed");
    return 0;
}
