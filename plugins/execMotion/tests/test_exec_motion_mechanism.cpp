// SPDX-License-Identifier: Apache-2.0
//
// The mechanism, end to end: discovery, request compile, compute, invalidation,
// recompute — P0-4 step 1's list, run against the built bundle.
//
// This executable deliberately does **not** link the plugin. It reaches
// `motion.identityPose` only through OpenExec's own lazy loading, driven by the
// `Info.Exec.Schemas` block in execMotion's plugInfo.json, so a plugInfo that is
// not staged fails here rather than shipping. That failure mode is the reason
// this test exists at all: a missing block does not fail loudly at load, it
// presents as "computation not found" (the 26.08 audit, §2.1).
//
// What it cannot assert, and why: that a cached value was not *recomputed*.
// A callback is required to be pure, so the only way to count invocations is to
// instrument one — and a counter exported from a shipped plugin is a worse thing
// to own than a slightly weaker test. What is asserted instead is the observable
// half: no invalidation was reported and the value is identical. The stronger
// measurement was taken with a throwaway probe and is recorded in
// docs/reports/openusd/26.08-openexec-mechanism.md §3.

#include "pxr/pxr.h"

#include "pxr/base/tf/errorMark.h"
#include "pxr/base/tf/token.h"
#include "pxr/base/vt/value.h"

#include "pxr/exec/exec/request.h"
#include "pxr/exec/exec/systemDiagnostics.h"
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

#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

namespace {

const TfToken kIdentityPose("motion.identityPose");

bool Has(const motion::HumanoidPose& pose, motion::HumanBone bone)
{
    return pose.validRotations.test(static_cast<std::size_t>(bone));
}

} // namespace

