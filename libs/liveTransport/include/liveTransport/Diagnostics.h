// SPDX-License-Identifier: Apache-2.0
//
// The diagnostic **vehicle** every live adapter reports through — and not one
// diagnostic code, which is the whole of the split WORKSPACE.md §2 draws here.
//
//     the vehicle  = this library   (the struct, the severity scale, the line)
//     the code set = the adapter    (frozen per protocol, before its decoder)
//
// Two adapters wrote the same 126 lines twice and differed only in their code
// table (roadmap/osc-and-vrchat-trackers.md §2). Everything the two copies
// agreed on is below; everything they disagreed on stayed where it was.
//
// ## Why a code cannot live here, stated as a mechanism rather than a rule
//
// A code set is frozen *before* the decoder that raises it, so that the set
// describes a protocol's failure modes rather than whichever bug was chased
// last (adapter plan §8). That freeze is per protocol by construction: mocopi
// can report a device that cannot solve, VMC cannot express one, and a shared
// enum would have to contain both and mean neither. So a code is an adapter's
// property, `Diagnostic::code` keeps its adapter's enum *type*, and this
// library never names a code — a `liveTransport` holding one is a contract
// violation, not a shortcut (WORKSPACE.md §2).
//
// What it does hold is the machinery a code table needs, so that the table is
// the only thing an adapter writes: `DiagnosticCodeTable` turns an array of
// rows into the four accessors both adapters had written out by hand.
#pragma once

#include "liveTransport/api.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace liveTransport
{

enum class DiagnosticSeverity : std::uint8_t
{
    Info,
    Warning,
    Error,
};

LIVETRANSPORT_API std::string_view DiagnosticSeverityString(
    DiagnosticSeverity severity) noexcept;

// One row of an adapter's frozen code table: the stable string, and the two
// defaults that must not be decided at a raise site. `name` is the contract —
// the enumerator spelling is not — and it points at a string literal the
// adapter owns, which is why this is a view and never a copy.
struct DiagnosticCodeEntry
{
    std::string_view name;
    DiagnosticSeverity severity;
    bool recoverable;
};

// The fields of a diagnostic that are not its code.
//
// Split out as a base rather than repeated in the template below so that the
// formatter can be an ordinary function: the line does not depend on which
// adapter's enum produced it, and a formatter instantiated per adapter would
// be the duplication this library exists to end, moved one layer down.
//
// Every optional field is optional because the layer that raises the
// diagnostic genuinely may not have it: a bind failure has no frame timestamp
// and no packet sequence.
struct DiagnosticFields
{
    DiagnosticSeverity severity = DiagnosticSeverity::Error;
    bool recoverable = false;

    // Where the input came from — a sender endpoint, or a recorded fixture's
    // name when the packets were replayed rather than received.
    std::string source;
    // Seconds in the source's own clock, when the diagnostic is tied to a
    // frame.
    std::optional<double> timestamp;
    // The subject the code is about — a joint name, an address pattern, an
    // endpoint. Plain text; an adapter never resolves a target joint.
    std::string subject;
    std::optional<std::uint64_t> sequence;
    std::string detail;
};

// One reported diagnostic, carrying the adapter's own code by value.
//
// `DefaultCode` is a parameter rather than `Code{}` because the two existing
// adapters disagree about it and both are right: each defaults to its own
// `PacketMalformed`, which is enumerator 0 in one set and 6 in the other. A
// default-constructed diagnostic must keep meaning what it meant.
template <class Code, Code DefaultCode>
struct Diagnostic : DiagnosticFields
{
    Code code = DefaultCode;
};

// A single deterministic line, stable enough for a golden test to compare:
//
//     [VRM_VMC_STALE_JOINT] warning recoverable source=127.0.0.1:39539
//     t=1.500000 subject=leftHand seq=42: no update for 0.5 s
//
// Absent optional fields are omitted rather than printed empty, and the field
// order is fixed. The code arrives already resolved to its string, because
// resolving it is the one step only the adapter can take.
LIVETRANSPORT_API std::string FormatDiagnostic(std::string_view codeString,
                                               const DiagnosticFields& fields);

// Six decimals in the classic locale, matching the recorded-trace format's
// quantum (motionRuntime/CaptureTrace.h), so a diagnostic line and the trace it
// refers to spell the same instant the same way.
//
// The classic locale is not decoration. `printf("%.6f")` and a default-imbued
// stream both take their decimal point from the *host's* locale, and a DCC that
// calls setlocale(LC_ALL, "") turns 1.500000 into 1,500000 — which would make a
// diagnostic and the trace it refers to disagree in exactly the environment
// where a live session is being debugged. CaptureTrace.cpp imbues the classic
// locale on both its reader and its writer for this reason; this matches it
// rather than inventing a second answer.
LIVETRANSPORT_API std::string FormatSeconds(double seconds);

// An adapter's code table, read.
//
// One table, in enum order, with severity and recoverability in it rather than
// at each raise site — so that two call sites cannot report the same code two
// ways, which is the failure mode a code table exists to prevent. The table
// itself stays in the adapter; this is only how it is looked up.
//
// Constructed from a pointer and a count rather than templated on the array's
// size, so that the type is one type per adapter and not one per table length.
template <class Code>
class DiagnosticCodeTable final
{
public:
    constexpr DiagnosticCodeTable(const DiagnosticCodeEntry* entries,
                                  std::size_t count) noexcept
        : _entries(entries)
        , _count(count)
    {
    }

    // Empty for a code outside the table, which is what an out-of-range cast
    // produces and the one input this cannot reject at compile time.
    std::string_view Name(Code code) const noexcept
    {
        const DiagnosticCodeEntry* entry = _Entry(code);
        return entry ? entry->name : std::string_view();
    }

    std::optional<Code> Find(std::string_view name) const noexcept
    {
        for (std::size_t i = 0; i < _count; ++i) {
            if (_entries[i].name == name) {
                return static_cast<Code>(i);
            }
        }
        return std::nullopt;
    }

    DiagnosticSeverity Severity(Code code) const noexcept
    {
        const DiagnosticCodeEntry* entry = _Entry(code);
        return entry ? entry->severity : DiagnosticSeverity::Error;
    }

    bool Recoverable(Code code) const noexcept
    {
        const DiagnosticCodeEntry* entry = _Entry(code);
        return entry ? entry->recoverable : false;
    }

private:
    const DiagnosticCodeEntry* _Entry(Code code) const noexcept
    {
        const auto index = static_cast<std::size_t>(code);
        return index < _count ? &_entries[index] : nullptr;
    }

    const DiagnosticCodeEntry* _entries;
    std::size_t _count;
};

} // namespace liveTransport
