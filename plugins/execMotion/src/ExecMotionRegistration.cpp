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
// # How a computation here refuses
//
// **By setting no value at all**, with `VdfContext::SetEmptyOutput`, after
// posting a `TF_RUNTIME_ERROR` that names the computation. Never by returning a
// default-constructed result.
//
// The rule is one rule and it is the same one the rate's refusal was written
// for: an answer nobody can tell from a refusal is worse than no answer. A
// default-constructed result fails that test for every type this bundle
// produces -- an empty `motion::HumanoidPose` is what a clip whose `joints` name
// no canonical bone legitimately samples to, and a cleared `motion::RootMotion`
// is `motion:root:intake = "ignore"`'s own answer, bit for bit. So a refusal
// that produced one would hand a misspelled `passthrough` the exact behaviour of
// a deliberate `ignore`, for any consumer not inspecting `TfError`s. The same
// holds for `motion::PoseSampleResult`, whose default is `Unavailable` -- the
// library's answer for a history that holds nothing, which is a legitimate
// answer and not the same statement as "this history cannot be sampled".
//
// An empty value is the one thing no computation here ever produces as an
// answer, so it is the only shape a refusal can take and stay distinguishable.
// It costs the value-returning callback form: a callback that may refuse takes
// `const VdfContext&` and returns void, calling `SetOutput` on every path that
// has an answer. `motion.identityPose` keeps the returning form, because it has
// no refusal to express -- the two forms coexist in one bundle, which is itself
// measured.
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

#include "pxr/usd/sdf/path.h"
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
    ((extractRootMotion, "motion.extractRootMotion"))
    ((poseHistory, "motion.poseHistory"))
    ((interpolatePose, "motion.interpolatePose"))
    ((blendPoses, "motion.blendPoses"))
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
    // What a clip states about how its root is taken in: one of
    // `motion::RootMotionIntake`'s three policies, spelled in lowerCamelCase.
    // Absent is the library's own default; a token naming no policy is refused
    // (ExecMotionPose.h, RootIntakeForToken).
    ((rootIntake, "motion:root:intake"))
    // What a blend states: the clips it blends, as a relationship, and one
    // weight per target, in target order. A relationship rather than
    // connections because fan-in is what a relationship carries in 26.08, and
    // `computeValue` over two connections silently falls back to the
    // attribute's own value (the migration audit section 5.1).
    ((blendSources, "motion:blend:sources"))
    ((blendWeights, "motion:blend:weights"))
    // The two names the fan-in arrives under. Both inputs traverse the same
    // relationship, so each is named for what it carries rather than left at
    // the computation name it reads.
    ((sourcePaths, "motion:blend:sourcePaths"))
    ((sourcePoses, "motion:blend:sourcePoses"))
);

