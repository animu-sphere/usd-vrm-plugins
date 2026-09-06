// SPDX-License-Identifier: Apache-2.0
//
// execMotion's OpenExec registration: the canonical value type, and the
// computations this bundle registers.
//
// The registrations live here and nothing else does. Everything a computation
// decides is in ExecMotionPose.cpp, over plain values; this file only marshals
// exec's inputs into those calls and hands the results back. That split is the
// plan's "a node is a thin wrapper" rule made structural rather than intended
// (motion policy §11.4) -- and it is what let this bundle record, rather than
// hide, that `motion.sampleAnimation` has no library call to wrap
// (docs/reports/openusd/26.08-openexec-sampling.md §5).
//
// # Why a registered aggregate
//
// `ExecTypeRegistry::RegisterType` static_asserts `!VtIsArray`, so a pose cannot
// cross a computation boundary as a `VtArray`. It also static_asserts equality
// comparability, which `motion::HumanoidPose` has carried since v0.6.0. So the
// canonical type crosses unchanged, which is the only shape under which a node
// stays a wrapper -- the alternatives dissolve the pose into channels and put
// the joint-ordering contract into masks
// (docs/reports/openusd/26.08-openexec-migration.md §4).
//
// # Why UsdSkelAnimation, and why only that
//
// A computation is registered *for a schema*, and 26.08 lets exactly one plugin
// declare a given schema: a second declarer is a coding error and its
// computations for that schema silently never register. `UsdSkelAnimation` is
// therefore this bundle's, and `execVrm` reaches an animation through an input
// accessor rather than by registering on it (WORKSPACE.md §2).

#include "ExecMotionPose.h"

#include "pxr/pxr.h"

#include "pxr/base/gf/quatf.h"
#include "pxr/base/gf/vec3f.h"
#include "pxr/base/tf/diagnostic.h"
#include "pxr/base/tf/staticTokens.h"
#include "pxr/base/tf/token.h"

#include "pxr/exec/ef/time.h"
#include "pxr/exec/exec/builtinComputations.h"
#include "pxr/exec/exec/registerSchema.h"
#include "pxr/exec/exec/typeRegistry.h"
#include "pxr/exec/vdf/context.h"
#include "pxr/exec/vdf/readIterator.h"

#include "pxr/usd/usd/timeCode.h"

#include <string>
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    // The plan's naming: a computation is `<layer>.<verb>`, and the layer is
    // what may not be a product name.
    ((identityPose, "motion.identityPose"))
    ((sampleAnimation, "motion.sampleAnimation"))
    ((priorPose, "motion.priorPose"))
    ((filterPose, "motion.filterPose"))
    // UsdSkelAnimation's own attributes. Each is declared with its ELEMENT
    // type below and read through an iterator, because an array-valued USD
    // input is boxed into a container of the element type on the way in.
    (joints)
    (rotations)
    (translations)
    // The rate that turns the frame a computation is handed into the seconds a
    // canonical pose is stamped in. It is an attribute on the clip because exec
    // will not deliver the stage metadata that means the same thing: the
    // builder accepts `Stage().Metadata<double>(timeCodesPerSecond)`, does not
    // refuse it with `.Required()`, and still yields no value at evaluation
    // (docs/reports/openusd/26.08-openexec-mechanism.md §5). This input is the
    // shim for that gap and is meant to go away when it closes.
    ((timeCodesPerSecond, "motion:timeCodesPerSecond"))
    // What a clip states about how it wants to be smoothed. All three are
    // optional and an absent one keeps `motion::PoseFilter`'s own default --
    // see ExecMotionPose.h for why these are defaulted where the rate above is
    // refused.
    ((cutoffHz, "motion:filter:cutoffHz"))
    ((filterRootPosition, "motion:filter:rootPosition"))
    ((filterRootOrientation, "motion:filter:rootOrientation"))
);

TF_REGISTRY_FUNCTION(ExecTypeRegistry)
{
    ExecTypeRegistry::RegisterType(motion::HumanoidPose{});
}

PXR_NAMESPACE_CLOSE_SCOPE

