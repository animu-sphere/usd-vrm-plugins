// SPDX-License-Identifier: Apache-2.0

#include "vrmRetarget/Diagnostics.h"

#include <array>
#include <utility>

namespace vrmRetarget
{

namespace
{

struct CodeRow
{
    std::string_view name;
    RetargetDiagnosticSeverity severity;
    bool recoverable;
};

// Indexed by RetargetDiagnosticCode. The strings are the contract, so they are
// written out rather than derived from the enumerator spelling: a rename in the
// enum must not rename a code a downstream tool matches on. Severity and
// recoverability live here and nowhere else, so two raise sites cannot report
// one code two ways.
constexpr std::array<CodeRow, RetargetDiagnosticCodeCount> kCodes = {{
    {"VRM_RETARGET_MISSING_REQUIRED_BONE",
     RetargetDiagnosticSeverity::Warning, true},
    {"VRM_RETARGET_UNBOUND_DRIVEN_BONE",
     RetargetDiagnosticSeverity::Warning, true},
    {"VRM_RETARGET_DUPLICATE_TARGET",
     RetargetDiagnosticSeverity::Warning, true},
    {"VRM_RETARGET_INVALID_HIERARCHY",
     RetargetDiagnosticSeverity::Warning, true},
    {"VRM_RETARGET_INVALID_ROOT_JOINT",
     RetargetDiagnosticSeverity::Warning, true},
    {"VRM_RETARGET_NON_UNIT_SCALE",
     RetargetDiagnosticSeverity::Warning, true},
    // Info rather than a warning: a clip holding one pose is a legitimate clip,
    // and the code exists to say which instant the answer was placed at, not
    // that anything is wrong.
    {"VRM_RETARGET_TIME_RANGE_DERIVED",
     RetargetDiagnosticSeverity::Info, true},
    {"VRM_RETARGET_OUTPUT_COLLIDES_WITH_INPUT",
     RetargetDiagnosticSeverity::Error, false},
}};

const CodeRow*
Row(RetargetDiagnosticCode code) noexcept
{
    const auto index = static_cast<std::size_t>(code);
    return index < kCodes.size() ? &kCodes[index] : nullptr;
}

} // namespace

std::string_view
RetargetDiagnosticCodeString(RetargetDiagnosticCode code) noexcept
{
    const CodeRow* row = Row(code);
    return row ? row->name : std::string_view();
}

std::optional<RetargetDiagnosticCode>
FindRetargetDiagnosticCode(std::string_view name) noexcept
{
    for (std::size_t index = 0; index < kCodes.size(); ++index) {
        if (kCodes[index].name == name) {
            return static_cast<RetargetDiagnosticCode>(index);
        }
    }
    return std::nullopt;
}

RetargetDiagnosticSeverity
RetargetDiagnosticDefaultSeverity(RetargetDiagnosticCode code) noexcept
{
    const CodeRow* row = Row(code);
    return row ? row->severity : RetargetDiagnosticSeverity::Error;
}

bool
RetargetDiagnosticIsRecoverable(RetargetDiagnosticCode code) noexcept
{
    const CodeRow* row = Row(code);
    return row ? row->recoverable : false;
}

bool
RetargetDiagnosticIsLibraryRaised(RetargetDiagnosticCode code) noexcept
{
    return static_cast<std::size_t>(code) < LibraryRetargetDiagnosticCodeCount;
}

std::string_view
RetargetDiagnosticSeverityString(RetargetDiagnosticSeverity severity) noexcept
{
    switch (severity) {
    case RetargetDiagnosticSeverity::Info:
        return "info";
    case RetargetDiagnosticSeverity::Warning:
        return "warning";
    case RetargetDiagnosticSeverity::Error:
        return "error";
    }
    return "error";
}

bool
operator==(const RetargetDiagnostic& a, const RetargetDiagnostic& b) noexcept
{
    return a.code == b.code && a.severity == b.severity
        && a.recoverable == b.recoverable && a.subject == b.subject
        && a.detail == b.detail;
}

bool
operator!=(const RetargetDiagnostic& a, const RetargetDiagnostic& b) noexcept
{
    return !(a == b);
}

RetargetDiagnostic
MakeRetargetDiagnostic(RetargetDiagnosticCode code, std::string subject,
                       std::string detail)
{
    RetargetDiagnostic diagnostic;
    diagnostic.code = code;
    diagnostic.severity = RetargetDiagnosticDefaultSeverity(code);
    diagnostic.recoverable = RetargetDiagnosticIsRecoverable(code);
    diagnostic.subject = std::move(subject);
    diagnostic.detail = std::move(detail);
    return diagnostic;
}

std::string
FormatRetargetDiagnostic(const RetargetDiagnostic& diagnostic)
{
    std::string line;
    line += '[';
    line += RetargetDiagnosticCodeString(diagnostic.code);
    line += "] ";
    line += RetargetDiagnosticSeverityString(diagnostic.severity);
    if (diagnostic.recoverable) {
        line += " recoverable";
    }
    if (!diagnostic.subject.empty()) {
        line += " subject=";
        line += diagnostic.subject;
    }
    if (!diagnostic.detail.empty()) {
        line += ": ";
        line += diagnostic.detail;
    }
    return line;
}

bool
RetargetDiagnostics::Report(RetargetDiagnostic diagnostic)
{
    if (Has(diagnostic.code, diagnostic.subject)) {
        return false;
    }
    reported.push_back(std::move(diagnostic));
    return true;
}

void
RetargetDiagnostics::Merge(const RetargetDiagnostics& other)
{
    for (const RetargetDiagnostic& diagnostic : other.reported) {
        Report(diagnostic);
    }
}

bool
RetargetDiagnostics::Has(RetargetDiagnosticCode code,
                         std::string_view subject) const
{
    for (const RetargetDiagnostic& diagnostic : reported) {
        if (diagnostic.code == code && diagnostic.subject == subject) {
            return true;
        }
    }
    return false;
}

std::vector<std::string>
RetargetDiagnostics::Subjects(RetargetDiagnosticCode code) const
{
    std::vector<std::string> subjects;
    for (const RetargetDiagnostic& diagnostic : reported) {
        if (diagnostic.code == code) {
            subjects.push_back(diagnostic.subject);
        }
    }
    return subjects;
}

bool
operator==(const RetargetDiagnostics& a, const RetargetDiagnostics& b) noexcept
{
    return a.reported == b.reported;
}

bool
operator!=(const RetargetDiagnostics& a, const RetargetDiagnostics& b) noexcept
{
    return !(a == b);
}

} // namespace vrmRetarget
