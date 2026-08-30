// SPDX-License-Identifier: Apache-2.0
//
// The sender's axes into the canonical ones. This is the first file in this
// adapter that produces a value the rest of this repository can read, and the
// only one that knows which way the numbers on this wire point
// (roadmap/osc-and-vrchat-trackers.md §9, VRC-3).
//
// It converts and it decides nothing else. A tracker's three floats become a
// canonical position or a canonical orientation and stop there: which tracker
// is on which body region is a policy outside this adapter, which observations
// belong to one frame is VRC-4's window, and what any of it means for an avatar
// joint is the solve's. **No body role and no avatar is named in this file**,
// under any outcome ([§5](../../../../../docs/roadmap/osc-and-vrchat-trackers.md#5-a-tracker-source-is-not-a-pose-source)).
//
// ## The basis was measured, not read
//
// VRChat documents its tracking space as Unity's — left-handed, +Y up, metres,
// Euler rotations — and this file agrees with that documentation in every
// respect. **That agreement is a result rather than an assumption**, which is
// the whole of why VRC-3 is a milestone and not a line of arithmetic: the
// handedness episode one adapter over cost a release because a documented basis
// was taken on trust
// ([the adapter plan](../../../../../docs/roadmap/adapters-mocopi-vmc-ardy.md#96-cross-source-comparison)),
// and a conversion that is wrong in the same way is wrong invisibly — every
// axis-aligned test pose passes and the body is mirrored the moment anything
// turns.
//
// So each of the five claims below is a reading of the 2026-08-30 session, and
// [report 03](../../../../../docs/reports/motion/03-2026-08-30-vrchat-osc-tracking-space.md)
// is the measurement:
//
//     unit        metres           a standing operator's head reports 1.5178,
//                                  hips 0.8922 and both feet 0.0920
//     up axis     +Y               those three separate on the second component
//                                  and on no other
//     forward     +Z               1.401 m of walking travels (-0.034, 1.400)
//                                  in (x, z) at a yaw of +8.9 deg
//     handedness  left-handed      a head turned to the operator's LEFT reports
//                                  a yaw of -77.5 deg, so the body's left is -X
//     angles      degrees, [0,360) 44 918 messages span -0.0053 to 359.9942 and
//                                  every address is `,fff`
//
// The fourth row is the one that had to be measured rather than reviewed, and
// the take that settles it is the *labelled* one: nothing in a stream of
// numbers says which arm a person raised or which way they turned, so a session
// that did not write it down could not answer this at all. It is also the row
// this sender could have failed on — the same device's native wire is
// right-handed with +X on the body's **left**, so the two outputs of one
// application disagree about the sign of X, and a decoder that carried the
// native reading over would have mirrored every session silently.
//
// ## The Euler order is measured to three candidates, not to one
//
// A rotation arrives as three angles, and three angles are not an orientation
// until something says in which order they are applied. This session narrows
// that to three of the six ways and cannot narrow it further, so the residual
// is stated here rather than discovered later:
//
// * The **yaw is outermost** — it is applied about the world's vertical, after
//   the other two. This is measured, on the labelled head turns: the three
//   orders that place Y elsewhere drag the head's 12–18 deg of pitch into
//   12–21 deg of apparent *roll* at the ends of an 80 deg turn, and a head
//   turning left and right does not roll. The three orders that keep Y
//   outermost hold the head's lateral axis within 2.6 deg of horizontal
//   throughout. That is the strongest statement this session supports and it is
//   an experiment, not a citation.
// * Which of X and Z sits inside it is **not measured**, because no sample in
//   the session rotates about two axes at once by enough to tell: across 44 918
//   messages the second-largest component of any orientation is 25.2 deg.
//
// This file composes **`Ry · Rx · Rz`**, applied to a column vector, so the Z
// angle turns first and the Y angle last. It is the survivor that Unity
// documents (Unity spells the same composition "ZXY", naming the angles in
// application order), which is the reason to prefer it among three the session
// cannot separate.
//
// **What the residual costs is measured too**, because "unverified" without a
// number is not a statement anyone can act on. Over the whole session the six
// orders disagree by up to 25.7 deg; the three survivors —  `Ry·Rx·Rz`,
// `Ry·Rz·Rx` and `Rz·Ry·Rx` — disagree by a median of 0.21 deg, 1.75 deg at the
// 95th percentile and 12.33 deg at worst, and 96 % of the session is inside
// 2 deg. The worst sample is a head at 31 deg of pitch and -22 deg of roll,
// which says exactly what take would close the gap: a **labelled rolled** head
// or foot, held. This session has none, because nobody thought to tilt.
//
// The composition itself is by the **right-hand rule, out of the raw numbers,
// with no reference to the sender's handedness** — the same rule
// `motionSource::ComposeSourceRotation` states for recorded sources. A
// left-handed source's positive angle comes out negated in canonical space, and
// that negation *is* what a left-hand-rule rotation becomes once mirrored.
// Applying the handedness twice — once in the angle sign and once in the mirror
// below — produces a body that is correct in every axis-aligned pose and wrong
// the moment anything turns, which is the one failure this paragraph exists to
// prevent.
//
// ## The change of basis is VRM 1.0's, and it is the sibling's
//
// Canonical motion is right-handed, +Y up, +Z forward, metres
// ([MOTION_CONTRACT.md](../../../../../docs/design/MOTION_CONTRACT.md)). The
// measured space agrees about up, about forward and about the unit, and
// disagrees about handedness alone, so the change of basis is the reflection
// through X that VRM 1.0 defines:
//
//     position   (x, y, z)     ->  (-x, y, z)
//     rotation   (w, (x, y, z))->  (w, (x, -y, -z))
//
// which is `M v` and `(w, det(M) · M v)` for `M = diag(-1, 1, 1)`, the general
// form the recorded half writes as a `CanonicalBasis`. Nothing is scaled: the
// unit is metres on both sides.
//
// **This is the third place in the tree that carries this arithmetic**, after
// `vrmAdapterVmc/SkeletonMap.cpp` and `motionSource/CanonicalConversion.cpp`,
// and it is written out here rather than shared because sharing it needs a home
// an adapter is allowed to reach: `motionSource` is the recorded half and
// [WORKSPACE.md §2](../../../../../docs/architecture/WORKSPACE.md) gives an
// adapter four edges that do not include it. What this adapter adds that
// neither sibling has is the Euler composition — VMC sends quaternions — so the
// case for a shared basis primitive in `motionCore` is stronger after this file
// than before it, and that is a contract change rather than a refactor
// ([§10](../../../../../docs/roadmap/osc-and-vrchat-trackers.md#10-contract-changes-this-plan-requires)).
//
// ## What this layer refuses, which is one thing
//
// The two conversion functions check nothing: they are arithmetic, and a
// non-finite input converts to a non-finite output. The two `Map` functions are
// the boundary, exactly as `MapVmcBoneTransform` is one adapter over, and they
// have a single refusal each — `VRM_VRCHAT_OSC_COORDINATE_INVALID` for a
// component that is not finite.
//
// **On the wire path that refusal cannot fire**, because `DecodeTrackerMessage`
// already refuses a non-finite component with the same code. It is here for the
// `TrackerMessage` that did not come from a datagram: the type is an aggregate
// of three floats and a caller can build one, and a NaN reaching a solve makes
// a tracker disappear from every comparison that had it rather than fail. Every
// other way three finite floats could be wrong in this space — a magnitude no
// room contains, a tracker below the floor — is a claim about a calibration
// this layer has not been given and must not invent
// ([§5.1](../../../../../docs/roadmap/osc-and-vrchat-trackers.md#51-assignment-is-a-third-thing-and-it-belongs-to-neither-end)).
#pragma once

