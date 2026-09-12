// SPDX-License-Identifier: Apache-2.0
//
// `motion.interpolatePose` and `motion.poseHistory` through the built bundle.
//
// What the node answers is `IMotionSource`'s one question -- what is the pose at
// this evaluation time? -- asked of a **snapshot**: a timestamped history a
// driver hands the graph, bracketing the instant the system is evaluating. The
// snapshot enters as an override on `motion.poseHistory`, which is the "immutable
// snapshot" motion policy §11.4 puts between a live source's buffer and every
// computation. Four things are measured here that no earlier suite could:
//
//   * a **third and fourth** registered value type: `motion::HumanoidAnimation`
//     crosses as the snapshot, and `motion::PoseSampleResult` -- `motionRuntime`'s
//     type rather than `motionCore`'s -- comes back as the answer;
//   * an override of a value key whose type is **not a pose** reaches its
//     dependents the way the pose-typed one does (the root-motion report §8 left
//     it untested), and an override of the **wrong** type is refused by exec;
//   * **two overrides of two different keys in one call**, each reaching its own
//     dependents -- the graph's previous answer fed back through
//     `motion.priorPose` and the source's history handed in through this node's
//     key, which are two different kinds of thing a driver holds; and
//   * the first answer in this bundle with an **absent state of its own**: an
//     empty history is the library's `Unavailable`, a value, where every earlier
//     node's "nothing" had to be a refusal.
//
// Like every suite here but `execMotion_pose`, this executable does not link the
// plugin. It does link motionRuntime, because `motion::PoseSampleResult` is
// declared there -- and it calls none of the library's functions: it reads the
// result's plain fields, so an expected value cannot come from the code under
// test.

#include "pxr/pxr.h"

#include "pxr/base/gf/quatf.h"
#include "pxr/base/gf/vec3f.h"
#include "pxr/base/tf/errorMark.h"
#include "pxr/base/tf/token.h"
#include "pxr/base/vt/value.h"

#include "pxr/exec/exec/request.h"
#include "pxr/exec/execUsd/cacheView.h"
#include "pxr/exec/execUsd/request.h"
#include "pxr/exec/execUsd/system.h"
#include "pxr/exec/execUsd/valueKey.h"
#include "pxr/exec/execUsd/valueOverride.h"

#include "pxr/usd/sdf/path.h"
#include "pxr/usd/usd/prim.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usd/timeCode.h"

#include <motionCore/Humanoid.h>
#include <motionRuntime/MotionSource.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

