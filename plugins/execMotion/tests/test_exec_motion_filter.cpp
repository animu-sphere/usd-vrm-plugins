// SPDX-License-Identifier: Apache-2.0
//
// `motion.filterPose` through the built bundle: the first node that wraps
// `motionRuntime`, the first that consumes another computation's result, and
// the first whose missing half the *caller* supplies.
//
// A filter is a recurrence -- this frame's answer is a function of the last
// one -- and an OpenExec callback is handed exactly one time with no way to
// reach another (docs/reports/openusd/26.08-openexec-sampling.md §5). Holding
// the previous pose inside the callback would be the mutable state exec's
// purity rule forbids and invalidation cannot see, so the prior pose is a value
// key of its own -- `motion.priorPose` -- and the driver replaces it through
// `ExecUsdSystem::ComputeWithOverrides`. This suite is where that mechanism is
// measured rather than assumed:
//
//   * un-overridden, `motion.filterPose` is `motion.sampleAnimation`;
//   * overridden, it is one step of `motion::PoseFilter` at the clip's stated
//     cutoff, and the step's weight is checked against the library's own
//     formula rather than against the library;
//   * the override reaches exactly one value key, and does not survive the
//     call; and
//   * a clip stating no policy gets `PoseFilter`'s defaults, not this bundle's.
//
// Like the mechanism and sample suites, this executable does not link the
// plugin: the computations are reached only through `PXR_PLUGINPATH_NAME` and
// the staged plugInfo.json.

#include "pxr/pxr.h"

#include "pxr/base/gf/quatf.h"
#include "pxr/base/gf/vec3f.h"
#include "pxr/base/tf/token.h"
#include "pxr/base/vt/value.h"

#include "pxr/exec/exec/request.h"
#include "pxr/exec/execUsd/cacheView.h"
#include "pxr/exec/execUsd/request.h"
#include "pxr/exec/execUsd/system.h"
#include "pxr/exec/execUsd/valueKey.h"
#include "pxr/exec/execUsd/valueOverride.h"

#include "pxr/usd/sdf/path.h"
#include "pxr/usd/usd/attribute.h"
#include "pxr/usd/usd/prim.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usd/timeCode.h"

#include <motionCore/Humanoid.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

