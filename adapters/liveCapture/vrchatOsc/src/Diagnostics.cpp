// SPDX-License-Identifier: Apache-2.0

#include "vrmAdapterVrchatOsc/Diagnostics.h"

#include <array>

namespace vrmAdapterVrchatOsc
{

namespace
{

using liveTransport::DiagnosticCodeEntry;

// One table, in enum order. Severity and recoverability live here rather than at
// each raise site so that two call sites cannot report the same code two ways --
// which is the failure mode a code table exists to prevent.
//
// The table is the whole of what this adapter contributes to the diagnostic
// ring. Its rows are this protocol's failure modes and nothing else's, which is
// exactly why a shared library may not hold one (liveTransport/Diagnostics.h).
//
// Exactly one code is fatal, and it is the same one both siblings make fatal: a
// receiver that never bound has nothing to recover into. Everything else a live
// session continues through, and three of them are worth saying out loud because
// a first reading makes each look fatal.
//
// An unsupported address is `Info`, not a warning, and this adapter is the one
// where that matters most. VRChat's OSC surface is far larger than the tracker
// subset read here -- avatar parameters, chatbox, input -- so a session
// carrying traffic this adapter maps to nothing is the *ordinary* case rather
// than a fault, and warning about it would train an operator to ignore the
// warnings that mean something.
//
// A source timeout is a warning a session continues through, for the reason the
// mocopi adapter states about its own: a receiver bound before the operator
// started the sender is the ordinary state of a session about to begin.
//
// `CalibrationRequired` is the one with no sibling precedent, and it is
// recoverable for a reason worth stating: calibration is something the *user*
// does, in the receiving application, while the stream is running. A session
// that ended on it would end exactly when an operator was about to fix it.
constexpr std::array<DiagnosticCodeEntry, DiagnosticCodeCount> kCodes{{
    {"VRM_VRCHAT_OSC_PACKET_MALFORMED", DiagnosticSeverity::Warning, true},
    {"VRM_VRCHAT_OSC_UNSUPPORTED_ADDRESS", DiagnosticSeverity::Info, true},
    {"VRM_VRCHAT_OSC_ARGUMENT_MISMATCH", DiagnosticSeverity::Warning, true},
    {"VRM_VRCHAT_OSC_TRACKER_ID_INVALID", DiagnosticSeverity::Warning, true},
    {"VRM_VRCHAT_OSC_TRACKER_PARTIAL", DiagnosticSeverity::Warning, true},
    {"VRM_VRCHAT_OSC_SOURCE_TIMEOUT", DiagnosticSeverity::Warning, true},
    {"VRM_VRCHAT_OSC_SOURCE_RESTARTED", DiagnosticSeverity::Info, true},
    {"VRM_VRCHAT_OSC_COORDINATE_INVALID", DiagnosticSeverity::Warning, true},
    {"VRM_VRCHAT_OSC_SOCKET_BIND_FAILED", DiagnosticSeverity::Error, false},
    {"VRM_VRCHAT_OSC_CALIBRATION_REQUIRED", DiagnosticSeverity::Warning, true},
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
    // the one argument the shared formatter cannot supply itself. The grammar is
    // both siblings', unavoidably now rather than by agreement -- which is an
    // improvement on agreement: an operator reading a session log with more than
    // one adapter in it does not have to learn a third line format to find out
    // which one complained.
    return liveTransport::FormatDiagnostic(
        DiagnosticCodeString(diagnostic.code), diagnostic);
}

} // namespace vrmAdapterVrchatOsc
