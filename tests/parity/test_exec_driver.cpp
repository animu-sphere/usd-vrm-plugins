// SPDX-License-Identifier: Apache-2.0
//
// exec_driver_contract -- the driver contract, run against execMotion
// (docs/design/MOTION_CONTRACT.md, "OpenExec driver contract").
//
// Each `VRM_OPENEXEC_*` code is raised here by the failure it names, and each
// is held to its subject: a request with one key nobody can compute beside one
// that answers names that key and still answers the other. Where a line of the
// contract exists because exec behaves in a way its headers do not say, the
// behaviour is measured here too, on the raw system, so the day it changes
// upstream is a red test and not a driver compensating for nothing.
//
// Like the mechanism suite it links no plugin: execMotion is reached through
// PXR_PLUGINPATH_NAME, which is also how a missing computation is produced --
// by naming one that no plugin registers.

#include "ExecDriver.h"

#include "pxr/pxr.h"

#include "pxr/base/tf/errorMark.h"
#include "pxr/base/tf/token.h"
#include "pxr/base/vt/value.h"

#include "pxr/exec/exec/systemDiagnostics.h"
#include "pxr/exec/execUsd/cacheView.h"
#include "pxr/exec/execUsd/request.h"
#include "pxr/exec/execUsd/system.h"
#include "pxr/exec/execUsd/valueKey.h"
#include "pxr/exec/execUsd/valueOverride.h"

#include "pxr/usd/sdf/layer.h"
#include "pxr/usd/sdf/path.h"
#include "pxr/usd/usd/prim.h"
#include "pxr/usd/usd/references.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usd/timeCode.h"

#include <motionCore/Humanoid.h>
#include <motionRuntime/MotionSource.h>

#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

using execdriver::Driver;
using execdriver::Frame;
using execdriver::Key;
using execdriver::OpenExecDiagnostic;
using execdriver::OpenExecDiagnosticCode;
using execdriver::Override;

