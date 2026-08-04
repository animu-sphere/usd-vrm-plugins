// SPDX-License-Identifier: Apache-2.0
//
// A recording plus what its producer meant, becoming canonical humanoid motion.
//
// This is the last of the crossings into canonical motion this library has a
// reason to grant (`motionSource_boundaries` carries the list), and unlike the
// other three it is the crossing rather than a corner of one: producing a
// `motion::HumanoidAnimation` is what a converter is for, and a converter that
// could not name a `GfQuatf` could not produce one.
//
// What it is **not** allowed to know stays exactly what it was: the target
// avatar. Nothing here reads a VRM, a joint order, or a target rest pose, and
// the day it does it becomes a second `vrmRetarget` whose fork nobody notices
// until two sources disagree about one avatar (roadmap §2, §4). The pipeline is
//
//     source local motion + source rest  ->  canonical semantic motion
//                                        ->  vrmRetarget + target rest
//
// and this file is the first arrow only.
//
// Seven decisions, each of which a different shape would have quietly cost.
//
// **The change of basis is one signed permutation, and handedness is its
// determinant.** A profile states an up axis, a forward axis and a handedness;
// those fix a 3x3 signed permutation whose rows read canonical components off
// source ones. Its determinant is +1 for a right-handed source and -1 for a
// left-handed one, because canonical space is right-handed and a left-handed
// source has to be mirrored to get there — and choosing the third row's sign to
// make the determinant come out right is *how* the mirror is applied, rather
// than a separate step somebody could forget. A position is `M v`; a rotation
// is `(w, det(M) · M v)`, which is the same conjugation written for
// quaternions.
//
// That one sign also settles the angle convention, which is the part worth
// stating because it looks like a second decision and is not. Angles are turned
// into quaternions by the right-hand rule always, out of the raw numbers, with
// no reference to the source's handedness. A left-handed source's positive
// angle then comes out negated in canonical space, which is exactly what a
// left-hand-rule rotation *is* once mirrored. Handling handedness twice — once
// in the angle sign and once in the mirror — is the failure this note exists to
// prevent, and it produces a body that is correct in every axis-aligned test
// pose and wrong the moment anything turns.
//
// **A bound bone's local rotation is the composition of the path above it.** A
// profile maps a rig onto a humanoid with fewer joints, so real rigs leave spine
// and neck segments mapped to nothing. Those are not rotations to drop: a joint
// between two mapped ones is *on the path* between them. So a bone's canonical
// local rotation is the product of the source local rotations from just below
// its nearest bound ancestor down to itself, root-first. Taking a mapped joint's
// local rotation verbatim instead would lose every rotation above it and place
// the arms and the head wrong — which reads as a subtly misassembled body rather
// than as a failure, and is the specific outcome roadmap §10 wrote this rule
// down to prevent.
//
// **The rest pose is built by the same walk, from whichever rotations the
// profile says are the rest.** One path composition, two callers: the animated
// rotations and the rest ones. A rest pose derived by a second traversal is a
// second traversal that can disagree with the first, and the disagreement would
// show up as a constant offset per bone that looks like a bad capture.
//
// **The rest pose is a value here and a `UsdSkelSkeleton` somewhere else.** It
// carries a local rotation and a local translation per bone and no parent
// array, because the semantic parent of a bone within a rig is
// `motion::NearestPresentAncestor` — a second copy of the humanoid taxonomy is
// the defect `HumanBoneParent` was moved into `motionCore` to avoid. It is
// deliberately *not* part of `motion::HumanoidAnimation`: that type is compared
// by `operator==` and round-tripped through the recorded-trace format, and a
// field added to it is a field every consumer of live capture inherits.
//
// **Root translation and root rotation are the profile's two questions and this
// file answers neither.** `AbsolutePosition` and `RestRelative` differ in where
// their zero is, so both become the same canonical thing — an absolute position
// in the clip's own space — by adding the root's rest where the profile says the
// samples are relative to it. `None` drops the channel rather than converting
// it. `RootRotationPolicy::None` drops the root's rotation *everywhere*,
// including out of the path composition above: a scene node's rotation reaching
// the first bound bone below it is body motion invented from a transform that
// says nothing about a body.
//
// **A track expressed as quaternions is refused, with the reason.** Nothing in
// this tree writes that form (SourceAnimation.h), so converting it would mean
// implementing a path no fixture exercises end to end and testing it against a
// value this repository invented. The alternative — a synthetic fixture, said
// to be synthetic in the corpus — stays open and is what a reader producing
// quaternions would arrive with. Refusing until then is roadmap §10's first
// honest answer taken deliberately, rather than the same gap discovered later
// as a wrong number.
//
// **What could not be carried is reported, and the report separates loss from
// noise.** Canonical motion carries body translation on the root alone, so every
// other joint's translation channels are dropped — and a rig that restates its
// rest geometry every frame loses nothing by that, while one whose elbow
// actually translates loses motion. Those are not the same event and a converter
// that reported them with one word would make the second invisible inside the
// first.
#pragma once

