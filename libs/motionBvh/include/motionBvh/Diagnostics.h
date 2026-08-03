// SPDX-License-Identifier: Apache-2.0
//
// The BVH path's diagnostic namespace
// (roadmap/recorded-motion-sources.md §6).
//
// The set is frozen before the parser exists, on purpose and for the same
// reason the VMC adapter's eight were: a parser written first invents a code
// per failure it happens to hit, and the set stops describing the format and
// starts describing whichever malformed file was chased last. Every failure the
// BVH path can report is one of the eleven below.
//
// **The eleven split in two, and the split is the layer boundary.** The five
// syntax codes say the *bytes* were refused — a token, a channel name, a row
// width, a frame time, a number. The six semantic codes say a document and a
// producer profile disagreed, and nothing in this library's syntax layer may
// raise one: it does not know what a profile is. `DiagnosticIsSyntax` states
// that boundary in code, and `motionBvh_boundaries` checks that the parser's
// sources never name a semantic code.
//
// **A namespace that does not merge with the live one.** `VRM_BVH_*` is a
// recorded file's; `VRM_VMC_*` is a protocol's; `VRM_MOTION_*` is the canonical
// layer's and belongs to the motion libraries rather than to any reader. A
// dropped packet and a short motion row are not the same class of event, and a
// reader should not have to know which adapter it is being compared against.
//
// **The set is closed, and a failure it does not name is `VRM_BVH_PARSE_FAILED`
// with a precise `detail`.** A file declaring ten frames and carrying eight is
// the standing example: there is no frame-count code, and adding one the moment
// a parser meets that file is exactly the drift freezing the set prevents.
#pragma once

#include "motionBvh/api.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace motionBvh
{

// Values are stable array indices; append only before Count, and only with a
// contract review — this enum is the ordering the two halves above are read
// from.
enum class DiagnosticCode : std::uint8_t
{
    // --- syntax: what the bytes say -----------------------------------------

    // The file is not a decodable BVH document: a missing or misplaced keyword,
    // an unbalanced brace, a malformed token, a declared frame count the file
    // does not carry, or a limit refused.
    ParseFailed,
    // A `CHANNELS` list names a channel this format model cannot represent.
    // Refused rather than retained as an unknown column: see BvhDocument.h.
    UnsupportedChannel,
    // A motion row does not carry exactly one value per declared channel.
    FrameWidthMismatch,
    // `Frame Time:` is absent, not a number, negative, or zero where an
    // interval is required.
    InvalidFrameTime,
    // A parsed number is NaN or infinite. Not a rounding question: a rest offset
    // or a rotation that is not finite has no interpretation at any layer above.
    NonFiniteValue,

    // --- semantics: what a producer meant -----------------------------------
    // Raised where a document meets a profile, never by the parser.

    // A conversion was asked for with no profile named. There is no default
    // profile and no automatic fallback (roadmap §3.1).
    ProfileRequired,
    // The named profile does not describe this document: its required joints,
    // hierarchy, or channel pattern do not match.
    ProfileMismatch,
    // The document carries a joint the profile does not map. Whether that is
    // fatal is the profile's unmapped-joint policy to state.
    UnmappedJoint,
    // The profile requires a joint the document does not carry.
    RequiredJointMissing,
    // A joint's rotation channels cannot form an Euler order: repeated, missing,
    // or interleaved with position channels in a way no order describes.
    InvalidRotationOrder,
    // A profile names a root translation or rotation policy that is not one of
    // the defined ones.
    InvalidRootPolicy,

    Count,
};

inline constexpr std::size_t DiagnosticCodeCount =
    static_cast<std::size_t>(DiagnosticCode::Count);

// The syntax codes are the leading run, so a range check is the layer check.
inline constexpr std::size_t SyntaxDiagnosticCodeCount =
    static_cast<std::size_t>(DiagnosticCode::NonFiniteValue) + 1;

enum class DiagnosticSeverity : std::uint8_t
{
    Info,
    Warning,
    Error,
};

// The stable string, e.g. "VRM_BVH_FRAME_WIDTH_MISMATCH". This is the contract;
// the enumerator spelling is not.
MOTIONBVH_API std::string_view DiagnosticCodeString(
    DiagnosticCode code) noexcept;

MOTIONBVH_API std::optional<DiagnosticCode> FindDiagnosticCode(
    std::string_view name) noexcept;

MOTIONBVH_API DiagnosticSeverity DiagnosticDefaultSeverity(
    DiagnosticCode code) noexcept;

// Whether the code belongs to the syntax half — the only half a reader may
// raise. A caller pairing a document with a profile is on the other side of
// this boundary and may raise either.
MOTIONBVH_API bool DiagnosticIsSyntax(DiagnosticCode code) noexcept;

// Whether a conversion can continue past this code by default. Only
// `VRM_BVH_UNMAPPED_JOINT` can: a file carrying a joint no profile maps is the
// normal case for every producer that exports more than a humanoid, and a
// profile's unmapped-joint policy is what decides. Nothing in the syntax half
// is recoverable — a document is parsed whole or refused, so there is no
// half-read file for a caller to continue from.
MOTIONBVH_API bool DiagnosticIsRecoverable(DiagnosticCode code) noexcept;

MOTIONBVH_API std::string_view DiagnosticSeverityString(
    DiagnosticSeverity severity) noexcept;

// One reported diagnostic. Every optional field is optional because the layer
// that raises it genuinely may not have it: a missing `MOTION` section has no
// joint to name, and a profile mismatch has no line.
struct Diagnostic
{
    DiagnosticCode code = DiagnosticCode::ParseFailed;
    DiagnosticSeverity severity = DiagnosticSeverity::Error;
    bool recoverable = false;

    // Where the input came from — a file path, or a fixture's name when the
    // text was parsed from memory.
    std::string source;
    // 1-based, in the source text. Absent when the diagnostic is about the file
    // as a whole rather than a place in it.
    std::optional<std::size_t> line;
    // The joint name, channel token, or keyword the code is about. Plain text,
    // verbatim from the file: this layer resolves no target joint and renames
    // nothing.
    std::string subject;
    std::string detail;
};

// Fills `severity` and `recoverable` from the code's defaults, so the two
// cannot silently disagree with the table above.
MOTIONBVH_API Diagnostic MakeDiagnostic(
    DiagnosticCode code, std::string detail = {});

// A single deterministic line, stable enough for a golden test to compare:
//
//     [VRM_BVH_FRAME_WIDTH_MISMATCH] error source=capture.bvh line=42
//     subject=frame 3: expected 57 values, read 54
//
// Absent optional fields are omitted rather than printed empty, the field order
// is fixed, and `recoverable` is printed only when it is true — an error that
// stops the read is the default and saying so on every line hides the one case
// that does not.
MOTIONBVH_API std::string FormatDiagnostic(const Diagnostic& diagnostic);

} // namespace motionBvh
