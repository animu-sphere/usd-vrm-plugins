// SPDX-License-Identifier: Apache-2.0

#include "vrmAdapterMocopi/Diagnostics.h"

#include <array>

namespace vrmAdapterMocopi
{

namespace
{

using liveTransport::DiagnosticCodeEntry;

// One table, in enum order. Severity and recoverability live here rather than
// at each raise site so that two call sites cannot report the same code two
// ways -- which is the failure mode a code table exists to prevent.
//
// The table is what stayed in this adapter when everything around it moved. Its
// rows are this protocol's failure modes and nothing else's, which is exactly
// why a shared library may not hold one (liveTransport/Diagnostics.h).
//
// Exactly one code is fatal, and it is the same one the sibling adapter makes
// fatal: a receiver that never bound has nothing to recover into. Everything
// else a live session continues through, and two of them are worth saying out
// loud because a first reading makes both look fatal. A device that is not
// there yet is the ordinary state of a receiver bound before the operator
// started the application on the phone, so a session that died on it would be
// unusable. And tracking loss is the device reporting on itself accurately --
// it is warned about rather than errored on for the same reason the sibling
// warns rather than errors when a bone goes stale.
constexpr std::array<DiagnosticCodeEntry, DiagnosticCodeCount> kCodes{{
    {"VRM_MOCOPI_SOCKET_BIND_FAILED", DiagnosticSeverity::Error, false},
    {"VRM_MOCOPI_TRACKING_LOST", DiagnosticSeverity::Warning, true},
    {"VRM_MOCOPI_DEVICE_UNAVAILABLE", DiagnosticSeverity::Warning, true},
    {"VRM_MOCOPI_TIMESTAMP_INVALID", DiagnosticSeverity::Warning, true},
    {"VRM_MOCOPI_UNSUPPORTED_JOINT", DiagnosticSeverity::Info, true},
    {"VRM_MOCOPI_SOURCE_RESTARTED", DiagnosticSeverity::Info, true},
    {"VRM_MOCOPI_PACKET_MALFORMED", DiagnosticSeverity::Warning, true},
    {"VRM_MOCOPI_FRAME_INCOMPLETE", DiagnosticSeverity::Warning, true},
    {"VRM_MOCOPI_NON_FINITE_TRANSFORM", DiagnosticSeverity::Warning, true},
}};

constexpr liveTransport::DiagnosticCodeTable<DiagnosticCode> kTable{
    kCodes.data(), kCodes.size()};

} // namespace

std::string_view
DiagnosticCodeString(DiagnosticCode code) noexcept
{
    return kTable.Name(code);
}

std::optional<DiagnosticCode>
FindDiagnosticCode(std::string_view name) noexcept
{
    return kTable.Find(name);
}

DiagnosticSeverity
DiagnosticDefaultSeverity(DiagnosticCode code) noexcept
{
    return kTable.Severity(code);
}

bool
DiagnosticIsRecoverable(DiagnosticCode code) noexcept
{
    return kTable.Recoverable(code);
}

Diagnostic
MakeDiagnostic(DiagnosticCode code, std::string detail)
{
    Diagnostic diagnostic;
    diagnostic.code = code;
    diagnostic.severity = DiagnosticDefaultSeverity(code);
    diagnostic.recoverable = DiagnosticIsRecoverable(code);
    diagnostic.detail = std::move(detail);
    return diagnostic;
}

std::string
FormatDiagnostic(const Diagnostic& diagnostic)
{
    // Resolving the code is the one step only this adapter can take, so it is
    // the one argument the shared formatter cannot supply itself. The grammar
    // is the sibling adapter's, deliberately -- and now unavoidably, which is
    // an improvement on "deliberately": an operator reading a session log with
    // both adapters in it does not have to learn a second line format to find
    // out which one complained.
    return liveTransport::FormatDiagnostic(
        DiagnosticCodeString(diagnostic.code), diagnostic);
}

} // namespace vrmAdapterMocopi