namespace {

const SdfPath kClip("/Clip");
const TfToken kSample("motion.sampleAnimation");
const TfToken kFilter("motion.filterPose");
const TfToken kPrior("motion.priorPose");
const TfToken kHistory("motion.poseHistory");
const TfToken kInterpolate("motion.interpolatePose");
const TfToken kNobody("motion.noSuchComputation");

std::string gFixture;

// The fixture sublayered into an in-memory stage, so an authored edit lands
// in a layer with no file behind it.
UsdStageRefPtr Open()
{
    UsdStageRefPtr stage = UsdStage::CreateInMemory();
    stage->GetRootLayer()->GetSubLayerPaths().push_back(gFixture);
    stage->SetTimeCodesPerSecond(50.0);
    assert(stage->GetPrimAtPath(kClip) && "the fixture has no /Clip");
    return stage;
}

bool Contains(const std::string& text, const std::string& part)
{
    return text.find(part) != std::string::npos;
}

bool AnyContains(const std::vector<std::string>& lines, const std::string& part)
{
    for (const std::string& line : lines) {
        if (Contains(line, part)) {
            return true;
        }
    }
    return false;
}

const OpenExecDiagnostic* Find(const Frame& frame, OpenExecDiagnosticCode code,
                               const std::string& subject)
{
    for (const OpenExecDiagnostic& d : frame.diagnostics.reported) {
        if (d.code == code && d.subject == subject) {
            return &d;
        }
    }
    return nullptr;
}

const std::string kSampleName = "/Clip [motion.sampleAnimation]";
const std::string kFilterName = "/Clip [motion.filterPose]";
const std::string kPriorName = "/Clip [motion.priorPose]";
const std::string kHistoryName = "/Clip [motion.poseHistory]";

// ---------------------------------------------------------------------------

void TheTable()
{
    using execdriver::FindOpenExecDiagnosticCode;
    using execdriver::OpenExecDiagnosticCodeString;
    assert(OpenExecDiagnosticCodeString(
               OpenExecDiagnosticCode::ComputationUnavailable)
           == "VRM_OPENEXEC_COMPUTATION_UNAVAILABLE");
    assert(OpenExecDiagnosticCodeString(OpenExecDiagnosticCode::TypeMismatch)
           == "VRM_OPENEXEC_TYPE_MISMATCH");
    assert(OpenExecDiagnosticCodeString(OpenExecDiagnosticCode::Invalidated)
           == "VRM_OPENEXEC_INVALIDATED");
    assert(OpenExecDiagnosticCodeString(OpenExecDiagnosticCode::Count).empty());
    for (std::size_t i = 0; i < execdriver::OpenExecDiagnosticCodeCount; ++i) {
        const auto code = static_cast<OpenExecDiagnosticCode>(i);
        assert(FindOpenExecDiagnosticCode(OpenExecDiagnosticCodeString(code))
               == code);
    }
    assert(!FindOpenExecDiagnosticCode("VRM_RETARGET_UNBOUND_DRIVEN_BONE") &&
           "the two namespaces are separate");

    // Two errors that end a frame, one warning the driver recovered from.
    assert(!execdriver::OpenExecDiagnosticIsRecoverable(
        OpenExecDiagnosticCode::ComputationUnavailable));
    assert(!execdriver::OpenExecDiagnosticIsRecoverable(
        OpenExecDiagnosticCode::TypeMismatch));
    assert(execdriver::OpenExecDiagnosticIsRecoverable(
        OpenExecDiagnosticCode::Invalidated));
    assert(execdriver::OpenExecDiagnosticDefaultSeverity(
               OpenExecDiagnosticCode::Invalidated)
           == execdriver::OpenExecDiagnosticSeverity::Warning);

    assert(execdriver::FormatOpenExecDiagnostic(execdriver::MakeOpenExecDiagnostic(
               OpenExecDiagnosticCode::Invalidated, kSampleName, "rebuilt"))
           == "[VRM_OPENEXEC_INVALIDATED] warning recoverable "
              "subject=/Clip [motion.sampleAnimation]: rebuilt");
    assert(execdriver::FormatOpenExecDiagnostic(execdriver::MakeOpenExecDiagnostic(
               OpenExecDiagnosticCode::TypeMismatch, kPriorName))
           == "[VRM_OPENEXEC_TYPE_MISMATCH] error "
              "subject=/Clip [motion.priorPose]");

    execdriver::OpenExecDiagnostics list;
    assert(list.Report(execdriver::MakeOpenExecDiagnostic(
        OpenExecDiagnosticCode::TypeMismatch, kPriorName, "first")));
    assert(!list.Report(execdriver::MakeOpenExecDiagnostic(
               OpenExecDiagnosticCode::TypeMismatch, kPriorName, "second")) &&
           "a code and subject are reported once");
    assert(list.reported.size() == 1 && list.reported[0].detail == "first");
    assert(list.HasError());
}

// Contract: arm each request with one compute, and keep what it posted.
void ArmingKeepsItsRefusals()
{
    Driver driver(Open());
    Frame arming;
    const Driver::RequestId id = driver.Add(
        {Key::Of<motion::PoseSampleResult>(kClip, kInterpolate)}, &arming);

    // The arm runs at the default time code, where the history sampler refuses
    // by design -- so the refusal is posted, kept, and is not a failure.
    assert(arming.values.size() == 1 && arming.values[0].IsEmpty());
    assert(AnyContains(arming.refusals, "motion.interpolatePose") &&
           "the arming compute's refusal was not kept");
    assert(arming.diagnostics.IsClean() && arming.errors.empty());
    assert(!arming.Failed() && "a by-design refusal failed the arming frame");

    const Frame frame = driver.Evaluate(id, UsdTimeCode(50.0));
    const auto* result = frame.Get<motion::PoseSampleResult>(0);
    assert(result && result->status == motion::PoseSampleStatus::Sampled);
    assert(frame.refusals.empty() && !frame.Failed());
}

// VRM_OPENEXEC_COMPUTATION_UNAVAILABLE, from a name nobody registers.
void AComputationNobodyRegisters()
{
    Driver driver(Open());
    Frame arming;
    const Driver::RequestId id = driver.Add(
        {Key::Of<motion::HumanoidPose>(kClip, kSample),
         Key::Of<motion::HumanoidPose>(kClip, kNobody)},
        &arming);

    const OpenExecDiagnostic* missing =
        Find(arming, OpenExecDiagnosticCode::ComputationUnavailable,
             "/Clip [motion.noSuchComputation]");
    assert(missing && "a computation nobody registers was not named");
    assert(arming.diagnostics.reported.size() == 1 &&
           "the key that exists was named as well");
    // Exec's own words travel in the detail, for a person; the code came
    // from the driver's probe, not from them.
    assert(Contains(missing->detail, "Failed to find computation"));
    assert(arming.errors.empty() && "the complaint was not attributed");

    const Frame frame = driver.Evaluate(id, UsdTimeCode(50.0));
    const auto* pose = frame.Get<motion::HumanoidPose>(0);
    assert(pose && pose->validRotations.any() &&
           "the key beside the missing one stopped answering");
    assert(frame.values[1].IsEmpty());
    assert(Find(frame, OpenExecDiagnosticCode::ComputationUnavailable,
                "/Clip [motion.noSuchComputation]") &&
           "an unavailable key is reported in every frame that asks for it");
    assert(frame.Failed());
    // Not handed to exec again, so exec has nothing to complain about.
    assert(frame.errors.empty());
}

// VRM_OPENEXEC_COMPUTATION_UNAVAILABLE, from a provider exec would expire.
void AProviderThatIsNotThere()
{
    Driver driver(Open());
    Frame arming;
    driver.Add({Key::Of<motion::HumanoidPose>(SdfPath("/NoSuchPrim"), kSample)},
               &arming);
    const OpenExecDiagnostic* missing =
        Find(arming, OpenExecDiagnosticCode::ComputationUnavailable,
             "/NoSuchPrim [motion.sampleAnimation]");
    assert(missing && Contains(missing->detail, "there is no prim"));
    assert(arming.errors.empty() &&
           "a key on no prim reached exec, which posts at every compile");
}

// A provider deactivated, then active again: unavailable while it is gone,
// silent when it is back, and INVALIDATED when it goes and comes back between
// two frames. Two clips, and only the second moves, because exec reports an
// expiry through IsValid() only while some key of the request is still live.
void AProviderThatGoesAndComesBack()
{
    UsdStageRefPtr stage = Open();
    // The second clip is the first, referenced: its own prim, its own
    // provider, the same answer.
    const SdfPath other("/Other");
    stage->DefinePrim(other).GetReferences().AddInternalReference(kClip);
    const std::string otherName = "/Other [motion.sampleAnimation]";

    Driver driver(stage);
    const Driver::RequestId id =
        driver.Add({Key::Of<motion::HumanoidPose>(kClip, kSample),
                    Key::Of<motion::HumanoidPose>(other, kSample)});
    const Frame before = driver.Evaluate(id, UsdTimeCode(50.0));
    assert(before.Get<motion::HumanoidPose>(0) && before.Get<motion::HumanoidPose>(1));
    const motion::HumanoidPose answered = *before.Get<motion::HumanoidPose>(1);
    assert(answered.validRotations.any());

    stage->GetPrimAtPath(other).SetActive(false);
    const Frame gone = driver.Evaluate(id, UsdTimeCode(50.0));
    const OpenExecDiagnostic* missing = Find(
        gone, OpenExecDiagnosticCode::ComputationUnavailable, otherName);
    assert(missing && Contains(missing->detail, "not active"));
    assert(gone.Get<motion::HumanoidPose>(0) && gone.values[1].IsEmpty());
    assert(gone.errors.empty() && gone.Failed());

    stage->GetPrimAtPath(other).SetActive(true);
    const Frame back = driver.Evaluate(id, UsdTimeCode(50.0));
    assert(back.diagnostics.IsClean() && back.errors.empty());
    assert(back.Get<motion::HumanoidPose>(1)
           && *back.Get<motion::HumanoidPose>(1) == answered);

    stage->GetPrimAtPath(other).SetActive(false);
    stage->GetPrimAtPath(other).SetActive(true);
    const Frame resynced = driver.Evaluate(id, UsdTimeCode(50.0));
    const OpenExecDiagnostic* invalidated =
        Find(resynced, OpenExecDiagnosticCode::Invalidated, otherName);
    assert(invalidated && "a request exec expired between two frames was "
                          "not reported");
    // Seen through IsValid(), before anything computes -- not recovered
    // afterwards from the coding error an expired request posts, which is
    // the InvalidateAll route and says so in other words.
    assert(Contains(invalidated->detail, "reported the request invalid"));
    assert(invalidated->recoverable && !resynced.Failed());
    assert(resynced.errors.empty());
    assert(resynced.Get<motion::HumanoidPose>(1)
           && *resynced.Get<motion::HumanoidPose>(1) == answered &&
           "the rebuilt request did not answer what the old one did");

    // Why the second clip: a request whose EVERY key expires is discarded,
    // and discarding clears the bits IsValid() reads. Measured on the raw
    // system, so the day upstream reports it is a red test.
    ExecUsdSystem& system = driver.System();
    ExecUsdRequest lone = system.BuildRequest(
        {ExecUsdValueKey(stage->GetPrimAtPath(other), kSample)});
    system.Compute(lone);
    stage->GetPrimAtPath(other).SetActive(false);
    assert(lone.IsValid() &&
           "exec now reports a request whose every key expired; the driver "
           "can see that route before computing, as it does the other");
    stage->GetPrimAtPath(other).SetActive(true);

    // And through the driver that route still ends in a rebuilt request, found
    // after the fact, the way an InvalidateAll is.
    const Driver::RequestId single =
        driver.Add({Key::Of<motion::HumanoidPose>(other, kSample)});
    driver.Evaluate(single, UsdTimeCode(50.0));
    stage->GetPrimAtPath(other).SetActive(false);
    stage->GetPrimAtPath(other).SetActive(true);
    const Frame whole = driver.Evaluate(single, UsdTimeCode(50.0));
    const OpenExecDiagnostic* discarded =
        Find(whole, OpenExecDiagnosticCode::Invalidated, otherName);
    assert(discarded
           && Contains(discarded->detail, "still reported itself valid"));
    assert(!whole.Failed() && whole.Get<motion::HumanoidPose>(0)
           && *whole.Get<motion::HumanoidPose>(0) == answered);
}

// Contract: an override holds exactly its key's type, checked before exec
// sees it -- and here is why.
void AMistypedOverride()
{
    UsdStageRefPtr stage = Open();
    Driver driver(stage);
    const Driver::RequestId id =
        driver.Add({Key::Of<motion::HumanoidPose>(kClip, kFilter)});
    const Key prior = Key::Of<motion::HumanoidPose>(kClip, kPrior);

    const Frame frame = driver.Evaluate(
        id, UsdTimeCode(50.0), {Override{prior, VtValue(motion::HumanoidAnimation{})}});
    const OpenExecDiagnostic* mismatch =
        Find(frame, OpenExecDiagnosticCode::TypeMismatch, kPriorName);
    assert(mismatch && Contains(mismatch->detail, "HumanoidAnimation"));
    assert(frame.Failed() && frame.values[0].IsEmpty() && frame.errors.empty());

    const Frame empty =
        driver.Evaluate(id, UsdTimeCode(50.0), {Override{prior, VtValue()}});
    const OpenExecDiagnostic* absence =
        Find(empty, OpenExecDiagnosticCode::TypeMismatch, kPriorName);
    assert(absence && Contains(absence->detail, "empty value"));
    assert(empty.Failed() && empty.values[0].IsEmpty());

    // Why: handed the same override directly, exec answers anyway -- a coding
    // error, and the filter run against the key's ordinary value.
    ExecUsdSystem& system = driver.System();
    ExecUsdRequest raw = system.BuildRequest(
        {ExecUsdValueKey(stage->GetPrimAtPath(kClip), kFilter)});
    system.ChangeTime(UsdTimeCode(50.0));
    system.Compute(raw);
    TfErrorMark mark;
    const VtValue answered =
        system
            .ComputeWithOverrides(
                raw, {ExecUsdValueOverride{
                         ExecUsdValueKey(stage->GetPrimAtPath(kClip), kPrior),
                         VtValue(motion::HumanoidAnimation{})}})
            .Get(0);
    assert(answered.IsHolding<motion::HumanoidPose>() &&
           "exec now refuses a mistyped override; the driver's check is "
           "no longer the only thing between a caller and a plausible answer");
    assert(!mark.IsClean());
    mark.Clear();
}

// VRM_OPENEXEC_TYPE_MISMATCH, from exec: an override that holds the declared
// type, when the declaration is wrong. Of two overrides, only that one is
// named.
void AnOverrideExecRejects()
{
    Driver driver(Open());
    const Driver::RequestId id =
        driver.Add({Key::Of<motion::HumanoidPose>(kClip, kFilter),
                    Key::Of<motion::PoseSampleResult>(kClip, kInterpolate)});

    motion::HumanoidAnimation history;
    history.samples.emplace_back();
    history.samples.back().timestamp = 1.0;

    const Frame frame = driver.Evaluate(
        id, UsdTimeCode(50.0),
        {Override{Key::Of<motion::HumanoidAnimation>(kClip, kPrior),
                  VtValue(motion::HumanoidAnimation{})},
         Override{Key::Of<motion::HumanoidAnimation>(kClip, kHistory),
                  VtValue(history)}});
    const OpenExecDiagnostic* mismatch =
        Find(frame, OpenExecDiagnosticCode::TypeMismatch, kPriorName);
    assert(mismatch && "exec's own type check was not attributed");
    assert(Contains(mismatch->detail, "Expected override of value key"));
    assert(!Find(frame, OpenExecDiagnosticCode::TypeMismatch, kHistoryName) &&
           "the override exec accepted was named as well");
    assert(frame.diagnostics.reported.size() == 1);
    assert(frame.Failed() && frame.errors.empty());
    assert(frame.values[0].IsEmpty() && frame.values[1].IsEmpty() &&
           "a frame computed against an override exec dropped was answered");

    // The same frame again, now with nothing new to build: the same answer.
    const Frame again = driver.Evaluate(
        id, UsdTimeCode(50.0),
        {Override{Key::Of<motion::HumanoidAnimation>(kClip, kPrior),
                  VtValue(motion::HumanoidAnimation{})},
         Override{Key::Of<motion::HumanoidAnimation>(kClip, kHistory),
                  VtValue(history)}});
    assert(Find(again, OpenExecDiagnosticCode::TypeMismatch, kPriorName)
           && again.diagnostics.reported.size() == 1 && again.Failed());
}

// VRM_OPENEXEC_TYPE_MISMATCH, from an answer.
void AnAnswerOfAnotherType()
{
    Driver driver(Open());
    const Driver::RequestId id =
        driver.Add({Key::Of<motion::HumanoidAnimation>(kClip, kSample)});
    const Frame frame = driver.Evaluate(id, UsdTimeCode(50.0));
    const OpenExecDiagnostic* mismatch =
        Find(frame, OpenExecDiagnosticCode::TypeMismatch, kSampleName);
    assert(mismatch && Contains(mismatch->detail, "HumanoidPose"));
    assert(frame.values[0].IsEmpty() && frame.Failed());
}

// Contract: exec skips an override of a key nothing compiled, without a word,
// and the driver requests every key it overrides so exec has compiled it.
void AnOverrideNothingCompiled()
{
    UsdStageRefPtr stage = Open();
    {
        // Measured on a system that has compiled the sampler and nothing that
        // reads motion.priorPose: a MISTYPED override of it passes in silence,
        // because the type check is reached only for a compiled output.
        ExecUsdSystem system(stage);
        ExecUsdRequest raw = system.BuildRequest(
            {ExecUsdValueKey(stage->GetPrimAtPath(kClip), kSample)});
        system.ChangeTime(UsdTimeCode(50.0));
        system.Compute(raw);
        TfErrorMark mark;
        system.ComputeWithOverrides(
            raw, {ExecUsdValueOverride{
                     ExecUsdValueKey(stage->GetPrimAtPath(kClip), kPrior),
                     VtValue(motion::HumanoidAnimation{})}});
        assert(mark.IsClean() &&
               "exec now reports an override of a key it has not compiled; "
               "the driver no longer has to request every key it overrides");
    }

    // Through the driver the key joins the request, so the same mistaken
    // declaration meets exec's check and is named.
    Driver driver(stage);
    const Driver::RequestId id =
        driver.Add({Key::Of<motion::HumanoidPose>(kClip, kSample)});
    const Frame frame = driver.Evaluate(
        id, UsdTimeCode(50.0),
        {Override{Key::Of<motion::HumanoidAnimation>(kClip, kPrior),
                  VtValue(motion::HumanoidAnimation{})}});
    assert(Find(frame, OpenExecDiagnosticCode::TypeMismatch, kPriorName) &&
           "an override of a key the request does not read went unchecked");
    assert(frame.Failed());
}

// VRM_OPENEXEC_INVALIDATED: an InvalidateAll behind the driver's back.
void ARequestExecStoppedAnswering()
{
    Driver driver(Open());
    const Driver::RequestId id =
        driver.Add({Key::Of<motion::HumanoidPose>(kClip, kSample),
                    Key::Of<motion::HumanoidPose>(kClip, kFilter)});
    const Frame before = driver.Evaluate(id, UsdTimeCode(50.0));
    assert(before.Get<motion::HumanoidPose>(0) && before.Get<motion::HumanoidPose>(1));

    {
        ExecSystem::Diagnostics diagnostics(&driver.System());
        diagnostics.InvalidateAll();
    }
    const Frame after = driver.Evaluate(id, UsdTimeCode(50.0));
    for (const std::string& name : {kSampleName, kFilterName}) {
        const OpenExecDiagnostic* invalidated =
            Find(after, OpenExecDiagnosticCode::Invalidated, name);
        assert(invalidated && "an expired request was not reported");
        assert(Contains(invalidated->detail, "still reported itself valid"));
    }
    assert(!after.Failed() && after.errors.empty());
    // The time was restated: InvalidateAll put the system at the default time
    // code, where the sampler answers an empty pose.
    assert(*after.Get<motion::HumanoidPose>(0) == *before.Get<motion::HumanoidPose>(0)
           && *after.Get<motion::HumanoidPose>(1)
                  == *before.Get<motion::HumanoidPose>(1));

    const Frame settled = driver.Evaluate(id, UsdTimeCode(50.0));
    assert(settled.diagnostics.IsClean() && settled.errors.empty());
    assert(driver.Reported().Has(OpenExecDiagnosticCode::Invalidated, kSampleName));
}

// Contract: one previous answer per prim, substituted through one call. The
// positive control for every override above.
void AnOverrideReachesItsDependent()
{
    Driver driver(Open());
    const Driver::RequestId id =
        driver.Add({Key::Of<motion::HumanoidPose>(kClip, kSample),
                    Key::Of<motion::HumanoidPose>(kClip, kFilter)});
    const Frame earlier = driver.Evaluate(id, UsdTimeCode(49.0));
    const motion::HumanoidPose previous = *earlier.Get<motion::HumanoidPose>(0);

    const Frame plain = driver.Evaluate(id, UsdTimeCode(50.0));
    assert(*plain.Get<motion::HumanoidPose>(1) == *plain.Get<motion::HumanoidPose>(0) &&
           "un-overridden, the filter is the sampler");

    const Frame stepped = driver.Evaluate(
        id, UsdTimeCode(50.0),
        {Override{Key::Of<motion::HumanoidPose>(kClip, kPrior), VtValue(previous)}});
    assert(!stepped.Failed() && stepped.diagnostics.IsClean());
    assert(*stepped.Get<motion::HumanoidPose>(1) != *stepped.Get<motion::HumanoidPose>(0) &&
           "the previous answer did not reach the filter");
    assert(*stepped.Get<motion::HumanoidPose>(0) == *plain.Get<motion::HumanoidPose>(0) &&
           "the override leaked into the sampler");
}

} // namespace

int main(int argc, char** argv)
{
    assert(argc == 2 && "usage: exec_driver_contract <filtered_clip.usda>");
    gFixture = argv[1];

    TheTable();
    ArmingKeepsItsRefusals();
    AComputationNobodyRegisters();
    AProviderThatIsNotThere();
    AProviderThatGoesAndComesBack();
    AMistypedOverride();
    AnOverrideExecRejects();
    AnAnswerOfAnotherType();
    AnOverrideNothingCompiled();
    ARequestExecStoppedAnswering();
    AnOverrideReachesItsDependent();

    std::puts("exec_driver_contract: ok");
    return 0;
}
