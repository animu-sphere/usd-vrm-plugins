// SPDX-License-Identifier: Apache-2.0
//
// The retarget's diagnostic namespace (the OpenExec plan's P1-1,
// docs/design/MOTION_CONTRACT.md "Retarget diagnostics").
//
// **Why a code and not a sentence.** The retarget reported in prose until the
// OpenExec foundation compared two implementations of it, and prose was the one
// thing the comparison could not do: the offline tool said "the clip drives
// bones the target rig does not map: upperChest", and `execVrm` said nothing at
// all, because a computation has no channel for a warning a caller of
// `Compute` sees. Two sentences cannot be compared; two lists of codes can. So
// every warning this library raises about a retarget is one of the codes below,
// with the bone or joint it is about as a separate field, and a diagnostic is a
// plain value -- comparable, copyable, and able to cross an exec boundary the
// way a pose does.
//
// **The set is frozen, and it was frozen after its raisers rather than before.**
// The VMC, mocopi and BVH sets were frozen before their decoders so that a code
// would describe the format and not the last bug chased. Here the raisers came
// first -- two bone lists and four prose warnings in `PoseRetargeter` since
// v0.4.0 -- so the freeze is a classification of what already existed, plus the
// three codes the plan named for callers. One code is an amendment to the
// plan's draft list, and it is there because a raiser needed it:
// `InvalidRootJoint`, which the library has warned about since v0.4.0 and which
// no drafted code described.
//
// **The eight split in two, and the split is the layer boundary.** The leading
// five are raised by this library, from plain values. The last three can only
// be raised by a caller that holds a stage or a file system -- a time range the
// clip did not state, an output path that names an input, and a scale this
// library never receives, since `TargetSkeleton` drops a rest's scale when it
// decomposes one. `RetargetDiagnosticIsLibraryRaised` states that boundary in
// code, and `vrmRetarget_boundaries` checks that this library's sources never
// name one of the three.
//
// **What is not here.** The expression and look-at resolvers still report in
// prose (`ExpressionDiagnostics`, `LookAtDiagnostics`); the frozen set is the
// body retarget's, which is what P0-6's parity compares. `VRM_OPENEXEC_*` is a
// separate namespace a *driver* raises around a request, and no library here is
// a driver.
#pragma once