namespace {

const TfToken kFilterPose("motion.filterPose");
const TfToken kSampleAnimation("motion.sampleAnimation");
const TfToken kPriorPose("motion.priorPose");

const TfToken kCutoffHz("motion:filter:cutoffHz");
const TfToken kRootPosition("motion:filter:rootPosition");
const TfToken kRootOrientation("motion:filter:rootOrientation");

// The three value keys every request below asks for, in this order.
constexpr int kFiltered = 0;
constexpr int kSampled = 1;
constexpr int kPrior = 2;

// The frame the whole suite reads at, and the second it is. Frame 50 is halfway
// between the fixture's two keys, so the pose there is a rotation and a
// translation that are both strictly between the ends -- a filter step that
// went the wrong way, or did not happen, cannot land on the right number by
// coincidence.
constexpr double kFrame = 50.0;
constexpr double kSecond = 1.0;

// The seconds between the prior pose and the sampled one: one frame at the
// fixture's 50 time codes per second. Small enough that neither of the two
// cutoffs saturates the step weight, which is what keeps 6 Hz and 12 Hz
// distinguishable in the assertions below.
constexpr double kStep = 0.02;

bool Has(const motion::HumanoidPose& pose, motion::HumanBone bone)
{
    return pose.validRotations.test(static_cast<std::size_t>(bone));
}

const GfQuatf& RotationOf(const motion::HumanoidPose& pose,
                          motion::HumanBone bone)
{
    return pose.localRotations[static_cast<std::size_t>(bone)];
}

// The angle of a unit quaternion, in degrees; formed in double precision for
// the reason motionCore's own comparison documents (acos turns a float's last
// bit into a milliradian near zero).
double AngleDegrees(const GfQuatf& q)
{
    const GfQuatf n = q.GetNormalized();
    const double w = std::min(1.0, std::max(-1.0, double(n.GetReal())));
    return 2.0 * std::acos(w) * 180.0 / M_PI;
}

bool NearlyEqual(double a, double b, double tolerance)
{
    return std::abs(a - b) <= tolerance;
}

bool NearlyEqual(const GfVec3f& a, const GfVec3f& b, double tolerance)
{
    return NearlyEqual(a[0], b[0], tolerance)
        && NearlyEqual(a[1], b[1], tolerance)
        && NearlyEqual(a[2], b[2], tolerance);
}

// `motion::PoseFilter`'s step weight, written out rather than called.
//
// The suite does not link motionRuntime on purpose: an expected value computed
// by the same function under test would assert that the library equals itself
// and say nothing about whether the cutoff the clip stated ever reached it.
// This is the exponential-smoothing weight from the library's documented
// definition -- frame-rate independent, derived from the elapsed time -- and a
// change to that definition should reach this suite as a red result.
double StepWeight(double cutoffHz, double dt)
{
    constexpr double kTwoPi = 6.2831853071795862;
    if (cutoffHz <= 0.0 || dt <= 0.0) {
        return 1.0;
    }
    return 1.0 - std::exp(-kTwoPi * cutoffHz * dt);
}

// The pose a driver would hand back as "the previous frame's answer".
//
// The four bones are the ones the fixtures name, all at identity, with the hips
// at the origin -- so the sampled pose at frame 50 differs from it in both a
// rotation and a translation, and one step of the filter has to move both.
motion::HumanoidPose PriorPose(double timestamp)
{
    motion::HumanoidPose pose;
    pose.timestamp = timestamp;
    for (const motion::HumanBone bone : {motion::HumanBone::Hips,
                                         motion::HumanBone::Spine,
                                         motion::HumanBone::Chest,
                                         motion::HumanBone::Head}) {
        pose.validRotations.set(static_cast<std::size_t>(bone));
    }
    pose.root.worldPosition = GfVec3f(0.0f);
    pose.root.hasPosition = true;
    return pose;
}

motion::HumanoidPose PoseAt(const ExecUsdCacheView& view, int index)
{
    const VtValue value = view.Get(index);
    assert(!value.IsEmpty() &&
           "no value came back -- if the plugInfo is unstaged this is what it "
           "looks like, not a load error");
    assert(value.IsHolding<motion::HumanoidPose>() &&
           "the canonical aggregate did not survive the boundary");
    return value.UncheckedGet<motion::HumanoidPose>();
}

ExecUsdValueOverrideVector PriorOverride(const UsdPrim& clip,
                                         const motion::HumanoidPose& pose)
{
    ExecUsdValueOverrideVector overrides;
    overrides.push_back(
        ExecUsdValueOverride{ExecUsdValueKey(clip, kPriorPose),
                             VtValue(pose)});
    return overrides;
}

// ---------------------------------------------------------------------------
// The clip that states a policy
// ---------------------------------------------------------------------------
void TestAClipWithAPolicy(const std::string& fixture)
{
    UsdStageRefPtr stage = UsdStage::Open(fixture);
    assert(stage && "the policy fixture did not open");

    UsdPrim clip = stage->GetPrimAtPath(SdfPath("/Clip"));
    assert(clip && "the fixture has no /Clip prim");

    // The premise of every number below. A fixture that had drifted off 12 Hz
    // would make each expected angle wrong in the same direction, which is the
    // shape of failure an assertion on the pose alone cannot attribute.
    float authoredCutoff = 0.0f;
    assert(clip.GetAttribute(kCutoffHz).Get(&authoredCutoff));
    assert(authoredCutoff == 12.0f && "the policy fixture is not at 12 Hz");

    ExecUsdSystem system(stage);

    int timeInvalidations = 0;
    bool filteredReported = false;
    std::vector<ExecUsdValueKey> keys;
    keys.emplace_back(clip, kFilterPose);
    keys.emplace_back(clip, kSampleAnimation);
    keys.emplace_back(clip, kPriorPose);
    ExecUsdRequest request = system.BuildRequest(
        std::move(keys),
        [](const ExecRequestIndexSet&, const EfTimeInterval&) {},
        [&](const ExecRequestIndexSet& indices) {
            ++timeInvalidations;
            filteredReported =
                filteredReported || indices.count(kFiltered) > 0;
        });
    assert(request.IsValid() &&
           "the request did not compile -- a computation that consumes another "
           "computation on the same prim is the new shape here");

    // A request reports no invalidation until it has been computed once --
    // `BuildRequest` registers the interest and `Compute` renews it -- so the
    // frame below moves *after* a first compute rather than before it. The
    // pose that first compute returns is empty, because this clip authors only
    // time samples and the system starts at the default time code (the sampling
    // report §4); it is the interest that is wanted here, not the value.
    {
        ExecUsdCacheView view = system.Compute(request);
        assert(PoseAt(view, kFiltered).validRotations.count() == 0 &&
               "a clip authoring only time samples resolved to something at the "
               "default time code");
    }

    system.ChangeTime(UsdTimeCode(kFrame));

    // ---- time reaches this node through its inputs ------------------------
    // `motion.filterPose` declares no `computeTime`: it does not use the frame,
    // and a node that declares it is recomputed on every frame change even when
    // nothing it reads has moved (the sampling report §3). It is still reported
    // when the frame moves, because the pose it consumes is -- so a dependent
    // value key inherits the time dependency of the node it reads, and a filter
    // does not have to ask for the clock to be re-evaluated with it.
    assert(timeInvalidations > 0 &&
           "moving the frame reached no value key at all");
    assert(filteredReported &&
           "motion.filterPose was NOT reported when the frame moved, although "
           "the pose it consumes is time dependent");

    // ---- un-overridden, the filter is the sampler -------------------------
    // `motion.priorPose` forwards the clip's own pose, so the filter smooths the
    // pose against itself: `dt` is zero, `motion::PoseFilter` reseeds and
    // returns its argument. Nothing in the bundle special-cases this; it is the
    // library's documented answer to a non-increasing timestamp.
    {
        ExecUsdCacheView view = system.Compute(request);
        const motion::HumanoidPose sampled = PoseAt(view, kSampled);
        const motion::HumanoidPose prior = PoseAt(view, kPrior);
        const motion::HumanoidPose filtered = PoseAt(view, kFiltered);

        assert(NearlyEqual(sampled.timestamp, kSecond, 1e-12));
        assert(NearlyEqual(AngleDegrees(RotationOf(sampled,
                                                   motion::HumanBone::Head)),
                           45.0, 1e-2) &&
               "the fixture is not the sampled clip's frame 50 after all");
        assert(prior == sampled &&
               "motion.priorPose did not forward the sampled pose unchanged");
        assert(filtered == sampled &&
               "an un-overridden filter changed the pose, so a prior pose was "
               "invented from somewhere");
    }

    // ---- overridden, the filter is one step -------------------------------
    const motion::HumanoidPose prior = PriorPose(kSecond - kStep);
    const double weight = StepWeight(12.0, kStep);
    {
        ExecUsdCacheView view = system.ComputeWithOverrides(
            request, PriorOverride(clip, prior));

        const motion::HumanoidPose filtered = PoseAt(view, kFiltered);
        const motion::HumanoidPose sampled = PoseAt(view, kSampled);
        const motion::HumanoidPose seen = PoseAt(view, kPrior);

        // The override reached the value key it named. Asserted rather than
        // inferred from the filtered pose, because everything below depends on
        // it and a mechanism that silently ignored the override would otherwise
        // look like a filter that did nothing.
        assert(seen == prior &&
               "the override did not reach motion.priorPose");

        // ...and only that one. `motion.sampleAnimation` is the same value it
        // computes without overrides, which is what makes the filtered pose
        // below a step between two known poses rather than between two unknown
        // ones.
        assert(NearlyEqual(AngleDegrees(RotationOf(sampled,
                                                   motion::HumanBone::Head)),
                           45.0, 1e-2) &&
               "the override leaked into motion.sampleAnimation");

        // A quaternion slerp between two rotations about the same axis moves
        // the angle linearly, so the head lands at `weight` of the 45 degrees
        // the clip states. This is the assertion that says the cutoff the
        // *clip* authored reached `motion::PoseFilter`: at the library's
        // default of 6 Hz the same step lands somewhere else, and the two are
        // checked apart below.
        assert(NearlyEqual(AngleDegrees(RotationOf(filtered,
                                                   motion::HumanBone::Head)),
                           45.0 * weight, 1e-2) &&
               "the head did not land one filter step from the prior pose");

        // The hips are lerped, not slerped, and by the same weight.
        assert(filtered.root.hasPosition);
        assert(NearlyEqual(filtered.root.worldPosition,
                           GfVec3f(0.0f,
                                   float(0.5 * weight),
                                   float(1.0 * weight)),
                           1e-4) &&
               "the root position did not take the same step as the rotations");

        // The result is stamped with the pose it filtered, not with the state
        // it started from: a filtered pose belongs to the frame it was asked
        // for.
        assert(NearlyEqual(filtered.timestamp, kSecond, 1e-12));
        assert(filtered.validRotations == sampled.validRotations);
        assert(Has(filtered, motion::HumanBone::Head));
    }

    // ---- an override does not survive the call ----------------------------
    // Documented on ComputeWithOverrides and load-bearing here: a driver steps
    // the recurrence forward once per frame, and an override that stuck would
    // make the next plain Compute answer with the last frame's state.
    {
        ExecUsdCacheView view = system.Compute(request);
        assert(PoseAt(view, kFiltered) == PoseAt(view, kSampled) &&
               "an override outlived the ComputeWithOverrides that supplied it");
    }

    // ---- a cutoff of zero is the library's pass-through --------------------
    // Authored on the stage rather than in a fourth fixture, because the point
    // is as much that the change *arrives*: this is an attribute input of a node
    // two links downstream of the one that reads it, so a filtered pose that
    // did not move here would be a stale cache rather than a wrong policy.
    assert(clip.GetAttribute(kCutoffHz).Set(0.0f));
    {
        ExecUsdCacheView view = system.ComputeWithOverrides(
            request, PriorOverride(clip, prior));
        assert(PoseAt(view, kFiltered) == PoseAt(view, kSampled) &&
               "a non-positive cutoff smoothed the pose anyway, or the authored "
               "value never reached the computation");
    }
    assert(clip.GetAttribute(kCutoffHz).Set(12.0f));

    // ---- the root flag is the clip's to turn off ---------------------------
    // The rotations still take their step and the hips stop taking theirs, which
    // is a stronger statement than either alone: the flag reached exactly the
    // field it names.
    assert(clip.GetAttribute(kRootPosition).Set(false));
    {
        ExecUsdCacheView view = system.ComputeWithOverrides(
            request, PriorOverride(clip, prior));
        const motion::HumanoidPose filtered = PoseAt(view, kFiltered);
        assert(NearlyEqual(filtered.root.worldPosition,
                           GfVec3f(0.0f, 0.5f, 1.0f), 1e-6) &&
               "the root position was smoothed although the clip said not to");
        assert(NearlyEqual(AngleDegrees(RotationOf(filtered,
                                                   motion::HumanBone::Head)),
                           45.0 * weight, 1e-2) &&
               "turning off root filtering stopped the rotations too");
    }
    assert(clip.GetAttribute(kRootPosition).Set(true));

    // The third flag is authored and inert, and that is worth stating once: a
    // pose read from a `UsdSkelAnimation` carries no root orientation, so
    // `PoseFilter` has nothing to apply it to. It exists because the wrapper
    // carries the whole of `PoseFilter::Options` and a live source does report
    // one.
    bool authoredRootOrientation = true;
    assert(clip.GetAttribute(kRootOrientation).Get(&authoredRootOrientation));
    assert(!authoredRootOrientation);
    {
        ExecUsdCacheView view = system.ComputeWithOverrides(
            request, PriorOverride(clip, prior));
        assert(!PoseAt(view, kFiltered).root.hasOrientation &&
               "a clip-sourced pose grew a root orientation");
    }

    std::printf("execMotion filter: a clip's policy reaches motion::PoseFilter, "
                "and the prior pose reaches it as an override\n");
}

// ---------------------------------------------------------------------------
// The clip that states none
// ---------------------------------------------------------------------------
void TestAClipWithNoPolicy(const std::string& fixture)
{
    UsdStageRefPtr stage = UsdStage::Open(fixture);
    assert(stage && "the sampled fixture did not open");

    UsdPrim clip = stage->GetPrimAtPath(SdfPath("/Clip"));
    assert(clip && "the fixture has no /Clip prim");

    // The premise: this clip says nothing about filtering. Three absent
    // attributes, and `.Required()` is not what refuses them -- the node simply
    // does not require them, because the library it wraps has an answer for
    // each.
    assert(!clip.GetAttribute(kCutoffHz));
    assert(!clip.GetAttribute(kRootPosition));
    assert(!clip.GetAttribute(kRootOrientation));

    ExecUsdSystem system(stage);
    std::vector<ExecUsdValueKey> keys;
    keys.emplace_back(clip, kFilterPose);
    keys.emplace_back(clip, kSampleAnimation);
    keys.emplace_back(clip, kPriorPose);
    ExecUsdRequest request = system.BuildRequest(std::move(keys));
    assert(request.IsValid());

    system.ChangeTime(UsdTimeCode(kFrame));

    const motion::HumanoidPose prior = PriorPose(kSecond - kStep);
    ExecUsdCacheView view = system.ComputeWithOverrides(
        request, PriorOverride(clip, prior));
    const motion::HumanoidPose filtered = PoseAt(view, kFiltered);

    // `motion::PoseFilter::Options`' own default is 6 Hz, and that is what a
    // clip stating nothing gets. The negative half is the one that matters: the
    // same step at the policy fixture's 12 Hz lands somewhere else, so this
    // assertion would not pass if the bundle had picked a default of its own or
    // carried the last clip's.
    const double defaultWeight = StepWeight(6.0, kStep);
    const double policyWeight = StepWeight(12.0, kStep);
    const double head =
        AngleDegrees(RotationOf(filtered, motion::HumanBone::Head));
    assert(NearlyEqual(head, 45.0 * defaultWeight, 1e-2) &&
           "a clip stating no cutoff was not filtered at the library's own "
           "default of 6 Hz");
    assert(!NearlyEqual(head, 45.0 * policyWeight, 1e-1) &&
           "6 Hz and 12 Hz are indistinguishable at this step, so the policy "
           "fixture's assertions prove nothing either");

    // The other two defaults are true, so the hips take the step here.
    assert(filtered.root.hasPosition);
    assert(NearlyEqual(filtered.root.worldPosition,
                       GfVec3f(0.0f,
                               float(0.5 * defaultWeight),
                               float(1.0 * defaultWeight)),
                       1e-4) &&
           "a clip stating nothing did not get PoseFilter's root defaults");

    std::printf("execMotion filter: a clip stating no policy is filtered with "
                "motion::PoseFilter's own defaults\n");
}

} // namespace

int main(int argc, char** argv)
{
    assert(argc == 3 &&
           "usage: execMotion_filter <filtered_clip.usda> <sampled_clip.usda>");
    TestAClipWithAPolicy(argv[1]);
    TestAClipWithNoPolicy(argv[2]);
    std::printf("execMotion filter: all checks passed\n");
    return 0;
}
