// SPDX-License-Identifier: Apache-2.0
//
// `motion.extractRootMotion` through the built bundle: the first computation
// that produces something other than a pose, and the first whose policy has
// three settings rather than a number.
//
// What it answers is where the body is, under the intake policy the clip
// states -- `motion::RootMotionIntake`, the library's own enum, spelled as a
// token. Three things are measured here that no earlier suite could:
//
//   * a **second registered value type** in one bundle: one request returns a
//     `motion::HumanoidPose` and a `motion::RootMotion` side by side, and the
//     dependent value key is a different type from the input it reads;
//   * a **derived velocity** -- a number that exists nowhere in the clip, is
//     the difference between two instants, and reaches the graph through the
//     same `motion.priorPose` override the filter takes; and
//   * **absent and unrecognized are different answers**: a clip stating no
//     policy gets `motion::LiveCaptureConfig`'s own default, and a clip stating
//     a token that names no policy is refused. The rate is refused when absent,
//     the filter's cutoff is defaulted when absent, and this attribute is the
//     one that does both -- which is what makes the rule "what an absent value
//     costs" rather than "what the node feels like".
//
// And a refusal here carries **no value at all**, which this node is the reason
// for. A cleared `motion::RootMotion` is `ignore`'s own legitimate answer, bit
// for bit, so a refusal that produced one would hand a misspelled `passthrough`
// the behaviour of a deliberate `ignore` -- indistinguishable to anyone not
// reading `TfError`s. The `ignore` block and the unrecognized-token block sit
// next to each other below, and the pair is the assertion.
//
// Like every suite here but `execMotion_pose`, this executable does not link
// the plugin: the computations are reached only through `PXR_PLUGINPATH_NAME`
// and the staged plugInfo.json.

#include "pxr/pxr.h"

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
#include "pxr/usd/usd/attribute.h"
#include "pxr/usd/usd/prim.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usd/timeCode.h"

#include <motionCore/Humanoid.h>

#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

