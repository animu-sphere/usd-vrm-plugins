// SPDX-License-Identifier: Apache-2.0
//
// The diagnostic vehicle, exercised against a code set this library does not
// have.
//
// The two adapters already test their own tables exhaustively — their code
// strings, severities, recoverability and formatted lines are frozen surfaces
// with tests written against the documents that froze them. This file tests
// what neither of them can: that the vehicle works for a code set it has never
// seen. The enum below is invented here, is not any adapter's, and is not
// contiguous with either — which is the point. A vehicle that only carries the
// two sets already in the tree is a vehicle that has been generalised on paper.
#include "liveTransport/Diagnostics.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>

namespace
{

using liveTransport::Diagnostic;
using liveTransport::DiagnosticCodeEntry;
using liveTransport::DiagnosticCodeTable;
using liveTransport::DiagnosticFields;
using liveTransport::DiagnosticSeverity;
using liveTransport::FormatDiagnostic;
using liveTransport::FormatSeconds;

enum class TestCode : std::uint8_t
{
    Bound,
    Silent,
    Refused,

    Count,
};

constexpr std::array<DiagnosticCodeEntry, 3> kTable{{
    {"TEST_BOUND", DiagnosticSeverity::Info, true},
    {"TEST_SILENT", DiagnosticSeverity::Warning, true},
    {"TEST_REFUSED", DiagnosticSeverity::Error, false},
}};

constexpr DiagnosticCodeTable<TestCode> kCodes{kTable.data(), kTable.size()};

void
TestTheTableReadsBothWays()
{
    assert(kCodes.Name(TestCode::Bound) == "TEST_BOUND");
    assert(kCodes.Name(TestCode::Silent) == "TEST_SILENT");
    assert(kCodes.Name(TestCode::Refused) == "TEST_REFUSED");

    assert(kCodes.Find("TEST_BOUND") == TestCode::Bound);
    assert(kCodes.Find("TEST_REFUSED") == TestCode::Refused);
    assert(!kCodes.Find("Bound"));
    assert(!kCodes.Find(""));
    assert(!kCodes.Find("TEST_BOUN"));

    assert(kCodes.Severity(TestCode::Bound) == DiagnosticSeverity::Info);
    assert(kCodes.Severity(TestCode::Refused) == DiagnosticSeverity::Error);
    assert(kCodes.Recoverable(TestCode::Silent));
    assert(!kCodes.Recoverable(TestCode::Refused));
}

// The one input a table cannot reject at compile time, and the reason the
// accessors take a bounds check rather than indexing: a value cast in from
// outside the enumerated range. Every accessor answers rather than reads past
// the end, and the answers are the conservative ones — no name, an error, not
// recoverable.
void
TestACodeOutsideTheTable()
{
    const auto past = TestCode::Count;
    assert(kCodes.Name(past).empty());
    assert(kCodes.Severity(past) == DiagnosticSeverity::Error);
    assert(!kCodes.Recoverable(past));

    const auto far = static_cast<TestCode>(200);
    assert(kCodes.Name(far).empty());
    assert(kCodes.Severity(far) == DiagnosticSeverity::Error);
    assert(!kCodes.Recoverable(far));
}

// `DefaultCode` is a template parameter rather than `Code{}` because the two
// adapters disagree about it and both are right: each defaults to its own
// `PacketMalformed`, which is enumerator 0 in one set and 6 in the other. A
// default-constructed diagnostic has to keep meaning what it meant.
void
TestTheDefaultCodeIsTheAdaptersChoiceAndNotZero()
{
    Diagnostic<TestCode, TestCode::Refused> refusedByDefault;
    assert(refusedByDefault.code == TestCode::Refused);

    Diagnostic<TestCode, TestCode::Bound> boundByDefault;
    assert(boundByDefault.code == TestCode::Bound);

    // The inherited half is defaulted the same way in both, so a caller that
    // fills only `code` still reports the conservative severity.
    assert(refusedByDefault.severity == DiagnosticSeverity::Error);
    assert(!refusedByDefault.recoverable);
    assert(!refusedByDefault.timestamp);
    assert(!refusedByDefault.sequence);
    assert(refusedByDefault.source.empty());
}

// The line is the contract: absent optional fields are omitted rather than
// printed empty, and the field order is fixed.
void
TestTheFormattedLine()
{
    Diagnostic<TestCode, TestCode::Bound> full;
    full.code = TestCode::Silent;
    full.severity = kCodes.Severity(full.code);
    full.recoverable = kCodes.Recoverable(full.code);
    full.source = "0.0.0.0:39539";
    full.timestamp = 1.5;
    full.subject = "leftHand";
    full.sequence = 42;
    full.detail = "no update for 0.5 s";

    assert(FormatDiagnostic(kCodes.Name(full.code), full)
           == "[TEST_SILENT] warning recoverable source=0.0.0.0:39539 "
              "t=1.500000 subject=leftHand seq=42: no update for 0.5 s");

    // Everything optional absent, and a fatal code.
    DiagnosticFields bare;
    bare.severity = DiagnosticSeverity::Error;
    bare.recoverable = false;
    assert(FormatDiagnostic("TEST_REFUSED", bare) == "[TEST_REFUSED] error fatal");

    // A sequence of 0 is a sequence, not an absent field; a timestamp of 0.0
    // likewise. This is what `std::optional` buys over a sentinel, and a
    // formatter that tested for truthiness would drop both.
    DiagnosticFields zeros;
    zeros.severity = DiagnosticSeverity::Info;
    zeros.recoverable = true;
    zeros.timestamp = 0.0;
    zeros.sequence = 0;
    assert(FormatDiagnostic("TEST_BOUND", zeros)
           == "[TEST_BOUND] info recoverable t=0.000000 seq=0");
}

// Six decimals, and the decimal point of the classic locale rather than the
// host's. A capture trace spells an instant the same way, so a diagnostic that
// took its separator from a DCC's `setlocale(LC_ALL, "")` would disagree with
// the trace it refers to in exactly the environment where a live session is
// being debugged.
void
TestSecondsAreSpelledOneWay()
{
    assert(FormatSeconds(0.0) == "0.000000");
    assert(FormatSeconds(1.5) == "1.500000");
    assert(FormatSeconds(-0.25) == "-0.250000");
    assert(FormatSeconds(1234.5) == "1234.500000");
    // Rounded to the quantum rather than truncated, and no exponent form.
    assert(FormatSeconds(0.0000004) == "0.000000");
    assert(FormatSeconds(1.0e7) == "10000000.000000");
}

void
TestSeverityStrings()
{
    assert(liveTransport::DiagnosticSeverityString(DiagnosticSeverity::Info)
           == "info");
    assert(liveTransport::DiagnosticSeverityString(DiagnosticSeverity::Warning)
           == "warning");
    assert(liveTransport::DiagnosticSeverityString(DiagnosticSeverity::Error)
           == "error");
}

} // namespace

int
main()
{
    TestTheTableReadsBothWays();
    TestACodeOutsideTheTable();
    TestTheDefaultCodeIsTheAdaptersChoiceAndNotZero();
    TestTheFormattedLine();
    TestSecondsAreSpelledOneWay();
    TestSeverityStrings();
    std::printf("liveTransport diagnostics: the vehicle carries a code set it "
                "has never seen\n");
    return 0;
}
