// SPDX-License-Identifier: Apache-2.0
//
// The poses this bundle's computations produce, over plain values.
//
// This is the bundle's project-owned seam and it takes **plain values**: joint
// paths, rotation and translation arrays, a frame and a rate -- never a
// `VdfContext` and never a stage. Marshalling exec's inputs into these arguments
// is the registration TU's job (motion policy §11.4: a computation is a thin
// wrapper over a library that does not know exec exists). Keeping the seam free
// of exec means it is testable with no stage, no system, and no request -- and
// that a failure in the mechanism cannot be mistaken for a failure in the value.
#pragma once

#include <motionCore/Humanoid.h>
#include <motionRuntime/Filter.h>
#include <motionRuntime/LiveCaptureSource.h>
#include <motionRuntime/MotionSource.h>

#include "pxr/base/gf/quatf.h"
#include "pxr/base/gf/vec3f.h"

#include <optional>
#include <string_view>
#include <string>
#include <vector>

namespace execmotion {

/// The bone a `UsdSkelAnimation` joint path names, or nullopt.
///
/// A joint path is `UsdSkelAnimation`'s own spelling, e.g. `hips/spine/chest`,
/// of which only the last segment is a bone name (`motion::HumanBoneJointPath`
/// authors the same shape). A path whose leaf names no canonical bone
/// contributes nothing and is not an error: this layer reports what it
/// recognized, and whoever knows which clip it is decides whether a gap matters.
///
/// Exposed for the tests, which check the leaf-segment rule directly rather than
/// through a pose.
std::optional<motion::HumanBone> BoneForJointPath(const std::string& jointPath);

/// The identity pose for `jointPaths`.
///
/// Every rotation is identity and `validRotations` carries exactly the bones the
/// joint paths name.
///
/// The pose's `timestamp` is left at zero, and the caller does not get to pass
/// one. This computation is the same pose at every frame, so it declares no time
/// input at all (see ExecMotionRegistration.cpp).
///
/// No sampling, no interpolation, no retarget. This is the identity, and it is
/// what makes the first OpenExec computation attributable: a wrong result is a
/// wrong mechanism, because there is no algorithm to blame.
motion::HumanoidPose IdentityPoseForJoints(
    const std::vector<std::string>& jointPaths);

/// What a `UsdSkelAnimation` states at one instant, as plain values.
///
/// `rotations` and `translations` are already resolved **at** `timeCode`: exec
/// resolves a time-sampled attribute input at the time the computation is
/// evaluated at, so this layer interpolates nothing and holds nothing. That is
/// the one behavioural difference from `motion::SampleAnimation`, which is
/// handed a whole `HumanoidAnimation` and does its own hold-at-the-edges lookup;
/// which of the two answers a frame between keys is USD's question here and
/// `motionRuntime`'s there, and P0-6 parity is where the two get compared.
struct ClipSample
{
    /// `UsdSkelAnimation`'s `joints`, in the order the clip authored them.
    std::vector<std::string> jointPaths;

    /// `rotations` and `translations` at `timeCode`. An array whose length
    /// disagrees with `jointPaths` contributes nothing, because a clip that
    /// cannot say which joint a value belongs to has not said it -- the same
    /// rule the offline reader applies (tools/motionRetarget StageIo.cpp).
    std::vector<pxr::GfQuatf> rotations;
    std::vector<pxr::GfVec3f> translations;

    /// The frame this sample was resolved at. `hasTimeCode` is false for the
    /// **default** time code, which is what an exec system evaluates at until
    /// `ChangeTime` is called -- so it is the common case rather than an edge
    /// one, and `UsdTimeCode::GetValue()` is a coding error on it.
    double timeCode = 0.0;
    bool hasTimeCode = false;

