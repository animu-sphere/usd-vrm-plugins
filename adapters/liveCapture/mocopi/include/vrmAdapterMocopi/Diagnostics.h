// SPDX-License-Identifier: Apache-2.0
//
// The mocopi adapter's diagnostic namespace
// (roadmap/adapters-mocopi-vmc-ardy.md §8).
//
// These codes were frozen on 2026-08-03, before any decoder existed and before
// this adapter had a directory — which is the point rather than an accident of
// scheduling. A code list written after a decoder describes whichever failures
// were hit first; this one had to describe the *protocol's* failure modes with
// nothing to look at, and four of the nine were added in that freeze precisely
// so the set would cover the same ground the VMC adapter's does rather than a
// subset of it. Every failure the mocopi path can report is one of the nine
// below, and adding a tenth is a contract change and not a commit.
//
// Two namespaces meet here and must not merge. `VRM_MOCOPI_*` says *this*
// protocol layer refused something — a datagram, a joint, a frame boundary.
// `VRM_MOTION_*` says the canonical layer's contract was violated, and those
// codes belong to the motion libraries rather than to any adapter, so a reader
// can tell a decode failure from a motion-contract violation without knowing
// which adapter produced it. Nothing here is a core code, and no core code is
// emitted from here. The sibling adapter's `VRM_VMC_*` set is a third namespace
// again: one event, one spelling, per source.
//
// `recoverable` is load-bearing rather than decorative in a live session, and
// this adapter leans on it harder than its sibling does. A native path meets
// two states a relay cannot even express — a device that is not there yet and a
// device that is there and cannot solve — and both are ordinary things for a
// session to continue through.
#pragma once

#include "vrmAdapterMocopi/api.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace vrmAdapterMocopi
{

// Values are stable array indices; append only before Count.
//
// The order is the order §8 lists the codes in, read across its two columns —
// the same reading the sibling adapter's enum takes of its own block, so the
// document and either enum can be diffed line for line.
enum class DiagnosticCode : std::uint8_t
{
    // The receiver could not bind its listen address and port.
    SocketBindFailed,
    // The source reports that it cannot currently solve a joint, or the whole
    // body. This is the device telling the truth about itself, not a defect:
    // an occluded or detached sensor is a phenomenon a session recovers from.
    TrackingLost,
    // No packets are arriving from a source a session expected. The silence of
    // a device that has not been started and of one that has stopped is the
    // same silence, and this code covers both.
    DeviceUnavailable,
    // A frame's timestamp cannot be used: it is not finite, or it does not
    // advance in a way the session can order frames by.
    TimestampInvalid,
    // A joint the source reports that this adapter maps to no canonical
    // humanoid bone. Not a defect in the source.
    UnsupportedJoint,
    // The source restarted: its sequence or clock began again from the start.
    SourceRestarted,
    // The datagram is not a decodable packet, or a field does not match its
    // declared length or type.
    PacketMalformed,
    // A frame boundary arrived with part of the expected content missing.
    FrameIncomplete,
    // A decoded transform carries a non-finite component, or a rotation of
    // zero length. It names no orientation, and the value that would have to be
    // invented to carry on is exactly the identity a reader could not tell from
    // a real sample.
    NonFiniteTransform,

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

// The stable string, e.g. "VRM_MOCOPI_PACKET_MALFORMED". This is the contract;
// the enumerator spelling is not.
VRMADAPTERMOCOPI_API std::string_view DiagnosticCodeString(
    DiagnosticCode code) noexcept;

VRMADAPTERMOCOPI_API std::optional<DiagnosticCode> FindDiagnosticCode(
    std::string_view name) noexcept;

VRMADAPTERMOCOPI_API DiagnosticSeverity DiagnosticDefaultSeverity(
    DiagnosticCode code) noexcept;

// Whether a live session can continue past this code by default. A caller may
// still escalate — a flood of recoverable diagnostics is its own signal — but
// it never has to guess which class a code belongs to.
VRMADAPTERMOCOPI_API bool DiagnosticIsRecoverable(DiagnosticCode code) noexcept;

VRMADAPTERMOCOPI_API std::string_view DiagnosticSeverityString(
    DiagnosticSeverity severity) noexcept;

// One reported diagnostic. The fields are §8's list — code, severity, source,
// timestamp, joint or message name, packet sequence, a recoverable flag, and
// human-readable detail — which is one list for every adapter rather than one
// per adapter, so this struct has the same shape as the sibling's on purpose.
// Every optional field is optional because the layer that raises the diagnostic
// genuinely may not have it: a bind failure has no frame timestamp and no
// packet sequence.
struct Diagnostic
{
    DiagnosticCode code = DiagnosticCode::PacketMalformed;
    DiagnosticSeverity severity = DiagnosticSeverity::Error;
    bool recoverable = false;

    // Where the input came from — a source endpoint, or a recorded fixture's
    // name when the packets were replayed rather than received.
    std::string source;
    // Seconds in the source's own clock, when the diagnostic is tied to a
    // frame.
    std::optional<double> timestamp;
    // The joint name or the field the code is about, whichever applies. Plain
    // text: the adapter reports semantics and never resolves a target joint.
    std::string subject;
    std::optional<std::uint64_t> sequence;
    std::string detail;
};

// Fills `severity` and `recoverable` from the code's defaults, so the two
// cannot silently disagree with the table above.
VRMADAPTERMOCOPI_API Diagnostic MakeDiagnostic(
    DiagnosticCode code, std::string detail = {});

// A single deterministic line, stable enough for a golden test to compare:
//
//     [VRM_MOCOPI_TRACKING_LOST] warning recoverable source=0.0.0.0:12351
//     t=1.500000 subject=leftHand seq=42: the source stopped solving this joint
//
// Absent optional fields are omitted rather than printed empty, and the field
// order is fixed. The grammar is the sibling adapter's, deliberately: an
// operator reading a session log with both adapters in it should not have to
// learn a second line format to find out which one complained.
VRMADAPTERMOCOPI_API std::string FormatDiagnostic(const Diagnostic& diagnostic);

} // namespace vrmAdapterMocopi
