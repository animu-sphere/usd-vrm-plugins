// SPDX-License-Identifier: Apache-2.0
//
// The recorded packet format, before anything decodes a packet.
//
// Two things are being pinned here and they are not the same thing. The unit
// tests pin the *format*: what it accepts, what it refuses, and the exact bytes
// the writer lays down. Corpus mode pins the *fixtures*: every committed
// capture parses, and re-emitting it reproduces the committed file byte for
// byte -- so a fixture can never drift from the writer without turning a test
// red, which is what lets every decoder test downstream compare a golden result
// rather than merely parse one. There is no corpus yet, and the reason is
// written down in the adapter's README: recording one needs the wire format,
// and this layer deliberately has none.
#include "vrmAdapterMocopi/PacketCapture.h"

#include "corpus.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <locale>
#include <sstream>
#include <string>
#include <vector>

namespace
{

using vrmAdapterMocopi::PacketCapture;
using vrmAdapterMocopi::PacketCaptureError;
using vrmAdapterMocopi::RecordedDatagram;

RecordedDatagram
Datagram(double receiveTime, std::vector<std::uint8_t> bytes)
{
    RecordedDatagram datagram;
    datagram.receiveTime = receiveTime;
    datagram.bytes = std::move(bytes);
    return datagram;
}

std::string
Write(const PacketCapture& capture)
{
    std::ostringstream output;
    const bool ok = vrmAdapterMocopi::WritePacketCapture(output, capture);
    assert(ok);
    (void)ok;
    return output.str();
}

bool
Read(const std::string& text, PacketCapture* capture,
     PacketCaptureError* error = nullptr)
{
    std::istringstream input(text);
    return vrmAdapterMocopi::ReadPacketCapture(input, capture, error);
}

// Sixteen bytes that are not a packet, and are not claimed to be one.
//
// Nothing in this change asserts anything about the wire format: the decoder
// that would make such an assertion testable does not exist yet, and a payload
// shaped to look plausible here would be this repository's guess about a
// vendor's protocol, wearing the appearance of evidence. What these bytes are
// for is the gutter -- a printable run followed by a binary tail is exactly the
// shape the gutter exists to make legible in a review.
std::vector<std::uint8_t>
NotAPacket()
{
    return {
        'n', 'o', 't', '-', 'a', '-', 'p', 'a', 'c', 'k', 'e', 't',
        0x00, 0x01, 0x80, 0xff,
    };
}

void
TestTheWrittenLayoutIsTheDocumentedOne()
{
    PacketCapture capture;
    capture.sender = "example.synthetic";
    capture.device = "example.synthetic";
    capture.sourceId = "layout-01";

    std::vector<std::uint8_t> bytes;
    for (int value = 0; value < 20; ++value) {
        bytes.push_back(static_cast<std::uint8_t>(value));
    }
    capture.datagrams.push_back(Datagram(0.0, bytes));

    // Sixteen bytes a line, lowercase, the hex column padded so a short last
    // line's gutter stays in the same column as a full one's. The header keys
    // are emitted in a fixed order and only when carried.
    const std::string expected =
        "!mocopi-packet-capture 1\n"
        "sender example.synthetic\n"
        "device example.synthetic\n"
        "sourceId layout-01\n"
        "\n"
        "d 0.000000 20\n"
        "  00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f"
        "  |................|\n"
        "  10 11 12 13"
        + std::string(38, ' ') + "|....|\n";

    const std::string written = Write(capture);
    if (written != expected) {
        std::fprintf(stderr, "written:\n%s\nexpected:\n%s\n", written.c_str(),
                     expected.c_str());
    }
    assert(written == expected);
}

void
TestRoundTripIsByteIdentical()
{
    PacketCapture capture;
    capture.sender = "example.synthetic";
    capture.device = "example.synthetic";
    capture.sourceId = "round-trip-01";
    capture.listenEndpoint = "0.0.0.0:12351";
    capture.peerEndpoint = "192.168.0.20:52001";
    capture.datagrams.push_back(Datagram(0.0, NotAPacket()));
    // A zero-length datagram, then one that is not a multiple of the line
    // width: the two shapes the emitter is easiest to get wrong on.
    capture.datagrams.push_back(Datagram(0.020000, {}));
    capture.datagrams.push_back(
        Datagram(0.020000, {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                            0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
                            0x10}));

    const std::string first = Write(capture);

    PacketCapture parsed;
    PacketCaptureError error;
    if (!Read(first, &parsed, &error)) {
        std::fprintf(stderr, "line %zu: %s\n", error.line,
                     error.message.c_str());
        assert(false);
    }

    assert(parsed.sender == capture.sender);
    assert(parsed.device == capture.device);
    assert(parsed.sourceId == capture.sourceId);
    assert(parsed.listenEndpoint == capture.listenEndpoint);
    assert(parsed.peerEndpoint == capture.peerEndpoint);
    assert(parsed.datagrams.size() == 3);
    assert(parsed.datagrams[0].bytes == NotAPacket());
    assert(parsed.datagrams[1].bytes.empty());
    assert(parsed.datagrams[2].bytes.size() == 17);
    // Equal receive times are legal: two datagrams can land in one clock tick.
    assert(parsed.datagrams[1].receiveTime == parsed.datagrams[2].receiveTime);

    assert(Write(parsed) == first);
}

void
TestAGutterIsCheckedRatherThanSkipped()
{
    // A reviewer reads the gutter and not the hex, so a gutter that disagrees
    // with its bytes is worse than no gutter at all.
    const std::string lying =
        "!mocopi-packet-capture 1\n"
        "\n"
        "d 0.000000 4\n"
        "  6e 6f 74 2d  |XXXX|\n";
    PacketCapture capture;
    PacketCaptureError error;
    assert(!Read(lying, &capture, &error));
    assert(error.line == 4);
    assert(capture.datagrams.empty());

    // Absent is fine -- a gutter is a review aid the writer always emits and a
    // hand-authored fixture may omit.
    const std::string bare =
        "!mocopi-packet-capture 1\n"
        "\n"
        "d 0.000000 4\n"
        "  6e 6f 74 2d\n";
    assert(Read(bare, &capture));
    assert(capture.datagrams.size() == 1);

    // Uppercase reads and is canonicalised on the way out, so a hand-edited
    // fixture cannot stay uppercase in the corpus without failing round trip.
    const std::string upper =
        "!mocopi-packet-capture 1\n"
        "\n"
        "d 0.000000 4\n"
        "  6E 6F 74 2D  |not-|\n";
    PacketCapture parsedUpper;
    assert(Read(upper, &parsedUpper));
    assert(parsedUpper.datagrams[0].bytes
           == std::vector<std::uint8_t>({0x6e, 0x6f, 0x74, 0x2d}));
    assert(Write(parsedUpper).find("6e 6f 74 2d") != std::string::npos);

    // A payload byte 0x7c renders as '|' inside the gutter, so the gutter runs
    // to the last '|' on the line rather than the second.
    const std::string pipes =
        "!mocopi-packet-capture 1\n"
        "\n"
        "d 0.000000 3\n"
        "  7c 41 7c  ||A||\n";
    PacketCapture parsedPipes;
    assert(Read(pipes, &parsedPipes));
    assert(parsedPipes.datagrams[0].bytes.size() == 3);
}

struct BadCapture
{
    const char* name;
    std::string text;
    std::size_t line;
};

void
TestMalformedCapturesAreRefusedAndSayWhere()
{
    const std::string header = "!mocopi-packet-capture 1\n";
    const BadCapture cases[] = {
        {"no magic", "sender example\n", 1},
        // The magic is the fixture's type tag, and this is the case that makes
        // it one: the sibling live adapter's capture format has the same record
        // grammar, so without a distinct magic its fixtures would parse here
        // and be blamed on a source that never sent them.
        {"the other live adapter's magic", "!vmc-packet-capture 1\n", 1},
        {"unsupported version", "!mocopi-packet-capture 2\n", 1},
        {"junk after the version", "!mocopi-packet-capture 1 extra\n", 1},
        {"unknown header key", header + "provider example\n", 2},
        {"header key with no value", header + "device\n", 2},
        {"duplicated header key", header + "device a\ndevice b\n", 3},
        {"header key after a record",
         header + "d 0.000000 1\n  6e  |n|\ndevice late\n", 4},
        {"receive time going backwards",
         header + "d 1.000000 1\n  6e  |n|\nd 0.500000 1\n  6e  |n|\n", 4},
        {"negative receive time", header + "d -0.000001 1\n", 2},
        {"non-finite receive time", header + "d nan 1\n", 2},
        {"missing byte length", header + "d 0.000000\n", 2},
        {"negative byte length", header + "d 0.000000 -4\n", 2},
        {"a datagram larger than any UDP payload",
         header + "d 0.000000 65508\n", 2},
        {"junk after the byte length", header + "d 0.000000 4 extra\n", 2},
        {"a one-digit hex token", header + "d 0.000000 4\n  6e 6 74 2d\n", 3},
        {"a non-hex token", header + "d 0.000000 4\n  6e 6f 74 zz\n", 3},
        {"more bytes than declared",
         header + "d 0.000000 2\n  6e 6f 74 2d\n", 3},
        {"a hex line carrying nothing",
         header + "d 0.000000 4\n  ||\n  6e 6f 74 2d\n", 3},
        {"an unclosed gutter",
         header + "d 0.000000 4\n  6e 6f 74 2d |not-\n", 3},
        {"text after the gutter",
         header + "d 0.000000 4\n  6e 6f 74 2d  |not-| trailing\n", 3},
        {"a record cut short by the next one",
         header + "d 0.000000 4\n  6e 6f\nd 0.100000 1\n  6e\n", 4},
        {"a record cut short by the end of the capture",
         header + "d 0.000000 4\n  6e 6f\n", 3},
        {"hex outside a record", header + "  6e 6f 74 2d\n", 2},
        {"a capture with no datagrams", header + "sender example\n", 2},
    };

    for (const BadCapture& testCase : cases) {
        PacketCapture capture;
        PacketCaptureError error;
        if (Read(testCase.text, &capture, &error)) {
            std::fprintf(stderr, "malformed capture was accepted: %s\n",
                         testCase.name);
            assert(false);
        }
        // A rejection has to say what and where, or a fixture cannot be fixed
        // without a debugger.
        assert(!error.message.empty());
        if (error.line != testCase.line) {
            std::fprintf(stderr, "%s: reported line %zu, expected %zu (%s)\n",
                         testCase.name, error.line, testCase.line,
                         error.message.c_str());
            assert(false);
        }
        // A failed parse leaves the caller's capture untouched.
        assert(capture.datagrams.empty());
        assert(capture.sender.empty());
        assert(capture.device.empty());
    }
}

// A locale whose decimal point is a comma, constructed in-process so this test
// depends on no system locale being installed anywhere.
struct CommaDecimalPoint : std::numpunct<char>
{
protected:
    char do_decimal_point() const override { return ','; }
};

void
TestTheWriterSurvivesAHostileGlobalLocale()
{
    // A DCC that calls setlocale is the realistic case, and `d 0,020000` would
    // be a capture this reader refuses -- written by this writer.
    PacketCapture capture;
    capture.datagrams.push_back(Datagram(0.020000, {0x6e}));

    const std::locale previous = std::locale::global(
        std::locale(std::locale::classic(), new CommaDecimalPoint));
    const std::string written = Write(capture);
    std::locale::global(previous);

    assert(written.find("d 0.020000 1\n") != std::string::npos);
    PacketCapture parsed;
    assert(Read(written, &parsed));
    assert(parsed.datagrams.size() == 1);
}

void
TestCommentsAndBlankLinesAreIgnored()
{
    const std::string text =
        "# a recorded session\n"
        "\n"
        "!mocopi-packet-capture 1\n"
        "# provenance\n"
        "sender example.synthetic\n"
        "device example.synthetic\n"
        "\n"
        "d 0.000000 4\n"
        "# the first four bytes only\n"
        "  6e 6f 74 2d  |not-|\n";
    PacketCapture capture;
    PacketCaptureError error;
    if (!Read(text, &capture, &error)) {
        std::fprintf(stderr, "line %zu: %s\n", error.line,
                     error.message.c_str());
        assert(false);
    }
    assert(capture.sender == "example.synthetic");
    assert(capture.device == "example.synthetic");
    assert(capture.datagrams.size() == 1);
}

// Corpus mode. Every committed capture must parse, and re-emitting it must
// reproduce the committed bytes exactly. Wired up now and fed nothing yet: the
// CTest entry that calls it only exists once a corpus directory does, so the
// first recorded session becomes a checked fixture rather than a file somebody
// remembers to look at.
int
CheckCorpus(const std::filesystem::path& directory)
{
    std::vector<std::filesystem::path> captures;
    if (!vrmAdapterMocopiTests::CollectCaptures(directory, &captures)) {
        return 1;
    }

    int failures = 0;
    for (const std::filesystem::path& path : captures) {
        const std::string name = path.filename().string();
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            std::fprintf(stderr, "%s: could not open\n", name.c_str());
            ++failures;
            continue;
        }
        std::ostringstream buffer;
        buffer << file.rdbuf();
        const std::string original = buffer.str();

        PacketCapture parsed;
        PacketCaptureError error;
        std::istringstream input(original);
        if (!vrmAdapterMocopi::ReadPacketCapture(input, &parsed, &error)) {
            std::fprintf(stderr, "%s:%zu: %s\n", name.c_str(), error.line,
                         error.message.c_str());
            ++failures;
            continue;
        }

        std::ostringstream rewritten;
        if (!vrmAdapterMocopi::WritePacketCapture(rewritten, parsed)
            || rewritten.str() != original) {
            std::fprintf(stderr, "%s: does not round trip byte-identically\n",
                         name.c_str());
            ++failures;
            continue;
        }

        // Provenance is the manifest's job to describe and the fixture's job to
        // carry: a committed capture that names neither its source nor its
        // session cannot be traced back to what produced it.
        if (parsed.sender.empty() || parsed.sourceId.empty()) {
            std::fprintf(stderr, "%s: no sender or sourceId\n", name.c_str());
            ++failures;
            continue;
        }

        std::size_t payload = 0;
        for (const RecordedDatagram& datagram : parsed.datagrams) {
            payload += datagram.bytes.size();
        }
        std::printf("%s: %zu datagram(s), %zu payload byte(s), %.3f s, round "
                    "trip ok\n",
                    name.c_str(), parsed.datagrams.size(), payload,
                    parsed.datagrams.back().receiveTime
                        - parsed.datagrams.front().receiveTime);
    }

    if (failures != 0) {
        std::fprintf(stderr, "%d corpus capture(s) failed\n", failures);
        return 1;
    }
    std::printf("mocopi packet corpus: %zu capture(s) verified\n",
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

    TestTheWrittenLayoutIsTheDocumentedOne();
    TestRoundTripIsByteIdentical();
    TestAGutterIsCheckedRatherThanSkipped();
    TestMalformedCapturesAreRefusedAndSayWhere();
    TestTheWriterSurvivesAHostileGlobalLocale();
    TestCommentsAndBlankLinesAreIgnored();
    std::puts("vrmAdapterMocopi packet capture tests passed");
    return 0;
}
