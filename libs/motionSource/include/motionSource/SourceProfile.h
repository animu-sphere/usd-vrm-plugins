// SPDX-License-Identifier: Apache-2.0
//
// What one producer's export means — declared, never computed.
//
// A reader knows a file syntax and no semantics; this library knows semantics
// and no file syntax; a **profile** supplies what neither can know on its own
// (roadmap/recorded-motion-sources.md §2, §3). Joint names, units, axes and root
// conventions are facts about the *writer* rather than about a format, so they
// arrive here as data and the code above never has a name for any of them.
//
// This is the second of two headers permitted to name a canonical type, and the
// permission is still narrow: a joint map's right-hand side is a
// `motion::HumanBone`, which is the entire point of the map.
// `CanonicalMetadata.h` argues the general case; `motionSource_boundaries`
// carries the list, so a third file joins it in review rather than quietly.
//
// Five decisions, each of which a different shape would have quietly cost.
//
// **Matching a profile against a rig returns facts, not a score.** A detector
// reports candidates with a confidence (roadmap §3.1), and the arithmetic that
// produces one belongs to the detector: a weighting fitted to the exports on
// hand today would be a producer's answer baked into the layer forbidden to hold
// one, arriving through a float instead of through an `if`. What this layer
// states is countable — which bones bound, which required ones did not, which
// source joints nothing maps, which names are ambiguous — and every one of those
// vectors is filled whatever the refusal, because a candidate report is mostly
// about profiles that did **not** match.
//
// **A refusal is a typed value here and a diagnostic code in the caller.** The
// semantic half of a reader's frozen diagnostic set is raised where a document
// meets a profile — which is this library, the one forbidden to know that reader
// exists (WORKSPACE.md §2). So the refusals below name the *event* in
// format-neutral terms and a caller holding both a reader and a profile maps
// them to that reader's codes. Two rejected alternatives are worth recording:
// plain text alone (`ValidateSourceSkeleton`'s answer, which is right for a
// structural invariant) would make that caller parse prose to pick a code, and a
// second code namespace here would give one event two spellings and duplicate a
// set whose whole value is being frozen and closed.
//
// **Every convention has an `Unspecified`, and validation refuses it.** There is
// no default profile and no automatic fallback, and a silently-assumed
// handedness or unit is the specific failure roadmap §3.1 forbids: motion that
// is subtly misassembled rather than absent, which is worse than a refusal
// because it looks like a result. A default-constructed `SourceProfile` is
// therefore invalid by construction, which is the point.
//
// **A joint map is a hierarchy embedding, not a name lookup.** Two profiles can
// map the same names to the same bones and disagree about which joint sits under
// which; the check that catches it is that each bound bone's nearest bound
// humanoid ancestor is also a source ancestor. It does not require the source
// hierarchy to match the humanoid one — a rig that solves no upper chest still
// parents its shoulders somewhere — and `motion::NearestPresentAncestor` is what
// answers that, rather than a second copy of the humanoid taxonomy here.
//
// **A profile knows the joints it deliberately does not map.** Every real
// producer exports more than a humanoid, so a policy of refusing unmapped joints
// would be decoration without a way to say "this one, on purpose".
// `ignoredJoints` is that way, and it is declarative like everything else: a
// name, not a rule.
#pragma once

#include "motionSource/SourceSkeleton.h"
#include "motionSource/api.h"

