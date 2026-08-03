// SPDX-License-Identifier: Apache-2.0
//
// The frozen diagnostic set: its strings, its two halves, and its formatting.
//
// The string table is checked against a list written out here rather than
// against the library's own table, because the two would otherwise be the same
// statement twice: a rename in `Diagnostics.cpp` that broke every downstream
// matcher would keep a test that read the table green.
#include "motionBvh/Diagnostics.h"

#include <cassert>
#include <cstdio>
#include <set>
#include <string>
#include <string_view>

namespace
{

using motionBvh::Diagnostic;
using motionBvh::DiagnosticCode;
using motionBvh::DiagnosticSeverity;

constexpr std::string_view kExpectedStrings[] = {
    "VRM_BVH_PARSE_FAILED",
    "VRM_BVH_UNSUPPORTED_CHANNEL",
    "VRM_BVH_FRAME_WIDTH_MISMATCH",
    "VRM_BVH_INVALID_FRAME_TIME",
    "VRM_BVH_NON_FINITE_VALUE",
    "VRM_BVH_PROFILE_REQUIRED",
    "VRM_BVH_PROFILE_MISMATCH",
    "VRM_BVH_UNMAPPED_JOINT",
    "VRM_BVH_REQUIRED_JOINT_MISSING",
    "VRM_BVH_INVALID_ROTATION_ORDER",
    "VRM_BVH_INVALID_ROOT_POLICY",
};

void
TestCodeStrings()
{
    static_assert(std::size(kExpectedStrings) == motionBvh::DiagnosticCodeCount,
                  "the set is frozen: a new code needs a contract change in "
                  "docs/roadmap/recorded-motion-sources.md §6 first");

    std::set<std::string_view> unique;
    for (std::size_t index = 0; index < motionBvh::DiagnosticCodeCount; ++index) {
        const auto code = static_cast<DiagnosticCode>(index);
        const std::string_view text = motionBvh::DiagnosticCodeString(code);
        assert(text == kExpectedStrings[index]);
        assert(text.rfind("VRM_BVH_", 0) == 0);
        assert(unique.insert(text).second);
        assert(motionBvh::FindDiagnosticCode(text) == code);
    }

    // Not a code, not this namespace's, and not an enumerator spelling.
    assert(!motionBvh::FindDiagnosticCode("VRM_VMC_PACKET_MALFORMED"));
    assert(!motionBvh::FindDiagnosticCode("ParseFailed"));
    assert(!motionBvh::FindDiagnosticCode(""));
    assert(motionBvh::DiagnosticCodeString(DiagnosticCode::Count).empty());
}

// The split is the layer boundary: a reader raises the first five and nothing
// else, because it does not know what a profile is.
void
TestSyntaxAndSemanticHalves()
{
    assert(motionBvh::SyntaxDiagnosticCodeCount == 5);
    assert(motionBvh::DiagnosticIsSyntax(DiagnosticCode::ParseFailed));
    assert(motionBvh::DiagnosticIsSyntax(DiagnosticCode::UnsupportedChannel));
    assert(motionBvh::DiagnosticIsSyntax(DiagnosticCode::FrameWidthMismatch));
    assert(motionBvh::DiagnosticIsSyntax(DiagnosticCode::InvalidFrameTime));
    assert(motionBvh::DiagnosticIsSyntax(DiagnosticCode::NonFiniteValue));

    assert(!motionBvh::DiagnosticIsSyntax(DiagnosticCode::ProfileRequired));
    assert(!motionBvh::DiagnosticIsSyntax(DiagnosticCode::ProfileMismatch));
    assert(!motionBvh::DiagnosticIsSyntax(DiagnosticCode::UnmappedJoint));
    assert(!motionBvh::DiagnosticIsSyntax(DiagnosticCode::RequiredJointMissing));
    assert(!motionBvh::DiagnosticIsSyntax(DiagnosticCode::InvalidRotationOrder));
    assert(!motionBvh::DiagnosticIsSyntax(DiagnosticCode::InvalidRootPolicy));
}

// One code continues, the rest stop. Nothing in the syntax half is recoverable:
// there is no half-read document to continue from.
void
TestSeverityAndRecoverability()
{
    for (std::size_t index = 0; index < motionBvh::DiagnosticCodeCount; ++index) {
        const auto code = static_cast<DiagnosticCode>(index);
        const bool isUnmapped = code == DiagnosticCode::UnmappedJoint;
        assert(motionBvh::DiagnosticIsRecoverable(code) == isUnmapped);
        assert(motionBvh::DiagnosticDefaultSeverity(code)
               == (isUnmapped ? DiagnosticSeverity::Warning
                              : DiagnosticSeverity::Error));
        if (motionBvh::DiagnosticIsSyntax(code)) {
            assert(!motionBvh::DiagnosticIsRecoverable(code));
        }
    }

    assert(motionBvh::DiagnosticSeverityString(DiagnosticSeverity::Info)
           == "info");
    assert(motionBvh::DiagnosticSeverityString(DiagnosticSeverity::Warning)
           == "warning");
    assert(motionBvh::DiagnosticSeverityString(DiagnosticSeverity::Error)
           == "error");
}

// MakeDiagnostic fills severity and recoverable from the code, so the two
// cannot silently disagree with the table.
void
TestMakeDiagnostic()
{
    const Diagnostic parse =
        motionBvh::MakeDiagnostic(DiagnosticCode::ParseFailed, "why");
    assert(parse.severity == DiagnosticSeverity::Error);
    assert(!parse.recoverable);
    assert(parse.detail == "why");
    assert(!parse.line);

    const Diagnostic unmapped =
        motionBvh::MakeDiagnostic(DiagnosticCode::UnmappedJoint);
    assert(unmapped.severity == DiagnosticSeverity::Warning);
    assert(unmapped.recoverable);
    assert(unmapped.detail.empty());
}

void
TestFormatting()
{
    Diagnostic diagnostic =
        motionBvh::MakeDiagnostic(DiagnosticCode::FrameWidthMismatch,
                                  "expected 57 values, read 54");
    diagnostic.source = "capture.bvh";
    diagnostic.line = 42;
    diagnostic.subject = "frame 3";
    assert(motionBvh::FormatDiagnostic(diagnostic)
           == "[VRM_BVH_FRAME_WIDTH_MISMATCH] error source=capture.bvh line=42 "
              "subject=frame 3: expected 57 values, read 54");

    // Absent optional fields are omitted rather than printed empty.
    const Diagnostic bare =
        motionBvh::MakeDiagnostic(DiagnosticCode::ProfileRequired);
    assert(motionBvh::FormatDiagnostic(bare)
           == "[VRM_BVH_PROFILE_REQUIRED] error");

    // `recoverable` is printed only when it is true -- the default is what
    // stops the read, and saying so on every line hides the one case that
    // does not.
    Diagnostic warning =
        motionBvh::MakeDiagnostic(DiagnosticCode::UnmappedJoint, "no mapping");
    warning.subject = "PropAnchor";
    assert(motionBvh::FormatDiagnostic(warning)
           == "[VRM_BVH_UNMAPPED_JOINT] warning recoverable subject=PropAnchor"
              ": no mapping");
}

} // namespace

int
main()
{
    TestCodeStrings();
    TestSyntaxAndSemanticHalves();
    TestSeverityAndRecoverability();
    TestMakeDiagnostic();
    TestFormatting();
    std::printf("motionBvh diagnostics: %zu code(s) verified\n",
                motionBvh::DiagnosticCodeCount);
    return 0;
}