int main(int argc, char** argv)
{
    assert(argc == 2 && "usage: execMotion_mechanism <identity_clip.usda>");
    const std::string fixture = argv[1];

    // The fixture is sublayered into an in-memory stage rather than opened
    // directly, so the authored change below lands in a layer that has no file
    // behind it. A test that edits its own fixture in place is one Save() away
    // from rewriting the input it is checking against.
    UsdStageRefPtr stage = UsdStage::CreateInMemory();
    assert(stage && "no in-memory stage");
    stage->GetRootLayer()->GetSubLayerPaths().push_back(fixture);
    assert(stage->GetRootLayer()->GetNumSubLayerPaths() == 1 &&
           "the fixture was not sublayered");

    // Authored so the pose can be checked for NOT carrying a converted time:
    // 50 time codes per second is a rate a computation cannot see, and a pose
    // whose timestamp were ever non-zero would mean one had been guessed.
    stage->SetTimeCodesPerSecond(50.0);

    UsdPrim clip = stage->GetPrimAtPath(SdfPath("/Clip"));
    assert(clip && "the fixture has no /Clip prim");

    // The system extends the stage's lifetime and is meant to live beside it;
    // one per stage, never one per frame (execUsd/system.h says so in as many
    // words, and the shipped ExecIr example holds one as a member).
    ExecUsdSystem system(stage);

    int valueInvalidations = 0;
    int timeInvalidations = 0;

    std::vector<ExecUsdValueKey> keys;
    keys.emplace_back(clip, kIdentityPose);
    ExecUsdRequest request = system.BuildRequest(
        std::move(keys),
        [&valueInvalidations](const ExecRequestIndexSet&,
                              const EfTimeInterval&) {
            // Calling back into execution from here is forbidden by the header,
            // so this counts and returns.
            ++valueInvalidations;
        },
        [&timeInvalidations](const ExecRequestIndexSet&) {
            ++timeInvalidations;
        });
    assert(request.IsValid() && "the request did not compile");

    // ---- compute ----------------------------------------------------------
    ExecUsdCacheView view = system.Compute(request);
    VtValue value = view.Get(0);
    assert(!value.IsEmpty() &&
           "no value came back -- if the plugInfo is unstaged this is what it "
           "looks like, not a load error");
    assert(value.IsHolding<motion::HumanoidPose>() &&
           "the canonical aggregate did not survive the boundary");

    const motion::HumanoidPose first = value.UncheckedGet<motion::HumanoidPose>();
    assert(first.validRotations.count() == 4);
    assert(Has(first, motion::HumanBone::Hips));
    assert(Has(first, motion::HumanBone::Spine));
    assert(Has(first, motion::HumanBone::Chest));
    assert(Has(first, motion::HumanBone::Head));

    // ---- recompute with nothing changed -----------------------------------
    const int invalidationsBefore = valueInvalidations;
    ExecUsdCacheView view2 = system.Compute(request);
    const VtValue value2 = view2.Get(0);
    assert(valueInvalidations == invalidationsBefore &&
           "an unchanged stage reported an invalidation");
    assert(value2.IsHolding<motion::HumanoidPose>());
    assert(value2.UncheckedGet<motion::HumanoidPose>() == first &&
           "the same request at the same time returned a different value");

    // ---- an authored change invalidates, and the recompute sees it --------
    UsdAttribute joints = clip.GetAttribute(TfToken("joints"));
    assert(joints && "the fixture clip has no joints attribute");
    VtArray<TfToken> jointPaths;
    assert(joints.Get(&jointPaths));
    jointPaths.push_back(TfToken("hips/spine/chest/leftShoulder"));
    assert(joints.Set(jointPaths));

    assert(valueInvalidations > invalidationsBefore &&
           "authoring an input did not reach the value callback");

    ExecUsdCacheView view3 = system.Compute(request);
    const VtValue value3 = view3.Get(0);
    assert(value3.IsHolding<motion::HumanoidPose>());
    const motion::HumanoidPose second =
        value3.UncheckedGet<motion::HumanoidPose>();
    assert(second.validRotations.count() == 5);
    assert(Has(second, motion::HumanBone::LeftShoulder));

    // ---- moving time changes nothing, and exec knows it -------------------
    // `motion.identityPose` has one input, a `uniform` array, so it does not
    // depend on time -- and it deliberately carries no timestamp, because
    // `HumanoidPose::timestamp` is seconds, a computation is handed a frame, and
    // the rate between them is stage metadata exec does not deliver to a
    // callback (measured: `Stage().Metadata<double>(timeCodesPerSecond)` is
    // accepted, is not refused with `.Required()`, and still yields no value).
    //
    // So what ChangeTime is asserted to do here is *nothing*, which is the
    // header's own promise: the time callback carries "value keys which are
    // time dependent, and for which input values are changing between the old
    // time and new time". A value key that fired here would mean this
    // computation had acquired a time dependency nobody declared.
    const int timeInvalidationsBefore = timeInvalidations;
    system.ChangeTime(UsdTimeCode(200.0));
    ExecUsdCacheView view4 = system.Compute(request);
    const VtValue value4 = view4.Get(0);
    assert(value4.IsHolding<motion::HumanoidPose>());
    assert(value4.UncheckedGet<motion::HumanoidPose>() == second &&
           "moving the frame changed a pose that does not depend on the frame");
    assert(value4.UncheckedGet<motion::HumanoidPose>().timestamp == 0.0 &&
           "a second was invented from a frame and a rate exec cannot see");
    assert(timeInvalidations == timeInvalidationsBefore &&
           "ChangeTime reported a value key with no time-dependent input");

    // ---- explicit invalidation expires the request ------------------------
    // `Diagnostics::InvalidateAll()` does not merely drop cached values: it
    // **expires every outstanding request**, and the request does not say so.
    // `IsValid()` is documented as "calling PrepareRequest or Compute is
    // allowed" and keeps returning true, while Compute posts two coding errors
    // and hands back a view nothing can be extracted from. A caller that holds
    // a long-lived request -- which every caller does, because the system is
    // meant to live beside the stage -- has to rebuild it.
    //
    // Asserted rather than described: nothing in the headers says this, so the
    // day it changes upstream should be a red test and not a silent one.
    {
        ExecSystem::Diagnostics diagnostics(&system);
        diagnostics.InvalidateAll();
    }
    assert(request.IsValid() &&
           "InvalidateAll now reports the request as invalid, which is what it "
           "should always have done -- the workaround below can go");
    {
        TfErrorMark expired;
        ExecUsdCacheView stale = system.Compute(request);
        assert(stale.Get(0).IsEmpty() &&
               "an expired request computed a value after all");
        assert(!expired.IsClean() &&
               "an expired request failed silently, which is worse");
        expired.Clear();
    }

    std::vector<ExecUsdValueKey> rebuiltKeys;
    rebuiltKeys.emplace_back(clip, kIdentityPose);
    ExecUsdRequest rebuilt = system.BuildRequest(std::move(rebuiltKeys));
    ExecUsdCacheView view5 = system.Compute(rebuilt);
    const VtValue value5 = view5.Get(0);
    assert(value5.IsHolding<motion::HumanoidPose>());
    // The stage's edit survived the invalidation -- the joint the authored
    // change added is still there, so InvalidateAll dropped computed values and
    // not the scene.
    assert(value5.UncheckedGet<motion::HumanoidPose>().validRotations.count() == 5 &&
           "InvalidateAll lost an authored change");

    assert(value5.UncheckedGet<motion::HumanoidPose>() ==
           value4.UncheckedGet<motion::HumanoidPose>() &&
           "an explicit invalidation changed the answer");

    // **InvalidateAll also resets the system's time**, and this suite can no
    // longer see it: the only witness was a pose that carried the frame it was
    // computed at, and carrying one meant writing a frame into a field that
    // means seconds. The measurement is kept in
    // docs/reports/openusd/26.08-openexec-mechanism.md §4 with the method that
    // produced it, rather than kept here at the price of a wrong number and a
    // time dependency this computation does not have.

    // ---- a computation nobody registered ----------------------------------
    // The shape a missing Info.Exec.Schemas block presents as. It is a coding
    // error plus an empty value, never a load failure -- so a diagnostic of our
    // own is the only thing that would name it, which is what P1-1 owes.
    {
        TfErrorMark mark;
        std::vector<ExecUsdValueKey> missingKeys;
        missingKeys.emplace_back(clip, TfToken("motion.noSuchComputation"));
        ExecUsdRequest missing = system.BuildRequest(std::move(missingKeys));
        ExecUsdCacheView missingView = system.Compute(missing);
        assert(missingView.Get(0).IsEmpty() &&
               "an unregistered computation returned a value");
        assert(!mark.IsClean() &&
               "an unregistered computation was silent");
        mark.Clear();
    }

    std::printf("execMotion mechanism: all checks passed "
                "(value invalidations=%d, time invalidations=%d)\n",
                valueInvalidations, timeInvalidations);
    return 0;
}
