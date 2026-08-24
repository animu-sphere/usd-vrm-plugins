// SPDX-License-Identifier: Apache-2.0
//
// The VRChat OSC adapter's diagnostic namespace
// (roadmap/osc-and-vrchat-trackers.md §8).
//
// These ten codes were frozen in that document before this directory existed
// and before anything here decodes a byte, as both siblings' sets were. The
// reason is the same one, and it is worth restating for a protocol whose
// receiving end is *published*: a code list written after a decoder describes
// whichever failures the first session happened to hit, and a published
// specification makes that trap worse rather than better — the first session is
// the one that makes a subset of the specification look like the whole of it.
// Every failure this path can report is one of the ten below, and adding an
// eleventh is a contract change and not a commit.
//
// ## What this set says that neither sibling's can
//
// Four of the ten are about a *tracker*, and they exist because a tracker
// observation is not a pose (§5). `VRM_VMC_*` and `VRM_MOCOPI_*` can say a bone
// is missing or stale; neither can say that tracker 4 sent a rotation and no
// position, because in those protocols a bone carries its whole transform or is
// absent. Here the two arrive on separate addresses and either can be missing on
// its own, so `TRACKER_PARTIAL` is a state this wire has and those wires do not.
//
// `CALIBRATION_REQUIRED` is the other one worth reading twice. VRChat's tracking
// space is the *player's*, established by a calibration the receiving
// application performs; a stream that has not been calibrated is well-formed and
// unusable, which is a distinct thing from a malformed packet and from a missing
// tracker.
//
// `VRM_VRCHAT_OSC_SOCKET_BIND_FAILED` is here because both siblings needed one
// and a set that omits it describes a decoder rather than a live adapter — the
// same correction the `VRM_MOCOPI_*` set took on 2026-08-03, made in advance
// this time.
//
// Three namespaces meet here and must not merge. `VRM_VRCHAT_OSC_*` says this
// protocol layer refused something. `VRM_MOTION_*` says the canonical layer's
// contract was violated, and belongs to the motion libraries rather than to any
// adapter. `VRM_VMC_*` is a sibling's and is not this adapter's to raise — a
// distinction that costs something real here rather than being a formality,
// since that adapter and this one speak the *same* wire format one layer down,
// and the shared decoder that will serve both has not yet decided whose codes it
// raises (§8's open question, deferred to OSC-3 on purpose).
//
// ## What is this adapter's, and what is `liveTransport`'s
//
// The code set, and only the code set (WORKSPACE.md §2). The **vehicle** — the
// `Diagnostic` struct, the severity scale, the code table's lookups and the
// formatted line — is shared, because none of it is a statement about this
// protocol. That split is why this file is mostly prose and about forty lines of
// declarations.
#pragma once

#include "vrmAdapterVrchatOsc/api.h"

