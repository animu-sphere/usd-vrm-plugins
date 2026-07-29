// SPDX-License-Identifier: Apache-2.0
//
// The VMC adapter's diagnostic namespace
// (roadmap/adapters-mocopi-vmc-ardy.md §8).
//
// These codes are the adapter's own and are frozen before the first decoder
// exists, on purpose: a decoder written first invents a code per failure it
// happens to hit, and the set stops describing the protocol and starts
// describing whichever bug was chased last. Every failure the VMC path can
// report is one of the eight below.
//
// Two namespaces meet here and must not merge. `VRM_VMC_*` says the *protocol*
// layer refused something — a datagram, an OSC type tag, a frame boundary.
// `VRM_MOTION_*` says the *canonical* layer's contract was violated, and those
// codes belong to the motion libraries rather than to any adapter, so that a
// reader can tell a decode failure from a motion-contract violation without
// knowing which adapter produced it. Nothing here is a core code, and no core
// code is emitted from here.
//
// `recoverable` is load-bearing rather than decorative in a live session: a
// dropped packet the receiver continues through must not be reported the same
// way as a socket that never bound.
#pragma once

#include "vrmAdapterVmc/api.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace vrmAdapterVmc
{

// Values are stable array indices; append only before Count.
enum class DiagnosticCode : std::uint8_t
{
    // The datagram is not a decodable OSC packet, or an OSC argument does not
    // match its type tag.
    PacketMalformed,
    // Well-formed OSC carrying an address pattern this adapter does not
    // implement. Not a defect in the sender.
    UnsupportedMessage,
    // A frame's timestamp is not greater than the previous accepted one,
    // without the sender having announced a restart.
    TimestampRegression,
    // One frame carried the same humanoid bone twice.
    DuplicateBone,
    // A frame boundary arrived with part of the expected content missing.
    IncompleteFrame,
    // The sender restarted: its sequence or clock began again from the start.
    SourceRestarted,
    // The receiver could not bind its listen address and port.
    SocketBindFailed,
    // A bone's last observed value is older than the staleness horizon, so it
    // is no longer being reported as current.
    StaleJoint,

    Count,
};

inline constexpr std::size_t DiagnosticCodeCount =
    static_cast<std::size_t>(DiagnosticCode::Count);

enum class DiagnosticSeverity : std::uint8_t
{
    Info,
    Warning,
    Error,
};

// The stable string, e.g. "VRM_VMC_PACKET_MALFORMED". This is the contract;
// the enumerator spelling is not.
VRMADAPTERVMC_API std::string_view DiagnosticCodeString(
    DiagnosticCode code) noexcept;

VRMADAPTERVMC_API std::optional<DiagnosticCode> FindDiagnosticCode(
    std::string_view name) noexcept;

VRMADAPTERVMC_API DiagnosticSeverity DiagnosticDefaultSeverity(
    DiagnosticCode code) noexcept;

// Whether a live session can continue past this code by default. A caller may
// still escalate — a flood of recoverable diagnostics is its own signal — but
// it never has to guess which class a code belongs to.
VRMADAPTERVMC_API bool DiagnosticIsRecoverable(DiagnosticCode code) noexcept;

VRMADAPTERVMC_API std::string_view DiagnosticSeverityString(
    DiagnosticSeverity severity) noexcept;

// One reported diagnostic. Every optional field is optional because the layer
// that raises the diagnostic genuinely may not have it: a bind failure has no
// frame timestamp and no packet sequence.
struct Diagnostic
{
    DiagnosticCode code = DiagnosticCode::PacketMalformed;
    DiagnosticSeverity severity = DiagnosticSeverity::Error;
    bool recoverable = false;

    // Where the input came from — a sender endpoint, or a recorded fixture's
    // name when the packets were replayed rather than received.
    std::string source;
    // Seconds in the source's own clock, when the diagnostic is tied to a
    // frame.
    std::optional<double> timestamp;
    // The humanoid bone name or the OSC address pattern, whichever the code is
    // about. Plain text; the adapter never resolves a target joint (§5.1).
    std::string subject;
    std::optional<std::uint64_t> sequence;
    std::string detail;
};

// Fills `severity` and `recoverable` from the code's defaults, so the two
// cannot silently disagree with the table above.
VRMADAPTERVMC_API Diagnostic MakeDiagnostic(
    DiagnosticCode code, std::string detail = {});

// A single deterministic line, stable enough for a golden test to compare:
//
//     [VRM_VMC_STALE_JOINT] warning recoverable source=127.0.0.1:39539
//     t=1.500000 subject=leftHand seq=42: no update for 0.5 s
//
// Absent optional fields are omitted rather than printed empty, and the field
// order is fixed.
VRMADAPTERVMC_API std::string FormatDiagnostic(const Diagnostic& diagnostic);

} // namespace vrmAdapterVmc