#include "motionSource/SourceAnimation.h"
#include "motionSource/SourceProfile.h"
#include "motionSource/SourceProvenance.h"
#include "motionSource/SourceSkeleton.h"
#include "motionSource/api.h"

#include "motionCore/Humanoid.h"

#include "pxr/base/gf/quatf.h"
#include "pxr/base/gf/vec3f.h"

#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace motionSource
{

// The canonical basis every conversion lands in: right-handed, +Y up, +Z
// forward, metres (MOTION_CONTRACT.md, "Coordinates and time"). Named here
// rather than assumed because a converter is the first code in this repository
// that has to *state* the forward axis: every producer before it happened to
// agree with canonical about which way a character faces.
inline constexpr double CanonicalUnitInMeters = 1.0;

// How a source's numbers become canonical ones: a signed permutation of the
// three components, plus the scale that turns the source's length unit into
// metres.
//
// The determinant is the whole of the handedness question. It is +1 when the
// change of basis is a rotation and -1 when it is a mirror, and it is a stored
// field rather than something each caller recomputes because the two conversions
// below use it differently — a position ignores it and a rotation does not, and
// a caller deriving it twice is a caller that can derive it inconsistently once.
struct CanonicalBasis
{
    // Which source component canonical component *i* reads.
    std::array<int, 3> component = {0, 1, 2};
    // Whether that read is negated.
    std::array<bool, 3> negate = {false, false, false};
    // +1 for a rotation, -1 for a mirror. See the header note.
    int determinant = 1;
    // Metres per source length unit; applies to translations, rest offsets and
    // tips alike, and to nothing angular.
    double scale = 1.0;

    friend bool operator==(const CanonicalBasis& lhs,
                           const CanonicalBasis& rhs) noexcept
    {
        return lhs.component == rhs.component && lhs.negate == rhs.negate
               && lhs.determinant == rhs.determinant && lhs.scale == rhs.scale;
    }
    friend bool operator!=(const CanonicalBasis& lhs,
                           const CanonicalBasis& rhs) noexcept
    {
        return !(lhs == rhs);
    }
};

// The basis `profile` describes, or nullopt when it does not describe one — an
// unstated convention, an unknown unit, or an up axis that is also the forward
// axis. Every one of those is refused by `ValidateSourceProfile` too, so a
// caller that matched a profile first will not see a nullopt here; the option
// exists because this function is also the one worth calling on its own.
MOTIONSOURCE_API std::optional<CanonicalBasis> MakeCanonicalBasis(
    const SourceProfile& profile);

// `scale * M v`. Lengths and positions alike: the profile states one length
// unit and applies it to both.
MOTIONSOURCE_API pxr::GfVec3f ConvertPosition(const CanonicalBasis& basis,
                                              const SourceVec3& value);

// `(w, det(M) · M v)`, normalised. A source may write a slightly-off-unit
// quaternion and `SourceQuat` deliberately keeps it (SourceSkeleton.h), but
// canonical motion is unit quaternions and the layer that changes basis is the
// last one able to say so. A zero-magnitude rotation is refused before it
// reaches here, by the validators this conversion runs first.
MOTIONSOURCE_API pxr::GfQuatf ConvertRotation(const CanonicalBasis& basis,
                                              const SourceQuat& value);

// Three angles composed into the rotation they describe, in the source's own
// component space and by the right-hand rule — see the header note on why the
// handedness is deliberately absent from this step.
//
// The composition is in declaration order with OpenUSD's convention (`a * b`
// applies `b` first), so an order of ZXY composes as `qZ * qX * qY` and the Y
// angle is applied first. That is the *intrinsic* reading of a named order —
// each successive angle turns about the axes the previous ones have already
// moved — and it is what a `SourceEulerOrder` means here, because the
// enumerator only says which angle is stored where (SourceAnimation.h) and
// something has to say what the three of them do.
//
// It is written down rather than assumed because the opposite reading also
// produces plausible motion: mirrored about the order, agreeing exactly
// wherever one axis moves at a time, and visibly wrong only once two do. A
// source whose writer meant the other reading is a source this convention does
// not describe, and that is a profile's problem to raise rather than a second
// composition to keep here — a converter with two would need a producer to pick
// between them, which is the one thing this layer may not have.
MOTIONSOURCE_API SourceQuat ComposeSourceRotation(
    const SourceEulerAngles& angles, SourceEulerOrder order,
    SourceAngleUnit unit) noexcept;

// The clip's own rest pose, per canonical bone, in canonical basis and metres.
//
// No parent array: the semantic parent of a bone within a rig carrying
// `present` is `motion::NearestPresentAncestor`, and a second copy of the
// humanoid taxonomy is a defect waiting to happen. A bone `present` does not
// carry has no rest and its entries are left at identity and zero.
struct CanonicalRestPose
{
    MOTIONSOURCE_API CanonicalRestPose();

    std::array<pxr::GfQuatf, motion::HumanBoneCount> localRotations;
    // From the bone's nearest present ancestor, in that ancestor's rest frame.
    std::array<pxr::GfVec3f, motion::HumanBoneCount> localTranslations;
    std::bitset<motion::HumanBoneCount> present;
};

// Why a conversion produced nothing. Values are stable; `None` is success.
//
// There is no refusal here for an unmapped joint, and that is not an omission:
// what a rig joint the profile maps nothing to means is the profile's
// `unmappedJoints` policy, `MatchSourceProfile` is what enforces it, and the
// joints themselves are on the match below for a caller to report one by one.
enum class ConversionRefusal : std::uint8_t
{
    None,
    // The profile does not describe this rig. `match` carries which of the
    // seven ways, and `detail` its text.
    ProfileMismatch,
    // The animation does not describe this rig, or is not internally
    // consistent. `ValidateSourceAnimation`'s words, verbatim.
    AnimationInvalid,
    // A track expresses its rotation as quaternions. See the header note.
    UnsupportedRotationForm,

    Count,
};

inline constexpr std::size_t ConversionRefusalCount =
    static_cast<std::size_t>(ConversionRefusal::Count);

MOTIONSOURCE_API std::string_view ConversionRefusalName(
    ConversionRefusal refusal) noexcept;

// What the conversion could not carry, and what it composed to avoid dropping.
//
// Every vector is in joint or declaration order, so two runs over one input
// produce two identical reports — a report whose row order depended on a hash
// would be a worse golden test than one whose order is the rig's.
struct ConversionReport
{
    // Rig joints whose translation channels canonical motion does not represent
    // and which stated nothing but their own rest. Restated rest geometry, not
    // motion: nothing was lost and this list is here to say so.
    std::vector<std::size_t> restatedTranslationJoints;
    // Rig joints whose translation channels actually varied. This *is* motion
    // the canonical clip does not carry, because body translation rides on the
    // root alone, and a caller with a diagnostic to raise should raise it here.
    std::vector<std::size_t> droppedTranslationJoints;
    // Bones whose local rotation is a composition of more than one source
    // joint — the unmapped segments between two mapped ones. Not a warning: it
    // is the rule working, and it is reported because *which* bones absorbed a
    // chain is the thing a cross-source comparison will want to know.
    std::vector<motion::HumanBone> composedBones;
};

// What a conversion produced, or did not.
struct SourceConversion
{
    MOTIONSOURCE_API SourceConversion();

    // Defaults to a refusal, not to success, for `SourceProfileMatch`'s reason:
    // a conversion nobody ran has produced nothing, and a struct whose default
    // state claims otherwise traps exactly the paths that forget to check.
    ConversionRefusal refusal = ConversionRefusal::ProfileMismatch;
    std::string detail;

    // Filled whatever the refusal, because a caller reporting on a conversion
    // that did not happen needs it most.
    SourceProfileMatch match;

    motion::HumanoidAnimation animation;
    CanonicalRestPose rest;
    ConversionReport report;

    // The reader's provenance with the profile's two answers filled in. It is
    // returned rather than only folded into `animation.source` because the
    // narrowing into canonical metadata drops the producer version and the
    // profile id (CanonicalMetadata.h), and those are what a clip writer
    // records beside the motion.
    SourceProvenance provenance;

    bool Converted() const noexcept
    {
        return refusal == ConversionRefusal::None;
    }
};

// Convert `animation` over `skeleton`, read as `profile` says to read it.
//
// The order of refusals is part of the contract, because one input can fail
// several at once and a test comparing one enumerator needs to know which:
// profile-against-rig, then animation-against-rig, then rotation form. It runs
// outermost-first — a profile that does not describe this rig has nothing to say
// about its frames.
//
// The producer and the profile id are taken from `profile` and overwrite
// whatever the reader left in `animation.provenance`. This is the one layer
// holding both a recording and a statement of who wrote it, which is exactly why
// a reader must not fill them in (SourceProvenance.h).
MOTIONSOURCE_API SourceConversion ConvertSourceToCanonical(
    const SourceSkeleton& skeleton, const SourceAnimation& animation,
    const SourceProfile& profile);

} // namespace motionSource
