// SPDX-License-Identifier: Apache-2.0
//
// The one part of the capture format no adapter's suite can own: who sent each
// datagram.
//
// Three suites already test this format, one per adapter, and each of them says
// in its own header that what it is really pinning is *its magic* — that a
// sibling's capture is refused at line 1. They test the grammar too, and that
// triplication is inherited rather than chosen; nothing here repeats it. What
// is here is the `p` line, which arrived after the extraction and belongs to no
// adapter: it names a transport identity, and a decoder may not read one
// (PacketCapture.h).
//
// The magic below is invented for this file and is not any adapter's, for the
// reason `test_diagnostics.cpp` invents a code set: a library test that only
// exercises the two tags already in the tree has been generalised on paper.
//
// **The claim these cases exist to hold is that a capture without peers is
// written exactly as it was before the `p` line existed.** Two corpora and
// sixty-odd committed fixtures depend on it, and the corpus round-trip tests
// would catch a break — after a rebuild of three adapters, in a failure that
// names a fixture rather than the rule. This says the rule.
#include "liveTransport/PacketCapture.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace
{

using liveTransport::PacketCapture;
using liveTransport::PacketCaptureError;
using liveTransport::RecordedDatagram;

// Not `!vmc-`, not `!mocopi-`, not `!vrchat-osc-`.
constexpr std::string_view kMagic = "!test-packet-capture";

RecordedDatagram
Datagram(double receiveTime, std::string peer)
{
    RecordedDatagram datagram;
    datagram.receiveTime = receiveTime;
    datagram.peer = std::move(peer);
    datagram.bytes = {0x2f, 0x74, 0x65, 0x73};
    return datagram;
}

std::string
Write(const PacketCapture& capture)
{
    std::ostringstream output;
    const bool ok = liveTransport::WritePacketCapture(kMagic, output, capture);
    assert(ok);
    (void)ok;
    return output.str();
}

bool
Read(const std::string& text, PacketCapture* capture,
     PacketCaptureError* error = nullptr)
{
    std::istringstream input(text);
    return liveTransport::ReadPacketCapture(kMagic, input, capture, error);
}

std::size_t
CountLines(const std::string& text, const std::string& prefix)
{
    std::size_t count = 0;
    std::istringstream input(text);
    std::string line;
    while (std::getline(input, line)) {
        if (line.compare(0, prefix.size(), prefix) == 0) {
            ++count;
        }
    }
    return count;
}

void
TestACaptureWithNoPeersIsWrittenAsItAlwaysWas()
{
    // The compatibility claim, as bytes. Every fixture committed before the `p`
    // line existed carries records with no peer, so this layout is what two
    // corpora are made of and it may not move.
    PacketCapture capture;
    capture.sender = "example.synthetic";
    capture.peerEndpoint = "192.168.0.20:52001";
    capture.datagrams.push_back(Datagram(0.0, {}));
    capture.datagrams.push_back(Datagram(0.020000, {}));

    const std::string expected =
        "!test-packet-capture 1\n"
        "sender example.synthetic\n"
        "peer 192.168.0.20:52001\n"
        "\n"
        "d 0.000000 4\n"
        "  2f 74 65 73" + std::string(38, ' ') + "|/tes|\n"
        "\n"
        "d 0.020000 4\n"
        "  2f 74 65 73" + std::string(38, ' ') + "|/tes|\n";

    const std::string written = Write(capture);
    if (written != expected) {
        std::fprintf(stderr, "written:\n%s\nexpected:\n%s\n", written.c_str(),
                     expected.c_str());
    }
    assert(written == expected);

    // And the header's peer is not a fallback: it describes the file, and a
    // record that names nobody still names nobody after a round trip.
    PacketCapture parsed;
    assert(Read(written, &parsed));
    assert(parsed.peerEndpoint == "192.168.0.20:52001");
    assert(parsed.datagrams[0].peer.empty());
    assert(parsed.datagrams[1].peer.empty());
}

void
TestOnePeerIsNamedOnceAndCarriedForward()
{
    PacketCapture capture;
    for (int index = 0; index < 4; ++index) {
        capture.datagrams.push_back(
            Datagram(index * 0.02, "192.168.1.8:51662"));
    }

    const std::string written = Write(capture);
    // Once, not four times. A 44 918-datagram session would otherwise carry
    // 44 918 copies of a string that never changed.
    assert(CountLines(written, "p ") == 1);
    assert(written.find("\np 192.168.1.8:51662\nd 0.000000 4\n")
           != std::string::npos);

    PacketCapture parsed;
    PacketCaptureError error;
    if (!Read(written, &parsed, &error)) {
        std::fprintf(stderr, "line %zu: %s\n", error.line,
                     error.message.c_str());
        assert(false);
    }
    assert(parsed.datagrams.size() == 4);
    for (const RecordedDatagram& datagram : parsed.datagrams) {
        assert(datagram.peer == "192.168.1.8:51662");
    }
    assert(Write(parsed) == written);
}

void
TestAChangeOfPeerIsTheRestartMarker()
{
    // The shape this line was added for, measured 2026-08-30: a mocopi VRChat
    // OSC session that stops and starts again resumes from a new ephemeral
    // source port, and that port is the only thing on that wire which says a
    // second session began rather than a first one paused.
    PacketCapture capture;
    capture.datagrams.push_back(Datagram(0.000000, "192.168.1.8:51662"));
    capture.datagrams.push_back(Datagram(0.017000, "192.168.1.8:51662"));
    capture.datagrams.push_back(Datagram(4.862000, "192.168.1.8:50035"));
    capture.datagrams.push_back(Datagram(4.879000, "192.168.1.8:50035"));

    const std::string written = Write(capture);
    assert(CountLines(written, "p ") == 2);

    PacketCapture parsed;
    assert(Read(written, &parsed));
    assert(parsed.datagrams.size() == 4);
    assert(parsed.datagrams[1].peer == "192.168.1.8:51662");
    assert(parsed.datagrams[2].peer == "192.168.1.8:50035");
    // The gap alone says something happened; the identity says what. Both are
    // readable from the file now, which is the whole of this change.
    assert(parsed.datagrams[2].receiveTime - parsed.datagrams[1].receiveTime
           > 4.0);
    assert(Write(parsed) == written);
}

void
TestAPeerCanGoAwayAndSaysSoExplicitly()
{
    // Known, then unknown. An omission cannot mean this once a peer has been
    // named — it would read as the previous peer continuing — so the format
    // spells it, and the round trip is what proves the spelling is complete.
    PacketCapture capture;
    capture.datagrams.push_back(Datagram(0.0, "192.168.1.8:51662"));
    capture.datagrams.push_back(Datagram(0.02, {}));
    capture.datagrams.push_back(Datagram(0.04, "192.168.1.8:51662"));

    const std::string written = Write(capture);
    assert(CountLines(written, "p ") == 3);
    assert(written.find("\np -\nd 0.020000 4\n") != std::string::npos);

    PacketCapture parsed;
    assert(Read(written, &parsed));
    assert(parsed.datagrams[0].peer == "192.168.1.8:51662");
    assert(parsed.datagrams[1].peer.empty());
    assert(parsed.datagrams[2].peer == "192.168.1.8:51662");
    assert(Write(parsed) == written);
}

void
TestARedundantPeerLineIsAcceptedAndCanonicalisedAway()
{
    // The uppercase-hex rule, applied to this line: a hand-authored fixture is
    // read and then written canonically, so a committed capture that repeats a
    // peer fails its corpus round trip rather than being refused at parse time
    // — which is the treatment every other cosmetic variation in this format
    // gets.
    const std::string text =
        "!test-packet-capture 1\n"
        "\n"
        "p 192.168.1.8:51662\n"
        "d 0.000000 1\n"
        "  2f  |/|\n"
        "\n"
        "p 192.168.1.8:51662\n"
        "d 0.020000 1\n"
        "  2f  |/|\n";

    PacketCapture parsed;
    PacketCaptureError error;
    if (!Read(text, &parsed, &error)) {
        std::fprintf(stderr, "line %zu: %s\n", error.line,
                     error.message.c_str());
        assert(false);
    }
    assert(parsed.datagrams.size() == 2);
    assert(parsed.datagrams[1].peer == "192.168.1.8:51662");
    assert(CountLines(Write(parsed), "p ") == 1);
}

struct BadCapture
{
    const char* name;
    std::string text;
    std::size_t line;
};

void
TestTheRefusalsThePLineInherits()
{
    const std::string header = "!test-packet-capture 1\n";
    const BadCapture cases[] = {
        {"a peer line with no peer", header + "p\n", 2},
        {"a peer line with two peers",
         header + "p 192.168.1.8:51662 192.168.1.8:50035\n", 2},
        // A record's bytes are contiguous. Without the explicit refusal this
        // reaches the hex reader and comes back as "'p' is not a two-digit hex
        // byte", which names the symptom and not the mistake.
        {"a peer line inside a record",
         header + "d 0.000000 4\n  2f 74\np 192.168.1.8:51662\n", 4},
        // The header describes the whole capture, and a `p` line is the record
        // stream beginning. A `sender` after one is exactly as late as a
        // `sender` after a datagram.
        {"a header key after a peer line",
         header + "p 192.168.1.8:51662\nsender late\nd 0.0 1\n  2f  |/|\n", 3},
        {"a peer line before the magic", "p 192.168.1.8:51662\n" + header, 1},
    };

    for (const BadCapture& testCase : cases) {
        PacketCapture capture;
        PacketCaptureError error;
        if (Read(testCase.text, &capture, &error)) {
            std::fprintf(stderr, "malformed capture was accepted: %s\n",
                         testCase.name);
            assert(false);
        }
        assert(!error.message.empty());
        if (error.line != testCase.line) {
            std::fprintf(stderr, "%s: reported line %zu, expected %zu (%s)\n",
                         testCase.name, error.line, testCase.line,
                         error.message.c_str());
            assert(false);
        }
        assert(capture.datagrams.empty());
    }
}

void
TestTheHeaderPeerAndTheRecordPeerAreDifferentStatements()
{
    // One describes the file and the other describes a datagram, and a capture
    // of a restart is exactly the case where they disagree: the header names
    // the first peer the recorder saw, and the records name every one of them.
    PacketCapture capture;
    capture.peerEndpoint = "192.168.1.8:51662";
    capture.datagrams.push_back(Datagram(0.0, "192.168.1.8:51662"));
    capture.datagrams.push_back(Datagram(5.0, "192.168.1.8:50035"));

    const std::string written = Write(capture);
    PacketCapture parsed;
    assert(Read(written, &parsed));
    assert(parsed.peerEndpoint == "192.168.1.8:51662");
    assert(parsed.datagrams[1].peer == "192.168.1.8:50035");
    assert(parsed.datagrams[1].peer != parsed.peerEndpoint);
}

} // namespace

int
main()
{
    TestACaptureWithNoPeersIsWrittenAsItAlwaysWas();
    TestOnePeerIsNamedOnceAndCarriedForward();
    TestAChangeOfPeerIsTheRestartMarker();
    TestAPeerCanGoAwayAndSaysSoExplicitly();
    TestARedundantPeerLineIsAcceptedAndCanonicalisedAway();
    TestTheRefusalsThePLineInherits();
    TestTheHeaderPeerAndTheRecordPeerAreDifferentStatements();
    std::printf("liveTransport packet capture: a record can say who sent it\n");
    return 0;
}
