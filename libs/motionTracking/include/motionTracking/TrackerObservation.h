// SPDX-License-Identifier: Apache-2.0
//
// What one device reported, under the identity the source calls it by — and
// nothing about what that means for a body.
//
// This is the value the third of
// [§5.1](../../../../docs/roadmap/osc-and-vrchat-trackers.md#51-assignment-is-a-third-thing-and-it-belongs-to-neither-end)'s
// three decisions consumes. The first two are already here — a region
// vocabulary and an operator's statement over it — and neither of them ever
// needed a number: an assignment is two names and an index
// (TrackerAssignment.h). A solve is where the numbers arrive, so this is the
// first file in this library that holds a geometric type at all.
//
// ## Why it is here and not in `motionCore`
//
// Because of what would read one. Every consumer of that header takes a *pose*
// — retarget maps bones onto a rig, the capture-trace format serialises poses,
// the comparison semantics compare them, the exec nodes evaluate them — so a
// tracker sample there would be a value with no reader in the aggregate
// product, carrying an equality, a comparison and a trace-format obligation
// regardless, because
// [MOTION_CONTRACT.md](../../../../docs/design/MOTION_CONTRACT.md) requires all
// three of anything added to the value types. And it is not the adapter's for
// the reason the milestone exists: a solve inside an adapter is a second motion
// pipeline, so an observation that never left the adapter would have kept the
// solve there with it.
//
// So the boundary is where it was — `motionCore` begins at the canonical pose —
// and this library owns the shape in front of it, with one edge in the one
// direction ([WORKSPACE.md §2](../../../../docs/architecture/WORKSPACE.md)).
//
// ## Canonical on arrival, and this library cannot check that
//
// `position` is metres and `rotation` is a right-handed, +Y-up, +Z-forward
// orientation — the canonical basis
// [MOTION_CONTRACT.md](../../../../docs/design/MOTION_CONTRACT.md) states, which
// a source's adapter converts *into* before anything here sees it. Nothing in a
// vector of three floats says which basis it is in, so this library takes the
// claim rather than verifying it; what it can verify it does verify, in the
// solve — a component that is not finite and a rotation that cannot be
// normalised are refused there rather than propagated into a pose.
//
// ## Three things it deliberately does not carry
//
// **No clock.** A frame's time is the frame's, and the layer that assembled one
// is the layer that knows it — so `SolveTrackerPose` takes a timestamp from its
// caller and an observation carries none. A per-observation time would be a
// field the solve reads nowhere, and the first consumer to fill it differently
// from its frame's would make two poses of one instant.
//
// **No repair of half an observation.** `hasPosition` and `hasRotation` are the
// adapter's two channels arriving separately, and an unset half is left at its
// default exactly as it is one layer down: a defaulted identity rotation is
// bit-for-bit what a tracker at rest reports, so holding one forward would be
// indistinguishable from observing it. What the solve does with a half is
// stated there.
//
// **No identity semantics.** `tracker` is opaque here for the same reason it is
// opaque in an assignment — it is whatever the source calls the device, and
// ordering or parsing it would make one wire's numbering convention this
// library's.
#pragma once

#include "motionTracking/api.h"

#include "pxr/base/gf/quatf.h"
#include "pxr/base/gf/vec3f.h"

#include <string>
#include <string_view>
#include <vector>

namespace motionTracking
{

// One device's placement, in the canonical basis.
struct TrackerObservation
{
    // Verbatim, as the source names it. This is the string an operator's
    // statement addresses, so a solve driven by an assignment built from a
    // different spelling places nothing — which is a refusal the assignment
    // layer reports, and this type is where the two spellings have to agree.
    std::string tracker;

    // Read only where the matching flag is set.
    pxr::GfVec3f position{0.0f, 0.0f, 0.0f};
    pxr::GfQuatf rotation = pxr::GfQuatf::GetIdentity();
    bool hasPosition = false;
    bool hasRotation = false;

    friend bool operator==(const TrackerObservation& lhs,
                           const TrackerObservation& rhs) noexcept
    {
        return lhs.tracker == rhs.tracker && lhs.position == rhs.position
               && lhs.rotation == rhs.rotation
               && lhs.hasPosition == rhs.hasPosition
               && lhs.hasRotation == rhs.hasRotation;
    }
    friend bool operator!=(const TrackerObservation& lhs,
                           const TrackerObservation& rhs) noexcept
    {
        return !(lhs == rhs);
    }
};

// The identities of `observed`, in its own order — which is the argument
// `AssignTrackers` takes.
//
// It exists so that the two calls cannot drift apart. A
// `TrackerAssignmentBinding` holds an index into the observation the assignment
// was made against, so an assignment built from one order and applied to
// another binds a region to a device nobody wore. Building the identity list by
// hand is how that happens; this is one line instead, and `SolveTrackerPose`
// refuses an index it cannot resolve rather than trusting the caller did it.
//
// The views borrow from `observed`, so it must outlive them.
MOTIONTRACKING_API std::vector<std::string_view> TrackerIdentities(
    const std::vector<TrackerObservation>& observed);

} // namespace motionTracking