#include "motionCore/Humanoid.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace motionSource
{

// Each enumerator below is a value a profile file states by name, and the
// `...Name` / `Find...` pairs are that vocabulary. Keeping the spellings in the
// library rather than in a loader is what stops a second vocabulary appearing
// the day a file is read: a loader becomes a table lookup, and a report prints
// the same word a file wrote.
//
// Values are stable array indices; append only before Count. `Unspecified` is
// index 0 in every one of them so that a profile nobody finished is refused
// rather than assumed.

enum class SourceHandedness : std::uint8_t
{
    Unspecified,
    Right,
    Left,

    Count,
};

// A **signed** axis, because "up" and "forward" are directions. Producers
// disagree about the sign as readily as about the letter, and an unsigned axis
// would make a rig that faces backwards indistinguishable from one that does
// not. `Find` accepts the unsigned spelling as the positive direction — a
// profile writing `Y` means `+Y`, and refusing it would be pedantry rather than
// a check.
enum class SourceAxis : std::uint8_t
{
    Unspecified,
    PlusX,
    MinusX,
    PlusY,
    MinusY,
    PlusZ,
    MinusZ,

    Count,
};

enum class SourceLengthUnit : std::uint8_t
{
    Unspecified,
    Meters,
    Centimeters,
    Millimeters,
    Inches,

    Count,
};

// What a root joint's translation samples *are*, which is not the same question
// as whether the motion is wanted. The two stated values differ in their zero,
// and a real export settled that this distinction is not theoretical: samples
// arriving as absolute positions that begin at the root's own rest offset read
// as a large constant displacement under the other interpretation.
enum class RootTranslationPolicy : std::uint8_t
{
    Unspecified,
    // The sample is the root's position in the source's own space. Displacement
    // is the sample less the root's rest translation.
    AbsolutePosition,
    // The sample is already a displacement from the root's rest translation.
    RestRelative,
    // The source's root translation carries no motion — a writer restating rest
    // geometry every frame. Dropped rather than converted.
    None,

    Count,
};

enum class RootRotationPolicy : std::uint8_t
{
    Unspecified,
    // The root's rotation is the character's orientation.
    BodyOrientation,
    // The root is a scene or armature node whose rotation says nothing about the
    // body. Dropped rather than converted.
    None,

    Count,
};

// Which of the things a source may state is its rest pose. The three are
// genuinely different files, not three readings of one: `SourceJoint` carries an
// optional rest rotation precisely because sources disagree about having one,
// and a writer whose first frame is the rest pose states neither.
enum class RestPoseSource : std::uint8_t
{
    Unspecified,
    // The rest translations alone; rest orientation is identity.
    RestOffsets,
    // The rest translations plus the rest rotations the source states per joint.
    StatedRestRotations,
    // The first animated frame is the rest pose.
    FirstFrame,

    Count,
};

// What a source joint the profile maps nothing to means. `Ignore` is silent,
// `Report` expects a recoverable diagnostic per joint, and `Refuse` stops the
// conversion — which only a profile listing its `ignoredJoints` can afford.
enum class UnmappedJointPolicy : std::uint8_t
{
    Unspecified,
    Ignore,
    Report,
    Refuse,

    Count,
};

MOTIONSOURCE_API std::string_view SourceHandednessName(
    SourceHandedness handedness) noexcept;
MOTIONSOURCE_API std::optional<SourceHandedness> FindSourceHandedness(
    std::string_view name) noexcept;

// The canonical spelling carries the sign, e.g. "+Y".
MOTIONSOURCE_API std::string_view SourceAxisName(SourceAxis axis) noexcept;
// ASCII case-insensitive, and "Y" reads as "+Y". See the enum's note.
MOTIONSOURCE_API std::optional<SourceAxis> FindSourceAxis(
    std::string_view name) noexcept;
// Which axis, ignoring the sign: 0 for X, 1 for Y, 2 for Z. Nullopt for
// `Unspecified` and for a non-enumerator.
MOTIONSOURCE_API std::optional<int> SourceAxisComponent(
    SourceAxis axis) noexcept;
MOTIONSOURCE_API bool SourceAxisIsNegative(SourceAxis axis) noexcept;

MOTIONSOURCE_API std::string_view SourceLengthUnitName(
    SourceLengthUnit unit) noexcept;
// Also accepts the `-tre` spellings: a unit spelled the other way is the same
// unit, and a profile refused over an `e` and an `r` teaches nobody anything.
MOTIONSOURCE_API std::optional<SourceLengthUnit> FindSourceLengthUnit(
    std::string_view name) noexcept;
// How many meters one unit is. Nullopt for `Unspecified` and for a
// non-enumerator — the value a converter multiplies by, and the one number in
// this header that is arithmetic rather than vocabulary.
MOTIONSOURCE_API std::optional<double> SourceLengthUnitInMeters(
    SourceLengthUnit unit) noexcept;

MOTIONSOURCE_API std::string_view RootTranslationPolicyName(
    RootTranslationPolicy policy) noexcept;
MOTIONSOURCE_API std::optional<RootTranslationPolicy> FindRootTranslationPolicy(
    std::string_view name) noexcept;

MOTIONSOURCE_API std::string_view RootRotationPolicyName(
    RootRotationPolicy policy) noexcept;
MOTIONSOURCE_API std::optional<RootRotationPolicy> FindRootRotationPolicy(
    std::string_view name) noexcept;

MOTIONSOURCE_API std::string_view RestPoseSourceName(
    RestPoseSource source) noexcept;
MOTIONSOURCE_API std::optional<RestPoseSource> FindRestPoseSource(
    std::string_view name) noexcept;

MOTIONSOURCE_API std::string_view UnmappedJointPolicyName(
    UnmappedJointPolicy policy) noexcept;
MOTIONSOURCE_API std::optional<UnmappedJointPolicy> FindUnmappedJointPolicy(
    std::string_view name) noexcept;

// One line of a profile's joint map.
struct SourceJointMapping
{
    // The joint name as the producer spells it, matched verbatim. A profile that
    // wanted case folding would be describing two producers.
    std::string sourceName;

    // The canonical bone this joint carries. `Count` is not a bone and is the
    // default for the reason every convention above has an `Unspecified`.
    motion::HumanBone bone = motion::HumanBone::Count;

    // Whether a rig missing this joint is still described by this profile. An
    // export preset that drops fingers is the ordinary case, and it is a
    // *profile's* answer rather than a target's: what the target needs is
    // `vrmRetarget`'s question, one pipeline stage further on.
    bool required = false;

    MOTIONSOURCE_API friend bool operator==(
        const SourceJointMapping& lhs, const SourceJointMapping& rhs) noexcept;
    MOTIONSOURCE_API friend bool operator!=(
        const SourceJointMapping& lhs, const SourceJointMapping& rhs) noexcept;
};

// One producer *and export preset*. A producer is not a profile: one
// application's presets can disagree with each other and two applications can
// agree, which is why the id below carries a preset and why nothing here is
// keyed on the producer label (roadmap §3).
struct SourceProfile
{
    // The profile's identity, carried into `SourceProvenance::profileId` so a
    // conversion records what it was read under. Its shape is a convention —
    // `<producer>-<format>-<preset>-v<N>` for the profiles this repository
    // ships — and deliberately **not** checked here: the plan's own
    // user-defined example carries no version part, and a library that enforced
    // a naming convention on data it does not own would refuse a file for a
    // reason that has nothing to do with the motion in it.
    std::string id;

    // The provenance label, reaching `SourceProvenance::producer` unchanged.
    // Never sniffed from a file and never compared to anything: a product name
    // may appear in this data precisely because no code here has a name for it
    // (WORKSPACE.md §1).
    std::string producer;

    // The source name of the joint the hierarchy roots at, which is the joint
    // the two root policies below describe. It need not be mapped to a bone — a
    // writer whose root is a scene node maps nothing to it and lists it in
    // `ignoredJoints`.
    std::string rootJoint;

    SourceHandedness handedness = SourceHandedness::Unspecified;
    SourceAxis upAxis = SourceAxis::Unspecified;
    SourceAxis forwardAxis = SourceAxis::Unspecified;
    // Applies to translations, rest offsets and tips alike. There is no angle
    // unit here: a format states whether its angles are degrees or radians and a
    // producer does not get to disagree with its own format about that, so a
    // reader answers it on the track (SourceAnimation.h).
    SourceLengthUnit translationUnit = SourceLengthUnit::Unspecified;

    RootTranslationPolicy rootTranslation = RootTranslationPolicy::Unspecified;
    RootRotationPolicy rootRotation = RootRotationPolicy::Unspecified;
    RestPoseSource restPose = RestPoseSource::Unspecified;
    UnmappedJointPolicy unmappedJoints = UnmappedJointPolicy::Unspecified;

    // In the profile's own declaration order, which is the order a match reports
    // in — a report whose row order depended on the rig would be a worse golden
    // test than one whose row order is the file's.
    std::vector<SourceJointMapping> joints;

    // Source names this profile knows and maps to nothing on purpose. Exempt
    // from `unmappedJoints`, and never overlapping the map: a joint cannot be
    // both mapped and deliberately ignored.
    std::vector<std::string> ignoredJoints;

    // Index into `joints`, or nullopt. Names are unique in a valid profile, so
    // unlike a rig's joints this really is a key.
    MOTIONSOURCE_API std::optional<std::size_t> FindMapping(
        std::string_view sourceName) const;
    MOTIONSOURCE_API std::optional<std::size_t> FindBoneMapping(
        motion::HumanBone bone) const;
    MOTIONSOURCE_API bool IgnoresJoint(std::string_view sourceName) const;

    // How many mappings this profile declares required. The denominator of a
    // detector's "required joints matched: n/m".
    MOTIONSOURCE_API std::size_t RequiredMappingCount() const noexcept;

    MOTIONSOURCE_API friend bool operator==(const SourceProfile& lhs,
                                            const SourceProfile& rhs) noexcept;
    MOTIONSOURCE_API friend bool operator!=(const SourceProfile& lhs,
                                            const SourceProfile& rhs) noexcept;
};

// Whether a profile is internally consistent: it is identified, it names a
// producer and a root joint, every convention is stated, its up and forward
// axes are different axes, it maps at least one joint, no source name and no
// bone appears twice, every bone is a bone, the canonical root is mapped and
// required, and nothing is both mapped and ignored.
//
// `reason` is plain text, for the reason `ValidateSourceSkeleton` states: a
// structural invariant of a value this layer owns is not an event in a reader's
// diagnostic set. The refusals a *rig* produces are the typed ones below, and
// the difference is real — one says the profile is not a profile, the other says
// this profile is not this rig's.
MOTIONSOURCE_API bool ValidateSourceProfile(const SourceProfile& profile,
                                            std::string* reason = nullptr);

// Why a profile does not describe a rig, in terms no format supplies. A caller
// holding both a reader and a profile maps these onto that reader's semantic
// diagnostics; see the header note. The enumerator values are stable, and they
// are *not* the contract a user reads — the reader's codes are.
enum class SourceProfileRefusal : std::uint8_t
{
    None,
    // The profile does not satisfy `ValidateSourceProfile`. `detail` carries its
    // reason verbatim.
    ProfileInvalid,
    // The rig does not satisfy `ValidateSourceSkeleton`. Distinct from the
    // above because a caller with a valid profile and a broken rig has a
    // different thing to fix, and both structs are ones a caller can assemble.
    SkeletonInvalid,
    // The rig does not root at the joint the profile names, so the root policies
    // describe nothing here.
    RootJointMismatch,
    // The rig repeats a name the profile maps. Not resolvable by taking the
    // first: a profile addresses joints by name, and silently binding one of two
    // is how a rig gets half mapped with nobody told.
    AmbiguousJointName,
    // The rig does not carry a joint the profile requires.
    RequiredJointMissing,
    // A bound bone does not sit under its nearest bound humanoid ancestor. The
    // near-miss profile: the names all matched and the body is assembled wrong.
    HierarchyMismatch,
    // The rig carries joints the profile neither maps nor ignores, and the
    // profile's policy is to refuse them.
    UnmappedJointRefused,

    Count,
};

MOTIONSOURCE_API std::string_view SourceProfileRefusalName(
    SourceProfileRefusal refusal) noexcept;

// One bone the profile bound to one joint of the rig.
struct SourceProfileBinding
{
    motion::HumanBone bone = motion::HumanBone::Count;
    std::size_t jointIndex = 0;

    friend bool operator==(const SourceProfileBinding& lhs,
                           const SourceProfileBinding& rhs) noexcept
    {
        return lhs.bone == rhs.bone && lhs.jointIndex == rhs.jointIndex;
    }
    friend bool operator!=(const SourceProfileBinding& lhs,
                           const SourceProfileBinding& rhs) noexcept
    {
        return !(lhs == rhs);
    }
};

// What matching a profile against a rig found. Every vector is filled whatever
// the refusal, because the caller that most needs this is a detector reporting
// on candidates that did not match (roadmap §3.1).
struct SourceProfileMatch
{
    // Defaults to a refusal, not to success: a `SourceProfileMatch` nobody
    // matched has concluded nothing, and a struct whose default state claims a
    // match is a trap for exactly the code paths that forget to check.
    SourceProfileRefusal refusal = SourceProfileRefusal::ProfileInvalid;

    // Plain text naming what was refused — the joint, the bone, the ancestor.
    // Empty when nothing was.
    std::string detail;

    // In the profile's declaration order.
    std::vector<SourceProfileBinding> bound;
    std::vector<motion::HumanBone> missingRequired;
    std::vector<motion::HumanBone> missingOptional;
    // Rig joints the profile neither maps nor ignores, in joint order.
    std::vector<std::size_t> unmappedJoints;
    // Mapped names the rig carries more than once, in the profile's order.
    std::vector<std::string> ambiguousNames;
    // Whether the rig roots at the joint the profile names.
    bool rootMatched = false;

    bool Matched() const noexcept
    {
        return refusal == SourceProfileRefusal::None;
    }

    // The rig joint bound to `bone`, or nullopt when nothing was.
    MOTIONSOURCE_API std::optional<std::size_t> JointFor(
        motion::HumanBone bone) const;
};

// Match `profile` against `skeleton`: bind what the rig carries, and conclude.
//
// The first refusal wins and the order is part of the contract, because a rig
// can fail several of these at once and a test comparing one enumerator needs to
// know which: profile · rig · root · ambiguity · required · hierarchy ·
// unmapped. It runs outermost-first — a profile that is not a profile has
// nothing to say about a rig, and a rig that does not root where the profile
// says is not addressed by any of the checks below it.
//
// No frame is read and none is needed: a profile is matched against a rig
// (SourceSkeleton.h), which is what lets a conversion refuse before decoding a
// clip and lets a detector report on a file it has only skimmed.
MOTIONSOURCE_API SourceProfileMatch MatchSourceProfile(
    const SourceProfile& profile, const SourceSkeleton& skeleton);

} // namespace motionSource
