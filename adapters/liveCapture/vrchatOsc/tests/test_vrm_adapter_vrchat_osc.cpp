// SPDX-License-Identifier: Apache-2.0
//
// Scaffold-stage tests: the diagnostic table is a contract before it has any
// caller, so it is tested before it has any caller.
#include "vrmAdapterVrchatOsc/Diagnostics.h"

#include "liveTransport/PacketCapture.h"

#include <cassert>
#include <cstdio>
#include <locale>
#include <set>
#include <string>

namespace
{

using vrmAdapterVrchatOsc::Diagnostic;
using vrmAdapterVrchatOsc::DiagnosticCode;
using vrmAdapterVrchatOsc::DiagnosticCodeCount;
using vrmAdapterVrchatOsc::DiagnosticSeverity;

// The ten codes roadmap/osc-and-vrchat-trackers.md §8 assigns to this adapter,
// spelled exactly as that document spells them and in the order it lists them,
// read across its two columns. This list is the reason to have a test at all:
// the set was frozen before this directory existed, and a renamed, dropped or
// quietly added code is a contract break that nothing else in the tree would
// notice.
constexpr const char* kExpectedCodes[] = {
    "VRM_VRCHAT_OSC_PACKET_MALFORMED",
    "VRM_VRCHAT_OSC_UNSUPPORTED_ADDRESS",
    "VRM_VRCHAT_OSC_ARGUMENT_MISMATCH",
    "VRM_VRCHAT_OSC_TRACKER_ID_INVALID",
    "VRM_VRCHAT_OSC_TRACKER_PARTIAL",
    "VRM_VRCHAT_OSC_SOURCE_TIMEOUT",
    "VRM_VRCHAT_OSC_SOURCE_RESTARTED",
    "VRM_VRCHAT_OSC_COORDINATE_INVALID",
    "VRM_VRCHAT_OSC_SOCKET_BIND_FAILED",
    "VRM_VRCHAT_OSC_CALIBRATION_REQUIRED",
};

void
TestEveryCodeIsNamedOnceAndRoundTrips()
{
    constexpr std::size_t expected =
        sizeof(kExpectedCodes) / sizeof(kExpectedCodes[0]);
    assert(DiagnosticCodeCount == expected);

    std::set<std::string> seen;
    for (std::size_t i = 0; i < DiagnosticCodeCount; ++i) {
        const auto code = static_cast<DiagnosticCode>(i);
        const std::string name(vrmAdapterVrchatOsc::DiagnosticCodeString(code));

        assert(name == kExpectedCodes[i]);
        assert(seen.insert(name).second);

        const auto found = vrmAdapterVrchatOsc::FindDiagnosticCode(name);
        assert(found && *found == code);
    }

    assert(!vrmAdapterVrchatOsc::FindDiagnosticCode("VRM_VRCHAT_OSC_NOT_A_CODE"));
    // The canonical layer's namespace is not this adapter's to emit (§8).
    assert(!vrmAdapterVrchatOsc::FindDiagnosticCode(
        "VRM_MOTION_NON_FINITE_TRANSFORM"));
    // Neither is a sibling's, and this pair matters more here than the
    // equivalent assertion does in either sibling's suite: `vrmAdapterVmc`
    // decodes the *same wire format* one layer down, and §8's open question is
    // precisely which of these two namespaces a shared decoder will raise. Until
    // that is decided, the only thing keeping them apart is that neither answers
    // to the other's spelling.
    assert(!vrmAdapterVrchatOsc::FindDiagnosticCode("VRM_VMC_PACKET_MALFORMED"));
    assert(!vrmAdapterVrchatOsc::FindDiagnosticCode(
        "VRM_MOCOPI_PACKET_MALFORMED"));
}

void
TestOnlyABindFailureStopsTheSession()
{
    // The recoverable flag is what lets a caller distinguish a live session that
    // can continue from one that cannot, so exactly one code is fatal: a
    // receiver that never bound has nothing to recover into.
    //
    // Three of the other nine are worth stating as a test rather than as a
    // comment, because each is one somebody would reasonably make fatal. An
    // unsupported address is the ordinary case on a wire whose surface is much
    // larger than the subset read here. A source that has not started is the
    // ordinary state of a receiver bound before its sender. And calibration is
    // something a user performs while the stream runs, so a session that ended
    // on it would end exactly when it was about to become usable.
    for (std::size_t i = 0; i < DiagnosticCodeCount; ++i) {
        const auto code = static_cast<DiagnosticCode>(i);
        const bool fatal = code == DiagnosticCode::SocketBindFailed;
        assert(vrmAdapterVrchatOsc::DiagnosticIsRecoverable(code) == !fatal);
        assert((vrmAdapterVrchatOsc::DiagnosticDefaultSeverity(code)
                == DiagnosticSeverity::Error)
               == fatal);
    }

    assert(vrmAdapterVrchatOsc::DiagnosticIsRecoverable(
        DiagnosticCode::UnsupportedAddress));
    assert(vrmAdapterVrchatOsc::DiagnosticIsRecoverable(
        DiagnosticCode::SourceTimeout));
    assert(vrmAdapterVrchatOsc::DiagnosticIsRecoverable(
        DiagnosticCode::CalibrationRequired));

    // And the one code whose severity is neither of the obvious two: traffic
    // this adapter maps to nothing is information, because warning about it
    // would train an operator to ignore the warnings that mean something.
    assert(vrmAdapterVrchatOsc::DiagnosticDefaultSeverity(
               DiagnosticCode::UnsupportedAddress)
           == DiagnosticSeverity::Info);
}

void
TestADefaultConstructedDiagnosticMeansWhatItSays()
{
    // Zero is `PacketMalformed` in this set, in `vrmAdapterVmc`'s, and in
    // neither of those by accident -- it is 6 in `vrmAdapterMocopi`'s, which is
    // why the vehicle takes its default as a template parameter rather than
    // using `Code{}`. Pinned here so that reordering the enum turns this red
    // instead of silently changing what an unfilled diagnostic reports.
    const Diagnostic empty;
    assert(empty.code == DiagnosticCode::PacketMalformed);
}

void
TestMakeDiagnosticCannotDisagreeWithTheTable()
{
    const Diagnostic partial = vrmAdapterVrchatOsc::MakeDiagnostic(
        DiagnosticCode::TrackerPartial, "a rotation arrived with no position");
    assert(partial.severity == DiagnosticSeverity::Warning);
    assert(partial.recoverable);
    assert(partial.detail == "a rotation arrived with no position");
    assert(!partial.timestamp);
    assert(!partial.sequence);
}

void
TestFormattingIsDeterministicAndOmitsAbsentFields()
{
    Diagnostic full = vrmAdapterVrchatOsc::MakeDiagnostic(
        DiagnosticCode::TrackerPartial, "a rotation arrived with no position");
    // The default listen endpoint, which is the port a session is observed on
    // rather than anything this test binds.
    full.source = "0.0.0.0:9000";
    full.timestamp = 1.5;
    // An OSC address, not a bone name. This layer reports what the wire said,
    // and a humanoid name here would be a claim it has not earned
    // (Diagnostics.h).
    full.subject = "/tracking/trackers/4";
    full.sequence = 42;

    assert(vrmAdapterVrchatOsc::FormatDiagnostic(full)
           == "[VRM_VRCHAT_OSC_TRACKER_PARTIAL] warning recoverable"
              " source=0.0.0.0:9000 t=1.500000 subject=/tracking/trackers/4"
              " seq=42: a rotation arrived with no position");

    const Diagnostic bare =
        vrmAdapterVrchatOsc::MakeDiagnostic(DiagnosticCode::SocketBindFailed);
    assert(vrmAdapterVrchatOsc::FormatDiagnostic(bare)
           == "[VRM_VRCHAT_OSC_SOCKET_BIND_FAILED] error fatal");
}

// A locale whose decimal point is a comma, constructed in-process so this test
// depends on no system locale being installed anywhere.
struct CommaDecimalPoint : std::numpunct<char>
{
protected:
    char do_decimal_point() const override { return ','; }
};

void
TestFormattingSurvivesAHostileGlobalLocale()
{
    // A default-constructed ostringstream is imbued with the *global* locale, so
    // a host that installs one -- a DCC calling setlocale is the realistic case
    // -- would otherwise turn `t=1.500000` into `t=1,500000` and make a
    // diagnostic disagree with the capture it refers to.
    Diagnostic pinned = vrmAdapterVrchatOsc::MakeDiagnostic(
        DiagnosticCode::CoordinateInvalid);
    pinned.timestamp = 1.5;

    const std::locale previous = std::locale::global(
        std::locale(std::locale::classic(), new CommaDecimalPoint));
    const std::string formatted = vrmAdapterVrchatOsc::FormatDiagnostic(pinned);
    std::locale::global(previous);

    assert(formatted
           == "[VRM_VRCHAT_OSC_COORDINATE_INVALID] warning recoverable"
              " t=1.500000");
}

void
TestTheDeclaredDependencyEdgeIsReal()
{
    // The one edge this adapter's manifest declares -- and, at this milestone,
    // the only one it has -- exercised here, so the manifest cannot claim a
    // dependency the library does not actually have.
    //
    // The two it does *not* declare are the point of this test as much as the
    // one it does. WORKSPACE.md §2 permits `motionCore` and `motionRuntime`, and
    // VRC-0 produces no canonical value, so declaring them would be a claim
    // about a dependency that is not there. When a decoder arrives they will be
    // declared, and this test is where their arrival becomes visible.
    liveTransport::PacketCapture capture;
    capture.sourceId = "edge-01";
    // Field by field rather than braced: a `RecordedDatagram` grew a `peer`
    // between `receiveTime` and `bytes` on 2026-08-30, and the braced form
    // went on compiling with the payload read as a one-character peer.
    liveTransport::RecordedDatagram datagram;
    datagram.bytes = {0x2f};
    capture.datagrams.push_back(datagram);
    assert(capture.datagrams.size() == 1);
    assert(liveTransport::PacketCaptureGutter(capture.datagrams[0].bytes.data(),
                                              capture.datagrams[0].bytes.size())
           == "/");
}

} // namespace

int
main()
{
    TestEveryCodeIsNamedOnceAndRoundTrips();
    TestOnlyABindFailureStopsTheSession();
    TestADefaultConstructedDiagnosticMeansWhatItSays();
    TestMakeDiagnosticCannotDisagreeWithTheTable();
    TestFormattingIsDeterministicAndOmitsAbsentFields();
    TestFormattingSurvivesAHostileGlobalLocale();
    TestTheDeclaredDependencyEdgeIsReal();
    std::puts("vrmAdapterVrchatOsc unit tests passed");
    return 0;
}
