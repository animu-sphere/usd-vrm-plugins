// SPDX-License-Identifier: Apache-2.0
//
// What this adapter adds to the shared OSC decoder, and the corpus that proves
// the pair works over recorded bytes.
//
// The wire format's own suite is `libs/osc`'s: it was written here, frozen by
// OSC-0 before anything moved, and travelled with the decoder. What is left is
// the half that could not travel, and both parts of it are about this adapter
// rather than about OSC.
//
// **The map onto a frozen code.** `libs/osc` refuses a datagram with a subject
// and a detail and no code at all. `VRM_VMC_PACKET_MALFORMED` is this adapter's
// and is golden-tested in its formatted form, so the tests below check that the
// code, its severity, its recoverability and its subject all still come out of
// a refusal -- which is the whole of what the extraction could have broken
// silently.
//
// **The corpus.** The decoder against every committed capture, which is where
// two independent things meet: the recorded corpus was authored by a generator
// that encodes OSC, this decodes it, and neither was written from the other.
#include "vrmAdapterVmc/OscPacket.h"

#include "vrmAdapterVmc/Diagnostics.h"
#include "vrmAdapterVmc/PacketCapture.h"

#include <algorithm>
#include <cassert>
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

    Bytes& F32(float value)
    {
        std::uint32_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        return U32(bits);
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

void
TestARefusalArrivesAsThisAdaptersCode()
{
    // Everything about *which* datagrams are refused is `libs/osc`'s suite.
    // What is checked here is the translation: one refused datagram, and the
    // four fields only this adapter can supply.
    Bytes oneFloat;
    oneFloat.F32(0.5f);
    const std::vector<std::uint8_t> datagram =
        Message("/VMC/Ext/T", "ff", oneFloat.data);

    OscPacket packet;
    packet.messages.push_back({});
    Diagnostic diagnostic;
    assert(!vrmAdapterVmc::DecodeOscPacket(datagram, &packet, &diagnostic));

    // The code, and the two defaults its table row decides. A raise site that
    // filled these by hand could disagree with the table; `MakeDiagnostic` is
    // what makes that impossible, and this is what checks it happened.
    assert(diagnostic.code == DiagnosticCode::PacketMalformed);
    assert(diagnostic.recoverable);
    assert(diagnostic.severity
           == vrmAdapterVmc::DiagnosticDefaultSeverity(
               DiagnosticCode::PacketMalformed));

    // The subject and the detail are the shared decoder's, carried across
    // rather than reworded: the address it had read, and the byte it stopped
    // at. A translation that dropped either would leave a diagnostic naming no
    // datagram in particular.
    assert(diagnostic.subject == "/VMC/Ext/T");
    assert(diagnostic.detail.find("needs 4 bytes") != std::string::npos);
    assert(diagnostic.detail.find("(at byte 20)") != std::string::npos);

    // And the string a golden test compares. This is the one assertion the
    // extraction existed to leave standing.
    assert(vrmAdapterVmc::FormatDiagnostic(diagnostic).find(
               "[VRM_VMC_PACKET_MALFORMED]") == 0);

    // A refused datagram leaves the caller's packet as it was.
    assert(packet.messages.size() == 1);

    // The no-diagnostic overload takes the same decision and must not crash.
    assert(!vrmAdapterVmc::DecodeOscPacket(datagram, &packet));
}

void
TestAnUnimplementedVmcAddressIsNotThisLayersRefusal()
{
    // `/VMC/Ext/Midi/Note` is well-formed OSC that this adapter does not
    // implement, and the difference matters: refusing it here would blame the
    // sender for something it did correctly. Deciding it is unimplemented is
    // `VmcMessage`'s, as VRM_VMC_UNSUPPORTED_MESSAGE, which is never raised
    // from this layer.
    Bytes three;
    three.U32(1).U32(60).U32(100);
    const std::vector<std::uint8_t> unimplemented =
        Message("/VMC/Ext/Midi/Note", "iii", three.data);

    OscPacket packet;
    Diagnostic diagnostic;
    assert(vrmAdapterVmc::DecodeOscPacket(unimplemented, &packet, &diagnostic));
    assert(packet.messages.front().address == "/VMC/Ext/Midi/Note");
    assert(packet.messages.front().arguments[1].integer == 60);
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

    TestARefusalArrivesAsThisAdaptersCode();
    TestAnUnimplementedVmcAddressIsNotThisLayersRefusal();
    std::puts("vrmAdapterVmc OSC packet tests passed");
    return 0;
}
