// SPDX-License-Identifier: Apache-2.0
//
// `motion.blendPoses` through the built bundle.
//
// The one node in the bundle that wants poses from **several places**, so the
// one where 26.08's fan-in lands. The poses arrive through a relationship --
// `motion:blend:sources` targets the clips and each target's
// `motion.sampleAnimation` is one value of the fan-in -- and four things are
// measured here that no earlier suite could reach:
//
//   * the fan-in arrives in the relationship's **authored target order**, at
//     first compile and again after the targets are edited, which is what
//     pairing a weight with its source by position needs;
//   * a target that provides no such computation, and a source that
//     **refused**, both vanish from the fan-in without a word -- so the node
//     counts the targets a second time, through the builtin `computePath`, and
//     refuses a count that disagrees;
//   * time dependence and authored-value invalidation reach the blend across
//     the relationship, and so does an edit of the relationship itself; and
//   * an override of a **source's** key reaches the blend on another prim --
//     the one route by which a pose a driver holds, rather than one a clip
//     states, enters a blend.
//
// Like the filter and root suites it does not link motionRuntime. The node is
// `motion::BlendPoses`, and an expected value produced by that function would
// assert that the library equals itself; the fixture's two clips turn one head
// about one axis, so the blend of two of them is an angle interpolated along
// that axis, and every number below is written from that definition.

#include "pxr/pxr.h"

#include "pxr/base/gf/quatf.h"
#include "pxr/base/gf/vec3f.h"
#include "pxr/base/tf/errorMark.h"
#include "pxr/base/tf/token.h"
#include "pxr/base/vt/array.h"
#include "pxr/base/vt/value.h"

#include "pxr/exec/exec/request.h"
#include "pxr/exec/execUsd/cacheView.h"
#include "pxr/exec/execUsd/request.h"
#include "pxr/exec/execUsd/system.h"
#include "pxr/exec/execUsd/valueKey.h"
#include "pxr/exec/execUsd/valueOverride.h"

#include "pxr/usd/sdf/path.h"
#include "pxr/usd/sdf/types.h"
#include "pxr/usd/usd/attribute.h"
#include "pxr/usd/usd/prim.h"
#include "pxr/usd/usd/relationship.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usd/timeCode.h"

#include <motionCore/Humanoid.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <utility>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

