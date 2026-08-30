// SPDX-License-Identifier: Apache-2.0
//
// Which tracker is which body region, stated by an operator and never guessed.
//
// [The OSC track §5.1](../../../../docs/roadmap/osc-and-vrchat-trackers.md#51-assignment-is-a-third-thing-and-it-belongs-to-neither-end)
// separates three decisions that collapse into each other if nobody holds them
// apart — decode, assignment, solve — and this file is the middle one, whole.
// It knows an opaque tracker **identity**, a `TrackerRegion`, and what to do
// when the two do not cover each other. It knows no wire, no address, no
// adapter and no bone, and the boundary check beside it is what makes that a
// property rather than a claim.
//
// ## Assignment is not a lookup, and it is not IK either
//
// A tracker index is an index into whatever the wearer strapped on. Nothing in
// a stream of numbers says which leg a device is on, so an adapter that mapped
// index 1 onto a body role would have invented a calibration and hidden it in a
// decoder. What settles it is a statement by the person who put the trackers
// on, which is why the required path is explicit and the *only* required path:
// `motion_bvh_convert` takes a named profile and detects none, for the reason
// that a detector written before the contract settles the contract on whichever
// rig was recorded first.
//
// Automatic assignment from rest geometry is a later aid **over this contract**
// — a producer of `TrackerAssignmentSpec`, never a second way to reach a
// binding.
//
// ## A set it cannot place is three answers, not one
//
// Three-point, six-point and full-body rigs differ in what is observable, not
// in what is solvable, so what happens to an observed tracker no statement
// places is a policy the caller chooses and this library states:
//
// | policy | what it is for | refusal |
// | --- | --- | --- |
// | `Refuse` | the statement is wrong for this rig | `UnplacedTracker` |
// | `Ignore` | the rig carries more than the solve needs | none |
// | `Hold`   | the rig has not finished coming up | `Held` |
//
// **`Refuse` and `Hold` both refuse, and the enumerator is the difference that
// matters to a live caller**: `UnplacedTracker` will still be true next frame,
// so a caller stops and tells the operator; `Held` may not be, so a caller
// keeps whatever assignment it had and tries the next frame. Collapsing them
// into one refusal would make "the operator mis-numbered a tracker" and "the
// third tracker has not powered on yet" the same event, and only one of those
// is worth interrupting a session for. `Refuse` is the default, because an
// unnoticed mis-statement drives a body with a foot on the wrong leg and every
// value in it is individually correct.
//
// ## The other direction is data, and deliberately not a refusal
//
// A *stated* tracker that did not arrive is `absent`, under every policy. That
// is the same decision `TrackerFrame::missing` makes one layer over: an absence
// is reported where a consumer can apply its own policy, because a rig coming
// up one tracker at a time would otherwise refuse every frame for the first
// second, and a statement listing five trackers for a three-point rig is
// something an operator has to *see* rather than something this library can
// resolve. `Hold` is how a caller turns that visibility into waiting.
//
// ## A refusal carries no diagnostic code
//
// The precedent is `motionSource`'s `SourceProfileRefusal` and `osc`'s
// `OscDecodeError`: a shared layer names the *event*, and whoever knows which
// adapter it is maps that onto that adapter's frozen codes. This library is
// reached from three adapters' tools eventually, and a `VRM_*` string in it
// would give one event one adapter's spelling for all of them.
#pragma once

