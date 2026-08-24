// SPDX-License-Identifier: Apache-2.0

#include "vrmAdapterVmc/Diagnostics.h"

#include <array>

namespace vrmAdapterVmc
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
constexpr std::array<DiagnosticCodeEntry, DiagnosticCodeCount> kCodes{{
    {"VRM_VMC_PACKET_MALFORMED", DiagnosticSeverity::Warning, true},
    {"VRM_VMC_UNSUPPORTED_MESSAGE", DiagnosticSeverity::Info, true},
    {"VRM_VMC_TIMESTAMP_REGRESSION", DiagnosticSeverity::Warning, true},
    {"VRM_VMC_DUPLICATE_BONE", DiagnosticSeverity::Warning, true},
    {"VRM_VMC_INCOMPLETE_FRAME", DiagnosticSeverity::Warning, true},
    {"VRM_VMC_SOURCE_RESTARTED", DiagnosticSeverity::Info, true},
    {"VRM_VMC_SOCKET_BIND_FAILED", DiagnosticSeverity::Error, false},
    {"VRM_VMC_STALE_JOINT", DiagnosticSeverity::Warning, true},
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
    // the one argument the shared formatter cannot supply itself.
    return liveTransport::FormatDiagnostic(DiagnosticCodeString(diagnostic.code),
                                           diagnostic);
}

} // namespace vrmAdapterVmc
