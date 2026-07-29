// SPDX-License-Identifier: Apache-2.0
//
// Scaffold-stage tests: the diagnostic table is a contract before it has any
// caller, so it is tested before it has any caller.
#include "vrmAdapterVmc/Diagnostics.h"

#include "motionCore/Humanoid.h"
#include "motionRuntime/LiveCaptureSource.h"

#include <cassert>
#include <cstdio>
#include <set>
#include <string>

namespace
{

using vrmAdapterVmc::Diagnostic;
using vrmAdapterVmc::DiagnosticCode;
using vrmAdapterVmc::DiagnosticCodeCount;
using vrmAdapterVmc::DiagnosticSeverity;

// The eight codes roadmap/adapters-mocopi-vmc-ardy.md §8 assigns to this
// adapter, spelled exactly as that document spells them. This list is the
// reason to have a test at all: a renamed or dropped code is a contract break
// that nothing else in the tree would notice.
constexpr const char* kExpectedCodes[] = {
    "VRM_VMC_PACKET_MALFORMED",
    "VRM_VMC_UNSUPPORTED_MESSAGE",
    "VRM_VMC_TIMESTAMP_REGRESSION",
    "VRM_VMC_DUPLICATE_BONE",
    "VRM_VMC_INCOMPLETE_FRAME",
    "VRM_VMC_SOURCE_RESTARTED",
    "VRM_VMC_SOCKET_BIND_FAILED",
    "VRM_VMC_STALE_JOINT",
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
        const std::string name(vrmAdapterVmc::DiagnosticCodeString(code));

        assert(name == kExpectedCodes[i]);
        assert(seen.insert(name).second);

        const auto found = vrmAdapterVmc::FindDiagnosticCode(name);
        assert(found && *found == code);
    }

    assert(!vrmAdapterVmc::FindDiagnosticCode("VRM_VMC_NOT_A_CODE"));
    // The canonical layer's namespace is not this adapter's to emit (§8).
    assert(!vrmAdapterVmc::FindDiagnosticCode("VRM_MOTION_SAMPLE_STALE"));
}

void
TestOnlyABindFailureStopsTheSession()
{
    // The recoverable flag is what lets a caller distinguish a live session
    // that can continue from one that cannot, so exactly one code is fatal:
    // a receiver that never bound has nothing to recover into.
    for (std::size_t i = 0; i < DiagnosticCodeCount; ++i) {
        const auto code = static_cast<DiagnosticCode>(i);
        const bool fatal = code == DiagnosticCode::SocketBindFailed;
        assert(vrmAdapterVmc::DiagnosticIsRecoverable(code) == !fatal);
        assert((vrmAdapterVmc::DiagnosticDefaultSeverity(code)
                == DiagnosticSeverity::Error)
               == fatal);
    }
}

void
TestMakeDiagnosticCannotDisagreeWithTheTable()
{
    const Diagnostic stale =
        vrmAdapterVmc::MakeDiagnostic(DiagnosticCode::StaleJoint,
                                      "no update for 0.5 s");
    assert(stale.severity == DiagnosticSeverity::Warning);
    assert(stale.recoverable);
    assert(stale.detail == "no update for 0.5 s");
    assert(!stale.timestamp);
    assert(!stale.sequence);
}

void
TestFormattingIsDeterministicAndOmitsAbsentFields()
{
    Diagnostic full =
        vrmAdapterVmc::MakeDiagnostic(DiagnosticCode::StaleJoint,
                                      "no update for 0.5 s");
    full.source = "127.0.0.1:39539";
    full.timestamp = 1.5;
    // A humanoid bone name, spelled the way motionCore spells it -- the adapter
    // reports semantics, never a target joint index (§5.1).
    full.subject = std::string(motion::HumanBoneName(motion::HumanBone::LeftHand));
    full.sequence = 42;

    assert(vrmAdapterVmc::FormatDiagnostic(full)
           == "[VRM_VMC_STALE_JOINT] warning recoverable source=127.0.0.1:39539"
              " t=1.500000 subject=leftHand seq=42: no update for 0.5 s");

    const Diagnostic bare =
        vrmAdapterVmc::MakeDiagnostic(DiagnosticCode::SocketBindFailed);
    assert(vrmAdapterVmc::FormatDiagnostic(bare)
           == "[VRM_VMC_SOCKET_BIND_FAILED] error fatal");
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
    TestTheDeclaredDependencyEdgesAreReal();
    std::puts("vrmAdapterVmc unit tests passed");
    return 0;
}