    /// The rate that turns `timeCode` into the seconds `HumanoidPose::timestamp`
    /// is expressed in. It is an authored input rather than stage metadata
    /// because a computation cannot reach `timeCodesPerSecond`
    /// (docs/reports/openusd/26.08-openexec-mechanism.md §5).
    double timeCodesPerSecond = 0.0;
};

/// The pose `sample` states, or nullopt when it cannot be stamped.
///
/// Returns nullopt for a non-positive `timeCodesPerSecond`, which covers both an
/// absent rate and a nonsense one. That is a refusal rather than a fallback on
/// purpose: `HumanoidPose::timestamp` is a plain double with no absent state, so
/// a pose produced without a rate would carry a second every consumer downstream
/// would take at face value, and there is no value of the field that spells
/// "unknown". A clip whose rate is missing is a clip this layer will not sample.
///
/// Everything else is a partial answer rather than a refusal, because a clip is
/// allowed to be sparse: a joint path naming no canonical bone contributes
/// nothing, an array whose length disagrees with `joints` contributes nothing,
/// and a clip that authors no translations produces a pose with no root
/// position. Only `hips` carries body translation (motion contract); the rest of
/// a `translations` array is rest-pose data a retargeter re-derives per rig.
std::optional<motion::HumanoidPose> PoseFromClipSample(const ClipSample& sample);

/// What a clip states about how it wants to be smoothed.
///
/// Every field is optional and an absent one is **not** a value this bundle
/// picks: it is left at `motion::PoseFilter::Options`' own default, because a
/// wrapper that supplied its own default would be a second policy sitting on
/// top of the library's, and a clip authoring nothing would then be smoothed
/// differently here than by the same library called anywhere else.
///
/// That is a different judgement from the sampling rate, and the difference is
/// what each absent value costs. A missing rate produces a *number* --
/// a `timestamp` in seconds -- that no consumer can tell from a measured one.
/// A missing cutoff selects the library's documented behaviour, which every
/// caller of `motion::PoseFilter` already gets. So the rate is refused and
/// these are defaulted (the sampling report's "every node owes its own
/// refusal" applies to what a node cannot compute without, and this one can).
struct FilterPolicy
{
    /// `motion:filter:cutoffHz`. Non-positive disables smoothing, which is
    /// `motion::PoseFilter`'s own documented pass-through and not a special
    /// case this layer added.
    std::optional<float> cutoffHz;