namespace {

const TfToken kInterpolatePose("motion.interpolatePose");
const TfToken kPoseHistory("motion.poseHistory");
const TfToken kSampleAnimation("motion.sampleAnimation");
const TfToken kPriorPose("motion.priorPose");
const TfToken kFilterPose("motion.filterPose");

// The value keys the main request asks for, in this order.
constexpr int kInterpolated = 0;
constexpr int kHistory = 1;
constexpr int kSampled = 2;

// Frame 50 of the fixture, and the second it is at 50 time codes per second --
// the pair every suite here reads at, so the clip's own answer is strictly
// between its keys: head at 45 degrees about +Y, hips at (0, 0.5, 1).
constexpr double kFrame = 50.0;
constexpr double kSecond = 1.0;
constexpr float kClipHeadDegrees = 45.0f;
const GfVec3f kClipHips(0.0f, 0.5f, 1.0f);

constexpr std::size_t kHead = static_cast<std::size_t>(motion::HumanBone::Head);

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

float HeadDegrees(const motion::HumanoidPose& pose)
{
    const GfQuatf head = pose.localRotations[kHead].GetNormalized();
    const double w = std::min(1.0, std::max(-1.0, double(head.GetReal())));
    return float(2.0 * std::acos(w) * 180.0 / M_PI);
}

// A pose a driver's buffer would hold: the four bones the fixture names, the
// head turned about +Y, the hips somewhere.
motion::HumanoidPose BufferedPose(double timestamp, float headDegrees,
                                  const GfVec3f& hips)
{
    motion::HumanoidPose pose;
    pose.timestamp = timestamp;
    for (const motion::HumanBone bone : {motion::HumanBone::Hips,
                                         motion::HumanBone::Spine,
                                         motion::HumanBone::Chest,
                                         motion::HumanBone::Head}) {
        pose.validRotations.set(static_cast<std::size_t>(bone));
    }
    const float half = headDegrees * float(M_PI) / 360.0f;
    pose.localRotations[kHead] =
        GfQuatf(std::cos(half), GfVec3f(0.0f, std::sin(half), 0.0f));
    pose.root.worldPosition = hips;
    pose.root.hasPosition = true;
    return pose;
}

motion::HumanoidAnimation HistoryOf(std::vector<motion::HumanoidPose> samples)
{
    motion::HumanoidAnimation history;
    history.samples = std::move(samples);
    if (!history.samples.empty()) {
        history.startTime = history.samples.front().timestamp;
        history.endTime = history.samples.back().timestamp;
    }
    return history;
}

// The driver's snapshot the suite overrides with: two samples four hundredths of
// a second apart, bracketing 1.0 s. Chosen to **disagree with the clip** at
// every field this suite reads -- the head turns 60 degrees rather than 90 and
// the hips travel along +X rather than up and forward -- so an answer that came
// from the clip instead of the snapshot cannot land on the right number.
motion::HumanoidAnimation Bracketing()
{
    return HistoryOf({
        BufferedPose(kSecond - 0.02, 0.0f, GfVec3f(2.0f, 0.0f, 0.0f)),
        BufferedPose(kSecond + 0.02, 60.0f, GfVec3f(4.0f, 0.0f, 0.0f))});
}

ExecUsdValueOverrideVector HistoryOverride(const UsdPrim& clip,
                                           const VtValue& history)
{
    ExecUsdValueOverrideVector overrides;
    overrides.push_back(
        ExecUsdValueOverride{ExecUsdValueKey(clip, kPoseHistory), history});
    return overrides;
}

motion::PoseSampleResult ResultAt(const ExecUsdCacheView& view, int index)
{
    const VtValue value = view.Get(index);
    assert(!value.IsEmpty() &&
           "no value came back -- if the plugInfo is unstaged this is what it "
           "looks like, not a load error");
    // The fourth registered type, and the first that is motionRuntime's. A
    // callback whose declared result type had drifted from what it sets would
    // surface here and nowhere earlier.
    assert(value.IsHolding<motion::PoseSampleResult>() &&
           "motion.interpolatePose did not return a motion::PoseSampleResult");
    return value.UncheckedGet<motion::PoseSampleResult>();
}

motion::HumanoidAnimation HistoryAt(const ExecUsdCacheView& view, int index)
{
    const VtValue value = view.Get(index);
    assert(!value.IsEmpty());
    assert(value.IsHolding<motion::HumanoidAnimation>() &&
           "motion.poseHistory did not return a motion::HumanoidAnimation");
    return value.UncheckedGet<motion::HumanoidAnimation>();
}

motion::HumanoidPose PoseAt(const ExecUsdCacheView& view, int index)
{
    const VtValue value = view.Get(index);
    assert(!value.IsEmpty());
    assert(value.IsHolding<motion::HumanoidPose>());
    return value.UncheckedGet<motion::HumanoidPose>();
}

// A refusal, which in this bundle is **no value at all** (README, "How a
// computation refuses").
void AssertRefused(const ExecUsdCacheView& view, int index)
{
    const VtValue value = view.Get(index);
    assert(value.IsEmpty() &&
           "a refusal came back carrying a value, which puts it back where a "
           "consumer cannot tell it from an answer");
}

bool MarkNames(const TfErrorMark& mark, const std::string& what)
{
    for (TfErrorMark::Iterator it = mark.GetBegin(); it != mark.GetEnd(); ++it) {
        if (it->GetCommentary().find(what) != std::string::npos) {
            return true;
        }
    }
    return false;
}

std::vector<ExecUsdValueKey> KeysFor(const UsdPrim& clip)
{
    std::vector<ExecUsdValueKey> keys;
    keys.emplace_back(clip, kInterpolatePose);
    keys.emplace_back(clip, kPoseHistory);
    keys.emplace_back(clip, kSampleAnimation);
    return keys;
}

// ---------------------------------------------------------------------------
// Nobody supplies a history
// ---------------------------------------------------------------------------
void TestUnoverriddenTheNodeIsTheClip(const std::string& fixture)
{
    UsdStageRefPtr stage = UsdStage::Open(fixture);
    assert(stage && "the sampled fixture did not open");
    UsdPrim clip = stage->GetPrimAtPath(SdfPath("/Clip"));
    assert(clip && "the fixture has no /Clip prim");

    ExecUsdSystem system(stage);

    bool interpolateReported = false;
    ExecUsdRequest request = system.BuildRequest(
        KeysFor(clip),
        [](const ExecRequestIndexSet&, const EfTimeInterval&) {},
        [&](const ExecRequestIndexSet& indices) {
            interpolateReported =
                interpolateReported || indices.count(kInterpolated) > 0;
        });
    assert(request.IsValid() &&
           "a request over a history-typed and a sample-result-typed key did "
           "not compile");

    // Armed by its first compute (the filtering report §4), which is at the
    // default time code -- and the default time code is no instant. The sampler
    // answers there (an empty pose stamped 0.0, which costs it nothing) and so
    // does the history built from it; this node refuses, because sampling a
    // history at a guessed 0.0 is the one thing it must not do. The one place
    // an un-overridden node differs from the sampler.
    {
        TfErrorMark mark;
        ExecUsdCacheView view = system.Compute(request);
        assert(!PoseAt(view, kSampled).validRotations.any());
        assert(HistoryAt(view, kHistory).samples.size() == 1);
        AssertRefused(view, kInterpolated);
        assert(MarkNames(mark, "default time code") &&
               "the default time code was refused without saying why");
        mark.Clear();
    }

    system.ChangeTime(UsdTimeCode(kFrame));

    // Declares no `computeTime`, and is reported: time dependence reaches it
    // through both of its inputs, one of which is itself a link away from the
    // clip.
    assert(interpolateReported &&
           "motion.interpolatePose was NOT reported when the frame moved");

    ExecUsdCacheView view = system.Compute(request);
    const motion::HumanoidPose sampled = PoseAt(view, kSampled);
    assert(NearlyEqual(sampled.timestamp, kSecond, 1e-12));

    // The third registered type, carrying the clip's pose as a history of one.
    const motion::HumanoidAnimation history = HistoryAt(view, kHistory);
    assert(history.samples.size() == 1);
    assert(history.samples.front() == sampled &&
           "the ordinary history is not the pose the clip states");
    assert(history.startTime == sampled.timestamp &&
           history.endTime == sampled.timestamp);

    // And the answer is that pose, sampled at its own instant: no bracket, no
    // hold, no lag. The pass-through `motion.filterPose` has un-overridden,
    // special-cased in neither.
    const motion::PoseSampleResult result = ResultAt(view, kInterpolated);
    assert(result.status == motion::PoseSampleStatus::Sampled);
    assert(result.pose && *result.pose == sampled &&
           "un-overridden, motion.interpolatePose is not motion.sampleAnimation");
    assert(result.lag == 0.0);

    std::printf("execMotion interpolate: un-overridden, the history is the "
                "clip's own pose and the answer is that pose, sampled\n");
}

// ---------------------------------------------------------------------------
// A driver supplies one
// ---------------------------------------------------------------------------
void TestAHistoryIsSampledAtTheEvaluatedInstant(const std::string& fixture)
{
    UsdStageRefPtr stage = UsdStage::Open(fixture);
    assert(stage);
    UsdPrim clip = stage->GetPrimAtPath(SdfPath("/Clip"));
    assert(clip);

    ExecUsdSystem system(stage);
    ExecUsdRequest request = system.BuildRequest(KeysFor(clip));
    assert(request.IsValid());

    const motion::HumanoidAnimation bracketing = Bracketing();

    // ---- a driver's first compute, with its buffer, before any ChangeTime ----
    // The shape a naive driver takes: it has samples, so it hands them over on
    // the compute that arms the request -- which is at the default time code.
    // The sampler stamps 0.0 there, and a history sampled at 0.0 would come
    // back a believable `Held` with a lag of -1.02 s: a second nobody asked
    // for, answered with a measured pose. Refused instead, with the override
    // itself arriving intact -- the refusal is this node's, not exec's.
    {
        TfErrorMark mark;
        ExecUsdCacheView view = system.ComputeWithOverrides(
            request, HistoryOverride(clip, VtValue(bracketing)));
        assert(HistoryAt(view, kHistory) == bracketing &&
               "the override did not reach motion.poseHistory at the default "
               "time code");
        AssertRefused(view, kInterpolated);
        assert(MarkNames(mark, "default time code") &&
               "a history was sampled, or refused silently, at the default "
               "time code");
        mark.Clear();
    }

    system.ChangeTime(UsdTimeCode(kFrame));

    // ---- bracketed: interpolated, from the snapshot and not from the clip ----
    {
        ExecUsdCacheView view = system.ComputeWithOverrides(
            request, HistoryOverride(clip, VtValue(bracketing)));

        // The override is visible as the key's own value -- measured for a
        // pose-typed key by the filtering report, and here for a key whose type
        // is a whole history.
        assert(HistoryAt(view, kHistory) == bracketing &&
               "an override of a HumanoidAnimation-typed key did not reach it");

        const motion::PoseSampleResult result = ResultAt(view, kInterpolated);
        assert(result.status == motion::PoseSampleStatus::Sampled);
        assert(result.pose);
        // Halfway between 0 and 60 degrees about one axis, and halfway from
        // (2, 0, 0) to (4, 0, 0): the snapshot's midpoint, which the clip's
        // 45 degrees and (0, 0.5, 1) cannot be mistaken for.
        assert(std::abs(HeadDegrees(*result.pose) - 30.0f) < 1e-3f &&
               "the answer did not come from the driver's history");
        assert(NearlyEqual(result.pose->root.worldPosition,
                           GfVec3f(3.0f, 0.0f, 0.0f), 1e-5));
        // Stamped at the evaluated instant, and behind the newest sample.
        assert(NearlyEqual(result.pose->timestamp, kSecond, 1e-12));
        assert(NearlyEqual(result.lag, -0.02, 1e-12));

        // The sibling that reads the clip is untouched: the history override
        // reaches its dependents and nothing else.
        const motion::HumanoidPose sampled = PoseAt(view, kSampled);
        assert(std::abs(HeadDegrees(sampled) - kClipHeadDegrees) < 1e-3f);
        assert(NearlyEqual(sampled.root.worldPosition, kClipHips, 1e-6));
    }

    // ---- past the newest sample: held, and the result says so -------------
    // A source that stopped delivering at 0.9 s. The pose that comes back is
    // stamped at 1.0 s like every other answer here, so the status and the lag
    // are the only fields that say the source has stopped -- which is why the
    // node returns them rather than the pose alone.
    {
        const motion::HumanoidAnimation stopped = HistoryOf({
            BufferedPose(0.5, 0.0f, GfVec3f(2.0f, 0.0f, 0.0f)),
            BufferedPose(0.9, 60.0f, GfVec3f(4.0f, 0.0f, 0.0f))});
        ExecUsdCacheView view = system.ComputeWithOverrides(
            request, HistoryOverride(clip, VtValue(stopped)));

        const motion::PoseSampleResult result = ResultAt(view, kInterpolated);
        assert(result.status == motion::PoseSampleStatus::Held &&
               "a request past the newest sample was not reported as a hold");
        assert(result.pose);
        assert(std::abs(HeadDegrees(*result.pose) - 60.0f) < 1e-3f);
        assert(NearlyEqual(result.pose->timestamp, kSecond, 1e-12) &&
               "a hold is no longer stamped at the request, so the reason the "
               "node returns the status needs restating");
        assert(NearlyEqual(result.lag, 0.1, 1e-12));
    }

    // ---- an empty history: an answer, not a refusal -------------------------
    // The library's `Unavailable`. The value is present -- `ResultAt` requires
    // it -- and carries no pose, which is a state this type has and the earlier
    // types did not. So nothing needs refusing: the answer already says there
    // was nothing to sample, and it cannot be mistaken for a pose.
    {
        TfErrorMark mark;
        ExecUsdCacheView view = system.ComputeWithOverrides(
            request, HistoryOverride(clip, VtValue(motion::HumanoidAnimation{})));
        const motion::PoseSampleResult result = ResultAt(view, kInterpolated);
        assert(result.status == motion::PoseSampleStatus::Unavailable);
        assert(!result.pose);
        assert(mark.IsClean() &&
               "an empty history posted an error, as though it were a refusal");
    }

    // ---- a history out of time order: refused ------------------------------
    // The one refusal the node has. Every value of the result type is an
    // answer, `Unavailable` included, so the refusal is no value at all.
    {
        const motion::HumanoidAnimation backwards = HistoryOf({
            BufferedPose(kSecond + 0.02, 60.0f, GfVec3f(4.0f, 0.0f, 0.0f)),
            BufferedPose(kSecond - 0.02, 0.0f, GfVec3f(2.0f, 0.0f, 0.0f))});

        TfErrorMark mark;
        ExecUsdCacheView view = system.ComputeWithOverrides(
            request, HistoryOverride(clip, VtValue(backwards)));
        AssertRefused(view, kInterpolated);
        assert(PoseAt(view, kSampled).root.hasPosition &&
               "a refused history took motion.sampleAnimation with it");
        assert(MarkNames(mark, "motion.interpolatePose") &&
               "the out-of-order history was refused without naming the "
               "computation that refused it");
        mark.Clear();
    }

    // ---- an override of the wrong type: exec's refusal, not this node's ----
    // 26.08 documents that an override "must have the same type as the
    // computed value" and that otherwise "a coding error is emitted". What it
    // does not document is what the dependents then see, and that is measured
    // here: the override is dropped and the key computes its ordinary value,
    // so the node answers the clip's own pose as though nobody had overridden
    // anything. A driver that got the type wrong gets a TfError and a plausible
    // answer -- the one shape this bundle refuses to produce itself, and one it
    // cannot prevent here, because the substitution never reaches a callback.
    {
        TfErrorMark mark;
        ExecUsdCacheView view = system.ComputeWithOverrides(
            request,
            HistoryOverride(clip, VtValue(Bracketing().samples.front())));
        assert(!mark.IsClean() &&
               "a wrongly typed override was accepted without a word");
        // The coding error names the key and both types -- measured text is
        // "Expected override of value key '/Clip [motion.poseHistory]' to have
        // type 'motion::HumanoidAnimation'; got 'motion::HumanoidPose'".
        assert(MarkNames(mark, "motion.poseHistory") &&
               "the wrongly typed override's error does not name the key");

        // The key itself computed its ordinary value -- the clip's pose as a
        // history of one -- rather than going empty.
        assert(HistoryAt(view, kHistory).samples.size() == 1 &&
               "a wrongly typed override no longer falls back to the key's "
               "ordinary value");

        const motion::PoseSampleResult result = ResultAt(view, kInterpolated);
        assert(result.status == motion::PoseSampleStatus::Sampled);
        assert(result.pose);
        assert(std::abs(HeadDegrees(*result.pose) - kClipHeadDegrees) < 1e-3f &&
               "a wrongly typed override no longer falls back to the ordinary "
               "value -- the paragraph above needs rewriting");
        mark.Clear();
    }

    // ---- an empty override is a wrong type too -----------------------------
    // So a driver cannot push an *absence* into a key: an empty `VtValue` is
    // refused as a value of type 'void' and the key computes its ordinary value
    // exactly as above. The only way a dependent here is handed no value is
    // that the key's own callback set none.
    {
        TfErrorMark mark;
        ExecUsdCacheView view = system.ComputeWithOverrides(
            request, HistoryOverride(clip, VtValue()));
        assert(MarkNames(mark, "motion.poseHistory") &&
               "an empty override was accepted without a word");
        assert(HistoryAt(view, kHistory).samples.size() == 1 &&
               "an empty override emptied the key rather than being dropped");
        assert(ResultAt(view, kInterpolated).status ==
               motion::PoseSampleStatus::Sampled);
        mark.Clear();
    }

    // ---- the override does not survive the call ---------------------------
    {
        ExecUsdCacheView view = system.Compute(request);
        assert(HistoryAt(view, kHistory).samples.size() == 1 &&
               "a history override outlived the ComputeWithOverrides that "
               "supplied it");
        const motion::PoseSampleResult result = ResultAt(view, kInterpolated);
        assert(result.pose &&
               std::abs(HeadDegrees(*result.pose) - kClipHeadDegrees) < 1e-3f);
    }

    std::printf("execMotion interpolate: a driver's history is sampled at the "
                "evaluated instant, held past its end, Unavailable when empty, "
                "and refused out of order\n");
}

// ---------------------------------------------------------------------------
// Two overrides, two kinds of thing
// ---------------------------------------------------------------------------
// `motion.priorPose` is the graph's own previous answer, fed back; this node's
// history is the source's input, handed in. A driver of a live source holds
// both, and this is the measurement that says it can hand them over in **one**
// call, each reaching the nodes that depend on it and nothing else.
void TestTwoOverridesOfTwoKeysInOneCall(const std::string& fixture)
{
    UsdStageRefPtr stage = UsdStage::Open(fixture);
    assert(stage);
    UsdPrim clip = stage->GetPrimAtPath(SdfPath("/Clip"));
    assert(clip);

    ExecUsdSystem system(stage);
    std::vector<ExecUsdValueKey> keys;
    keys.emplace_back(clip, kInterpolatePose);
    keys.emplace_back(clip, kFilterPose);
    keys.emplace_back(clip, kPriorPose);
    keys.emplace_back(clip, kPoseHistory);
    ExecUsdRequest request = system.BuildRequest(std::move(keys));
    assert(request.IsValid());
    {
        // Arming, at the default time code, where this node refuses.
        TfErrorMark mark;
        system.Compute(request);
        mark.Clear();
    }
    system.ChangeTime(UsdTimeCode(kFrame));

    // The previous frame's answer: the four bones at identity, the hips at the
    // origin, one frame earlier.
    motion::HumanoidPose prior = BufferedPose(kSecond - 0.02, 0.0f,
                                              GfVec3f(0.0f));
    const motion::HumanoidAnimation bracketing = Bracketing();

    ExecUsdValueOverrideVector overrides;
    overrides.push_back(ExecUsdValueOverride{ExecUsdValueKey(clip, kPriorPose),
                                             VtValue(prior)});
    overrides.push_back(ExecUsdValueOverride{
        ExecUsdValueKey(clip, kPoseHistory), VtValue(bracketing)});
    ExecUsdCacheView view = system.ComputeWithOverrides(request,
                                                        std::move(overrides));

    // Each key carries its own substitute...
    assert(PoseAt(view, 2) == prior);
    assert(HistoryAt(view, 3) == bracketing);

    // ...the history reached the sampler of histories...
    const motion::PoseSampleResult result = ResultAt(view, 0);
    assert(result.pose && std::abs(HeadDegrees(*result.pose) - 30.0f) < 1e-3f);

    // ...and the prior reached the filter, which smoothed the **clip** against
    // it: the filtered hips are strictly between the prior's origin and the
    // clip's (0, 0.5, 1), and nowhere near the history's +X. Neither override
    // leaked into the other's dependents.
    const motion::HumanoidPose filtered = PoseAt(view, 1);
    assert(filtered.root.hasPosition);
    assert(filtered.root.worldPosition[2] > 0.0f &&
           filtered.root.worldPosition[2] < kClipHips[2] &&
           "the prior-pose override did not reach motion.filterPose");
    assert(filtered.root.worldPosition[0] == 0.0f &&
           "the history override leaked into motion.filterPose");

    std::printf("execMotion interpolate: a prior pose and a history, "
                "overridden together, each reach only their own dependents\n");
}

// ---------------------------------------------------------------------------
// A clip with no rate has no instant
// ---------------------------------------------------------------------------
void TestNoRateIsNoInstant(const std::string& fixture)
{
    UsdStageRefPtr stage = UsdStage::Open(fixture);
    assert(stage && "the unrated fixture did not open");
    UsdPrim clip = stage->GetPrimAtPath(SdfPath("/Clip"));
    assert(clip);

    ExecUsdSystem system(stage);
    ExecUsdRequest request = system.BuildRequest(KeysFor(clip));
    assert(request.IsValid());
    {
        // Arming: the sampler refuses (no rate) and so does this node.
        TfErrorMark mark;
        system.Compute(request);
        mark.Clear();
    }
    system.ChangeTime(UsdTimeCode(kFrame));

    // Un-overridden: the sampler refuses, the history forwards the absence
    // without a word of its own, and this node refuses in turn, naming itself.
    {
        TfErrorMark mark;
        ExecUsdCacheView view = system.Compute(request);
        AssertRefused(view, kSampled);
        AssertRefused(view, kHistory);
        AssertRefused(view, kInterpolated);
        assert(MarkNames(mark, "motion.interpolatePose") &&
               "the refusal reached motion.interpolatePose silently");
        mark.Clear();
    }

    // And a driver's history does not rescue it. The instant a history is
    // sampled at is the second the clip's rate turns the frame into; with no
    // rate there is no instant, whatever the history holds. That is the
    // conversion having one home -- the node reads the instant off the sampled
    // pose rather than converting the frame again.
    {
        TfErrorMark mark;
        ExecUsdCacheView view = system.ComputeWithOverrides(
            request, HistoryOverride(clip, VtValue(Bracketing())));
        AssertRefused(view, kInterpolated);
        assert(MarkNames(mark, "motion.interpolatePose"));
        mark.Clear();
    }

    std::printf("execMotion interpolate: a clip with no rate has no instant "
                "to sample a history at, supplied or not\n");
}

} // namespace

int main(int argc, char** argv)
{
    assert(argc == 3 &&
           "usage: execMotion_interpolate <sampled_clip.usda> "
           "<unrated_clip.usda>");
    TestUnoverriddenTheNodeIsTheClip(argv[1]);
    TestAHistoryIsSampledAtTheEvaluatedInstant(argv[1]);
    TestTwoOverridesOfTwoKeysInOneCall(argv[1]);
    TestNoRateIsNoInstant(argv[2]);
    std::printf("execMotion interpolate: all checks passed\n");
    return 0;
}
