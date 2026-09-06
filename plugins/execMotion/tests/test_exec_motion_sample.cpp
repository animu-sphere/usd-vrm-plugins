// SPDX-License-Identifier: Apache-2.0
//
// `motion.sampleAnimation` through the built bundle: the first computation in
// this bundle with a value to get wrong, and the first with a time dependency.
//
// Separate from `execMotion_mechanism` on purpose. That suite holds the
// mechanism at its weakest possible value -- the identity -- so a red result
// there is a discovery, compile, cache or invalidation failure and nothing
// else. This one moves the frame and reads the pose, over three clips that
// differ by one thing each: one keyed, one holding still, one stating no rate.
// It carries the two measurements the node's signature was decided on:
//
//   * a clip states the rate its frames are counted at, as an authored
//     attribute, because exec delivers no stage metadata to a callback
//     (docs/reports/openusd/26.08-openexec-mechanism.md §5); and
//   * a clip that states none is refused rather than stamped with a guess.
//
// Like the mechanism suite, this executable does not link the plugin: the
// computation is reached only through `PXR_PLUGINPATH_NAME` and the staged
// plugInfo.json.

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
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

namespace {

const TfToken kSampleAnimation("motion.sampleAnimation");

bool Has(const motion::HumanoidPose& pose, motion::HumanBone bone)
{
    return pose.validRotations.test(static_cast<std::size_t>(bone));
}

const GfQuatf& RotationOf(const motion::HumanoidPose& pose,
                          motion::HumanBone bone)
{
    return pose.localRotations[static_cast<std::size_t>(bone)];
}

// The angle of a unit quaternion, in degrees. Formed in double precision from
// the normalized quaternion, because acos turns a float's last bit into a
// milliradian near zero -- the trap motionCore's own comparison documents.
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

motion::HumanoidPose ComputePose(ExecUsdSystem& system,
                                 ExecUsdRequest& request)
{
    ExecUsdCacheView view = system.Compute(request);
    const VtValue value = view.Get(0);
    assert(!value.IsEmpty() &&
           "no value came back -- if the plugInfo is unstaged this is what it "
           "looks like, not a load error");
    assert(value.IsHolding<motion::HumanoidPose>() &&
           "the canonical aggregate did not survive the boundary");
    return value.UncheckedGet<motion::HumanoidPose>();
}

// ---------------------------------------------------------------------------
// The clip that states its rate
// ---------------------------------------------------------------------------
void TestASampledClip(const std::string& fixture)
{
    UsdStageRefPtr stage = UsdStage::Open(fixture);
    assert(stage && "the sampled fixture did not open");

    UsdPrim clip = stage->GetPrimAtPath(SdfPath("/Clip"));
    assert(clip && "the fixture has no /Clip prim");

    // The authored rate and the stage's own metadatum state the same number,
    // and the duplication is deliberate: the second is what a tool reads and
    // the first is the only one a computation can. A fixture where they drifted
    // would make every timestamp below wrong in a way no assertion on the pose
    // alone could attribute, so it is checked here rather than assumed.
    double authoredRate = 0.0;
    assert(clip.GetAttribute(TfToken("motion:timeCodesPerSecond"))
               .Get(&authoredRate));
    assert(authoredRate == stage->GetTimeCodesPerSecond() &&
           "the clip's authored rate and the stage's metadatum disagree");

    ExecUsdSystem system(stage);

    int timeInvalidations = 0;
    std::vector<ExecUsdValueKey> keys;
    keys.emplace_back(clip, kSampleAnimation);
    ExecUsdRequest request = system.BuildRequest(
        std::move(keys),
        [](const ExecRequestIndexSet&, const EfTimeInterval&) {},
        [&timeInvalidations](const ExecRequestIndexSet&) {
            ++timeInvalidations;
        });
    assert(request.IsValid() && "the request did not compile");

    // ---- the default time code is not frame zero --------------------------
    // A system evaluates at the default time until ChangeTime is called, and
    // this clip authors every channel as time samples and no default value at
    // all -- so what USD resolves there is nothing, and the pose says nothing:
    // no bone, no root, no second. That is the honest answer for a clip asked
    // about a time it has no opinion at, and it is the reason the seam treats
    // "no numeric frame" as "no timestamp" rather than as frame zero.
    const motion::HumanoidPose atDefault = ComputePose(system, request);
    assert(atDefault.validRotations.count() == 0 &&
           "a clip with no default values resolved to a pose at the default "
           "time code");
    assert(!atDefault.root.hasPosition);
    assert(atDefault.timestamp == 0.0);

    // ---- frame 0 ----------------------------------------------------------
    const int timeInvalidationsBefore = timeInvalidations;
    system.ChangeTime(UsdTimeCode(0.0));
    const motion::HumanoidPose atZero = ComputePose(system, request);

    // Unlike `motion.identityPose`, this value key IS time dependent and the
    // request is told so. What this assertion alone cannot say is *why*: this
    // node declares time-sampled attribute inputs and the builtin `computeTime`,
    // and either would do it. `TestAClipThatHoldsStill` below removes the first
    // and keeps the second, which is what actually separates them.
    assert(timeInvalidations > timeInvalidationsBefore &&
           "moving the frame did not reach the time callback of a computation "
           "that reads both time-sampled attributes and the stage's time");

    assert(atZero.timestamp == 0.0);
    assert(atZero.validRotations.count() == 4 &&
           "the clip names four canonical bones and one thing that is not one");
    assert(Has(atZero, motion::HumanBone::Hips));
    assert(Has(atZero, motion::HumanBone::Spine));
    assert(Has(atZero, motion::HumanBone::Chest));
    assert(Has(atZero, motion::HumanBone::Head));
    assert(NearlyEqual(AngleDegrees(RotationOf(atZero, motion::HumanBone::Head)),
                       0.0, 1e-3));
    assert(atZero.root.hasPosition);
    assert(NearlyEqual(atZero.root.worldPosition, GfVec3f(0.0f), 1e-6));

    // ---- frame 100, which is second 2 at 50 time codes per second ---------
    system.ChangeTime(UsdTimeCode(100.0));
    const motion::HumanoidPose atHundred = ComputePose(system, request);
    assert(NearlyEqual(atHundred.timestamp, 2.0, 1e-12) &&
           "the frame reached the pose as a frame rather than as a second");
    assert(NearlyEqual(
               AngleDegrees(RotationOf(atHundred, motion::HumanBone::Head)),
               90.0, 1e-2));

    // Only the hips carry body translation. The clip authors a translation for
    // every joint -- including 9,9,9 on the one that names no bone -- so a node
    // that took the wrong row would land somewhere obviously wrong rather than
    // plausibly wrong.
    assert(atHundred.root.hasPosition);
    assert(NearlyEqual(atHundred.root.worldPosition, GfVec3f(0.0f, 1.0f, 2.0f),
                       1e-6));

    // ---- frame 50, which nothing keyed ------------------------------------
    // Sampling between two keys is USD's answer, not this bundle's: an exec
    // input arrives already resolved at the evaluated time. Measured rather
    // than assumed, because it is the half of `motion.sampleAnimation` that is
    // NOT a wrapper over `motion::SampleAnimation` -- that function holds a
    // whole animation and performs its own lookup, and P0-6 parity is where the
    // two answers get compared. USD documents quaternion arrays as interpolated
    // by slerp, and a 90 degree turn halfway is 45 degrees.
    system.ChangeTime(UsdTimeCode(50.0));
    const motion::HumanoidPose atFifty = ComputePose(system, request);
    assert(NearlyEqual(atFifty.timestamp, 1.0, 1e-12));
    assert(NearlyEqual(AngleDegrees(RotationOf(atFifty, motion::HumanBone::Head)),
                       45.0, 1e-2) &&
           "a frame between two keys was not slerped");
    assert(NearlyEqual(atFifty.root.worldPosition, GfVec3f(0.0f, 0.5f, 1.0f),
                       1e-6));

    // ---- the same frame twice ---------------------------------------------
    const motion::HumanoidPose again = ComputePose(system, request);
    assert(again == atFifty &&
           "the same request at the same time returned a different value");
}

// ---------------------------------------------------------------------------
// The clip that does not
// ---------------------------------------------------------------------------
void TestAClipWithNoRate(const std::string& fixture)
{
    UsdStageRefPtr stage = UsdStage::Open(fixture);
    assert(stage && "the unrated fixture did not open");
    // The number is on the stage, and that is the whole difficulty: a tool
    // reads it here and a computation cannot.
    assert(stage->GetTimeCodesPerSecond() == 50.0);

    UsdPrim clip = stage->GetPrimAtPath(SdfPath("/Clip"));
    assert(clip && "the fixture has no /Clip prim");
    assert(!clip.GetAttribute(TfToken("motion:timeCodesPerSecond")) &&
           "the unrated fixture states a rate after all");

    ExecUsdSystem system(stage);
    system.ChangeTime(UsdTimeCode(100.0));

    std::vector<ExecUsdValueKey> keys;
    keys.emplace_back(clip, kSampleAnimation);

    TfErrorMark mark;
    ExecUsdRequest request = system.BuildRequest(std::move(keys));

    // **`.Required()` does not refuse a missing attribute**, measured here
    // rather than assumed: the input is declared `.Required()` and the stage
    // carries no `motion:timeCodesPerSecond` at all, and the request compiles
    // anyway and runs the callback with an input that has no value. That is the
    // same shape §5 of the mechanism report found for a `.Required()` stage
    // metadatum, on a second kind of input -- so in 26.08 the refusal has to be
    // the callback's, and this assertion is what turns an upstream change to
    // that into a red test rather than a silent one.
    assert(request.IsValid() &&
           "a missing .Required() attribute now refuses the request, which is "
           "what it should always have done -- the callback's own refusal "
           "below can become an assertion that it never runs");

    ExecUsdCacheView view = system.Compute(request);
    const VtValue value = view.Get(0);
    assert(value.IsHolding<motion::HumanoidPose>() &&
           "the callback did not run, so something other than the rate is "
           "missing");

    // A pose with nothing in it, rather than a pose stamped with a guess. The
    // difference is the whole reason the node refuses: an empty pose is
    // recognisable downstream and a wrong second is not.
    const motion::HumanoidPose pose = value.UncheckedGet<motion::HumanoidPose>();
    assert(pose == motion::HumanoidPose{} &&
           "a clip with no rate produced a pose with content in it");
    assert(pose.timestamp == 0.0 &&
           "a second was invented from a frame and a rate nobody stated");

    // And it said so, in this bundle's own words. Asserting the text rather
    // than only the mark is what distinguishes our refusal from any other error
    // exec might have posted on the way -- without it, this suite would pass on
    // an unrelated failure that happened to leave the mark dirty.
    assert(!mark.IsClean() &&
           "a clip with no rate was refused silently, which is worse than "
           "being refused");
    bool named = false;
    for (TfErrorMark::Iterator it = mark.GetBegin(); it != mark.GetEnd(); ++it) {
        if (it->GetCommentary().find("motion.sampleAnimation")
            != std::string::npos) {
            named = true;
        }
    }
    assert(named &&
           "the errors posted came from somewhere other than the computation");
    mark.Clear();

    std::printf("execMotion sample: a clip with no rate compiles a request and "
                "is refused by the callback, with an empty pose\n");
}

// ---------------------------------------------------------------------------
// The clip that holds still, which is the control
// ---------------------------------------------------------------------------
void TestAClipThatHoldsStill(const std::string& fixture)
{
    UsdStageRefPtr stage = UsdStage::Open(fixture);
    assert(stage && "the static fixture did not open");

    UsdPrim clip = stage->GetPrimAtPath(SdfPath("/Clip"));
    assert(clip && "the fixture has no /Clip prim");

    // The premise of the whole test: this clip states its values as defaults
    // and keys nothing. If a time sample ever appeared here, the measurement
    // below would quietly stop isolating anything.
    std::vector<double> times;
    assert(clip.GetAttribute(TfToken("rotations")).GetTimeSamples(&times));
    assert(times.empty() && "the static fixture has time samples after all");
    assert(clip.GetAttribute(TfToken("translations")).GetTimeSamples(&times));
    assert(times.empty() && "the static fixture has time samples after all");

    ExecUsdSystem system(stage);

    int timeInvalidations = 0;
    std::vector<ExecUsdValueKey> keys;
    keys.emplace_back(clip, kSampleAnimation);
    ExecUsdRequest request = system.BuildRequest(
        std::move(keys),
        [](const ExecRequestIndexSet&, const EfTimeInterval&) {},
        [&timeInvalidations](const ExecRequestIndexSet&) {
            ++timeInvalidations;
        });
    assert(request.IsValid());

    // A clip whose values are defaults resolves at the default time code, which
    // the keyed fixture does not -- so this pose is a pose, and the contrast in
    // TestASampledClip is about the clip rather than about the node.
    const motion::HumanoidPose atDefault = ComputePose(system, request);
    assert(atDefault.validRotations.count() == 4);
    assert(atDefault.timestamp == 0.0);

    system.ChangeTime(UsdTimeCode(0.0));
    const motion::HumanoidPose atZero = ComputePose(system, request);
    assert(atZero.validRotations.count() == 4);

    // ---- the isolation ----------------------------------------------------
    // Same node, same declared inputs, and no attribute on this clip has a time
    // sample. **The request is still told.** So a clip's time samples are not
    // what carries the time dependency, and the value key is reported on a frame
    // change that changes none of its attribute values.
    //
    // Which of the node's inputs carries it was settled with a throwaway probe
    // rather than here, because separating the two candidates needs a build with
    // one of them deleted and no shipped bundle should carry a computation that
    // exists to be measured. With `computeTime` removed, this clip stops being
    // reported and the keyed one goes on being reported -- so a keyed attribute
    // and `computeTime` are each sufficient alone, and neither is necessary
    // (docs/reports/openusd/26.08-openexec-sampling.md §3 has the four cells).
    //
    // The cell that matters for the later nodes is this one: a node that
    // declares `computeTime` is recomputed on every frame change even when
    // nothing it reads has moved. `motion.filterPose` and `motion.blendPoses`
    // should declare it because they use it, never as a formality.
    const int before = timeInvalidations;
    system.ChangeTime(UsdTimeCode(100.0));
    assert(timeInvalidations > before &&
           "a value key over a clip with no time samples was NOT reported when "
           "the frame moved -- the sampling report's section 3 says it is, and "
           "one of them is now wrong");

    // The pose itself is the same one, because nothing in the clip moved. The
    // stamp is not: the frame is an input to the second, and this is the one
    // thing in the pose that a static clip still changes.
    const motion::HumanoidPose atHundred = ComputePose(system, request);
    assert(NearlyEqual(atHundred.timestamp, 2.0, 1e-12));
    assert(atHundred.validRotations == atZero.validRotations);
    assert(atHundred.root.worldPosition == atZero.root.worldPosition);
}

} // namespace

int main(int argc, char** argv)
{
    assert(argc == 4 &&
           "usage: execMotion_sample <sampled_clip.usda> <static_clip.usda> "
           "<unrated_clip.usda>");
    TestASampledClip(argv[1]);
    TestAClipThatHoldsStill(argv[2]);
    TestAClipWithNoRate(argv[3]);
    std::printf("execMotion sample: all checks passed\n");
    return 0;
}
