// SPDX-License-Identifier: Apache-2.0

#include "liveTransport/Diagnostics.h"

#include <iomanip>
#include <ios>
#include <locale>
#include <sstream>

namespace liveTransport
{

std::string_view
DiagnosticSeverityString(DiagnosticSeverity severity) noexcept
{
    switch (severity) {
    case DiagnosticSeverity::Info:
        return "info";
    case DiagnosticSeverity::Warning:
        return "warning";
    case DiagnosticSeverity::Error:
        return "error";
    }
    return "error";
}

std::string
FormatSeconds(double seconds)
{
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::fixed << std::setprecision(6) << seconds;
    return out.str();
}

std::string
FormatDiagnostic(std::string_view codeString, const DiagnosticFields& fields)
{
    std::string line;
    line.reserve(128);

    line += '[';
    line += codeString;
    line += "] ";
    line += DiagnosticSeverityString(fields.severity);
    line += fields.recoverable ? " recoverable" : " fatal";

    if (!fields.source.empty()) {
        line += " source=";
        line += fields.source;
    }
    if (fields.timestamp) {
        line += " t=";
        line += FormatSeconds(*fields.timestamp);
    }
    if (!fields.subject.empty()) {
        line += " subject=";
        line += fields.subject;
    }
    if (fields.sequence) {
        line += " seq=";
        line += std::to_string(*fields.sequence);
    }
    if (!fields.detail.empty()) {
        line += ": ";
        line += fields.detail;
    }
    return line;
}

} // namespace liveTransport