EXEC_REGISTER_COMPUTATIONS_FOR_SCHEMA(UsdSkelAnimation)
{
    self.PrimComputation(_tokens->identityPose)
        .Callback<motion::HumanoidPose>(+[](const VdfContext &ctx) {
            std::vector<std::string> jointPaths;
            VdfReadIterator<TfToken> joint(ctx, _tokens->joints);
            jointPaths.reserve(joint.ComputeSize());
            for (; !joint.IsAtEnd(); ++joint) {
                jointPaths.push_back(joint->GetString());
            }

            // Every input the callback reads arrives through .Inputs() below.
            // A value read from anywhere else -- a clock, a global, a captured
            // reference -- is invisible to invalidation and becomes a stale
            // result no test would see (exec's own "cache safe" contract). This
            // callback reads exactly one thing, and it is declared there.
            //
            // The pose carries NO timestamp, and that is a measurement rather
            // than an omission. A time code is a FRAME; `HumanoidPose::timestamp`
            // is SECONDS; converting needs the stage's `timeCodesPerSecond`, and
            // a computation cannot reach it. `Stage().Metadata<double>()` for
            // that field is accepted by the builder, is not refused even with
            // `.Required()`, and still delivers no value -- so the only rate a
            // callback could apply would be a guess, and a guessed rate is a
            // wrong second that every consumer downstream would take at face
            // value. `tools/motionRetarget` converts with the rate because it
            // holds the stage; exec does not, which is why
            // `motion.sampleAnimation` below is *given* one, as an attribute
            // the clip states
            // (docs/reports/openusd/26.08-openexec-mechanism.md §5).
            //
            // The identity pose is the same pose at every time, so this
            // computation declares no time input either: reading `computeTime`
            // and discarding it would tell exec this value changes with the
            // frame, which is a false statement about a computation whose only
            // input is a `uniform` array.
            return execmotion::IdentityPoseForJoints(jointPaths);
        })
        .Inputs(
            AttributeValue<TfToken>(_tokens->joints).Required());

    // -----------------------------------------------------------------------
    // motion.sampleAnimation -- the clip, at the frame the system is evaluating
    // -----------------------------------------------------------------------
    //
    // The first computation with an algorithm behind it, and the first with a
    // time dependency: `rotations` and `translations` are time-sampled, so exec
    // resolves them at the evaluated frame and reports this value key to a
    // request's time callback when the frame moves. `motion.identityPose` reads
    // one `uniform` array and is reported to neither, which is the contrast the
    // suites assert in both directions.
    //
    // **Nothing here interpolates.** A frame between two keys is resolved by
    // USD, not by this bundle, which is why this node is not a wrapper over
    // `motion::SampleAnimation` -- that function is handed a whole
    // `HumanoidAnimation` and performs its own lookup, and an exec input arrives
    // already resolved at one time. The two answers are compared at P0-6 rather
    // than assumed equal.
    self.PrimComputation(_tokens->sampleAnimation)
        .Callback<motion::HumanoidPose>(+[](const VdfContext &ctx) {
            execmotion::ClipSample sample;

            VdfReadIterator<TfToken> joint(ctx, _tokens->joints);
            sample.jointPaths.reserve(joint.ComputeSize());
            for (; !joint.IsAtEnd(); ++joint) {
                sample.jointPaths.push_back(joint->GetString());
            }

            // Neither of the next two is `.Required()`, so an unconnected input
            // is an iterator already at its end rather than an error -- which is
            // how a clip that authors no translations reaches the seam as a pose
            // with no root position instead of as a failure. The seam judges the
            // two arrays separately, for the same reason.
            VdfReadIterator<GfQuatf> rotation(ctx, _tokens->rotations);
            sample.rotations.reserve(rotation.ComputeSize());
            for (; !rotation.IsAtEnd(); ++rotation) {
                sample.rotations.push_back(*rotation);
            }

            VdfReadIterator<GfVec3f> translation(ctx, _tokens->translations);
            sample.translations.reserve(translation.ComputeSize());
            for (; !translation.IsAtEnd(); ++translation) {
                sample.translations.push_back(*translation);
            }

            if (const double *const rate =
                    ctx.GetInputValuePtr<double>(_tokens->timeCodesPerSecond)) {
                sample.timeCodesPerSecond = *rate;
            }

            // `EfTime` carries a `UsdTimeCode`, and `GetValue()` is a coding
            // error on the default one -- which is what a system evaluates at
            // until `ChangeTime` is called, so it is the common case and not an
            // edge one. `IsNumeric()` is the check that has to come first.
            const UsdTimeCode timeCode =
                ctx.GetInputValue<EfTime>(
                    ExecBuiltinComputations->computeTime).GetTimeCode();
            if (timeCode.IsNumeric()) {
                sample.timeCode = timeCode.GetValue();
                sample.hasTimeCode = true;
            }

            if (std::optional<motion::HumanoidPose> pose =
                    execmotion::PoseFromClipSample(sample)) {
                return *pose;
            }

            // The seam refuses exactly one thing, and this is it: without a
            // positive rate the frame cannot become the second the canonical
            // pose is stamped in. Reported rather than defaulted, because a
            // pose carrying a guessed second is indistinguishable downstream
            // from one carrying a measured one, and an empty pose is not.
            TF_RUNTIME_ERROR(
                "motion.sampleAnimation: the clip states no usable "
                "'motion:timeCodesPerSecond', so the frame it is evaluated at "
                "cannot be converted to seconds; no pose was sampled");
            return motion::HumanoidPose{};
        })
        .Inputs(
            AttributeValue<TfToken>(_tokens->joints).Required(),
            AttributeValue<GfQuatf>(_tokens->rotations),
            AttributeValue<GfVec3f>(_tokens->translations),
            AttributeValue<double>(_tokens->timeCodesPerSecond).Required(),
            Stage().Computation<EfTime>(
                ExecBuiltinComputations->computeTime).Required());

    // -----------------------------------------------------------------------
    // motion.priorPose -- the pose a filter step starts from
    // -----------------------------------------------------------------------
    //
    // The value is the clip's own pose at the evaluated frame, forwarded
    // unchanged, and the node exists for what a *caller* can put in its place:
    // it is the value key `ExecUsdSystem::ComputeWithOverrides` is aimed at.
    //
    // A filter is a recurrence -- this frame's answer is a function of the last
    // one -- and OpenExec hands a callback exactly one time with no way to
    // reach another (the sampling report section 5). The two ways to hold the
    // missing half inside the graph are both refused by the purity rule: a
    // static in the callback is mutable state invalidation cannot see, and a
    // second attribute stating "the previous pose" would put a derived value
    // into the scene. So the recurrence stays with whoever drives the graph --
    // which is where it already lives for a live source, in `motionRuntime`'s
    // pose buffer -- and reaches exec as an override on this key.
    //
    // Forwarding, rather than a second copy of the sampling callback: the two
    // value keys must be distinct so an override can name one of them, but the
    // behaviour must not be. Un-overridden, `motion.filterPose` therefore
    // smooths the clip against itself, which `motion::PoseFilter` answers by
    // returning it unchanged -- the pass-through falls out of `dt == 0` rather
    // than being special-cased anywhere in this bundle.
    self.PrimComputation(_tokens->priorPose)
        .Callback<motion::HumanoidPose>(+[](const VdfContext &ctx) {
            const motion::HumanoidPose *const pose =
                ctx.GetInputValuePtr<motion::HumanoidPose>(
                    _tokens->sampleAnimation);
            return pose ? *pose : motion::HumanoidPose{};
        })
        .Inputs(
            Computation<motion::HumanoidPose>(
                _tokens->sampleAnimation).Required());

    // -----------------------------------------------------------------------
    // motion.filterPose -- one step of motion::PoseFilter
    // -----------------------------------------------------------------------
    //
    // The first node in this bundle that wraps `motionRuntime`, and the first
    // that consumes another computation's result rather than an attribute --
    // the plan's chain (SampleAnimation -> FilterPose -> ...) as two links.
    //
    // It declares no `computeTime`, and that is a decision rather than an
    // oversight: it does not use the frame, and a node that declares
    // `computeTime` is recomputed on every frame change even when nothing it
    // reads has moved (the sampling report section 3). What time this pose
    // belongs to arrives inside the poses themselves, as the seconds
    // `motion.sampleAnimation` stamped -- and those are what the filter's step
    // weight is derived from, which is the whole reason the rate had to enter
    // the graph one node earlier.
    self.PrimComputation(_tokens->filterPose)
        .Callback<motion::HumanoidPose>(+[](const VdfContext &ctx) {
            const motion::HumanoidPose *const pose =
                ctx.GetInputValuePtr<motion::HumanoidPose>(
                    _tokens->sampleAnimation);
            if (!pose) {
                // Required inputs are not guaranteed to arrive with a value
                // (the sampling report section 2), so the one thing this node
                // cannot compute without is checked rather than assumed. It
                // has no other refusal: an absent policy is the library's
                // default, and an absent prior pose is the pose itself.
                TF_RUNTIME_ERROR(
                    "motion.filterPose: no pose came back from "
                    "motion.sampleAnimation, so there is nothing to filter");
                return motion::HumanoidPose{};
            }

            const motion::HumanoidPose *const prior =
                ctx.GetInputValuePtr<motion::HumanoidPose>(_tokens->priorPose);

            execmotion::FilterPolicy policy;
            if (const float *const cutoff =
                    ctx.GetInputValuePtr<float>(_tokens->cutoffHz)) {
                policy.cutoffHz = *cutoff;
            }
            if (const bool *const root =
                    ctx.GetInputValuePtr<bool>(_tokens->filterRootPosition)) {
                policy.filterRootPosition = *root;
            }
            if (const bool *const root =
                    ctx.GetInputValuePtr<bool>(
                        _tokens->filterRootOrientation)) {
                policy.filterRootOrientation = *root;
            }

            return execmotion::FilteredPose(prior ? *prior : *pose, *pose,
                                            policy);
        })
        .Inputs(
            Computation<motion::HumanoidPose>(
                _tokens->sampleAnimation).Required(),
            Computation<motion::HumanoidPose>(_tokens->priorPose).Required(),
            AttributeValue<float>(_tokens->cutoffHz),
            AttributeValue<bool>(_tokens->filterRootPosition),
            AttributeValue<bool>(_tokens->filterRootOrientation));
}