#include "vrmAdapterVrchatOsc/Diagnostics.h"
#include "vrmAdapterVrchatOsc/TrackerMessage.h"
#include "vrmAdapterVrchatOsc/api.h"

#include "pxr/base/gf/quatf.h"
#include "pxr/base/gf/vec3f.h"

#include <array>
#include <string>

namespace vrmAdapterVrchatOsc
{

// Metres per length unit on this wire, measured rather than declared: a
// standing operator's head reads 1.5178 and their feet 0.0920, which no other
// unit produces for a person. Applies to positions and to nothing angular.
inline constexpr double TrackingSpaceUnitInMeters = 1.0;

// Degrees per angular unit. The session spans -0.0053 to 359.9942 over 44 918
// messages — a `[0, 360)` convention with float error either side of the wrap —
// and a radian reading of the same numbers would put a standing operator's feet
// through several full turns.
inline constexpr double TrackingSpaceAngleUnitInDegrees = 1.0;

// The reflection this basis change is: canonical component *i* reads the
// sender's component *i*, and the first is negated. Written as data because it
// is the whole of the conversion below and a reader should be able to check the
// arithmetic against something shorter than the arithmetic.
inline constexpr int TrackingSpaceMirroredComponent = 0;

// -1 for a mirror, +1 for a rotation. It is -1 because the measured space is
// left-handed and canonical space is not, and it is the sign the rotation half
// of the conversion applies to the imaginary part — the same `det(M)` the
// recorded half's `CanonicalBasis` carries.
inline constexpr int TrackingSpaceDeterminant = -1;

// The sender's position into the canonical one, with no validity check: a
// non-finite input converts to a non-finite output rather than being caught
// here, because this is the arithmetic and `MapTrackerPosition` is the
// boundary.
VRMADAPTERVRCHATOSC_API pxr::GfVec3f ToCanonicalPosition(
    const std::array<float, 3>& position) noexcept;

// Three angles in the sender's own space into a canonical orientation. Degrees,
// composed `Ry · Rx · Rz` by the right-hand rule and then mirrored — see the
// header on which half of that is measured and which is the documented survivor
// of three.
//
// The result is normalised. Any three finite angles name an orientation, so
// unlike the sibling's quaternion path there is no zero-length case to refuse:
// the composition of three unit quaternions is unit up to float error, and the
// normalisation is arithmetic rather than a repair.
VRMADAPTERVRCHATOSC_API pxr::GfQuatf ToCanonicalRotation(
    const std::array<float, 3>& eulerDegrees) noexcept;

// The address a decoded message came from, rebuilt: `/tracking/trackers/1/position`.
// It is a diagnostic subject rather than a routing key — a `TrackerMessage`
// carries the identity and the channel the address was read from, and a refusal
// at this layer has to name what the wire said rather than what this adapter
// made of it (Diagnostics.h).
VRMADAPTERVRCHATOSC_API std::string TrackerMessageAddress(
    const TrackerMessage& message);

// `message` must carry `TrackerChannel::Position`. Returns false and fills
// `diagnostic` for a component that is not finite
// (`VRM_VRCHAT_OSC_COORDINATE_INVALID`) or for a caller's own mistake — a null
// `out`, or the other channel — which raise `VRM_VRCHAT_OSC_PACKET_MALFORMED`
// as every caller-precondition guard in this adapter does. `out` is left
// untouched on every failure.
//
// **The channel guard is not defensive.** Position and rotation are three
// floats each on this wire, so a caller that passed the wrong one would get a
// plausible value out of arithmetic that cannot tell them apart — a rotation
// read as a position is a point half a kilometre away, and a position read as a
// rotation is a fraction of a degree from identity. The second of those is
// indistinguishable from a tracker at rest.
VRMADAPTERVRCHATOSC_API bool MapTrackerPosition(const TrackerMessage& message,
                                                pxr::GfVec3f* out,
                                                Diagnostic* diagnostic = nullptr);

// `message` must carry `TrackerChannel::Rotation`; the failures are the
// position function's.
VRMADAPTERVRCHATOSC_API bool MapTrackerRotation(const TrackerMessage& message,
                                                pxr::GfQuatf* out,
                                                Diagnostic* diagnostic = nullptr);

} // namespace vrmAdapterVrchatOsc