    /// `motion:filter:rootPosition` / `motion:filter:rootOrientation`.
    ///
    /// The orientation flag is inert over a clip-sourced pose and is carried
    /// anyway: `PoseFromClipSample` never sets `root.hasOrientation`, because a
    /// `UsdSkelAnimation` states rotations per joint and no separate root
    /// orientation, and `PoseFilter` skips a field the pose does not carry. It
    /// is here because `Options` has it, and a wrapper does not get to drop a
    /// field of the thing it wraps -- a pose reaching this node from a live
    /// source (P0-4's later inputs) does carry one.
    std::optional<bool> filterRootPosition;
    std::optional<bool> filterRootOrientation;
};

/// `pose` smoothed against `prior`, under `policy`.
///
/// One step of `motion::PoseFilter`, and the state it needs is passed in rather
/// than kept. That is forced rather than chosen: an OpenExec callback is handed
/// exactly one time and no way to reach another
/// ([the sampling report](../../../docs/reports/openusd/26.08-openexec-sampling.md) §5),
/// and a filter that remembered the last pose in a static would be the mutable
/// state the purity rule forbids and invalidation cannot see. So the recurrence
/// -- "the prior pose is the previous frame's answer" -- belongs to whoever
/// drives the graph, which is where it already lives for a live source:
/// `motionRuntime`'s pose buffer.
///
/// Seeding is the library's own: a `PoseFilter` with no state returns its first
/// pose unchanged and keeps it, so `prior` costs one `Apply` and no special
/// case. Two consequences fall out rather than being written: passing the same
/// pose as both arguments returns it unchanged (`dt` is zero), and so does a
/// `prior` stamped at or after `pose` -- a seek backwards is a reseed, exactly
/// as it is for a streamed source.
///
/// **What the round trip through a pose costs, measured rather than assumed.**
/// `PoseFilter` retains a state strictly richer than the pose it returns: a bone
/// a pose does not report keeps its stored rotation in the *state* and stays out
/// of the *result*, so a brief dropout does not restart that bone's history.
/// Only a result can travel back in as the next `prior`, so that retained half
/// does not survive the trip, and a bone returning after a missing frame is
/// passed through here where the streaming filter would slerp it -- 45 degrees
/// against 23.8 in the case `execMotion_pose` pins, in both directions.
///
/// It costs nothing for a clip, whose `joints` are `uniform` so no bone ever
/// drops out, and it is real for a live source, which is what this node is
/// aimed at. It is also the sharpened half of this bundle's ask on
/// `motionRuntime`: a one-step entry point has to hand back the *state* as well
/// as the result, or a caller cannot carry the history that makes a dropout
/// survivable. Reproducing that carry-forward rule here instead would be the
/// second algorithm the wrapper rule forbids, so the difference is recorded
/// (P0-6 parity compares the two).
motion::HumanoidPose FilteredPose(const motion::HumanoidPose& prior,
                                  const motion::HumanoidPose& pose,
                                  const FilterPolicy& policy);

/// What a clip states about how its root is taken in.
///
/// The vocabulary is the library's -- `motion::RootMotionIntake`, the same enum
/// a live session configures `motionRuntime` with -- rather than a second one
/// spelled for exec. A wrapper that named its own policies would be a wrapper
/// over a contract of its own making.
///
/// Absent means the library's own default, the way `FilterPolicy`'s fields do,
/// and for the same reason: `LiveCaptureConfig::rootMotion` already answers this
/// for every other caller and a default invented here would answer it
/// differently. The default is *read from* `LiveCaptureConfig` rather than
/// restated (see RootMotionFrom), so the day the library moves it, this bundle
/// moves with it.
struct RootPolicy
{
    /// `motion:root:intake`. Nullopt is `LiveCaptureConfig`'s own default.
    std::optional<motion::RootMotionIntake> intake;
};

/// The intake policy `token` names, or nullopt for a token this layer does not
/// recognize.
///
/// Absent and unrecognized are different answers, and this is where the
/// difference is drawn: an absent attribute is a clip that said nothing and gets
/// the library's default; a token that spells no policy is a clip that *stated*
/// something this layer cannot honour, and the caller refuses it. Falling back
/// to the default there would give a clip asking for `Ignore` -- misspelled --
/// the root motion it asked not to have.
///
/// The spellings are the enum's own names in lowerCamelCase, which is what a
/// USD token attribute reads like: `passthrough`, `ignore`, `deriveVelocity`.
std::optional<motion::RootMotionIntake> RootIntakeForToken(
    std::string_view token);

/// The root motion `pose` states, under `policy`, given the pose before it.
///
/// `Ignore` yields a default-constructed `motion::RootMotion` -- every presence
/// flag clear, which is what "this clip's placement is not the capture's to
/// decide" looks like downstream. `Passthrough` yields the pose's own root
/// unchanged. `DeriveVelocity` is `Passthrough` plus one thing: when the pose
/// carries a position and no linear velocity and `prior` carries a position, the
/// velocity is the distance between the two over the seconds between them.
///
/// `prior` is the same value `motion.filterPose` takes, and it reaches this
/// function under the same rule: a computation is handed one instant and a
/// velocity needs two, so the previous frame's answer is supplied by whoever
/// drives the graph. Un-overridden it is the pose itself, the elapsed time is
/// zero, and no velocity is derived -- the pose passes through, which is the
/// same shape a zero-length filter step has and is not special-cased here
/// either.
///
/// **This node is the plan's third "not a wrapper" finding.** The rule it
/// applies is `motionRuntime`'s, written down in the motion contract and
/// implemented in `LiveCaptureSource::_Condition` -- a **private** method of a
/// class that is a capture *session*: it owns a pose buffer, a filter, held-bone
/// state and statistics, and it refuses a frame whose timestamp does not
/// increase. So there is no call to make. The seed-then-step idiom
/// `FilteredPose` uses was tried first and does not transfer: pushing `prior`
/// and then `pose` into a local `LiveCaptureSource` gives the wrong answer in
/// this bundle's *ordinary* case, because two poses at the same instant are one
/// accepted frame and one refusal, and the buffer head is then the prior rather
/// than the pose. `PoseFilter` reseeds where `LiveCaptureSource` refuses, and
/// that difference is what decides it.
///
/// So the three lines are here, matched condition for condition to the library's
/// and asserted against the definition rather than against the library, and the
/// ask goes to [boundary consolidation](../../../docs/roadmap/boundary-consolidation.md):
/// a stateless `ConditionRootMotion(prior, pose, intake)` free function beside
/// the session class, so the rule has one implementation again.
motion::RootMotion RootMotionFrom(const motion::HumanoidPose& prior,
                                  const motion::HumanoidPose& pose,
                                  const RootPolicy& policy);

/// The history a driver's pose buffer holds, when no driver supplies one: the
/// pose at the evaluated instant, as a one-sample `motion::HumanoidAnimation`.
///
/// It is `motion.poseHistory`'s ordinary value and it exists for the same reason
/// `motion.priorPose`'s does -- to be *replaced*. A computation evaluates an
/// immutable snapshot and never reaches for one (motion policy §11.4), so a live
/// source's buffered samples reach the graph the one way a value the scene does
/// not state can: as an override on a value key.
///
/// The span is the one instant, `startTime == endTime == pose.timestamp`, because
/// that is what a history of one sample spans. `nominalFrameRate` is left at the
/// library's own default rather than set: a single sample has no rate to state,
/// and the node that reads this value does not use one.
motion::HumanoidAnimation HistoryOfOne(const motion::HumanoidPose& pose);

/// What `history` states at `seconds`, or nullopt when it cannot be sampled.
///
/// **The whole node, and it is a wrapper**: `motion::ClipSource`, constructed
/// over the history, asked `Sample(seconds)`. That is `IMotionSource`'s one
/// question -- "what is the pose at this evaluation time?" -- asked of the
/// implementation that serves a finished animation, which is what a snapshot is
/// once it has been taken. So every rule in the answer is the library's:
/// bracketing samples are interpolated by `motion::LerpPose` (a missing bone held,
/// never faded), a time outside the history holds the nearer boundary, and the
/// pose comes back stamped at `seconds`, on the consumer's clock.
///
/// The result is the library's `motion::PoseSampleResult` and not a bare pose,
/// because the status is part of the answer (motion contract, live-capture
/// semantics). The answer is stamped at the evaluated instant *whether or not*
/// the history reached it, so a pose alone cannot say whether it was sampled or
/// held -- and a source that has stopped delivering keeps answering `Held`
/// forever, which a consumer holding only the pose would read as live. A wrapper
/// does not get to drop a field of the thing it wraps.
///
/// An **empty** history is an answer, not a refusal: the library's
/// `Unavailable`, carrying no pose. The bundle refuses where an answer would be
/// indistinguishable from one it measured, and this type has an absent state of
/// its own, so there is nothing to refuse -- the first result in the bundle for
/// which that is true.
///
/// The one refusal is a history whose timestamps **decrease** somewhere.
/// `motion::SampleAnimation`, which `ClipSource` samples through, binary-searches
/// the samples and so relies on their being in time order -- a precondition it
/// neither states nor checks -- and a history out of order would answer with a
/// bracket nobody measured. Repeated timestamps are *not* refused: the library's
/// search answers them deterministically, landing on the first of the pair, and
/// a check stricter than the library's own need would be a policy of this
/// bundle's -- `motion::PoseBuffer::Push`'s strictly-increasing rule is a
/// property of how a buffer is *filled*, not of what can be sampled.
std::optional<motion::PoseSampleResult> SampleHistory(
    const motion::HumanoidAnimation& history, double seconds);

} // namespace execmotion