#include "vrmRetarget/api.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vrmRetarget
{

// Values are stable array indices; append only before Count, and only with a
// contract review. The strings, not these spellings, are the contract.
enum class RetargetDiagnosticCode : std::uint8_t
{
    // --- raised by this library: where a clip meets a rig --------------------

    // A bone VRM 1.0 requires has no joint on this rig: the map binds none, or
    // binds an index the rig does not have. Retargeting onto such a rig is
    // legal and useful, which is why this is a warning; doing it silently is
    // not. Subject: the bone. `hips` names a consequence as well, in its
    // detail, when root motion was asked for.
    MissingRequiredBone,
    // The clip drives a bone this rig binds no joint for, so that bone's motion
    // reaches nothing. Subject: the bone.
    UnboundDrivenBone,
    // Two bones are bound to one joint, and the one later in the vocabulary
    // writes over the other. Always a mapping defect. Subject: the joint.
    DuplicateTarget,
    // The rig's joints are not in parent-before-child order, which
    // UsdSkelSkeleton requires. The retarget is still correct joint by joint.
    // Subject: the first joint whose parent follows it.
    InvalidHierarchy,
    // Root motion was asked to land on a joint the rig does not have, so no
    // root translation was authored. Subject: the index asked for.
    InvalidRootJoint,

    // --- raised by a caller: what a stage or a file system adds ---------------

    // A non-unit scale reached a retarget that authors identity scale. Frozen
    // for the scale policy (the OpenExec plan's P1-2), which decides what raises
    // it; nothing does yet.
    NonUnitScale,
    // The clip states no time samples, so the retarget answers one pose at a
    // time the stage chose rather than one the clip stated. Subject: the clip.
    TimeRangeDerived,
    // The output names a layer the retarget read, and writing it would replace
    // an input. Subject: the output path.
    OutputCollidesWithInput,

    Count,
};

inline constexpr std::size_t RetargetDiagnosticCodeCount =
    static_cast<std::size_t>(RetargetDiagnosticCode::Count);

// The library's codes are the leading run, so a range check is the layer check.
inline constexpr std::size_t LibraryRetargetDiagnosticCodeCount =
    static_cast<std::size_t>(RetargetDiagnosticCode::InvalidRootJoint) + 1;

enum class RetargetDiagnosticSeverity : std::uint8_t
{
    Info,
    Warning,
    Error,
};

// The stable string, e.g. "VRM_RETARGET_UNBOUND_DRIVEN_BONE". Empty for a
// value outside the enum, which is what an out-of-range cast produces.
VRMRETARGET_API std::string_view RetargetDiagnosticCodeString(
    RetargetDiagnosticCode code) noexcept;

VRMRETARGET_API std::optional<RetargetDiagnosticCode> FindRetargetDiagnosticCode(
    std::string_view name) noexcept;

VRMRETARGET_API RetargetDiagnosticSeverity RetargetDiagnosticDefaultSeverity(
    RetargetDiagnosticCode code) noexcept;

// Whether a retarget can continue past this code. Everything the library raises
// can: each one names what the rig or the clip did not say and what the result
// did instead. Only a collision cannot, because the answer to it is not to
// write at all.
VRMRETARGET_API bool RetargetDiagnosticIsRecoverable(
    RetargetDiagnosticCode code) noexcept;

// Whether this library may raise the code. A caller holding a stage is on the
// other side of this boundary and may raise either half.
VRMRETARGET_API bool RetargetDiagnosticIsLibraryRaised(
    RetargetDiagnosticCode code) noexcept;

VRMRETARGET_API std::string_view RetargetDiagnosticSeverityString(
    RetargetDiagnosticSeverity severity) noexcept;

// One reported diagnostic.
struct RetargetDiagnostic
{
    RetargetDiagnosticCode code = RetargetDiagnosticCode::MissingRequiredBone;
    RetargetDiagnosticSeverity severity = RetargetDiagnosticSeverity::Warning;
    bool recoverable = true;

    // What the code is about, as plain text: a human bone's VRM name, a joint
    // token, a path. Separate from `detail` because it is what two
    // implementations are compared on -- the sentence around it is not.
    std::string subject;
    std::string detail;
};

// Exact, every field. A diagnostic is a value like the pose it is reported
// beside, and `ExecTypeRegistry::RegisterType` will not register a type it
// cannot compare.
VRMRETARGET_API bool operator==(const RetargetDiagnostic& a,
                                const RetargetDiagnostic& b) noexcept;
VRMRETARGET_API bool operator!=(const RetargetDiagnostic& a,
                                const RetargetDiagnostic& b) noexcept;

// Fills `severity` and `recoverable` from the code's defaults, so a raise site
// cannot report a code differently from the table.
VRMRETARGET_API RetargetDiagnostic MakeRetargetDiagnostic(
    RetargetDiagnosticCode code, std::string subject, std::string detail = {});

// A single deterministic line, stable enough for a golden test to compare:
//
//     [VRM_RETARGET_UNBOUND_DRIVEN_BONE] warning recoverable
//     subject=upperChest: the clip drives it and the target rig binds no joint
//     for it
//
// The field order is fixed, an empty subject or detail is omitted rather than
// printed empty, and `recoverable` is printed only when it is true -- the
// convention of the BVH and live adapters' lines, so the three read alike.
VRMRETARGET_API std::string FormatRetargetDiagnostic(
    const RetargetDiagnostic& diagnostic);

// What a retarget reported, in the order it was raised, each code and subject
// once.
//
// "Once" is the rule rather than a convenience: a bone the clip drives and the
// rig does not bind is one fact about a clip and a rig, however many samples
// carry it, and a list that repeated it per sample would compare unequal for
// two clips that differ only in length.
struct VRMRETARGET_API RetargetDiagnostics
{
    std::vector<RetargetDiagnostic> reported;

    // Appends `diagnostic` unless one with the same code and subject is
    // already reported; returns whether it was appended. The first report
    // wins, detail included.
    bool Report(RetargetDiagnostic diagnostic);

    // Reports each of `other`'s entries in order, under the same rule.
    void Merge(const RetargetDiagnostics& other);

    bool Has(RetargetDiagnosticCode code, std::string_view subject) const;

    // The subjects reported under `code`, in report order.
    std::vector<std::string> Subjects(RetargetDiagnosticCode code) const;

    bool IsClean() const noexcept { return reported.empty(); }
};

// Exact, entry by entry and in order: two retargets that raised the same
// diagnostics in a different order reported differently, and that is a fact a
// comparison should show rather than hide.
VRMRETARGET_API bool operator==(const RetargetDiagnostics& a,
                                const RetargetDiagnostics& b) noexcept;
VRMRETARGET_API bool operator!=(const RetargetDiagnostics& a,
                                const RetargetDiagnostics& b) noexcept;

} // namespace vrmRetarget