namespace {

const TfToken kExtractRootMotion("motion.extractRootMotion");
const TfToken kSampleAnimation("motion.sampleAnimation");
const TfToken kPriorPose("motion.priorPose");

const TfToken kRootIntake("motion:root:intake");

// The three value keys every request below asks for, in this order.
constexpr int kRoot = 0;
constexpr int kSampled = 1;
constexpr int kPrior = 2;

// The frame the whole suite reads at, and the second it is -- the same pair the
// filter suite uses, for the same reason: frame 50 is halfway between the
// fixture's two keys, so the hips are strictly between the ends and a root that
// came from the wrong frame cannot land on the right number by coincidence.
constexpr double kFrame = 50.0;
constexpr double kSecond = 1.0;

// The seconds between the prior pose and the sampled one: one frame at the
// fixture's 50 time codes per second.
constexpr double kStep = 0.02;

// Where the clip puts the hips at frame 50, halfway from (0, 0, 0) to (0, 1, 2).
const GfVec3f kHipsAtFrame50(0.0f, 0.5f, 1.0f);

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

motion::RootMotion RootAt(const ExecUsdCacheView& view, int index)
{
    const VtValue value = view.Get(index);
    assert(!value.IsEmpty() &&
           "no value came back -- if the plugInfo is unstaged this is what it "
           "looks like, not a load error");
    // The bundle registers two types and this is the second one. A request that
    // returned a pose here would mean the computation's declared result type
    // and its callback had drifted apart, which is not a thing the compiler
    // catches on the far side of a VtValue.
    assert(value.IsHolding<motion::RootMotion>() &&
           "motion.extractRootMotion did not return a motion::RootMotion");
    return value.UncheckedGet<motion::RootMotion>();
}

// A refusal, which in this bundle is **no value at all**.
//
// The distinction this asserts is the whole of the review finding that produced
// it: a cleared `motion::RootMotion` is `ignore`'s own legitimate answer, bit
// for bit, so a refusal that produced one would be indistinguishable from a
// deliberate "this clip does not place the body" for anyone not reading
// `TfError`s. An empty `VtValue` is the one shape no computation here ever
// produces as an answer.
void AssertRefused(const ExecUsdCacheView& view, int index)
{
    const VtValue value = view.Get(index);
    assert(value.IsEmpty() &&
           "a refusal came back carrying a value, which puts it back where a "
           "consumer cannot tell it from an answer");
    assert(!value.IsHolding<motion::RootMotion>());
}

motion::HumanoidPose PoseAt(const ExecUsdCacheView& view, int index)
{
    const VtValue value = view.Get(index);
    assert(!value.IsEmpty());
    assert(value.IsHolding<motion::HumanoidPose>() &&
           "the canonical aggregate did not survive the boundary");
    return value.UncheckedGet<motion::HumanoidPose>();
}

// The pose a driver would hand back as "the previous frame's answer": the four
// bones the fixtures name, at identity, with the hips at the origin. So the
// sampled pose at frame 50 is a known distance away from it, and the velocity
// between them is a number this suite can write out.
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

ExecUsdValueOverrideVector PriorOverride(const UsdPrim& clip,
                                         const motion::HumanoidPose& pose)
{
    ExecUsdValueOverrideVector overrides;
    overrides.push_back(
        ExecUsdValueOverride{ExecUsdValueKey(clip, kPriorPose),
                             VtValue(pose)});
    return overrides;
}

// The velocity the derivation owes, written out rather than called.
//
// It is a definition -- distance over the seconds between two instants -- and
// the suite states it here for the same reason the filter suite writes out
// `PoseFilter`'s step weight: an expected value produced by the code under test
// asserts only that it equals itself.
GfVec3f VelocityBetween(const GfVec3f& from, const GfVec3f& to, double seconds)
{
    return (to - from) / static_cast<float>(seconds);
}

std::vector<ExecUsdValueKey> KeysFor(const UsdPrim& clip)
{
    std::vector<ExecUsdValueKey> keys;
    keys.emplace_back(clip, kExtractRootMotion);
    keys.emplace_back(clip, kSampleAnimation);
    keys.emplace_back(clip, kPriorPose);
    return keys;
}

// ---------------------------------------------------------------------------
// The clip that states a policy
// ---------------------------------------------------------------------------
void TestAClipWithAPolicy(const std::string& fixture)
{
    UsdStageRefPtr stage = UsdStage::Open(fixture);
    assert(stage && "the root policy fixture did not open");

    UsdPrim clip = stage->GetPrimAtPath(SdfPath("/Clip"));
    assert(clip && "the fixture has no /Clip prim");

    // The premise of every assertion below. `passthrough` is the neighbouring
    // policy to the library's default and differs from it in exactly one field,
    // so a fixture that had drifted onto the default would make this suite
    // agree with the no-policy one for the wrong reason.
    TfToken authoredIntake;
    assert(clip.GetAttribute(kRootIntake).Get(&authoredIntake));
    assert(authoredIntake == TfToken("passthrough") &&
           "the root policy fixture does not state passthrough");

    ExecUsdSystem system(stage);

    int timeInvalidations = 0;
    bool rootReported = false;
    ExecUsdRequest request = system.BuildRequest(
        KeysFor(clip),
        [](const ExecRequestIndexSet&, const EfTimeInterval&) {},
        [&](const ExecRequestIndexSet& indices) {
            ++timeInvalidations;
            rootReported = rootReported || indices.count(kRoot) > 0;
        });
    assert(request.IsValid() &&
           "the request did not compile -- a computation whose result type is "
           "not the type of the computation it reads is the new shape here");

    // A request is armed by its first `Compute` (the filtering report §4), so
    // the frame moves after one. That first compute is at the default time
    // code, which resolves a clip authoring only time samples to nothing -- so
    // the pose is empty, and an empty pose has no root.
    {
        ExecUsdCacheView view = system.Compute(request);
        // A cleared root, and an **answer** rather than a refusal: `RootAt`
        // requires the value to be present. This is the third way a cleared
        // `motion::RootMotion` legitimately arises -- beside `ignore` and a
        // pose that states no position -- which is why a refusal cannot be
        // spelled that way.
        assert(RootAt(view, kRoot) == motion::RootMotion{} &&
               "a pose with no root produced a root anyway");
    }

    system.ChangeTime(UsdTimeCode(kFrame));

    // ---- time reaches this node through its input, across a type change ----
    // Like `motion.filterPose`, this node declares no `computeTime` and is
    // still reported when the frame moves, because the pose it reads is time
    // dependent. What is new is that the dependent value is a *different type*
    // from the one it depends on: time dependence propagates along the link and
    // not along the type.
    assert(timeInvalidations > 0 &&
           "moving the frame reached no value key at all");
    assert(rootReported &&
           "motion.extractRootMotion was NOT reported when the frame moved, "
           "although the pose it consumes is time dependent");

    // ---- un-overridden, there is no velocity to derive ---------------------
    // `motion.priorPose` forwards the clip's own pose, so the two instants are
    // the same instant: the elapsed time is zero and nothing is differentiated.
    // The same shape the filter has un-overridden, and special-cased nowhere.
    {
        ExecUsdCacheView view = system.Compute(request);
        const motion::HumanoidPose sampled = PoseAt(view, kSampled);
        const motion::RootMotion root = RootAt(view, kRoot);

        // Both registered types, out of one request, side by side.
        assert(NearlyEqual(sampled.timestamp, kSecond, 1e-12));
        assert(sampled.root.hasPosition &&
               "the fixture is not the sampled clip after all");

        assert(root.hasPosition);
        assert(NearlyEqual(root.worldPosition, kHipsAtFrame50, 1e-6) &&
               "the root did not come from the hips at the evaluated frame");
        assert(!root.hasLinearVelocity &&
               "a velocity was derived between a pose and itself");
        assert(root == sampled.root &&
               "passthrough changed the root the pose stated");
    }

    // ---- overridden, passthrough still derives nothing ---------------------
    // The negative half of the pair, and the one that says the *clip's* token
    // reached `motion::RootMotionIntake`: the same two poses under the
    // library's default produce a velocity, asserted in the no-policy suite
    // below. A bundle that ignored the attribute would fail one of the two
    // whichever way it defaulted.
    const motion::HumanoidPose prior = PriorPose(kSecond - kStep);
    {
        ExecUsdCacheView view = system.ComputeWithOverrides(
            request, PriorOverride(clip, prior));

        assert(PoseAt(view, kPrior) == prior &&
               "the override did not reach motion.priorPose");

        const motion::RootMotion root = RootAt(view, kRoot);
        assert(root.hasPosition);
        assert(NearlyEqual(root.worldPosition, kHipsAtFrame50, 1e-6));
        assert(!root.hasLinearVelocity &&
               "passthrough derived a velocity, so the clip's policy never "
               "reached the library's vocabulary");
    }

    // ---- deriveVelocity, authored onto the stage ---------------------------
    // The number below exists nowhere in the clip: it is the distance between
    // two instants over the time between them, and it is the first value this
    // bundle produces that no attribute states. Authored here rather than in a
    // third fixture because the point is as much that the change arrives at a
    // node two links downstream of the one that reads the clip.
    assert(clip.GetAttribute(kRootIntake).Set(TfToken("deriveVelocity")));
    {
        ExecUsdCacheView view = system.ComputeWithOverrides(
            request, PriorOverride(clip, prior));
        const motion::RootMotion root = RootAt(view, kRoot);

        assert(root.hasLinearVelocity &&
               "deriveVelocity derived nothing, or the authored token never "
               "reached the computation");
        assert(NearlyEqual(root.linearVelocity,
                           VelocityBetween(prior.root.worldPosition,
                                           kHipsAtFrame50, kStep),
                           1e-3) &&
               "the derived velocity is not the distance between the two "
               "instants over the time between them");

        // And the position is untouched: the derivation adds a field, it does
        // not move the one it differentiated.
        assert(root.hasPosition);
        assert(NearlyEqual(root.worldPosition, kHipsAtFrame50, 1e-6) &&
               "deriving a velocity moved the position it was derived from");
    }

    // ---- ignore, which is the policy that answers with nothing -------------
    // A cleared `motion::RootMotion` rather than a zeroed position: every
    // presence flag is false, which is what "this clip's placement is not the
    // capture's to decide" looks like downstream. A zero position with
    // `hasPosition` set would put the avatar at the origin instead.
    assert(clip.GetAttribute(kRootIntake).Set(TfToken("ignore")));
    {
        ExecUsdCacheView view = system.ComputeWithOverrides(
            request, PriorOverride(clip, prior));

        // `RootAt` asserts the value is present and holds a `RootMotion`, so
        // this is also the positive half of the refusal shape: `ignore` is an
        // **answer**, and it stays one. The block below states the same clip's
        // refusal, and the pair is what makes the two distinguishable rather
        // than merely differently commented.
        const motion::RootMotion root = RootAt(view, kRoot);
        assert(root == motion::RootMotion{} &&
               "ignore left something of the root behind");
        assert(!root.hasPosition &&
               "ignore zeroed the position instead of clearing it");

        // The pose it read is unchanged, so `ignore` is this node's answer and
        // not a clip that stopped stating a root.
        assert(PoseAt(view, kSampled).root.hasPosition &&
               "the intake policy reached motion.sampleAnimation as well");
    }

    // ---- a token that names no policy is refused ---------------------------
    // The other half of "what an absent value costs". An absent attribute is a
    // clip that said nothing and gets the library's default; this is a clip
    // that stated something, and defaulting here would hand a misspelled
    // `ignore` the root motion it asked not to have.
    //
    // And the refusal carries **no value**, which is the mirror of that same
    // argument and the reason this suite has an `AssertRefused` at all: a
    // cleared `motion::RootMotion` is what the block immediately above returns
    // for a deliberate `ignore`, bit for bit, so a refusal producing one would
    // hand a misspelled `passthrough` the behaviour of an `ignore` nobody
    // asked for. The two blocks are next to each other on purpose.
    assert(clip.GetAttribute(kRootIntake).Set(TfToken("smooth")));
    {
        TfErrorMark mark;
        ExecUsdCacheView view = system.ComputeWithOverrides(
            request, PriorOverride(clip, prior));
        AssertRefused(view, kRoot);

        // The nodes it did not refuse are unaffected: a refusal is this value
        // key's, not the request's.
        assert(PoseAt(view, kSampled).root.hasPosition &&
               "a refused intake policy took motion.sampleAnimation with it");

        assert(!mark.IsClean() &&
               "an unrecognized policy was refused silently, which is worse "
               "than being refused");
        bool named = false;
        for (TfErrorMark::Iterator it = mark.GetBegin(); it != mark.GetEnd();
             ++it) {
            if (it->GetCommentary().find("motion.extractRootMotion")
                != std::string::npos) {
                named = true;
            }
        }
        assert(named &&
               "the errors posted came from somewhere other than the "
               "computation");
        mark.Clear();
    }
    assert(clip.GetAttribute(kRootIntake).Set(TfToken("passthrough")));

    std::printf("execMotion root: a clip's intake policy reaches "
                "motion::RootMotionIntake, and a token naming none is "
                "refused\n");
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

    // The premise: this clip says nothing about its root. The attribute is not
    // `.Required()`, because the library it wraps has an answer for it.
    assert(!clip.GetAttribute(kRootIntake));

    ExecUsdSystem system(stage);
    ExecUsdRequest request = system.BuildRequest(KeysFor(clip));
    assert(request.IsValid());

    system.ChangeTime(UsdTimeCode(kFrame));

    const motion::HumanoidPose prior = PriorPose(kSecond - kStep);
    {
        ExecUsdCacheView view = system.ComputeWithOverrides(
            request, PriorOverride(clip, prior));
        const motion::RootMotion root = RootAt(view, kRoot);

        // `motion::LiveCaptureConfig`'s own default is `DeriveVelocity`, and
        // that is what a clip stating nothing gets. The assertion is the same
        // shape as the filter suite's 6 Hz: the *other* two policies are
        // distinguishable here, so this would not pass if the bundle had picked
        // a default of its own -- `Passthrough` derives nothing and `Ignore`
        // clears the position.
        assert(root.hasLinearVelocity &&
               "a clip stating no policy was not taken in with "
               "LiveCaptureConfig's own DeriveVelocity default");
        assert(NearlyEqual(root.linearVelocity,
                           VelocityBetween(prior.root.worldPosition,
                                           kHipsAtFrame50, kStep),
                           1e-3));
        assert(root.hasPosition);
        assert(NearlyEqual(root.worldPosition, kHipsAtFrame50, 1e-6));
    }

    // Un-overridden the default derives nothing either, so the velocity above
    // is the override's doing and not the clip's.
    {
        ExecUsdCacheView view = system.Compute(request);
        const motion::RootMotion root = RootAt(view, kRoot);
        assert(!root.hasLinearVelocity &&
               "a velocity survived the ComputeWithOverrides that supplied the "
               "prior pose it was derived from");
        assert(NearlyEqual(root.worldPosition, kHipsAtFrame50, 1e-6));
    }

    std::printf("execMotion root: a clip stating no policy is taken in with "
                "motion::LiveCaptureConfig's own default\n");
}

// ---------------------------------------------------------------------------
// One override, two recurrences
// ---------------------------------------------------------------------------
// `motion.filterPose` and `motion.extractRootMotion` both want the previous
// frame's answer, and both reach it through `motion.priorPose`. This is the
// measurement that says a driver does not have to know which nodes want it: one
// substituted value per frame, one `ComputeWithOverrides`, and every dependent
// of that key steps.
//
// It matters because the alternative is a per-node prior, and a driver holding
// two of them would have to keep them in step by hand -- which is the state
// exec's purity rule pushed out of the graph in the first place.
void TestOneOverrideDrivesBothRecurrences(const std::string& fixture)
{
    UsdStageRefPtr stage = UsdStage::Open(fixture);
    assert(stage && "the sampled fixture did not open");

    UsdPrim clip = stage->GetPrimAtPath(SdfPath("/Clip"));
    assert(clip && "the fixture has no /Clip prim");

    ExecUsdSystem system(stage);
    std::vector<ExecUsdValueKey> keys;
    keys.emplace_back(clip, kExtractRootMotion);
    keys.emplace_back(clip, TfToken("motion.filterPose"));
    ExecUsdRequest request = system.BuildRequest(std::move(keys));
    assert(request.IsValid() &&
           "two computations reading the same third one did not compile");

    system.ChangeTime(UsdTimeCode(kFrame));

    const motion::HumanoidPose prior = PriorPose(kSecond - kStep);
    ExecUsdCacheView view = system.ComputeWithOverrides(
        request, PriorOverride(clip, prior));

    // The root derived its velocity from the substituted pose...
    const motion::RootMotion root = RootAt(view, 0);
    assert(root.hasLinearVelocity &&
           "the override did not reach motion.extractRootMotion");
    assert(NearlyEqual(root.linearVelocity,
                       VelocityBetween(prior.root.worldPosition,
                                       kHipsAtFrame50, kStep),
                       1e-3));

    // ...and the filter took its step from the same one, in the same call. The
    // filtered hips are strictly between the prior's origin and the clip's
    // position, which is what a step looks like and neither endpoint does.
    const motion::HumanoidPose filtered = PoseAt(view, 1);
    assert(filtered.root.hasPosition);
    assert(filtered.root.worldPosition[2] > 0.0f &&
           filtered.root.worldPosition[2] < kHipsAtFrame50[2] &&
           "the override did not reach motion.filterPose in the same call");

    std::printf("execMotion root: one motion.priorPose override drives both "
                "the filter step and the derived velocity\n");
}

} // namespace

int main(int argc, char** argv)
{
    assert(argc == 3 &&
           "usage: execMotion_root <rooted_clip.usda> <sampled_clip.usda>");
    TestAClipWithAPolicy(argv[1]);
    TestAClipWithNoPolicy(argv[2]);
    TestOneOverrideDrivesBothRecurrences(argv[2]);
    std::printf("execMotion root: all checks passed\n");
    return 0;
}