namespace {

const TfToken kBlendPoses("motion.blendPoses");
const TfToken kSampleAnimation("motion.sampleAnimation");
const TfToken kSources("motion:blend:sources");
const TfToken kWeights("motion:blend:weights");
const TfToken kRate("motion:timeCodesPerSecond");

// The value keys every request here asks for, in this order.
constexpr int kBlended = 0;
constexpr int kWalkSampled = 1;
constexpr int kTurnSampled = 2;

// Frame 50, second 1 at 50 time codes per second: /Walk's head at 45 degrees
// about +Y and its hips at (0, 0.5, 1); /Turn holding its head at 90 degrees
// about the same axis and its hips at (2, 0, 0).
constexpr double kFrame = 50.0;
constexpr double kSecond = 1.0;
constexpr float kWalkHead = 45.0f;
constexpr float kTurnHead = 90.0f;
const GfVec3f kWalkHips(0.0f, 0.5f, 1.0f);
const GfVec3f kTurnHips(2.0f, 0.0f, 0.0f);

constexpr std::size_t kHead = static_cast<std::size_t>(motion::HumanBone::Head);
constexpr std::size_t kSpine =
    static_cast<std::size_t>(motion::HumanBone::Spine);
constexpr std::size_t kChest =
    static_cast<std::size_t>(motion::HumanBone::Chest);

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

// The head's turn about +Y, in degrees -- signed, so a blend that went the long
// way round would not land on the right number by symmetry.
float HeadDegrees(const motion::HumanoidPose& pose)
{
    const GfQuatf head = pose.localRotations[kHead].GetNormalized();
    const float sign = head.GetImaginary()[1] < 0.0f ? -1.0f : 1.0f;
    const double w = std::min(1.0, std::max(-1.0, double(std::abs(head.GetReal()))));
    return sign * float(2.0 * std::acos(w) * 180.0 / M_PI);
}

// What the library's fold makes of two poses: the second folded into the first
// at its share of the total. Written out from the definition for the one shape
// this fixture has -- one head turning about one axis -- where a slerp is an
// angle interpolated linearly and a lerp is a lerp.
float BlendedHead(float first, float second, float w1, float w2)
{
    return first + (second - first) * (w2 / (w1 + w2));
}

GfVec3f BlendedHips(const GfVec3f& first, const GfVec3f& second,
                    float w1, float w2)
{
    return first + (second - first) * (w2 / (w1 + w2));
}

motion::HumanoidPose PoseAt(const ExecUsdCacheView& view, int index)
{
    const VtValue value = view.Get(index);
    assert(!value.IsEmpty() &&
           "no value came back -- if the plugInfo is unstaged this is what it "
           "looks like, not a load error");
    assert(value.IsHolding<motion::HumanoidPose>() &&
           "motion.blendPoses did not return a motion::HumanoidPose");
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

struct Stage
{
    UsdStageRefPtr stage;
    UsdPrim walk;
    UsdPrim turn;
    UsdPrim blend;
};

Stage Open(const std::string& fixture)
{
    Stage s;
    s.stage = UsdStage::Open(fixture);
    assert(s.stage && "the blend fixture did not open");
    s.walk = s.stage->GetPrimAtPath(SdfPath("/Walk"));
    s.turn = s.stage->GetPrimAtPath(SdfPath("/Turn"));
    s.blend = s.stage->GetPrimAtPath(SdfPath("/Blend"));
    assert(s.walk && s.turn && s.blend &&
           "the blend fixture is missing /Walk, /Turn or /Blend");
    return s;
}

std::vector<ExecUsdValueKey> KeysFor(const Stage& s)
{
    std::vector<ExecUsdValueKey> keys;
    keys.emplace_back(s.blend, kBlendPoses);
    keys.emplace_back(s.walk, kSampleAnimation);
    keys.emplace_back(s.turn, kSampleAnimation);
    return keys;
}

void SetWeights(const Stage& s, std::vector<float> weights)
{
    VtFloatArray array(weights.begin(), weights.end());
    assert(s.blend.GetAttribute(kWeights).Set(array));
}

void SetSources(const Stage& s, const SdfPathVector& targets)
{
    assert(s.blend.GetRelationship(kSources).SetTargets(targets));
}

// A request built, armed at the default time code and moved to frame 50 -- the
// shape every test below starts from. A request is armed by its first
// `Compute` (the filtering report §4), so the frame moves after one.
ExecUsdRequest ArmedAtFrame(ExecUsdSystem& system, const Stage& s)
{
    ExecUsdRequest request = system.BuildRequest(KeysFor(s));
    assert(request.IsValid());
    {
        TfErrorMark mark;
        system.Compute(request);
        mark.Clear();
    }
    system.ChangeTime(UsdTimeCode(kFrame));
    return request;
}

// ---------------------------------------------------------------------------
// The blend the fixture states
// ---------------------------------------------------------------------------
void TestTheBlendIsTheLibrarysWeightedFold(const std::string& fixture)
{
    const Stage s = Open(fixture);
    ExecUsdSystem system(s.stage);

    bool blendReportedByTime = false;
    ExecUsdRequest request = system.BuildRequest(
        KeysFor(s),
        [](const ExecRequestIndexSet&, const EfTimeInterval&) {},
        [&](const ExecRequestIndexSet& indices) {
            blendReportedByTime =
                blendReportedByTime || indices.count(kBlended) > 0;
        });
    assert(request.IsValid() &&
           "a request over a relationship fan-in did not compile");

    // ---- armed at the default time code, where the blend answers ----------
    // Unlike `motion.interpolatePose`, which samples a timeline and so refuses
    // where there is no instant, a blend combines what its sources answered,
    // and they answer there: /Walk, keyed only, resolves to no bones at all,
    // /Turn to its default values, and both are stamped 0.0. So the blend is
    // /Turn's bones -- the ones only one source reports are taken from it --
    // stamped 0.0, and the compute that arms the request is not a refusal.
    {
        TfErrorMark mark;
        ExecUsdCacheView view = system.Compute(request);
        const motion::HumanoidPose blended = PoseAt(view, kBlended);
        assert(mark.IsClean() &&
               "the arming compute posted an error, so a driver's first frame "
               "through a blend is a refusal");
        assert(blended.timestamp == 0.0);
        assert(!PoseAt(view, kWalkSampled).validRotations.any());
        assert(blended == PoseAt(view, kTurnSampled) &&
               "a source reporting no bones changed the blend");
    }

    system.ChangeTime(UsdTimeCode(kFrame));

    // Declares no `computeTime` and reads no keyed attribute of its own -- its
    // weights are not keyed in this fixture -- and is reported: time dependence
    // reaches it across the relationship, from /Walk.
    assert(blendReportedByTime &&
           "motion.blendPoses was NOT reported when the frame moved, although "
           "one of the clips it blends is keyed");

    ExecUsdCacheView view = system.Compute(request);
    const motion::HumanoidPose walk = PoseAt(view, kWalkSampled);
    const motion::HumanoidPose turn = PoseAt(view, kTurnSampled);
    assert(std::abs(HeadDegrees(walk) - kWalkHead) < 1e-3f);
    assert(std::abs(HeadDegrees(turn) - kTurnHead) < 1e-3f);

    const motion::HumanoidPose blended = PoseAt(view, kBlended);

    // /Turn folded into /Walk at 0.75 / (0.25 + 0.75): 78.75 degrees, and the
    // hips three quarters of the way to (2, 0, 0). A blend that paired the
    // weights the other way round lands on 56.25 degrees and misses both.
    assert(std::abs(HeadDegrees(blended)
                    - BlendedHead(kWalkHead, kTurnHead, 0.25f, 0.75f)) < 1e-3f &&
           "the head is not the weighted fold of the two clips");
    assert(blended.root.hasPosition);
    assert(NearlyEqual(blended.root.worldPosition,
                       BlendedHips(kWalkHips, kTurnHips, 0.25f, 0.75f), 1e-5) &&
           "the hips are not the weighted fold of the two clips");

    // Stamped at the instant both clips were sampled at, exactly.
    assert(blended.timestamp == kSecond);

    // A bone only /Walk reports is taken from /Walk rather than blended toward
    // identity -- the library's rule, arriving through the wrapper unchanged.
    assert(blended.validRotations.test(kSpine) &&
           blended.validRotations.test(kChest) &&
           "a bone only one source reports was dropped from the blend");
    assert(blended.localRotations[kSpine] == walk.localRotations[kSpine]);

    std::printf("execMotion blend: the fixture's blend is the weighted fold of "
                "its two clips, stamped at their instant, and time reaches it "
                "across the relationship\n");
}

// ---------------------------------------------------------------------------
// The order the fan-in arrives in
// ---------------------------------------------------------------------------
// A weight is paired with its source by position, so the fan-in's order is the
// whole of the pairing. Weights of 1 and 0 make the measurement exact: the
// blend is then the first source's pose, bit for bit, and which clip came first
// is the only thing that decides it.
void TestTheFanInArrivesInAuthoredOrder(const std::string& fixture)
{
    const Stage s = Open(fixture);
    SetWeights(s, {1.0f, 0.0f});

    ExecUsdSystem system(s.stage);
    bool blendReportedByValue = false;
    ExecUsdRequest request = system.BuildRequest(
        KeysFor(s),
        [&](const ExecRequestIndexSet& indices, const EfTimeInterval&) {
            blendReportedByValue =
                blendReportedByValue || indices.count(kBlended) > 0;
        });
    assert(request.IsValid());
    system.Compute(request);
    system.ChangeTime(UsdTimeCode(kFrame));

    // ---- as authored: [</Walk>, </Turn>] -------------------------------------
    {
        ExecUsdCacheView view = system.Compute(request);
        assert(PoseAt(view, kBlended) == PoseAt(view, kWalkSampled) &&
               "the first weight did not land on the first target");
    }

    // ---- the targets edited to [</Turn>, </Walk>] ----------------------------
    // Both source nodes already exist in the compiled network, so this is the
    // case where an order taken from *when a node was compiled* rather than
    // from the relationship would show: /Walk's node is older, and would come
    // first.
    //
    // The edit is also an invalidation, and it is reported like an authored
    // value: a relationship's targets are something exec journals while it
    // compiles the fan-in, so changing them reaches the value callback of the
    // node that reads through them, with no request rebuilt.
    blendReportedByValue = false;
    SetSources(s, {SdfPath("/Turn"), SdfPath("/Walk")});
    assert(blendReportedByValue &&
           "editing the relationship's targets did not reach the blend's value "
           "callback");
    {
        ExecUsdCacheView view = system.Compute(request);
        assert(PoseAt(view, kBlended) == PoseAt(view, kTurnSampled) &&
               "after the targets were reordered, the first weight did not "
               "land on the new first target -- the fan-in's order is not the "
               "relationship's");
    }

    // ---- and a system that never saw the old order --------------------------
    {
        ExecUsdSystem fresh(s.stage);
        ExecUsdRequest freshRequest = ArmedAtFrame(fresh, s);
        ExecUsdCacheView view = fresh.Compute(freshRequest);
        assert(PoseAt(view, kBlended) == PoseAt(view, kTurnSampled));
    }

    std::printf("execMotion blend: the fan-in arrives in the relationship's "
                "authored order, at first compile and after an edit\n");
}

// ---------------------------------------------------------------------------
// A source that is not there, and one that refused
// ---------------------------------------------------------------------------
// 26.08 drops both from a fan-in without a word -- the first while compiling
// (`exec/inputResolver.cpp` skips a target that does not provide the
// computation), the second while reading (`vdf/readIterator.h` skips an input
// that holds no value). A blend that paired weights with whatever came back
// would then weight the wrong clip; this one counts its targets a second time
// and refuses.
void TestADroppedSourceIsCountedNotBlendedAround(const std::string& fixture)
{
    // ---- a target that is not a clip ---------------------------------------
    {
        const Stage s = Open(fixture);
        s.stage->DefinePrim(SdfPath("/NotAClip"), TfToken("Xform"));
        SetSources(s, {SdfPath("/Walk"), SdfPath("/NotAClip"),
                       SdfPath("/Turn")});
        SetWeights(s, {0.25f, 0.5f, 0.75f});

        ExecUsdSystem system(s.stage);
        ExecUsdRequest request = ArmedAtFrame(system, s);

        TfErrorMark mark;
        ExecUsdCacheView view = system.Compute(request);
        AssertRefused(view, kBlended);
        assert(MarkNames(mark, "motion.blendPoses") &&
               MarkNames(mark, "/NotAClip") &&
               "a target that is not a clip was refused without naming the "
               "computation or the targets");
        // The sources themselves are untouched: the refusal is the blend's.
        assert(PoseAt(view, kWalkSampled).root.hasPosition);
        assert(PoseAt(view, kTurnSampled).root.hasPosition);
        mark.Clear();
    }

    // ---- a source whose sampler refused -------------------------------------
    // /Turn stops stating its rate, so `motion.sampleAnimation` refuses there
    // and sets no value. The fan-in then holds one pose where there are two
    // targets -- and without the count, the blend would be /Walk alone at a
    // weight of 0.25, which is /Walk: an answer nobody could tell from a blend.
    {
        const Stage s = Open(fixture);
        assert(s.turn.GetAttribute(kRate).Clear());

        ExecUsdSystem system(s.stage);
        ExecUsdRequest request = ArmedAtFrame(system, s);

        TfErrorMark mark;
        ExecUsdCacheView view = system.Compute(request);
        AssertRefused(view, kTurnSampled);
        AssertRefused(view, kBlended);
        assert(MarkNames(mark, "motion.blendPoses") &&
               "a source that refused was blended around, or refused silently");
        mark.Clear();
    }

    // ---- a target naming nothing on the stage --------------------------------
    // Invisible to both reads of the relationship, so it is not counted as a
    // source at all -- and the weights, authored one per *target*, then
    // disagree with the count. That is how it surfaces, and the only way.
    {
        const Stage s = Open(fixture);
        SetSources(s, {SdfPath("/Walk"), SdfPath("/Turn"),
                       SdfPath("/Missing")});
        SetWeights(s, {0.25f, 0.75f, 1.0f});

        ExecUsdSystem system(s.stage);
        ExecUsdRequest request = ArmedAtFrame(system, s);

        TfErrorMark mark;
        ExecUsdCacheView view = system.Compute(request);
        AssertRefused(view, kBlended);
        assert(MarkNames(mark, "states 3 'motion:blend:weights' for the 2") &&
               "a target naming nothing was counted as a source");
        mark.Clear();
    }

    std::printf("execMotion blend: a target that is not a clip, a source that "
                "refused and a target naming nothing are each refused rather "
                "than blended around\n");
}

// ---------------------------------------------------------------------------
// Weights
// ---------------------------------------------------------------------------
void TestWeightsAreStatedOnePerSource(const std::string& fixture)
{
    Stage s = Open(fixture);
    ExecUsdSystem system(s.stage);

    bool blendReportedByValue = false;
    ExecUsdRequest request = system.BuildRequest(
        KeysFor(s),
        [&](const ExecRequestIndexSet& indices, const EfTimeInterval&) {
            blendReportedByValue =
                blendReportedByValue || indices.count(kBlended) > 0;
        });
    assert(request.IsValid());
    system.Compute(request);
    system.ChangeTime(UsdTimeCode(kFrame));
    system.Compute(request);

    // ---- an authored weight reaches the blend ------------------------------
    blendReportedByValue = false;
    SetWeights(s, {0.75f, 0.25f});
    assert(blendReportedByValue &&
           "an authored weight change was not reported for the blend");
    {
        ExecUsdCacheView view = system.Compute(request);
        const motion::HumanoidPose blended = PoseAt(view, kBlended);
        assert(std::abs(HeadDegrees(blended)
                        - BlendedHead(kWalkHead, kTurnHead, 0.75f, 0.25f))
               < 1e-3f);
    }

    // ---- a negative weight is the library's zero -----------------------------
    // Documented by `motion::BlendPoses`, so an answer: /Turn alone.
    SetWeights(s, {-1.0f, 1.0f});
    {
        TfErrorMark mark;
        ExecUsdCacheView view = system.Compute(request);
        assert(PoseAt(view, kBlended) == PoseAt(view, kTurnSampled) &&
               "a negative weight was not treated as the library's zero");
        assert(mark.IsClean());
    }

    // ---- too few weights ----------------------------------------------------
    SetWeights(s, {1.0f});
    {
        TfErrorMark mark;
        ExecUsdCacheView view = system.Compute(request);
        AssertRefused(view, kBlended);
        assert(MarkNames(mark, "states 1 'motion:blend:weights' for the 2") &&
               "a weight count that does not pair with the targets was not "
               "refused with the counts");
        mark.Clear();
    }

    // ---- nothing weighted ---------------------------------------------------
    // The library's answer is a default pose stamped 0.0 -- a second nobody
    // sampled, a frame at which both clips were sampled at 1.0 s.
    SetWeights(s, {0.0f, -1.0f});
    {
        TfErrorMark mark;
        ExecUsdCacheView view = system.Compute(request);
        AssertRefused(view, kBlended);
        assert(MarkNames(mark, "no 'motion:blend:weights' entry is positive"));
        mark.Clear();
    }

    // ---- a weight that is not a number ---------------------------------------
    SetWeights(s, {std::numeric_limits<float>::quiet_NaN(), 1.0f});
    {
        TfErrorMark mark;
        ExecUsdCacheView view = system.Compute(request);
        AssertRefused(view, kBlended);
        assert(MarkNames(mark, "not finite"));
        mark.Clear();
    }

    // ---- no weights at all ----------------------------------------------------
    // Refused, not blended evenly. The callback cannot tell an absent array from
    // an authored empty one, so a default for the first would be a default for
    // the second -- a blend that stated no weights, blended anyway.
    assert(s.blend.RemoveProperty(kWeights));
    assert(!s.blend.GetAttribute(kWeights));
    {
        TfErrorMark mark;
        ExecUsdCacheView view = system.Compute(request);
        AssertRefused(view, kBlended);
        assert(MarkNames(mark, "states 0 'motion:blend:weights' for the 2") &&
               "a blend with no weights was blended anyway");
        mark.Clear();
    }

    std::printf("execMotion blend: weights pair one per source, a negative one "
                "is the library's zero, and too few, none positive, one not "
                "finite or none at all are refused\n");
}

// ---------------------------------------------------------------------------
// One instant
// ---------------------------------------------------------------------------
// Every source is sampled at the same frame, and each converts it to seconds
// at the rate it states. Two clips stating two rates are two clocks, and a
// blend between them would stamp a second neither sampled.
void TestSourcesMustShareAnInstant(const std::string& fixture)
{
    const Stage s = Open(fixture);
    assert(s.turn.GetAttribute(kRate).Set(100.0));

    ExecUsdSystem system(s.stage);
    ExecUsdRequest request = ArmedAtFrame(system, s);

    // Frame 50: /Walk says 1.0 s, /Turn says 0.5 s.
    {
        TfErrorMark mark;
        ExecUsdCacheView view = system.Compute(request);
        assert(PoseAt(view, kWalkSampled).timestamp == 1.0);
        assert(PoseAt(view, kTurnSampled).timestamp == 0.5);
        AssertRefused(view, kBlended);
        assert(MarkNames(mark, "one finite instant") &&
               "two clips at two rates were blended into a second neither "
               "sampled");
        mark.Clear();
    }

    // Frame 0 is second 0 at every rate, so there the two clocks agree and the
    // blend answers. The check is on the instant the sources state, not on the
    // rates behind it.
    system.ChangeTime(UsdTimeCode(0.0));
    {
        TfErrorMark mark;
        ExecUsdCacheView view = system.Compute(request);
        assert(PoseAt(view, kBlended).timestamp == 0.0);
        assert(mark.IsClean());
    }

    std::printf("execMotion blend: two clips counting one frame at two rates "
                "are refused where their seconds differ and blended where "
                "they agree\n");
}

// ---------------------------------------------------------------------------
// A pose a driver holds, handed to a source
// ---------------------------------------------------------------------------
// A blend reads what its sources' `motion.sampleAnimation` answers, and a value
// key can be overridden. So a driver that holds a pose -- a live source's, say
// -- hands it to a blend by overriding one source's sampled pose, and the
// override crosses the relationship to a node on another prim.
void TestAPoseHandedToASourceReachesTheBlend(const std::string& fixture)
{
    const Stage s = Open(fixture);
    ExecUsdSystem system(s.stage);
    ExecUsdRequest request = ArmedAtFrame(system, s);

    motion::HumanoidPose held;
    held.timestamp = kSecond;
    held.validRotations.set(kHead);
    held.localRotations[kHead] = GfQuatf(1.0f);
    held.root.worldPosition = GfVec3f(4.0f, 0.0f, 0.0f);
    held.root.hasPosition = true;

    // ---- stamped at the instant the others were sampled at ------------------
    {
        ExecUsdValueOverrideVector overrides;
        overrides.push_back(ExecUsdValueOverride{
            ExecUsdValueKey(s.turn, kSampleAnimation), VtValue(held)});
        ExecUsdCacheView view =
            system.ComputeWithOverrides(request, std::move(overrides));

        assert(PoseAt(view, kTurnSampled) == held);
        const motion::HumanoidPose blended = PoseAt(view, kBlended);
        assert(std::abs(HeadDegrees(blended)
                        - BlendedHead(kWalkHead, 0.0f, 0.25f, 0.75f)) < 1e-3f &&
               "the override of a source's key did not reach the blend");
        assert(NearlyEqual(blended.root.worldPosition,
                           BlendedHips(kWalkHips, held.root.worldPosition,
                                       0.25f, 0.75f),
                           1e-5));
    }

    // ---- stamped somewhere else ------------------------------------------------
    // A pose from a driver's own clock, a frame behind: refused like two clips
    // at two rates, because it is the same statement.
    {
        motion::HumanoidPose behind = held;
        behind.timestamp = kSecond - 0.02;
        ExecUsdValueOverrideVector overrides;
        overrides.push_back(ExecUsdValueOverride{
            ExecUsdValueKey(s.turn, kSampleAnimation), VtValue(behind)});

        TfErrorMark mark;
        ExecUsdCacheView view =
            system.ComputeWithOverrides(request, std::move(overrides));
        AssertRefused(view, kBlended);
        assert(MarkNames(mark, "one finite instant"));
        mark.Clear();
    }

    std::printf("execMotion blend: a pose handed to a source's key reaches the "
                "blend on another prim, and is refused if stamped elsewhere\n");
}

} // namespace

int main(int argc, char** argv)
{
    assert(argc == 2 && "usage: execMotion_blend <blended_clips.usda>");
    TestTheBlendIsTheLibrarysWeightedFold(argv[1]);
    TestTheFanInArrivesInAuthoredOrder(argv[1]);
    TestADroppedSourceIsCountedNotBlendedAround(argv[1]);
    TestWeightsAreStatedOnePerSource(argv[1]);
    TestSourcesMustShareAnInstant(argv[1]);
    TestAPoseHandedToASourceReachesTheBlend(argv[1]);
    std::printf("execMotion blend: all checks passed\n");
    return 0;
}
