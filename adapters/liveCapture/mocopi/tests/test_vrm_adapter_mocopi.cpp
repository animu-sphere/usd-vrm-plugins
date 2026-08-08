// SPDX-License-Identifier: Apache-2.0
//
// Scaffold-stage tests: the diagnostic table is a contract before it has any
// caller, so it is tested before it has any caller.
#include "vrmAdapterMocopi/Diagnostics.h"

#include "motionCore/Humanoid.h"
#include "motionRuntime/LiveCaptureSource.h"

#include <cassert>
#include <cstdio>
#include <locale>
#include <set>
#include <string>

namespace
{

using vrmAdapterMocopi::Diagnostic;
using vrmAdapterMocopi::DiagnosticCode;
using vrmAdapterMocopi::DiagnosticCodeCount;
using vrmAdapterMocopi::DiagnosticSeverity;

// The nine codes roadmap/adapters-mocopi-vmc-ardy.md §8 assigns to this
// adapter, spelled exactly as that document spells them and in the order it
// lists them. This list is the reason to have a test at all: the set was frozen
// two days before this directory existed, and a renamed, dropped or quietly
// added code is a contract break that nothing else in the tree would notice.
constexpr const char* kExpectedCodes[] = {
    "VRM_MOCOPI_SOCKET_BIND_FAILED",
    "VRM_MOCOPI_TRACKING_LOST",
    "VRM_MOCOPI_DEVICE_UNAVAILABLE",
    "VRM_MOCOPI_TIMESTAMP_INVALID",
    "VRM_MOCOPI_UNSUPPORTED_JOINT",
    "VRM_MOCOPI_SOURCE_RESTARTED",
    "VRM_MOCOPI_PACKET_MALFORMED",
    "VRM_MOCOPI_FRAME_INCOMPLETE",
    "VRM_MOCOPI_NON_FINITE_TRANSFORM",
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
        const std::string name(vrmAdapterMocopi::DiagnosticCodeString(code));

        assert(name == kExpectedCodes[i]);
        assert(seen.insert(name).second);

        const auto found = vrmAdapterMocopi::FindDiagnosticCode(name);
        assert(found && *found == code);
    }

    assert(!vrmAdapterMocopi::FindDiagnosticCode("VRM_MOCOPI_NOT_A_CODE"));
    // The canonical layer's namespace is not this adapter's to emit (§8).
    assert(!vrmAdapterMocopi::FindDiagnosticCode("VRM_MOTION_NON_FINITE_TRANSFORM"));
    // Neither is the other live adapter's, which matters more here than it
    // looks: this set and that one describe overlapping events on purpose, so
    // the only thing keeping them apart is that neither answers to the other's
    // spelling.
    assert(!vrmAdapterMocopi::FindDiagnosticCode("VRM_VMC_PACKET_MALFORMED"));
}

void
TestOnlyABindFailureStopsTheSession()
{
    // The recoverable flag is what lets a caller distinguish a live session
    // that can continue from one that cannot, so exactly one code is fatal:
    // a receiver that never bound has nothing to recover into.
    //
    // Two of the other eight are worth stating as a test rather than as a
    // comment, because a native path is where the temptation to make them fatal
    // appears. A device that is not there yet is the ordinary state of a
    // receiver bound before the operator started the application, and tracking
    // loss is the device reporting on itself accurately.
    for (std::size_t i = 0; i < DiagnosticCodeCount; ++i) {
        const auto code = static_cast<DiagnosticCode>(i);
        const bool fatal = code == DiagnosticCode::SocketBindFailed;
        assert(vrmAdapterMocopi::DiagnosticIsRecoverable(code) == !fatal);
        assert((vrmAdapterMocopi::DiagnosticDefaultSeverity(code)
                == DiagnosticSeverity::Error)
               == fatal);
    }

    assert(vrmAdapterMocopi::DiagnosticIsRecoverable(
        DiagnosticCode::DeviceUnavailable));
    assert(vrmAdapterMocopi::DiagnosticIsRecoverable(
        DiagnosticCode::TrackingLost));
}

void
TestMakeDiagnosticCannotDisagreeWithTheTable()
{
    const Diagnostic lost =
        vrmAdapterMocopi::MakeDiagnostic(DiagnosticCode::TrackingLost,
                                         "the source stopped solving this joint");
    assert(lost.severity == DiagnosticSeverity::Warning);
    assert(lost.recoverable);
    assert(lost.detail == "the source stopped solving this joint");
    assert(!lost.timestamp);
    assert(!lost.sequence);
}

void
TestFormattingIsDeterministicAndOmitsAbsentFields()
{
    Diagnostic full =
        vrmAdapterMocopi::MakeDiagnostic(DiagnosticCode::TrackingLost,
                                         "the source stopped solving this joint");
    // The default listen endpoint the source's own documentation states, which
    // is the port a session is observed on rather than anything this library
    // binds today.
    full.source = "0.0.0.0:12351";
    full.timestamp = 1.5;
    // A humanoid bone name, spelled the way motionCore spells it -- the adapter
    // reports semantics, never a target joint index.
    full.subject = std::string(motion::HumanBoneName(motion::HumanBone::LeftHand));
    full.sequence = 42;

    assert(vrmAdapterMocopi::FormatDiagnostic(full)
           == "[VRM_MOCOPI_TRACKING_LOST] warning recoverable"
              " source=0.0.0.0:12351 t=1.500000 subject=leftHand seq=42:"
              " the source stopped solving this joint");

    const Diagnostic bare =
        vrmAdapterMocopi::MakeDiagnostic(DiagnosticCode::SocketBindFailed);
    assert(vrmAdapterMocopi::FormatDiagnostic(bare)
           == "[VRM_MOCOPI_SOCKET_BIND_FAILED] error fatal");
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
    // A default-constructed ostringstream is imbued with the *global* locale,
    // so a host that installs one — a DCC calling setlocale is the realistic
    // case — would otherwise turn `t=1.500000` into `t=1,500000` and make a
    // diagnostic disagree with the capture trace it refers to.
    Diagnostic pinned =
        vrmAdapterMocopi::MakeDiagnostic(DiagnosticCode::TimestampInvalid);
    pinned.timestamp = 1.5;

    const std::locale previous = std::locale::global(
        std::locale(std::locale::classic(), new CommaDecimalPoint));
    const std::string formatted = vrmAdapterMocopi::FormatDiagnostic(pinned);
    std::locale::global(previous);

    assert(formatted
           == "[VRM_MOCOPI_TIMESTAMP_INVALID] warning recoverable t=1.500000");
}

void
TestTheDeclaredDependencyEdgesAreReal()
{
    // Both of the two edges this adapter's manifest declares -- and the only
    // two WORKSPACE.md §2 permits it -- are exercised here, so the manifest
    // cannot claim a dependency the library does not actually have.
    motion::LiveCaptureSource source;
    motion::HumanoidPose pose;
    pose.timestamp = 0.0;
    pose.validRotations.set(
        static_cast<std::size_t>(motion::HumanBone::Hips));

    assert(source.Push(pose));
    assert(!source.IsEmpty());
}

} // namespace

int
main()
{
    TestEveryCodeIsNamedOnceAndRoundTrips();
    TestOnlyABindFailureStopsTheSession();
    TestMakeDiagnosticCannotDisagreeWithTheTable();
    TestFormattingIsDeterministicAndOmitsAbsentFields();
    TestFormattingSurvivesAHostileGlobalLocale();
    TestTheDeclaredDependencyEdgesAreReal();
    std::puts("vrmAdapterMocopi unit tests passed");
    return 0;
}
