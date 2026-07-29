// SPDX-License-Identifier: Apache-2.0

#include "vrmAdapterVmc/Diagnostics.h"

#include <array>
#include <iomanip>
#include <ios>
#include <locale>
#include <sstream>

namespace vrmAdapterVmc
{

namespace
{

struct CodeEntry
{
    std::string_view name;
    DiagnosticSeverity severity;
    bool recoverable;
};

// One table, in enum order. Severity and recoverability live here rather than
// at each raise site so that two call sites cannot report the same code two
// ways -- which is the failure mode a code table exists to prevent.
constexpr std::array<CodeEntry, DiagnosticCodeCount> kCodes{{
    {"VRM_VMC_PACKET_MALFORMED", DiagnosticSeverity::Warning, true},
    {"VRM_VMC_UNSUPPORTED_MESSAGE", DiagnosticSeverity::Info, true},
    {"VRM_VMC_TIMESTAMP_REGRESSION", DiagnosticSeverity::Warning, true},
    {"VRM_VMC_DUPLICATE_BONE", DiagnosticSeverity::Warning, true},
    {"VRM_VMC_INCOMPLETE_FRAME", DiagnosticSeverity::Warning, true},
    {"VRM_VMC_SOURCE_RESTARTED", DiagnosticSeverity::Info, true},
    {"VRM_VMC_SOCKET_BIND_FAILED", DiagnosticSeverity::Error, false},
    {"VRM_VMC_STALE_JOINT", DiagnosticSeverity::Warning, true},
}};

const CodeEntry* Entry(DiagnosticCode code) noexcept
{
    const auto index = static_cast<std::size_t>(code);
    return index < kCodes.size() ? &kCodes[index] : nullptr;
}

// Six decimals, matching the recorded-trace format's quantum
// (motionRuntime/CaptureTrace.h), so a diagnostic line and the trace it refers
// to spell the same instant the same way.
//
// The classic locale is not decoration. `printf("%.6f")` and a default-imbued
// stream both take their decimal point from the *host's* locale, and a DCC that
// calls setlocale(LC_ALL, "") turns 1.500000 into 1,500000 — which would make a
// diagnostic and the trace it refers to disagree in exactly the environment
// where a live session is being debugged. CaptureTrace.cpp imbues the classic
// locale on both its reader and its writer for this reason; this matches it
// rather than inventing a second answer.
std::string FormatSeconds(double seconds)
{
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::fixed << std::setprecision(6) << seconds;
    return out.str();
}

} // namespace

std::string_view DiagnosticCodeString(DiagnosticCode code) noexcept
{
    const CodeEntry* entry = Entry(code);
    return entry ? entry->name : std::string_view();
}

std::optional<DiagnosticCode> FindDiagnosticCode(std::string_view name) noexcept
{
    for (std::size_t i = 0; i < kCodes.size(); ++i)
    {
        if (kCodes[i].name == name)
        {
            return static_cast<DiagnosticCode>(i);
        }
    }
    return std::nullopt;
}

DiagnosticSeverity DiagnosticDefaultSeverity(DiagnosticCode code) noexcept
{
    const CodeEntry* entry = Entry(code);
    return entry ? entry->severity : DiagnosticSeverity::Error;
}

bool DiagnosticIsRecoverable(DiagnosticCode code) noexcept
{
    const CodeEntry* entry = Entry(code);
    return entry ? entry->recoverable : false;
}

std::string_view DiagnosticSeverityString(
    DiagnosticSeverity severity) noexcept
{
    switch (severity)
    {
    case DiagnosticSeverity::Info:
        return "info";
    case DiagnosticSeverity::Warning:
        return "warning";
    case DiagnosticSeverity::Error:
        return "error";
    }
    return "error";
}

Diagnostic MakeDiagnostic(DiagnosticCode code, std::string detail)
{
    Diagnostic diagnostic;
    diagnostic.code = code;
    diagnostic.severity = DiagnosticDefaultSeverity(code);
    diagnostic.recoverable = DiagnosticIsRecoverable(code);
    diagnostic.detail = std::move(detail);
    return diagnostic;
}

std::string FormatDiagnostic(const Diagnostic& diagnostic)
{
    std::string line;
    line.reserve(128);

    line += '[';
    line += DiagnosticCodeString(diagnostic.code);
    line += "] ";
    line += DiagnosticSeverityString(diagnostic.severity);
    line += diagnostic.recoverable ? " recoverable" : " fatal";

    if (!diagnostic.source.empty())
    {
        line += " source=";
        line += diagnostic.source;
    }
    if (diagnostic.timestamp)
    {
        line += " t=";
        line += FormatSeconds(*diagnostic.timestamp);
    }
    if (!diagnostic.subject.empty())
    {
        line += " subject=";
        line += diagnostic.subject;
    }
    if (diagnostic.sequence)
    {
        line += " seq=";
        line += std::to_string(*diagnostic.sequence);
    }
    if (!diagnostic.detail.empty())
    {
        line += ": ";
        line += diagnostic.detail;
    }
    return line;
}

} // namespace vrmAdapterVmc