TF_REGISTRY_FUNCTION(ExecTypeRegistry)
{
    // Four registered types, each for the same reason as the first: it is what
    // a computation here produces, it is the library's value rather than a
    // shape invented here, and it satisfies the registry's two requirements --
    // not a `VtArray`, and equality comparable.
    //
    //   * `motion::RootMotion` is what `motion.extractRootMotion` produces;
    //   * `motion::HumanoidAnimation` is the snapshot `motion.poseHistory`
    //     carries, and what a driver overrides it with -- a history of samples
    //     crosses as the canonical clip aggregate, whose `std::vector` of poses
    //     is not a `VtArray` and so is not refused;
    //   * `motion::PoseSampleResult` is what `motion.interpolatePose` produces,
    //     and the one type here that is `motionRuntime`'s rather than
    //     `motionCore`'s. It is the only one that needed a change to register:
    //     it had no `operator==` until this node asked for one, the same ask
    //     `motionCore`'s aggregates answered in v0.6.0.
    ExecTypeRegistry::RegisterType(motion::HumanoidPose{});
    ExecTypeRegistry::RegisterType(motion::RootMotion{});
    ExecTypeRegistry::RegisterType(motion::HumanoidAnimation{});
    ExecTypeRegistry::RegisterType(motion::PoseSampleResult{});
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
                ctx.SetOutput(*pose);
                return;
            }

            // The seam refuses exactly one thing, and this is it: without a
            // positive rate the frame cannot become the second the canonical
            // pose is stamped in. Reported rather than defaulted, because a
            // pose carrying a guessed second is indistinguishable downstream
            // from one carrying a measured one.
            //
            // And the refusal sets NO value rather than a default-constructed
            // one, which is this bundle's one refusal shape (see the block
            // above the registrations). An empty pose is a pose a clip can
            // legitimately produce -- one whose `joints` name no canonical bone
            // does -- so returning one would make a refusal indistinguishable
            // from an answer for anyone not reading TfErrors.
            TF_RUNTIME_ERROR(
                "motion.sampleAnimation: the clip states no usable "
                "'motion:timeCodesPerSecond', so the frame it is evaluated at "
                "cannot be converted to seconds; no pose was sampled");
            ctx.SetEmptyOutput();
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
            if (!pose) {
                // The node it forwards refused, and forwarding means forwarding
                // that too: a default pose here would turn one node's refusal
                // into another node's answer, and would be the value an
                // override is compared against besides.
                ctx.SetEmptyOutput();
                return;
            }
            ctx.SetOutput(*pose);
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
                ctx.SetEmptyOutput();
                return;
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

            ctx.SetOutput(
                execmotion::FilteredPose(prior ? *prior : *pose, *pose,
                                         policy));
        })
        .Inputs(
            Computation<motion::HumanoidPose>(
                _tokens->sampleAnimation).Required(),
            Computation<motion::HumanoidPose>(_tokens->priorPose).Required(),
            AttributeValue<float>(_tokens->cutoffHz),
            AttributeValue<bool>(_tokens->filterRootPosition),
            AttributeValue<bool>(_tokens->filterRootOrientation));

    // -----------------------------------------------------------------------
    // motion.extractRootMotion -- where the body is, under the clip's policy
    // -----------------------------------------------------------------------
    //
    // The first computation that produces something other than a pose, and the
    // reason the bundle registers a second value type: a `motion::RootMotion`
    // is what a consumer of body placement wants, and dissolving it into
    // channels would put the presence flags into masks the way the migration
    // report's rejected pose shapes did.
    //
    // It reads `motion.sampleAnimation` rather than `motion.filterPose`, and
    // that is the library's ordering rather than a preference:
    // `LiveCaptureSource` conditions the root of the frame as it *arrived* and
    // smooths afterwards, so a node deriving its velocity from a filtered
    // position would answer a different question from the one `motionRuntime`
    // answers, and P0-6 parity would have to explain the difference rather than
    // measure it.
    //
    // The prior pose is the same `motion.priorPose` the filter takes, and it is
    // the same override a driver already sets: one substituted value per frame
    // feeds both nodes, because a velocity and a filter step want the identical
    // thing -- the previous frame's answer.
    self.PrimComputation(_tokens->extractRootMotion)
        .Callback<motion::RootMotion>(+[](const VdfContext &ctx) {
            const motion::HumanoidPose *const pose =
                ctx.GetInputValuePtr<motion::HumanoidPose>(
                    _tokens->sampleAnimation);
            if (!pose) {
                // The one thing this node cannot compute without, checked
                // rather than assumed: a `.Required()` input is not guaranteed
                // to arrive with a value (the sampling report section 2).
                TF_RUNTIME_ERROR(
                    "motion.extractRootMotion: no pose came back from "
                    "motion.sampleAnimation, so there is no root to extract");
                ctx.SetEmptyOutput();
                return;
            }

            execmotion::RootPolicy policy;
            if (const TfToken *const stated =
                    ctx.GetInputValuePtr<TfToken>(_tokens->rootIntake)) {
                policy.intake =
                    execmotion::RootIntakeForToken(stated->GetString());
                if (!policy.intake) {
                    // Absent and unrecognized are different answers. An absent
                    // attribute is a clip that said nothing and gets the
                    // library's default; a token spelling no policy is a clip
                    // that stated something this bundle cannot honour, and
                    // defaulting there would hand a misspelled `Ignore` the
                    // root motion it asked not to have.
                    //
                    // And this is the node where setting NO value rather than a
                    // cleared one is load-bearing rather than tidy: a cleared
                    // `motion::RootMotion` is `ignore`'s own legitimate answer,
                    // bit for bit, so a refusal that produced one would hand a
                    // misspelled `passthrough` the exact behaviour of a
                    // deliberate `ignore` -- the mirror image of the mistake
                    // the paragraph above refuses to make.
                    TF_RUNTIME_ERROR(
                        "motion.extractRootMotion: the clip states "
                        "'motion:root:intake' = '%s', which names no "
                        "motion::RootMotionIntake policy; no root motion was "
                        "extracted",
                        stated->GetText());
                    ctx.SetEmptyOutput();
                    return;
                }
            }

            const motion::HumanoidPose *const prior =
                ctx.GetInputValuePtr<motion::HumanoidPose>(_tokens->priorPose);

            ctx.SetOutput(
                execmotion::RootMotionFrom(prior ? *prior : *pose, *pose,
                                           policy));
        })
        .Inputs(
            Computation<motion::HumanoidPose>(
                _tokens->sampleAnimation).Required(),
            Computation<motion::HumanoidPose>(_tokens->priorPose).Required(),
            AttributeValue<TfToken>(_tokens->rootIntake));

    // -----------------------------------------------------------------------
    // motion.poseHistory -- the snapshot a driver's pose buffer supplies
    // -----------------------------------------------------------------------
    //
    // The second value key in this bundle whose purpose is to be replaced, and
    // a different kind of thing from the first. `motion.priorPose` is the
    // graph's own previous *answer*, fed back; this is the source's *input*,
    // handed in -- the "immutable snapshot" motion policy §11.4 puts between a
    // live source's buffer and every computation. A computation never reaches
    // for a buffer, so the buffer's contents reach the graph the one way a
    // value the scene does not state can: `ComputeWithOverrides`.
    //
    // Un-overridden it is the clip's own pose at the evaluated frame as a
    // history of one sample, which makes `motion.interpolatePose` answer that
    // pose -- sampled at its own instant, no bracket and no hold. The same
    // pass-through `motion.filterPose` has un-overridden, and special-cased in
    // neither.
    self.PrimComputation(_tokens->poseHistory)
        .Callback<motion::HumanoidAnimation>(+[](const VdfContext &ctx) {
            const motion::HumanoidPose *const pose =
                ctx.GetInputValuePtr<motion::HumanoidPose>(
                    _tokens->sampleAnimation);
            if (!pose) {
                // Forwarding the refusal, as `motion.priorPose` does: a history
                // of one default pose would turn the sampler's refusal into a
                // sample, and an empty history would turn it into the library's
                // `Unavailable` -- an answer, and the wrong one.
                ctx.SetEmptyOutput();
                return;
            }
            ctx.SetOutput(execmotion::HistoryOfOne(*pose));
        })
        .Inputs(
            Computation<motion::HumanoidPose>(
                _tokens->sampleAnimation).Required());

    // -----------------------------------------------------------------------
    // motion.interpolatePose -- what the history states at the evaluated time
    // -----------------------------------------------------------------------
    //
    // `motion::ClipSource::Sample` over the snapshot, at the evaluated instant:
    // `IMotionSource`'s one question, asked of the implementation that serves a
    // finished animation. **Nothing here is a clip's interpolation** -- between
    // two keys of a `UsdSkelAnimation` the answer is USD's, resolved before any
    // callback runs (the sampling report section 5). What this node
    // interpolates is a history the graph cannot see any other way: a driver's
    // buffered samples, bracketing the instant the system is evaluating.
    //
    // The instant arrives inside `motion.sampleAnimation`'s pose, as the
    // seconds that node stamped, rather than through `computeTime` and the rate
    // a second time: the conversion has one home, and the refusal of a clip that
    // states no rate reaches this node by propagation instead of by a copy.
    //
    // `computeTime` is declared all the same, for the one thing the stamp cannot
    // say: whether there *is* an instant. At the default time code the sampler
    // stamps 0.0, which costs it nothing -- what USD resolved there happened
    // before its callback ran -- but a history lives entirely on a timeline, and
    // sampling one at 0.0 would answer a believable `Held` at a second nobody
    // asked for. So this node refuses there, overridden or not. Declaring the
    // input costs no extra recompute: the node is time dependent through both
    // of its other inputs already (the filtering report section 2).
    //
    // The answer is the library's `motion::PoseSampleResult` whole. The pose in
    // it is stamped at the evaluated instant whether the history reached that
    // instant or not, so the status is the only thing that tells a sample from a
    // hold -- and a stopped source answers `Held` forever.
    self.PrimComputation(_tokens->interpolatePose)
        .Callback<motion::PoseSampleResult>(+[](const VdfContext &ctx) {
            const motion::HumanoidPose *const pose =
                ctx.GetInputValuePtr<motion::HumanoidPose>(
                    _tokens->sampleAnimation);
            if (!pose) {
                TF_RUNTIME_ERROR(
                    "motion.interpolatePose: no pose came back from "
                    "motion.sampleAnimation, so there is no evaluated instant "
                    "to sample the history at");
                ctx.SetEmptyOutput();
                return;
            }

            // The default time code is no instant. It is also what every request
            // is armed at (the filtering report section 4), so this refusal is
            // the common case of a driver's first compute rather than an edge
            // one -- which is exactly why a guessed 0.0 here would be reached.
            const UsdTimeCode timeCode =
                ctx.GetInputValue<EfTime>(
                    ExecBuiltinComputations->computeTime).GetTimeCode();
            if (!timeCode.IsNumeric()) {
                TF_RUNTIME_ERROR(
                    "motion.interpolatePose: the system is evaluating at the "
                    "default time code, which is no instant on a timeline; a "
                    "history cannot be sampled until ChangeTime names one");
                ctx.SetEmptyOutput();
                return;
            }

            const motion::HumanoidAnimation *const history =
                ctx.GetInputValuePtr<motion::HumanoidAnimation>(
                    _tokens->poseHistory);
            if (!history) {
                // Not reachable from the graph as declared: `motion.poseHistory`
                // sets no value only when the pose above is absent, which
                // returned already, and a driver cannot override the key *to*
                // nothing -- an empty `VtValue` is a type mismatch exec drops,
                // computing the ordinary value instead (measured in
                // `execMotion_interpolate`). What does reach it is the input
                // below going **undeclared**: the callback then runs with a
                // null pointer and nothing else reports it (the root-motion
                // report section 3), so this refusal is the only thing that
                // does -- verified by deleting the declaration.
                //
                // An empty *history* is a different thing again: the library's
                // `Unavailable`, which is an answer.
                TF_RUNTIME_ERROR(
                    "motion.interpolatePose: no history came back from "
                    "motion.poseHistory, so there is nothing to sample");
                ctx.SetEmptyOutput();
                return;
            }

            if (std::optional<motion::PoseSampleResult> result =
                    execmotion::SampleHistory(*history, pose->timestamp)) {
                ctx.SetOutput(*result);
                return;
            }

            // The one refusal the seam has: a history whose timestamps are not
            // finite or not in time order, which the library's binary search
            // would answer with a bracket nobody measured. A value of the
            // result type cannot say that -- every one of them is an answer,
            // `Unavailable` included -- so this sets none.
            TF_RUNTIME_ERROR(
                "motion.interpolatePose: the history's timestamps are not "
                "finite and in time order, so the samples bracketing %g s "
                "cannot be found; no pose was sampled",
                pose->timestamp);
            ctx.SetEmptyOutput();
        })
        .Inputs(
            Computation<motion::HumanoidPose>(
                _tokens->sampleAnimation).Required(),
            Computation<motion::HumanoidAnimation>(
                _tokens->poseHistory).Required(),
            Stage().Computation<EfTime>(
                ExecBuiltinComputations->computeTime).Required());

    // -----------------------------------------------------------------------
    // motion.blendPoses -- several clips, weighted, at the evaluated frame
    // -----------------------------------------------------------------------
    //
    // The one node in this bundle that wants poses from more than one place,
    // and so the one where 26.08's fan-in rules land -- which is why the plan
    // put it last. It is `motion::BlendPoses` over what the clips
    // `motion:blend:sources` targets each sample at this frame, weighted by
    // `motion:blend:weights`: one library call, like `motion.interpolatePose`.
    //
    // **Fan-in through a relationship, and read twice.** Each target's
    // `motion.sampleAnimation` arrives as one value of a fan-in, and 26.08 drops
    // two kinds of target from it without a word: one that does not provide the
    // computation is skipped while the network compiles
    // (`exec/inputResolver.cpp`), and one whose computation refused is skipped
    // by the read iterator, which passes over an input that holds no value
    // (`vdf/readIterator.h`). A blend that paired weights with whatever came
    // back would then weight the wrong clip. So the relationship is read a
    // second time, for the builtin `computePath` that every object on the stage
    // provides, and a count that disagrees is refused -- measured by deleting
    // that check and watching a blend of two quietly become a blend of one.
    //
    // It reads each source's `motion.sampleAnimation` -- what the clip states
    // at this frame -- and not its filtered pose. A blend combines sources; a
    // filter is a recurrence whose previous answer belongs to whoever drives
    // that source, and reading it here would make one clip's driver state part
    // of another prim's answer.
    //
    // It declares no `computeTime`: it does not use the frame, and time
    // dependence reaches it across the fan-in from every source that has it
    // (measured, the filtering report section 2 across a relationship).
    self.PrimComputation(_tokens->blendPoses)
        .Callback<motion::HumanoidPose>(+[](const VdfContext &ctx) {
            execmotion::BlendInputs inputs;

            std::vector<SdfPath> targets;
            for (VdfReadIterator<SdfPath> path(ctx, _tokens->sourcePaths);
                 !path.IsAtEnd(); ++path) {
                targets.push_back(*path);
            }
            inputs.sourceCount = targets.size();

            for (VdfReadIterator<motion::HumanoidPose> pose(
                     ctx, _tokens->sourcePoses);
                 !pose.IsAtEnd(); ++pose) {
                inputs.poses.push_back(*pose);
            }

            for (VdfReadIterator<float> weight(ctx, _tokens->blendWeights);
                 !weight.IsAtEnd(); ++weight) {
                inputs.weights.push_back(*weight);
            }

            execmotion::BlendOutcome outcome = execmotion::BlendedPose(inputs);
            if (outcome.pose) {
                ctx.SetOutput(std::move(*outcome.pose));
                return;
            }

            // The targets by path, because the one refusal a reader will most
            // want to act on -- a source that answered nothing -- cannot say
            // which of them it was: the fan-in hands back values, not the
            // objects they came from.
            std::string named;
            for (const SdfPath &target : targets) {
                named += named.empty() ? "<" : ", <";
                named += target.GetString();
                named += ">";
            }

            switch (outcome.refusal) {
            case execmotion::BlendRefusal::NoSource:
                TF_RUNTIME_ERROR(
                    "motion.blendPoses: 'motion:blend:sources' reaches "
                    "nothing on the stage, so there is nothing to blend; no "
                    "pose was blended");
                break;
            case execmotion::BlendRefusal::SourceUnanswered:
                TF_RUNTIME_ERROR(
                    "motion.blendPoses: %zu of the %zu objects "
                    "'motion:blend:sources' targets (%s) answered no "
                    "motion.sampleAnimation -- a target that is not a "
                    "UsdSkelAnimation, or whose sampler refused, is dropped "
                    "from the fan-in without a word, and a weight paired with "
                    "what is left would land on the wrong clip; no pose was "
                    "blended",
                    inputs.sourceCount - inputs.poses.size(),
                    inputs.sourceCount, named.c_str());
                break;
            case execmotion::BlendRefusal::WeightCount:
                TF_RUNTIME_ERROR(
                    "motion.blendPoses: the blend states %zu "
                    "'motion:blend:weights' for the %zu objects "
                    "'motion:blend:sources' targets (%s), and a weight is "
                    "paired with its source by position; no pose was blended",
                    inputs.weights.size(), inputs.sourceCount, named.c_str());
                break;
            case execmotion::BlendRefusal::WeightNotFinite:
                TF_RUNTIME_ERROR(
                    "motion.blendPoses: a 'motion:blend:weights' entry is not "
                    "finite, and motion::BlendPoses would carry it into every "
                    "rotation; no pose was blended");
                break;
            case execmotion::BlendRefusal::InstantsDisagree:
                TF_RUNTIME_ERROR(
                    "motion.blendPoses: the sources (%s) were not sampled at "
                    "one finite instant -- two clips counting the same frame "
                    "at different 'motion:timeCodesPerSecond', or a pose "
                    "handed in stamped elsewhere -- and a blend would stamp "
                    "a second between them that nobody sampled; no pose was "
                    "blended",
                    named.c_str());
                break;
            case execmotion::BlendRefusal::NothingWeighted:
                TF_RUNTIME_ERROR(
                    "motion.blendPoses: no 'motion:blend:weights' entry is "
                    "positive, and motion::BlendPoses answers that with a "
                    "default pose stamped 0.0 rather than at the instant the "
                    "sources were sampled; no pose was blended");
                break;
            }
            ctx.SetEmptyOutput();
        })
        .Inputs(
            Relationship(_tokens->blendSources)
                .TargetedObjects<SdfPath>(ExecBuiltinComputations->computePath)
                .InputName(_tokens->sourcePaths),
            Relationship(_tokens->blendSources)
                .TargetedObjects<motion::HumanoidPose>(
                    _tokens->sampleAnimation)
                .InputName(_tokens->sourcePoses),
            AttributeValue<float>(_tokens->blendWeights));
}