#include "liveTransport/Diagnostics.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace vrmAdapterVrchatOsc
{

// Values are stable array indices; append only before Count.
//
// The order is the order §8 lists the codes in, read across its two columns —
// the same reading both siblings' enums take of their own blocks, so the
// document and any of the three enums can be diffed line for line.
enum class DiagnosticCode : std::uint8_t
{
    // The datagram is not a decodable OSC packet: a message with no address, a
    // declared length that does not agree with the payload, or padding that is
    // not there.
    PacketMalformed,
    // A well-formed OSC address this adapter maps to nothing. Not a defect in
    // the sender: VRChat's OSC surface is far larger than the tracker subset
    // this adapter reads, and a session carrying avatar parameters alongside
    // tracker data is the ordinary case rather than a fault.
    UnsupportedAddress,
    // The address is one this adapter knows and its arguments are not: the
    // wrong count, or a type tag naming a type that address does not take.
    ArgumentMismatch,
    // A tracker index outside the range the surface defines, or one that cannot
    // be read out of the address at all.
    TrackerIdInvalid,
    // A tracker reported part of itself: a position with no rotation, or the
    // reverse, within the window a frame is assembled over.
    TrackerPartial,
    // No packets are arriving from a source a session expected. The silence of
    // a sender that has not been started and of one that has stopped is the
    // same silence, and this code covers both.
    SourceTimeout,
    // The source restarted: its stream began again from the start.
    SourceRestarted,
    // A coordinate cannot be used: a non-finite component, or a rotation of zero
    // length. It names no orientation, and the value that would have to be
    // invented to carry on is exactly the identity a reader could not tell from
    // a real sample.
    CoordinateInvalid,
    // The receiver could not bind its listen address and port.
    SocketBindFailed,
    // The stream is well-formed and cannot be used as motion yet: tracking space
    // is the player's, established by a calibration the receiving application
    // performs, and an uncalibrated stream carries positions in a space nothing
    // here can name.
    CalibrationRequired,

    Count,
};

inline constexpr std::size_t DiagnosticCodeCount =
    static_cast<std::size_t>(DiagnosticCode::Count);

// The severity scale is shared, because "info / warning / error" is not a
// statement about this protocol.
using DiagnosticSeverity = liveTransport::DiagnosticSeverity;
using liveTransport::DiagnosticSeverityString;

// The stable string, e.g. "VRM_VRCHAT_OSC_PACKET_MALFORMED". This is the
// contract; the enumerator spelling is not.
VRMADAPTERVRCHATOSC_API std::string_view DiagnosticCodeString(
    DiagnosticCode code) noexcept;

VRMADAPTERVRCHATOSC_API std::optional<DiagnosticCode> FindDiagnosticCode(
    std::string_view name) noexcept;

VRMADAPTERVRCHATOSC_API DiagnosticSeverity DiagnosticDefaultSeverity(
    DiagnosticCode code) noexcept;

// Whether a live session can continue past this code by default. A caller may
// still escalate — a flood of recoverable diagnostics is its own signal — but it
// never has to guess which class a code belongs to.
VRMADAPTERVRCHATOSC_API bool DiagnosticIsRecoverable(
    DiagnosticCode code) noexcept;

// One reported diagnostic: this adapter's code, in the shared vehicle.
//
// The default code is named rather than left to the enum's zero even though the
// two agree here, and the redundancy is deliberate: `PacketMalformed` is
// enumerator 0 in this set and in `vrmAdapterVmc`'s, and 6 in
// `vrmAdapterMocopi`'s. A default that reads as "whatever is first" is one
// reordering away from silently changing what a default-constructed diagnostic
// means, which is the behaviour change OSC-2 came closest to shipping unnoticed.
using Diagnostic = liveTransport::Diagnostic<DiagnosticCode,
                                             DiagnosticCode::PacketMalformed>;

// Fills `severity` and `recoverable` from the code's defaults, so the two cannot
// silently disagree with the table.
VRMADAPTERVRCHATOSC_API Diagnostic MakeDiagnostic(
    DiagnosticCode code, std::string detail = {});

// A single deterministic line, stable enough for a golden test to compare:
//
//     [VRM_VRCHAT_OSC_TRACKER_PARTIAL] warning recoverable
//     source=0.0.0.0:9000 t=1.500000 subject=/tracking/trackers/4 seq=42:
//     a rotation arrived with no position
//
// Absent optional fields are omitted rather than printed empty, and the field
// order is fixed. The grammar is both siblings', deliberately: an operator
// reading a session log with more than one adapter in it should not have to
// learn a third line format to find out which one complained.
//
// `subject` carries an OSC address here, where the siblings put a bone name.
// That is the honest spelling for this layer: it reports what the wire said, and
// this wire says `/tracking/trackers/4/position` rather than `leftHand`. A bone
// name in this field would be a humanoid claim made by a layer that has not made
// one.
VRMADAPTERVRCHATOSC_API std::string FormatDiagnostic(
    const Diagnostic& diagnostic);

} // namespace vrmAdapterVrchatOsc