#include "motionTracking/TrackerRegion.h"
#include "motionTracking/api.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace motionTracking
{

// One line of an operator's statement: this tracker is on this region.
//
// `tracker` is **opaque**. It is whatever the source calls the device, copied
// through without interpretation — a number, a name, a serial. Nothing here
// parses it, orders by it, or reads meaning out of its shape, which is what
// keeps one wire's numbering convention from becoming this library's.
struct TrackerRegionStatement
{
    std::string tracker;
    TrackerRegion region = TrackerRegion::Count;

    friend bool operator==(const TrackerRegionStatement& lhs,
                           const TrackerRegionStatement& rhs) noexcept
    {
        return lhs.tracker == rhs.tracker && lhs.region == rhs.region;
    }
    friend bool operator!=(const TrackerRegionStatement& lhs,
                           const TrackerRegionStatement& rhs) noexcept
    {
        return !(lhs == rhs);
    }
};

// What to do with an observed tracker no statement places. See the header note;
// the three differ in what a caller does next, not only in what they return.
enum class UnplacedTrackerPolicy : std::uint8_t
{
    Refuse,
    Ignore,
    Hold,

    Count,
};

MOTIONTRACKING_API std::string_view UnplacedTrackerPolicyName(
    UnplacedTrackerPolicy policy) noexcept;

MOTIONTRACKING_API std::optional<UnplacedTrackerPolicy>
ParseUnplacedTrackerPolicy(std::string_view name) noexcept;

// An operator's statement, whole.
struct TrackerAssignmentSpec
{
    // In the order the operator wrote them. That order is the report order of
    // every vector below, so a printed assignment reads back in the shape it
    // was written rather than in an enum's.
    std::vector<TrackerRegionStatement> statements;

    UnplacedTrackerPolicy unplaced = UnplacedTrackerPolicy::Refuse;
};

// Whether `spec` is a statement at all, independent of any observation.
//
// Six ways to fail, and one of them is not obvious: an identity that cannot be
// *written down* is refused. `ParseTrackerAssignmentSpec` below is the form an
// operator types, so an identity carrying whitespace or `=` would produce a
// spec no operator could have stated and none can restate — and a contract
// whose only required path is an explicit statement cannot hold values that
// path cannot express.
//
// `reason` is filled with plain text naming the first failure when it is not
// null; it is untouched on success.
MOTIONTRACKING_API bool ValidateTrackerAssignmentSpec(
    const TrackerAssignmentSpec& spec, std::string* reason = nullptr);

// Read a statement in the form an operator types:
//
//     1=leftFoot 2=rightFoot head=head      # comments run to end of line
//
// Pairs are separated by whitespace or commas, in any mix. The **policy is not
// in this syntax**, deliberately: it is the caller's flag, and a `unplaced=...`
// directive here would make `unplaced` a tracker identity nobody could use.
//
// Returns false with `reason` filled on the first syntax error or on a spec
// that does not validate — the two are one refusal to a caller, and separating
// them would offer a spec that parsed and cannot be used.
MOTIONTRACKING_API bool ParseTrackerAssignmentSpec(
    std::string_view text, TrackerAssignmentSpec* out,
    std::string* reason = nullptr);

// Why an observation was not assigned. The enumerator values are stable, and
// they are *not* the contract a user reads — an adapter's codes are.
enum class TrackerAssignmentRefusal : std::uint8_t
{
    None,
    // `spec` does not satisfy `ValidateTrackerAssignmentSpec`. `detail` carries
    // its reason verbatim.
    SpecInvalid,
    // The observation is not one: an empty identity, or the same identity
    // twice. Not resolvable by taking the first — a statement addresses a
    // tracker by name, and binding one of two identically named devices is how
    // a rig gets half assigned with nobody told.
    ObservationInvalid,
    // An observed tracker no statement places, under `Refuse`. Still true next
    // frame.
    UnplacedTracker,
    // The same, under `Hold`. May not be true next frame; a caller waits.
    Held,
    // Nothing bound. Raised under every policy, including `Ignore`, because an
    // assignment that placed no tracker is not an assignment — and `Ignore` is
    // the policy that would otherwise return success with an empty binding set
    // and let a caller drive a solve from nothing.
    NothingPlaced,

    Count,
};

inline constexpr std::size_t TrackerAssignmentRefusalCount =
    static_cast<std::size_t>(TrackerAssignmentRefusal::Count);

MOTIONTRACKING_API std::string_view TrackerAssignmentRefusalName(
    TrackerAssignmentRefusal refusal) noexcept;

// One region bound to one observed tracker.
struct TrackerAssignmentBinding
{
    TrackerRegion region = TrackerRegion::Count;
    // An index into the observation `AssignTrackers` was given, never into the
    // spec: the caller holds the samples, and handing back a position in its own
    // array is what lets this library stay ignorant of what a sample is.
    std::size_t observedIndex = 0;

    friend bool operator==(const TrackerAssignmentBinding& lhs,
                           const TrackerAssignmentBinding& rhs) noexcept
    {
        return lhs.region == rhs.region
               && lhs.observedIndex == rhs.observedIndex;
    }
    friend bool operator!=(const TrackerAssignmentBinding& lhs,
                           const TrackerAssignmentBinding& rhs) noexcept
    {
        return !(lhs == rhs);
    }
};

// What assigning a statement to an observation found.
//
// **Every vector is filled whatever the refusal**, on `SourceProfileMatch`'s
// rule and for its reason: the caller that most needs this struct is the one
// reporting on an assignment that did *not* succeed, and a refusal that cleared
// its own evidence would make "the operator named a tracker this rig does not
// carry" indistinguishable from "nothing was tried". `Placed()` is the gate,
// and it is the only thing a caller may drive a solve from.
struct TrackerAssignment
{
    // Defaults to a refusal, not to success. A `TrackerAssignment` nobody
    // assigned has concluded nothing, and a struct whose default state claims a
    // binding is a trap for exactly the paths that forget to check.
    TrackerAssignmentRefusal refusal = TrackerAssignmentRefusal::SpecInvalid;

    // Plain text naming what was refused — the identity, the region, the
    // reason. Empty when nothing was.
    std::string detail;

    // In the spec's declaration order.
    std::vector<TrackerAssignmentBinding> bound;
    // Observed trackers no statement places, in observation order.
    std::vector<std::size_t> unplaced;
    // Stated regions whose tracker did not arrive, in declaration order. Data
    // under every policy; see the header note.
    std::vector<TrackerRegion> absent;

    bool Placed() const noexcept
    {
        return refusal == TrackerAssignmentRefusal::None;
    }

    // The observed tracker bound to `region`, or nullopt when none is.
    MOTIONTRACKING_API std::optional<std::size_t> ObservedFor(
        TrackerRegion region) const;

    // The region an observed tracker was bound to, or nullopt when it is
    // unplaced or out of range.
    MOTIONTRACKING_API std::optional<TrackerRegion> RegionFor(
        std::size_t observedIndex) const;
};

// Assign `spec` to the trackers an observation carries.
//
// `observed` is the identities, in the order the caller holds them — nothing
// more. A caller with tracker samples passes their identities and keeps the
// samples; that is what lets this library be reached from a tracker frame it
// has no type for.
//
// **The first refusal wins and the order is part of the contract**, because an
// observation can fail several of these at once and a test comparing one
// enumerator needs to know which: spec · observation · unplaced policy ·
// nothing placed. It runs outermost-first — a statement that is not a statement
// says nothing about a rig, and an observation that is not one is not addressed
// by any check below it.
MOTIONTRACKING_API TrackerAssignment AssignTrackers(
    const TrackerAssignmentSpec& spec,
    const std::vector<std::string_view>& observed);

} // namespace motionTracking
